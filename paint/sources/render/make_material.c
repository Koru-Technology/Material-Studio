
#include "../global.h"
#include <stdatomic.h>
#include <unistd.h>
#ifdef __APPLE__
#include <pthread.h>
#endif

static shader_context_t *make_material_default_scon = NULL;
static shader_context_t *make_material_saved_scon   = NULL;

// Objects without an override material draw with the pass shaders
bool make_material_is_default(shader_data_t *m) {
	return m == g_project->_->materials->buffer[0]->data;
}

bool make_material_get_mout() {
	for (i32 i = 0; i < g_context->material->canvas->nodes->length; ++i) {
		ui_node_t *n = g_context->material->canvas->nodes->buffer[i];
		if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
			return true;
		}
	}
	return false;
}

void make_material_delete_context_on_next_frame(shader_context_t *c) {
	shader_context_delete(c);
	make_material_free_context(c);
}

// Frees what a parsed context holds, its pipeline deleted already or never made (a context that was not loaded)
void make_material_free_context(shader_context_t *c) {
	if (c->bind_textures != NULL) {
		for (i32 i = 0; i < c->bind_textures->length; ++i) {
			bind_tex_t *tex = c->bind_textures->buffer[i];
			free(tex->name);
			free(tex->file);
			free(tex);
		}
		array_free(c->bind_textures);
		free(c->bind_textures);
		c->bind_textures = NULL;
	}
	if (c->_ != NULL && c->_->textures != NULL) {
		array_free(c->_->textures);
		free(c->_->textures);
		c->_->textures = NULL;
	}

	if (!c->shader_from_source) {
		return;
	}

	if (c->constants != NULL) {
		for (i32 i = 0; i < c->constants->length; ++i) {
			shader_const_t *sc = c->constants->buffer[i];
			free(sc->name);
			free(sc->type);
			free(sc->link);
			free(sc);
		}
		array_free(c->constants);
		free(c->constants);
	}
	if (c->texture_units != NULL) {
		for (i32 i = 0; i < c->texture_units->length; ++i) {
			tex_unit_t *tu = c->texture_units->buffer[i];
			free(tu->name);
			free(tu->link);
			free(tu);
		}
		array_free(c->texture_units);
		free(c->texture_units);
	}
	if (c->vertex_elements != NULL) {
		for (i32 i = 0; i < c->vertex_elements->length; ++i) {
			free(c->vertex_elements->buffer[i]);
		}
		array_free(c->vertex_elements);
		free(c->vertex_elements);
	}
	array_delete(c->color_writes_red);
	array_delete(c->color_writes_green);
	array_delete(c->color_writes_blue);
	array_delete(c->color_writes_alpha);
	array_delete(c->color_attachments);
	if (c->_ != NULL) {
		if (c->_->constants != NULL) {
			array_free(c->_->constants);
			free(c->_->constants);
		}
		if (c->_->tex_units != NULL) {
			array_free(c->_->tex_units);
			free(c->_->tex_units);
		}
		free(c->_);
	}
	free(c);
}

void make_material_delete_context(shader_context_t *c) {
	// Ensure pipeline is no longer in use
	sys_notify_on_next_frame(&make_material_delete_context_on_next_frame, c);
}

void make_material_parse_mesh_material() {
	render_path_base_redraw(); // The 3D view shows the change
	if (make_material_mesh_scons == NULL) {
		make_material_mesh_scons = any_array_create(0);
	}
	for (i32 i = 0; i < make_material_mesh_scons->length; ++i) {
		if (make_material_mesh_scons->buffer[i] != NULL) {
			make_material_delete_context(make_material_mesh_scons->buffer[i]);
		}
	}
	make_material_mesh_scons->length = 0;

	// The first pass decides the layer pass count
	for (i32 i = 0; i == 0 || i < make_mesh_layer_pass_count; ++i) {
		material_t            *mm  = ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
		node_shader_context_t *con = make_mesh_run(mm, i);
		shader_context_load(con->data);
		any_array_push(make_material_mesh_scons, con->data);
		node_shader_context_free(con);
		free(mm);
	}

	g_context->ddirty  = 2;
	g_context->rtdirty = 1;

	if (g_project->_->translucency) {
		make_material_parse_depth_material();
	}
}

void make_material_parse_mesh_preview_material() {
	if (!make_material_get_mout()) {
		return;
	}

	// Replaces the first mesh pass shader, make_material_parse_mesh_material() restores it
	material_t            *sd  = ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
	node_shader_context_t *con = make_mesh_preview_run(sd, false);
	shader_context_load(con->data);
	if (make_material_mesh_scons == NULL) {
		make_material_mesh_scons = any_array_create(0);
	}
	if (make_material_mesh_scons->length == 0) {
		any_array_push(make_material_mesh_scons, NULL);
	}
	if (make_material_mesh_scons->buffer[0] != NULL) {
		make_material_delete_context(make_material_mesh_scons->buffer[0]);
	}
	make_material_mesh_scons->buffer[0] = con->data;
	node_shader_context_free(con);
	free(sd);
}

static bool make_material_bake_linked = false;

char *make_material_node_preview_id(ui_node_t *node, ui_node_t_array_t *parents) {
	return string_tmp("m%d_%s", g_context->material->id, parser_material_node_name(node, parents));
}

static gpu_texture_t *make_material_node_preview_target(char *id, i32 res_x, i32 res_y, gpu_texture_format_t format) {
	gpu_texture_t *image = any_map_get(g_context->node_previews, id);
	any_array_push(g_context->node_previews_used, id);
	if (image == NULL || image->width != res_x || image->height != res_y || image->format != format) {
		if (image != NULL) {
			gpu_delete_texture(image);
		}
		image = gpu_create_render_target(res_x, res_y, format);
		any_map_set(g_context->node_previews, string_copy(id), image);
	}
	return image;
}

void make_material_bake_node_preview(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	if (string_equals(node->type, "BLUR")) {
		char *id     = make_material_node_preview_id(node, parents);
		i32   passes = blur_node_seamless_passes(node);
		i32   res_x  = math_floor(config_get_texture_res_x() / 4.0);
		i32   res_y  = math_floor(config_get_texture_res_y() / 4.0);
		// Seamless blur renders the input to _src, then diffuses it into the sampled texture
		gpu_texture_t *image = make_material_node_preview_target(passes > 0 ? string_tmp("%s_src", id) : id, res_x, res_y, GPU_TEXTURE_FORMAT_RGBA32);

		parser_material_blur_passthrough = true;
		util_render_make_node_preview(g_context->material->canvas, node, image, group, parents);
		parser_material_blur_passthrough = false;

		if (passes > 0) {
			util_uv_cache_seam_map(res_x, res_y);
			gpu_texture_t *dst = make_material_node_preview_target(id, res_x, res_y, GPU_TEXTURE_FORMAT_RGBA64);
			gpu_texture_t *tmp = make_material_node_preview_target(string_tmp("%s_tmp", id), res_x, res_y, GPU_TEXTURE_FORMAT_RGBA64);
			util_uv_blur_seamless(dst, tmp, image, passes);
		}
	}
	else if (string_equals(node->type, "DIRECT_WARP")) {
		char          *id    = make_material_node_preview_id(node, parents);
		gpu_texture_t *image = any_map_get(g_context->node_previews, id);
		any_array_push(g_context->node_previews_used, id);
		i32 res_x = math_floor(config_get_texture_res_x());
		i32 res_y = math_floor(config_get_texture_res_y());
		if (image == NULL || image->width != res_x || image->height != res_y) {
			if (image != NULL) {
				gpu_delete_texture(image);
			}
			image = gpu_create_render_target(res_x, res_y, GPU_TEXTURE_FORMAT_RGBA32);
			any_map_set(g_context->node_previews, string_copy(id), image);
		}

		parser_material_warp_passthrough = true;
		util_render_make_node_preview(g_context->material->canvas, node, image, group, parents);
		parser_material_warp_passthrough = false;
	}
	else if (string_equals(node->type, "BAKE_CURVATURE")) {
		make_curvature_bake(node, group, parents, make_material_node_preview_id(node, parents));
	}
	else if (string_equals(node->type, "CURVATURE_MASK") && group == NULL) {
		curvature_mask_bake(node);
	}
}

void make_material_traverse_nodes(ui_node_t_array_t *nodes, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *node = nodes->buffer[i];
		make_material_bake_node_preview(node, group, parents);
		if (string_equals(node->type, "GROUP")) {
			for (i32 j = 0; j < g_project->_->material_groups->length; ++j) {
				node_group_t *g     = g_project->_->material_groups->buffer[j];
				char         *cname = g->canvas->name;
				if (string_equals(cname, node->name)) {
					any_array_push(parents, node);
					make_material_traverse_nodes(g->canvas->nodes, g->canvas, parents);
					array_pop(parents);
					break;
				}
			}
		}
	}
}

// Deletes previews of material m, except the ones listed in keep
void make_material_delete_node_previews(slot_material_t *m, string_array_t *keep) {
	if (g_context->node_previews == NULL) {
		return;
	}
	char           *prefix = string_tmp("m%d_", m->id);
	string_array_t *keys   = map_keys(g_context->node_previews);
	for (i32 i = 0; i < keys->length; ++i) {
		char *key = keys->buffer[i];
		if (starts_with(key, prefix) && (keep == NULL || string_array_index_of(keep, key) == -1)) {
			gpu_texture_t *image = any_map_get(g_context->node_previews, key);
			gpu_delete_texture(image);
			map_delete(g_context->node_previews, key);
			free(key);
		}
	}
	array_free(keys);
	free(keys);
}

void make_material_bake_node_previews() {
	g_context->node_previews_used = any_array_create_from_raw((void *[]){}, 0);
	if (g_context->node_previews == NULL) {
		g_context->node_previews = any_map_create();
	}
	ui_node_t_array_t *empty = any_array_create_from_raw((void *[]){}, 0);
	i32                need  = mm_buffers_require(MM_BUFFERS_FULL); // Blur and warp previews feed the material: full Material Maker buffers
	make_material_traverse_nodes(g_context->material->canvas->nodes, NULL, empty);
	mm_buffers_require(need);

	make_material_delete_node_previews(g_context->material, g_context->node_previews_used);
	array_free(empty);
	free(empty);
	array_free(g_context->node_previews_used);
	free(g_context->node_previews_used);
	g_context->node_previews_used = NULL;
}

// Refresh previews of a material that is not selected but fills linked layers,
// blur and warp sample other layers so they go stale on every stroke
void make_material_bake_node_previews_linked() {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();
	make_material_bake_linked = true;
	make_material_bake_node_previews();
	make_material_bake_linked = false;
	if (in_use)
		draw_begin(current, false, 0);
}

// The position picker shader reads only the gbuffer, it does not depend on the material and is built once
static shader_context_t *make_material_pick_scon = NULL;

static void make_material_set_paint_scon(shader_context_t *scon) {
	shader_context_t *old = make_material_paint_scon;
	if (old != NULL && old != scon && old != make_material_default_scon && old != make_material_saved_scon && old != make_material_pick_scon) {
		make_material_delete_context(old);
	}
	make_material_paint_scon = scon;
}

static void make_material_bake_previews_now(void) {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();
	make_material_bake_node_previews();
	if (in_use)
		draw_begin(current, false, 0);
}

static void make_material_parse_paint_material_now(bool bake_previews) {
	if (!make_material_get_mout()) {
		return;
	}

	if (bake_previews) {
		make_material_bake_previews_now();
	}

	material_t            *sdata = ALLOC_INIT(material_t, {.name = "Material", .canvas = g_context->material->canvas});
	node_shader_context_t *con   = make_paint_run(sdata);
	shader_context_load(con->data);
	make_material_set_paint_scon(con->data);
	node_shader_context_free(con);
	free(sdata);

	if (make_material_default_scon == NULL) {
		make_material_default_scon = make_material_paint_scon;
	}
}

static void make_material_pending_drop(void);

// The paint shader of the context, compiled now. A paint shader still compiling for an earlier context
// (make_material_parse_paint_material_async) is dropped: this one is newer.
void make_material_parse_paint_material(bool bake_previews) {
	make_material_pending_drop();
	make_material_parse_paint_material_now(bake_previews);
}

// Paint shader for util_render_pick_pos_nor_tex, the picker tool and pick flag are set by the caller
void make_material_use_pick_material() {
	if (make_material_pick_scon == NULL) {
		make_material_parse_paint_material_now(false); // The material's shader compiling (pending) stays
		make_material_pick_scon = make_material_paint_scon;
		return;
	}
	make_material_set_paint_scon(make_material_pick_scon);
}

// A paint shader for the context as it is now, kept by the caller: the current one stays. Its pipeline is not compiled,
// and with a GLSL translation make_paint.c's cache does not have, its shaders are empty and *glsl holds the GLSL to
// translate (shader_context_set_sources). NULL when the parse made none.
static shader_context_t *make_material_parse_kept_deferred(char **glsl) {
	shader_context_t *current = make_material_paint_scon;
	shader_context_t *saved   = make_material_saved_scon;
	make_material_saved_scon  = current; // Not deleted by the parse
	shader_compile_batch_begin();
	free(make_paint_deferred_glsl);
	make_paint_deferred_glsl     = NULL;
	make_paint_defer_translation = true;
	make_material_parse_paint_material_now(false);
	make_paint_defer_translation = false;
	*glsl                        = make_paint_deferred_glsl;
	make_paint_deferred_glsl     = NULL;
	shader_context_t  *kept      = make_material_paint_scon;
	i32                count     = 0;
	shader_context_t **items     = shader_compile_batch_take(&count);
	for (i32 i = 0; i < count; ++i) {
		if (items[i] != kept) { // Others the parse made, compiled now
			shader_context_finish_compile(items[i]);
		}
	}
	make_material_paint_scon = current;
	make_material_saved_scon = saved;
	if (kept == current) {
		free(*glsl);
		*glsl = NULL;
		return NULL;
	}
	return kept;
}

// Paint shaders compiling on a thread: parsed on the main thread (make_paint_run, tens of ms), their GLSL translated
// (glslang and SPIRV-Cross, when make_paint.c's cache does not have it) and their pipeline compiled (Metal libraries of
// both stages and the pipeline state, hundreds of ms on the iPad for a heavy material) on a thread.
struct make_material_async {
	shader_context_t *scon;
	char             *glsl; // To translate on the thread, NULL when the parse had the shaders
	char             *vs;
	char             *fs;
	iron_thread_t     thread;
	atomic_bool       done;
	bool              started; // Its thread was started: a shader to wait for
	bool              compiling;
	bool              ready;
	f64               t0;
};

#ifdef IRON_METAL
void gpu_shader_precompile(gpu_shader_t *shader); // metal_gpu.m

static void make_material_async_frag_worker(void *param) {
#ifdef __APPLE__
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	gpu_shader_precompile(param);
}
#endif

static void make_material_async_worker(void *param) {
	make_material_async_t *a = param;
#ifdef __APPLE__
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); // Strokes wait for it
#endif
	if (a->glsl != NULL && iron_glsl_translate(a->glsl, &a->vs, &a->fs)) {
		shader_context_set_sources(a->scon, a->vs, a->fs);
	}
#ifdef IRON_METAL
	// The fragment library (the material, the longer one) on a second thread while this one compiles the vertex library
	gpu_pipeline_t *pipe = a->scon->_->pipe;
	iron_thread_t   frag;
	iron_thread_init(&frag, make_material_async_frag_worker, pipe->fragment_shader);
	gpu_shader_precompile(pipe->vertex_shader);
	iron_thread_wait_and_destroy(&frag);
#endif
	gpu_pipeline_compile(a->scon->_->pipe); // Empty shaders after a failed translation: no pipeline, as when parsed at once
	atomic_store(&a->done, true);
}

// Threads at work, joined at exit: glslang's tables go with the process's static destructors, a translation still
// running then crashed
#define MAKE_MATERIAL_THREADS_MAX 64
static make_material_async_t *make_material_threads[MAKE_MATERIAL_THREADS_MAX];
static i32                    make_material_nthreads = 0;

static void make_material_thread_ended(make_material_async_t *a) {
	for (i32 i = 0; i < make_material_nthreads; ++i) {
		if (make_material_threads[i] == a) {
			make_material_threads[i] = make_material_threads[--make_material_nthreads];
			return;
		}
	}
}

static void make_material_threads_join(void) {
	for (i32 i = 0; i < make_material_nthreads; ++i) {
		make_material_async_t *a = make_material_threads[i];
		if (a->compiling) {
			iron_thread_wait_and_destroy(&a->thread);
			a->compiling = false;
		}
	}
	make_material_nthreads = 0;
}

// The paint shader of the context as it is now (the caller sets it up), kept by the caller: parsed now, translated and
// compiled on a thread. The current paint shader stays.
make_material_async_t *make_material_async_start(void) {
	static bool at_exit = false;
	if (!at_exit) {
		at_exit = true;
		atexit(make_material_threads_join);
	}
	make_material_async_t *a = calloc(1, sizeof(make_material_async_t));
	atomic_init(&a->done, false);
	a->t0   = iron_time();
	a->scon = make_material_parse_kept_deferred(&a->glsl);
	if (a->scon != NULL && a->scon->_ != NULL && a->scon->_->pipe != NULL && a->scon->_->pipe->vertex_shader != NULL &&
	    a->scon->_->pipe->fragment_shader != NULL) {
		a->started   = true;
		a->compiling = true;
		if (make_material_nthreads < MAKE_MATERIAL_THREADS_MAX) {
			make_material_threads[make_material_nthreads++] = a;
		}
		iron_thread_init(&a->thread, make_material_async_worker, a);
	}
	return a;
}

// Its thread was started: false when the parse made no shader to compile
bool make_material_async_started(make_material_async_t *a) {
	return a->started;
}

static void make_material_async_joined(make_material_async_t *a) {
	iron_thread_wait_and_destroy(&a->thread);
	a->compiling = false;
	make_material_thread_ended(a);
	shader_context_bind(a->scon);
	make_paint_cache_put(a->glsl, a->vs, a->fs); // The same material parsed again translates no more
	free(a->glsl);
	free(a->vs);
	free(a->fs);
	a->glsl  = NULL;
	a->vs    = NULL;
	a->fs    = NULL;
	a->ready = true;
}

// True once its pipeline is compiled and bound, or when there was none to compile
bool make_material_async_ready(make_material_async_t *a) {
	if (a->ready || !a->started) {
		return true;
	}
	if (!atomic_load(&a->done)) {
		return false;
	}
	make_material_async_joined(a);
	return true;
}

static void make_material_async_wait(make_material_async_t *a) {
	if (a->compiling) {
		make_material_async_joined(a);
	}
}

shader_context_t *make_material_async_scon(make_material_async_t *a) {
	return a->scon;
}

void make_material_async_free(make_material_async_t *a) {
	if (a == NULL) {
		return;
	}
	if (a->compiling) {
		iron_thread_wait_and_destroy(&a->thread);
		a->compiling = false;
	}
	make_material_thread_ended(a);
	make_material_free_paint_scon(a->scon);
	free(a->glsl);
	free(a->vs);
	free(a->fs);
	free(a);
}

// The context's paint shader compiling (make_material_parse_paint_material_async): make_material_paint_scon until it is
// ready. Strokes wait for it (util_brush_update, render_path_paint_begin), the live brush preview skips it.
static make_material_async_t *make_material_pending = NULL;

#define MAKE_MATERIAL_DROPPED_MAX 16
static make_material_async_t *make_material_dropped[MAKE_MATERIAL_DROPPED_MAX]; // Replaced while compiling: freed once compiled
static i32                    make_material_ndropped   = 0;
static bool                   make_material_registered = false;
static i32                    make_material_stats[8];

static bool make_material_async_timing(void) {
	static int timing = -1;
	if (timing < 0) {
		timing = getenv("IRON_SHADER_TIMING") != NULL;
	}
	return timing == 1;
}

static void make_material_pending_drop(void) {
	make_material_async_t *a = make_material_pending;
	if (a == NULL) {
		return;
	}
	make_material_pending = NULL;
	mm_buffers_hold_workers(false);
	make_material_stats[3]++;
	if (a->compiling && !atomic_load(&a->done) && make_material_ndropped < MAKE_MATERIAL_DROPPED_MAX) {
		make_material_dropped[make_material_ndropped++] = a;
		return;
	}
	make_material_async_free(a); // Compiled already (or too many dropped: waits)
}

// The pending shader replaces the context's paint shader
static void make_material_pending_use(void) {
	make_material_async_t *a    = make_material_pending;
	shader_context_t      *scon = a->scon;
	make_material_pending       = NULL;
	a->scon                     = NULL;
	mm_buffers_hold_workers(false);
	make_material_stats[2]++;
	make_material_stats[5] = (i32)((iron_time() - a->t0) * 1000000.0);
	if (make_material_async_timing()) {
		iron_log("[shader] paint shader ready %.1f ms after its parse started", (iron_time() - a->t0) * 1000.0);
	}
	make_material_async_free(a);
	if (scon == NULL) {
		return;
	}
	if (make_material_saved_scon != NULL) { // Another paint shader in use for a moment (make_material_save_paint_material): replaces the saved one
		shader_context_t *old    = make_material_saved_scon;
		make_material_saved_scon = scon;
		make_material_free_paint_scon(old != scon ? old : NULL);
		return;
	}
	make_material_set_paint_scon(scon);
}

// The context's paint shader is current. wait: seconds to wait for its compile at most, below 0 until it is compiled.
bool make_material_paint_ready(f64 wait) {
	make_material_async_t *a = make_material_pending;
	if (a == NULL) {
		return true;
	}
	if (a->compiling && !atomic_load(&a->done) && wait != 0.0) {
		f64 t0 = iron_time();
		if (wait < 0.0) {
			make_material_async_wait(a);
		}
		else {
			while (!atomic_load(&a->done) && iron_time() - t0 < wait) {
				usleep(1000);
			}
		}
		i32 us = (i32)((iron_time() - t0) * 1000000.0);
		make_material_stats[6]++;
		make_material_stats[7] = us > make_material_stats[7] ? us : make_material_stats[7];
		if (make_material_async_timing()) {
			iron_log("[shader] waited %.1f ms for the paint shader (%s)", us / 1000.0, atomic_load(&a->done) ? "ready" : "still compiling");
		}
	}
	if (!make_material_async_ready(a)) {
		return false;
	}
	make_material_pending_use();
	return true;
}

// A paint shader of the context compiling, make_material_paint_scon is the previous one meanwhile
bool make_material_paint_pending(void) {
	return make_material_pending != NULL;
}

// Each frame: the pending shader is used once compiled, dropped ones are freed
static void make_material_async_update(void *_) {
	for (i32 i = 0; i < make_material_ndropped; ++i) {
		make_material_async_t *a = make_material_dropped[i];
		if (!a->compiling || atomic_load(&a->done)) {
			make_material_async_free(a);
			make_material_dropped[i--] = make_material_dropped[--make_material_ndropped];
		}
	}
	make_material_paint_ready(0.0);
}

// The paint shader of the context, as make_material_parse_paint_material: parsed now, translated and compiled on a
// thread (a heavy material's took 400 ms of the frame on the iPad). make_material_paint_scon stays the previous one until
// it is ready (make_material_paint_ready).
void make_material_parse_paint_material_async(bool bake_previews) {
	static int on = -1; // Env PAINT_SHADER_ASYNC=0: compiled at once, as before (comparisons)
	if (on < 0) {
		char *e = getenv("PAINT_SHADER_ASYNC");
		on      = e != NULL && e[0] == '0' ? 0 : 1;
	}
	if (!on || make_material_paint_scon == NULL || make_material_default_scon == NULL || make_material_paint_scon == make_material_pick_scon ||
	    make_material_saved_scon != NULL || !make_material_get_mout()) {
		make_material_parse_paint_material(bake_previews); // The first one, or in the middle of another use
		return;
	}
	if (!make_material_registered) {
		make_material_registered = true;
		sys_notify_on_update(make_material_async_update, NULL);
	}
	if (bake_previews) {
		make_material_bake_previews_now();
	}
	make_material_pending_drop();
	f64 t0                = iron_time();
	make_material_pending = make_material_async_start();
	make_material_stats[1]++;
	make_material_stats[4] = (i32)((iron_time() - t0) * 1000000.0);
	if (make_material_async_timing()) {
		iron_log("[shader] paint shader parsed in %.1f ms, %s on a thread", (iron_time() - t0) * 1000.0,
		         make_material_pending->glsl != NULL ? "translates and compiles" : "compiles");
	}
	if (!make_material_pending->started) {
		make_material_paint_ready(0.0); // Nothing to compile: used now, as a parse at once would
	}
	else {
		mm_buffers_hold_workers(true); // Metal's compiler on this one first, the material's buffers wait for it
	}
}

// Tests: 0 1 while a paint shader compiles, 1 paint shaders parsed to compile on a thread, 2 of them used, 3 dropped
// (replaced while compiling), 4 the last one's parse in microseconds (main thread), 5 from its parse until it was used,
// in microseconds, 6 waits for one, 7 the longest wait in microseconds
i32 make_material_paint_stat(i32 what) {
	if (what == 0) {
		return make_material_pending != NULL ? 1 : 0;
	}
	return what > 0 && what < 8 ? make_material_stats[what] : 0;
}

void make_material_free_paint_scon(shader_context_t *scon) {
	if (scon != NULL && scon != make_material_paint_scon && scon != make_material_default_scon && scon != make_material_pick_scon) {
		make_material_delete_context(scon);
	}
}

void make_material_save_paint_material() {
	make_material_saved_scon = make_material_paint_scon;
}

void make_material_restore_paint_material() {
	if (make_material_saved_scon == NULL) {
		return;
	}
	shader_context_t *saved  = make_material_saved_scon;
	make_material_saved_scon = NULL;
	make_material_set_paint_scon(saved);
}

shader_context_t *make_material_parse_node_preview_material(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	if (node->outputs->length == 0) {
		return NULL;
	}

	material_t            *sdata         = ALLOC_INIT(material_t, {.name = "Material", .canvas = g_context->material->canvas});
	node_shader_context_t *con           = make_node_preview_run(sdata, node, group, parents);
	bool                   compile_error = false;
	shader_context_t      *scon;
	shader_context_load(con->data);

	if (con->data == NULL) {
		compile_error = true;
	}
	scon = con->data;
	node_shader_context_free(con);
	free(sdata);
	if (compile_error) {
		return NULL;
	}

	return scon;
}

void make_material_parse_brush() {
	parser_logic_parse(g_context->brush->canvas);
}

// Blend modes also apply to occlusion, roughness, metallic and height, except the hue, saturation, color and value modes
bool make_material_blend_mode_channels(i32 blending) {
	return blending != BLEND_TYPE_MIX && blending < BLEND_TYPE_HUE;
}

char *make_material_blend_mode(node_shader_t *shader, i32 blending, char *cola, char *colb, char *opac) {
	if (blending == BLEND_TYPE_MIX) {
		return string_tmp("mix(%s, %s, %s)", cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_DARKEN) {
		return string_tmp("mix(%s, min(%s, %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_MULTIPLY) {
		return string_tmp("mix(%s, %s * %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_BURN) {
		return string_tmp("mix(%s, vec3(1.0, 1.0, 1.0) - (vec3(1.0, 1.0, 1.0) - %s) / %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_LIGHTEN) {
		return string_tmp("max(%s, %s * %s)", cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_SCREEN) {
		return string_tmp("(vec3(1.0, 1.0, 1.0) - (vec3(1.0 - %s, 1.0 - %s, 1.0 - %s) + %s * (vec3(1.0, 1.0, 1.0) - %s)) * (vec3(1.0, 1.0, 1.0) - %s))",
		                  opac, opac, opac, opac, colb, cola);
	}
	else if (blending == BLEND_TYPE_DODGE) {
		return string_tmp("mix(%s, %s / (vec3(1.0, 1.0, 1.0) - %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_ADD) {
		return string_tmp("mix(%s, %s + %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_OVERLAY) {
		// return "lerp(" + cola + ", vec3( \
		// 	" + cola + ".r < 0.5 ? 2.0 * " + cola + ".r * " + colb + ".r : 1.0 - 2.0 * (1.0 - " + cola + ".r) * (1.0 - " + colb + ".r), \
		// 	" + cola + ".g < 0.5 ? 2.0 * " + cola + ".g * " + colb + ".g : 1.0 - 2.0 * (1.0 - " + cola + ".g) * (1.0 - " + colb + ".g), \
		// 	" + cola + ".b < 0.5 ? 2.0 * " + cola + ".b * " + colb + ".b : 1.0 - 2.0 * (1.0 - " + cola + ".b) * (1.0 - " + colb + ".b) \
		// ), " + opac + ")";
		char *cola_rgb = string_tmp("%s_rgb", string_replace_all(cola, ".", "_"));
		char *colb_rgb = string_tmp("%s_rgb", string_replace_all(colb, ".", "_"));
		char *res_r    = string_tmp("%s_res_r", string_replace_all(cola, ".", "_"));
		char *res_g    = string_tmp("%s_res_g", string_replace_all(cola, ".", "_"));
		char *res_b    = string_tmp("%s_res_b", string_replace_all(cola, ".", "_"));
		node_shader_write_frag(shader, string_tmp("float %s;", res_r));
		node_shader_write_frag(shader, string_tmp("float %s;", res_g));
		node_shader_write_frag(shader, string_tmp("float %s;", res_b));
		node_shader_write_frag(shader, string_tmp("vec3 %s = %s;", cola_rgb, cola)); // cola_rgb = cola.rgb
		node_shader_write_frag(shader, string_tmp("vec3 %s = %s;", colb_rgb, colb));
		node_shader_write_frag(shader, string_tmp("if (%s.r < 0.5) { %s = 2.0 * %s.r * %s.r; } else { %s = 1.0 - 2.0 * (1.0 - %s.r) * (1.0 - %s.r); }", cola_rgb,
		                                        res_r, cola_rgb, colb_rgb, res_r, cola_rgb, colb_rgb));
		node_shader_write_frag(shader, string_tmp("if (%s.g < 0.5) { %s = 2.0 * %s.g * %s.g; } else { %s = 1.0 - 2.0 * (1.0 - %s.g) * (1.0 - %s.g); }", cola_rgb,
		                                        res_g, cola_rgb, colb_rgb, res_g, cola_rgb, colb_rgb));
		node_shader_write_frag(shader, string_tmp("if (%s.b < 0.5) { %s = 2.0 * %s.b * %s.b; } else { %s = 1.0 - 2.0 * (1.0 - %s.b) * (1.0 - %s.b); }", cola_rgb,
		                                        res_b, cola_rgb, colb_rgb, res_b, cola_rgb, colb_rgb));
		return string_tmp("mix(%s, vec3(%s, %s, %s), %s)", cola, res_r, res_g, res_b, opac);
	}
	else if (blending == BLEND_TYPE_SOFT_LIGHT) {
		return string_tmp("((1.0 - %s) * %s + %s * ((vec3(1.0, 1.0, 1.0) - %s) * %s * %s + %s * (vec3(1.0, 1.0, 1.0) - (vec3(1.0, 1.0, 1.0) - %s) * "
		                  "(vec3(1.0, 1.0, 1.0) - %s))))",
		                  opac, cola, opac, cola, colb, cola, cola, colb, cola);
	}
	else if (blending == BLEND_TYPE_LINEAR_LIGHT) {
		return string_tmp("(%s + %s * (vec3(2.0, 2.0, 2.0) * (%s - vec3(0.5, 0.5, 0.5))))", cola, opac, colb);
	}
	else if (blending == BLEND_TYPE_DIFFERENCE) {
		return string_tmp("mix(%s, abs(%s - %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_SUBTRACT) {
		return string_tmp("mix(%s, %s - %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_DIVIDE) {
		return string_tmp("vec3(1.0 - %s, 1.0 - %s, 1.0 - %s) * %s + vec3(%s, %s, %s) * %s / %s", opac, opac, opac, cola, opac, opac, opac, cola, colb);
	}
	else if (blending == BLEND_TYPE_HUE) {
		node_shader_add_function(shader, str_hue_sat);
		return string_tmp("mix(%s, hsv_to_rgb(vec3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, colb, cola, cola, opac);
	}
	else if (blending == BLEND_TYPE_SATURATION) {
		node_shader_add_function(shader, str_hue_sat);
		return string_tmp("mix(%s, hsv_to_rgb(vec3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, cola, colb, cola, opac);
	}
	else if (blending == BLEND_TYPE_COLOR) {
		node_shader_add_function(shader, str_hue_sat);
		return string_tmp("mix(%s, hsv_to_rgb(vec3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, colb, colb, cola, opac);
	}
	else { // BlendValue
		node_shader_add_function(shader, str_hue_sat);
		return string_tmp("mix(%s, hsv_to_rgb(vec3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, cola, cola, colb, opac);
	}
}

void make_material_parse_depth_material() {
	if (make_material_depth_scon != NULL) {
		make_material_delete_context(make_material_depth_scon);
	}
	material_t            *sdata = ALLOC_INIT(material_t, {.name = "Material", .canvas = g_context->material->canvas});
	node_shader_context_t *con   = make_depth_run(sdata);
	shader_context_load(con->data);
	make_material_depth_scon = con->data;
	node_shader_context_free(con);
	free(sdata);
}
