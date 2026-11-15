#include "global.h"

// The native mask effects interface (app_api_layers.h, app_mask_*): effects listed top first, adding each type, order,
// delete, visibility, blending, slider drags as one undo step, clamping to the settings table, the props signature,
// Invert as an effect, the AO warning of Dirt and the mask thumbnail after an edit

int step   = 0;
int frame  = 0;
int checks = 0;
int fails  = 0;
int a      = -1; // Paint layer
int m      = -1; // Its mask
int sig    = 0;
int pv     = 0;

int PAINT       = 0;
int FILL        = 1;
int LEVELS      = 2;
int INVERT      = 3;
int EDGE_WEAR   = 4;
int DIRT        = 5;
int POSITION    = 6;
int DIRECTIONAL = 7;
int AO          = 0;

void check(bool ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[app_api_masks] %s %s", ok ? "ok  " : "FAIL", name);
}

int selected() {
	return script_get_context()->layer->id;
}

// The props signature changed since the last call
bool sig_changed() {
	int s = app_layers_props_signature();
	bool c = s != sig;
	sig = s;
	return c;
}

bool near(float v, float to) {
	return v > to - 0.001 && v < to + 0.001;
}

int index_of(int type) {
	for (int i = 0; i < app_mask_effect_count(m); ++i) {
		if (app_mask_effect_type(m, i) == type) {
			return i;
		}
	}
	return -1;
}

int count_of(int type) {
	int n = 0;
	for (int i = 0; i < app_mask_effect_count(m); ++i) {
		if (app_mask_effect_type(m, i) == type) {
			n = n + 1;
		}
	}
	return n;
}

char *test_path(char *name) {
	return string("%s/%s", script_save_path(), name);
}

void next() {
	frame = frame + 1;
	if (frame < 3) { // Let each step's work reach the screen before the next
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		check(app_mask_effect_type_count() == 8, "eight effect types");
		check(app_mask_effect_type_name(EDGE_WEAR) != NULL && app_mask_effect_type_name(8) == NULL, "type names, none past the last");
		check(app_mask_effect_param_count(PAINT) == 0 && app_mask_effect_param_count(FILL) == 1 && app_mask_effect_param_count(LEVELS) == 5,
		      "settings of the plain effects");
		check(app_mask_effect_param_count(EDGE_WEAR) == 6 && app_mask_effect_param_count(DIRT) == 7, "generators end with the noise block");
		check(app_mask_blending_count() == 7, "seven mask blendings");
		check(app_layer_new(0), "paint layer");
		a = selected();
		check(app_layer_add_mask(a, 9), "black mask");
		m = selected();
		check(app_mask_effect_count(a) == 0, "a layer has no effects");
		check(app_mask_effect_count(m) == 1 && app_mask_effect_type(m, 0) == PAINT, "a new mask is its Paint effect");
		sig_changed();
		for (int t = FILL; t <= DIRECTIONAL; ++t) {
			int i = app_mask_add_effect(m, t);
			check(i == 0 && app_mask_effect_type(m, 0) == t, string("type %d added on top", t));
			check(sig_changed(), string("signature changes with type %d", t));
		}
		check(app_mask_effect_count(m) == 8, "eight effects");
		check(app_mask_add_effect(m, PAINT) == -1 && app_mask_effect_count(m) == 8, "a second Paint effect refused");
		check(app_mask_add_effect(m, 8) == -1, "unknown type refused");
		bool ordered = true;
		for (int i = 0; i < 8; ++i) {
			ordered = ordered && app_mask_effect_type(m, i) == DIRECTIONAL - i;
		}
		check(ordered, "listed top first, Paint at the bottom");
		check(app_mask_effect_type(m, 8) == -1 && app_mask_effect_type(m, -1) == -1, "indices out of range");
	}
	else if (step == 1) {
		check(!app_mask_remove_effect(m, 7), "Paint can not be deleted");
		check(app_mask_remove_effect(m, 0) && app_mask_effect_count(m) == 7 && app_mask_effect_type(m, 0) == POSITION, "top effect deleted");
		check(sig_changed(), "signature changes with a delete");
		app_undo();
		check(app_mask_effect_count(m) == 8 && app_mask_effect_type(m, 0) == DIRECTIONAL, "undo brings it back");
		app_redo();
		check(app_mask_effect_count(m) == 7 && app_mask_effect_type(m, 0) == POSITION, "redo deletes it again");
		sig_changed();

		// [Position, Dirt, Edge Wear, Invert, Levels, Fill, Paint]
		check(app_mask_move_effect(m, 0, 2) && app_mask_effect_type(m, 0) == DIRT && app_mask_effect_type(m, 2) == POSITION, "moved down two");
		check(sig_changed(), "signature changes with a move");
		check(!app_mask_move_effect(m, 0, 99) && !app_mask_move_effect(m, 1, 1), "moves out of range or in place refused");
		check(app_mask_add_effect(m, FILL) == 0 && app_mask_effect_type(m, 0) == FILL && app_mask_effect_type(m, 3) == POSITION, "added on top");
		app_undo();
		check(app_mask_effect_count(m) == 7 && count_of(FILL) == 1, "undo the add");
		app_undo();
		check(app_mask_effect_type(m, 0) == POSITION, "undo the move");
	}
	else if (step == 2) {
		sig_changed();
		check(app_mask_set_effect_visible(m, 0, false) && !app_mask_effect_visible(m, 0), "hidden");
		check(sig_changed(), "signature changes with visibility");
		check(!app_mask_set_effect_visible(m, 0, false), "hidden again: no change");
		app_undo();
		check(app_mask_effect_visible(m, 0), "undo shows it");
		sig_changed();

		check(app_mask_set_effect_blending(m, 0, 1) && app_mask_effect_blending(m, 0) == 1, "blending multiply");
		check(sig_changed(), "signature changes with blending");
		check(!app_mask_set_effect_blending(m, 0, 7), "blending out of range refused");
		check(!app_mask_set_effect_blending(m, index_of(LEVELS), 1), "filters have no blending");

		int ew = index_of(EDGE_WEAR);
		app_mask_effect_value_begin(m, ew, 0);
		app_mask_set_effect_value(m, ew, 0, 0.2);
		app_mask_set_effect_value(m, ew, 0, 0.8);
		app_mask_effect_value_end();
		check(near(app_mask_effect_value(m, ew, 0), 0.8), "amount set by the drag");
		check(sig_changed(), "signature changes with a drag");
		app_undo();
		check(near(app_mask_effect_value(m, ew, 0), 0.5), "one undo reverts the whole drag");
		check(app_mask_effect_blending(m, 0) == 1, "the blending step stays");

		check(app_mask_set_effect_value(m, ew, 0, 0.3) && app_mask_set_effect_value(m, ew, 0, 0.4), "two sets without a drag");
		app_undo();
		check(near(app_mask_effect_value(m, ew, 0), 0.3), "the second is its own undo step");
		app_undo();
		check(near(app_mask_effect_value(m, ew, 0), 0.5), "and so is the first");
		check(!app_mask_set_effect_value(m, ew, 0, 0.5), "a set to the same value: no step");
	}
	else if (step == 3) {
		int ew  = index_of(EDGE_WEAR);
		int lev = index_of(LEVELS);
		int pos = index_of(POSITION);
		check(app_mask_set_effect_value(m, ew, 0, 5.0) && near(app_mask_effect_value(m, ew, 0), 1.0), "amount clamped to 1");
		check(app_mask_set_effect_value(m, lev, 2, 50.0) && near(app_mask_effect_value(m, lev, 2), 10.0), "gamma clamped to 10");
		check(app_mask_set_effect_value(m, lev, 2, -1.0) && near(app_mask_effect_value(m, lev, 2), 0.1), "gamma clamped to 0.1");
		check(app_mask_set_effect_value(m, pos, 0, 2.7) && near(app_mask_effect_value(m, pos, 0), 2.0), "axis: a whole choice in range");
		check(app_mask_set_effect_value(m, ew, 14, 12.4) && near(app_mask_effect_value(m, ew, 14), 12.0), "seed whole");
		check(app_mask_set_effect_value(m, ew, -1, -2.0) && near(app_mask_effect_value(m, ew, -1), 0.0), "opacity at least 0");
		check(app_mask_set_effect_value(m, ew, -1, 3.0) && near(app_mask_effect_value(m, ew, -1), 1.0), "opacity at most 1");
		check(!app_mask_set_effect_value(m, ew, 5, 0.5), "a setting the type does not have refused");
		check(!app_mask_set_effect_value(m, index_of(PAINT), 0, 0.5), "Paint has no settings");
		check(!app_mask_set_effect_value(m, 99, 0, 0.5), "index out of range refused");
		check(app_mask_reset_effect(m, ew) && near(app_mask_effect_value(m, ew, 0), 0.5) && near(app_mask_effect_value(m, ew, 14), 0.0),
		      "reset to the defaults");
		check(!app_mask_reset_effect(m, ew), "reset again: no change");
	}
	else if (step == 4) {
		int n = count_of(INVERT);
		check(app_layer_invert_mask(m) && count_of(INVERT) == n + 1, "invert adds an Invert effect");
		app_undo();
		check(count_of(INVERT) == n, "one undo step");

		mesh_maps_set(AO, NULL);
		sig_changed();
		check(app_mask_effect_needs_ao(m, index_of(DIRT)), "Dirt without an AO map");
		check(!app_mask_effect_needs_ao(m, index_of(EDGE_WEAR)), "only Dirt uses AO");
		iron_write_png(test_path("koru_app_api_masks_ao.png"), buffer_create(16 * 16 * 4), 16, 16, 0);
		script_import_asset(test_path("koru_app_api_masks_ao.png"), 0); // *_ao registers as the AO map
	}
	else if (step == 5) {
		check(mesh_maps_get_asset(AO) != NULL, "AO map registered");
		check(!app_mask_effect_needs_ao(m, index_of(DIRT)), "Dirt with an AO map");
		check(sig_changed(), "signature changes with the AO map");
		mesh_maps_set(AO, NULL);
		check(app_mask_effect_needs_ao(m, index_of(DIRT)) && sig_changed(), "cleared again");
		script_assets_remove("koru_app_api_masks_ao.png");

		pv = app_layer_previews_version();
	}
	else if (step == 6) {
		check(app_layer_previews_version() == pv, "no thumbnail redraws while idle");
		check(app_mask_set_effect_value(m, index_of(FILL), 0, 0.25), "fill value edited");
	}
	else if (step == 7) {
		check(app_layer_previews_version() != pv, "the mask thumbnail is redrawn after an effect edit");
		check(app_layer_delete(a), "layer deleted");
		check(app_mask_effect_count(m) == 0, "a deleted mask has no effects");
	}
	else if (step == 8) { // One mask per layer, as Painter: more goes into its effects
		check(app_layer_new(0), "plain paint layer");
		a     = selected();
		int n = app_layer_count();
		int k = app_layer_add_effect(a, EDGE_WEAR);
		check(k >= 0 && app_layer_count() == n + 1 && selected() == k, "an effect on a layer without a mask makes a black mask");
		check(app_mask_effect_count(k) == 2 && app_mask_effect_type(k, 0) == EDGE_WEAR && app_mask_effect_type(k, 1) == PAINT, "the effect sits on its paint");
		check(app_mask_effect_blending(k, 0) == 4 && app_mask_effect_blending(k, 1) == 0, "generators start on max, paint on normal");
		check(!app_layer_add_mask(a, 9) && !app_layer_can_new(9) && !app_layer_can_new(12), "no second mask");
		check(app_layer_add_effect(a, DIRT) == k && app_mask_effect_count(k) == 3, "the next effect goes into the same mask");
		check(app_layer_add_effect(k, LEVELS) == k && app_mask_effect_count(k) == 4, "and from the mask's own menu");
		app_undo();
		app_undo();
		check(app_mask_effect_count(k) == 2, "undo both adds");
		app_undo();
		check(app_layer_count() == n, "one undo takes the effect and the mask it made");
		app_redo();
		check(app_layer_count() == n + 1, "redo brings the mask back");
		check(app_layer_add_effect(a, 0) == -1 && app_layer_add_effect(a, 8) == -1, "Paint and unknown types refused");
	}
	else {
		printf("[app_api_masks] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "APP_API_MASKS PASS" : "APP_API_MASKS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
