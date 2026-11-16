#include "global.h"

// Timelines in shadow mode: every op appends the entry kind it should, every position keeps a state, and writing a
// kept state back into the layer gives it byte for byte as it was live. Undo and redo move the cursor.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

// tl_kind_t
int TL_CLEAR      = 2;
int TL_FILL       = 4;
int TL_INVERT     = 5;
int TL_APPLY_MASK = 9;
int TL_MERGE      = 10;
int TL_RASTER     = 12;

slot_layer_t   *a      = NULL;
slot_layer_t   *b      = NULL;
slot_layer_t   *m      = NULL;
string_array_t *hashes = NULL; // Of a after each of its entries, [0] before the first

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[timeline_complete] %s %s", ok ? "ok  " : "FAIL", name);
}

char *layer_hash(slot_layer_t *l) {
	if (l->texpaint_nor == NULL) {
		return script_texture_hash(l->texpaint);
	}
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 12; ++i) {
		float t = i / 11.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	script_paint_end();
}

slot_layer_t *find_uid(int uid) {
	for (int i = 0; i < script_layer_count(); ++i) {
		if (script_layer_at(i)->uid == uid) {
			return script_layer_at(i);
		}
	}
	return NULL;
}

// The last entry of l is kind and the timeline has n entries
void check_last(slot_layer_t *l, int n, int kind, char *name) {
	int count = timeline_test_count(l);
	int last  = timeline_test_kind(l, count - 1);
	check(count == n && last == kind, string("%s (%d entries, last %d)", name, count, last));
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame          = 0;
	context_t *ctx = script_get_context();

	if (step == 0) {
		a = script_layer_new();
		check(timeline_test_count(a) == -1, "a new layer has no timeline before its first change");
		string_array_push(hashes, layer_hash(a));
		script_layer_set(a);
		stroke(0.35, 0.45, 0.6, 0.5);
		string_array_push(hashes, layer_hash(a));
		check_last(a, 1, TL_RASTER, "a stroke appends a raster entry");
		stroke(0.4, 0.3, 0.45, 0.7);
		string_array_push(hashes, layer_hash(a));
		script_layer_clear(a);
	}
	else if (step == 1) {
		string_array_push(hashes, layer_hash(a));
		check_last(a, 3, TL_CLEAR, "clear appends a clear entry");
		stroke(0.3, 0.6, 0.7, 0.6);
		string_array_push(hashes, layer_hash(a));
		b = script_layer_duplicate(a);
	}
	else if (step == 2) {
		check(b != NULL && b != a && timeline_test_count(b) == 4, string("duplicate copies the entries (%d)", timeline_test_count(b)));
		script_layer_set(a);
		m = script_layer_new_mask(a, 1); // White
	}
	else if (step == 3) {
		script_layer_set(m);
		stroke(0.35, 0.45, 0.6, 0.5);
		check_last(m, 1, TL_RASTER, "a stroke on a mask appends a raster entry");
		script_layer_invert_mask(m);
	}
	else if (step == 4) {
		check_last(m, 2, TL_INVERT, "invert appends an invert entry");
		script_layer_apply_mask(m);
	}
	else if (step == 5) {
		string_array_push(hashes, layer_hash(a));
		check_last(a, 5, TL_APPLY_MASK, "apply mask appends to the layer");
		script_layer_set(b);
		stroke(0.5, 0.2, 0.55, 0.8);
		script_layer_merge_down(b);
	}
	else if (step == 6) {
		a = script_get_context()->layer;
		string_array_push(hashes, layer_hash(a));
		check_last(a, 6, TL_MERGE, "merge down appends to the layer below");
		timeline_finalize();
		check(timeline_test_states(a) == 7, string("every position keeps a state (%d of 7)", timeline_test_states(a)));
		history_undo();
	}
	else if (step == 7) {
		slot_layer_t *back = find_uid(a->uid); // The layer as it was before the merge, it shares the timeline
		check(back != NULL && back != a && timeline_test_cursor(back) == 5, string("undo moves the cursor back (%d)", timeline_test_cursor(back)));
		check(back != NULL && string_equals(layer_hash(back), hashes->buffer[5]), "undo of the merge");
		history_redo();
	}
	else if (step == 8) {
		check(timeline_test_cursor(a) == 6, string("redo moves it forth (%d)", timeline_test_cursor(a)));
		// Every kept state back into the layer, after a trim dropped what nothing references
		hstore_test_trim(0);
		for (int i = 0; i <= 6; ++i) {
			bool ok = timeline_test_materialize(a, i);
			check(ok && string_equals(layer_hash(a), hashes->buffer[i]), string("state %d written back byte for byte", i));
		}
		check(script_layer_to_fill(a), "a to fill layer");
	}
	else if (step == 9) {
		check(timeline_test_count(a) == -1, "a fill layer has no timeline");
		check(script_layer_to_paint(a), "a to paint layer");
	}
	else if (step == 10) {
		check_last(a, 8, TL_FILL, "to fill and to paint append fill entries");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[timeline_complete] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "TIMELINE_COMPLETE PASS" : "TIMELINE_COMPLETE FAIL");
		script_get_config()->timeline_shadow = false;
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_get_config()->timeline_shadow = true;
	script_brush_defaults();
	script_project_new();
	hashes               = string_array_create(0);
	slot_material_t *mat = script_material_create("timeline_blue");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.3;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
