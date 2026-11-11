
#include "global.h"

i32 uniforms_ext_i32_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_bloom_current_mip")) {
		return render_path_base_bloom_current_mip;
	}
	return INT_MAX;
}

f32 uniforms_ext_f32_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_brush_radius")) {
		bool decal                   = context_is_decal();
		bool decal_mask              = context_is_decal_mask_paint_pass();
		f32  brush_decal_mask_radius = g_context->brush_decal_mask_radius;
		brush_decal_mask_radius *= 2.0;
		f32 radius = decal_mask ? brush_decal_mask_radius : g_context->brush_radius;
		f32 val    = (radius * g_context->brush_nodes_radius * g_context->brush_dab_radius) / 15.0;
		if (!decal) {
			val *= util_brush_dynamic(g_config->dyn_size);
		}
		f32 scale2d = (900 / (float)base_h()) * g_config->window_scale;
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
		bool stamps = g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE;
		return stamps && !slot_layer_is_path(g_context->layer) ? g_context->brush_flow * util_brush_dynamic(g_config->dyn_flow) : 1.0;
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
		f32 val = g_context->brush_opacity * g_context->brush_nodes_opacity * g_context->brush_dab_opacity;
		val *= util_brush_dynamic(g_config->dyn_opacity);
		return math_max(0.0, math_min(1.0, val));
	}
	else if (string_equals(link, "_brush_hardness")) {
		bool decal_mask = context_is_decal_mask_paint_pass();
		if (g_context->tool != TOOL_TYPE_BRUSH && g_context->tool != TOOL_TYPE_ERASER && g_context->tool != TOOL_TYPE_CLONE && !decal_mask) {
			return 1.0;
		}
		f32 val = fmaxf((g_context->brush_hardness * g_context->brush_nodes_hardness) - 0.02, 0.0);
		val *= util_brush_dynamic(g_config->dyn_hardness);
		val *= val;
		return val;
	}
	else if (string_equals(link, "_brush_scale")) {
		bool fill = g_context->layer->fill_material != NULL;
		f32  val  = (fill ? g_context->layer->scale : g_context->brush_scale) * g_context->brush_nodes_scale;
		return val;
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
	else if (string_equals(link, "_particle_radius")) {
		i32 idx           = g_context->particle_index;
		f32 speed         = g_context->particles[idx].body != NULL ? physics_body_get_speed(g_context->particles[idx].body) : 0.0f;
		f32 vel_scale     = fminf(speed / 0.12f, 1.0f);
		f32 contact_scale = 1.0f - fminf(g_context->particles[idx].contact_time, 1.0f);
		return fmaxf(g_context->brush_radius * vel_scale * contact_scale, 0.1f);
	}
	else if (string_equals(link, "_decal_layer_dim")) {
		vec4_t sc = mat4_get_scale(g_context->layer->decal_mat);
		return sc.z * 0.5;
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
	else if (string_equals(link, "_timeline_frame")) {
		return (f32)tab_timeline_selected_frame;
	}
	else if (string_equals(link, "_path_length")) {
		return util_layer_path_length();
	}
	if (parser_material_script_links != NULL && link[0] == '_') {
		char *script = any_map_get(parser_material_script_links, link + 1);
		if (script != NULL) {
			f32 result = 0.0;
			if (project_scripts_trusted && !string_equals(script, "")) {
				minic_ctx_t *_ctx = minic_eval(string_tmp("float main() { return %s; }", script));
				result            = minic_ctx_result(_ctx);
				minic_ctx_free(_ctx);
			}
			return result;
		}
	}
	return NAN;
}

vec2_t uniforms_ext_vec2_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_gbuffer_size")) {
		render_target_t *gbuffer2 = any_map_get(render_path_render_targets, "gbuffer2");
		return (vec2_t){gbuffer2->_image->width, gbuffer2->_image->height};
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
		return (vec2_t){math_cos(-g_context->brush_dab_angle), math_sin(-g_context->brush_dab_angle)};
	}
	else if (string_equals(link, "_brush_angle")) {
		f32 brush_angle = g_context->brush_angle + g_context->brush_nodes_angle;
		f32 angle       = g_context->layer->fill_material != NULL ? g_context->layer->angle : brush_angle;
		angle *= (math_pi() / 180.0);
		if (g_context->layer->fill_material == NULL && !slot_layer_is_path(g_context->layer)) {
			if (g_config->dyn_angle == 1 && (pen_down("tip") || pen_released("tip"))) {
				angle += util_brush_pen_pressure() * math_pi(); // Pressure turns the brush up to half a turn
			}
			else if (g_config->dyn_angle == 2 && g_context->brush_time > 0) {
				angle += g_context->brush_speed * math_pi(); // Faster strokes turn the brush further
			}
			else if (g_config->dyn_angle == 3 && g_context->brush_time > 0) {
				angle += g_context->brush_stroke_angle; // Follows the stroke
			}
		}
		return (vec2_t){math_cos(-angle), math_sin(-angle)};
	}
	return vec2_nan();
}

vec4_t uniforms_ext_vec3_link(object_t *object, shader_data_t *mat, char *link) {
	vec4_t v = vec4_nan();
	if (string_equals(link, "_brush_direction")) {
		// Discard first paint for directional brush (no prev position yet)
		bool allow_paint = g_context->prev_paint_vec_x > 0 && g_context->prev_paint_vec_y > 0 &&
		                   (g_context->prev_paint_vec_x != g_context->paint_vec.x || g_context->prev_paint_vec_y != g_context->paint_vec.y);
		f32 x                       = g_context->paint_vec.x;
		f32 y                       = g_context->paint_vec.y;
		f32 lastx                   = g_context->prev_paint_vec_x;
		f32 lasty                   = g_context->prev_paint_vec_y;
		f32 angle                   = math_atan2(-y + lasty, x - lastx) - math_pi() / 2.0;
		v                           = (vec4_t){math_cos(angle), math_sin(angle), allow_paint ? 1 : 0, 1.0};
		g_context->prev_paint_vec_x = g_context->last_paint_vec_x;
		g_context->prev_paint_vec_y = g_context->last_paint_vec_y;
		return v;
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
	else if (string_equals(link, "_decal_layer_loc")) {
		v = (vec4_t){g_context->layer->decal_mat.m30, g_context->layer->decal_mat.m31, g_context->layer->decal_mat.m32, 1.0};
		return v;
	}
	else if (string_equals(link, "_decal_layer_nor")) {
		v = (vec4_t){g_context->layer->decal_mat.m20, g_context->layer->decal_mat.m21, g_context->layer->decal_mat.m22, 1.0};
		v = vec4_norm(v);
		return v;
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
		v = (vec4_t){g_context->particle_hit_x, g_context->particle_hit_y, g_context->particle_hit_z, 1.0};
		return v;
	}
	else if (string_equals(link, "_particle_hit_last")) {
		v = (vec4_t){g_context->last_particle_hit_x, g_context->last_particle_hit_y, g_context->last_particle_hit_z, 1.0};
		return v;
	}
	else if (string_equals(link, "_camera_right")) {
		v = camera_object_right_world(scene_camera);
		return v;
	}
	else if (string_equals(link, "_camera_up")) {
		v = camera_object_up_world(scene_camera);
		return v;
	}
	return v;
}

vec4_t uniforms_ext_vec4_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_pixel_snap_uv")) {
		if (!g_context->paint2d_view) {
			return (vec4_t){0.0, 0.0, 0.0, 0.0};
		}
		f32 tx, ty, tw, th;
		ui_view2d_tex_rect(config_get_texture_res_y() / (float)config_get_texture_res_x(), &tx, &ty, &tw, &th);
		f32 top = ui_view2d_top();
		return (vec4_t){(g_context->paint_vec.x * base_w() - tx) / tw, (g_context->paint_vec.y * base_h() + top - ty) / th,
		                (g_context->last_paint_vec_x * base_w() - tx) / tw, (g_context->last_paint_vec_y * base_h() + top - ty) / th};
	}
	if (string_equals(link, "_input_brush")) {
		bool   down = mouse_down("left") || pen_down("tip");
		vec4_t v    = (vec4_t){g_context->paint_vec.x, g_context->paint_vec.y, down ? 1.0 : 0.0, 0.0};
		return v;
	}
	else if (string_equals(link, "_input_brush_last")) {
		bool   down = mouse_down("left") || pen_down("tip");
		vec4_t v    = (vec4_t){g_context->last_paint_vec_x, g_context->last_paint_vec_y, down ? 1.0 : 0.0, 0.0};
		return v;
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
		bool decal_mask = context_is_decal_mask_paint_pass();
		f32  val        = (g_context->brush_radius * g_context->brush_nodes_radius) / 15.0;
		f32  scale2d    = (900 / (float)base_h()) * g_config->window_scale;
		val *= scale2d * 2.0;
		vec4_t v = (vec4_t){g_context->decal_x, g_context->decal_y, decal_mask ? 1 : 0, val};
		return v;
	}
	else if (string_equals(link, "_select_mask")) {
		return (vec4_t){g_context->select_x1, g_context->select_y1, g_context->select_x2, g_context->select_y2};
	}

	return render_pass_get_vec4(link);
}

mat4_t uniforms_ext_mat4_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_decal_layer_matrix")) { // Decal layer
		mat4_t m            = mat4_inv(g_context->layer->decal_mat);
		f32    parent_scale = object->parent != NULL ? object->parent->transform->scale.x : 1.0;
		f32    f            = parent_scale * object->transform->scale_world;
		m                   = mat4_scale(m, (vec4_t){f, f, f, 1.0});
		m                   = mat4_mult_mat(m, uniforms_ext_ortho_p);
		return m;
	}

	return mat4_nan();
}

void uniforms_ext_cache_uv_island_map(void *_) {
	util_uv_cache_uv_island_map();
}

void uniforms_ext_cache_dilate_map(void *_) {
	util_uv_cache_dilate_map();
}

void uniforms_ext_cache_triangle_map(void *_) {
	util_uv_cache_triangle_map();
}

void uniforms_ext_cache_uv_map(void *_) {
	util_uv_cache_uv_map();
}

static gpu_texture_t *_uniforms_ext_get_target(char *name) {
	render_target_t *rt = any_map_get(render_path_render_targets, name);
	if (rt == NULL) {
		rt = any_map_get(render_path_render_targets, "empty_black");
	}
	return rt->_image;
}

// A layer texture bound as a render target of the current pass reads as black
static gpu_texture_t *uniforms_layer_tex(gpu_texture_t *t) {
	for (i32 i = 0; gpu_in_use && i < current_render_targets_count; ++i) {
		if (t == current_render_targets[i]) {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
			return rt->_image;
		}
	}
	return t;
}

gpu_texture_t *uniforms_ext_tex_link(object_t *object, shader_data_t *mat, char *link) {
	if (string_equals(link, "_texpaint_undo")) {
		i32 i = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
		return _uniforms_ext_get_target(string_tmp("texpaint_undo%d", i));
	}
	else if (string_equals(link, "_texpaint_nor_undo")) {
		i32 i = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
		return _uniforms_ext_get_target(string_tmp("texpaint_nor_undo%d", i));
	}
	else if (string_equals(link, "_texpaint_pack_undo")) {
		i32 i = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
		return _uniforms_ext_get_target(string_tmp("texpaint_pack_undo%d", i));
	}
	else if (string_equals(link, "_texpaint_ref")) {
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
		return image != NULL ? image : ((render_target_t *)any_map_get(render_path_render_targets, "empty_black"))->_image;
	}
	else if (string_equals(link, "_texinpaint_overlay")) {
		gpu_texture_t *overlay = box_bake_maps_inpaint_overlay();
		return overlay != NULL ? overlay : ((render_target_t *)any_map_get(render_path_render_targets, "empty_black"))->_image;
	}
	else if (string_equals(link, "_texcolorid")) {
		if (g_project->_->assets->length == 0) {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_white");
			return rt->_image;
		}
		else {
			return project_get_image(g_project->_->assets->buffer[g_context->colorid]);
		}
	}
	else if (string_equals(link, "_textexttool")) { // Opacity map for text
		return g_context->text_tool_image;
	}
	else if (string_equals(link, "_texbrushmask")) {
		return g_context->brush_mask_image;
	}
	else if (string_equals(link, "_texselectmask")) {
		return g_context->select_mask_image;
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
	else if (string_equals(link, "_textrianglemap")) {
		if (!util_uv_trianglemap_cached) {
			sys_notify_on_next_frame(&uniforms_ext_cache_triangle_map, NULL);
		}
		return util_uv_trianglemap;
	}
	else if (string_equals(link, "_texdilatemap")) {
		if (util_uv_dilatemap_cached) {
			return util_uv_dilatemap;
		}
		sys_notify_on_next_frame(&uniforms_ext_cache_dilate_map, NULL);
		render_target_t *rt = any_map_get(render_path_render_targets, "empty_white");
		return rt->_image;
	}
	else if (string_equals(link, "_texuvislandmap")) {
		sys_notify_on_next_frame(&uniforms_ext_cache_uv_island_map, NULL);
		if (util_uv_uvislandmap_cached) {
			return util_uv_uvislandmap;
		}
		else {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
			return rt->_image;
		}
	}
	else if (string_equals(link, "_texdilatemap")) {
		return util_uv_dilatemap;
	}
	if (starts_with(link, "_texpaint_pack_vert")) {
		render_target_t *rt = any_map_get(render_path_render_targets, string_tmp("texpaint_pack%c", link[string_length(link) - 1]));
		return rt->_image;
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
		return tid < g_project->_->layers->length ? uniforms_layer_tex(g_project->_->layers->buffer[tid]->texpaint) : NULL;
	}
	if (starts_with(link, "_texblur_")) {
		char *id = link + 9;
		if (g_context->node_previews != NULL) {
			return any_map_get(g_context->node_previews, id);
		}
		else {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
			return rt->_image;
		}
	}
	if (starts_with(link, "_texwarp_")) {
		char *id = link + 9;
		if (g_context->node_previews != NULL) {
			return any_map_get(g_context->node_previews, id);
		}
		else {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
			return rt->_image;
		}
	}
	if (starts_with(link, "_texbake_")) {
		char *id = link + 9;
		if (g_context->node_previews != NULL) {
			return any_map_get(g_context->node_previews, id);
		}
		else {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
			return rt->_image;
		}
	}
	if (string_equals(link, "_camera_texture")) {
		render_target_t *rt = any_map_get(render_path_render_targets, "last");
		return rt->_image;
	}
	if (string_equals(link, "_lut_tex")) {
		if (lut_image == NULL) {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_white");
			return rt->_image;
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
