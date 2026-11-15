#include "global.h"

// Layer operations and their undo: steps undo the layer they were made on whichever one is selected

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_material_t *mat_red = NULL;
slot_layer_t    *a       = NULL;
slot_layer_t    *b       = NULL;
int              texels  = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[layer_history] %s %s", ok ? "ok  " : "FAIL", name);
}

// Texels with alpha above 128
int opaque(slot_layer_t *l) {
	buffer_t *p = script_get_texture_pixels(l->texpaint);
	int       n = 0;
	for (int i = 0; i < p->length; i += 4) {
		if (buffer_get_u8(p, i + 3) > 128) {
			n = n + 1;
		}
	}
	return n;
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
		a      = script_layer_new_color(0xffffffff, 1.0, 0.5, 0.0);
		b      = script_layer_new();
		texels = opaque(a);
		check(texels > 1000 && opaque(b) == 0, "a is opaque, b is empty");
		script_layer_clear(a);
	}
	else if (step == 1) {
		check(opaque(a) == 0, "a cleared");
		script_layer_set(b); // Another layer selected before the undo
		history_undo();
	}
	else if (step == 2) {
		check(opaque(a) == texels, "undo of a clear restores the cleared layer");
		check(opaque(b) == 0, "undo of a clear leaves the selected layer alone");
		script_material_set(mat_red);
		check(script_layer_to_fill(a), "a to fill layer");
	}
	else if (step == 3) {
		check(a->fill_material == mat_red, "a is a fill layer");
		script_layer_set(b);
		history_undo();
	}
	else if (step == 4) {
		check(a->fill_material == NULL, "undo of to fill turns the converted layer back");
		check(opaque(a) == texels, "undo of to fill restores its pixels");
		check(b->fill_material == NULL && opaque(b) == 0, "undo of to fill leaves the selected layer alone");
		script_layer_set(a);
		history_redo();
	}
	else if (step == 5) {
		check(a->fill_material == mat_red, "redo of to fill");
		script_layer_set(b);
		history_undo();
		history_redo();
	}
	else if (step == 6) {
		check(a->fill_material == mat_red && b->fill_material == NULL, "redo of to fill converts the layer it was made on");
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[layer_history] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "LAYER_HISTORY PASS" : "LAYER_HISTORY FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	mat_red      = script_material_create("history_red");
	ui_node_t *r = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(r, 0, 0, 0.9, 0.1, 0.1, 1.0);
	script_material_connect(r, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	script_notify_on_next_frame(next);
}
