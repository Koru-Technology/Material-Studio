#include "global.h"

// The native UI's interface (app_api.h): layer actions, one undo step per slider drag, thumbnails, brush setters.

int step   = 0;
int frame  = 0;
int checks = 0;
int fails  = 0;
int count0 = 0;
int added  = -1;

void check(bool ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[app_api] %s %s", ok ? "ok  " : "FAIL", name);
}

slot_layer_t *layer_by_id(int id) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->id == id) {
			return l;
		}
	}
	return NULL;
}

void next() {
	frame = frame + 1;
	if (frame < 3) { // Let each step's work reach the screen before the next
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		count0 = app_layer_count();
		check(app_layer_add(), "new layer");
		check(app_layer_count() == count0 + 1, "layer count grew by one");
		added = app_layer_id_at(0);
		check(script_get_context()->layer->id == added, "the new layer is selected and listed first");
	}
	else if (step == 1) {
		slot_layer_t *l = layer_by_id(added);
		app_layer_opacity_begin(added);
		app_layer_set_opacity(added, 0.5);
		app_layer_set_opacity(added, 0.25);
		app_edit_end();
		check(l->mask_opacity > 0.24 && l->mask_opacity < 0.26, "opacity set by the drag");
		app_undo();
		check(l->mask_opacity > 0.99, "one undo reverts the whole drag");
		app_redo();
		check(l->mask_opacity < 0.26, "redo applies it again");
	}
	else if (step == 2) {
		slot_layer_t *l = layer_by_id(added);
		check(app_layer_set_visible(added, false) && !l->visible, "hidden");
		app_undo();
		check(l->visible, "undo shows it again");
	}
	else if (step == 3) {
		check(app_layer_thumbnail_size(added) > 0, "thumbnail reads back");
		app_set_brush_radius(0.3);
		check(script_get_context()->brush_radius > 0.29 && script_get_context()->brush_radius < 0.31, "brush radius");
		app_set_brush_radius(9.0);
		check(script_get_context()->brush_radius <= 2.0, "brush radius clamped");
		app_set_tool(1);
		check(script_get_context()->tool == 1, "eraser tool");
		app_set_tool(0);
	}
	else if (step == 4) {
		check(app_layer_can_delete(added), "can delete");
		check(app_layer_delete(added), "deleted");
		check(app_layer_count() == count0 && layer_by_id(added) == NULL, "layer gone");
		check(!app_layer_select(added), "a deleted id is not found");
	}
	else {
		printf("[app_api] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "APP_API PASS" : "APP_API FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
