
#include "../global.h"

static string_array_t *render_path_paint_additional(char *nor, char *pack) {
	static string_array_t ar = {0};
	ar.length                = 0;
	string_array_push(&ar, nor);
	string_array_push(&ar, pack);
	string_array_push(&ar, "texpaint_blend0");
	return &ar;
}

// Translucent surfaces are discarded from "main", painting still needs to hit them
static char *render_path_paint_depth() {
	return render_path_base_has_paint_depth() ? "main_paint" : "main";
}

static bool render_path_paint_uses_gbuffer0() {
	if (g_context->xray || g_config->brush_angle_reject || context_is_decal()) {
		return true;
	}
	if (g_context->brush_mask_image != NULL && (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER)) {
		return true;
	}
	if (g_context->tool == TOOL_TYPE_FILL && g_context->fill_type == FILL_TYPE_ANGLE) {
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
render_target_t *_render_path_paint_texpaint_undo;
render_target_t *_render_path_paint_texpaint_nor_undo;
render_target_t *_render_path_paint_texpaint_pack_undo;
bake_type_t      _render_path_paint_bake_type;

void render_path_paint_init() {

	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_blend0"; // Stroke coverage, 16 bit so low flow builds up without banding
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = "R16";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_blend1";
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = "R16";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_colorid";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_colorid_sel"; // Colors of the parts picked for an ID mask, see ui_id_mask.c
		t->width           = 16;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_picker";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_nor_picker";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_pack_picker";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_uv_picker";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_posnortex_picker0";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA128";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_posnortex_picker1";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA128";
		render_path_create_render_target(t);
	}

	render_path_load_shader("Scene/copy_mrt3_pass/copy_mrt3_pass");
	render_path_load_shader("Scene/copy_mrt3_pass/copy_mrt3RGBA64_pass");
	render_path_load_shader("Scene/copy_mrt3_pass/copy_mrt3RGBA128_pass");
	render_path_load_shader("Scene/undo_tiles_pass/undo_tiles_pass");
	render_path_load_shader("Scene/dilate_pass/dilate_pass");
	render_path_load_shader("Scene/dilate_pass/dilateRGBA64_pass");
	render_path_load_shader("Scene/dilate_pass/dilateRGBA128_pass");
}

void render_path_paint_draw_fullscreen_triangle() {
	// Vertices are mangled in vertex shader to form a fullscreen triangle,
	// so plane transform does not matter
	if (make_material_paint_scon != NULL) {
		mesh_object_draw(scene_get_child(".Plane")->ext, make_material_paint_scon, _render_path_bind_params);
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
#ifdef IRON_METAL
static void render_path_paint_cull(mesh_object_t *o);
#endif

void render_path_paint_draw_target() {
	mesh_object_t *o = render_path_paint_target();
	if (o != NULL && make_material_paint_scon != NULL) {
		_mesh_object_last_pipeline = NULL;
#ifdef IRON_METAL
		render_path_paint_cull(o);
#endif
		mesh_object_draw(o, make_material_paint_scon, _render_path_bind_params);
	}
	render_path_end();
}

static mesh_object_t *render_path_paint_fill_object  = NULL; // Picked on mesh fill stroke start
static gpu_buffer_t  *render_path_paint_fill_indices = NULL; // Submesh fill, triangles connected to the clicked one

static void render_path_paint_pick_fill_submesh() {
	if (render_path_paint_fill_indices != NULL) {
		gpu_delete_buffer(render_path_paint_fill_indices);
		render_path_paint_fill_indices = NULL;
	}
	mesh_object_t *o = render_path_paint_fill_object;
	if (o == NULL) {
		return;
	}
	ray_t *ray = raycast_get_ray(mouse_view_x(), mouse_view_y(), scene_camera);
	i32    tri = util_mesh_raycast_triangle(o, ray);
	if (tri < 0) {
		return;
	}
	u32_array_t *indices           = util_mesh_connected_indices(o->data, tri);
	render_path_paint_fill_indices = gpu_create_index_buffer(indices->length);
	mesh_data_build_indices(render_path_paint_fill_indices, indices);
	array_delete(indices);
}

// Mesh fill, only the clicked paint object is drawn, submesh fill only its clicked connected part
static void render_path_paint_draw_fill_object() {
	mesh_object_t *o       = render_path_paint_fill_object;
	mesh_object_t *target  = render_path_paint_target();
	bool           merged  = target != NULL && target == g_context->merged_object;
	bool           painted = o != NULL && (merged || o == target) && array_index_of(g_project->_->paint_objects, o) >= 0;
	bool           submesh = g_context->fill_type == FILL_TYPE_SUBMESH;
	if (submesh && render_path_paint_fill_indices == NULL) {
		painted = false;
	}
	if (painted && o->base->visible && make_material_is_default(o->material) && make_material_paint_scon != NULL) {
		_mesh_object_last_pipeline = NULL;
		gpu_buffer_t *indices      = o->data->_->index_buffer;
		if (submesh) {
			o->data->_->index_buffer = render_path_paint_fill_indices;
		}
		mesh_object_draw(o, make_material_paint_scon, _render_path_bind_params);
		o->data->_->index_buffer = indices;
	}
	render_path_end();
}

void render_path_paint_commands_particle(i32 tid, char *texpaint, bool is_mask) {
	// Paint pass per particle
	i32 drawn = 0;
	for (i32 _pi = 0; _pi < 32; ++_pi) {
		if (g_context->particles[_pi].timer == NULL || !g_context->particles[_pi].hit) {
			continue;
		}
		g_context->particle_index      = _pi;
		g_context->particle_hit_x      = g_context->particles[_pi].hit_x;
		g_context->particle_hit_y      = g_context->particles[_pi].hit_y;
		g_context->particle_hit_z      = g_context->particles[_pi].hit_z;
		g_context->last_particle_hit_x = g_context->particles[_pi].hit_last_x;
		g_context->last_particle_hit_y = g_context->particles[_pi].hit_last_y;
		g_context->last_particle_hit_z = g_context->particles[_pi].hit_last_z;
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

		if (drawn > 0) {
			render_path_set_target("texpaint_blend1", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("texpaint_blend0", "tex");
			render_path_draw_shader("Scene/copy_pass/copyR16_pass");
		}
		drawn++;

		render_path_set_target(texpaint, render_path_paint_additional(string_tmp("texpaint_nor%d", tid), string_tmp("texpaint_pack%d", tid)), NULL,
		                       GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(render_path_paint_depth(), "gbufferD");
		if (render_path_paint_uses_gbuffer0()) {
			render_path_bind_target("gbuffer0", "gbuffer0");
		}
		render_path_bind_target("texpaint_blend1", "paintmask");
		render_path_paint_draw_target();
	}
}

static void render_path_paint_ref_target(char *name, char *format) {
	render_target_t *rt = any_map_get(render_path_render_targets, name);
	if (rt != NULL && (rt->width != config_get_texture_res_x() || rt->height != config_get_texture_res_y() || !string_equals(rt->format, format))) {
		gpu_delete_texture(rt->_image);
		map_delete(render_path_render_targets, name);
		rt = NULL;
	}
	if (rt == NULL) {
		render_target_t *t = render_target_create();
		t->name            = name;
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = format;
		render_path_create_render_target(t);
	}
}

// Frees the full size scratch targets, they are created again when needed
void render_path_paint_release_scratch() {
	char *names[] = {"texpaint_ref", "texpaint_nor_ref", "texpaint_pack_ref"};
	for (i32 i = 0; i < 3; ++i) {
		render_target_t *rt = any_map_get(render_path_render_targets, names[i]);
		if (rt != NULL) {
			gpu_delete_texture(rt->_image);
			map_delete(render_path_render_targets, names[i]);
		}
	}
	if (layers_temp_image != NULL) {
		gpu_delete_texture(layers_temp_image);
		map_delete(render_path_render_targets, "temptex0");
		layers_temp_image = NULL;
	}
}

static i32 render_path_paint_ref_frame = -1; // Blur snapshot once per frame, the dabs of a frame blur the same source
static i32 render_path_paint_ref_tid   = -1;

static void render_path_paint_ref_snapshot(i32 tid, bool is_mask) {
	if (render_path_paint_ref_frame == render_path_paint_dilate_frame && render_path_paint_ref_tid == tid &&
	    any_map_get(render_path_render_targets, "texpaint_ref") != NULL) {
		return;
	}
	render_path_paint_ref_frame = render_path_paint_dilate_frame;
	render_path_paint_ref_tid   = tid;
	if (is_mask) {
		render_path_paint_ref_target("texpaint_ref", "RGBA32");
		render_path_set_target("texpaint_ref", NULL, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0);
		render_path_bind_target(string_tmp("texpaint%d", tid), "tex");
		render_path_draw_shader("Scene/copy_pass/copy_pass");
		return;
	}
	char *format = base_bits == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";
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
static void render_path_paint_clone_snapshot() {
	layers_flatten(false, NULL);
	char *format = base_bits == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";
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

// Renders object ids and returns the paint object under the cursor, NULL on a miss.
// Expects the cursor tool with pick_object_id set, the paint material is parsed for it here.
static mesh_object_t *render_path_paint_pick_object() {
	viewport_mode_t _viewport_mode = g_context->viewport_mode;
	g_context->viewport_mode       = VIEWPORT_MODE_OBJECT_ID;
	make_material_parse_mesh_material();

	// Objects with assigned material do not write object id
	shader_data_t *base_material     = g_project->_->materials->buffer[0]->data;
	any_array_t   *_object_materials = any_array_create(g_project->_->paint_objects->length);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o             = g_project->_->paint_objects->buffer[i];
		_object_materials->buffer[i] = o->material;
		o->material                  = base_material;
	}

	render_path_base_draw_gbuffer();

	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		g_project->_->paint_objects->buffer[i]->material = _object_materials->buffer[i];
	}

	g_context->viewport_mode = _viewport_mode;
	make_material_parse_mesh_material();

	g_context->pdirty = 1;
	make_material_parse_paint_material(false);
	render_path_set_target("texpaint_picker", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("gbuffer1", "gbuffer1");
	render_path_bind_target("gbuffer2", "gbuffer2");
	render_path_paint_draw_fullscreen_triangle();

	render_target_t *rt    = any_map_get(render_path_render_targets, "texpaint_picker");
	buffer_t        *pixel = gpu_get_texture_pixels(rt->_image);
	if (buffer_get_u8(pixel, 3) > 0) {
#ifdef IRON_BGRA
		i32 r_byte = buffer_get_u8(pixel, 2);
#else
		i32 r_byte = buffer_get_u8(pixel, 0);
#endif
		i32 g_byte = buffer_get_u8(pixel, 1);
		i32 index  = r_byte + g_byte * 256 - 1;
		if (index >= 0 && index < g_project->_->paint_objects->length) {
			return g_project->_->paint_objects->buffer[index];
		}
	}
	return NULL;
}

f32    uniforms_ext_f32_link(object_t *object, shader_data_t *mat, char *link);
vec2_t uniforms_ext_vec2_link(object_t *object, shader_data_t *mat, char *link);
vec4_t uniforms_ext_vec4_link(object_t *object, shader_data_t *mat, char *link);

// Dabs painted by one draw when the paint shader is batched (render_path_paint_batched): the stamps or samples of a frame
// are collected, uploaded as a small texture and painted at once instead of a full size pass each.
#define RENDER_PATH_PAINT_BATCH_MAX 64

static f32     render_path_paint_batch[RENDER_PATH_PAINT_BATCH_MAX * 12];
static i32     render_path_paint_batch_count  = 0;
static bool    render_path_paint_batch_ready  = false; // Uploaded for the next paint draw
gpu_texture_t *render_path_paint_batch_texture = NULL;
f32            render_path_paint_batch_drawn   = 0.0; // Dabs in the texture

// Whether the paint shader in use paints batches of dabs. Asked of the shader itself, not make_paint_batched: previews build
// other paint shaders and restore the saved one (make_material_restore_paint_material), which leaves that flag stale.
bool render_path_paint_batched() {
	shader_context_t *scon = make_material_paint_scon;
	if (scon == NULL || scon->texture_units == NULL) {
		return false;
	}
	for (i32 i = 0; i < scon->texture_units->length; ++i) {
		char *link = scon->texture_units->buffer[i]->link;
		if (link != NULL && string_equals(link, "_texpaintdabs")) {
			return true;
		}
	}
	return false;
}

// Adds a dab from the current brush state, as the uniforms of a single draw would see it
void render_path_paint_batch_add() {
	if (render_path_paint_batch_count == RENDER_PATH_PAINT_BATCH_MAX) {
		render_path_paint_batch_flush(false);
	}
	vec4_t inp   = uniforms_ext_vec4_link(NULL, NULL, "_input_brush");
	vec4_t last  = uniforms_ext_vec4_link(NULL, NULL, "_input_brush_last");
	vec2_t angle = uniforms_ext_vec2_link(NULL, NULL, "_brush_angle");
	vec2_t dab   = uniforms_ext_vec2_link(NULL, NULL, "_brush_dab_angle");
	f32   *d     = &render_path_paint_batch[render_path_paint_batch_count * 12];
	d[0]         = inp.x;
	d[1]         = inp.y;
	d[2]         = last.x;
	d[3]         = last.y;
	d[4]         = uniforms_ext_f32_link(NULL, NULL, "_brush_radius");
	d[5]         = uniforms_ext_f32_link(NULL, NULL, "_brush_opacity");
	d[6]         = uniforms_ext_f32_link(NULL, NULL, "_brush_hardness");
	d[7]         = uniforms_ext_f32_link(NULL, NULL, "_brush_flow");
	d[8]         = angle.x * dab.x - angle.y * dab.y; // Both rotations of the mask
	d[9]         = angle.x * dab.y + angle.y * dab.x;
	vec2_t tilt  = util_brush_tilt_shape();
	d[10]        = tilt.x;
	d[11]        = tilt.y;
	render_path_paint_batch_count++;
}

static void render_path_paint_batch_upload() {
	if (render_path_paint_batch_texture != NULL) {
		gpu_delete_texture(render_path_paint_batch_texture); // Freed after the frame
	}
	buffer_t b                      = {.buffer = (u8 *)render_path_paint_batch, .length = render_path_paint_batch_count * 12 * sizeof(f32)};
	render_path_paint_batch_texture = gpu_create_texture_from_bytes(&b, render_path_paint_batch_count * 3, 1, GPU_TEXTURE_FORMAT_RGBA128);
	render_path_paint_batch_drawn   = render_path_paint_batch_count;
	render_path_paint_batch_count   = 0;
	render_path_paint_batch_ready   = true;
}

#ifdef IRON_METAL
// Only the parts of the mesh near the dabs of this draw, see paint_cull.c
static void render_path_paint_cull(mesh_object_t *o) {
	tool_type_t tool = g_context->tool;
	if ((tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_BLUR) || g_context->paint2d ||
	    context_is_decal() || g_context->xray || context_is_pixel_snap() || context_is_brush_camera_align() || g_context->pick_pos_nor_tex ||
	    g_context->layer->fill_material != NULL) {
		return;
	}
	static paint_cull_dab_t dabs[RENDER_PATH_PAINT_BATCH_MAX];
	i32                     count   = 0;
	f32                     stretch = g_config->tilt_shading ? 3.0 : 1.0;
	if (render_path_paint_batched()) {
		for (i32 i = 0; i < (i32)render_path_paint_batch_drawn && i < RENDER_PATH_PAINT_BATCH_MAX; ++i) {
			f32 *d       = &render_path_paint_batch[i * 12];
			dabs[count++] = (paint_cull_dab_t){d[0] * 2.0 - 1.0, 1.0 - d[1] * 2.0, d[2] * 2.0 - 1.0, 1.0 - d[3] * 2.0, d[4] * stretch};
		}
	}
	else {
		vec4_t inp    = uniforms_ext_vec4_link(NULL, NULL, "_input_brush");
		vec4_t last   = uniforms_ext_vec4_link(NULL, NULL, "_input_brush_last");
		f32    radius = uniforms_ext_f32_link(NULL, NULL, "_brush_radius");
		dabs[count++] = (paint_cull_dab_t){inp.x * 2.0 - 1.0, 1.0 - inp.y * 2.0, last.x * 2.0 - 1.0, 1.0 - last.y * 2.0, radius * stretch};
	}
	paint_cull_draw_next(o, dabs, count);
}
#endif

// Paints the collected dabs
void render_path_paint_batch_flush(bool dilation) {
	if (render_path_paint_batch_count == 0) {
		return;
	}
	render_path_paint_batch_upload();
	render_path_paint_commands_paint(dilation);
}

static void render_path_paint_commands_paint_internal(bool dilation);

void render_path_paint_commands_paint(bool dilation) {
	if (g_context->pdirty <= 0) {
		render_path_paint_batch_count = 0;
		render_path_paint_batch_ready = false;
		return;
	}
	if (render_path_paint_batched() && !render_path_paint_batch_ready) { // Single draw, from the current brush state
		render_path_paint_batch_count = 0;
		render_path_paint_batch_add();
		render_path_paint_batch_upload();
	}
	render_path_paint_commands_paint_internal(dilation);
	render_path_paint_batch_ready = false;
}

// Picker results: surface values under the cursor into the picked color, the material and fill layers with picker nodes.
// tool is the tool when the pick was drawn, the values may arrive a frame later.
static void render_path_paint_picker_apply(u8 *a, u8 *b, u8 *c, u8 *d, bool is_mask, tool_type_t tool) {
#ifdef IRON_BGRA
	i32 i0 = 2;
	i32 i1 = 1;
	i32 i2 = 0;
#else
	i32 i0 = 0;
	i32 i1 = 1;
	i32 i2 = 2;
#endif
	i32 i3                           = 3;
	g_context->picked_color->base    = color_set_rb(g_context->picked_color->base, a[i0]);
	g_context->picked_color->base    = color_set_gb(g_context->picked_color->base, a[i1]);
	g_context->picked_color->base    = color_set_bb(g_context->picked_color->base, a[i2]);
	g_context->picked_color->opacity = a[i3] / 255.0;
	g_context->uvx_picked            = d[i0] / 255.0;
	g_context->uvy_picked            = d[i1] / 255.0;

	if (!is_mask) {
		g_context->picked_color->normal    = color_set_rb(g_context->picked_color->normal, b[i0]);
		g_context->picked_color->normal    = color_set_gb(g_context->picked_color->normal, b[i1]);
		g_context->picked_color->normal    = color_set_bb(g_context->picked_color->normal, b[i2]);
		g_context->picked_color->occlusion = c[i0] / 255.0;
		g_context->picked_color->roughness = c[i1] / 255.0;
		g_context->picked_color->metallic  = c[i2] / 255.0;
		g_context->picked_color->height    = c[i3] / 255.0;

		// Pick material
		if (g_context->color_picker_callback == NULL) {
			// matid % 3 == 0 - normal, 1 - emission, 2 - subsurface
			i32 id    = b[3];
			i32 matid = math_floor((id - (id % 3)) / 3.0);
			for (i32 i = 0; i < g_project->_->materials->length; ++i) {
				slot_material_t *m = g_project->_->materials->buffer[i];
				if (m->id == matid) {
					if (g_context->picker_select_material) {
						context_set_material(m);
					}
					g_context->materialid_picked = matid;
					break;
				}
			}
		}
	}

	if (g_context->color_picker_callback != NULL) {
		g_context->color_picker_callback(g_context->picked_color);
	}

	if (tool == TOOL_TYPE_PICKER && g_context->color_picker_callback == NULL && !g_context->picker_select_material) {
		tab_materials_apply_picked_color(g_context->picked_color);
	}

	// Update fill layers which are using materials containing a PICKER node
	slot_material_t *_material = g_context->material;
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m          = g_project->_->materials->buffer[i];
		bool             has_picker = false;
		for (i32 j = 0; j < m->canvas->nodes->length; ++j) {
			ui_node_t *node = m->canvas->nodes->buffer[j];
			if (string_equals(node->type, "PICKER")) {
				has_picker = true;
				break;
			}
		}
		if (has_picker) {
			g_context->material = m;
			layers_update_fill_layers();
			util_render_make_material_preview();
			ui_dock_redraw(TAB_AREA_SIDEBAR1);
		}
	}
	g_context->material = _material;
	ui_header_handle->redraws = 2;
	ui_dock_redraw(2);
}

static gpu_texture_t *render_path_paint_picker_target(char *name) {
	render_target_t *rt = any_map_get(render_path_render_targets, name);
	return rt->_image;
}

#ifdef IRON_METAL
void gpu_read_textures_async(gpu_texture_t **textures, int count);
bool gpu_read_textures_async_poll(uint8_t **data);

static bool        render_path_paint_picker_mask = false;
static tool_type_t render_path_paint_picker_tool = TOOL_TYPE_PICKER;

// Applies the picker values once the gpu read them back, the pick does not wait for the gpu
void render_path_paint_picker_poll() {
	uint8_t *data[4];
	if (gpu_read_textures_async_poll(data)) {
		bool mask = render_path_paint_picker_mask;
		render_path_paint_picker_apply(data[0], mask ? NULL : data[2], mask ? NULL : data[3], data[1], mask, render_path_paint_picker_tool);
	}
}
#else
void render_path_paint_picker_poll() {}
#endif

static void render_path_paint_picker_read(bool is_mask) {
#ifdef IRON_METAL
	gpu_texture_t *textures[4] = {render_path_paint_picker_target("texpaint_picker"), render_path_paint_picker_target("texpaint_uv_picker"),
	                              is_mask ? NULL : render_path_paint_picker_target("texpaint_nor_picker"),
	                              is_mask ? NULL : render_path_paint_picker_target("texpaint_pack_picker")};
	render_path_paint_picker_mask = is_mask;
	render_path_paint_picker_tool = g_context->tool;
	gpu_read_textures_async(textures, is_mask ? 2 : 4);
#else
	u8 *a = gpu_get_texture_pixels(render_path_paint_picker_target("texpaint_picker"))->buffer;
	u8 *d = gpu_get_texture_pixels(render_path_paint_picker_target("texpaint_uv_picker"))->buffer;
	u8 *b = is_mask ? NULL : gpu_get_texture_pixels(render_path_paint_picker_target("texpaint_nor_picker"))->buffer;
	u8 *c = is_mask ? NULL : gpu_get_texture_pixels(render_path_paint_picker_target("texpaint_pack_picker"))->buffer;
	render_path_paint_picker_apply(a, b, c, d, is_mask, g_context->tool);
#endif
}

static void render_path_paint_commands_paint_internal(bool dilation) {
	i32 tid = g_context->layer->id;

	if (g_context->pdirty <= 0) {
		return;
	}

	if (g_context->brush_time == 0 && g_context->tool != TOOL_TYPE_PARTICLE && !g_context->pick_pos_nor_tex) {
		g_context->brush_blend_dirty = true;
	}

	if (g_context->tool == TOOL_TYPE_COLORID) {
		render_path_set_target("texpaint_colorid", NULL, NULL, GPU_CLEAR_COLOR, 0xff000000, 0.0);
		render_path_bind_target("gbuffer2", "gbuffer2");
		render_path_paint_draw_fullscreen_triangle();
		render_target_t *rt = any_map_get(render_path_render_targets, "texpaint_colorid");
		buffer_t        *a  = gpu_get_texture_pixels(rt->_image);
#ifdef IRON_BGRA
		i32 i0 = 2;
		i32 i1 = 1;
		i32 i2 = 0;
#else
		i32 i0 = 0;
		i32 i1 = 1;
		i32 i2 = 2;
#endif
		if (ui_id_mask_pick_pending) {
			ui_id_mask_pick_pending = false;
			ui_id_mask_toggle(((u32)buffer_get_u8(a, i0) << 16) | ((u32)buffer_get_u8(a, i1) << 8) | (u32)buffer_get_u8(a, i2));
		}
	}
	else if (g_context->tool == TOOL_TYPE_PICKER || g_context->tool == TOOL_TYPE_MATERIAL) {
		if (g_context->pick_pos_nor_tex) {
			if (g_context->paint2d) {
				string_array_t *additional = any_array_create_from_raw(
				    (void *[]){
				        "gbuffer1",
				        "gbuffer2",
				    },
				    2);
				render_path_set_target("gbuffer0", additional, "main", GPU_CLEAR_NONE, 0, 0.0);
				render_path_base_draw_meshes(render_path_base_mesh_scon(0), true);
			}
			string_array_t *additional = any_array_create_from_raw(
			    (void *[]){
			        "texpaint_posnortex_picker1",
			    },
			    1);
			render_path_set_target("texpaint_posnortex_picker0", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("gbuffer2", "gbuffer2");
			render_path_bind_target("main", "gbufferD");
			render_path_paint_draw_fullscreen_triangle();
			render_target_t *picker0                    = any_map_get(render_path_render_targets, "texpaint_posnortex_picker0");
			render_target_t *picker1                    = any_map_get(render_path_render_targets, "texpaint_posnortex_picker1");
			gpu_texture_t   *texpaint_posnortex_picker0 = picker0->_image;
			gpu_texture_t   *texpaint_posnortex_picker1 = picker1->_image;
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
				string_array_t *additional = any_array_create_from_raw(
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
				string_array_t *additional = any_array_create_from_raw(
				    (void *[]){
				        "texpaint_nor_picker",
				        "texpaint_pack_picker",
				        "texpaint_uv_picker",
				    },
				    3);
				render_path_set_target("texpaint_picker", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
				render_path_bind_target("gbuffer2", "gbuffer2");
				bool use_live_layer = g_context->tool == TOOL_TYPE_MATERIAL;
				if (use_live_layer) {
					render_path_paint_use_live_layer(true);
				}
				render_path_bind_target(string_tmp("texpaint%d", tid), "texpaint");
				render_path_bind_target(string_tmp("texpaint_nor%d", tid), "texpaint_nor");
				render_path_bind_target(string_tmp("texpaint_pack%d", tid), "texpaint_pack");
				render_path_paint_draw_fullscreen_triangle();
				if (use_live_layer) {
					render_path_paint_use_live_layer(false);
				}
			}
			ui_header_handle->redraws = 2;
			ui_dock_redraw(2);
			render_path_paint_picker_read(is_mask);
		}
	}
	else if (g_context->tool == TOOL_TYPE_CURSOR) {
		if (g_context->pick_object_id) {
			mesh_object_t *o   = render_path_paint_pick_object();
			bool           hit = o != NULL;
			if (hit) {
				if (keyboard_down("shift")) {
					// Added to the selection, the clicked mesh becomes the active one
					tab_meshes_add_objects(any_array_create_from_raw((void *[]){o}, 1));
					g_context->paint_object = o;
				}
				else {
					// Only the clicked mesh stays selected
					g_context->paint_object = o;
					tab_meshes_select_objects(any_array_create_from_raw((void *[]){o}, 1));
				}
				context_deselected_uid    = -1;
				ui_header_handle->redraws = 2;
				tab_meshes_reveal_slot(g_context->paint_object);

				// g_context->layer->object_mask = index + 1;
				// context_set_layer(g_context->layer);
				// make_material_parse_mesh_material();
				// layers_set_object_mask();

				base_redraw_status();
			}
			if (!hit && !keyboard_down("shift")) {
				// Nothing to outline, the mesh stays selected in the meshes tab
				context_deselected_uid = g_context->paint_object->base->uid;
				g_context->ddirty      = 2;
			}

			g_context->pick_object_id = false;
			make_material_parse_paint_material(false);
			render_path_base_draw_gbuffer();
		}
	}
	else {
		bool mesh_fill = g_context->tool == TOOL_TYPE_FILL && (g_context->fill_type == FILL_TYPE_MESH || g_context->fill_type == FILL_TYPE_SUBMESH);
		if (mesh_fill && render_path_paint_fill_pick) {
			// Pick the clicked mesh once per stroke
			render_path_paint_fill_pick   = false;
			i32 _pdirty                   = g_context->pdirty;
			g_context->tool               = TOOL_TYPE_CURSOR;
			g_context->pick_object_id     = true;
			render_path_paint_fill_object = render_path_paint_pick_object();
			if (g_context->fill_type == FILL_TYPE_SUBMESH) {
				render_path_paint_pick_fill_submesh();
			}
			g_context->pick_object_id = false;
			g_context->tool           = TOOL_TYPE_FILL;
			g_context->pdirty         = _pdirty;
			make_material_parse_paint_material(false);
			render_path_base_draw_gbuffer();
		}

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
		bool read_tc = (g_context->tool == TOOL_TYPE_FILL && g_context->fill_type == FILL_TYPE_FACE) || g_context->tool == TOOL_TYPE_CLONE ||
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
				render_path_bind_target("gbuffer2", "gbuffer2");
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
				render_target_t *t = render_target_create();
				t->name            = "texpaint_blur";
				t->width           = math_floor(config_get_texture_res_x() * 0.95);
				t->height          = math_floor(config_get_texture_res_y() * 0.95);
				t->format          = "RGBA32";
				render_path_create_render_target(t);
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
	i32 tid = g_context->layer->id;
	i32 hid = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
	if (use) {
		_render_path_paint_texpaint           = any_map_get(render_path_render_targets, string_tmp("texpaint%d", tid));
		_render_path_paint_texpaint_undo      = any_map_get(render_path_render_targets, string_tmp("texpaint_undo%d", hid));
		_render_path_paint_texpaint_nor_undo  = any_map_get(render_path_render_targets, string_tmp("texpaint_nor_undo%d", hid));
		_render_path_paint_texpaint_pack_undo = any_map_get(render_path_render_targets, string_tmp("texpaint_pack_undo%d", hid));
		_render_path_paint_texpaint_nor       = any_map_get(render_path_render_targets, string_tmp("texpaint_nor%d", tid));
		_render_path_paint_texpaint_pack      = any_map_get(render_path_render_targets, string_tmp("texpaint_pack%d", tid));
		any_map_set(render_path_render_targets, render_path_paint_key("texpaint_undo%d", hid), any_map_get(render_path_render_targets, string_tmp("texpaint%d", tid)));
		any_map_set(render_path_render_targets, render_path_paint_key("texpaint%d", tid), any_map_get(render_path_render_targets, "texpaint_live"));
		if (slot_layer_is_layer(g_context->layer)) {
			any_map_set(render_path_render_targets, render_path_paint_key("texpaint_nor_undo%d", hid),
			            any_map_get(render_path_render_targets, string_tmp("texpaint_nor%d", tid)));
			any_map_set(render_path_render_targets, render_path_paint_key("texpaint_pack_undo%d", hid),
			            any_map_get(render_path_render_targets, string_tmp("texpaint_pack%d", tid)));
			any_map_set(render_path_render_targets, render_path_paint_key("texpaint_nor%d", tid), any_map_get(render_path_render_targets, "texpaint_nor_live"));
			any_map_set(render_path_render_targets, render_path_paint_key("texpaint_pack%d", tid), any_map_get(render_path_render_targets, "texpaint_pack_live"));
		}
	}
	else {
		any_map_set(render_path_render_targets, render_path_paint_key("texpaint%d", tid), _render_path_paint_texpaint);
		any_map_set(render_path_render_targets, render_path_paint_key("texpaint_undo%d", hid), _render_path_paint_texpaint_undo);
		if (slot_layer_is_layer(g_context->layer)) {
			any_map_set(render_path_render_targets, render_path_paint_key("texpaint_nor_undo%d", hid), _render_path_paint_texpaint_nor_undo);
			any_map_set(render_path_render_targets, render_path_paint_key("texpaint_pack_undo%d", hid), _render_path_paint_texpaint_pack_undo);
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
			render_path_paint_commands_paint(false);
		}
	}
	t->loc   = loc0;
	t->rot   = rot0;
	t->scale = s0;
	transform_build_matrix(t);
}

void render_path_paint_commands_live_brush() {
	tool_type_t tool = g_context->tool;
	if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_DECAL && tool != TOOL_TYPE_TEXT &&
	    tool != TOOL_TYPE_BLUR) {
		return;
	}

	if (render_path_paint_live_layer_locked) {
		return;
	}

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
		string_array_t *additional = any_array_create_from_raw(
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
	render_path_paint_commands_paint(true);

	render_path_paint_use_live_layer(false);

	g_context->paint_vec.x       = _x;
	g_context->paint_vec.y       = _y;
	g_context->last_paint_vec_x  = _last_x;
	g_context->last_paint_vec_y  = _last_y;
	g_context->pdirty            = _pdirty;
	g_context->brush_blend_dirty = true;
}

void render_path_paint_draw_cursor(f32 mx, f32 my, f32 radius, f32 tint_r, f32 tint_g, f32 tint_b) {
	mesh_object_t *plane = scene_get_child(".Plane")->ext;
	mesh_data_t   *geom  = plane->data;

	gpu_vertex_textures = true;
	render_path_set_target("", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipes_cursor);
	render_target_t *rt   = any_map_get(render_path_render_targets, render_path_paint_depth());
	gpu_texture_t   *main = rt->_image;
	gpu_set_texture(pipes_cursor_gbufferd, main);
	gpu_set_float2(pipes_cursor_mouse, mx, my);
	gpu_set_float2(pipes_cursor_tex_step, 1 / (float)main->width, 1 / (float)main->height);
	gpu_set_float(pipes_cursor_radius, radius);
	vec4_t right = vec4_norm(camera_object_right_world(scene_camera));
	gpu_set_float3(pipes_cursor_camera_right, right.x, right.y, right.z);
	gpu_set_float3(pipes_cursor_tint, tint_r, tint_g, tint_b);
	vec4_t up = vec4_norm(camera_object_up_world(scene_camera));
	gpu_set_float3(pipes_cursor_camera_up, up.x, up.y, up.z);
	gpu_set_float(pipes_cursor_camera_align, context_is_brush_camera_align() ? 1.0f : 0.0f);
	gpu_set_mat4(pipes_cursor_vp, scene_camera->vp);
	mat4_t inv_vp = mat4_inv(scene_camera->vp);
	gpu_set_mat4(pipes_cursor_inv_vp, inv_vp);
	gpu_set_vertex_buffer(geom->_->vertex_buffer);
	gpu_set_index_buffer(geom->_->index_buffer);
	gpu_draw();
	render_path_end();
}

void render_path_paint_draw_cursor_decal(f32 mx, f32 my, f32 radius, f32 opacity_scale, gpu_texture_t *image) {
	if (image == NULL) {
		return;
	}

	mesh_object_t *plane = scene_get_child(".Plane")->ext;
	mesh_data_t   *geom  = plane->data;

	// Prevent cursor jumping constantly if randomness is used in brush nodes
	f32 brush_nodes_opacity = g_context->brush_nodes_opacity;
	f32 brush_nodes_angle   = g_context->brush_nodes_angle;
	if (g_context->brush_nodes_uses_random) {
		brush_nodes_opacity = 1.0;
		brush_nodes_angle   = 0.0;
	}

	gpu_vertex_textures = true;
	render_path_set_target("", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipes_cursor_decal);
	render_target_t *rt   = any_map_get(render_path_render_targets, render_path_paint_depth());
	gpu_texture_t   *main = rt->_image;
	gpu_set_texture(pipes_cursor_decal_gbufferd, main);
	gpu_set_texture(pipes_cursor_decal_texdecal, image);
	render_target_t *gbuffer0_rt = any_map_get(render_path_render_targets, "gbuffer0");
	gpu_set_texture(pipes_cursor_decal_gbuffer0, gbuffer0_rt->_image);
	gpu_set_float2(pipes_cursor_decal_mouse, mx, my);
	gpu_set_float2(pipes_cursor_decal_tex_step, 1 / (float)main->width, 1 / (float)main->height);
	gpu_set_float(pipes_cursor_decal_radius, radius);
	vec4_t right = vec4_norm(camera_object_right_world(scene_camera));
	gpu_set_float3(pipes_cursor_decal_camera_right, right.x, right.y, right.z);
	vec4_t up = vec4_norm(camera_object_up_world(scene_camera));
	gpu_set_float3(pipes_cursor_decal_camera_up, up.x, up.y, up.z);
	gpu_set_float(pipes_cursor_decal_camera_align, context_is_brush_camera_align() ? 1.0f : 0.0f);
	f32 opacity = math_max(0.0, math_min(1.0, g_context->brush_opacity * brush_nodes_opacity * opacity_scale));
	gpu_set_float(pipes_cursor_decal_opacity, opacity);
	f32 angle = (g_context->brush_angle + brush_nodes_angle) * (math_pi() / 180.0);
	gpu_set_float2(pipes_cursor_decal_angle, math_cos(angle), math_sin(angle));
	gpu_set_float(pipes_cursor_decal_scale_x, g_context->brush_scale_x);
	gpu_set_mat4(pipes_cursor_decal_vp, scene_camera->vp);
	mat4_t inv_vp = mat4_inv(scene_camera->vp);
	gpu_set_mat4(pipes_cursor_decal_inv_vp, inv_vp);
	gpu_set_vertex_buffer(geom->_->vertex_buffer);
	gpu_set_index_buffer(geom->_->index_buffer);
	gpu_draw();
	render_path_end();
}

void render_path_paint_commands_cursor() {
	bool        decal_mask = context_is_decal_mask();
	tool_type_t tool       = g_context->tool;
	if (tool == TOOL_TYPE_INPAINT) {
		if (base_ui_enabled && !base_is_dragging && !base_paint_in_2d()) {
			render_path_paint_draw_cursor(g_context->paint_vec.x, 1.0 - g_context->paint_vec.y, g_context->brush_radius / 3.4, 1.0, 1.0, 1.0);
		}
		return;
	}
	if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_BLUR && tool != TOOL_TYPE_PARTICLE &&
	    !context_is_decal() && !decal_mask) {
		return;
	}

	bool fill_layer  = g_context->layer->fill_material != NULL && !layers_fill_paint_tool(tool); // Brushes paint its mask
	bool group_layer = slot_layer_is_group(g_context->layer);
	if (!base_ui_enabled || base_is_dragging || fill_layer || group_layer) {
		return;
	}

	// Paint coordinates point into the 2D view, it draws its own cursor
	if (base_paint_in_2d()) {
		return;
	}

	// The pencil hovered away from the screen and nothing touches it
	if (pen_hover_left && !pen_down("tip") && !mouse_down_any()) {
		return;
	}

	f32 mx = g_context->paint_vec.x;
	f32 my = 1.0 - g_context->paint_vec.y;

	// Ahead of the pencil where it is expected next, the stroke feels less behind. Only drawn, never painted.
	if (pen_down("tip") && pen_predicted && g_context->brush_lazy_radius <= 0.0 && !context_is_ruler_paint()) {
		mx = base_paint_x(pen_predicted_x);
		my = 1.0 - base_paint_y(pen_predicted_y);
	}

	// Fainter the higher the pencil hovers
	f32 tint = pen_hover >= 0.0 && !pen_down("tip") ? 1.0 - 0.6 * pen_hover : 1.0;

	// Prevent cursor jumping constantly if randomness is used in brush nodes
	f32 brush_nodes_radius = g_context->brush_nodes_radius;
	if (g_context->brush_nodes_uses_random) {
		brush_nodes_radius = 1.0;
	}

	if (context_is_decal()) {
		f32            scale2d = (900 / (f32)base_h()) * g_config->window_scale;
		f32            radius  = brush_nodes_radius * g_context->brush_radius / 15.0 * scale2d * 2.0;
		f32            cmx     = decal_mask ? g_context->decal_x : mx;
		f32            cmy     = decal_mask ? 1.0f - g_context->decal_y : my;
		gpu_texture_t *image   = g_context->decal_image;
		render_path_paint_draw_cursor_decal(cmx, cmy, radius, decal_mask ? 0.3f : 0.9f, image);
		if (decal_mask) {
			render_path_paint_draw_cursor(mx, my, brush_nodes_radius * g_context->brush_decal_mask_radius / 3.4, 1.0, 1.0, 1.0);
		}
		return;
	}

	f32 radius = decal_mask ? g_context->brush_decal_mask_radius : g_context->brush_radius;
	render_path_paint_draw_cursor(mx, my, brush_nodes_radius * radius / 3.4, tint, tint, tint);
}

bool render_path_paint_paint_enabled() {
	if (args_player) {
		return false;
	}

	if (g_context->tool == TOOL_TYPE_INPAINT) {
		return false; // Inpaint handles the strokes itself
	}

	if (g_context->tool == TOOL_TYPE_BAKE) {
		return true;
	}

	if (g_context->tool == TOOL_TYPE_CURSOR) {
		return true;
	}

	bool fill_layer = g_context->layer->fill_material != NULL && g_context->tool != TOOL_TYPE_PICKER && g_context->tool != TOOL_TYPE_MATERIAL &&
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
		render_path_paint_dilate(render_path_paint_dilate_base_pending, !render_path_paint_dilated);
		render_path_paint_dilated             = true;
		render_path_paint_dilate_base_pending = false;
	}

	if (!render_path_paint_paint_enabled()) {
		return;
	}

	render_path_paint_push_undo_last = history_push_undo;

	if (history_push_undo && history_undo_layers != NULL) {
		history_paint();
		if (g_context->tool == TOOL_TYPE_CLONE && g_context->clone_layers == CLONE_LAYERS_VISIBLE) {
			render_path_paint_clone_snapshot();
		}
	}

	if (g_context->paint2d) {
		render_path_paint_set_plane_mesh();
	}

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
		layers_update_linked_layers();
	}

	g_context->pdirty--;
}

static void render_path_paint_commands_low_poly() {
	mesh_object_t *merged = g_context->merged_object;
	if (merged == NULL || !merged->base->visible) {
		render_path_paint_commands_paint(true);
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
		render_path_paint_commands_paint(true);
		return;
	}
	mesh_object_t *_paint_object = g_context->paint_object;
	merged->base->visible        = false;
	for (i32 i = 0; i <= last; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p != high_poly && p->base->visible) {
			context_select_paint_object(p);
			render_path_paint_commands_paint(i == last); // Dilate once all objects are drawn
		}
	}
	merged->base->visible = true;
	context_select_paint_object(_paint_object);
}

void _render_path_paint_final() {
	g_context->bake_type = _render_path_paint_bake_type;
	make_material_parse_paint_material(false);
	g_context->pdirty = 1;
	render_path_paint_commands_low_poly();
	g_context->pdirty        = 0;
	render_path_paint_baking = false;
}

void _render_path_paint_deriv_on_next_frame(void *_) {
	_render_path_paint_final();
}

void _render_path_paint_deriv() {
	g_context->bake_type = BAKE_TYPE_HEIGHT;
	make_material_parse_paint_material(false);
	g_context->pdirty = 1;
	render_path_paint_commands_low_poly();
	g_context->pdirty = 0;
	if (render_path_paint_push_undo_last) {
		history_paint();
	}
	sys_notify_on_next_frame(&_render_path_paint_deriv_on_next_frame, NULL);
}

bool render_path_paint_is_rt_bake() {
	return (g_context->bake_type == BAKE_TYPE_OCCLUSION || g_context->bake_type == BAKE_TYPE_LIGHTMAP || g_context->bake_type == BAKE_TYPE_BENT_NORMAL ||
	        g_context->bake_type == BAKE_TYPE_THICKNESS);
}

void render_path_paint_draw_bake(void *_) {
	_render_path_paint_final();
}

void render_path_paint_draw_bake_derivative(void *_) {
	_render_path_paint_deriv();
}

void render_path_paint_clear_blend() {
	if (g_context->brush_blend_dirty) {
		g_context->brush_blend_dirty = false;
		render_path_set_target("texpaint_blend0", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		render_path_set_target("texpaint_blend1", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		render_path_end();
	}
}

// Brush stamps: spacing, scatter and jitter
static u32 render_path_paint_stamp_seed = 1;

static f32 render_path_paint_stamp_random() {
	render_path_paint_stamp_seed = render_path_paint_stamp_seed * 1664525u + 1013904223u;
	return (render_path_paint_stamp_seed >> 8) / 16777216.0f;
}

bool render_path_paint_stamps() {
	bool tool = g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE;
	return tool && g_context->brush_spacing > 0.0 && !slot_layer_is_path(g_context->layer) && g_context->layer->fill_material == NULL &&
	       !context_is_decal() && !context_is_pixel_snap();
}

static void render_path_paint_stamp(f32 x, f32 y, f32 aspect, f32 diameter, bool last) {
	f32 scatter = g_context->brush_scatter;
	if (scatter > 0.0) {
		f32 a = render_path_paint_stamp_random() * math_pi() * 2.0;
		f32 d = render_path_paint_stamp_random() * scatter * diameter;
		x += math_cos(a) * d / aspect;
		y += math_sin(a) * d;
	}
	g_context->brush_dab_radius  = 1.0 - g_context->brush_jitter_size * render_path_paint_stamp_random();
	g_context->brush_dab_opacity = 1.0 - g_context->brush_jitter_opacity * render_path_paint_stamp_random();
	g_context->brush_dab_angle   = (render_path_paint_stamp_random() * 2.0 - 1.0) * math_pi() * g_context->brush_jitter_angle;
	g_context->paint_vec.x       = x;
	g_context->paint_vec.y       = y;
	g_context->last_paint_vec_x  = x;
	g_context->last_paint_vec_y  = y;
	if (render_path_paint_batched()) { // Painted with the other stamps of the frame by render_path_paint_batch_flush
		render_path_paint_batch_add();
		return;
	}
	render_path_paint_commands_symmetry();
	render_path_paint_commands_paint(last);
}

// Stamps from the last one to the brush position, one every spacing; the remainder carries over to the next frame
void render_path_paint_commands_stamps() {
	if (g_context->pdirty <= 0) {
		return;
	}
	f32 cx       = g_context->paint_vec.x;
	f32 cy       = g_context->paint_vec.y;
	f32 aspect   = base_paint_view_w() / (f32)base_paint_view_h();
	f32 diameter = util_layer_brush_screen_radius() * 2.0;
	f32 step     = math_max(g_context->brush_spacing * diameter, 0.0005);

	if (g_context->brush_dab_x < 0.0) { // First stamp of the stroke
		render_path_paint_stamp_seed = (u32)(cx * 100003.0) ^ (u32)(cy * 7919.0) ^ (u32)(sys_time() * 1000.0);
		render_path_paint_stamp(cx, cy, aspect, diameter, true);
		g_context->brush_dab_x = cx;
		g_context->brush_dab_y = cy;
	}
	else {
		f32 dx    = (cx - g_context->brush_dab_x) * aspect;
		f32 dy    = cy - g_context->brush_dab_y;
		f32 dist  = math_sqrt(dx * dx + dy * dy);
		i32 count = (i32)(dist / step);
		if (count > 64) { // Keep the frame responsive on a fast stroke, the rest follows on the next frames
			count = 64;
		}
		f32 sx = g_context->brush_dab_x;
		f32 sy = g_context->brush_dab_y;
		for (i32 i = 1; i <= count; ++i) {
			f32 t = (step * i) / dist;
			f32 x = sx + (cx - sx) * t;
			f32 y = sy + (cy - sy) * t;
			render_path_paint_stamp(x, y, aspect, diameter, i == count);
			g_context->brush_dab_x = x;
			g_context->brush_dab_y = y;
		}
	}
	if (render_path_paint_batched()) {
		render_path_paint_batch_flush(true);
	}

	g_context->paint_vec.x       = cx;
	g_context->paint_vec.y       = cy;
	g_context->brush_dab_radius  = 1.0;
	g_context->brush_dab_opacity = 1.0;
	g_context->brush_dab_angle   = 0.0;
}

// Pen samples of the frame, drawn one by one so fast curves stay round and the pressure varies along the stroke.
// Only for plain strokes, smoothing, rulers and snapping work on the frame position.
typedef struct {
	f32 x;
	f32 y;
	f32 p; // Pressure
	f32 a; // Altitude, azimuth and roll of the pen
	f32 z;
	f32 r;
} render_path_paint_point_t;

// The brush uniforms read the pressure and angles of this point instead of the current pen
static void render_path_paint_point_use(render_path_paint_point_t *pt) {
	util_brush_sample_pressure = pt->p;
	util_brush_sample_altitude = pt->a;
	util_brush_sample_azimuth  = pt->z;
	util_brush_sample_roll     = pt->r;
}

static void render_path_paint_point_clear() {
	util_brush_sample_pressure = -1.0;
	util_brush_sample_altitude = -1.0;
}

static f32 render_path_paint_angle_lerp(f32 a, f32 b, f32 t) {
	f32 d = b - a;
	while (d > math_pi()) {
		d -= 2.0 * math_pi();
	}
	while (d < -math_pi()) {
		d += 2.0 * math_pi();
	}
	return a + d * t;
}

static render_path_paint_point_t render_path_paint_point_lerp(render_path_paint_point_t *a, render_path_paint_point_t *b, f32 t) {
	return (render_path_paint_point_t){a->x + (b->x - a->x) * t,
	                                   a->y + (b->y - a->y) * t,
	                                   a->p + (b->p - a->p) * t,
	                                   a->a + (b->a - a->a) * t,
	                                   render_path_paint_angle_lerp(a->z, b->z, t),
	                                   render_path_paint_angle_lerp(a->r, b->r, t)};
}

#define RENDER_PATH_PAINT_POINTS_MAX 128

bool render_path_paint_use_samples() {
	tool_type_t tool = g_context->tool;
	if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_BLUR) {
		return false;
	}
	if (pen_sample_count == 0 || !(pen_down("tip") || pen_released("tip")) || g_context->pdirty <= 0) {
		return false;
	}
	bool in_2d = context_in_2d_view(VIEW_2D_TYPE_LAYER);
	bool snap  = g_context->grid_snap_toggle || keymap_shortcut(any_map_get(g_keymap, "grid_snap"), SHORTCUT_TYPE_DOWN) ||
	            (g_config->view2d_grid_snap && in_2d) || context_is_pixel_snap();
	return g_context->brush_lazy_radius <= 0.0 && !context_is_ruler_paint() && !snap && !slot_layer_is_path(g_context->layer) &&
	       g_context->layer->fill_material == NULL && !context_is_decal();
}

// Frame samples in paint space, the last one is the brush position
static i32 render_path_paint_points(render_path_paint_point_t *out) {
	f32 w     = (f32)base_paint_view_w();
	f32 h     = (f32)base_paint_view_h();
	i32 count = 0;
	for (i32 i = 0; i < pen_sample_count; ++i) {
		pen_sample_t *s = &pen_samples[i];
		f32           x = base_paint_x(s->x);
		f32           y = base_paint_y(s->y);
		if (count > 0) {
			f32 dx = (x - out[count - 1].x) * w;
			f32 dy = (y - out[count - 1].y) * h;
			if (dx * dx + dy * dy < 0.25) { // Closer than half a pixel
				out[count - 1].p = s->pressure;
				out[count - 1].a = s->altitude;
				out[count - 1].z = s->azimuth;
				out[count - 1].r = s->roll;
				continue;
			}
		}
		if (count == RENDER_PATH_PAINT_POINTS_MAX) { // Too many for a frame, keep every other one
			for (i32 k = 0; k < count / 2; ++k) {
				out[k] = out[k * 2 + 1];
			}
			count /= 2;
		}
		out[count++] = (render_path_paint_point_t){x, y, s->pressure, s->altitude, s->azimuth, s->roll};
		// Drop the middle of three points when it adds nothing: within a third of a pixel of the line and the pressure about linear,
		// each point is a draw over the whole layer
		if (count >= 3) {
			render_path_paint_point_t *a  = &out[count - 3];
			render_path_paint_point_t *b  = &out[count - 2];
			render_path_paint_point_t *c  = &out[count - 1];
			f32                        ax = a->x * w, ay = a->y * h, bx = b->x * w, by = b->y * h, cx = c->x * w, cy = c->y * h;
			f32                        lx = cx - ax, ly = cy - ay;
			f32                        ll = lx * lx + ly * ly;
			f32                        t  = ll > 0.0 ? ((bx - ax) * lx + (by - ay) * ly) / ll : 0.0;
			t                             = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
			f32 dx = ax + lx * t - bx, dy = ay + ly * t - by;
			f32 dp = a->p + (c->p - a->p) * t - b->p;
			f32 da = a->a + (c->a - a->a) * t - b->a;
			if (dx * dx + dy * dy < 0.11 && dp < 0.02 && dp > -0.02 && da < 0.02 && da > -0.02) {
				*b = *c;
				count--;
			}
		}
	}
	if (count > 0) {
		out[count - 1].x = g_context->paint_vec.x;
		out[count - 1].y = g_context->paint_vec.y;
	}
	return count;
}

static bool                      render_path_paint_samples_started = false;
static render_path_paint_point_t render_path_paint_samples_last;
static f32                       render_path_paint_samples_carry = 0.0; // Distance since the last stamp

void render_path_paint_samples_reset() {
	render_path_paint_samples_started = false;
}

// One capsule per sample, from the previous one
static void render_path_paint_commands_samples() {
	render_path_paint_point_t pts[RENDER_PATH_PAINT_POINTS_MAX];
	i32                       count = render_path_paint_points(pts);
	if (!render_path_paint_samples_started) {
		render_path_paint_samples_last    = pts[0];
		render_path_paint_samples_started = true;
	}
	f32 cx = g_context->paint_vec.x;
	f32 cy = g_context->paint_vec.y;
	for (i32 i = 0; i < count; ++i) {
		g_context->last_paint_vec_x = render_path_paint_samples_last.x;
		g_context->last_paint_vec_y = render_path_paint_samples_last.y;
		g_context->paint_vec.x      = pts[i].x;
		g_context->paint_vec.y      = pts[i].y;
		render_path_paint_point_use(&pts[i]);
		if (render_path_paint_batched()) {
			render_path_paint_batch_add();
		}
		else {
			render_path_paint_commands_symmetry();
			render_path_paint_commands_paint(i == count - 1);
		}
		render_path_paint_samples_last = pts[i];
	}
	if (render_path_paint_batched()) {
		render_path_paint_batch_flush(true);
	}
	render_path_paint_point_clear();
	g_context->paint_vec.x     = cx;
	g_context->paint_vec.y     = cy;
}

// Stamps every spacing along the samples, the pressure interpolated between them
static void render_path_paint_commands_samples_stamps() {
	render_path_paint_point_t pts[RENDER_PATH_PAINT_POINTS_MAX];
	i32                       count    = render_path_paint_points(pts);
	f32                       cx       = g_context->paint_vec.x;
	f32                       cy       = g_context->paint_vec.y;
	f32                       aspect   = base_paint_view_w() / (f32)base_paint_view_h();
	f32                       diameter = util_layer_brush_screen_radius() * 2.0;
	f32                       step     = math_max(g_context->brush_spacing * diameter, 0.0005);
	i32                       stamps   = 0;
	i32                       first    = 0;

	if (g_context->brush_dab_x < 0.0 || !render_path_paint_samples_started) { // First stamp of the stroke
		render_path_paint_stamp_seed = (u32)(pts[0].x * 100003.0) ^ (u32)(pts[0].y * 7919.0) ^ (u32)(sys_time() * 1000.0);
		render_path_paint_point_use(&pts[0]);
		render_path_paint_stamp(pts[0].x, pts[0].y, aspect, diameter, count == 1);
		render_path_paint_samples_last    = pts[0];
		render_path_paint_samples_carry   = 0.0;
		render_path_paint_samples_started = true;
		g_context->brush_dab_x            = pts[0].x;
		g_context->brush_dab_y            = pts[0].y;
		first                             = 1;
	}

	render_path_paint_point_t a = render_path_paint_samples_last;
	for (i32 i = first; i < count && stamps < 256; ++i) {
		render_path_paint_point_t b   = pts[i];
		f32                       dx  = (b.x - a.x) * aspect;
		f32                       dy  = b.y - a.y;
		f32                       len = math_sqrt(dx * dx + dy * dy);
		f32                       d   = step - render_path_paint_samples_carry; // Distance along the segment to the next stamp
		while (d <= len && stamps < 256) {
			f32                       t  = len > 0.0 ? d / len : 1.0;
			render_path_paint_point_t at = render_path_paint_point_lerp(&a, &b, t);
			render_path_paint_point_use(&at);
			render_path_paint_stamp(at.x, at.y, aspect, diameter, false);
			stamps++;
			d += step;
		}
		render_path_paint_samples_carry = len - (d - step);
		a                               = b;
	}
	render_path_paint_samples_last = a;
	g_context->brush_dab_x         = a.x;
	g_context->brush_dab_y         = a.y;
	if (render_path_paint_batched()) {
		render_path_paint_batch_flush(false);
	}
	if (stamps > 0) {
		render_path_paint_dilate_after_paint();
	}

	render_path_paint_point_clear();
	g_context->paint_vec.x       = cx;
	g_context->paint_vec.y       = cy;
	g_context->brush_dab_radius  = 1.0;
	g_context->brush_dab_opacity = 1.0;
	g_context->brush_dab_angle   = 0.0;
}

void render_path_paint_draw() {
	if (!render_path_paint_paint_enabled()) {
		return;
	}

	if (g_config->brush_live && g_context->pdirty <= 0 && g_context->ddirty > 0 && g_context->brush_time == 0) {
		// gbuffer has been updated now but brush will lag 1 frame
		render_path_paint_commands_live_brush();
	}

	if (history_undo_layers != NULL) {
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
					render_path_paint_commands_paint(true);
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
					render_path_paint_commands_paint(true);
				}

				g_context->layer_filter = _layer_filter;
				context_select_paint_object(_paint_object);
				if (is_merged)
					g_context->merged_object->base->visible = _visible;
			}
			else if (render_path_paint_is_rt_bake()) {
				bool dispatched = false;
				if (!render_path_raytrace_bake_lightmap_busy()) {
					dispatched = render_path_raytrace_bake_commands(make_material_parse_paint_material);
					render_path_paint_dilate(true, false);
				}
				// The raytrace setup frame and frames waiting on a lightmap bake trace no rays, keep their sample
				if (!dispatched && g_context->pdirty > 0) {
					g_context->pdirty++;
				}
			}
			else {
				render_path_paint_commands_paint(true);
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
			render_path_paint_commands_paint(true);
		}
	}

	render_path_paint_clear_blend();

	if (g_context->paint2d) {
		render_path_paint_restore_plane_mesh();
	}
}

void render_path_paint_set_plane_mesh() {
	g_context->paint2d_view    = true;
	render_path_paint_painto   = g_context->paint_object;
	render_path_paint_visibles = u8_array_create_from_raw((u8[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		u8_array_push(render_path_paint_visibles, p->base->visible);
		p->base->visible = false;
	}
	if (g_context->merged_object != NULL) {
		render_path_paint_merged_object_visible = g_context->merged_object->base->visible;
		g_context->merged_object->base->visible = false;
	}

	// Camera looks down at the layer plane, the ortho bounds place the plane where the 2D view shows the texture
	camera_object_t *cam          = scene_camera;
	g_context->saved_camera       = cam->base->transform->local;
	render_path_paint_saved_ortho = cam->data->ortho;
	// Turned like the 2D view shows the layer, the camera rolls the other way around the plane center
	transform_set_matrix(cam->base->transform, mat4_translate(mat4_rot_z(ui_view2d_rotation), 0, 0, 1));
	f32 ratio = config_get_texture_res_y() / (float)config_get_texture_res_x();
	f32 tx, ty, tw, th;
	ui_view2d_tex_rect(ratio, &tx, &ty, &tw, &th);
	f32                 s     = ui_view2d_world_scale();
	f32                 cx    = tx + tw / 2.0;
	f32                 cy    = ty + th / 2.0 - ui_view2d_top();
	static f32_array_t *ortho = NULL;
	if (ortho == NULL) {
		ortho = f32_array_create(4);
	}
	ortho->buffer[0] = -cx / s;
	ortho->buffer[1] = (base_w() - cx) / s;
	ortho->buffer[2] = -(base_h() - cy) / s;
	ortho->buffer[3] = cy / s;
	cam->data->ortho = ortho;
	camera_object_build_proj(cam, -1.0);
	camera_object_build_mat(cam);

	bool tiled = ui_view2d_tiled_show;
	if (tiled && scene_get_child(".PlaneTiled") == NULL) {
		// 3x3 planes
		i16_array_t *posa = i16_array_create_from_raw(
		    (i16[]){
		        32767,  0, -32767, 0, 10922,  0, -10922, 0, 10922,  0, -32767, 0, 10922,  0, -10922, 0, -10922, 0, 10922,  0, -10922, 0, -10922, 0,
		        -10922, 0, 10922,  0, -32767, 0, 32767,  0, -32767, 0, 10922,  0, 10922,  0, 10922,  0, -10922, 0, 32767,  0, -10922, 0, 10922,  0,
		        32767,  0, 10922,  0, 10922,  0, 32767,  0, 10922,  0, 10922,  0, -10922, 0, -10922, 0, -32767, 0, 10922,  0, -32767, 0, -10922, 0,
		        32767,  0, -10922, 0, 10922,  0, 10922,  0, 10922,  0, -10922, 0, -10922, 0, -32767, 0, -32767, 0, -10922, 0, -32767, 0, -32767, 0,
		        10922,  0, -32767, 0, -10922, 0, -10922, 0, -10922, 0, -32767, 0, 32767,  0, -32767, 0, 32767,  0, -10922, 0, 10922,  0, -10922, 0,
		        10922,  0, -10922, 0, 10922,  0, 10922,  0, -10922, 0, 10922,  0, -10922, 0, 10922,  0, -10922, 0, 32767,  0, -32767, 0, 32767,  0,
		        10922,  0, 10922,  0, 10922,  0, 32767,  0, -10922, 0, 32767,  0, 32767,  0, 10922,  0, 32767,  0, 32767,  0, 10922,  0, 32767,  0,
		        -10922, 0, -10922, 0, -10922, 0, 10922,  0, -32767, 0, 10922,  0, 32767,  0, -10922, 0, 32767,  0, 10922,  0, 10922,  0, 10922,  0,
		        -10922, 0, -32767, 0, -10922, 0, -10922, 0, -32767, 0, -10922, 0, 10922,  0, -32767, 0, 10922,  0, -10922, 0, -10922, 0, -10922, 0,
		    },
		    216);
		i16_array_t *nora = i16_array_create_from_raw(
		    (i16[]){
		        0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
		        0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
		        0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
		        0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
		        0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
		    },
		    108);
		i16_array_t *texa = i16_array_create_from_raw(
		    (i16[]){
		        32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767,
		        32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767,
		        32767, 32767, 0,     0, 0, 32767, 32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,
		        32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,
		        32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,
		    },
		    108);
		u32_array_t *inda = u32_array_create_from_raw(
		    (u32[]){
		        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
		        27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
		    },
		    54);
		mesh_data_t *raw =
		    ALLOC_INIT(mesh_data_t, {.name          = ".PlaneTiled",
		                             .vertex_arrays = any_array_create_from_raw(
		                                 (void *[]){
		                                     ALLOC_INIT(vertex_array_t, {.attrib = "pos", .values = i16_array_create_from_array(posa), .data = "short4norm"}),
		                                     ALLOC_INIT(vertex_array_t, {.attrib = "nor", .values = i16_array_create_from_array(nora), .data = "short2norm"}),
		                                     ALLOC_INIT(vertex_array_t, {.attrib = "tex", .values = i16_array_create_from_array(texa), .data = "short2norm"}),
		                                 },
		                                 3),
		                             .index_array = u32_array_create_from_array(inda),
		                             .scale_pos   = 1.5,
		                             .scale_tex   = 1.0});
		mesh_data_t   *md       = mesh_data_create(raw);
		mesh_object_t *mo       = scene_get_child(".Plane")->ext;
		shader_data_t *material = mo->material;
		mesh_object_t *o        = scene_add_mesh_object(md, material, NULL);
		o->base->name           = ".PlaneTiled";
	}

	render_path_paint_planeo = scene_get_child(tiled ? ".PlaneTiled" : ".Plane")->ext;

	render_path_paint_planeo->base->visible          = true;
	g_context->paint_object                          = render_path_paint_planeo;
	render_path_paint_planeo->base->transform->loc   = (vec4_t){0.0, 0.0, 0.0, 1.0};
	render_path_paint_planeo->base->transform->rot   = quat_from_euler(-math_pi() / 2.0, 0, 0);
	render_path_paint_planeo->base->transform->scale = (vec4_t){2.0, 1.0, 2.0 * ratio, 1.0};
	transform_build_matrix(render_path_paint_planeo->base->transform);
}

void render_path_paint_restore_plane_mesh() {
	g_context->paint2d_view                 = false;
	render_path_paint_planeo->base->visible = false;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		g_project->_->paint_objects->buffer[i]->base->visible = render_path_paint_visibles->buffer[i];
	}
	if (g_context->merged_object != NULL) {
		g_context->merged_object->base->visible = render_path_paint_merged_object_visible;
	}
	g_context->paint_object = render_path_paint_painto;
	transform_set_matrix(scene_camera->base->transform, g_context->saved_camera);
	scene_camera->data->ortho = render_path_paint_saved_ortho;
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);
	g_context->ddirty = 2;

	render_path_base_draw_gbuffer();
}

void render_path_paint_bind_layers() {
	bool is_live          = g_config->brush_live && render_path_paint_live_layer_drawn > 0;
	bool is_material_tool = g_context->tool == TOOL_TYPE_MATERIAL;
	if (is_live || is_material_tool) {
		render_path_paint_use_live_layer(true);
	}

	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		char         *n = string_tmp("texpaint%d", l->id);
		render_path_bind_target(n, n);

		if (slot_layer_is_layer(l)) {
			n = string_tmp("texpaint_nor%d", l->id);
			render_path_bind_target(n, n);
			n = string_tmp("texpaint_pack%d", l->id);
			render_path_bind_target(n, n);
		}
	}

}

void render_path_paint_unbind_layers() {
	bool is_live          = g_config->brush_live && render_path_paint_live_layer_drawn > 0;
	bool is_material_tool = g_context->tool == TOOL_TYPE_MATERIAL;
	if (is_live || is_material_tool) {
		render_path_paint_use_live_layer(false);
	}
}

// The live layer is three full size textures, only the material tool and live brush need it all the time.
// For the brush preview it is created when needed and freed after a while.
static f64 render_path_paint_live_last_use = 0.0;

void render_path_paint_live_layer_used() {
	render_path_paint_live_last_use = iron_time();
}

void render_path_paint_live_layer_release_idle() {
	if (render_path_paint_live_layer == NULL || render_path_paint_live_layer_locked) {
		return;
	}
	if (g_context->tool == TOOL_TYPE_MATERIAL || g_config->brush_live) {
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
	if (g_context->brush_time > 0) {
		render_path_paint_dilate_base_pending = true;
		return;
	}
	render_path_paint_dilate(true, false);
}

void render_path_paint_dilate(bool base, bool nor_pack) {
	if (g_config->dilate_radius > 0 && !g_context->paint2d) {
		util_uv_cache_dilate_map();
		layers_make_temp_img();
		i32 tid = g_context->layer->id;

		char *format      = base_bits == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";
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
