#include "global.h"

// Layer operations and their undo: steps undo the layer they were made on whichever one is selected

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_material_t *mat_red  = NULL;
slot_material_t *mat_blue = NULL;
slot_layer_t    *a       = NULL;
slot_layer_t    *b       = NULL;
slot_layer_t    *m       = NULL;
slot_layer_t    *c       = NULL;
ui_node_t       *ln      = NULL;
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
		m = script_layer_new_mask(b, 0); // Black
	}
	else if (step == 7) {
		check(m != NULL && opaque(m) == 0, "black mask");
		script_layer_clear(m); // Clears a mask white
	}
	else if (step == 8) {
		texels = opaque(m);
		check(texels > 1000, "cleared mask is white");
		history_undo();
	}
	else if (step == 9) {
		check(opaque(m) == 0, "undo of the clear brings the black mask back");
		history_redo();
	}
	else if (step == 10) {
		check(opaque(m) == texels, "redo of a mask clear clears it white again");
		script_material_set(mat_blue); // Another material selected than the one of the fill layer
		check(script_layer_to_paint(a) && a->fill_material == NULL, "a to paint layer");
	}
	else if (step == 11) {
		history_undo();
	}
	else if (step == 12) {
		check(a->fill_material == mat_red, "undo of to paint gives the layer its own material back");
		int count  = script_layer_count();
		b->visible = 0;
		check(script_layer_merge_down(b) == NULL && script_layer_count() == count, "a hidden layer is not merged down");
		b->visible = 1;
		a->visible = 0;
		check(script_layer_merge_down(b) == NULL && script_layer_count() == count, "nothing merges down into a hidden layer");
		a->visible = 1;
		check(script_layer_get(b->name) == b && script_layer_get(a->name) == a, "both layers kept");

		// A Layer node reads a: it follows a when layers are added or removed below it
		slot_material_t *reader = script_material_create("history_reader");
		ln                      = script_material_create_node_at("LAYER", -300.0, 0.0);
		script_material_set_button(ln, 0, script_layer_index(a));
		script_material_connect(ln, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
		script_material_update();
		script_layer_set(script_layer_at(0));
		c = script_layer_new(); // Above the bottom layer, below a
	}
	else if (step == 13) {
		int v = ln->buttons->buffer[0]->default_value->buffer[0];
		check(script_layer_index(c) < script_layer_index(a) && v == script_layer_index(a), string("layer node follows a new layer below (%d)", v));
		script_layer_delete(script_layer_at(0));
	}
	else if (step == 14) {
		int v = ln->buttons->buffer[0]->default_value->buffer[0];
		check(v == script_layer_index(a), string("layer node follows a deleted layer below (%d)", v));
		history_undo();
	}
	else if (step == 15) {
		int v = ln->buttons->buffer[0]->default_value->buffer[0];
		check(v == script_layer_index(a), string("layer node follows an undone delete (%d)", v));
		history_undo(); // The new layer
	}
	else if (step == 16) {
		int v = ln->buttons->buffer[0]->default_value->buffer[0];
		check(script_layer_index(c) == -1 && v == script_layer_index(a), string("layer node follows an undone new layer (%d)", v));
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
	mat_blue     = script_material_create("history_blue");
	ui_node_t *u = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(u, 0, 0, 0.1, 0.1, 0.9, 1.0);
	script_material_connect(u, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	script_notify_on_next_frame(next);
}
