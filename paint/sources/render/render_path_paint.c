
#include "../global.h"

static string_array_t *render_path_paint_additional(char *nor, char *pack) {
	static string_array_t ar = {0};
	ar.length                = 0;
	string_array_push(&ar, nor);
	string_array_push(&ar, pack);
	string_array_push(&ar, "texpaint_blend0");
	return &ar;
}

// render_path_paint_stroke.c
extern bool render_path_paint_tail_pending;

// Translucent surfaces are discarded from "main", painting still needs to hit them
static char *render_path_paint_depth() {
	if (paint_exec_replay && paint_replay_depth != NULL) { // From the recorded camera (paint_replay.c)
		return paint_replay_depth;
	}
	return render_path_base_has_paint_depth() ? "main_paint" : "main";
}

// The uv coordinates under the screen points clone and smudge read: the paint depth's uv map (make_depth_run), from the
// recorded camera replaying; gbuffer2 before a paint depth is drawn
char *render_path_paint_uv_target() {
	if (paint_exec_replay && paint_replay_uv != NULL) {
		return paint_replay_uv;
	}
	return render_path_base_has_paint_depth() ? "paint_uv" : "gbuffer2";
}

static bool render_path_paint_uses_gbuffer0() {
	if (g_context->xray || g_config->brush_angle_reject || context_is_decal()) {
		return true;
	}
	if (g_context->brush_mask_image != NULL && (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER)) {
		return true;
	}
	return false;
}

bool             render_path_paint_dilated               = true;
static bool      render_path_paint_dilate_base_pending   = false; // Base dilation deferred during a stroke
static i32       render_path_paint_dilate_frame          = 0;
mesh_object_t   *render_path_paint_painto                = NULL;
mesh_object_t   *render_path_paint_planeo                = NULL;
u8_array_t      *render_path_paint_visibles              = NULL;
bool             render_path_paint_merged_object_visible = false;
f32_array_t     *render_path_paint_saved_ortho           = NULL;
render_target_t *_render_path_paint_texpaint;
render_target_t *_render_path_paint_texpaint_nor;
render_target_t *_render_path_paint_texpaint_pack;
bake_type_t      _render_path_paint_bake_type;

void render_path_paint_init() {

	render_path_add_target("texpaint_blend0", config_get_texture_res_x(), config_get_texture_res_y(), "R16", 1.0); // Stroke coverage, 16 bit so low flow builds up without banding
	render_path_add_target("texpaint_blend1", config_get_texture_res_x(), config_get_texture_res_y(), "R16", 1.0);
	render_path_add_target("texpaint_colorid", 1, 1, "RGBA32", 1.0);
	render_path_add_target("texpaint_colorid_sel", 16, 1, "RGBA32", 1.0); // Colors of the parts picked for an ID mask, see ui_id_mask.c
	render_path_add_target("texpaint_picker", 1, 1, "RGBA32", 1.0);
	render_path_add_target("texpaint_nor_picker", 1, 1, "RGBA32", 1.0);
	render_path_add_target("texpaint_pack_picker", 1, 1, "RGBA32", 1.0);
	render_path_add_target("texpaint_uv_picker", 1, 1, "RGBA32", 1.0);
	render_path_add_target("texpaint_posnortex_picker0", 1, 1, "RGBA128", 1.0);
	render_path_add_target("texpaint_posnortex_picker1", 1, 1, "RGBA128", 1.0);

	render_path_load_shader("Scene/copy_mrt3_pass/copy_mrt3_pass");
	render_path_load_shader("Scene/copy_mrt3_pass/copy_mrt3RGBA64_pass");
	render_path_load_shader("Scene/copy_mrt3_pass/copy_mrt3RGBA128_pass");
	render_path_load_shader("Scene/undo_tiles_pass/undo_tiles_pass");
	render_path_load_shader("Scene/dilate_pass/dilate_pass");
	render_path_load_shader("Scene/dilate_pass/dilateRGBA64_pass");
	render_path_load_shader("Scene/dilate_pass/dilateRGBA128_pass");
	paint_rec_init();
}

// Paint draws write layers: Material Maker buffers the material samples render first when missing (mm_buffers_require),
// at the tiny tier, on the spot: a stroke started before they are done paints at once, util_stroke_refine.c sharpens it
// (and fill layers) as better tiers land. Bakes take full ones. The live brush preview takes what is there.
static bool render_path_paint_live_brush = false;

// A band of rows the paint draw is scissored to (h 0: none): a heavy material rendered whole into a layer (sharpening,
// util_stroke_refine.c) spreads over frames
static i32 render_path_paint_band_y = 0;
static i32 render_path_paint_band_w = 0;
static i32 render_path_paint_band_h = 0;

void render_path_paint_set_band(i32 y, i32 w, i32 h) {
	render_path_paint_band_y = y;
	render_path_paint_band_w = w;
	render_path_paint_band_h = h;
}

static void render_path_paint_draw_material(mesh_object_t *o) {
	i32 need = mm_buffers_require(render_path_paint_live_brush ? 0 : g_context->tool == TOOL_TYPE_BAKE || paint_exec_replay ? MM_BUFFERS_FULL : MM_BUFFERS_TINY);
	if (render_path_paint_band_h > 0) {
		gpu_scissor(0, render_path_paint_band_y, render_path_paint_band_w, render_path_paint_band_h);
	}
	mesh_object_draw(o, make_material_paint_scon, _render_path_bind_params);
	if (render_path_paint_band_h > 0) {
		gpu_disable_scissor();
	}
	mm_buffers_require(need);
}

void render_path_paint_draw_fullscreen_triangle() {
	// Vertices are mangled in vertex shader to form a fullscreen triangle,
	// so plane transform does not matter
	if (make_material_paint_scon != NULL) {
		render_path_paint_draw_material(scene_get_child(".Plane")->ext);
	}
	render_path_end();
}

// The merged mesh is painted unless an object mask selects a single object
mesh_object_t *render_path_paint_target() {
	mesh_object_t *merged = g_context->merged_object;
	if (merged != NULL && merged->base->visible) {
		return merged;
	}
	mesh_object_t *o = g_context->paint_object;
	return o->base->visible && make_material_is_default(o->material) ? o : NULL;
}

// Texture space, the whole mesh is drawn wherever the camera looks
static void render_path_paint_cull(mesh_object_t *o);

void render_path_paint_draw_target() {
	mesh_object_t *o = render_path_paint_target();
	if (o != NULL && make_material_paint_scon != NULL) {
		_mesh_object_last_pipeline = NULL;
		render_path_paint_cull(o);
		render_path_paint_draw_material(o);
	}
	render_path_end();
}

// Fills that pick what they paint (mesh, submesh, face, uv island, angle): the paint object and its triangle under the
// brush, picked once a stroke with the live camera (render_path_paint_fill_pick_now), recorded in the paint state and
// set again by replays (paint_replay.c). Mesh fill draws the object, submesh the triangles connected to the one picked,
// face that triangle, uv island the triangles of its island in the layer's uv map (util_mesh_uv_island_indices: its
// topology, any resolution); angle fill compares normals with the picked triangle's.
mesh_object_t        *render_path_paint_fill_object  = NULL;
i32                   render_path_paint_fill_tri     = -1;
static gpu_buffer_t  *render_path_paint_fill_indices = NULL; // The triangles drawn, NULL: the whole object
static u64            render_path_paint_fill_key     = 0;    // What they are of

bool render_path_paint_fill_picks() {
	fill_type_t t = g_context->fill_type;
	return g_context->tool == TOOL_TYPE_FILL && t != FILL_TYPE_OBJECT;
}

// Fill draws only the picked object: mesh, submesh, face and uv island fills
static bool render_path_paint_fill_draws_object() {
	fill_type_t t = g_context->fill_type;
	return g_context->tool == TOOL_TYPE_FILL && (t == FILL_TYPE_MESH || t == FILL_TYPE_SUBMESH || t == FILL_TYPE_FACE || t == FILL_TYPE_UV_ISLAND);
}

// The fill's object and triangle under the brush, with the live camera: the nearest hit of the visible paint objects
void render_path_paint_fill_pick_now() {
	render_path_paint_fill_pick   = false;
	render_path_paint_fill_object = NULL;
	render_path_paint_fill_tri    = -1;
	ray_t *ray                    = raycast_get_ray(g_context->paint_vec.x * sys_w(), g_context->paint_vec.y * sys_h(), scene_camera); // Where it paints
	f32    best                   = 0.0;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		if (!o->base->visible) {
			continue;
		}
		i32 tri = util_mesh_raycast_triangle(o, ray);
		if (tri < 0) {
			continue;
		}
		f32 d = util_mesh_raycast_triangle_dist(o, ray, tri);
		if (render_path_paint_fill_object == NULL || d < best) {
			render_path_paint_fill_object = o;
			render_path_paint_fill_tri    = tri;
			best                          = d;
		}
	}
}

// The index buffer of the triangles the fill draws, made again when its object, triangle, type or uv map changed
static gpu_buffer_t *render_path_paint_fill_triangles() {
	mesh_object_t *o   = render_path_paint_fill_object;
	fill_type_t    t   = g_context->fill_type;
	i32            map = g_context->layer->uv_map;
	u64            key = (u64)(uintptr_t)o ^ ((u64)(u32)render_path_paint_fill_tri << 20) ^ ((u64)t << 52) ^ ((u64)map << 60) ^ (u64)(uintptr_t)o->data;
	if (key == render_path_paint_fill_key && render_path_paint_fill_indices != NULL) {
		return render_path_paint_fill_indices;
	}
	if (render_path_paint_fill_indices != NULL) {
		gpu_delete_buffer(render_path_paint_fill_indices);
		render_path_paint_fill_indices = NULL;
	}
	render_path_paint_fill_key = key;
	i32          tri           = render_path_paint_fill_tri;
	u32_array_t *indices       = NULL;
	if (t == FILL_TYPE_SUBMESH) {
		indices = util_mesh_connected_indices(o->data, tri);
	}
	else if (t == FILL_TYPE_UV_ISLAND) {
		indices = util_mesh_uv_island_indices(o->data, tri, map);
	}
	else if (t == FILL_TYPE_FACE && tri >= 0 && (tri + 1) * 3 <= (i32)o->data->index_array->length) {
		indices = u32_array_create(0);
		for (i32 k = 0; k < 3; ++k) {
			u32_array_push(indices, o->data->index_array->buffer[tri * 3 + k]);
		}
	}
	if (indices == NULL) {
		return NULL;
	}
	render_path_paint_fill_indices = gpu_create_index_buffer(indices->length);
	mesh_data_build_indices(render_path_paint_fill_indices, indices);
	array_delete(indices);
	return render_path_paint_fill_indices;
}

// Mesh fill draws the picked paint object, the other fills of the object some of its triangles
static void render_path_paint_draw_fill_object() {
	mesh_object_t *o       = render_path_paint_fill_object;
	mesh_object_t *target  = render_path_paint_target();
	bool           merged  = target != NULL && target == g_context->merged_object;
	bool           painted = o != NULL && (merged || o == target) && array_index_of(g_project->_->paint_objects, o) >= 0;
	gpu_buffer_t  *subset  = NULL;
	if (painted && g_context->fill_type != FILL_TYPE_MESH) {
		subset  = render_path_paint_fill_triangles();
		painted = subset != NULL;
	}
	if (painted && o->base->visible && make_material_is_default(o->material) && make_material_paint_scon != NULL) {
		_mesh_object_last_pipeline = NULL;
		gpu_buffer_t *indices      = o->data->_->index_buffer;
		if (subset != NULL) {
			o->data->_->index_buffer = subset;
		}
		render_path_paint_draw_material(o);
		o->data->_->index_buffer = indices;
	}
	render_path_end();
}

// A particle's draw, the op executed (paint_exec_particles): the coverage so far copied already
void render_path_paint_commands_particle(i32 tid, char *texpaint, bool is_mask) {
	if (is_mask) {
		tid = g_context->layer->parent->id;
		if (slot_layer_is_group(g_context->layer->parent)) {
			for (i32 i = 0; i < slot_layer_get_children(g_context->layer->parent)->length; ++i) {
				slot_layer_t *c = slot_layer_get_children(g_context->layer->parent)->buffer[i];
				tid             = c->id;
				break;
			}
		}
	}
	render_path_set_target(texpaint, render_path_paint_additional(string_tmp("texpaint_nor%d", tid), string_tmp("texpaint_pack%d", tid)), NULL,
	                       GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target(render_path_paint_depth(), "gbufferD");
	if (render_path_paint_uses_gbuffer0()) {
		render_path_bind_target("gbuffer0", "gbuffer0");
	}
	render_path_bind_target("texpaint_blend1", "paintmask");
	render_path_paint_draw_target();
}

// Scratch targets the size of the layer painted
// The size of the target painted: the layer's, a replay's own (paint_replay.c swaps it in)
static void render_path_paint_ref_target(char *name, char *format) {
	i32            w = config_get_texture_res_x();
	i32            h = config_get_texture_res_y();
	gpu_texture_t *t = g_context->layer != NULL ? render_path_target_image(string_tmp("texpaint%d", g_context->layer->id)) : NULL;
	if (t != NULL) {
		w = t->width;
		h = t->height;
	}
	render_path_ensure_target(name, w, h, format);
}

// The stroke coverage targets at w x h, the size of the layer painted
void render_path_paint_scratch_resize(i32 w, i32 h) {
	char *names[] = {"texpaint_blend0", "texpaint_blend1"};
	for (i32 i = 0; i < 2; ++i) {
		render_target_t *rt = any_map_get(render_path_render_targets, names[i]);
		if (rt->_image != NULL && rt->width == w && rt->height == h) {
			continue;
		}
		gpu_delete_texture(rt->_image);
		rt->width  = w;
		rt->height = h;
		rt->_image = gpu_create_render_target(w, h, GPU_TEXTURE_FORMAT_R16);
	}
	g_context->brush_blend_dirty = true;
}

// Frees the full size scratch targets, they are created again when needed
void render_path_paint_release_scratch() {
	char *names[] = {"texpaint_ref", "texpaint_nor_ref", "texpaint_pack_ref"};
	for (i32 i = 0; i < 3; ++i) {
		render_path_remove_target(names[i]);
	}
	if (layers_temp_image != NULL) {
		render_path_remove_target("temptex0");
		layers_temp_image = NULL;
	}
}

static i32 render_path_paint_ref_frame = -1; // Blur snapshot once per frame, the dabs of a frame blur the same source
bool       render_path_paint_ref_taken = false; // A snapshot was taken (recorded with the draw: its replay takes it again)

// The next blur draw takes its snapshot again (a replayed draw that took one live)
void render_path_paint_ref_invalidate() {
	render_path_paint_ref_frame = -1;
}

static i32 render_path_paint_ref_tid   = -1;

static void render_path_paint_ref_snapshot(i32 tid, bool is_mask) {
	if (render_path_paint_ref_frame == render_path_paint_dilate_frame && render_path_paint_ref_tid == tid &&
	    any_map_get(render_path_render_targets, "texpaint_ref") != NULL) {
		return;
	}
	render_path_paint_ref_frame = render_path_paint_dilate_frame;
	render_path_paint_ref_tid   = tid;
	render_path_paint_ref_taken = true;
	if (is_mask) {
		render_path_paint_ref_target("texpaint_ref", "RGBA32");
		render_path_set_target("texpaint_ref", NULL, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0);
		render_path_bind_target(string_tmp("texpaint%d", tid), "tex");
		render_path_draw_shader("Scene/copy_pass/copy_pass");
		return;
	}
	char *format = gpu_texture_format_name(layers_format(base_bits));
	render_path_paint_ref_target("texpaint_ref", format);
	render_path_paint_ref_target("texpaint_nor_ref", format);
	render_path_paint_ref_target("texpaint_pack_ref", format);
	string_array_t *additional = any_array_create_from_raw_tmp(
	    (void *[]){
	        "texpaint_nor_ref",
	        "texpaint_pack_ref",
	    },
	    2);
	render_path_set_target("texpaint_ref", additional, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0);
	render_path_bind_target(string_tmp("texpaint%d", tid), "tex0");
	render_path_bind_target(string_tmp("texpaint_nor%d", tid), "tex1");
	render_path_bind_target(string_tmp("texpaint_pack%d", tid), "tex2");
	char *pipe = string_equals(format, "RGBA32") ? "copy_mrt3_pass" : string_equals(format, "RGBA64") ? "copy_mrt3RGBA64_pass" : "copy_mrt3RGBA128_pass";
	render_path_draw_shader(string_tmp("Scene/copy_mrt3_pass/%s", pipe));
}

// Clone source for the visible layers mode
void render_path_paint_clone_snapshot() {
	layers_flatten(false, NULL);
	char *format = gpu_texture_format_name(layers_format(base_bits));
	render_path_paint_ref_target("texpaint_ref", format);
	render_path_paint_ref_target("texpaint_nor_ref", format);
	render_path_paint_ref_target("texpaint_pack_ref", format);
	string_array_t *additional = any_array_create_from_raw_tmp(
	    (void *[]){
	        "texpaint_nor_ref",
	        "texpaint_pack_ref",
	    },
	    2);
	render_path_set_target("texpaint_ref", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("expa", "tex0");
	render_path_bind_target("expb", "tex1");
	render_path_bind_target("expc", "tex2");
	char *pipe = string_equals(format, "RGBA32") ? "copy_mrt3_pass" : string_equals(format, "RGBA64") ? "copy_mrt3RGBA64_pass" : "copy_mrt3RGBA128_pass";
	render_path_draw_shader(string_tmp("Scene/copy_mrt3_pass/%s", pipe));
}

#define RENDER_PATH_PAINT_BATCH_MAX 64 // Dabs of a batched draw

// Whether the paint shader in use reads resolved dabs (make_paint_resolved), its draws go through the paint executor
bool render_path_paint_resolved() {
	shader_context_t *scon = make_material_paint_scon;
	if (scon == NULL || scon->constants == NULL) {
		return false;
	}
	for (i32 i = 0; i < scon->constants->length; ++i) {
		char *link = ((shader_const_t *)scon->constants->buffer[i])->link;
		if (link != NULL && string_equals(link, "_paint_dab_first")) {
			return true;
		}
	}
	return false;
}

// Whether the paint shader in use paints batches of dabs. Asked of the shader itself, not make_paint_batched: previews build
// other paint shaders and restore the saved one (make_material_restore_paint_material), which leaves that flag stale.
bool render_path_paint_batched() { // Its dab count: shaders reading resolved dabs read _texpaintdabs batched or not
	shader_context_t *scon = make_material_paint_scon;
	if (scon == NULL || scon->constants == NULL) {
		return false;
	}
	for (i32 i = 0; i < scon->constants->length; ++i) {
		char *link = ((shader_const_t *)scon->constants->buffer[i])->link;
		if (link != NULL && string_equals(link, "_paint_dab_count")) {
			return true;
		}
	}
	return false;
}

// Only the parts of the mesh near the dabs of this draw, see paint_cull.c
static void render_path_paint_cull(mesh_object_t *o) {
	tool_type_t      tool = g_context->tool;
	paint_exec_op_t *op   = paint_exec_current();
	if ((tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_BLUR) || g_context->paint2d ||
	    context_is_decal() || g_context->xray || context_is_pixel_snap() || context_is_brush_camera_align() || g_context->pick_pos_nor_tex ||
	    g_context->layer->fill != NULL || op == NULL) {
		return;
	}
	static paint_cull_dab_t dabs[RENDER_PATH_PAINT_BATCH_MAX];
	i32                     count   = 0;
	f32                     stretch = g_context->brush_tilt_shading ? 3.0 : 1.0;
	for (i32 i = 0; i < op->dab_count && i < RENDER_PATH_PAINT_BATCH_MAX; ++i) { // The dabs of the draw being executed
		f32 *d = paint_exec_dab(op->dab_first + i);
		if (!paint_exec_replay && d[4] != PAINT_DAB_MODE_BRUSH) {
			continue; // A decal anchor
		}
		dabs[count++] = (paint_cull_dab_t){d[16] * 2.0 - 1.0, 1.0 - d[17] * 2.0, d[18] * 2.0 - 1.0, 1.0 - d[19] * 2.0, d[12] * stretch};
	}
	paint_cull_draw_next(o, dabs, count);
}

static void render_path_paint_commands_paint_internal(bool dilation);

// IRON_PAINT_TIMING: the GPU time of the paint draws apart from the rest of the frame. The work before them and the
// draws go in command buffers of their own, the draws' GPU time is logged when they complete (with IRON_FRAME_TIMING,
// on the same clock as the frames).
static i32 render_path_paint_timing = -1;

typedef struct {
	f64 t;
	f64 cpu;
	i32 dabs;
} render_path_paint_timing_t;

static void render_path_paint_timing_done(void *data, f64 gpu_ms) {
	render_path_paint_timing_t *p = data;
	fprintf(stderr, "[paint] t %.1f dabs %d cpu %.2f gpu %.2f ms\n", p->t, p->dabs, p->cpu, gpu_ms);
	free(p);
}

static void render_path_paint_timing_flush() { // The open pass ends: what was drawn so far can be committed
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
}

// A paint draw of the stroke model, an op of the paint executor (paint_exec.c): it waits for the end of the open scope
void render_path_paint_commands_paint_stroke(bool dilation) {
	paint_exec_draw(dilation);
}

// A paint draw from outside the stroke model (fill layers, bakes, paths, ...): executed now, isolated (paint_exec.c)
void render_path_paint_commands_paint(bool dilation) {
	paint_exec_isolate_begin();
	paint_exec_draw(dilation);
	paint_exec_isolate_end();
}

// The draw itself, the paint executor's: the brush state from the uniforms, while it executes an op the op's
void render_path_paint_commands_paint_now(bool dilation) {
	bool exec = paint_exec_current() != NULL;
	if (g_context->pdirty <= 0) {
		return;
	}
	material_cache_paint_check(); // A paint shader reading a material cache that no longer fits paints live
	if (slot_layer_is_mask(g_context->layer)) {
		g_context->layer->mask_stack_dirty = true; // The paint effect changes
	}
	mask_stack_layer_changed(g_context->layer); // Masks anchoring it follow the stroke
	if (render_path_paint_timing < 0) {
		render_path_paint_timing = getenv("IRON_PAINT_TIMING") != NULL ? 1 : 0;
	}
	if (!render_path_paint_timing || render_path_paint_band_h > 0 || exec) { // A band: its caller times it, the executor its ops
		render_path_paint_commands_paint_internal(dilation);
		return;
	}
	render_path_paint_timing_t *p = malloc(sizeof(render_path_paint_timing_t));
	p->t                          = iron_glsl_now_ms();
	p->dabs                       = 1;
	render_path_paint_timing_flush();
	if (!gpu_in_use) {
		gpu_commit(NULL, NULL);
	}
	render_path_paint_commands_paint_internal(dilation);
	render_path_paint_timing_flush();
	p->cpu = iron_glsl_now_ms() - p->t;
	if (gpu_in_use) { // Inside a pass of the caller: no time of its own
		p->cpu = -p->cpu;
		render_path_paint_timing_done(p, -1.0);
		return;
	}
	gpu_commit(&render_path_paint_timing_done, p);
}

static void render_path_paint_commands_paint_internal(bool dilation) {
	i32 tid = g_context->layer->id;

	if (g_context->pdirty <= 0) {
		return;
	}

	if (g_context->brush_time == 0 && g_context->tool != TOOL_TYPE_PARTICLE && !g_context->pick_pos_nor_tex && !paint_exec_replay) {
		g_context->brush_blend_dirty = true;
	}

	if (g_context->tool == TOOL_TYPE_COLORID) {
		render_path_set_target("texpaint_colorid", NULL, NULL, GPU_CLEAR_COLOR, 0xff000000, 0.0);
		render_path_bind_target("gbuffer2", "gbuffer2");
		render_path_paint_draw_fullscreen_triangle();
		buffer_t *a = gpu_get_texture_pixels_rgba8(render_path_target_image("texpaint_colorid"));
		if (ui_id_mask_pick_pending) {
			ui_id_mask_pick_pending = false;
			ui_id_mask_toggle(((u32)buffer_get_u8(a, 0) << 16) | ((u32)buffer_get_u8(a, 1) << 8) | (u32)buffer_get_u8(a, 2));
		}
	}
	else if (g_context->tool == TOOL_TYPE_PICKER) {
		if (g_context->pick_pos_nor_tex) {
			if (g_context->paint2d) {
				string_array_t *additional = any_array_create_from_raw_tmp(
				    (void *[]){
				        "gbuffer1",
				        "gbuffer2",
				    },
				    2);
				render_path_set_target("gbuffer0", additional, "main", GPU_CLEAR_NONE, 0, 0.0);
				render_path_base_draw_meshes(render_path_base_mesh_scon(0), true);
			}
			string_array_t *additional = any_array_create_from_raw_tmp(
			    (void *[]){
			        "texpaint_posnortex_picker1",
			    },
			    1);
			render_path_set_target("texpaint_posnortex_picker0", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("gbuffer2", "gbuffer2");
			render_path_bind_target("main", "gbufferD");
			render_path_paint_draw_fullscreen_triangle();
			gpu_texture_t *texpaint_posnortex_picker0 = render_path_target_image("texpaint_posnortex_picker0");
			gpu_texture_t *texpaint_posnortex_picker1 = render_path_target_image("texpaint_posnortex_picker1");
			if (util_render_pick_transfer != NULL) { // Read back without waiting (util_render_pick_pos_nor_tex_async)
				if (_render_path_current_image != NULL) {
					render_path_end();
				}
				gpu_transfer_map(util_render_pick_transfer, 32);
				gpu_copy_regions_begin();
				gpu_copy_region_to_transfer(util_render_pick_transfer, texpaint_posnortex_picker0, 0, 0, 1, 1, 0);
				gpu_copy_region_to_transfer(util_render_pick_transfer, texpaint_posnortex_picker1, 0, 0, 1, 1, 16);
				gpu_copy_regions_end();
				return;
			}
			buffer_t        *a                          = gpu_get_texture_pixels(texpaint_posnortex_picker0);
			buffer_t        *b                          = gpu_get_texture_pixels(texpaint_posnortex_picker1);
			g_context->posx_picked                      = buffer_get_f32(a, 0);
			g_context->posy_picked                      = buffer_get_f32(a, 4);
			g_context->posz_picked                      = buffer_get_f32(a, 8);
			g_context->uvx_picked                       = buffer_get_f32(a, 12);
			g_context->norx_picked                      = buffer_get_f32(b, 0);
			g_context->nory_picked                      = buffer_get_f32(b, 4);
			g_context->norz_picked                      = buffer_get_f32(b, 8);
			g_context->uvy_picked                       = buffer_get_f32(b, 12);
		}
		else {
			tid          = g_context->layer->id;
			bool is_mask = slot_layer_is_mask(g_context->layer);
			if (is_mask) {
				string_array_t *additional = any_array_create_from_raw_tmp(
				    (void *[]){
				        "texpaint_uv_picker",
				    },
				    1);
				render_path_set_target("texpaint_picker", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
				render_path_bind_target("gbuffer2", "gbuffer2");
				render_path_bind_target(string_tmp("texpaint%d", tid), "texpaint");
				render_path_paint_draw_fullscreen_triangle();
			}
			else {
				string_array_t *additional = any_array_create_from_raw_tmp(
				    (void *[]){
				        "texpaint_nor_picker",
				        "texpaint_pack_picker",
				        "texpaint_uv_picker",
				    },
				    3);
				render_path_set_target("texpaint_picker", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
				render_path_bind_target("gbuffer2", "gbuffer2");
				if (make_picking_visible()) { // What shows (make_picking.c)
					render_path_bind_target("gbuffer0", "gbuffer0");
					render_path_bind_target("gbuffer1", "gbuffer1");
					render_path_bind_target("main", "gbufferD");
					render_path_bind_target(string_tmp("texpaint_nor%d", tid), "texpaint_nor");
				}
				else {
					render_path_bind_target(string_tmp("texpaint%d", tid), "texpaint");
					render_path_bind_target(string_tmp("texpaint_nor%d", tid), "texpaint_nor");
					render_path_bind_target(string_tmp("texpaint_pack%d", tid), "texpaint_pack");
				}
				render_path_paint_draw_fullscreen_triangle();
			}
			render_path_paint_picker_read(is_mask);
		}
	}
	else {
		bool mesh_fill = render_path_paint_fill_draws_object();
		char *texpaint = string_tmp("texpaint%d", tid);
		if (g_context->tool == TOOL_TYPE_BAKE && g_context->brush_time == sys_delta()) {
			// Clear to black on bake start
			render_path_set_target(texpaint, NULL, NULL, GPU_CLEAR_COLOR, 0xff000000, 0.0);
		}

		render_path_set_target("texpaint_blend1", NULL, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0); // Overwritten whole
		render_path_bind_target("texpaint_blend0", "tex");
		render_path_draw_shader("Scene/copy_pass/copyR16_pass");
		bool is_mask = slot_layer_is_mask(g_context->layer);

		// Read texcoords from gbuffer
		bool read_tc = g_context->tool == TOOL_TYPE_CLONE ||
		               g_context->tool == TOOL_TYPE_BLUR || context_is_pixel_snap();

		if (g_context->tool == TOOL_TYPE_BLUR) {
			render_path_paint_ref_snapshot(tid, is_mask);
		}

		if (g_context->tool == TOOL_TYPE_PARTICLE) {
			render_path_paint_commands_particle(tid, texpaint, is_mask);
		}
		else {
			if (is_mask) {
				i32 ptid = g_context->layer->parent->id;
				if (slot_layer_is_group(g_context->layer->parent)) { // Group mask
					for (i32 i = 0; i < slot_layer_get_children(g_context->layer->parent)->length; ++i) {
						slot_layer_t *c = slot_layer_get_children(g_context->layer->parent)->buffer[i];
						ptid            = c->id;
						break;
					}
				}
				render_path_set_target(texpaint, render_path_paint_additional(string_tmp("texpaint_nor%d", ptid), string_tmp("texpaint_pack%d", ptid)), NULL,
				                       GPU_CLEAR_NONE, 0, 0.0);
			}
			else {
				render_path_set_target(texpaint, render_path_paint_additional(string_tmp("texpaint_nor%d", tid), string_tmp("texpaint_pack%d", tid)), NULL,
				                       GPU_CLEAR_NONE, 0, 0.0);
			}
			render_path_bind_target(render_path_paint_depth(), "gbufferD");
			if (render_path_paint_uses_gbuffer0()) {
				render_path_bind_target("gbuffer0", "gbuffer0");
			}
			render_path_bind_target("texpaint_blend1", "paintmask");
			if (read_tc) {
				render_path_bind_target(render_path_paint_uv_target(), "gbuffer2");
			}
			if (mesh_fill) {
				render_path_paint_draw_fill_object();
			}
			else {
				render_path_paint_draw_target();
			}
		}

		if (g_context->tool == TOOL_TYPE_BAKE && g_context->bake_type == BAKE_TYPE_CURVATURE && g_context->bake_curv_smooth > 0) {
			if (any_map_get(render_path_render_targets, "texpaint_blur") == NULL) {
				render_path_add_target("texpaint_blur", math_floor(config_get_texture_res_x() * 0.95), math_floor(config_get_texture_res_y() * 0.95), "RGBA32", 1.0);
			}
			i32 blurs = math_round(g_context->bake_curv_smooth);
			for (i32 i = 0; i < blurs; ++i) {
				render_path_set_target("texpaint_blur", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
				render_path_bind_target(texpaint, "tex");
				render_path_draw_shader("Scene/copy_pass/copy_pass");
				render_path_set_target(texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
				render_path_bind_target("texpaint_blur", "tex");
				render_path_draw_shader("Scene/copy_pass/copy_pass");
			}
		}

		if (dilation) {
			render_path_paint_dilate_after_paint();
		}
	}
}

// Map keys stay allocated, a name is kept once instead of allocating it on every call
static char *render_path_paint_key(char *format, i32 id) {
	static any_map_t *keys = NULL;
	if (keys == NULL) {
		keys = any_map_create();
	}
	char *tmp = string_tmp(format, id);
	char *key = any_map_get(keys, tmp);
	if (key == NULL) {
		key = string_copy(tmp);
		any_map_set(keys, key, key);
	}
	return key;
}

void render_path_paint_use_live_layer(bool use) {
	slot_layer_t *l   = g_context->layer;
	i32           tid = l->id;
	if (use) {
		_render_path_paint_texpaint      = any_map_get(render_path_render_targets, string_tmp("texpaint%d", tid));
		_render_path_paint_texpaint_nor  = any_map_get(render_path_render_targets, string_tmp("texpaint_nor%d", tid));
		_render_path_paint_texpaint_pack = any_map_get(render_path_render_targets, string_tmp("texpaint_pack%d", tid));
		history_tiles_source_set(l->texpaint, l->texpaint_nor, l->texpaint_pack); // The preview blends over the layer as it is
		any_map_set(render_path_render_targets, render_path_paint_key("texpaint%d", tid), any_map_get(render_path_render_targets, "texpaint_live"));
		if (slot_layer_is_layer(l)) {
			any_map_set(render_path_render_targets, render_path_paint_key("texpaint_nor%d", tid), any_map_get(render_path_render_targets, "texpaint_nor_live"));
			any_map_set(render_path_render_targets, render_path_paint_key("texpaint_pack%d", tid), any_map_get(render_path_render_targets, "texpaint_pack_live"));
		}
	}
	else {
		history_tiles_source_set(NULL, NULL, NULL);
		any_map_set(render_path_render_targets, render_path_paint_key("texpaint%d", tid), _render_path_paint_texpaint);
		if (slot_layer_is_layer(l)) {
			any_map_set(render_path_render_targets, render_path_paint_key("texpaint_nor%d", tid), _render_path_paint_texpaint_nor);
			any_map_set(render_path_render_targets, render_path_paint_key("texpaint_pack%d", tid), _render_path_paint_texpaint_pack);
		}
	}
	render_path_paint_live_layer_locked = use;
}

bool render_path_paint_symmetry_on() {
	return g_context->sym_x || g_context->sym_y || g_context->sym_z || g_context->sym_radial > 1;
}

// Mirrored and radial copies of the stroke: the paint object is drawn transformed, texels whose transformed position falls in the brush get
// painted. Copies turn around and mirror about the object origin or the center of the mesh bounds.
void render_path_paint_commands_symmetry() {
	if (!render_path_paint_symmetry_on() || g_context->pdirty <= 0) { // The mirrored copies only draw while painting
		return;
	}
	g_context->ddirty = 2;
	transform_t *t    = g_context->paint_object->base->transform;
	vec4_t       loc0 = t->loc;
	quat_t       rot0 = t->rot;
	vec4_t       s0   = t->scale;

	vec4_t c = (vec4_t){0.0, 0.0, 0.0, 0.0};
	if (g_context->sym_center && g_context->paint_object->data != NULL) {
		// Bounds of the mesh, cached, scanning every vertex for every draw is slow on dense meshes
		static mesh_data_t *cached_data   = NULL;
		static i32          cached_length = -1;
		static vec4_t       cached_center;
		mesh_data_t        *data   = g_context->paint_object->data;
		i32                 length = data->vertex_arrays != NULL && data->vertex_arrays->length > 0 ? data->vertex_arrays->buffer[0]->values->length : 0;
		if (data != cached_data || length != cached_length) {
			vec4_t bmin;
			vec4_t bmax;
			mesh_data_calculate_aabb_min_max(data, &bmin, &bmax);
			cached_center = (vec4_t){(bmin.x + bmax.x) * 0.5, (bmin.y + bmax.y) * 0.5, (bmin.z + bmax.z) * 0.5, 0.0};
			cached_data   = data;
			cached_length = length;
		}
		c = cached_center;
	}
	vec4_t sc = (vec4_t){c.x * s0.x, c.y * s0.y, c.z * s0.z, 0.0}; // Center in scaled object space

	paint_exec_begin(); // The copies resolve their dabs together, with the paint object back in place
	i32    radial = g_context->sym_radial > 1 ? g_context->sym_radial : 1;
	vec4_t axis   = g_context->sym_radial_axis == 0   ? (vec4_t){1.0, 0.0, 0.0, 0.0}
	                : g_context->sym_radial_axis == 1 ? (vec4_t){0.0, 1.0, 0.0, 0.0}
	                                                  : (vec4_t){0.0, 0.0, 1.0, 0.0};
	for (i32 k = 0; k < radial; ++k) {
		quat_t q = quat_from_axis_angle(axis, (math_pi() * 2.0 * k) / radial);
		for (i32 m = 0; m < 8; ++m) { // Mirror axes, bit 0 x, 1 y, 2 z
			if (((m & 1) && !g_context->sym_x) || ((m & 2) && !g_context->sym_y) || ((m & 4) && !g_context->sym_z)) {
				continue;
			}
			if (k == 0 && m == 0) {
				continue; // The stroke itself
			}
			vec4_t f = (vec4_t){(m & 1) ? -1.0 : 1.0, (m & 2) ? -1.0 : 1.0, (m & 4) ? -1.0 : 1.0, 0.0};
			// v' = c + Q F (v - c): mirror then turn around the center
			vec4_t fc  = vec4_apply_quat((vec4_t){f.x * sc.x, f.y * sc.y, f.z * sc.z, 0.0}, q);
			vec4_t off = vec4_apply_quat((vec4_t){sc.x - fc.x, sc.y - fc.y, sc.z - fc.z, 0.0}, rot0);
			t->loc     = (vec4_t){loc0.x + off.x, loc0.y + off.y, loc0.z + off.z, loc0.w};
			t->rot     = quat_mult(rot0, q);
			t->scale   = (vec4_t){s0.x * f.x, s0.y * f.y, s0.z * f.z, s0.w};
			transform_build_matrix(t);
			render_path_paint_commands_paint_stroke(false);
		}
	}
	t->loc   = loc0;
	t->rot   = rot0;
	t->scale = s0;
	transform_build_matrix(t);
	paint_exec_end();
}

// The live layer becomes a copy of the selected layer, made when there is none
void render_path_paint_live_layer_copy() {
	if (render_path_paint_live_layer == NULL) {
		render_path_paint_live_layer = slot_layer_create("_live", LAYER_SLOT_TYPE_LAYER, NULL);
	}
	render_path_paint_live_layer_used();

	i32 tid = g_context->layer->id;
	if (slot_layer_is_mask(g_context->layer)) {
		render_path_set_target("texpaint_live", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string_tmp("texpaint%d", tid), "tex");
		render_path_draw_shader("Scene/copy_pass/copy_pass");
	}
	else {
		string_array_t *additional = any_array_create_from_raw_tmp(
		    (void *[]){
		        "texpaint_nor_live",
		        "texpaint_pack_live",
		    },
		    2);
		render_path_set_target("texpaint_live", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string_tmp("texpaint%d", tid), "tex0");
		render_path_bind_target(string_tmp("texpaint_nor%d", tid), "tex1");
		render_path_bind_target(string_tmp("texpaint_pack%d", tid), "tex2");
		render_path_draw_shader("Scene/copy_mrt3_pass/copy_mrt3_pass");
	}
}

void render_path_paint_commands_live_brush() {
	tool_type_t tool = g_context->tool;
	if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_DECAL && tool != TOOL_TYPE_TEXT &&
	    tool != TOOL_TYPE_BLUR) {
		return;
	}

	if (render_path_paint_live_layer_locked || ui_brush_creator_shown()) { // The drawing pad paints its strokes in the live layer
		return;
	}

	if (make_material_paint_pending()) { // The material just picked compiles: its preview once it is ready, not the previous one's
		return;
	}

	render_path_paint_live_layer_copy();
	render_path_paint_use_live_layer(true);

	render_path_paint_live_layer_drawn = 2;

	ui_view2d_hwnd->redraws     = 2;
	f32 _x                      = g_context->paint_vec.x;
	f32 _y                      = g_context->paint_vec.y;
	f32 _last_x                 = g_context->last_paint_vec_x;
	f32 _last_y                 = g_context->last_paint_vec_y;
	i32 _pdirty                 = g_context->pdirty;
	g_context->last_paint_vec_x = g_context->paint_vec.x;
	g_context->last_paint_vec_y = g_context->paint_vec.y;
	if (keymap_shortcut(any_map_get(g_keymap, "brush_ruler"), SHORTCUT_TYPE_STARTED)) {
		g_context->last_paint_vec_x = g_context->last_paint_x;
		g_context->last_paint_vec_y = g_context->last_paint_y;
	}
	g_context->pdirty = 2;

	render_path_paint_commands_symmetry();
	render_path_paint_live_brush = true; // A preview: Material Maker buffers as they are, better ones redraw it
	render_path_paint_commands_paint_stroke(true);
	render_path_paint_live_brush = false;

	render_path_paint_use_live_layer(false);

	g_context->paint_vec.x       = _x;
	g_context->paint_vec.y       = _y;
	g_context->last_paint_vec_x  = _last_x;
	g_context->last_paint_vec_y  = _last_y;
	g_context->pdirty            = _pdirty;
	g_context->brush_blend_dirty = true;
}

bool render_path_paint_paint_enabled() {
	if (g_context->tool == TOOL_TYPE_BAKE) {
		return true;
	}

	if (g_context->tool == TOOL_TYPE_CURSOR) {
		return true;
	}

	bool fill_layer = g_context->layer->fill != NULL && g_context->tool != TOOL_TYPE_PICKER &&
	                  g_context->tool != TOOL_TYPE_COLORID;
	bool group_layer = slot_layer_is_group(g_context->layer);

	// Skip paint if material references this layer and wants to paint into it at the same time
	bool               self_in_material = false;
	i32                layer_i          = array_index_of(g_project->_->layers, g_context->layer);
	ui_node_t_array_t *nodes            = g_context->material->canvas->nodes;
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, "LAYER") && (i32)n->buttons->buffer[0]->default_value->buffer[0] == layer_i) {
			self_in_material = true;
			break;
		}
	}

	return !fill_layer && !group_layer && !g_context->foreground_event && !self_in_material;
}

void render_path_paint_begin() {
	render_path_paint_picker_poll();
	if (render_path_paint_live_layer_drawn > 0) {
		render_path_paint_live_layer_drawn--;
	}

	render_path_paint_live_layer_release_idle();

	// Dilation fills the gutter around uv islands, six full size passes: during a stroke only every few frames,
	// always once the stroke ended
	render_path_paint_dilate_frame++;
	bool stroke = g_context->brush_time > 0;
	bool due    = !stroke || render_path_paint_dilate_frame % 6 == 0;
	if (due && (!render_path_paint_dilated || render_path_paint_dilate_base_pending)) {
		paint_exec_dilate(render_path_paint_dilate_base_pending, !render_path_paint_dilated);
		render_path_paint_dilated             = true;
		render_path_paint_dilate_base_pending = false;
	}

	if (!render_path_paint_paint_enabled()) {
		return;
	}

	// Painting with the paint shader of a material just picked, still compiling on a thread: waits for it (strokes
	// waited in util_brush_update already, this is for other paint)
	if (g_context->pdirty > 0 && make_material_paint_pending()) {
		make_material_paint_ready(-1.0);
	}

	render_path_paint_push_undo_last = history_push_undo;

	if (history_push_undo && history_steps != NULL) {
		history_paint();
		if (g_context->tool == TOOL_TYPE_CLONE && g_context->clone_layers == CLONE_LAYERS_VISIBLE) {
			paint_exec_clone_flatten();
		}
	}

	if (g_context->paint2d) {
		render_path_paint_set_plane_mesh();
	}

	util_curve_stroke_render(); // A curve being edited, painted again when it changed

	if (g_config->brush_live && g_context->pdirty <= 0 && g_context->ddirty <= 0 && g_context->brush_time == 0) {
		// Depth is unchanged, draw before gbuffer gets updated
		render_path_paint_commands_live_brush();
	}
}

void render_path_paint_end() {
	g_context->ddirty--;

	g_context->last_paint_vec_x = g_context->paint_vec.x;
	g_context->last_paint_vec_y = g_context->paint_vec.y;

	if (!render_path_paint_paint_enabled()) {
		return;
	}

	if (g_context->pdirty > 0) {
		if (slot_layer_is_mask(g_context->layer)) {
			g_context->layer->mask_stack_dirty = true; // The paint effect changed
		}
		layers_update_linked_layers();
	}

	g_context->pdirty--;
}

static void render_path_paint_commands_low_poly() {
	mesh_object_t *merged = g_context->merged_object;
	if (merged == NULL || !merged->base->visible) {
		render_path_paint_commands_paint_stroke(true);
		return;
	}
	mesh_object_t *high_poly = g_project->_->paint_objects->buffer[g_context->bake_high_poly];
	i32            last      = -1;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p != high_poly && p->base->visible) {
			last = i;
		}
	}
	if (last < 0) { // The high poly is the only mesh, it bakes onto itself
		render_path_paint_commands_paint_stroke(true);
		return;
	}
	mesh_object_t *_paint_object = g_context->paint_object;
	merged->base->visible        = false;
	for (i32 i = 0; i <= last; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p != high_poly && p->base->visible) {
			context_select_paint_object(p);
			render_path_paint_commands_paint_stroke(i == last); // Dilate once all objects are drawn
		}
	}
	merged->base->visible = true;
	context_select_paint_object(_paint_object);
}

static void render_path_paint_bake_final() {
	g_context->bake_type = _render_path_paint_bake_type;
	make_material_parse_paint_material(false);
	g_context->pdirty = 1;
	render_path_paint_commands_low_poly();
	g_context->pdirty        = 0;
	render_path_paint_baking = false;
}

static void render_path_paint_bake_deriv_on_next_frame(void *_) {
	render_path_paint_bake_final();
}

static void render_path_paint_bake_deriv() {
	g_context->bake_type = BAKE_TYPE_HEIGHT;
	make_material_parse_paint_material(false);
	g_context->pdirty = 1;
	render_path_paint_commands_low_poly();
	g_context->pdirty = 0;
	if (render_path_paint_push_undo_last) {
		history_paint();
	}
	sys_notify_on_next_frame(&render_path_paint_bake_deriv_on_next_frame, NULL);
}

bool render_path_paint_is_rt_bake() {
	return (g_context->bake_type == BAKE_TYPE_OCCLUSION || g_context->bake_type == BAKE_TYPE_LIGHTMAP || g_context->bake_type == BAKE_TYPE_BENT_NORMAL ||
	        g_context->bake_type == BAKE_TYPE_THICKNESS);
}

void render_path_paint_draw_bake(void *_) {
	render_path_paint_bake_final();
}

void render_path_paint_draw_bake_derivative(void *_) {
	render_path_paint_bake_deriv();
}

void render_path_paint_clear_blend_stroke() {
	paint_exec_clear_blend();
}

// From outside the stroke model: now, isolated
void render_path_paint_clear_blend() {
	paint_exec_isolate_begin();
	paint_exec_clear_blend();
	paint_exec_isolate_end();
}

void render_path_paint_clear_blend_now() {
	if (g_context->brush_blend_dirty) {
		g_context->brush_blend_dirty = false;
		render_path_set_target("texpaint_blend0", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		render_path_set_target("texpaint_blend1", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		render_path_end();
	}
}

void render_path_paint_draw() {
	if (!render_path_paint_paint_enabled()) {
		return;
	}

	if (g_config->brush_live && g_context->pdirty <= 0 && g_context->ddirty > 0 && g_context->brush_time == 0) {
		// gbuffer has been updated now but brush will lag 1 frame
		render_path_paint_commands_live_brush();
	}

	paint_exec_begin(); // The draws of the frame resolve their dabs together
	if (history_steps != NULL && render_path_paint_tail_pending) {
		// The brush was released, the stamps the end taper kept back paint now. Painted as part of the stroke: its paint so far
		// is kept, the coverage clears after this draw.
		f64 brush_time        = g_context->brush_time;
		g_context->brush_time = brush_time > 0.0 ? brush_time : 0.001;
		render_path_paint_tail_flush();
		g_context->brush_time = brush_time;
	}
	else if (history_steps != NULL) {
		bool stamps  = render_path_paint_stamps();
		bool samples = render_path_paint_use_samples();
		if (!stamps && !samples) {
			render_path_paint_commands_symmetry();
		}

		if (g_context->pdirty > 0) {
			render_path_paint_dilated = false;
		}

		if (g_context->tool == TOOL_TYPE_BAKE) {
			if (g_context->bake_type == BAKE_TYPE_NORMAL || g_context->bake_type == BAKE_TYPE_HEIGHT || g_context->bake_type == BAKE_TYPE_DERIVATIVE) {
				if (!render_path_paint_baking && g_context->pdirty > 0) {

					render_path_paint_baking     = true;
					_render_path_paint_bake_type = g_context->bake_type;
					g_context->bake_type = g_context->bake_type == BAKE_TYPE_NORMAL ? BAKE_TYPE_NORMAL_OBJECT : BAKE_TYPE_POSITION; // Bake high poly data
					make_material_parse_paint_material(false);
					mesh_object_t *_paint_object = g_context->paint_object;
					mesh_object_t *high_poly     = g_project->_->paint_objects->buffer[g_context->bake_high_poly];
					bool           _visible      = high_poly->base->visible;
					mesh_object_t *merged        = g_context->merged_object;
					bool           _merged       = merged != NULL && merged->base->visible;
					high_poly->base->visible     = true;
					if (_merged) {
						merged->base->visible = false; // Draw the high poly alone
					}
					context_select_paint_object(high_poly);
					render_path_paint_commands_paint_stroke(true);
					high_poly->base->visible = _visible;
					if (_merged) {
						merged->base->visible = true;
					}
					if (render_path_paint_push_undo_last) {
						history_paint();
					}
					context_select_paint_object(_paint_object);

					if (g_context->bake_type == BAKE_TYPE_DERIVATIVE) {
						sys_notify_on_next_frame(&render_path_paint_draw_bake_derivative, NULL);
					}
					else {
						sys_notify_on_next_frame(&render_path_paint_draw_bake, NULL);
					}
				}
			}
			else if (g_context->bake_type == BAKE_TYPE_OBJECTID) {
				i32            _layer_filter = g_context->layer_filter;
				mesh_object_t *_paint_object = g_context->paint_object;
				bool           is_merged     = g_context->merged_object != NULL;
				bool           _visible      = is_merged && g_context->merged_object->base->visible;
				g_context->layer_filter      = 1;
				if (is_merged) {
					g_context->merged_object->base->visible = false;
				}

				for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
					mesh_object_t *p = g_project->_->paint_objects->buffer[i];
					context_select_paint_object(p);
					render_path_paint_commands_paint_stroke(true);
				}

				g_context->layer_filter = _layer_filter;
				context_select_paint_object(_paint_object);
				if (is_merged)
					g_context->merged_object->base->visible = _visible;
			}
			else if (render_path_paint_is_rt_bake()) {
				bool dispatched = render_path_raytrace_bake_commands(make_material_parse_paint_material);
				render_path_paint_dilate_stroke(true, false);
				// The raytrace setup frame traces no rays, keep its sample
				if (!dispatched && g_context->pdirty > 0) {
					g_context->pdirty++;
				}
			}
			else {
				render_path_paint_commands_paint_stroke(true);
			}
		}
		else if (stamps && samples) { // Stamps along the pen samples
			render_path_paint_commands_samples_stamps();
		}
		else if (stamps) { // Paint with stamps spaced along the stroke
			render_path_paint_commands_stamps();
		}
		else if (samples) { // A capsule per pen sample
			render_path_paint_commands_samples();
		}
		else { // Paint
			render_path_paint_commands_paint_stroke(true);
		}
	}
	paint_exec_end();

	render_path_paint_clear_blend_stroke();

	if (g_context->paint2d) {
		render_path_paint_restore_plane_mesh();
	}
}

// The selected layer shows as the live layer: the live brush's preview, a curve being edited (util_curve_stroke.c)
bool render_path_paint_live_shown() {
	return (g_config->brush_live && render_path_paint_live_layer_drawn > 0) || util_curve_stroke_previewing();
}

// The live layer swapped in by render_path_paint_bind_layers, not by a caller already painting into it (the gbuffer
// drawn again while a curve paints its preview from another view, util_curve_stroke.c)
static bool render_path_paint_bound_live = false;

void render_path_paint_bind_layers() {
	render_path_paint_bound_live = render_path_paint_live_shown() && !render_path_paint_live_layer_locked;
	if (render_path_paint_bound_live) {
		render_path_paint_use_live_layer(true);
	}

	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		char         *n = string_tmp("texpaint%d", l->id);
		render_path_bind_target(n, n);

		if (slot_layer_is_mask(l)) { // Masks are sampled as texmask: the evaluated stack, texpaint for an implicit one
			char *t = l->texmask != NULL && !mask_stack_is_trivial(l) ? string_tmp("texmask%s", l->ext) : n;
			render_path_bind_target(t, string_tmp("texmask%d", l->id));
		}

		if (slot_layer_is_layer(l)) {
			n = string_tmp("texpaint_nor%d", l->id);
			render_path_bind_target(n, n);
			n = string_tmp("texpaint_pack%d", l->id);
			render_path_bind_target(n, n);
		}
	}
	layer_cache_bind();
}

void render_path_paint_unbind_layers() {
	if (render_path_paint_bound_live) {
		render_path_paint_bound_live = false;
		render_path_paint_use_live_layer(false);
	}
}

// The live layer is three full size textures, only the live brush needs it all the time.
// For the brush preview it is created when needed and freed after a while.
static f64 render_path_paint_live_last_use = 0.0;

void render_path_paint_live_layer_used() {
	render_path_paint_live_last_use = iron_time();
}

void render_path_paint_live_layer_release_idle() {
	if (render_path_paint_live_layer == NULL || render_path_paint_live_layer_locked) {
		return;
	}
	if (g_config->brush_live || ui_brush_creator_shown()) { // The drawing pad keeps the stroke being drawn in it
		render_path_paint_live_last_use = iron_time();
		return;
	}
	if (iron_time() - render_path_paint_live_last_use > 5.0) {
		slot_layer_unload(render_path_paint_live_layer);
		render_path_paint_live_layer       = NULL;
		render_path_paint_live_layer_drawn = 0;
	}
}

// Base dilation after painting, deferred to render_path_paint_begin while a stroke is in progress
void render_path_paint_dilate_after_paint() {
	paint_exec_dilate_after_paint();
}

void render_path_paint_dilate_after_paint_now() {
	if (g_context->brush_time > 0) {
		render_path_paint_dilate_base_pending = true;
		return;
	}
	render_path_paint_dilate_now(true, false);
}

// Dilation, an op of the paint executor
void render_path_paint_dilate_stroke(bool base, bool nor_pack) {
	paint_exec_dilate(base, nor_pack);
}

// From outside the stroke model: now, isolated
void render_path_paint_dilate(bool base, bool nor_pack) {
	paint_exec_isolate_begin();
	paint_exec_dilate(base, nor_pack);
	paint_exec_isolate_end();
}

void render_path_paint_dilate_now(bool base, bool nor_pack) {
	if (g_config->dilate_radius > 0 && !g_context->paint2d) {
		if (!paint_exec_replay) { // Replays have their own, the size of their target (paint_replay.c)
			util_uv_cache_dilate_map();
			layers_make_temp_img();
		}
		i32 tid = g_context->layer->id;

		char *format      = gpu_texture_format_name(layers_format(base_bits));
		char *copy_pass   = string_equals(format, "RGBA64") ? "copyRGBA64_pass" : string_equals(format, "RGBA128") ? "copyRGBA128_pass" : "copy_pass";
		char *dilate_pass = string_equals(format, "RGBA64") ? "dilateRGBA64_pass" : string_equals(format, "RGBA128") ? "dilateRGBA128_pass" : "dilate_pass";

		if (base) {
			render_path_set_target("temptex0", NULL, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0); // Overwritten whole
			render_path_bind_target(string_tmp("texpaint%d", tid), "tex");
			render_path_draw_shader(string_tmp("Scene/copy_pass/%s", copy_pass));
			render_path_set_target(string_tmp("texpaint%d", tid), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("temptex0", "tex");
			render_path_draw_shader(string_tmp("Scene/dilate_pass/%s", dilate_pass));
		}
		if (nor_pack && !slot_layer_is_mask(g_context->layer)) {
			render_path_set_target("temptex0", NULL, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0); // Overwritten whole
			render_path_bind_target(string_tmp("texpaint_nor%d", tid), "tex");
			render_path_draw_shader(string_tmp("Scene/copy_pass/%s", copy_pass));
			render_path_set_target(string_tmp("texpaint_nor%d", tid), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("temptex0", "tex");
			render_path_draw_shader(string_tmp("Scene/dilate_pass/%s", dilate_pass));
			render_path_set_target("temptex0", NULL, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0); // Overwritten whole
			render_path_bind_target(string_tmp("texpaint_pack%d", tid), "tex");
			render_path_draw_shader(string_tmp("Scene/copy_pass/%s", copy_pass));
			render_path_set_target(string_tmp("texpaint_pack%d", tid), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("temptex0", "tex");
			render_path_draw_shader(string_tmp("Scene/dilate_pass/%s", dilate_pass));
		}
	}
}
