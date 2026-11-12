#include "global.h"

// Pencil features that can run without a pencil: tilt shading and the tilt / azimuth dynamics compile and paint,
// the quick menu picks an item with a tap and a tap outside closes it without painting, double tap and squeeze actions.

int frame  = 0;
int step   = 0;
int fails  = 0;
int checks = 0;

buffer_t *before         = NULL;
float     frame_time_sum = 0.0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[pencil] %s %s", ok ? "ok  " : "FAIL", name);
}

buffer_t *copy_pixels() {
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	buffer_t     *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

int changed_texels() {
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	int           n = 0;
	for (int i = 0; i < p->length; i += 64) {
		int d = buffer_get_u8(p, i) - buffer_get_u8(before, i);
		int e = buffer_get_u8(p, i + 2) - buffer_get_u8(before, i + 2);
		if (d > 8 || d < -8 || e > 8 || e < -8) {
			n = n + 1;
		}
	}
	return n;
}

int cx() {
	return sys_x() + sys_w() / 2;
}

int cy() {
	return sys_h() / 2;
}

// A live stroke over frames: press, 20 moves, release
int stroke_frame = 0;
int stroke_y     = 0;

// Returns 1 once the stroke is done
int live_stroke() {
	int x = cx() - 100 + stroke_frame * 10;
	if (stroke_frame == 0) {
		script_mouse_press(x, stroke_y, 0);
	}
	else if (stroke_frame < 20) {
		script_mouse_move(x, stroke_y);
	}
	else if (stroke_frame == 20) {
		script_mouse_release(x, stroke_y, 0);
	}
	stroke_frame = stroke_frame + 1;
	if (stroke_frame > 26) {
		stroke_frame = 0;
		return 1;
	}
	return 0;
}

// Center of a quick menu slot, menu opened at the viewport center
int slot_x(int slot) {
	float a = slot * 3.14159265 / 4.0;
	return cx() + (int)(sin(a) * 85.0 * UI_SCALE());
}

int slot_y(int slot) {
	float a = slot * 3.14159265 / 4.0;
	return cy() - (int)(cos(a) * 85.0 * UI_SCALE());
}

void next() {
	frame = frame + 1;
	context_t *ctx = script_get_context();
	config_t  *cfg = script_get_config();

	if (step == 0) { // Frame rate, then tilt shading on: the paint shader with the stretch compiles, batched and single dab
		if (frame > 30 && frame <= 150) {
			frame_time_sum = frame_time_sum + script_frame_time();
		}
		if (frame < 151) {
			script_notify_on_next_frame(next);
			return;
		}
		printf("[pencil] frame rate %.1f fps", 120.0 / frame_time_sum);
		cfg->tilt_shading = 1;
		cfg->dyn_size     = 4; // Tilt
		cfg->dyn_angle    = 5; // Azimuth
		make_paint_set_batching(1);
		check(make_paint_is_batched(), "tilt shading paint shader is batched");
		before   = copy_pixels();
		stroke_y = cy() - 60;
		step     = 1;
		frame    = 0;
	}
	else if (step == 1) {
		if (live_stroke()) {
			int n = changed_texels();
			check(n > 200, string("live stroke with tilt shading and tilt / azimuth dynamics paints (%d)", n));
			make_paint_set_batching(0);
			before   = copy_pixels();
			stroke_y = cy() - 20;
			step     = 2;
		}
	}
	else if (step == 2) {
		if (live_stroke()) {
			int n = changed_texels();
			check(n > 200, string("single dab stroke with tilt shading paints (%d)", n));
			make_paint_set_batching(1);
			cfg->tilt_shading = 0;
			cfg->dyn_size     = 1;
			cfg->dyn_angle    = 0;
			make_paint_set_batching(1);
			// Quick menu, tap on the eraser (top right)
			context_select_tool(TOOL_TYPE_BRUSH);
			ui_quick_menu_open(cx(), cy());
			check(ui_quick_menu_blocking(), "quick menu opens");
			step  = 3;
			frame = 0;
		}
	}
	else if (step == 3) {
		if (frame == 2) {
			script_mouse_move(slot_x(1), slot_y(1));
		}
		else if (frame == 3) {
			script_mouse_press(slot_x(1), slot_y(1), 0);
		}
		else if (frame == 5) {
			script_mouse_release(slot_x(1), slot_y(1), 0);
		}
		else if (frame == 8) {
			check(ctx->tool == TOOL_TYPE_ERASER, string("quick menu tap picks the eraser (tool %d)", ctx->tool));
			check(!ui_quick_menu_blocking(), "quick menu closes after the pick");
			// A press outside closes it and does not paint
			context_select_tool(TOOL_TYPE_BRUSH);
			ui_quick_menu_open(cx(), cy());
			before   = copy_pixels();
			stroke_y = cy() + 450;
			step     = 4;
			frame    = 0;
		}
	}
	else if (step == 4) {
		if (live_stroke()) {
			int n = changed_texels();
			check(n == 0, string("a stroke started outside the quick menu closes it without painting (%d)", n));
			check(!ui_quick_menu_blocking(), "quick menu closed");
			// Double tap toggles the eraser and back
			cfg->pencil_double_tap = 1; // Toggle eraser
			util_pencil_on_gesture(0, -1.0, -1.0);
			step  = 5;
			frame = 0;
		}
	}
	else if (step == 5) {
		if (frame == 2) {
			check(ctx->tool == TOOL_TYPE_ERASER, "double tap toggles to the eraser");
			util_pencil_on_gesture(0, -1.0, -1.0);
		}
		else if (frame == 4) {
			check(ctx->tool == TOOL_TYPE_BRUSH, "double tap toggles back to the brush");
			// Squeeze opens the quick menu where the pencil hovers
			cfg->pencil_squeeze = 4; // Quick menu
			util_pencil_on_gesture(1, cx(), cy());
		}
		else if (frame == 6) {
			check(ui_quick_menu_blocking(), "squeeze opens the quick menu");
			util_pencil_on_gesture(1, cx(), cy());
		}
		else if (frame == 8) {
			check(!ui_quick_menu_blocking(), "a second squeeze closes it");
			// Double tap undo takes the last stroke back
			cfg->pencil_double_tap = 3; // Undo
			before   = copy_pixels();
			stroke_y = cy() + 40;
			step     = 6;
			frame    = 0;
		}
	}
	else if (step == 6) {
		if (live_stroke()) {
			int n = changed_texels();
			check(n > 200, string("stroke before the undo paints (%d)", n));
			util_pencil_on_gesture(0, -1.0, -1.0);
			step  = 7;
			frame = 0;
		}
	}
	else if (step == 7) {
		if (frame == 4) {
			int n = changed_texels();
			check(n < 20, string("double tap undo takes the stroke back (%d left)", n));
			cfg->pencil_double_tap = 0;
			cfg->pencil_squeeze    = 0;
			printf("[pencil] %d checks, %d failures", checks, fails);
			printf(fails == 0 ? "PENCIL PASS" : "PENCIL FAIL");
			script_quit();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	slot_material_t *m   = script_material_create("pencil_blue");
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
