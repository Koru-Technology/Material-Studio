
#include "../global.h"
#include <stdatomic.h>
#include <unistd.h>
#include "util_hash.h"

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

// Replay caches, see below
struct material_cache_replay {
	hash128_t         snap;   // Material snapshot
	i32               uv_map; // The layer's uv map
	u64               mesh;   // The mesh painted
	i32               w;      // The replay's target, texels and part of the layer
	i32               h;
	f32               rect[4];
	i32               state;
	gpu_texture_t    *tex[3];
	make_material_async_t *bake; // Its shader, translated and compiled on a thread
	f64               bake_t0;
	bool              bake_ready;
	bool              emis_used;
	bool              subs_used;
	bool              height_used;
	u64              *done; // Blocks of MATERIAL_CACHE_REPLAY_BLOCK^2 texels rendered, bw x bh of them
	i32               bw;
	i32               bh;
	i32               ndone;
	bool              cleared;
	i64               band_px; // Texels of the next band
	f32               px_peak; // Highest ms a texel so far
	f32               gpu_ms;
	double            used;
};

#define MATERIAL_CACHE_REPLAY_MAX   8 // Entries, the unfit ones among them (no textures)
#define MATERIAL_CACHE_REPLAY_BLOCK 64

static material_cache_replay_t *material_cache_replays[MATERIAL_CACHE_REPLAY_MAX];
static i32                      material_cache_replay_count = 0;
static material_cache_replay_t *material_cache_replay_bound = NULL;
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
static void material_cache_replay_remove(i32 i);

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

static u64 material_cache_hash_f32s(u64 h, f32_array_t *a) {
	if (a == NULL) {
		return hash_fnv1a_i32(h, -1);
	}
	return hash_fnv1a_i32(hash_fnv1a(h, a->buffer, sizeof(f32) * a->length), a->length);
}

// What a canvas evaluates to: nodes, their values and links, the groups and materials it opens (not where nodes are)
static u64 material_cache_hash_canvas(u64 h, ui_node_canvas_t *c, i32 depth) {
	if (c == NULL || depth > 8) {
		return hash_fnv1a_i32(h, -1);
	}
	h = hash_fnv1a_i32(h, c->nodes->length);
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		h            = hash_fnv1a_i32(h, n->id);
		h            = hash_fnv1a_str(h, n->type);
		h            = hash_fnv1a_str(h, n->name);
		h            = hash_fnv1a_i32(h, n->inputs->length);
		for (i32 j = 0; j < n->inputs->length; ++j) {
			h = material_cache_hash_f32s(h, n->inputs->buffer[j]->default_value);
		}
		h = hash_fnv1a_i32(h, n->outputs->length);
		for (i32 j = 0; j < n->outputs->length; ++j) {
			h = material_cache_hash_f32s(h, n->outputs->buffer[j]->default_value);
		}
		h = hash_fnv1a_i32(h, n->buttons->length);
		for (i32 j = 0; j < n->buttons->length; ++j) {
			ui_node_button_t *b = n->buttons->buffer[j];
			h                   = hash_fnv1a_str(h, b->name);
			h                   = material_cache_hash_f32s(h, b->default_value);
			if (b->data != NULL) {
				h = hash_fnv1a_i32(hash_fnv1a(h, b->data->buffer, b->data->length), b->data->length);
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
	h = hash_fnv1a_i32(h, c->links->length);
	for (i32 i = 0; i < c->links->length; ++i) {
		ui_node_link_t *l = c->links->buffer[i];
		i32             v[4] = {l->from_id, l->from_socket, l->to_id, l->to_socket};
		h                    = hash_fnv1a(h, v, sizeof(v));
	}
	return h;
}

static u64 material_cache_signature(slot_material_t *m) {
	u64 h = material_cache_hash_canvas(1469598103934665603ull, m->canvas, 0);
	u8  s[3] = {m->paint_emis, m->paint_subs, m->paint_height};
	h        = hash_fnv1a(h, s, sizeof(s));
	h        = hash_fnv1a(h, &m->uv_angle, sizeof(f32));
	h        = hash_fnv1a_i32(h, g_context->layer->uv_map);
	mesh_object_t *o = render_path_paint_target();
	void          *p[2] = {o, o != NULL ? o->data : NULL};
	h                   = hash_fnv1a(h, p, sizeof(p));
	h                   = hash_fnv1a(h, &material_cache_mesh_gen, sizeof(u64));
	i32 size[3]         = {config_get_texture_res_x(), config_get_texture_res_y(), (i32)g_context->layer->texpaint->format};
	h                   = hash_fnv1a(h, size, sizeof(size));
	for (i32 i = 0; i < g_project->_->assets->length; ++i) { // Image nodes pick images by their place in the list
		h = hash_fnv1a_str(h, g_project->_->assets->buffer[i]->file);
	}
	return h;
}

// The context paints the material at its uv coordinates on a paint layer, scale 1, no decal, triplanar, udim or 2D view
static bool material_cache_context_fits(void) {
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
	return g_context->paint_object != NULL && render_path_paint_target() != NULL;
}

// And the layer is the size of the project's textures
static bool material_cache_context_ok(void) {
	if (!material_cache_on() || !material_cache_context_fits()) {
		return false;
	}
	slot_layer_t *l = g_context->layer;
	return (i32)l->texpaint->width == config_get_texture_res_x() && (i32)l->texpaint->height == config_get_texture_res_y();
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


// Paint shaders (make_paint.c): whether the material's outputs come from its cache, and the flags its parse would set.
// Replaying, a replay cache bound answers; a replay at another size without one parses the material.
bool material_cache_paint_info(bool is_atlas, bool *emis_used, bool *subs_used, bool *height_used) {
	if (is_atlas) {
		return false;
	}
	material_cache_replay_t *r = material_cache_replay_bound;
	if (r != NULL) {
		*emis_used   = r->emis_used;
		*subs_used   = r->subs_used;
		*height_used = r->height_used;
		return true;
	}
	if (paint_replay_uncached || !material_cache_context_ok()) {
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
	material_cache_replay_t *r = material_cache_replay_bound;
	if (r != NULL) {
		return i >= 0 && i < 3 && r->tex[i] != NULL ? r->tex[i] : render_path_empty_black();
	}
	material_cache_t *e = material_cache_find(g_context->material);
	if (e != NULL && e->state == MATERIAL_CACHE_READY && i >= 0 && i < 3 && e->tex[i] != NULL) {
		return e->tex[i];
	}
	return render_path_empty_black();
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
	node_shader_context_t *con = make_paint_cache_bake_run(sdata, gpu_texture_format_name(format), &e->emis_used, &e->subs_used, &e->height_used, &glsl, &why);
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
	draw_saved_t saved_draw = draw_suspend();
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
	draw_resume(saved_draw);
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
		if (g_context->brush_time > 0 || iron_time() - material_cache_settle_t < MATERIAL_CACHE_SETTLE || paint_refine_stat(0) > 0 ||
		    stroke_refine_stat(1) > 0) { // Strokes and fill layers sharpen first
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

// A cache translating, compiling or rendering its bands: work spread over frames that other work waits behind
bool material_cache_building(void) {
	for (i32 i = 0; i < material_cache_count; ++i) {
		i32 st = material_cache_entries[i]->state;
		if (st == MATERIAL_CACHE_TRANSLATE || st == MATERIAL_CACHE_COMPILE || st == MATERIAL_CACHE_BUILD) {
			return true;
		}
	}
	return false;
}

// A new project, a resize: every cache goes
void material_cache_reset(void) {
	while (material_cache_count > 0) {
		material_cache_remove(material_cache_count - 1);
	}
	material_cache_replay_bound = NULL;
	while (material_cache_replay_count > 0) {
		material_cache_replay_remove(material_cache_replay_count - 1);
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

// Replay caches (paint_replay.c, composite-fast): a material snapshot's outputs for the target of a replay, its strokes
// read a texel instead of evaluating the material for every dab (stroke refine, replays at another size, export tiles).
// One per snapshot, target size and uv_rect, rendered from the snapshot's material at the full tier into RGBA128 with
// the paint draws' vertex transform (_paint_uv_tile): the floats a draw evaluating the material computes, the strokes
// paint the same bits. Rendered where the strokes replayed write (their uv bounds, in blocks), in bands sized by their GPU
// time (MATERIAL_CACHE_BAND_MS): a band a frame (paint_refine.c), or all of them at once with a commit between bands
// (export). Fit: as material_cache_context_ok
// at any size, and the bake's own reasons (make_paint_cache_bake_run), remembered for the snapshot. Transient: at most
// MATERIAL_CACHE_REPLAY_MB, the least recently used go, all of them after MATERIAL_CACHE_REPLAY_IDLE seconds unused.

#define MATERIAL_CACHE_REPLAY_IDLE 2.0
#ifdef IRON_IOS
#define MATERIAL_CACHE_REPLAY_MB 256
#else
#define MATERIAL_CACHE_REPLAY_MB 1024
#endif

static i32                      material_cache_replay_on    = -1; // material_cache_replay_set_enabled, env MATERIAL_CACHE_REPLAY=0
static bool                     material_cache_replay_hooked = false;
static i32                      material_cache_replay_stats[8];

// Its bands: one in flight at a time
static atomic_int               material_cache_replay_us     = -1;
static atomic_bool              material_cache_replay_flight = false;
static i32                      material_cache_replay_band   = 0; // Rows of the band in flight
static material_cache_replay_t *material_cache_replay_owner  = NULL; // Whose band it is
static i32                      material_cache_replay_wait   = 0;

static i64 material_cache_replay_bytes(material_cache_replay_t *e) {
	return e->tex[0] != NULL ? (i64)e->w * e->h * 16 * 3 : 0;
}

static i64 material_cache_replay_total(void) {
	i64 t = 0;
	for (i32 i = 0; i < material_cache_replay_count; ++i) {
		t += material_cache_replay_bytes(material_cache_replays[i]);
	}
	return t;
}

static void material_cache_replay_free_bake(material_cache_replay_t *e) {
	make_material_async_drop(e->bake); // Its context deleted next frame (its band may be in flight), once its thread ended
	e->bake       = NULL;
	e->bake_ready = false;
}

static void material_cache_replay_remove(i32 i) {
	material_cache_replay_t *e = material_cache_replays[i];
	if (material_cache_replay_bound == e) {
		material_cache_replay_bound = NULL;
	}
	if (material_cache_replay_owner == e) {
		material_cache_replay_owner = NULL;
	}
	material_cache_replay_free_bake(e);
	for (i32 k = 0; k < 3; ++k) {
		if (e->tex[k] != NULL) {
			gpu_delete_texture(e->tex[k]);
		}
	}
	free(e->done);
	free(e);
	material_cache_replays[i] = material_cache_replays[--material_cache_replay_count];
}

// Every replay cache goes (an export or a refine job over, a new project); the unfit ones stay known
void material_cache_replay_release(void) {
	for (i32 i = material_cache_replay_count - 1; i >= 0; --i) {
		if (material_cache_replays[i]->state != MATERIAL_CACHE_UNFIT && material_cache_replays[i] != material_cache_replay_bound) {
			material_cache_replay_remove(i);
		}
	}
}

static void material_cache_replay_idle(void *_) {
	double now = iron_time();
	for (i32 i = material_cache_replay_count - 1; i >= 0; --i) {
		material_cache_replay_t *e = material_cache_replays[i];
		if (e->state != MATERIAL_CACHE_UNFIT && e != material_cache_replay_bound && now - e->used > MATERIAL_CACHE_REPLAY_IDLE) {
			material_cache_replay_remove(i);
		}
	}
}

static bool material_cache_replay_enabled(void) {
	if (material_cache_replay_on < 0) {
		char *e                  = getenv("MATERIAL_CACHE_REPLAY");
		material_cache_replay_on = e != NULL && e[0] == '0' ? 0 : 1;
	}
	return material_cache_replay_on == 1;
}

static u64 material_cache_replay_mesh(void) {
	mesh_object_t *o    = render_path_paint_target();
	void          *p[2] = {o, o != NULL ? o->data : NULL};
	return hash_fnv1a(hash_fnv1a(1469598103934665603ull, p, sizeof(p)), &material_cache_mesh_gen, sizeof(u64));
}

// The replay cache of snapshot snap for target t, made when there is none (not rendered yet). NULL when the context
// replayed (g_context, a state of the stroke applied) does not read its material from a cache, when the snapshot's
// material is unfit, or when its textures do not fit the budget.
material_cache_replay_t *material_cache_replay_get(hash128_t snap, i32 w, i32 h, const f32 *uv_rect) {
	if (!material_cache_replay_enabled() || !material_cache_context_fits()) {
		return NULL;
	}
	if (!material_cache_replay_hooked) {
		material_cache_replay_hooked = true;
		sys_notify_on_update(material_cache_replay_idle, NULL);
	}
	i32 uv_map = g_context->layer->uv_map;
	u64 mesh   = material_cache_replay_mesh();
	for (i32 i = 0; i < material_cache_replay_count; ++i) {
		material_cache_replay_t *e = material_cache_replays[i];
		if (!hash128_eq(e->snap, snap) || e->uv_map != uv_map || e->mesh != mesh) {
			continue;
		}
		if (e->state == MATERIAL_CACHE_UNFIT) {
			return NULL;
		}
		if (e->w == w && e->h == h && memcmp(e->rect, uv_rect, sizeof(e->rect)) == 0) {
			e->used = iron_time();
			return e;
		}
	}
	i64 need   = (i64)w * h * 16 * 3;
	i64 budget = (i64)MATERIAL_CACHE_REPLAY_MB * 1024 * 1024;
#ifdef IRON_IOS
	if (__builtin_available(iOS 13.0, *)) {
		i64 avail = (i64)os_proc_available_memory();
		if (avail > 0 && budget > avail / 4) {
			budget = avail / 4;
		}
	}
#endif
	if (need > budget) {
		material_cache_replay_stats[3]++;
		return NULL;
	}
	while (material_cache_replay_total() + need > budget || material_cache_replay_count == MATERIAL_CACHE_REPLAY_MAX) {
		i32 lru = -1;
		for (i32 i = 0; i < material_cache_replay_count; ++i) {
			material_cache_replay_t *e = material_cache_replays[i];
			if (e != material_cache_replay_bound && (material_cache_replay_count == MATERIAL_CACHE_REPLAY_MAX || e->tex[0] != NULL) &&
			    (lru < 0 || e->used < material_cache_replays[lru]->used)) {
				lru = i;
			}
		}
		if (lru < 0) {
			return NULL;
		}
		material_cache_replay_remove(lru);
	}
	material_cache_replay_t *e = calloc(1, sizeof(material_cache_replay_t));
	e->snap                    = snap;
	e->uv_map                  = uv_map;
	e->mesh                    = mesh;
	e->w                       = w;
	e->h                       = h;
	memcpy(e->rect, uv_rect, sizeof(e->rect));
	e->state                                             = MATERIAL_CACHE_NEW;
	e->used                                              = iron_time();
	material_cache_replays[material_cache_replay_count++] = e;
	return e;
}

static void material_cache_replay_unfit(material_cache_replay_t *e, char *why) {
	iron_log("[matcache] replays of material snapshot %016llx evaluate it: %s", (unsigned long long)e->snap.lo, why);
	material_cache_replay_free_bake(e);
	for (i32 k = 0; k < 3; ++k) {
		if (e->tex[k] != NULL) {
			gpu_delete_texture(e->tex[k]);
			e->tex[k] = NULL;
		}
	}
	e->state = MATERIAL_CACHE_UNFIT;
	material_cache_replay_stats[2]++;
}

static void material_cache_replay_start(material_cache_replay_t *e) {
	for (i32 k = 0; k < 3; ++k) {
		e->tex[k] = gpu_create_render_target(e->w, e->h, GPU_TEXTURE_FORMAT_RGBA128);
	}
	e->state   = MATERIAL_CACHE_BUILD;
	e->bw      = (e->w + MATERIAL_CACHE_REPLAY_BLOCK - 1) / MATERIAL_CACHE_REPLAY_BLOCK;
	e->bh      = (e->h + MATERIAL_CACHE_REPLAY_BLOCK - 1) / MATERIAL_CACHE_REPLAY_BLOCK;
	e->done    = calloc((e->bw * e->bh + 63) / 64, sizeof(u64));
	e->ndone   = 0;
	e->cleared = false;
	e->band_px = (i64)e->w * (e->h / 32 < 64 ? 64 : e->h / 32);
	e->px_peak = 0.0f;
	e->gpu_ms  = 0.0f;
	material_cache_replay_stats[1]++;
}

// Its shader parsed from the material replayed (g_context's, the snapshot's lists swapped in), translated and compiled
// on a thread (hundreds of ms for a heavy material with Metal's cache cold); or taken from a cache of the same material
// for another target (the tiles of an export)
static bool material_cache_replay_parse(material_cache_replay_t *e) {
	for (i32 i = 0; i < material_cache_replay_count; ++i) {
		material_cache_replay_t *o = material_cache_replays[i];
		if (o != e && o->bake != NULL && hash128_eq(o->snap, e->snap) && o->uv_map == e->uv_map && o->mesh == e->mesh) {
			e->bake        = o->bake;
			e->bake_t0     = o->bake_t0;
			e->bake_ready  = o->bake_ready;
			e->emis_used   = o->emis_used;
			e->subs_used   = o->subs_used;
			e->height_used = o->height_used;
			o->bake        = NULL;
			o->bake_ready  = false;
			return true;
		}
	}
	e->bake_t0        = iron_time();
	material_t *sdata = ALLOC_INIT(material_t, {.name = "Material", .canvas = g_context->material->canvas});
	char       *glsl  = NULL;
	char       *why   = NULL;
	paint_mat_snapshot_parse_begin(g_context->material);
	node_shader_context_t *con =
	    make_paint_cache_bake_run(sdata, gpu_texture_format_name(GPU_TEXTURE_FORMAT_RGBA128), &e->emis_used, &e->subs_used, &e->height_used, &glsl, &why);
	paint_mat_snapshot_parse_end();
	shader_context_t *s = con->data;
	node_shader_context_free(con);
	free(sdata);
	if (why != NULL) {
		free(glsl);
		s->shader_from_source = true;
		make_material_free_context(s);
		material_cache_replay_unfit(e, why);
		return false;
	}
	e->bake = make_material_async_compile(s, glsl);
	if (!make_material_async_started(e->bake)) {
		material_cache_replay_unfit(e, "its shader did not load");
		return false;
	}
	return true;
}

// Its shader compiled: false while it compiles (sync: waited for), or when it failed (unfit then)
static bool material_cache_replay_compiled(material_cache_replay_t *e, bool sync) {
	if (e->bake_ready) {
		return true;
	}
	while (!make_material_async_ready(e->bake)) {
		if (!sync) {
			return false;
		}
		usleep(500);
	}
	shader_context_t *s = make_material_async_scon(e->bake);
	if (s->_->pipe == NULL || s->_->pipe->impl.pipeline == NULL) {
		material_cache_replay_unfit(e, "its shader did not compile");
		return false;
	}
	e->bake_ready                  = true;
	material_cache_replay_stats[6] = (i32)((iron_time() - e->bake_t0) * 1000000.0);
	if (material_cache_timing()) {
		iron_log("[matcache] replay cache shader of snapshot %016llx: parsed, then translated and compiled on a thread, ready in %.1f ms",
		         (unsigned long long)e->snap.lo, (iron_time() - e->bake_t0) * 1000.0);
	}
	return true;
}

static void material_cache_replay_done(void *data, double gpu_ms) {
	atomic_store(&material_cache_replay_us, gpu_ms < 0.0 ? 0 : (i32)(gpu_ms * 1000.0));
	atomic_store(&material_cache_replay_flight, false);
}

// The last band's GPU time in: the texels of the next band. False while it is in flight.
static bool material_cache_replay_poll(void) {
	if (atomic_load(&material_cache_replay_flight)) {
		if (++material_cache_replay_wait > 60) { // Its time is not coming: on with the size it had
			atomic_store(&material_cache_replay_flight, false);
			material_cache_replay_band = 0;
		}
		return false;
	}
	i32                      us = atomic_exchange(&material_cache_replay_us, -1);
	material_cache_replay_t *e  = material_cache_replay_owner;
	if (us >= 0 && e != NULL && material_cache_replay_band > 0) {
		f32 ms      = us / 1000.0f;
		f32 per_px  = ms / material_cache_replay_band;
		e->px_peak  = per_px > e->px_peak ? per_px : e->px_peak;
		f32 est     = per_px > e->px_peak * 0.75f ? per_px : e->px_peak * 0.75f; // Dense parts may come next
		f64 px      = est > 0.0f ? MATERIAL_CACHE_BAND_MS / est : material_cache_replay_band * 2.0;
		px          = px > material_cache_replay_band * 2.0 ? material_cache_replay_band * 2.0 : px;
		e->band_px  = px < MATERIAL_CACHE_REPLAY_BLOCK * MATERIAL_CACHE_REPLAY_BLOCK ? MATERIAL_CACHE_REPLAY_BLOCK * MATERIAL_CACHE_REPLAY_BLOCK : (i64)px;
		e->gpu_ms += ms;
		material_cache_replay_stats[5] = us > material_cache_replay_stats[5] ? us : material_cache_replay_stats[5];
	}
	material_cache_replay_band = 0;
	return true;
}

static bool material_cache_replay_block_done(material_cache_replay_t *e, i32 bx, i32 by) {
	i32 i = by * e->bw + bx;
	return (e->done[i >> 6] >> (i & 63)) & 1;
}

// The blocks of e a replay reads, from uv bounds of the layer (NULL: all of them): [b[0], b[2]) x [b[1], b[3])
static void material_cache_replay_need(material_cache_replay_t *e, const f32 *uv, i32 *b) {
	b[0] = 0, b[1] = 0, b[2] = e->bw, b[3] = e->bh;
	if (uv == NULL) {
		return;
	}
	f32 sx = e->w / (e->rect[2] - e->rect[0]);
	f32 sy = e->h / (e->rect[3] - e->rect[1]);
	i32 x0 = (i32)floorf((uv[0] - e->rect[0]) * sx) - MATERIAL_CACHE_REPLAY_BLOCK; // A block around: rasterization at
	i32 y0 = (i32)floorf((uv[1] - e->rect[1]) * sy) - MATERIAL_CACHE_REPLAY_BLOCK; // this size, the bounds' tile grid
	i32 x1 = (i32)ceilf((uv[2] - e->rect[0]) * sx) + MATERIAL_CACHE_REPLAY_BLOCK;
	i32 y1 = (i32)ceilf((uv[3] - e->rect[1]) * sy) + MATERIAL_CACHE_REPLAY_BLOCK;
	b[0]   = x0 < 0 ? 0 : x0 / MATERIAL_CACHE_REPLAY_BLOCK;
	b[1]   = y0 < 0 ? 0 : y0 / MATERIAL_CACHE_REPLAY_BLOCK;
	b[2]   = x1 <= 0 ? 0 : (x1 + MATERIAL_CACHE_REPLAY_BLOCK - 1) / MATERIAL_CACHE_REPLAY_BLOCK;
	b[3]   = y1 <= 0 ? 0 : (y1 + MATERIAL_CACHE_REPLAY_BLOCK - 1) / MATERIAL_CACHE_REPLAY_BLOCK;
	b[2]   = b[2] > e->bw ? e->bw : b[2];
	b[3]   = b[3] > e->bh ? e->bh : b[3];
	b[0]   = b[0] > b[2] ? b[2] : b[0];
	b[1]   = b[1] > b[3] ? b[3] : b[1];
}

// A band of the blocks needed b not rendered yet: rows of blocks from the first one with some, each as wide as the
// blocks missing in them, about e->band_px texels. The mesh drawn at its uv coordinates in the replay's part of the
// layer, scissored. False when every block of b is rendered.
static bool material_cache_replay_draw_band(material_cache_replay_t *e, const i32 *b) {
	i32 r0 = -1, xa = 0, xb = 0, r1 = 0;
	for (i32 by = b[1]; by < b[3]; ++by) {
		i32 lo = -1, hi = -1;
		for (i32 bx = b[0]; bx < b[2]; ++bx) {
			if (!material_cache_replay_block_done(e, bx, by)) {
				lo = lo < 0 ? bx : lo;
				hi = bx + 1;
			}
		}
		if (lo < 0) {
			if (r0 >= 0) {
				break; // The band ends at a row without missing blocks
			}
			continue;
		}
		i32 nxa = r0 < 0 || lo < xa ? lo : xa;
		i32 nxb = r0 < 0 || hi > xb ? hi : xb;
		if (r0 >= 0 && (i64)(nxb - nxa) * (by + 1 - r0) * MATERIAL_CACHE_REPLAY_BLOCK * MATERIAL_CACHE_REPLAY_BLOCK > e->band_px) {
			break;
		}
		r0 = r0 < 0 ? by : r0;
		xa = nxa, xb = nxb, r1 = by + 1;
	}
	if (r0 < 0) {
		return false;
	}
	i32 x = xa * MATERIAL_CACHE_REPLAY_BLOCK, y = r0 * MATERIAL_CACHE_REPLAY_BLOCK;
	i32 w = xb * MATERIAL_CACHE_REPLAY_BLOCK > e->w ? e->w - x : (xb - xa) * MATERIAL_CACHE_REPLAY_BLOCK;
	i32 h = r1 * MATERIAL_CACHE_REPLAY_BLOCK > e->h ? e->h - y : (r1 - r0) * MATERIAL_CACHE_REPLAY_BLOCK;
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	draw_saved_t saved_draw = draw_suspend();
	gpu_commit(NULL, NULL); // The work so far apart: the band's GPU time is its own
	gpu_begin(e->tex, 3, NULL, e->cleared ? GPU_CLEAR_NONE : GPU_CLEAR_COLOR, 0x00000000, 0.0f); // Never rendered: zeros, read as nothing
	e->cleared = true;
	gpu_scissor(x, y, w, h);
	_mesh_object_last_pipeline = NULL;
	i32 need                   = mm_buffers_require(MM_BUFFERS_FULL);
	mesh_object_draw(render_path_paint_target(), make_material_async_scon(e->bake), NULL);
	mm_buffers_require(need);
	gpu_disable_scissor();
	gpu_end();
	_mesh_object_last_pipeline  = NULL;
	material_cache_replay_band  = w * h;
	material_cache_replay_owner = e;
	material_cache_replay_wait  = 0;
	atomic_store(&material_cache_replay_us, -1);
	atomic_store(&material_cache_replay_flight, true);
	gpu_commit(material_cache_replay_done, NULL);
	draw_resume(saved_draw);
	for (i32 by = r0; by < r1; ++by) {
		for (i32 bx = xa; bx < xb; ++bx) {
			i32 i = by * e->bw + bx;
			if (!((e->done[i >> 6] >> (i & 63)) & 1)) {
				e->done[i >> 6] |= 1ULL << (i & 63);
				e->ndone++;
			}
		}
	}
	material_cache_replay_stats[4]++;
	return true;
}

// Moves e's rendering on, in the context replayed (its material, a state of the stroke and the target's _paint_uv_tile),
// for what a replay writes: uv bounds of the layer (NULL: all of it). sync: to the end, each band waited for. Else a
// step: the shader, or a band once the last one's time is in. 1 ready there, 0 not yet, -1 unfit.
i32 material_cache_replay_build(material_cache_replay_t *e, const f32 *uv, bool sync) {
	while (true) {
		e->used = iron_time();
		if (e->state == MATERIAL_CACHE_UNFIT) {
			return -1;
		}
		if (e->state == MATERIAL_CACHE_READY) {
			return 1;
		}
		if (e->bake == NULL) { // New, or its shader went on to another target of the material
			bool shared = e->state != MATERIAL_CACHE_NEW;
			for (i32 i = 0; i < material_cache_replay_count && !shared; ++i) {
				material_cache_replay_t *o = material_cache_replays[i];
				shared = o != e && o->bake != NULL && hash128_eq(o->snap, e->snap) && o->uv_map == e->uv_map && o->mesh == e->mesh;
			}
			if (!material_cache_replay_parse(e)) {
				return -1;
			}
			if (e->state == MATERIAL_CACHE_NEW) {
				material_cache_replay_start(e);
			}
			if (!sync && !shared) { // A frame of its own for a parse
				return 0;
			}
			continue;
		}
		if (!material_cache_replay_compiled(e, sync)) {
			return e->state == MATERIAL_CACHE_UNFIT ? -1 : 0;
		}
		i32 b[4];
		material_cache_replay_need(e, uv, b);
		bool all = e->ndone == e->bw * e->bh;
		bool any = false; // A block of b missing
		for (i32 by = b[1]; by < b[3] && !any && !all; ++by) {
			for (i32 bx = b[0]; bx < b[2] && !any; ++bx) {
				any = !material_cache_replay_block_done(e, bx, by);
			}
		}
		if (!any || all) {
			if (all) {
				e->state = MATERIAL_CACHE_READY; // Its shader kept for the next target of the material
			}
			if (material_cache_timing()) {
				iron_log("[matcache] replay cache %dx%d of snapshot %016llx: %d of %d blocks, %.1f ms of GPU, %.0f MB", e->w, e->h,
				         (unsigned long long)e->snap.lo, e->ndone, e->bw * e->bh, e->gpu_ms, material_cache_replay_total() / (1024.0 * 1024.0));
			}
			material_cache_replay_stats[7] = (i32)(e->gpu_ms * 1000.0f);
			return 1;
		}
		if (!material_cache_replay_poll()) { // A band in flight, this one's or another's
			if (!sync) {
				return 0;
			}
			usleep(200);
			continue;
		}
		material_cache_replay_draw_band(e, b);
		if (!sync) {
			return 0;
		}
	}
}

// Paint shaders parsed and drawn while it is bound read e (NULL: none)
void material_cache_replay_bind(material_cache_replay_t *e) {
	material_cache_replay_bound = e;
}

bool material_cache_replay_is_bound(void) {
	return material_cache_replay_bound != NULL;
}

// Tests: replay caches off (strokes evaluate their material), on again
void material_cache_replay_set_enabled(i32 on) {
	material_cache_replay_on = on ? 1 : 0;
	if (!on) {
		material_cache_replay_release();
	}
}

// Tests: 0 replay caches, 1 rendered, 2 unfit, 3 over the budget, 4 bands, 5 the longest band in microseconds of GPU
// time, 6 the last shader's parse and compile in microseconds, 7 the GPU time of the last one rendered in microseconds,
// 8 their texture memory in KB
i32 material_cache_replay_stat(i32 what) {
	if (what == 0) {
		return material_cache_replay_count;
	}
	if (what == 8) {
		return (i32)(material_cache_replay_total() / 1024);
	}
	return what > 0 && what < 8 ? material_cache_replay_stats[what] : 0;
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
