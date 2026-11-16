#include "global.h"

// Undo v2, apply mask: a painted mask onto a fill layer, a fill mask onto a paint layer. Undo gives the layer and the mask
// back byte for byte and fill again, redo gives the applied layer back.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *f      = NULL; // Fill layer
slot_layer_t *p      = NULL; // Paint layer
slot_layer_t *m      = NULL;
char         *before = NULL; // Layer
char         *mask   = NULL;
char         *after  = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[undo_apply_mask] %s %s", ok ? "ok  " : "FAIL", name);
}

char *layer_hash(slot_layer_t *l) {
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

// The mask of l, NULL when none
slot_layer_t *mask_of(slot_layer_t *l) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *c = script_layer_at(i);
		if (c->parent == l && c->texpaint_nor == NULL) {
			return c;
		}
	}
	return NULL;
}

void stroke() {
	for (int k = 0; k < 10; ++k) {
		float t = k / 9.0;
		script_paint(0.3 + 0.4 * t, 0.45 + 0.1 * t);
	}
	script_paint_end();
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		f = script_layer_new_fill();
	}
	else if (step == 1) {
		m = script_layer_new_mask(f, 0); // Black, painted white
	}
	else if (step == 2) {
		script_layer_set(m);
		stroke();
		before = layer_hash(f);
		mask   = script_texture_hash(m->texpaint);
		script_layer_apply_mask(m);
	}
	else if (step == 3) {
		after = layer_hash(f);
		check(f->fill_material == NULL && mask_of(f) == NULL && !string_equals(after, before), "mask applied to the fill layer, now painted");
		history_undo();
	}
	else if (step == 4) {
		m = mask_of(f);
		check(f->fill_material != NULL, "undo: a fill layer again");
		check(m != NULL && string_equals(script_texture_hash(m->texpaint), mask), "undo: the mask back byte for byte");
		history_redo();
	}
	else if (step == 5) {
		check(f->fill_material == NULL && mask_of(f) == NULL && string_equals(layer_hash(f), after), "redo: the applied layer back");
		// A fill mask onto a paint layer
		p = script_layer_new();
	}
	else if (step == 6) {
		script_layer_set(p);
		stroke();
		m = script_layer_new_mask(p, 2);
	}
	else if (step == 7) {
		before = layer_hash(p);
		mask   = script_texture_hash(m->texpaint);
		check(m->fill_material != NULL, "a fill mask");
		script_layer_apply_mask(m);
	}
	else if (step == 8) {
		after = layer_hash(p);
		history_undo();
	}
	else if (step == 9) {
		m = mask_of(p);
		check(string_equals(layer_hash(p), before), "undo: the layer back byte for byte");
		check(m != NULL && m->fill_material != NULL, "undo: a fill mask again");
		check(m != NULL && string_equals(script_texture_hash(m->texpaint), mask), "undo: the mask's render back");
		history_redo(); // Restored at once (frames after it the selected layer may be dilated again, as after any apply)
		check(mask_of(p) == NULL && string_equals(layer_hash(p), after), "redo: the applied layer back");
	}
	else if (step == 10) {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[undo_apply_mask] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "UNDO_APPLY_MASK PASS" : "UNDO_APPLY_MASK FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	slot_material_t *mat = script_material_create("apply_orange");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.9, 0.5, 0.1, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.15;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
