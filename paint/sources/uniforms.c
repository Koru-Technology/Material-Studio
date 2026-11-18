
#include "global.h"

// Brush state as the paint shaders see it, also read by the dab batches (paint_exec_batch_add). While the paint
// executor runs a draw, the state it was emitted with: its first dab (paint_exec.c).
static f32 *uniforms_exec_dab() {
	paint_exec_op_t *op = paint_exec_current();
	return op != NULL ? paint_exec_dab(op->dab_first) : NULL;
}

f32 uniforms_brush_radius() {
	f32 *d = uniforms_exec_dab();
	if (d != NULL) {
		return d[12];
	}
	bool decal                   = context_is_decal();
	bool decal_mask              = context_is_decal_mask_paint_pass();
	f32  brush_decal_mask_radius = g_context->brush_decal_mask_radius;
	brush_decal_mask_radius *= 2.0;
	f32 radius = decal_mask ? brush_decal_mask_radius : g_context->brush_radius;
	f32 val    = (radius * g_context->brush_nodes_radius * g_context->brush_dab_radius) / 15.0;
	if (!decal) {
		val *= util_brush_dynamic(g_context->brush_pressure_size, g_context->brush_tilt_size, true);
	}
	f32 scale2d = base_scale2d(base_h());
	if (!decal) {
		val *= 2;
	}
	else if (decal_mask) {
		val *= scale2d;
	}
	else {
		val *= scale2d * 2.0;
	}
	return val;
}

f32 uniforms_brush_flow() {
	f32 *d = uniforms_exec_dab();
	if (d != NULL) {
		return d[15];
	}
	bool stamps = g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE;
	return stamps && !slot_layer_is_path(g_context->layer) ? g_context->brush_flow * util_brush_dynamic(g_context->brush_pressure_flow, 0.0, false) : 1.0;
}

f32 uniforms_brush_opacity() {
	f32 *d = uniforms_exec_dab();
	if (d != NULL) {
		return d[13];
	}
	f32 val = g_context->brush_opacity * g_context->brush_nodes_opacity * g_context->brush_dab_opacity;
	val *= util_brush_dynamic(g_context->brush_pressure_opacity, g_context->brush_tilt_opacity, false);
	return math_max(0.0, math_min(1.0, val));
}

f32 uniforms_brush_hardness() {
	f32 *d = uniforms_exec_dab();
	if (d != NULL) {
		return d[14];
	}
	bool decal_mask = context_is_decal_mask_paint_pass();
	if (g_context->tool != TOOL_TYPE_BRUSH && g_context->tool != TOOL_TYPE_ERASER && g_context->tool != TOOL_TYPE_CLONE && !decal_mask) {
		return 1.0;
	}
	f32 val = fmaxf((g_context->brush_hardness * g_context->brush_nodes_hardness) - 0.02, 0.0);
	val *= util_brush_dynamic(g_context->brush_pressure_hardness, 0.0, false);
	if (g_context->brush_tilt_shading) { // The side of the lead paints a softer edge
		val *= 1.0 - 0.6 * util_brush_pen_tilt();
	}
	val *= val;
	return val;
}

vec2_t uniforms_brush_dab_angle() {
	f32 *d = uniforms_exec_dab();
	if (d != NULL) {
		return (vec2_t){d[30], d[31]};
	}
	return (vec2_t){math_cos(-g_context->brush_dab_angle), math_sin(-g_context->brush_dab_angle)};
}

vec2_t uniforms_brush_angle() {
	f32 *d = uniforms_exec_dab();
	if (d != NULL) {
		return (vec2_t){d[28], d[29]};
	}
	f32 angle = util_brush_tip_angle();
	return (vec2_t){math_cos(-angle), math_sin(-angle)};
}

vec4_t uniforms_input_brush() {
	f32 *d = uniforms_exec_dab();
	if (d != NULL) {
		return (vec4_t){d[16], d[17], paint_exec_current()->down, 0.0};
	}
	bool   down = mouse_down("left") || pen_down("tip");
	vec4_t v    = (vec4_t){g_context->paint_vec.x, g_context->paint_vec.y, down ? 1.0 : 0.0, 0.0};
	return v;
}

vec4_t uniforms_input_brush_last() {
	f32 *d = uniforms_exec_dab();
	if (d != NULL) {
		return (vec4_t){d[18], d[19], paint_exec_current()->down, 0.0};
	}
	bool   down = mouse_down("left") || pen_down("tip");
	vec4_t v    = (vec4_t){g_context->last_paint_vec_x, g_context->last_paint_vec_y, down ? 1.0 : 0.0, 0.0};
	return v;
}

i32 uniforms_ext_i32_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_bloom_current_mip")) {
		return render_path_base_bloom_current_mip;
	}
	return INT_MAX;
}

f32 uniforms_ext_f32_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_mm_zero")) { // Added to Material Maker parameters, so the compiler does not fold them (see
		return 0.0;                        // mm_uniform_runtime_declaration)
	}
	if (string_equals(link, "_paint_aspect")) { // The window's aspect, replaying the recorded camera's (paint_rec_cam_t)
		return paint_exec_replay ? paint_replay_cam.aspect : sys_aspect();
	}
	if (starts_with(link, "_layer_opacity")) { // _layer_opacity<layer id>, see make_mesh_opacity
		i32 id = atoi(link + 14);
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (l->id == id) {
				return slot_layer_get_opacity(l);
			}
		}
		return 1.0;
	}
	if (starts_with(link, "_layer_raise")) { // _layer_raise<layer id>, see make_mesh_write_raise
		i32 id = atoi(link + 12);
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (l->id == id) {
				return l->height_raise;
			}
		}
		return 0.0;
	}
	if (string_equals(link, "_brush_radius")) {
		return uniforms_brush_radius();
	}
	else if (string_equals(link, "_pixel_snap_2d")) {
		return g_context->paint2d_view ? 1.0f : 0.0f;
	}
	else if (string_equals(link, "_vignette_strength")) {
		return g_config->rp_vignette * 0.1;
	}
	else if (string_equals(link, "_grain_strength")) {
		return g_config->rp_grain;
	}
	else if (string_equals(link, "_contrast_strength")) {
		return g_config->rp_contrast;
	}
	else if (string_equals(link, "_gamma_strength")) {
		return g_config->rp_gamma;
	}
	else if (string_equals(link, "_tonemap_strength")) {
		bool tonemap = g_context->viewport_mode == VIEWPORT_MODE_LIT || g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE;
		return tonemap ? (f32)(g_config->rp_view_transform + 1) : 0.0; // 1 filmic, 2 aces, 3 agx, 4 standard
	}
	else if (string_equals(link, "_lut_size")) {
		return lut_image != NULL ? (f32)lut_size : 0.0;
	}
	else if (string_equals(link, "_bloom_sample_scale")) {
		return render_path_base_bloom_sample_scale;
	}
	else if (string_equals(link, "_bloom_strength")) {
		f32 base = g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE ? 0.2 : 0.02;
		return base * g_config->rp_bloom;
	}
	else if (string_equals(link, "_brush_flow")) {
		return uniforms_brush_flow();
	}
	else if (string_equals(link, "_brush_strength")) {
		return g_context->brush_strength;
	}
	else if (string_equals(link, "_brush_scale_x")) {
		return 1 / (float)g_context->brush_scale_x;
	}
	else if (string_equals(link, "_picker_color_mask_tolerance")) {
		return g_context->picker_color_mask_tolerance;
	}
	else if (string_equals(link, "_brush_opacity")) {
		return uniforms_brush_opacity();
	}
	else if (string_equals(link, "_brush_hardness")) {
		return uniforms_brush_hardness();
	}
	else if (string_equals(link, "_brush_scale")) {
		// The decal's, else the material's
		f32 base = context_is_decal() ? g_context->brush_scale : g_context->material->uv_scale;
		f32 val  = base * g_context->brush_nodes_scale;
		return val;
	}
	else if (string_equals(link, "_mask_value")) {
		return g_context->mask_value;
	}
	else if (string_equals(link, "_bump_scale")) {
		return layers_bump_scale(config_get_texture_res_x());
	}
	else if (string_equals(link, "_atlas_stride")) {
		return util_mesh_udim_active() ? util_mesh_atlas_stride() : 1.0;
	}
	else if (string_equals(link, "_object_id")) {
		return array_index_of(g_project->_->paint_objects, object->ext);
	}
	else if (string_equals(link, "_dilate_radius")) {
		return util_uv_dilatemap != NULL ? g_config->dilate_radius : 0.0;
	}
	else if (string_equals(link, "_particle_radius")) { // A particle's draw (paint_exec_particles)
		paint_exec_op_t *op = paint_exec_current();
		return op != NULL ? op->particle_hit.w : 0.1f;
	}
	else if (string_equals(link, "_paint_dab_count")) {
		paint_exec_op_t *op = paint_exec_current();
		return op != NULL ? op->dab_count : 0.0;
	}
	else if (string_equals(link, "_paint_dab_replay")) {
		return paint_exec_replay ? 1.0 : 0.0;
	}
	else if (string_equals(link, "_paint_dab_first")) {
		paint_exec_op_t *op = paint_exec_current();
		return op != NULL ? op->dab_first : 0.0;
	}
	else if (string_equals(link, "_brush_camera_align")) {
		return context_is_brush_camera_align() ? 1.0f : 0.0f;
	}
	else if (string_equals(link, "_picker_opacity")) {
		return g_context->picked_color->opacity;
	}
	else if (string_equals(link, "_picker_occlusion")) {
		return g_context->picked_color->occlusion;
	}
	else if (string_equals(link, "_picker_roughness")) {
		return g_context->picked_color->roughness;
	}
	else if (string_equals(link, "_picker_metallic")) {
		return g_context->picked_color->metallic;
	}
	else if (string_equals(link, "_picker_height")) {
		return g_context->picked_color->height;
	}
	else if (string_equals(link, "_taa_blend")) {
		return scene_camera->frame == 0 ? 0.0 : 0.5;
	}
	else if (string_equals(link, "_ssao_strength")) {
		return g_config->rp_ssao * 0.95;
	}
	else if (string_equals(link, "_ssao_frame")) {
		return scene_camera->frame % 2 == 0 ? 0.0 : 0.5;
	}
	else if (string_equals(link, "_path_length")) {
		return util_layer_path_length();
	}
	return NAN;
}

// Texture rotation of the material painted on paint layers, degrees
static f32 uniforms_material_uv_angle() {
	return g_context->material->uv_angle;
}

vec2_t uniforms_ext_vec2_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_gbuffer_size")) { // Of the uv map clone and smudge read
		gpu_texture_t *uv = render_path_target_image(render_path_paint_uv_target());
		return (vec2_t){uv->width, uv->height};
	}
	else if (string_equals(link, "_clone_delta")) {
		return (vec2_t){g_context->clone_delta_x, g_context->clone_delta_y};
	}
	else if (string_equals(link, "_grab_start")) {
		return (vec2_t){g_context->grab_start_x, g_context->grab_start_y};
	}
	else if (string_equals(link, "_texpaint_size")) {
		return (vec2_t){config_get_texture_res_x(), config_get_texture_res_y()};
	}
	else if (string_equals(link, "_brush_dab_angle")) {
		return uniforms_brush_dab_angle();
	}
	else if (string_equals(link, "_uv_angle")) {
		// Rotation of the material texture: the brush tip's for decals, else the material's
		if (context_is_decal()) {
			return uniforms_brush_angle();
		}
		f32 angle = uniforms_material_uv_angle();
		angle *= (math_pi() / 180.0);
		return (vec2_t){math_cos(-angle), math_sin(-angle)};
	}
	else if (string_equals(link, "_brush_angle")) {
		return uniforms_brush_angle();
	}
	else if (string_equals(link, "_brush_tilt")) {
		f32 *d = uniforms_exec_dab();
		return d != NULL ? (vec2_t){d[26], d[27]} : util_brush_tilt_shape();
	}
	else if (string_equals(link, "_paint_res0")) { // The resolution recorded with the stroke, live the layer's
		if (paint_exec_res0.x > 0.0) {
			return paint_exec_res0;
		}
		return (vec2_t){config_get_texture_res_x(), config_get_texture_res_y()};
	}
	else if (string_equals(link, "_undo_tile_size")) {
		return (vec2_t){(f32)history_tiles_block, (f32)history_tiles_blocks};
	}
	return vec2_nan();
}

// Direction of a directional brush from the previous dab, moves the previous dab on: the paint executor asks once per dab
vec4_t uniforms_brush_direction_now() {
	// Discard first paint for directional brush (no prev position yet)
	bool allow_paint = g_context->prev_paint_vec_x > 0 && g_context->prev_paint_vec_y > 0 &&
	                   (g_context->prev_paint_vec_x != g_context->paint_vec.x || g_context->prev_paint_vec_y != g_context->paint_vec.y);
	f32    x                    = g_context->paint_vec.x;
	f32    y                    = g_context->paint_vec.y;
	f32    lastx                = g_context->prev_paint_vec_x;
	f32    lasty                = g_context->prev_paint_vec_y;
	f32    angle                = math_atan2(-y + lasty, x - lastx) - math_pi() / 2.0;
	vec4_t v                    = (vec4_t){math_cos(angle), math_sin(angle), allow_paint ? 1 : 0, 1.0};
	g_context->prev_paint_vec_x = g_context->last_paint_vec_x;
	g_context->prev_paint_vec_y = g_context->last_paint_vec_y;
	return v;
}

vec4_t uniforms_pixel_snap_uv() {
	if (!g_context->paint2d_view) {
		return (vec4_t){0.0, 0.0, 0.0, 0.0};
	}
	f32 tx, ty, tw, th;
	ui_view2d_tex_rect(config_get_texture_res_y() / (float)config_get_texture_res_x(), &tx, &ty, &tw, &th);
	return (vec4_t){(g_context->paint_vec.x * base_w() - tx) / tw, (g_context->paint_vec.y * base_h() - ty) / th,
	                (g_context->last_paint_vec_x * base_w() - tx) / tw, (g_context->last_paint_vec_y * base_h() - ty) / th};
}

vec4_t uniforms_ext_vec3_link(object_t *object, shader_data_t *mat, char *link) {
	vec4_t v = vec4_nan();
	if (string_equals(link, "_brush_direction")) {
		paint_exec_op_t *op = paint_exec_current();
		return op != NULL ? op->direction : uniforms_brush_direction_now(); // The executor's has no side effect
	}
	else if (string_equals(link, "_atlas_transform")) {
		if (!util_mesh_udim_active() || render_path_raytrace_override_pass) {
			return (vec4_t){0.0, 0.0, 1.0, 1.0};
		}
		i32 stride = util_mesh_atlas_stride();
		i32 slot   = util_mesh_atlas_slot(object);
		if (slot < 0) {
			return (vec4_t){0.0, 0.0, 1.0, 1.0};
		}
		return (vec4_t){(slot % stride) / (f32)stride, (slot / stride) / (f32)stride, 1.0 / stride, 1.0};
	}
	else if (string_equals(link, "_picker_base")) {
		v = (vec4_t){color_get_rb(g_context->picked_color->base) / 255.0, color_get_gb(g_context->picked_color->base) / 255.0,
		             color_get_bb(g_context->picked_color->base) / 255.0, 1.0};
		return v;
	}
	else if (string_equals(link, "_picker_normal")) {
		v = (vec4_t){color_get_rb(g_context->picked_color->normal) / 255.0, color_get_gb(g_context->picked_color->normal) / 255.0,
		             color_get_bb(g_context->picked_color->normal) / 255.0, 1.0};
		return v;
	}
	else if (string_equals(link, "_particle_hit")) {
		paint_exec_op_t *op = paint_exec_current();
		v                   = op != NULL ? (vec4_t){op->particle_hit.x, op->particle_hit.y, op->particle_hit.z, 1.0} : (vec4_t){0.0, 0.0, 0.0, 1.0};
		return v;
	}
	else if (string_equals(link, "_particle_hit_last")) {
		paint_exec_op_t *op = paint_exec_current();
		v                   = op != NULL ? (vec4_t){op->particle_last.x, op->particle_last.y, op->particle_last.z, 1.0} : (vec4_t){0.0, 0.0, 0.0, 1.0};
		return v;
	}
	else if (string_equals(link, "_camera_right")) {
		v = paint_exec_replay ? paint_replay_cam.right : camera_object_right_world(scene_camera); // Replaying: as recorded
		return v;
	}
	else if (string_equals(link, "_camera_up")) {
		v = paint_exec_replay ? paint_replay_cam.up : camera_object_up_world(scene_camera);
		return v;
	}
	return v;
}

// Decals: the anchor on screen (decal_x, y), 1 in the mask pass, the mask's radius
vec4_t uniforms_decal_mask() {
	bool decal_mask = context_is_decal_mask_paint_pass();
	f32  val        = (g_context->brush_radius * g_context->brush_nodes_radius) / 15.0;
	f32  scale2d    = base_scale2d(base_h());
	val *= scale2d * 2.0;
	return (vec4_t){g_context->decal_x, g_context->decal_y, decal_mask ? 1 : 0, val};
}

vec4_t uniforms_ext_vec4_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_pixel_snap_uv")) {
		paint_exec_op_t *op = paint_exec_current();
		return op != NULL ? op->pixel_snap_uv : uniforms_pixel_snap_uv();
	}
	if (string_equals(link, "_paint_uv_tile")) {
		return paint_exec_uv_tile;
	}
	if (string_equals(link, "_fill_uv_tile")) { // Fill rasterizing, util_fill.c
		return fill_uv_tile;
	}
	if (string_equals(link, "_fill_normal")) { // Angle fill: the picked triangle's (render_path_paint.c)
		mesh_object_t *o = render_path_paint_fill_object;
		return o != NULL ? util_mesh_triangle_normal(o, render_path_paint_fill_tri) : (vec4_t){0.0, 0.0, 0.0, 0.0};
	}
	if (string_equals(link, "_paint_dab_aabb_min") || string_equals(link, "_paint_dab_aabb_size")) {
		mat4_t obj, obj_inv;
		vec4_t bmin, bsize;
		paint_exec_space(&obj, &obj_inv, &bmin, &bsize);
		return string_equals(link, "_paint_dab_aabb_min") ? bmin : bsize;
	}
	if (string_equals(link, "_input_brush")) {
		return uniforms_input_brush();
	}
	else if (string_equals(link, "_input_brush_last")) {
		return uniforms_input_brush_last();
	}
	else if (string_equals(link, "_path_a")) {
		return util_layer_path_segment_a();
	}
	else if (string_equals(link, "_path_b")) {
		return util_layer_path_segment_b();
	}
	else if (string_equals(link, "_envmap_data")) {
		return (vec4_t){g_context->envmap_angle, math_sin(-g_context->envmap_angle), math_cos(-g_context->envmap_angle), scene_world->strength * 2.0};
	}
	else if (string_equals(link, "_envmap_data_world")) {
		bool tonemap = g_context->viewport_mode == VIEWPORT_MODE_LIT || g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE;
		return (vec4_t){g_context->envmap_angle, tonemap ? 0.0 : (f32)(g_config->rp_view_transform + 1), 0.0, g_context->show_envmap ? scene_world->strength : 1.0};
	}
	else if (string_equals(link, "_stencil_transform")) {
		vec4_t v = (vec4_t){g_context->brush_stencil_x, g_context->brush_stencil_y, g_context->brush_stencil_scale, g_context->brush_stencil_angle};
		return v;
	}
	else if (string_equals(link, "_decal_mask")) {
		paint_exec_op_t *op = paint_exec_current();
		return op != NULL ? op->decal_mask : uniforms_decal_mask();
	}

	return render_pass_get_vec4(link);
}

mat4_t uniforms_ext_mat4_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_taa_inv_vp")) { // Reprojection, see taa_pass.shader
		return mat4_inv(mat4_mult_mat(scene_camera->v, scene_camera->no_jitter_p));
	}
	if (string_equals(link, "_taa_prev_vp")) {
		return render_path_base_taa_prev_vp;
	}
	if (string_equals(link, "_paint_dab_obj") || string_equals(link, "_paint_dab_obj_inv")) { // Object space of the dab resolve pass
		mat4_t obj, obj_inv;
		vec4_t bmin, bsize;
		paint_exec_space(&obj, &obj_inv, &bmin, &bsize);
		return string_equals(link, "_paint_dab_obj") ? obj : obj_inv;
	}
	return mat4_nan();
}

void uniforms_ext_cache_dilate_map(void *_) {
	util_uv_cache_dilate_map();
}

void uniforms_ext_cache_uv_map(void *_) {
	util_uv_cache_uv_map();
}

static gpu_texture_t *_uniforms_ext_get_target(char *name) {
	return render_path_target_image(name);
}

// A layer texture bound as a render target of the current pass reads as black
static gpu_texture_t *uniforms_layer_tex(gpu_texture_t *t) {
	for (i32 i = 0; gpu_in_use && i < current_render_targets_count; ++i) {
		if (t == current_render_targets[i]) {
			return render_path_empty_black();
		}
	}
	return t;
}

gpu_texture_t *uniforms_ext_tex_link(object_t *object, shader_data_t *mat, char *link) {
	if (starts_with(link, "_matcache")) { // The current material's outputs, see util_material_cache.c
		return material_cache_texture(link[9] - '0');
	}
	if (string_equals(link, "_texfill")) { // Fill layers: the parameters, see util_fill.c
		return fill_params_texture();
	}
	if (starts_with(link, "_fillimg")) { // _fillimg<asset id>
		return fill_image_texture(atoi(link + 8));
	}
	if (starts_with(link, "_filltile")) { // _filltile<material uid>_<k>
		char *us = strchr(link, '_' ) ;
		u32   uid = (u32)strtoul(link + 9, &us, 10);
		return fill_tile_texture(uid, us != NULL && *us == '_' ? atoi(us + 1) : 0);
	}
	// The layer before the stroke: replaying, the target as it was, else history_tiles_stroke_source
	if (string_equals(link, "_texpaint_undo") || string_equals(link, "_texpaint_nor_undo") || string_equals(link, "_texpaint_pack_undo")) {
		i32 i = string_equals(link, "_texpaint_undo") ? 0 : string_equals(link, "_texpaint_nor_undo") ? 1 : 2;
		if (paint_exec_replay) {
			return paint_replay_s0[i] != NULL ? paint_replay_s0[i] : paint_replay_s0[0];
		}
		return history_tiles_stroke_source(i);
	}
	if (string_equals(link, "_texpaint_ref")) {
		return _uniforms_ext_get_target("texpaint_ref");
	}
	else if (string_equals(link, "_texpaint_nor_ref")) {
		return _uniforms_ext_get_target("texpaint_nor_ref");
	}
	else if (string_equals(link, "_texpaint_pack_ref")) {
		return _uniforms_ext_get_target("texpaint_pack_ref");
	}
	else if (string_equals(link, "_texviewport")) {
		gpu_texture_t *image = g_context->viewport_texture;
		return image != NULL ? image : render_path_empty_black();
	}
	else if (string_equals(link, "_texinpaint_overlay")) {
		gpu_texture_t *overlay = box_bake_maps_inpaint_overlay();
		return overlay != NULL ? overlay : render_path_empty_black();
	}
	else if (string_equals(link, "_texcolorid")) {
		asset_t *map = project_id_map_asset();
		if (map == NULL) {
			return render_path_empty_white();
		}
		return project_get_image(map);
	}
	else if (starts_with(link, "_texmeshmap")) { // _texmeshmap<mesh_map_t>
		return mesh_maps_texture(parse_int(link + 11));
	}
	else if (string_equals(link, "_texcolorid_sel")) {
		return render_path_target_image("texpaint_colorid_sel");
	}
	else if (string_equals(link, "_textexttool")) { // Opacity map for text
		return g_context->text_tool_image;
	}
	else if (string_equals(link, "_texpaintdabs")) {
		gpu_texture_t *t = paint_exec_current() != NULL ? render_path_target_image("texpaint_dabs_resolved") : NULL;
		return t != NULL ? t : render_path_empty_black();
	}
	else if (string_equals(link, "_texpaintdabsin")) {
		gpu_texture_t *t = paint_exec_dabs_in();
		return t != NULL ? t : render_path_empty_black();
	}
	else if (string_equals(link, "_texbrushmask")) {
		return g_context->brush_mask_image;
	}
	else if (string_equals(link, "_texbrushstencil")) {
		return g_context->brush_stencil_image;
	}
	else if (string_equals(link, "_texuvmap")) {
		if (!util_uv_uvmap_cached) {
			sys_notify_on_next_frame(&uniforms_ext_cache_uv_map, NULL);
		}
		return util_uv_uvmap;
	}
	else if (string_equals(link, "_texdilatemap") && paint_exec_replay && paint_replay_dilatemap != NULL) {
		return paint_replay_dilatemap;
	}
	else if (string_equals(link, "_texdilatemap")) {
		if (util_uv_dilatemap_cached) {
			return util_uv_dilatemap;
		}
		sys_notify_on_next_frame(&uniforms_ext_cache_dilate_map, NULL);
		return render_path_empty_white();
	}
	else if (string_equals(link, "_texdilatemap")) {
		return util_uv_dilatemap;
	}
	if (starts_with(link, "_texpaint_pack_vert")) {
		return render_path_target_image(string_tmp("texpaint_pack%c", link[string_length(link) - 1]));
	}
	if (starts_with(link, "_texpaint_vert")) {
		i32 tid = parse_int(link + string_length("_texpaint_vert"));
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			if (g_project->_->layers->buffer[i]->id == tid) {
				return g_project->_->layers->buffer[i]->texpaint;
			}
		}
		return NULL;
	}
	// Layer and Layer Mask nodes, the index follows the prefix
	if (starts_with(link, "_texpaint_nor")) {
		i32 tid = parse_int(link + string_length("_texpaint_nor"));
		return tid < g_project->_->layers->length ? uniforms_layer_tex(g_project->_->layers->buffer[tid]->texpaint_nor) : NULL;
	}
	if (starts_with(link, "_texpaint_pack")) {
		i32 tid = parse_int(link + string_length("_texpaint_pack"));
		return tid < g_project->_->layers->length ? uniforms_layer_tex(g_project->_->layers->buffer[tid]->texpaint_pack) : NULL;
	}
	if (starts_with(link, "_texpaint")) {
		i32 tid = parse_int(link + string_length("_texpaint"));
		if (tid >= g_project->_->layers->length) {
			return NULL;
		}
		slot_layer_t *l = g_project->_->layers->buffer[tid];
		return uniforms_layer_tex(slot_layer_is_mask(l) ? slot_layer_mask_texture(l) : l->texpaint); // Masks read their evaluated stack
	}
	if (starts_with(link, "_texblur_")) {
		char *id = link + 9;
		if (g_context->node_previews != NULL) {
			return any_map_get(g_context->node_previews, id);
		}
		else {
			return render_path_empty_black();
		}
	}
	if (starts_with(link, "_texwarp_")) {
		char *id = link + 9;
		if (g_context->node_previews != NULL) {
			return any_map_get(g_context->node_previews, id);
		}
		else {
			return render_path_empty_black();
		}
	}
	if (starts_with(link, "_mmbuf")) { // Material Maker buffers, see mm_buffers_link
		gpu_texture_t *buf = mm_buffers_texture(link);
		return buf != NULL ? buf : render_path_empty_black();
	}
	if (starts_with(link, "_texlive")) { // Material Studio preview values
		gpu_texture_t *live = parser_material_live_texture(link);
		if (live != NULL) {
			return live;
		}
		return render_path_empty_black();
	}
	if (starts_with(link, "_texbake_")) {
		char *id = link + 9;
		if (g_context->node_previews != NULL) {
			return any_map_get(g_context->node_previews, id);
		}
		else {
			return render_path_empty_black();
		}
	}
	if (string_equals(link, "_camera_texture")) {
		return render_path_target_image("last");
	}
	if (string_equals(link, "_lut_tex")) {
		if (lut_image == NULL) {
			return render_path_empty_white();
		}
		return lut_image;
	}
	return NULL;
}

void uniforms_ext_init() {
	uniforms_i32_links  = uniforms_ext_i32_link;
	uniforms_f32_links  = uniforms_ext_f32_link;
	uniforms_vec2_links = uniforms_ext_vec2_link;
	uniforms_vec3_links = uniforms_ext_vec3_link;
	uniforms_vec4_links = uniforms_ext_vec4_link;
	uniforms_mat4_links = uniforms_ext_mat4_link;
	uniforms_tex_links  = uniforms_ext_tex_link;
}
