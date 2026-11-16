
#include "../global.h"

static mesh_object_t *render_pathsphere_obj = NULL;

void render_pathsphere() {
	if (g_context->capturing_screenshot || g_context->paint2d) {
		return;
	}

	slot_layer_t *l = g_context->layer;
	if (l == NULL || !slot_layer_is_path(l)) {
		return;
	}

	f32_array_t *points_world = l->path_points_world;
	if (points_world == NULL || points_world->length == 0) {
		return;
	}

	if (path_point_dragging >= 0) {
		return;
	}

	if (render_pathsphere_obj == NULL) {
		render_pathsphere_obj = render_sphere_object("render_pathsphere", "vec3(0.4, 0.0, 0.0)", 0.5, 0.0);
	}

	i32 num_points   = points_world->length / 3;
	i32 active_index = path_layer_last_active;

	for (i32 i = 0; i < num_points; i++) {
		f32 wx    = points_world->buffer[i * 3];
		f32 wy    = points_world->buffer[i * 3 + 1];
		f32 wz    = points_world->buffer[i * 3 + 2];
		f32 dist  = vec4_dist(scene_camera->base->transform->loc, (vec4_t){wx, wy, wz, 1.0});
		f32 fov   = scene_camera->data->fov;
		f32 scale = dist / 8.0f * fov * 0.1f * (i == active_index ? 1.5f : 1.0f);

		render_pathsphere_obj->base->transform->loc   = (vec4_t){wx, wy, wz, 1.0};
		render_pathsphere_obj->base->transform->scale = (vec4_t){scale, scale, scale, 1.0};
		transform_build_matrix(render_pathsphere_obj->base->transform);
		mesh_object_draw(render_pathsphere_obj, render_path_base_own_scon(render_pathsphere_obj), NULL);
	}
}
