#include "global.h"

// The color picker tool reads what shows (render/make_picking.c): a red fill layer under an empty paint layer, the
// empty one selected, picks red in the lit view. The material painted is a node graph: the pick gives way to a new
// color material of it, a second pick goes into that one. Off the mesh the color stays. In a channel view it reads the
// selected layer, empty there: the color stays.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int cx     = 0;
int cy     = 0;
int n0     = 0;

slot_material_t *red;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[picker] %s %s", ok ? "ok  " : "FAIL", name);
}

int ch(int c, int shift) {
	return (c >> shift) & 0xff;
}

// A pick at x y over frames: press at 0, release at 5
void pick(int x, int y) {
	if (frame == 1) {
		script_mouse_press(x, y, 0);
	}
	else if (frame == 6) {
		script_mouse_release(x, y, 0);
	}
}

void next() {
	frame          = frame + 1;
	context_t *ctx = script_get_context();

	if (step == 0) {
		if (frame > 10) {
			cx = sys_x() + sys_w() / 2;
			cy = sys_h() / 2;
			context_select_tool(TOOL_TYPE_PICKER);
			app_color_set_base(0xffffff);
			n0    = app_material_count();
			step  = 1;
			frame = 0;
		}
	}
	else if (step == 1) { // Over the mesh, lit: red, a new color material
		pick(cx, cy);
		if (frame == 20) {
			int c = app_color_base();
			check(ch(c, 16) > ch(c, 8) + 100 && ch(c, 16) > ch(c, 0) + 100, string("picks the red fill under the empty layer (%d %d %d)", ch(c, 16), ch(c, 8), ch(c, 0)));
			check(app_material_count() == n0 + 1 && ctx->material != red, string("a new color material in place of the node graph (%d materials, were %d)", app_material_count(), n0));
			n0 = app_material_count();
			app_color_set_base(0x00ff00);
			step  = 2;
			frame = 0;
		}
	}
	else if (step == 2) { // Off the mesh: the color stays
		pick(sys_x() + 8, 8);
		if (frame == 20) {
			check((app_color_base() & 0xffffff) == 0x00ff00, string("off the mesh the color stays (%06x)", app_color_base() & 0xffffff));
			step  = 3;
			frame = 0;
		}
	}
	else if (step == 3) { // Again over the mesh: into the color material, no other made
		pick(cx, cy);
		if (frame == 20) {
			int c = app_color_base();
			check(ch(c, 16) > ch(c, 8) + 100, string("picks red again (%06x)", c));
			check(app_material_count() == n0, string("the color material takes it, no other made (%d)", app_material_count()));
			context_set_viewport_mode(1); // Base color: the selected layer, empty
			app_color_set_base(0x0000ff);
			step  = 4;
			frame = 0;
		}
	}
	else if (step == 4) {
		pick(cx, cy);
		if (frame == 20) {
			check((app_color_base() & 0xffffff) == 0x0000ff, string("a channel view reads the empty selected layer: the color stays (%06x)", app_color_base() & 0xffffff));
			context_set_viewport_mode(0);
			check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
			check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
			printf("[picker] %d checks, %d failures", checks, fails);
			printf(fails == 0 ? "PICKER PASS" : "PICKER FAIL");
			script_quit();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	for (int i = 0; i < 3 && ui_base_view_mode() != 0; ++i) {
		ui_base_cycle_view_mode();
	}
	script_project_new();
	context_set_viewport_mode(0); // Lit
	red            = script_material_create("picker_red");
	ui_node_t *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.9, 0.05, 0.05, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	script_layer_new_fill();
	app_layer_new(0); // Empty, selected
	script_notify_on_next_frame(next);
}
