#include "../global.h"

// Painting in the 2D view: the layer plane in front of the camera in place of the paint objects

void render_path_paint_set_plane_mesh() {
	g_context->paint2d_view    = true;
	render_path_paint_painto   = g_context->paint_object;
	if (render_path_paint_visibles == NULL) { // Set every frame of a 2D view stroke, kept between calls
		render_path_paint_visibles = u8_array_create(0);
	}
	render_path_paint_visibles->length = 0;
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
	for (i32 i = 0; i < g_project->_->paint_objects->length && i < (i32)render_path_paint_visibles->length; ++i) {
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
