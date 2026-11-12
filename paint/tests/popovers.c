#include "global.h"

// Touch popovers: the bar at the top right opens them, a touch outside closes one and still paints

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int px     = 0; // Layers icon, found from the screen size
int py     = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[popover] %s %s", ok ? "ok  " : "FAIL", name);
}

buffer_t *before = NULL;

buffer_t *copy_layer() {
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	buffer_t     *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

// Texels changed since the copy
int painted() {
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	int           n = 0;
	for (int i = 0; i < p->length; i += 4) {
		int d = buffer_get_u8(p, i) - buffer_get_u8(before, i);
		if (d > 8 || d < -8) {
			n = n + 1;
		}
	}
	return n;
}

void next() {
	frame = frame + 1;
	if (frame < 6) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		check(ui_popovers_current() == 0, "starts closed");
		ui_popovers_show(1);
	}
	else if (step == 1) {
		check(ui_popovers_current() == 1, "layers popover opens");
		ui_popovers_show(3);
	}
	else if (step == 2) {
		check(ui_popovers_current() == 3, "materials popover replaces it");
		before = copy_layer();
		// A touch in the middle of the view
		script_mouse_press(sys_x() + sys_w() / 2, sys_h() / 2, 0);
	}
	else if (step == 3) {
		script_mouse_move(sys_x() + sys_w() / 2 + 40, sys_h() / 2 + 10);
	}
	else if (step == 4) {
		script_mouse_release(sys_x() + sys_w() / 2 + 40, sys_h() / 2 + 10, 0);
	}
	else if (step == 5) {
		check(ui_popovers_current() == 0, "touch outside closes it");
		check(painted() > 0, string("that touch still painted (%d)", painted()));
	}
	else {
		printf("[popover] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "POPOVER PASS" : "POPOVER FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults(); // A plain round brush, whatever the user left picked
	script_project_new();
	context_select_tool(TOOL_TYPE_BRUSH);
	script_get_context()->brush_opacity = 1.0;
	script_notify_on_next_frame(next);
}
