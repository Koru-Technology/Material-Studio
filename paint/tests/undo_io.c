#include "global.h"

// Saved undo history (history_io.c), history_v2: strokes on two layers and a mask, a clear, a mask invert, a layer
// deleted, a node edit. The history is encoded, dropped and decoded again as a project open does (its GPU undo tiles
// gone): undoing step by step gives each state back as it was before, redoing gives the end back. Then clear history
// keeps the layers and the timelines.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t   *a      = NULL;
slot_layer_t   *b      = NULL;
slot_layer_t   *m      = NULL;
string_array_t *states = NULL; // Of the project after each step, [0] before the first
int             undos0 = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[undo_io] %s %s", ok ? "ok  " : "FAIL", name);
}

char *layer_hash(slot_layer_t *l) {
	if (l->texpaint == NULL) {
		return string("group %s", l->name);
	}
	if (l->texpaint_nor == NULL) {
		return script_texture_hash(l->texpaint);
	}
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

char *project_hash() {
	char *h = string("%d", script_layer_count());
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		h               = string("%s | %d %s", h, l->uid, layer_hash(l));
	}
	return h;
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 10; ++i) {
		float t = i / 9.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	script_paint_end();
}

int NSTEPS = 9;

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		a = script_layer_new();
		b = script_layer_new();
		m = script_layer_new_mask(a, 1);
	}
	else if (step == 1) {
		undos0 = history_test_undos();
		string_array_push(states, project_hash());
		script_layer_set(a);
		stroke(0.3, 0.4, 0.6, 0.45);
	}
	else if (step == 2) {
		string_array_push(states, project_hash());
		script_layer_set(b);
		stroke(0.4, 0.3, 0.45, 0.7);
	}
	else if (step == 3) {
		string_array_push(states, project_hash());
		script_layer_set(m);
		stroke(0.3, 0.5, 0.7, 0.5);
	}
	else if (step == 4) {
		string_array_push(states, project_hash());
		script_layer_invert_mask(m);
	}
	else if (step == 5) {
		string_array_push(states, project_hash());
		script_layer_set(a);
		stroke(0.35, 0.6, 0.6, 0.65);
	}
	else if (step == 6) {
		string_array_push(states, project_hash());
		script_layer_clear(b);
	}
	else if (step == 7) {
		string_array_push(states, project_hash());
		script_layer_set(b);
		stroke(0.5, 0.2, 0.55, 0.8);
	}
	else if (step == 8) {
		string_array_push(states, project_hash());
		script_layer_delete(b);
	}
	else if (step == 9) {
		string_array_push(states, project_hash());
		script_layer_set(a);
		stroke(0.2, 0.2, 0.8, 0.3);
	}
	else if (step == 10) {
		string_array_push(states, project_hash());
		check(history_test_undos() == undos0 + NSTEPS, string("%d steps to undo (%d)", NSTEPS, history_test_undos() - undos0));
		int len = history_io_test_roundtrip(0);
		check(len > 0, string("the history encodes and decodes (%d bytes)", len));
		check(history_test_undos() == undos0 + NSTEPS, string("as many steps after (%d)", history_test_undos() - undos0));
		check(string_equals(project_hash(), states->buffer[NSTEPS]), "the project is as it was");
		history_undo();
	}
	else if (step >= 11 && step < 11 + NSTEPS) {
		int k = NSTEPS - (step - 10);
		check(!history_busy(), "restores done");
		check(string_equals(project_hash(), states->buffer[k]), string("undo back to state %d", k));
		if (k > 0) {
			history_undo();
		}
		else {
			for (int i = 0; i < NSTEPS; ++i) {
				history_redo();
			}
		}
	}
	else if (step == 11 + NSTEPS) {
		check(string_equals(project_hash(), states->buffer[NSTEPS]), "redo to the end");
		int count = timeline_test_count(a);
		history_io_clear();
		check(history_test_undos() == 0, string("clear history: nothing to undo (%d)", history_test_undos()));
		check(timeline_test_count(a) == count, "clear history keeps the timeline entries");
		check(string_equals(project_hash(), states->buffer[NSTEPS]), "clear history keeps the layers");
		check(timeline_test_materialize(a, count) && string_equals(project_hash(), states->buffer[NSTEPS]), "the layer materializes after clear history");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[undo_io] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "UNDO_IO PASS" : "UNDO_IO FAIL");
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
	states               = string_array_create(0);
	slot_material_t *mat = script_material_create("undo_io_red");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.9, 0.2, 0.1, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.2;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
