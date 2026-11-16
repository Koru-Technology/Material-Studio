#include "global.h"

// Undo v2, coalescing: 50 undos in one frame restore the layer once and land on the right state. The counts follow at
// once, the pixels on the next frame. Painting another layer meanwhile does not wait for the restore.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t   *a      = NULL;
slot_layer_t   *b      = NULL;
string_array_t *hashes = NULL; // Of a after k strokes
int             undos  = 0;
int             runs   = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[undo_coalesce] %s %s", ok ? "ok  " : "FAIL", name);
}

char *layer_hash(slot_layer_t *l) {
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

void stroke(slot_layer_t *l, int i) {
	script_layer_set(l);
	float y = 0.2 + (i % 29) * 0.02;
	for (int k = 0; k < 8; ++k) {
		float t = k / 7.0;
		script_paint(0.25 + 0.5 * t, y + 0.03 * t);
	}
	script_paint_end();
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		b = script_layer_new();
		a = script_layer_new();
	}
	else if (step == 1) {
		string_array_push(hashes, layer_hash(a));
		for (int i = 0; i < 60; ++i) {
			stroke(a, i);
			string_array_push(hashes, layer_hash(a));
		}
		history_tiles_reset(); // Undo restores from the timeline
		undos = history_test_undos();
		runs  = history_test_restore_runs();
		for (int i = 0; i < 50; ++i) {
			history_undo();
		}
		check(history_test_undos() == undos - 50, "the undo count follows at once");
		check(history_busy(), "busy while the layer is to be restored");
		stroke(b, 0); // Another layer paints meanwhile
		check(history_busy(), "painting another layer does not wait for the restore");
		check(history_test_restore_runs() == runs, "nothing restored yet");
		script_notify_on_next_frame(next);
		step = 2;
		return;
	}
	else if (step == 2) {
		check(!history_busy(), "restored on the next frame");
		check(history_test_restore_runs() == runs + 1, string("one restore for 50 undos (%d)", history_test_restore_runs() - runs));
		check(string_equals(layer_hash(a), hashes->buffer[10]), "the layer is as after stroke 10");
		// Undo again, then paint the layer being restored: its restore runs first
		runs = history_test_restore_runs();
		for (int i = 0; i < 5; ++i) {
			history_undo(); // The stroke on b, then 4 on a
		}
		stroke(a, 100);
		check(history_test_restore_runs() == runs + 1, "painting the layer restores it first");
		check(!history_busy(), "nothing left to restore");
		history_undo(); // The stroke
	}
	else if (step == 3) {
		check(string_equals(layer_hash(a), hashes->buffer[6]), "undo of the stroke after the restore");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[undo_coalesce] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "UNDO_COALESCE PASS" : "UNDO_COALESCE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	hashes               = string_array_create(0);
	slot_material_t *mat = script_material_create("coalesce_green");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.8, 0.2, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.15;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
