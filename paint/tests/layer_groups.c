#include "global.h"

// Layers dropped on rows of the layers list (app_layer_drop): above or below a row, beside it in its group or none, or
// into a group; groups do not nest. A layer held over another makes a group of both (app_layer_group_with), one undo
// step; dragged out of it, the last layer takes the group away.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

int a = -1;
int b = -1;
int c = -1;
int g = -1;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[layer_groups] %s %s", ok ? "ok  " : "FAIL", name);
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

int parent_of(int id) {
	slot_layer_t *l = layer_by_id(id);
	return l != NULL && l->parent != NULL ? l->parent->id : -1;
}

// Listed position, 0 the top
int row_of(int id) {
	for (int i = 0; i < app_layer_count(); ++i) {
		if (app_layer_id_at(i) == id) {
			return i;
		}
	}
	return -1;
}

int selected() {
	return script_get_context()->layer->id;
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		a = selected();
		check(app_layer_new(0), "a second layer");
	}
	else if (step == 1) {
		b = selected();
		check(app_layer_new(0), "a third layer");
	}
	else if (step == 2) {
		c = selected(); // c, b, a from the top
		int count = app_layer_count();
		g = app_layer_group_with(c, a);
		check(g >= 0 && parent_of(a) == g && parent_of(c) == g, "c held over a: a group of both");
		check(app_layer_count() == count + 1 && row_of(c) == row_of(g) + 1 && row_of(a) == row_of(c) + 1, "the group where a was, c above a in it");
		check(selected() == c, "the dragged layer stays selected");
		app_undo();
		check(layer_by_id(g) == NULL && parent_of(a) == -1 && parent_of(c) == -1 && app_layer_count() == count, "one undo takes it all back");
		check(row_of(c) == 0 && row_of(a) == 2, "where they were");
		app_redo();
		g = selected() == c ? parent_of(c) : -1; // Redo makes the group again
		check(g >= 0 && parent_of(a) == g, "redo");
	}
	else if (step == 3) {
		check(app_layer_group_with(b, a) == -1, "a layer in a group does not make another");
		check(app_layer_drop(b, g, 2) && parent_of(b) == g, "dropped into the group");
		check(app_layer_drop(b, a, 0) && parent_of(b) == g && row_of(b) == row_of(a) - 1, "above a layer in it: in it, above that layer");
		check(app_layer_drop(b, g, 0) && parent_of(b) == -1 && row_of(b) == row_of(g) - 1, "above the group: out of it, above it");
		check(app_layer_drop(b, g, 1) && parent_of(b) == -1 && row_of(b) == row_of(a) + 1, "below the group: below all of it");
		check(!app_layer_drop(g, b, 2), "into a layer: refused");
	}
	else if (step == 4) {
		check(app_layer_drop(c, b, 1) && parent_of(c) == -1, "c out of the group");
		check(app_layer_drop(a, b, 1) && parent_of(a) == -1, "a, the last one, out of it");
		check(layer_by_id(g) == NULL, "the empty group goes");
	}
	else {
		printf("[layer_groups] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "LAYER_GROUPS PASS" : "LAYER_GROUPS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
