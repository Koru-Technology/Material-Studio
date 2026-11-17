#include "global.h"

// The native layers panel's interface (app_api_layers.h): new layer kinds, the layer menu, order and groups, properties

int step   = 0;
int frame  = 0;
int checks = 0;
int fails  = 0;
int count0 = 0;
int count1 = 0;
int a      = -1; // Paint layers
int b      = -1;
int g      = -1; // Group of b
int m      = -1; // Mask of a
int dup    = -1;

void check(bool ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[app_api_layers] %s %s", ok ? "ok  " : "FAIL", name);
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

bool is_group(slot_layer_t *l) {
	return l->texpaint == NULL;
}

bool is_mask(slot_layer_t *l) {
	return l->texpaint != NULL && l->texpaint_nor == NULL;
}

int selected() {
	return script_get_context()->layer->id;
}

int parent_id(int id) {
	slot_layer_t *l = layer_by_id(id);
	if (l == NULL || l->parent == NULL) {
		return -1;
	}
	return l->parent->id;
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
		check(app_layer_can_new(0), "can add a paint layer");
		check(app_layer_new(0), "paint layer a");
		a = selected();
		check(app_layer_new(0), "paint layer b");
		b = selected();
		check(app_layer_new(7), "group of b");
		g = selected();
		check(is_group(layer_by_id(g)) && parent_id(b) == g, "b is in the group");
		check(app_layer_count() == count0 + 3, "three layers more");
	}
	else if (step == 1) {
		check(app_layer_set_group(a, g) && parent_id(a) == g, "a into the group");
		check(!app_layer_set_group(a, g), "into its own group again: no change");
		check(!app_layer_set_group(g, g), "a group into itself refused");
		check(app_layer_set_group(a, -1) && parent_id(a) == -1, "a out of the group");
		check(layer_by_id(g) != NULL && parent_id(b) == g, "the group stays with b");
		app_undo();
		check(parent_id(a) == g, "undo puts a back in the group");
		app_redo();
		check(parent_id(a) == -1, "redo takes it out again");
	}
	else if (step == 2) {
		check(app_layer_move_below(a, -1) && app_layer_id_at(0) == a, "a to the top");
		check(!app_layer_can_move_below(a, a), "below itself refused");
		check(!app_layer_can_move_below(g, b), "a group below its own child refused");
		check(app_layer_move_below(a, g) && parent_id(a) == g, "below an open group: into it");
		check(app_layer_set_group(a, -1) && parent_id(a) == -1, "out again, from the top of the group");
		check(app_layer_move_below(a, -1), "a to the top again");
		check(app_layer_set_expanded(g, false) && !app_layer_expanded(g), "group folded");
		check(app_layer_move_below(a, g) && parent_id(a) == -1, "below a folded group: after it");
		check(app_layer_set_expanded(g, true) && app_layer_expanded(g), "group open");
		check(app_layer_move_below(a, b) && parent_id(a) == g, "below the last child: into the group in place");
		app_undo();
		check(parent_id(a) == -1, "undo takes it out");
		app_redo();
		check(parent_id(a) == g, "redo puts it in");
		check(app_layer_set_group(a, -1) && parent_id(a) == -1, "the last child out in place");
	}
	else if (step == 3) {
		slot_layer_t *l = layer_by_id(a);
		check(app_layer_set_blending(a, 2) && l->blending == 2, "blending multiply");
		app_undo();
		check(l->blending == 0, "undo blending");
		check(app_layer_set_channel(a, 2, false) && !l->paint_nor, "normal channel off");
		check(app_layer_set_channel(a, 2, true) && l->paint_nor, "normal channel on");
		check(!app_layer_set_channel(g, 0, false), "groups have no channels");
		check(!app_layer_set_uv_map(a, 5), "uv map out of range refused");
		char *name0 = string_copy(l->name);
		check(app_layer_set_name(a, "Renamed") && string_equals(l->name, "Renamed"), "renamed");
		app_undo();
		check(string_equals(l->name, name0), "undo rename");
		check(app_layer_pick(a) && selected() == a, "picked");
	}
	else if (step == 4) {
		check(app_layer_add_mask(a, 9), "black mask on a");
		m = selected();
		slot_layer_t *mask = layer_by_id(m);
		check(is_mask(mask) && parent_id(m) == a, "the mask is a's");
		check(!app_layer_add_mask(m, 9), "no mask on a mask");
		check(!app_layer_set_value(m, 6, 1.0) && !app_layer_set_value(a, 6, 1.0), "a painted mask or layer has no fill values");
		check(app_layer_invert_mask(m), "mask inverted");
	}
	else if (step == 5) {
		count1 = app_layer_count();
		check(app_layer_apply_mask(m) && layer_by_id(m) == NULL, "mask applied");
		check(app_layer_count() == count1 - 1 && selected() == a, "a selected after");
		check(app_layer_to_fill(a) && layer_by_id(a)->fill != NULL, "a to fill layer");
		check(!app_layer_clear(a), "a fill layer does not clear");
	}
	else if (step == 6) {
		slot_layer_t *l = layer_by_id(a);
		float scale0 = app_layer_value(a, 6);
		check(app_layer_set_value(a, 6, 2.0) && app_layer_value(a, 6) > 1.99 && app_layer_value(a, 6) < 2.01, "fill scale");
		app_undo();
		check(app_layer_value(a, 6) > scale0 - 0.01 && app_layer_value(a, 6) < scale0 + 0.01, "undo fill scale");
		check(app_layer_set_fill_projection(a, 1) && app_layer_fill_projection(a) == 1, "triplanar");
		check(app_layer_material_count() > 0, "materials listed");
		check(!app_layer_set_fill_material(a, 0, app_layer_material_count()), "material out of range refused");
	}
	else if (step == 7) {
		check(app_layer_to_paint(a) && layer_by_id(a)->fill == NULL, "a to paint layer");
		check(app_layer_clear(a), "cleared");
		count1 = app_layer_count();
		check(app_layer_duplicate(a) && app_layer_count() == count1 + 1, "duplicated");
		dup = selected();
		check(dup != a, "the copy is selected");
		check(app_layer_merge_down(dup) && app_layer_count() == count1, "merged down");
	}
	else if (step == 8) {
		count1 = app_layer_count();
		check(!app_layer_merge_group(a), "merge group of a layer refused");
		check(app_layer_merge_group(g) && layer_by_id(g) == NULL, "group merged");
		check(app_layer_count() == count1 - 1, "the group became one layer");
		count1 = app_layer_count();
		check(app_layer_new(1), "fill layer, next frame");
	}
	else if (step == 9) {
		check(app_layer_count() == count1 + 1 && layer_by_id(selected())->fill != NULL, "fill layer added");
		count1 = app_layer_count();
		check(app_layer_new(2), "decal, next frame");
	}
	else if (step == 10) {
		check(app_layer_count() == count1 + 1 && app_layer_fill_projection(selected()) == 2, "decal added");
		count1 = app_layer_count();
		check(app_layer_new(3), "color layer, next frame");
	}
	else if (step == 11) {
		check(app_layer_count() == count1 + 1, "color layer added");
		check(app_layer_new(4) && layer_by_id(selected())->path_text, "text layer");
		check(app_layer_new(5) && layer_by_id(selected())->path_points != NULL, "path layer");
		check(app_layer_new(6) && layer_by_id(selected())->path_curved, "curve layer");
		int path = selected();
		check(app_layer_new(10) && parent_id(selected()) == path, "white mask on the selected layer");
		check(!app_layer_can_new(9) && !app_layer_new(9), "one mask per layer, also from its mask");
		check(!app_layer_add_mask(path, 9), "no second mask from the layer menu");
		check(!app_layer_add_mask(path, 11), "no fill masks"); // 11: APP_LAYER_NEW_ID_MASK, not a mask kind
	}
	else if (step == 12) {
		int n = app_layer_count();
		int deleted = 0;
		for (int i = 0; i < n; ++i) { // Back to one layer
			int id = app_layer_id_at(0);
			if (app_layer_count() > count0 && app_layer_can_delete(id) && app_layer_delete(id)) {
				deleted = deleted + 1;
			}
		}
		check(deleted > 0, "layers deleted");
		check(app_layers_props_signature() != 0, "signature");
	}
	else {
		printf("[app_api_layers] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "APP_API_LAYERS PASS" : "APP_API_LAYERS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
