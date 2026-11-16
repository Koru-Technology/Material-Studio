#include "global.h"

// Undo v2, deep: 200 strokes on 3 layers and a mask, the GPU undo tiles dropped half way, a memory warning at the end. Undoing back to the start and
// redoing to the end gives every layer back byte for byte: tile swaps for the recent steps, timeline states for the rest.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int done   = 0; // Strokes painted

slot_layer_t *a = NULL;
slot_layer_t *b = NULL;
slot_layer_t *c = NULL;
slot_layer_t *m = NULL;

char *h_start = NULL; // Before the strokes
char *h_mid   = NULL; // After 100
char *h_end   = NULL; // After 200
int   undos   = 0;    // Undo steps available before the strokes

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[undo_deep] %s %s", ok ? "ok  " : "FAIL", name);
}

char *layer_hash(slot_layer_t *l) {
	if (l->texpaint_nor == NULL) {
		return script_texture_hash(l->texpaint);
	}
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

char *all_hash() {
	return string("%s | %s | %s | %s", layer_hash(a), layer_hash(b), layer_hash(c), layer_hash(m));
}

void stroke(int i) {
	slot_layer_t *l = i % 4 == 0 ? a : i % 4 == 1 ? b : i % 4 == 2 ? c : m;
	script_layer_set(l);
	float y = 0.2 + (i % 37) * 0.016;
	float x = 0.2 + (i % 11) * 0.05;
	for (int k = 0; k < 8; ++k) {
		float t = k / 7.0;
		script_paint(x + 0.2 * t, y + 0.05 * t);
	}
	script_paint_end();
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame          = 0;
	context_t *ctx = script_get_context();

	if (step == 0) {
		a = script_layer_new();
		b = script_layer_new();
		c = script_layer_new();
		m = script_layer_new_mask(a, 1);
	}
	else if (step == 1) {
		h_start = all_hash();
		undos   = history_test_undos();
		step    = 2;
	}
	if (step == 2) { // 20 strokes a frame
		for (int i = 0; i < 20; ++i) {
			stroke(done);
			done = done + 1;
		}
		if (done == 100) {
			h_mid = all_hash();
			history_tiles_reset(); // GPU tiles of the first 100 strokes gone
		}
		if (done < 200) {
			script_notify_on_next_frame(next);
			return;
		}
		h_end = all_hash();
		history_on_memory_warning(); // Tiles of the last 4 steps stay, the store drops what it can without reading back
		check(!string_equals(h_end, h_start) && !string_equals(h_mid, h_start), "the strokes paint");
		check(history_test_undos() == undos + 200, string("200 steps to undo (%d)", history_test_undos() - undos));
		for (int i = 0; i < 100; ++i) {
			history_undo();
		}
		step = 3;
		script_notify_on_next_frame(next);
		return;
	}
	else if (step == 3) {
		check(!history_busy(), "restores done");
		check(string_equals(all_hash(), h_mid), "100 undos (4 from tiles) give the layers after stroke 100 back");
		for (int i = 0; i < 100; ++i) {
			history_undo();
		}
	}
	else if (step == 4) {
		check(!history_busy(), "restores done");
		check(string_equals(all_hash(), h_start), "200 undos give the layers before the strokes back, past the dropped tiles");
		for (int i = 0; i < 100; ++i) {
			history_redo();
		}
	}
	else if (step == 5) {
		check(string_equals(all_hash(), h_mid), "100 redos");
		for (int i = 0; i < 100; ++i) {
			history_redo();
		}
	}
	else if (step == 6) {
		check(string_equals(all_hash(), h_end), "200 redos give the layers after the last stroke back");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[undo_deep] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "UNDO_DEEP PASS" : "UNDO_DEEP FAIL");
		script_get_config()->history_v2 = false;
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_get_config()->history_v2 = true;
	script_brush_defaults();
	script_project_new();
	slot_material_t *mat = script_material_create("deep_red");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.9, 0.2, 0.1, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.15;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
