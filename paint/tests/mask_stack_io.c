#include "global.h"

// Mask stacks and mesh maps in the project: undo and redo give back the packed effects exactly, duplicates copy them,
// save and load keep them, a mask without a stack stays without one, apply mask undo brings back the mask's settings

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *base   = NULL;
slot_layer_t *mask   = NULL;
slot_layer_t *plain  = NULL;
slot_layer_t *dup    = NULL;
char         *file   = NULL;
int           layers = 0;

// Packed effects after each edit, s0 the implicit stack
char *s0 = NULL;
char *s1 = NULL;
char *s2 = NULL;
char *s3 = NULL;
char *s4 = NULL;
char *s5 = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mask_stack_io] %s %s", ok ? "ok  " : "FAIL", name);
}

char *effects(slot_layer_t *m) {
	return string_copy(script_mask_effects_string(m));
}

char *snapshot(int k) {
	if (k == 0) {
		return s0;
	}
	if (k == 1) {
		return s1;
	}
	if (k == 2) {
		return s2;
	}
	if (k == 3) {
		return s3;
	}
	if (k == 4) {
		return s4;
	}
	return s5;
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
		base  = ctx->layer;
		mask  = script_layer_new_mask(base, 1);
		plain = script_layer_new_mask(base, 0);
		script_layer_set_name(mask, "stack_mask");
		script_layer_set_name(plain, "plain_mask");
	}
	else if (step == 1) {
		check(mask != NULL && plain != NULL, "two masks");
		check(mask->mask_effects == NULL && mask_stack_count(mask) == 1, "a new mask has the implicit stack");
		s0 = effects(mask);
		check(string_equals(s0, "null"), string("implicit stack reads as null (%s)", s0));

		check(script_mask_add_effect(mask, 2, -1) == 1, "levels goes on top of the paint effect");
		s1 = effects(mask);
		script_mask_set_effect_param(mask, 1, 2, 0.5); // Gamma
		s2 = effects(mask);
		check(script_mask_add_effect(mask, 1, 0) == 0, "fill goes to the bottom");
		s3 = effects(mask);
		script_mask_set_effect_param(mask, 0, 0, 0.25); // Value
		s4 = effects(mask);
		script_mask_set_effect(mask, 0, 1, 0.75, 1); // Multiply
		s5 = effects(mask);

		check(mask_stack_count(mask) == 3, string("three effects (%d)", mask_stack_count(mask)));
		check(script_mask_effect_type(mask, 0) == 1 && script_mask_effect_type(mask, 1) == 0 && script_mask_effect_type(mask, 2) == 2,
		      "fill, paint, levels from the bottom");
		check(script_mask_get_effect_param(mask, 0, 0) == 0.25 && script_mask_get_effect_param(mask, 2, 2) == 0.5, "params read back");
		check(!string_equals(s4, s5) && !string_equals(s3, s4) && !string_equals(s1, s2), "every edit changes the packed effects");
	}
	else if (step == 2) {
		int ok = 1;
		for (int k = 5; k > 0; --k) {
			history_undo();
			char *e = effects(mask);
			if (!string_equals(e, snapshot(k - 1))) {
				ok = 0;
				printf("[mask_stack_io] undo %d: %s, expected %s", k, e, snapshot(k - 1));
			}
		}
		check(ok, "each undo gives back the effects before the edit");
		check(mask->mask_effects == NULL, "undoing the first edit brings back the implicit stack");
	}
	else if (step == 3) {
		int ok = 1;
		for (int k = 1; k <= 5; ++k) {
			history_redo();
			char *e = effects(mask);
			if (!string_equals(e, snapshot(k))) {
				ok = 0;
				printf("[mask_stack_io] redo %d: %s, expected %s", k, e, snapshot(k));
			}
		}
		check(ok, "each redo gives back the effects after the edit");
		check(mask->mask_stack_dirty || mask_stack_count(mask) == 3, "redo leaves the stack to evaluate");
	}
	else if (step == 4) {
		dup = script_layer_duplicate(mask);
	}
	else if (step == 5) {
		check(dup != NULL && dup != mask, "mask duplicated");
		check(string_equals(effects(dup), s5), "duplicate has the same effects");
		check(dup->mask_effects != mask->mask_effects, "duplicate owns its effects");
		script_mask_set_effect_param(dup, 2, 2, 2.0);
		check(string_equals(effects(mask), s5), "editing the duplicate leaves the original");
		check(script_mask_get_effect_param(dup, 2, 2) == 2.0, "duplicate edited");
		script_layer_set_name(dup, "dup_mask");

		check(string_equals(script_mesh_map_name(0), ""), "no mesh maps yet");
		script_mesh_map_set_name(0, "koru_ao.png");
		script_mesh_map_set_name(1, "koru_curvature.png");
		layers = script_layer_count();
		file   = string("%s/mask_stack_io_test.arm", script_save_path());
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 6) {
		script_project_new();
	}
	else if (step == 7) {
		check(string_equals(script_mesh_map_name(0), ""), "a new project has no mesh maps");
		check(script_layer_get("stack_mask") == NULL, "a new project has no masks");
		script_project_open(file);
	}
	else if (step == 8) {
		check(script_layer_count() == layers, string("layers survive (%d of %d)", script_layer_count(), layers));
		mask  = script_layer_get("stack_mask");
		plain = script_layer_get("plain_mask");
		dup   = script_layer_get("dup_mask");
		check(mask != NULL && plain != NULL && dup != NULL, "masks found after load");
		if (mask != NULL && plain != NULL && dup != NULL) {
			char *e = effects(mask);
			check(string_equals(e, s5), string("effects survive save and load (%s)", e));
			check(mask_stack_count(mask) == 3 && script_mask_get_effect_param(mask, 0, 0) == 0.25, "loaded stack reads back");
			check(script_mask_get_effect_param(dup, 2, 2) == 2.0, "duplicate's own edit survives");
			check(plain->mask_effects == NULL, "a mask without a stack loads without one");
		}
		check(string_equals(script_mesh_map_name(0), "koru_ao.png") && string_equals(script_mesh_map_name(1), "koru_curvature.png"),
		      "mesh map names survive");
		check(string_equals(script_mesh_map_name(2), ""), "unset mesh maps stay empty");

		// Apply mask undo: the mask comes back with its settings
		if (mask != NULL) {
			mask->mask_opacity = 0.5;
			mask->blending     = 3;
			mask->height_blend = 1;
			script_layer_apply_mask(mask);
		}
	}
	else if (step == 9) {
		check(script_layer_count() == layers - 1, "mask applied");
		history_undo();
		slot_layer_t *m = ctx->layer;
		check(script_layer_count() == layers, "undo brings the mask back");
		check(script_layer_index(m) >= 0 && m->texpaint != NULL && m->texpaint_nor == NULL, "the restored mask is selected");
		check(m->mask_opacity == 0.5 && m->blending == 3, string("opacity and blending restored (%f %d)", m->mask_opacity, m->blending));
		check(m->height_blend, "height blend restored");
		check(string_equals(effects(m), s5), string("effects restored (%s)", effects(m)));
		history_redo();
	}
	else if (step == 10) {
		check(script_layer_count() == layers - 1, "redo applies the mask again");
		history_undo();
	}
	else if (step == 11) {
		check(string_equals(effects(ctx->layer), s5), "a second undo restores the effects again");
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[mask_stack_io] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MASK_STACK_IO PASS" : "MASK_STACK_IO FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	script_notify_on_next_frame(next);
}
