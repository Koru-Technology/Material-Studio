#include "global.h"

// Hold a stroke to make it a curve (util/util_curve_stroke.c). A straight stroke held becomes a curve of 2 points, an S
// of 3 to 8; holding takes the stroke back (the layer as before it) and the curve shows in the live layer only. A point
// dragged lands on the surface; Confirm paints the curve as one stroke, along where the points are now (as a stroke
// painted straight between them), recorded for replay; undo and redo it. Undo while editing drops the curve. A new
// touch on the canvas confirms it. With real mouse input: a stroke held still for a second becomes a curve, one that
// keeps moving does not. The view turned before a point is dragged: it still confirms as one stroke.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

buffer_t *before = NULL; // The layer before any stroke
buffer_t *curve  = NULL; // The layer with the confirmed curve
int       undos0 = 0;
int       stored = 0;
float     ax     = 0.0; // The curve's points after the drag, paint view 0-1
float     ay     = 0.0;
float     bx     = 0.0;
float     by     = 0.0;
double    held   = 0.0;
int       cx     = 0;
int       cy     = 0;
int       tick   = 0; // Frames of a step that waits or drags

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[curve_stroke] %s %s", ok ? "ok  " : "FAIL", name);
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

int differs(buffer_t *p, buffer_t *b, int i) {
	int d = buffer_get_u8(p, i) - buffer_get_u8(b, i);
	int e = buffer_get_u8(p, i + 3) - buffer_get_u8(b, i + 3);
	return d > 8 || d < -8 || e > 8 || e < -8;
}

// Texels changed from b, every 16th sampled
int changed_from(buffer_t *b) {
	buffer_t *p = script_get_texture_pixels(script_get_context()->layer->texpaint);
	int       n = 0;
	for (int i = 0; i < p->length; i += 64) {
		n = n + differs(p, b, i);
	}
	return n;
}

int changed() {
	return changed_from(before);
}

// Of the texels the curve painted, the share the layer has painted now
float overlap() {
	buffer_t *p    = script_get_texture_pixels(script_get_context()->layer->texpaint);
	int       both = 0;
	int       c    = 0;
	for (int i = 0; i < p->length; i += 64) {
		if (differs(curve, before, i)) {
			c = c + 1;
			if (differs(p, before, i)) {
				both = both + 1;
			}
		}
	}
	return c > 0 ? both * 1.0 / c : 0.0;
}

// Paints a straight stroke from (x0, y0) to (x1, y1), 0-1 of the paint view, and its points into a (x, y, -1 each)
f32_array_t *paint_line(float x0, float y0, float x1, float y1) {
	f32_array_t *a = f32_array_create(0);
	for (int i = 0; i <= 40; ++i) {
		float t = i / 40.0;
		float x = x0 + (x1 - x0) * t;
		float y = y0 + (y1 - y0) * t;
		script_paint(x, y);
		f32_array_push(a, x);
		f32_array_push(a, y);
		f32_array_push(a, -1.0);
	}
	script_paint_end();
	return a;
}

f32_array_t *paint_s() {
	f32_array_t *a = f32_array_create(0);
	for (int i = 0; i <= 60; ++i) {
		float t = i / 60.0;
		float x = 0.42 + 0.16 * t;
		float y = 0.5 + 0.06 * sin(t * 6.2832);
		script_paint(x, y);
		f32_array_push(a, x);
		f32_array_push(a, y);
		f32_array_push(a, -1.0);
	}
	script_paint_end();
	return a;
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;
	cx    = sys_x() + sys_w() / 2;
	cy    = sys_h() / 2;

	if (step == 0) {
		before = copy_pixels();
		undos0 = history_test_undos();
		f32_array_t *a = paint_line(0.42, 0.5, 0.58, 0.5);
		check(changed() > 0, "the stroke painted");
		check(util_curve_stroke_test_hold(a), "held: it becomes a curve");
		check(util_curve_stroke_active() && util_curve_stroke_count() == 2, string("a straight stroke: 2 points (%d)", util_curve_stroke_count()));
		check(changed() == 0, "the stroke is taken back");
		check(history_test_undos() == undos0, "its step is undone");
	}
	else if (step == 1) {
		check(changed() == 0, "the curve shows in the live layer, not in the layer");
		float x = util_curve_stroke_test_point(1, 0);
		float y = util_curve_stroke_test_point(1, 1);
		check(x > 0.0 && y > 0.0, "its end shows");
		app_curve_move(1, x, y + sys_h() * 0.06);
	}
	else if (step == 2) {
		float y = util_curve_stroke_test_point(1, 1);
		check(y > cy + sys_h() * 0.04, string("a dragged point lands on the surface where it was dropped (%.0f)", y));
		ax     = base_paint_x(util_curve_stroke_test_point(0, 0));
		ay     = base_paint_y(util_curve_stroke_test_point(0, 1));
		bx     = base_paint_x(util_curve_stroke_test_point(1, 0));
		by     = base_paint_y(util_curve_stroke_test_point(1, 1));
		stored = paint_rec_test_stored();
		util_curve_stroke_confirm();
		check(!util_curve_stroke_active(), "confirmed");
		check(history_test_undos() == undos0 + 1, "one step");
		check(changed() > 0, "it painted");
		curve = copy_pixels();
	}
	else if (step == 3) {
		history_user_undo();
	}
	else if (step == 4) {
		check(changed() == 0, "undo takes the curve back");
		check(paint_rec_test_stored() > stored, "it was recorded for replay");
		history_redo();
	}
	else if (step == 5) {
		check(changed_from(curve) == 0, "redo paints it again");
		history_undo();
	}
	else if (step == 6) { // The same line painted straight between the points
		paint_line(ax, ay, bx, by);
		float o = overlap();
		check(o > 0.7, string("it painted along the points as they are now (%.2f of it as a straight stroke between them)", o));
		history_undo();
	}
	else if (step == 7) {
		f32_array_t *a = paint_s();
		util_curve_stroke_test_hold(a);
		int n = util_curve_stroke_count();
		check(n >= 3 && n <= 8, string("an S: 3 to 8 points (%d)", n));
		history_user_undo();
		check(!util_curve_stroke_active(), "undo while editing drops the curve");
		check(changed() == 0 && history_test_undos() == undos0, "nothing painted, no step");
	}
	else if (step == 8) { // A new touch confirms
		util_curve_stroke_test_hold(paint_line(0.42, 0.5, 0.58, 0.5));
		script_mouse_press(cx, cy + 20, 0);
	}
	else if (step == 9) {
		script_mouse_move(cx + 20, cy + 20);
	}
	else if (step == 10) {
		script_mouse_release(cx + 20, cy + 20, 0);
	}
	else if (step == 11) {
		tick = tick + 1;
		if (tick < 30) { // A finger waits to tell a tap from a two finger gesture
			script_notify_on_next_frame(next);
			frame = 4;
			return;
		}
		tick = 0;
		check(!util_curve_stroke_active(), "a touch on the canvas confirms the curve");
		check(history_test_undos() >= undos0 + 1, string("its step is there (%d)", history_test_undos() - undos0));
		for (int k = history_test_undos() - undos0; k > 0; --k) {
			history_undo();
		}
	}
	else if (step == 12) { // Real input: press, drag, hold still
		script_mouse_press(cx - 120, cy, 0);
	}
	else if (step == 13) {
		tick = tick + 1;
		if (tick < 24) {
			script_mouse_move(cx - 120 + tick * 10, cy);
			script_notify_on_next_frame(next);
			frame = 4;
			return;
		}
		tick = 0;
		held = sys_time();
	}
	else if (step == 14) {
		if (sys_time() - held < 3.0 && !util_curve_stroke_active()) { // Slow frames: the second may show a frame late
			script_mouse_move(cx - 120 + 23 * 10, cy); // Still
			script_notify_on_next_frame(next);
			frame = 4;
			return;
		}
		check(util_curve_stroke_active(), string("held still for a second: a curve (%d points)", util_curve_stroke_count()));
		script_mouse_release(cx - 120 + 23 * 10, cy, 0);
	}
	else if (step == 15) {
		check(changed() == 0, "the held stroke is taken back");
		check(util_curve_stroke_active(), "lifting keeps the curve");
		util_curve_stroke_confirm();
		check(changed() > 0, "confirmed, it paints");
		history_undo();
		script_mouse_press(cx - 120, cy + 40, 0);
	}
	else if (step == 16) {
		tick = tick + 1;
		if (tick < 24) {
			script_mouse_move(cx - 120 + tick * 10, cy + 40);
			script_notify_on_next_frame(next);
			frame = 4;
			return;
		}
		tick = 0;
		script_mouse_release(cx - 120 + 23 * 10, cy + 40, 0);
	}
	else if (step == 17) {
		check(!util_curve_stroke_active(), "a stroke that keeps moving stays a stroke");
		check(changed() > 0, "and paints");
		history_undo();
	}
	else if (step == 18) { // The view turns, then a point is dragged
		util_curve_stroke_test_hold(paint_line(0.42, 0.5, 0.58, 0.5));
		script_camera_save();
		script_camera_turn(0.3, 0.1);
	}
	else if (step == 19) {
		float x = util_curve_stroke_test_point(1, 0);
		float y = util_curve_stroke_test_point(1, 1);
		check(x > 0.0, "the point shows from the turned view");
		app_curve_move(1, x, y + sys_h() * 0.04);
	}
	else if (step == 20) {
		util_curve_stroke_confirm();
		check(history_test_undos() == undos0 + 1, "turned: confirmed as one step");
		check(changed() > 0, string("turned: it painted (%d)", changed()));
		script_camera_restore();
		history_undo();
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[curve_stroke] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "CURVE_STROKE PASS" : "CURVE_STROKE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	app_set_brush_radius(0.3);
	script_project_new();
	script_notify_on_next_frame(next);
}
