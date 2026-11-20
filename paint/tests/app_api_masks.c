#include "global.h"

// The native mask effects interface (app_api_layers.h, app_mask_*): effects listed top first, adding each type, order,
// delete, visibility, blending, slider drags as one undo step, clamping to the settings table, the props signature,
// Invert as an effect, the AO warning of Dirt and the mask thumbnail after an edit. The Add Effect menu's classification,
// the newer types, the thickness and ID map warnings, picking the parts of an ID Selection (also from "ID Mask..."), and
// the layers an Anchor references

int step   = 0;
int frame  = 0;
int checks = 0;
int fails  = 0;
int a      = -1; // Paint layer
int m      = -1; // Its mask
int sig    = 0;
int pv     = 0;
int pv_set = 0;
int b      = -1; // Paint layer of the newer types
int c      = -1; // Paint layer of "ID Mask..."
int k      = -1; // Its mask
int n      = 0;

int PAINT       = 0;
int FILL        = 1;
int LEVELS      = 2;
int INVERT      = 3;
int EDGE_WEAR   = 4;
int DIRT        = 5;
int POSITION    = 6;
int DIRECTIONAL = 7;
int ID          = 8;
int NOISE       = 9;
int LIGHT       = 10;
int THICKNESS   = 11;
int HISTOGRAM   = 12;
int BLUR        = 13;
int ANCHOR      = 14;
int BUILDER     = 15;
int AO_GEN      = 16;
int HB          = 17;
int EB          = 18;
int GROW        = 19;
int TYPES       = 20;
int AO          = 0;
int THICK_MAP   = 3;
int NEW_ID_MASK = 11;
int fifteenth   = 0; // Color of the fifteenth part picked
int RED         = 16711680; // 0xff0000
int BLUE        = 255;

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

// Colors as floats: whole, too large for near
bool same(float v, float to) {
	return v >= to - 0.5 && v <= to + 0.5;
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

slot_layer_t *layer_by_id(int id) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->id == id) {
			return l;
		}
	}
	return NULL;
}

bool is_filter(slot_layer_t *l) {
	return l->texpaint != NULL && l->texpaint_nor != NULL && l->parent != NULL && l->parent->texpaint != NULL && l->parent->texpaint_nor != NULL;
}

// Paint and fill layers: not groups (no texpaint), masks (no texpaint_nor) or filters
bool is_paint_or_fill(slot_layer_t *l) {
	return l->texpaint != NULL && l->texpaint_nor != NULL && !is_filter(l);
}

// Twenty flat stripes, green 12 apart: more parts than one ID Selection holds
void write_id_map_stripes(char *path) {
	int       w   = 80;
	buffer_t *buf = buffer_create(w * 64 * 4);
	for (int y = 0; y < 64; ++y) {
		for (int x = 0; x < w; ++x) {
			int i = (y * w + x) * 4;
			buffer_set_u8(buf, i, 64);
			buffer_set_u8(buf, i + 1, (x / 4 + 1) * 12);
			buffer_set_u8(buf, i + 2, 0);
			buffer_set_u8(buf, i + 3, 255);
		}
	}
	iron_write_png(path, buf, w, 64, 0);
}

// Four flat quadrants: red, green, blue, yellow
void write_id_map(char *path) {
	int       w   = 64;
	buffer_t *buf = buffer_create(w * w * 4);
	for (int y = 0; y < w; ++y) {
		for (int x = 0; x < w; ++x) {
			int i = (y * w + x) * 4;
			bool left = x < w / 2;
			bool top  = y < w / 2;
			buffer_set_u8(buf, i, (left && top) || (!left && !top) ? 255 : 0);
			buffer_set_u8(buf, i + 1, !left ? 255 : 0);
			buffer_set_u8(buf, i + 2, left && !top ? 255 : 0);
			buffer_set_u8(buf, i + 3, 255);
		}
	}
	iron_write_png(path, buf, w, w, 0);
}

bool picking() {
	return script_get_context()->tool == TOOL_TYPE_COLORID;
}

void next() {
	frame = frame + 1;
	if (frame < 3) { // Let each step's work reach the screen before the next
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		check(app_mask_effect_type_count() == TYPES, "twenty effect types");
		check(!string_equals(app_mask_effect_type_name(ANCHOR), "") && string_equals(app_mask_effect_type_name(TYPES), ""), "type names, empty past the last");
		bool classified = true;
		for (int t = 0; t <= TYPES; ++t) {
			bool filter    = t == LEVELS || t == INVERT || t == HISTOGRAM || t == BLUR || t == HB || t == EB || t == GROW;
			bool generator = (t >= EDGE_WEAR && t <= DIRECTIONAL) || (t >= NOISE && t <= THICKNESS) || t == BUILDER || t == AO_GEN;
			classified     = classified && app_mask_effect_type_is_filter(t) == filter && app_mask_effect_type_is_generator(t) == generator;
		}
		check(classified, "filters and generators, the rest are sources (Fill, ID Selection, Anchor)");
		check(app_mask_effect_param_count(ANCHOR) == 2 && app_mask_effect_param_count(ID) == 0, "anchor: layer and channel, ID Selection: picked");
		check(app_mask_effect_param_count(PAINT) == 0 && app_mask_effect_param_count(FILL) == 11 && app_mask_effect_param_count(LEVELS) == 5,
		      "settings of the plain effects");
		check(app_mask_effect_param_count(BUILDER) == 16 && app_mask_effect_param_count(AO_GEN) == 7 && app_mask_effect_param_count(HB) == 6 &&
		          app_mask_effect_param_count(EB) == 6 && app_mask_effect_param_count(GROW) == 3,
		      "mask builder, ambient occlusion, height blend and edge breakup settings");
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
		check(app_mask_add_effect(m, TYPES) == -1, "unknown type refused");
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
		app_drag_end();
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
		if (app_thumbnails_busy() || !pv_set) { // Thumbnails read back land first, bumping the version once
			if (!app_thumbnails_busy()) {
				pv     = app_layer_previews_version();
				pv_set = 1;
			}
			script_notify_on_next_frame(next);
			return;
		}
		check(app_layer_previews_version() == pv, "no thumbnail redraws while idle");
		check(app_mask_set_effect_value(m, index_of(FILL), 0, 0.25), "fill value edited");
		check(!app_mask_set_effect_value(m, index_of(FILL), 2, app_mask_image_count() * 1.0), "a fill's image past the project's refused");
		check(app_mask_image_count() == 0 || app_mask_set_effect_value(m, index_of(FILL), 2, 0.0), "a project image taken");
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
		check(!app_layer_add_mask(a, 9) && !app_layer_can_new(9) && !app_layer_can_new(10), "no second mask");
		check(app_layer_add_effect(a, DIRT) == k && app_mask_effect_count(k) == 3, "the next effect goes into the same mask");
		check(app_layer_add_effect(k, LEVELS) == k && app_mask_effect_count(k) == 4, "and from the mask's own menu");
		app_undo();
		app_undo();
		check(app_mask_effect_count(k) == 2, "undo both adds");
		app_undo();
		check(app_layer_count() == n, "one undo takes the effect and the mask it made");
		app_redo();
		check(app_layer_count() == n + 1, "redo brings the mask back");
		check(app_layer_add_effect(a, 0) == -1 && app_layer_add_effect(a, TYPES) == -1, "Paint and unknown types refused");
	}
	else if (step == 9) { // The newer types
		check(app_layer_new(0), "paint layer for the newer types");
		b = selected();
		check(app_layer_add_mask(b, 9), "its black mask");
		m = selected();
		sig_changed();
		for (int t = NOISE; t <= ANCHOR; ++t) {
			check(app_mask_add_effect(m, t) == 0 && app_mask_effect_type(m, 0) == t, string("type %d added on top", t));
			check(sig_changed(), string("signature changes with type %d", t));
		}
		check(app_mask_effect_blending(m, index_of(NOISE)) == 4 && app_mask_effect_blending(m, index_of(ANCHOR)) == 4, "sources start on max");
		check(near(app_mask_effect_value(m, index_of(ANCHOR), 0), -1.0), "an anchor starts on no layer");

		mesh_maps_set(THICK_MAP, NULL);
		sig_changed();
		check(app_mask_effect_needs_thickness(m, index_of(THICKNESS)), "Thickness without a thickness map");
		check(!app_mask_effect_needs_thickness(m, index_of(LIGHT)) && !app_mask_effect_needs_ao(m, index_of(THICKNESS)), "only Thickness");
		iron_write_png(test_path("koru_app_api_masks_thickness.png"), buffer_create(16 * 16 * 4), 16, 16, 0);
		script_import_asset(test_path("koru_app_api_masks_thickness.png"), 0); // *_thickness registers as the thickness map
	}
	else if (step == 10) {
		check(mesh_maps_get_asset(THICK_MAP) != NULL, "thickness map registered");
		check(!app_mask_effect_needs_thickness(m, index_of(THICKNESS)), "Thickness with a thickness map");
		check(sig_changed(), "signature changes with the thickness map");
		mesh_maps_set(THICK_MAP, NULL);
		check(app_mask_effect_needs_thickness(m, index_of(THICKNESS)) && sig_changed(), "cleared again");
		script_assets_remove("koru_app_api_masks_thickness.png");

		n = app_mask_effect_count(m);
		check(app_mask_add_effect(m, ID) == 0 && app_mask_effect_needs_id_map(m, 0), "ID Selection without an ID map");
		check(!app_mask_effect_needs_id_map(m, 1), "only ID Selection");
	}
	else if (step == 11) {
		check(!picking(), "no ID map: asks to import one instead of picking");
		ui_box_hide();
		app_undo();
		check(app_mask_effect_count(m) == n, "undo the ID Selection");
		write_id_map(test_path("koru_app_api_masks_id.png"));
		script_import_asset(test_path("koru_app_api_masks_id.png"), 0);
		write_id_map_stripes(test_path("koru_app_api_masks_id20.png"));
		script_import_asset(test_path("koru_app_api_masks_id20.png"), 0);
	}
	else if (step == 12) {
		sig_changed();
		check(script_set_id_map(script_asset_index("koru_app_api_masks_id.png")) == 1, "ID map set");
		check(sig_changed(), "signature changes with the ID map");
		check(app_mask_add_effect(m, ID) == 0 && !app_mask_effect_needs_id_map(m, 0), "ID Selection with an ID map");
		check(near(app_mask_effect_value(m, 0, 0), 0.0), "no parts yet");
	}
	else if (step == 13) {
		check(picking() && ui_id_mask_selected() == 0, "adding an ID Selection starts picking");
		check(app_test_id_parts() == 4, string("the ID map's four parts are listed (%d)", app_test_id_parts()));
		check(string_equals(app_id_part_name(0), "Part 1"), string("an imported map's parts are numbered (%s)", app_id_part_name(0)));
		int red = -1;
		for (int i = 0; i < app_test_id_parts(); ++i) {
			if (app_id_part_color(i) == RED) {
				red = i;
			}
		}
		check(red >= 0 && !app_id_part_picked(red), "red is a part, not picked");
		app_id_part_toggle(red);
		check(app_id_part_picked(red) && ui_id_mask_selected() == 1, "picked from the list");
		ui_id_mask_toggle(BLUE); // A tap on the model
		check(ui_id_mask_selected() == 2, "two parts picked");
		ui_id_mask_toggle(4194304 + 12 * 256);
		check(ui_id_mask_selected() == 2, "a color not in the ID map picks nothing");
		app_id_pick_confirm();
	}
	else if (step == 14) {
		check(!picking(), "done leaves the ID map view");
		check(near(app_mask_effect_value(m, 0, 0), 2.0) && same(app_mask_effect_value(m, 0, 1), RED) && same(app_mask_effect_value(m, 0, 2), BLUE),
		      "done writes the count and the colors");
		check(app_mask_effect_type(m, 0) == ID && app_mask_effect_count(m) == n + 1, "into the effect, nothing else added");
		app_undo();
		check(near(app_mask_effect_value(m, 0, 0), 0.0) && app_mask_effect_type(m, 0) == ID, "one undo step takes the parts back");
		app_redo();
		check(near(app_mask_effect_value(m, 0, 0), 2.0), "redo");
		check(!app_mask_effect_pick_ids(m, 1) && !app_mask_effect_pick_ids(a, 0), "picks only for an ID Selection");
		check(app_mask_effect_pick_ids(m, 0), "pick again");
	}
	else if (step == 15) {
		check(picking() && ui_id_mask_selected() == 2, "picking again starts from its parts");
		ui_id_mask_toggle(65280); // Green
		check(ui_id_mask_selected() == 3, "a third part");
		ui_id_mask_cancel();
	}
	else if (step == 16) {
		check(!picking() && near(app_mask_effect_value(m, 0, 0), 2.0), "cancel leaves the parts as they were");
		app_mask_effect_pick_ids(m, 0);
	}
	else if (step == 17) { // An ID map of twenty parts
		app_id_pick_cancel();
		check(script_set_id_map(script_asset_index("koru_app_api_masks_id20.png")) == 1, "a twenty part ID map");
		app_mask_effect_pick_ids(m, 0);
	}
	else if (step == 18) {
		check(app_test_id_parts() == 20, string("twenty parts listed (%d)", app_test_id_parts()));
		check(ui_id_mask_selected() == 0, "the parts of the other ID map are dropped");
		for (int i = 0; i < 20; ++i) {
			ui_id_mask_toggle(app_id_part_color(i)); // A tap on each stripe
		}
		check(ui_id_mask_selected() == 15, string("at most fifteen parts (%d)", ui_id_mask_selected()));
		fifteenth = app_id_part_color(14);
		ui_id_mask_done();
	}
	else if (step == 19) {
		check(near(app_mask_effect_value(m, 0, 0), 15.0) && same(app_mask_effect_value(m, 0, 15), fifteenth), "fifteen written");
		app_undo();
		check(near(app_mask_effect_value(m, 0, 0), 2.0), "undo");
		script_set_id_map(script_asset_index("koru_app_api_masks_id.png"));
	}
	else if (step == 20) { // "ID Mask...": an ID Selection in the layer's mask, made first when it has none
		check(app_layer_new(0), "paint layer for an ID mask");
		c = selected();
		n = app_layer_count();
		check(app_layer_can_new(NEW_ID_MASK) && app_layer_new(NEW_ID_MASK), "ID Mask... on a layer without a mask");
		k = selected();
		check(app_layer_count() == n + 1 && layer_by_id(k)->parent == layer_by_id(c), "makes its mask");
		check(app_mask_effect_count(k) == 2 && app_mask_effect_type(k, 0) == ID && app_mask_effect_type(k, 1) == PAINT, "with an ID Selection on its paint");
	}
	else if (step == 21) {
		check(picking(), "and picks its parts");
		ui_id_mask_toggle(65280);
		ui_id_mask_done();
	}
	else if (step == 22) {
		check(near(app_mask_effect_value(k, 0, 0), 1.0), "one part");
		app_undo();
		check(near(app_mask_effect_value(k, 0, 0), 0.0), "undo the part");
		app_undo();
		check(app_layer_count() == n, "one undo takes the effect and the mask it made");
		app_redo();
		app_redo();
		check(app_layer_count() == n + 1, "redo");
		check(app_layer_pick(c) && app_layer_can_new(NEW_ID_MASK) && app_layer_new(NEW_ID_MASK), "ID Mask... on a layer with a mask");
		k = selected();
		check(app_layer_count() == n + 1 && app_mask_effect_count(k) == 3 && app_mask_effect_type(k, 0) == ID, "adds to its mask");
	}
	else if (step == 23) {
		check(picking(), "picking");
		ui_id_mask_cancel();
	}
	else if (step == 24) {
		check(!picking() && app_mask_effect_count(k) == 3 && near(app_mask_effect_value(k, 0, 0), 0.0), "cancel leaves the new effect, no parts");
		check(app_layer_pick(c) && app_layer_new(7) && !app_layer_can_new(NEW_ID_MASK), "a group takes no ID mask");
		check(app_layer_pick(c) && app_layer_new(8), "filter, next frame");
	}
	else if (step == 25) { // Anchor: other paint and fill layers
		int  count    = app_mask_anchor_layer_count(m);
		int  expected = 0;
		bool filters  = false;
		for (int i = 0; i < script_layer_count(); ++i) {
			slot_layer_t *l = script_layer_at(i);
			filters         = filters || is_filter(l);
			if (is_paint_or_fill(l) && l->id != b) {
				expected = expected + 1;
			}
		}
		check(filters, "the project has a filter");
		check(count == expected && count >= 2, string("anchor list: every paint or fill layer but the mask's own (%d of %d)", count, expected));
		bool right = true;
		bool has_c = false;
		for (int i = 0; i < count; ++i) {
			int id = app_mask_anchor_layer_id(m, i);
			right  = right && id != b && id != m && is_paint_or_fill(layer_by_id(id));
			has_c  = has_c || id == c;
		}
		check(right && has_c && app_mask_anchor_layer_id(m, count) == -1, "no groups, masks, filters or its own layer");
		check(app_mask_anchor_layer_count(b) == 0, "only masks have anchors");

		int ai = index_of(ANCHOR);
		sig_changed();
		check(app_mask_set_effect_value(m, ai, 0, c) && near(app_mask_effect_value(m, ai, 0), c), "anchored to another layer");
		check(sig_changed(), "signature changes with the anchor");
		check(!app_mask_set_effect_value(m, ai, 0, b) && !app_mask_set_effect_value(m, ai, 0, k) && !app_mask_set_effect_value(m, ai, 0, m),
		      "its own layer and masks refused");
		app_undo();
		check(near(app_mask_effect_value(m, ai, 0), -1.0), "one undo step");
		app_redo();
		check(app_mask_set_effect_value(m, ai, 0, -1) && near(app_mask_effect_value(m, ai, 0), -1.0), "back to none");
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
