#include "global.h"

// The simpler masks of the iPad layers panel: a new fill layer comes with a white mask, one undo removes both; a
// material applied as a new layer goes on top of the stack, selected, with its mask; effects go in a mask at any
// position (app_mask_insert_effect, 0 the top); a dragged layer goes beside the one it is dropped below, never into a
// group or a layer (app_layer_move_below_flat), its mask with it.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

int count0 = 0;
int fill_id   = -1;
int top    = -1;
int paint  = -1;
int group  = -1;
int inner  = -1;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[masks_simplify] %s %s", ok ? "ok  " : "FAIL", name);
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

// The first mask of layer id, -1 none
int mask_of(int id) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->parent != NULL && l->parent->id == id && slot_layer_is_mask(l)) {
			return l->id;
		}
	}
	return -1;
}

int parent_of(int id) {
	slot_layer_t *l = layer_by_id(id);
	return l != NULL && l->parent != NULL ? l->parent->id : -1;
}

int selected() {
	return script_get_context()->layer->id;
}

void next() {
	frame = frame + 1;
	if (frame < 4) { // Fill layers are made on the next frame
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		count0 = app_layer_count();
		check(app_layer_new(1), "new fill layer"); // APP_LAYER_NEW_FILL
	}
	else if (step == 1) {
		fill_id = selected();
		check(layer_by_id(fill_id) != NULL && layer_by_id(fill_id)->fill != NULL, "the fill layer is selected");
		int m = mask_of(fill_id);
		check(m >= 0, "it has a mask");
		check(m >= 0 && app_mask_effect_count(m) >= 1, string("the mask has its paint (%d effects)", m >= 0 ? app_mask_effect_count(m) : 0));
		app_undo();
		check(app_layer_count() == count0, "one undo removes the layer and its mask");
		app_redo();
		check(app_layer_count() == count0 + 2, "redo brings both back");
	}
	else if (step == 2) {
		slot_layer_t *s = script_get_context()->layer; // Redo made the fill layer again, under a new id
		fill_id         = slot_layer_is_mask(s) ? s->parent->id : s->id;
		check(layer_by_id(fill_id)->fill != NULL, "the fill layer selected again");
		check(app_layer_new(0), "a paint layer"); // Above the fill layer, then a material goes on top of it
	}
	else if (step == 3) {
		paint = selected();
		check(app_material_new_top_layer(app_material_id_at(0)), "material as a new layer");
	}
	else if (step == 4) {
		top = selected();
		check(top != paint && layer_by_id(top)->fill != NULL, "a new fill layer, selected");
		check(app_layer_id_at(0) == top, "on top of the stack");
		check(mask_of(top) >= 0, "with its mask");
		int m = mask_of(top);
		int n = app_mask_effect_count(m);
		check(app_mask_insert_effect(m, 2, n) == n, "levels at the bottom");
		check(app_mask_effect_type(m, n) == 2, "applied first");
		check(app_mask_insert_effect(m, 3, 0) == 0 && app_mask_effect_type(m, 0) == 3, "invert on top");
		check(app_mask_effect_type(m, n + 1) == 2, "levels still at the bottom");
		app_undo();
		check(app_mask_effect_type(m, 0) != 3, "undo: the invert goes");
		// A group with a layer in it, to drop beside
		check(app_layer_new(0), "a paint layer for the group");
	}
	else if (step == 5) {
		inner = selected();
		check(app_layer_new(7), "a group"); // APP_LAYER_NEW_GROUP
	}
	else if (step == 6) {
		group = selected();
		check(parent_of(inner) == group || (app_layer_set_group(inner, group) && parent_of(inner) == group), "a layer in the group");
	}
	else if (step == 7) {
		check(app_layer_move_below_flat(paint, inner), "dropped below the layer in the group");
		check(parent_of(paint) == -1, "beside the group, not in it");
		check(app_layer_move_below_flat(top, fill_id), "the masked fill layer dropped below another masked fill layer");
		check(parent_of(top) == -1 && parent_of(mask_of(fill_id)) == fill_id, "not into it, nor between it and its mask");
		check(mask_of(top) >= 0 && parent_of(mask_of(top)) == top, "its mask goes with it");
		check(app_layer_move_below_flat(inner, -1) && parent_of(inner) == -1, "out of the group to the top");
	}
	else {
		printf("[masks_simplify] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MASKS_SIMPLIFY PASS" : "MASKS_SIMPLIFY FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
