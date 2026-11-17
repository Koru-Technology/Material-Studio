#include "global.h"

// Paint paths the other suites leave out: a live stroke in the 2D view alone, a blur stroke across the edge of a stroke,
// the color picker reading the painted color under the cursor.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int cx     = 0; // Center of the 3D view, the 2D view alone takes its place (sys_w() is 1 then)
int cy     = 0;

buffer_t *before = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[render_paint] %s %s", ok ? "ok  " : "FAIL", name);
}

buffer_t *copy_layer() {
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	buffer_t     *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

// Texels changed since the copy in any channel, every 16th texel sampled
int changed_texels() {
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	int           n = 0;
	for (int i = 0; i < p->length; i += 64) {
		for (int c = 0; c < 4; ++c) {
			int d = buffer_get_u8(p, i + c) - buffer_get_u8(before, i + c);
			if (d > 4 || d < -4) {
				n = n + 1;
				break;
			}
		}
	}
	return n;
}

void next() {
	frame          = frame + 1;
	context_t *ctx = script_get_context();

	if (step == 0) { // Settle, then the 2D view alone
		if (frame > 10) {
			cx = sys_x() + sys_w() / 2;
			cy = sys_h() / 2;
			ui_base_cycle_view_mode();
			step  = 1;
			frame = 0;
		}
	}
	else if (step == 1) { // Live stroke in the 2D view
		if (frame == 10) {
			check(ui_base_view_mode() == 1, string("the 2D view alone (%d)", ui_base_view_mode()));
			before = copy_layer();
			script_mouse_press(cx - 150, cy, 0);
		}
		else if (frame > 10 && frame <= 40) {
			script_mouse_move(cx - 150 + (frame - 10) * 10, cy);
		}
		else if (frame == 41) {
			script_mouse_release(cx + 150, cy, 0);
		}
		else if (frame == 55) {
			int n = changed_texels();
			check(n > 100, string("a live stroke in the 2D view paints (%d texels)", n));
			for (int i = 0; i < 3 && ui_base_view_mode() != 0; ++i) {
				ui_base_cycle_view_mode();
			}
			history_undo();
			step  = 2;
			frame = 0;
		}
	}
	else if (step == 2) { // A stroke, then a blur stroke across its edge
		if (frame == 10) {
			check(ui_base_view_mode() == 0, "back to the 3D view");
			for (int i = 0; i < 20; ++i) {
				script_paint(0.4 + i * 0.01, 0.5);
			}
			script_paint_end();
		}
		else if (frame == 15) {
			context_select_tool(TOOL_TYPE_BLUR);
			ctx->brush_radius = 0.5;
			before            = copy_layer();
			for (int i = 0; i < 20; ++i) {
				script_paint(0.5, 0.4 + i * 0.01);
			}
			script_paint_end();
		}
		else if (frame == 20) {
			int n = changed_texels();
			check(n > 20, string("a blur stroke softens the edge of a stroke (%d texels)", n));
			context_select_tool(TOOL_TYPE_PICKER);
			step  = 3;
			frame = 0;
		}
	}
	else if (step == 3) { // The picker over the painted stroke
		if (frame == 5) {
			app_color_set_base(0xffffff);
			script_mouse_press(cx, cy, 0);
		}
		else if (frame == 10) {
			script_mouse_release(cx, cy, 0);
		}
		else if (frame == 20) {
			int c = app_color_base();
			int r = (c >> 16) & 0xff;
			int g = (c >> 8) & 0xff;
			int b = c & 0xff;
			check(b > r + 60 && b > g + 30, string("the picker reads the painted blue (%d %d %d)", r, g, b));
			check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
			check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
			printf("[render_paint] %d checks, %d failures", checks, fails);
			printf(fails == 0 ? "RENDER_PAINT PASS" : "RENDER_PAINT FAIL");
			script_quit();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults(); // A plain round brush, whatever the user left picked
	for (int i = 0; i < 3 && ui_base_view_mode() != 0; ++i) {
		ui_base_cycle_view_mode();
	}
	script_project_new();
	slot_material_t *m   = script_material_create("render_paint_blue");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.5;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
