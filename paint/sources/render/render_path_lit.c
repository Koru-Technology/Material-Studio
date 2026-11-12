
#include "../global.h"

// Grid, envmap spheres, path points and gizmo over the image, each pass loads and stores the whole target,
// so they only start when something draws
void render_path_draw_overlays(char *target) {
	bool hidden = base_player || g_context->capturing_screenshot;
	if (g_context->show_grid && !hidden) {
		render_path_set_target(target, NULL, "main", GPU_CLEAR_NONE, 0, 0.0);
		render_grid();
	}
	i32 path_count = 0;
	i32 path_active;
	if (!hidden && !g_context->paint2d) {
		util_path_tool_get_points(&path_count, &path_active);
	}
	bool envsphere = g_context->show_envmap_spheres && !hidden;
	if (envsphere || path_count > 0 || g_context->gizmo->visible) {
		render_path_set_target(target, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_envsphere();
		render_pathsphere_tool();
		render_gizmo_draw();
	}
}

void render_path_lit_draw() {
	render_path_base_draw_ssao();
	render_path_base_draw_deferred_light();
	render_path_base_draw_bloom("buf", "buf");
	// draw_histogram();

	render_path_set_target("gbuffer1", NULL, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0);
	render_path_bind_target("buf", "tex");
	// render_path_bind_target("histogram", "histogram");
	render_path_draw_shader("Scene/compositor_pass/compositor_pass");
	render_pass_draw(RENDER_PASS_POST, "gbuffer1");

	if (!args_player) {
		render_path_draw_overlays("gbuffer1");
	}
	render_path_base_draw_taa("gbuffer1", "buf");
}

void render_path_lit_commands() {
	render_path_base_commands(render_path_lit_draw);
}
