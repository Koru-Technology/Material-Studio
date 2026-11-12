
#include "../global.h"

// Grid, envmap spheres and gizmo over the image, each pass loads and stores the whole target,
// so they only start when something draws
void render_path_draw_overlays(char *target) {
	bool hidden = base_player || g_context->capturing_screenshot;
	if (g_context->show_grid && !hidden) {
		render_path_set_target(target, NULL, "main", GPU_CLEAR_NONE, 0, 0.0);
		render_grid();
	}
	bool envsphere = g_context->show_envmap_spheres && !hidden;
	if (envsphere || g_context->gizmo->visible) {
		render_path_set_target(target, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_envsphere();
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

	render_path_draw_overlays("gbuffer1");
	render_path_base_draw_taa("gbuffer1", "buf");
}

void render_path_lit_commands() {
	render_path_base_commands(render_path_lit_draw);
}
