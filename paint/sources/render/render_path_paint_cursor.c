#include "../global.h"

// Brush cursor drawn over the 3D view: the circle on the surface, the decal preview

void render_path_paint_draw_cursor(f32 mx, f32 my, f32 radius, f32 tint_r, f32 tint_g, f32 tint_b) {
	mesh_object_t *plane = scene_get_child(".Plane")->ext;
	mesh_data_t   *geom  = plane->data;

	gpu_vertex_textures = true;
	render_path_set_target("", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipes_cursor);
	gpu_texture_t *main = render_path_target_image(render_path_paint_depth());
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
	gpu_texture_t *main = render_path_target_image(render_path_paint_depth());
	gpu_set_texture(pipes_cursor_decal_gbufferd, main);
	gpu_set_texture(pipes_cursor_decal_texdecal, image);
	gpu_set_texture(pipes_cursor_decal_gbuffer0, render_path_target_image("gbuffer0"));
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
		if (base_ui_enabled && !base_paint_in_2d()) {
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
	if (!base_ui_enabled || fill_layer || group_layer) {
		return;
	}

	// Paint coordinates point into the 2D view, it draws its own cursor
	if (base_paint_in_2d()) {
		return;
	}

	// Hidden while painting, it covers the paint (also the first frame of a touch, before the stroke counts its time)
	if ((g_context->brush_time > 0 || pen_down("tip") || mouse_down("left"))) {
		return;
	}

	// The pencil hovered away from the screen and nothing touches it
	if (pen_hover_left && !pen_down("tip") && !mouse_down_any()) {
		return;
	}

	f32 mx = g_context->paint_vec.x;
	f32 my = 1.0 - g_context->paint_vec.y;

	// Fainter the higher the pencil hovers
	f32 tint = pen_hover >= 0.0 && !pen_down("tip") ? 1.0 - 0.6 * pen_hover : 1.0;

	// Prevent cursor jumping constantly if randomness is used in brush nodes
	f32 brush_nodes_radius = g_context->brush_nodes_radius;
	if (g_context->brush_nodes_uses_random) {
		brush_nodes_radius = 1.0;
	}

	if (context_is_decal()) {
		f32            scale2d = base_scale2d(base_h());
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
