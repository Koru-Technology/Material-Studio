
#include "../global.h"
#include <stdatomic.h>

// Material caches: a material's outputs rendered once into textures of the layers' size and format (base color and
// opacity, tangent normal and the emission / subsurface flag, occlusion, roughness, metallic and height), so that paint
// draws read a texel instead of evaluating the whole graph for every texel under the brush (a heavy Material Maker
// material costs hundreds of ms of GPU per draw on the iPad, its cache a few). make_paint.c reads the cache in place of
// parser_material_parse's outputs; everything after the material's outputs (brush mask, flow, blending, matid,
// channels) is the same.
//
// Fit: uv mapped painting (brush, eraser, fill) on a paint layer, scale 1, no decal, triplanar, udim or 2D view, and a
// material that reads nothing but its uv coordinates and constant textures (make_paint_cache_bake_run says why not).
// Anything else paints live, as before.
//
// Validity: a signature of everything the cache depends on: the material's graph (its groups and the materials it
// nests), its emission / subsurface / height switches and rotation, the layer's uv map, the mesh painted, the texture
// size and format, the project's images. A cache is read only while it matches; a graph edit, a resize, a new mesh...
// change it and the cache is built again. Memory only: nothing is written to disk, a new project frees every cache.
//
// Building: the current material's, once its Material Maker buffers are full (a cache never needs sharpening: strokes
// painted before it is ready paint live, util_stroke_refine.c sharpens them as before) and it stayed unchanged for a
// moment while no stroke is in progress. Its own shader (make_paint_cache_bake_run) is parsed then, translated and
// compiled on threads, then rendered in bands of rows (scissor), one band a frame, sized from the GPU time of the
// previous bands (MATERIAL_CACHE_BAND_MS each). Once complete, the paint shader is made again: it reads the cache. A
// material whose whole texture rendered in less than MATERIAL_CACHE_MIN_MS of GPU (env too) keeps no cache: painting it
// live costs about as little, the memory is worth more (the default material, plain colors).
//
// Budget: MATERIAL_CACHE_BUDGET_MB of textures (env MATERIAL_CACHE_MB), on iOS at most a sixth of what the system still
// lets the app use (the Material Maker buffers take up to a third); past it the least recently used caches go first,
// never the current material's. A memory warning frees all but the current material's. Env MATERIAL_CACHE=0 disables.

#define MATERIAL_CACHE_MAX       16
#define MATERIAL_CACHE_KEYS      64
#define MATERIAL_CACHE_BUDGET_MB 256
#define MATERIAL_CACHE_BAND_MS   20.0 // GPU ms a band aims at (rows cost unevenly: some take twice that)
#define MATERIAL_CACHE_SETTLE    0.3 // Seconds the material stays unchanged before its cache is parsed
#define MATERIAL_CACHE_MIN_MS    8.0 // GPU ms of the whole texture below which a cache is not kept (a cheap material)

enum {
	MATERIAL_CACHE_NEW,       // To parse
	MATERIAL_CACHE_TRANSLATE, // GLSL translating on a thread
	MATERIAL_CACHE_COMPILE,   // Pipeline compiling on a thread
	MATERIAL_CACHE_WAIT,      // Waits for full buffers
	MATERIAL_CACHE_BUILD,     // Bands rendering
	MATERIAL_CACHE_READY,
	MATERIAL_CACHE_UNFIT, // The material reads more than its uv coordinates, or its shader failed
};

typedef struct {
	slot_material_t  *material;
	u64               sig;
	u64               bake_sig; // The signature its shader was parsed with
	i32               state;
	gpu_texture_t    *tex[3]; // Base, normal, pack
	i32               w;
	i32               h;
	i32               format;
	u64               used; // Least recently used goes first
	bool              emis_used;
	bool              subs_used;
	bool              height_used;
	u64               keys[MATERIAL_CACHE_KEYS]; // Buffers its shader samples
	i32               nkeys;
	shader_context_t *bake;
	bool              bake_loaded;
	char             *glsl;
	char             *vs;
	char             *fs;
	iron_thread_t     thread;
	bool              thread_on;
	atomic_bool       thread_done;
	bool              thread_ok;
	i32               row;    // Rows rendered
	f32               gpu_ms; // GPU time of its bands
} material_cache_t;

static material_cache_t *material_cache_entries[MATERIAL_CACHE_MAX];
static i32               material_cache_count      = 0;
static bool              material_cache_registered = false;
static i32               material_cache_enabled    = -1;
static i32               material_cache_budget_mb  = 0; // material_cache_test_budget, 0: the default
static u64               material_cache_clock      = 0;
static u64               material_cache_mesh_gen   = 0;
static u64               material_cache_settle_sig = 0;
static double            material_cache_settle_t   = 0.0;
static i32               material_cache_stats[16];

// Bands: one in flight at a time, its GPU time comes back from gpu_commit
static atomic_int  material_cache_band_us     = -1;
static atomic_bool material_cache_band_flight = false;
static i32               material_cache_band_rows  = 0; // Rows of the band in flight
static material_cache_t *material_cache_band_entry = NULL; // Whose band it is
static f32               material_cache_min_ms     = -1.0f; // material_cache_test_min_ms, below 0: MATERIAL_CACHE_MIN_MS
static i32         material_cache_band_wait   = 0; // Frames it is in flight
static i32         material_cache_rows        = 0; // Rows of the next band
static f32         material_cache_row_peak    = 0.0; // Highest ms a row of the build so far

static void material_cache_update(void *_);

static bool material_cache_timing(void) {
	static int timing = -1;
	if (timing < 0) {
		timing = getenv("IRON_PAINT_TIMING") != NULL || getenv("IRON_SHADER_TIMING") != NULL;
	}
	return timing == 1;
}

static bool material_cache_on(void) {
	if (material_cache_enabled < 0) {
		char *e                = getenv("MATERIAL_CACHE");
		material_cache_enabled = e != NULL && e[0] == '0' ? 0 : 1;
	}
	if (material_cache_enabled && !material_cache_registered) {
		material_cache_registered = true;
		sys_notify_on_update(material_cache_update, NULL);
	}
	return material_cache_enabled == 1;
}

// Signature

static u64 material_cache_hash(u64 h, const void *data, size_t n) {
	const u8 *p = data;
	for (size_t i = 0; i < n; ++i) {
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

static u64 material_cache_hash_i32(u64 h, i32 v) {
	return material_cache_hash(h, &v, sizeof(v));
}

static u64 material_cache_hash_str(u64 h, char *s) {
	if (s == NULL) {
		return material_cache_hash_i32(h, -1);
	}
	size_t n = strlen(s);
	return material_cache_hash_i32(material_cache_hash(h, s, n), (i32)n);
}

static u64 material_cache_hash_f32s(u64 h, f32_array_t *a) {
	if (a == NULL) {
		return material_cache_hash_i32(h, -1);
	}
	return material_cache_hash_i32(material_cache_hash(h, a->buffer, sizeof(f32) * a->length), a->length);
}

// What a canvas evaluates to: nodes, their values and links, the groups and materials it opens (not where nodes are)
static u64 material_cache_hash_canvas(u64 h, ui_node_canvas_t *c, i32 depth) {
	if (c == NULL || depth > 8) {
		return material_cache_hash_i32(h, -1);
	}
	h = material_cache_hash_i32(h, c->nodes->length);
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		h            = material_cache_hash_i32(h, n->id);
		h            = material_cache_hash_str(h, n->type);
		h            = material_cache_hash_str(h, n->name);
		h            = material_cache_hash_i32(h, n->inputs->length);
		for (i32 j = 0; j < n->inputs->length; ++j) {
			h = material_cache_hash_f32s(h, n->inputs->buffer[j]->default_value);
		}
		h = material_cache_hash_i32(h, n->outputs->length);
		for (i32 j = 0; j < n->outputs->length; ++j) {
			h = material_cache_hash_f32s(h, n->outputs->buffer[j]->default_value);
		}
		h = material_cache_hash_i32(h, n->buttons->length);
		for (i32 j = 0; j < n->buttons->length; ++j) {
			ui_node_button_t *b = n->buttons->buffer[j];
			h                   = material_cache_hash_str(h, b->name);
			h                   = material_cache_hash_f32s(h, b->default_value);
			if (b->data != NULL) {
				h = material_cache_hash_i32(material_cache_hash(h, b->data->buffer, b->data->length), b->data->length);
			}
		}
		if (string_equals(n->type, "GROUP")) {
			h = material_cache_hash_canvas(h, parser_material_get_group(n->name), depth + 1);
		}
		else if (string_equals(n->type, "MATERIAL") && n->buttons->length > 0 && n->buttons->buffer[0]->default_value != NULL) {
			i32 mi = (i32)n->buttons->buffer[0]->default_value->buffer[0];
			h      = material_cache_hash_canvas(h, mi >= 0 && mi < g_project->_->materials->length ? g_project->_->materials->buffer[mi]->canvas : NULL, depth + 1);
		}
	}
	h = material_cache_hash_i32(h, c->links->length);
	for (i32 i = 0; i < c->links->length; ++i) {
		ui_node_link_t *l = c->links->buffer[i];
		i32             v[4] = {l->from_id, l->from_socket, l->to_id, l->to_socket};
		h                    = material_cache_hash(h, v, sizeof(v));
	}
	return h;
}

static u64 material_cache_signature(slot_material_t *m) {
	u64 h = material_cache_hash_canvas(1469598103934665603ull, m->canvas, 0);
	u8  s[3] = {m->paint_emis, m->paint_subs, m->paint_height};
	h        = material_cache_hash(h, s, sizeof(s));
	h        = material_cache_hash(h, &m->uv_angle, sizeof(f32));
	h        = material_cache_hash_i32(h, g_context->layer->uv_map);
	mesh_object_t *o = render_path_paint_target();
	void          *p[2] = {o, o != NULL ? o->data : NULL};
	h                   = material_cache_hash(h, p, sizeof(p));
	h                   = material_cache_hash(h, &material_cache_mesh_gen, sizeof(u64));
	i32 size[3]         = {config_get_texture_res_x(), config_get_texture_res_y(), (i32)g_context->layer->texpaint->format};
	h                   = material_cache_hash(h, size, sizeof(size));
	for (i32 i = 0; i < g_project->_->assets->length; ++i) { // Image nodes pick images by their place in the list
		h = material_cache_hash_str(h, g_project->_->assets->buffer[i]->file);
	}
	return h;
}

// The context paints the material at its uv coordinates on a paint layer the size of the project's textures, scale 1
static bool material_cache_context_ok(void) {
	if (!material_cache_on()) {
		return false;
	}
	tool_type_t tool = g_context->tool;
	if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_FILL) {
		return false;
	}
	slot_layer_t *l = g_context->layer;
	if (l == NULL || !slot_layer_is_layer(l) || slot_layer_is_path(l) || l->fill_material != NULL || l->texpaint == NULL) {
		return false;
	}
	if (g_context->brush_paint != UV_TYPE_UVMAP || context_is_decal() || g_context->paint2d || g_context->pick_pos_nor_tex || util_mesh_udim_layer(l)) {
		return false;
	}
	slot_material_t *m = g_context->material;
	if (m == NULL || m->canvas == NULL || m->uv_scale * g_context->brush_nodes_scale != 1.0f) {
		return false; // Other scales paint live (not cached for now)
	}
	if ((i32)l->texpaint->width != config_get_texture_res_x() || (i32)l->texpaint->height != config_get_texture_res_y()) {
		return false;
	}
	return g_context->paint_object != NULL && render_path_paint_target() != NULL;
}

// Entries

static material_cache_t *material_cache_find(slot_material_t *m) {
	for (i32 i = 0; i < material_cache_count; ++i) {
		if (material_cache_entries[i]->material == m) {
			return material_cache_entries[i];
		}
	}
	return NULL;
}

static i32 material_cache_texel_bytes(i32 format) {
	return format == GPU_TEXTURE_FORMAT_RGBA128 ? 16 : format == GPU_TEXTURE_FORMAT_RGBA64 ? 8 : 4;
}

static i64 material_cache_bytes(material_cache_t *e) {
	return e->tex[0] != NULL ? (i64)e->w * e->h * material_cache_texel_bytes(e->format) * 3 : 0;
}

static i64 material_cache_total(void) {
	i64 t = 0;
	for (i32 i = 0; i < material_cache_count; ++i) {
		t += material_cache_bytes(material_cache_entries[i]);
	}
	return t;
}

static i64 material_cache_budget(void) {
	const i64 mb = 1024 * 1024;
	i64       b  = MATERIAL_CACHE_BUDGET_MB;
	char     *e  = getenv("MATERIAL_CACHE_MB");
	if (material_cache_budget_mb > 0) {
		b = material_cache_budget_mb;
	}
	else if (e != NULL && atoi(e) > 0) {
		b = atoi(e);
	}
	b *= mb;
#ifdef IRON_IOS
	if (__builtin_available(iOS 13.0, *)) {
		i64 avail = (i64)os_proc_available_memory(); // 0 when unknown
		if (avail > 0 && b > avail / 6) {
			b = avail / 6;
		}
	}
#endif
	return b;
}

static void material_cache_free_textures(material_cache_t *e) {
	for (i32 i = 0; i < 3; ++i) {
		if (e->tex[i] != NULL) {
			gpu_delete_texture(e->tex[i]);
			e->tex[i] = NULL;
		}
	}
}

static void material_cache_free_bake(material_cache_t *e) {
	if (e->thread_on) {
		iron_thread_wait_and_destroy(&e->thread);
		e->thread_on = false;
	}
	if (e->bake != NULL) {
		if (e->bake_loaded) {
			make_material_delete_context(e->bake); // Next frame: a band may still use it
		}
		else {
			e->bake->shader_from_source = true;
			make_material_free_context(e->bake);
		}
		e->bake        = NULL;
		e->bake_loaded = false;
	}
	free(e->glsl);
	free(e->vs);
	free(e->fs);
	e->glsl = NULL;
	e->vs   = NULL;
	e->fs   = NULL;
}

static void material_cache_remove(i32 i) {
	material_cache_t *e = material_cache_entries[i];
	material_cache_free_bake(e);
	material_cache_free_textures(e);
	if (material_cache_band_entry == e) {
		material_cache_band_entry = NULL;
	}
	free(e);
	for (i32 j = i; j < material_cache_count - 1; ++j) {
		material_cache_entries[j] = material_cache_entries[j + 1];
	}
	material_cache_count--;
}

// The least recently used entry that may go (not the current material's, no thread at work), with textures or not
static i32 material_cache_lru(bool textured) {
	i32 best = -1;
	for (i32 i = 0; i < material_cache_count; ++i) {
		material_cache_t *e = material_cache_entries[i];
		if (e->material == g_context->material || e->thread_on || (textured && e->tex[0] == NULL)) {
			continue;
		}
		if (best < 0 || e->used < material_cache_entries[best]->used) {
			best = i;
		}
	}
	return best;
}

// Room for need more bytes within the budget: the least recently used caches go (none when that would not be enough)
static bool material_cache_fit(i64 need) {
	i64 budget = material_cache_budget();
	if (need > 0) {
		i64 kept = 0; // Caches that can not go
		for (i32 i = 0; i < material_cache_count; ++i) {
			material_cache_t *e = material_cache_entries[i];
			if (e->material == g_context->material || e->thread_on) {
				kept += material_cache_bytes(e);
			}
		}
		if (kept + need > budget) {
			return false;
		}
	}
	while (material_cache_total() + need > budget) {
		i32 i = material_cache_lru(true);
		if (i < 0) {
			return false;
		}
		if (material_cache_timing()) {
			iron_log("[matcache] evicted the cache of material %d (%.0f MB)", material_cache_entries[i]->material->id,
			         material_cache_bytes(material_cache_entries[i]) / (1024.0 * 1024.0));
		}
		material_cache_remove(i);
		material_cache_stats[4]++;
	}
	return true;
}

static material_cache_t *material_cache_new(slot_material_t *m, u64 sig) {
	if (material_cache_count == MATERIAL_CACHE_MAX) {
		i32 i = material_cache_lru(false);
		if (i < 0) {
			return NULL;
		}
		material_cache_remove(i);
	}
	material_cache_t *e = calloc(1, sizeof(material_cache_t));
	e->material         = m;
	e->sig              = sig;
	e->state            = MATERIAL_CACHE_NEW;
	atomic_init(&e->thread_done, false);
	material_cache_entries[material_cache_count++] = e;
	return e;
}

// Built again: its shader goes, its textures are kept for the new build
static void material_cache_restart(material_cache_t *e, u64 sig) {
	material_cache_free_bake(e);
	e->sig   = sig;
	e->state = MATERIAL_CACHE_NEW;
	e->row   = 0;
}

// The paint shader reads a cache
static bool material_cache_scon_reads(shader_context_t *scon) {
	for (i32 i = 0; scon != NULL && scon->texture_units != NULL && i < scon->texture_units->length; ++i) {
		char *link = scon->texture_units->buffer[i]->link;
		if (link != NULL && string_equals(link, "_matcache0")) {
			return true;
		}
	}
	return false;
}

// Paint shaders (make_paint.c): whether the material's outputs come from its cache, and the flags its parse would set
bool material_cache_paint_info(bool is_atlas, bool *emis_used, bool *subs_used, bool *height_used) {
	if (is_atlas || !material_cache_context_ok()) {
		return false;
	}
	material_cache_t *e = material_cache_find(g_context->material);
	if (e == NULL || e->state != MATERIAL_CACHE_READY || e->sig != material_cache_signature(e->material)) {
		return false;
	}
	*emis_used   = e->emis_used;
	*subs_used   = e->subs_used;
	*height_used = e->height_used;
	e->used      = ++material_cache_clock;
	return true;
}

gpu_texture_t *material_cache_texture(i32 i) {
	material_cache_t *e = material_cache_find(g_context->material);
	if (e != NULL && e->state == MATERIAL_CACHE_READY && i >= 0 && i < 3 && e->tex[i] != NULL) {
		return e->tex[i];
	}
	render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
	return rt->_image;
}

// Paint draws (render_path_paint_commands_paint): a paint shader reading a cache that no longer fits (scale, rotation,
// graph...) is made again, live
void material_cache_paint_check(void) {
	if (!material_cache_scon_reads(make_material_paint_scon)) {
		return;
	}
	bool e, s, h;
	if (!material_cache_paint_info(false, &e, &s, &h)) {
		if (material_cache_timing()) {
			iron_log("[matcache] the cache no longer fits: the paint shader is parsed again");
		}
		make_material_parse_paint_material(false);
	}
}

// Building

static void material_cache_translate_worker(void *param) {
	material_cache_t *e = param;
	e->thread_ok        = iron_glsl_translate(e->glsl, &e->vs, &e->fs);
	atomic_store(&e->thread_done, true);
}

static void material_cache_compile_worker(void *param) {
	material_cache_t *e = param;
	gpu_pipeline_compile(e->bake->_->pipe);
	atomic_store(&e->thread_done, true);
}

static void material_cache_thread(material_cache_t *e, void (*f)(void *)) {
	atomic_store(&e->thread_done, false);
	e->thread_on = true;
	iron_thread_init(&e->thread, f, e);
}

// True once the thread at work ended (joined)
static bool material_cache_thread_ended(material_cache_t *e) {
	if (e->thread_on && !atomic_load(&e->thread_done)) {
		return false;
	}
	if (e->thread_on) {
		iron_thread_wait_and_destroy(&e->thread);
		e->thread_on = false;
	}
	return true;
}

static char *material_cache_format_name(i32 format) {
	return format == GPU_TEXTURE_FORMAT_RGBA128 ? "RGBA128" : format == GPU_TEXTURE_FORMAT_RGBA64 ? "RGBA64" : "RGBA32";
}

static void material_cache_unfit(material_cache_t *e, char *why) {
	if (material_cache_timing()) {
		iron_log("[matcache] material %d paints live: %s", e->material->id, why);
	}
	material_cache_free_bake(e);
	material_cache_free_textures(e);
	e->state = MATERIAL_CACHE_UNFIT;
}

// The material's cache shader parsed (main thread, a heavy material's parse is a long frame), translated on a thread
static void material_cache_parse(material_cache_t *e) {
	double      t0     = iron_time();
	i32         format = g_context->layer->texpaint->format;
	material_t *sdata  = ALLOC_INIT(material_t, {.name = "Material", .canvas = e->material->canvas});
	char       *glsl   = NULL;
	char       *why    = NULL;
	node_shader_context_t *con = make_paint_cache_bake_run(sdata, material_cache_format_name(format), &e->emis_used, &e->subs_used, &e->height_used, &glsl, &why);
	e->bake                    = con->data;
	e->bake_loaded             = false;
	node_shader_context_free(con);
	free(sdata);
	double ms = (iron_time() - t0) * 1000.0;
	material_cache_stats[8] = (i32)(ms * 1000.0);
	if (material_cache_timing()) {
		iron_log("[matcache] material %d: cache shader parsed in %.1f ms", e->material->id, ms);
	}
	if (why != NULL) {
		material_cache_unfit(e, why);
		return;
	}
	e->glsl     = glsl;
	e->bake_sig = e->sig;
	e->format   = format;
	e->nkeys    = mm_buffers_paint_keys(e->bake, e->keys, MATERIAL_CACHE_KEYS);
	e->state    = MATERIAL_CACHE_TRANSLATE;
	material_cache_thread(e, material_cache_translate_worker);
}

// Translated: its shaders made, its pipeline compiled on a thread
static void material_cache_load(material_cache_t *e) {
	if (!e->thread_ok || e->vs == NULL || e->fs == NULL) {
		material_cache_unfit(e, "its shader did not translate");
		return;
	}
	shader_context_t *s        = e->bake;
	s->shader_from_source      = true;
	s->vertex_shader           = e->vs;
	s->fragment_shader         = e->fs;
	s->_->vertex_shader_size   = 0;
	s->_->fragment_shader_size = 0;
	shader_compile_batch_begin();
	shader_context_load(s);
	i32 count = 0;
	shader_compile_batch_take(&count);
	e->bake_loaded = true;
	if (count != 1 || s->_->pipe == NULL || s->_->pipe->vertex_shader == NULL || s->_->pipe->fragment_shader == NULL) {
		material_cache_unfit(e, "its shader did not load");
		return;
	}
	e->state = MATERIAL_CACHE_COMPILE;
	material_cache_thread(e, material_cache_compile_worker);
}

static void material_cache_band_done(void *data, double gpu_ms) {
	atomic_store(&material_cache_band_us, gpu_ms < 0.0 ? 0 : (i32)(gpu_ms * 1000.0));
	atomic_store(&material_cache_band_flight, false);
}

// The GPU time of the last band sizes the next one
static void material_cache_band_poll(void) {
	if (!atomic_load(&material_cache_band_flight)) {
		i32 us = atomic_exchange(&material_cache_band_us, -1);
		if (us >= 0 && material_cache_band_rows > 0) {
			f32 ms      = us / 1000.0f;
			f32 per_row = ms / material_cache_band_rows;
			if (per_row > material_cache_row_peak) {
				material_cache_row_peak = per_row;
			}
			f32 est  = per_row > material_cache_row_peak * 0.75f ? per_row : material_cache_row_peak * 0.75f; // Dense rows may come next
			i32 rows = est > 0.0f ? (i32)(MATERIAL_CACHE_BAND_MS / est) : material_cache_band_rows * 2;
			rows     = rows > material_cache_band_rows * 2 ? material_cache_band_rows * 2 : rows;
			material_cache_rows = rows < 8 ? 8 : rows;
			if (us > material_cache_stats[5]) {
				material_cache_stats[5] = us;
			}
			if (material_cache_band_entry != NULL) {
				material_cache_band_entry->gpu_ms += ms;
			}
			if (material_cache_timing()) {
				iron_log("[matcache] band of %d rows: gpu %.2f ms", material_cache_band_rows, ms);
			}
			material_cache_band_rows = 0;
		}
		return;
	}
	if (++material_cache_band_wait > 60) { // Its time is not coming: on with the size it had
		atomic_store(&material_cache_band_flight, false);
		material_cache_band_rows = 0;
	}
}

// A band of rows of the cache rendered: the mesh drawn at its uv coordinates, scissored
static void material_cache_band(material_cache_t *e) {
	i32 rows = material_cache_rows;
	if (e->row + rows > e->h) {
		rows = e->h - e->row;
	}
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	bool           drawing = gpu_in_use;
	gpu_texture_t *target  = _draw_current;
	if (drawing) {
		draw_end();
	}
	gpu_commit(NULL, NULL); // The work so far apart: the band's GPU time is its own
	gpu_begin(e->tex, 3, NULL, e->row == 0 ? GPU_CLEAR_COLOR : GPU_CLEAR_NONE, 0x00000000, 0.0f);
	gpu_scissor(0, e->row, e->w, rows);
	_mesh_object_last_pipeline = NULL;
	i32 need                   = mm_buffers_require(0);
	mesh_object_draw(render_path_paint_target(), e->bake, NULL);
	mm_buffers_require(need);
	gpu_disable_scissor();
	gpu_end();
	_mesh_object_last_pipeline = NULL;
	material_cache_band_rows   = rows;
	material_cache_band_entry  = e;
	material_cache_band_wait   = 0;
	atomic_store(&material_cache_band_us, -1);
	atomic_store(&material_cache_band_flight, true);
	gpu_commit(material_cache_band_done, NULL);
	if (drawing) {
		draw_begin(target, false, 0);
	}
	e->row += rows;
	material_cache_stats[11]++;
}

// Buffers the cache shader samples are full (none: full)
static bool material_cache_full(u64 *keys, i32 n) {
	return n == 0 || mm_buffers_links_tier(keys, n) >= MM_BUFFERS_FULL;
}

static void material_cache_build_start(material_cache_t *e) {
	i32 w = config_get_texture_res_x();
	i32 h = config_get_texture_res_y();
	if (e->tex[0] == NULL || e->w != w || e->h != h || (i32)e->tex[0]->format != e->format) {
		material_cache_free_textures(e);
		e->w = w;
		e->h = h;
		if (!material_cache_fit((i64)w * h * material_cache_texel_bytes(e->format) * 3)) {
			return; // Over the budget: waits, the budget may grow
		}
		for (i32 i = 0; i < 3; ++i) {
			e->tex[i] = gpu_create_render_target(w, h, e->format);
		}
	}
	e->row                  = 0;
	e->gpu_ms               = 0.0f;
	e->state                = MATERIAL_CACHE_BUILD;
	material_cache_rows     = h / 32 < 8 ? 8 : h / 32;
	material_cache_row_peak = 0.0f;
	material_cache_stats[5] = 0;
	material_cache_stats[11] = 0;
}

static f32 material_cache_cheap_ms(void) {
	if (material_cache_min_ms >= 0.0f) {
		return material_cache_min_ms;
	}
	char *e = getenv("MATERIAL_CACHE_MIN_MS");
	return e != NULL ? (f32)atof(e) : MATERIAL_CACHE_MIN_MS;
}

// Every band rendered and timed: kept, unless the whole material costs so little that its cache would only take memory
static void material_cache_ready(material_cache_t *e) {
	material_cache_stats[10] = (i32)(e->gpu_ms * 1000.0f);
	if (e->gpu_ms < material_cache_cheap_ms()) {
		material_cache_unfit(e, string_tmp("cheap, %.1f ms of GPU for the whole texture", e->gpu_ms));
		return;
	}
	e->state = MATERIAL_CACHE_READY;
	material_cache_free_bake(e);
	material_cache_stats[3]++;
	if (material_cache_timing()) {
		iron_log("[matcache] material %d cached: %d bands, longest %.2f ms, %.1f ms in all, %.0f MB in %d caches", e->material->id, material_cache_stats[11],
		         material_cache_stats[5] / 1000.0, e->gpu_ms, material_cache_total() / (1024.0 * 1024.0), material_cache_count);
	}
}

// Each frame: the current material's cache moves on, a paint shader that could read it is made again
static void material_cache_update(void *_) {
	if (g_project == NULL || g_context == NULL || g_project->_->materials == NULL) {
		return;
	}
	material_cache_band_poll();
	for (i32 i = 0; i < material_cache_count; ++i) { // Gone materials
		material_cache_t *e = material_cache_entries[i];
		if (!e->thread_on && array_index_of(g_project->_->materials, e->material) < 0) {
			material_cache_remove(i--);
		}
	}
	material_cache_fit(0); // The budget can shrink (os_proc_available_memory)
	if (!material_cache_context_ok() || make_material_paint_pending()) { // On once the paint shader of a material just picked is in use
		return;
	}
	slot_material_t  *m   = g_context->material;
	u64               sig = material_cache_signature(m);
	material_cache_t *e   = material_cache_find(m);
	if (sig != material_cache_settle_sig) {
		material_cache_settle_sig = sig;
		material_cache_settle_t   = iron_time();
	}
	if (e == NULL) {
		e = material_cache_new(m, sig);
		if (e == NULL) {
			return;
		}
	}
	e->used = ++material_cache_clock;
	if (e->sig != sig) {
		if (e->thread_on && !material_cache_thread_ended(e)) {
			return; // Its thread ends first
		}
		material_cache_restart(e, sig);
	}

	if (e->state == MATERIAL_CACHE_NEW) {
		if (g_context->brush_time > 0 || iron_time() - material_cache_settle_t < MATERIAL_CACHE_SETTLE || stroke_refine_stat(0) > 0 ||
		    stroke_refine_stat(1) > 0) {
			return;
		}
		u64 keys[MATERIAL_CACHE_KEYS];
		i32 n    = mm_buffers_paint_keys(make_material_paint_scon, keys, MATERIAL_CACHE_KEYS); // The live paint shader's
		i32 tier = n > 0 ? mm_buffers_links_tier(keys, n) : MM_BUFFERS_FULL;
		if (tier >= 0 && tier < MM_BUFFERS_FULL) {
			return; // Parsed once its buffers are full
		}
		material_cache_parse(e);
	}
	else if (e->state == MATERIAL_CACHE_TRANSLATE) {
		if (material_cache_thread_ended(e)) {
			material_cache_load(e);
		}
	}
	else if (e->state == MATERIAL_CACHE_COMPILE) {
		if (material_cache_thread_ended(e)) {
			shader_context_bind(e->bake);
			free(e->vs);
			free(e->fs);
			e->vs                    = NULL;
			e->fs                    = NULL;
			e->bake->vertex_shader   = NULL;
			e->bake->fragment_shader = NULL;
			if (e->bake->_->pipe->impl.pipeline == NULL) {
				material_cache_unfit(e, "its pipeline did not compile");
			}
			else {
				e->state = MATERIAL_CACHE_WAIT;
			}
		}
	}
	if (e->state == MATERIAL_CACHE_WAIT && material_cache_full(e->keys, e->nkeys)) {
		material_cache_build_start(e);
	}
	if (e->state == MATERIAL_CACHE_BUILD) {
		if (atomic_load(&material_cache_band_flight)) {
			return; // One band at a time
		}
		if (e->row >= e->h) {
			material_cache_ready(e); // The last band's time is in
		}
		else if (material_cache_full(e->keys, e->nkeys)) { // Else buffers were evicted meanwhile: on once they are back
			material_cache_band(e);
		}
	}
	// Brush, eraser and fill paint shaders always take the material's outputs from make_paint.c's branch that reads the
	// cache when it fits: made again, the shader reads it
	if (e->state == MATERIAL_CACHE_READY && make_material_paint_scon != NULL && !material_cache_scon_reads(make_material_paint_scon)) {
		bool a, b, c;
		if (material_cache_paint_info(false, &a, &b, &c)) {
			make_material_parse_paint_material_async(false); // Compiles on a thread, used once ready
		}
	}
}

// A new project, a resize: every cache goes
void material_cache_reset(void) {
	while (material_cache_count > 0) {
		material_cache_remove(material_cache_count - 1);
	}
	material_cache_settle_sig = 0;
}

// The mesh painted changed (import, merge, unwrap): caches made for the old one no longer match
void material_cache_mesh_changed(void) {
	material_cache_mesh_gen++;
}

// iOS memory warning: every cache but the current material's goes
void material_cache_trim(void) {
	for (i32 i = material_cache_count - 1; i >= 0; --i) {
		material_cache_t *e = material_cache_entries[i];
		if (e->material != g_context->material && !e->thread_on) {
			material_cache_remove(i);
		}
	}
}

// Tests: off, paint shaders evaluate materials live (reading a cache, the next paint draw makes the shader again)
void material_cache_set_enabled(i32 on) {
	material_cache_on(); // Registered
	material_cache_enabled = on ? 1 : 0;
}

// Tests: a texture budget in MB, 0 restores the default
void material_cache_test_budget(i32 mb) {
	material_cache_budget_mb = mb;
}

// Tests: materials whose whole texture renders in less GPU time get no cache, below 0 restores the default
void material_cache_test_min_ms(f32 ms) {
	material_cache_min_ms = ms;
}

// Tests: 0 caches, 1 their texture memory in KB, 2 the current material's state (0 none, 1 parsing or compiling or
// waiting for its buffers, 2 building, 3 ready, 4 unfit), 3 builds done, 4 caches evicted, 5 the longest band of the
// last build in microseconds of GPU time, 6 the budget in KB, 7 1 when the paint shader reads a cache, 8 the last cache
// shader's parse in microseconds, 9 ready caches, 10 the GPU time of the last build in microseconds, 11 its bands
i32 material_cache_stat(i32 what) {
	material_cache_on();
	if (what == 0) {
		return material_cache_count;
	}
	if (what == 1) {
		return (i32)(material_cache_total() / 1024);
	}
	if (what == 2) {
		material_cache_t *e = g_context->material != NULL ? material_cache_find(g_context->material) : NULL;
		if (e == NULL) {
			return 0;
		}
		return e->state == MATERIAL_CACHE_READY ? 3 : e->state == MATERIAL_CACHE_UNFIT ? 4 : e->state == MATERIAL_CACHE_BUILD ? 2 : 1;
	}
	if (what == 6) {
		return (i32)(material_cache_budget() / 1024);
	}
	if (what == 7) {
		return material_cache_scon_reads(make_material_paint_scon) ? 1 : 0;
	}
	if (what == 9) {
		i32 n = 0;
		for (i32 i = 0; i < material_cache_count; ++i) {
			n += material_cache_entries[i]->state == MATERIAL_CACHE_READY;
		}
		return n;
	}
	return what >= 0 && what < 16 ? material_cache_stats[what] : 0;
}
