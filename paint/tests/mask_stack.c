#include "global.h"

// Mask stacks: effects evaluated bottom to top into texmask, read by the viewport, apply and export in place of texpaint

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *layer  = NULL;
slot_layer_t *mask   = NULL;
int           fill_i = -1;
int           inv_i  = -1;
int           lev_i  = -1;
int           dark   = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mask_stack] %s %s", ok ? "ok  " : "FAIL", name);
}

// Gray value at the texture center (masks are gray, red sits in byte 0 or 2 depending on the backend)
int center(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	int       i = ((t->height / 2) * t->width + t->width / 2) * 4;
	int       r = buffer_get_u8(p, i);
	int       b = buffer_get_u8(p, i + 2);
	return r > b ? r : b;
}

// Texels of t with byte (channel) in [lo, hi]
int count_range(gpu_texture_t *t, int channel, int lo, int hi) {
	buffer_t *p = script_get_texture_pixels(t);
	int       n = 0;
	for (int i = 0; i < p->length; i += 4) {
		int v = buffer_get_u8(p, i + channel);
		if (v >= lo && v <= hi) {
			n = n + 1;
		}
	}
	return n;
}

int near(int v, int target) {
	int d = v - target;
	return d >= -3 && d <= 3;
}

void stroke(float y) {
	for (int i = 0; i < 20; ++i) {
		script_paint(0.35 + i * 0.015, y + i * 0.004);
	}
	script_paint_end();
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
		layer = script_layer_new_color(0xffffffff, 1.0, 0.5, 0.0);
		mask  = script_layer_new_mask(layer, 1); // White
	}
	else if (step == 1) {
		check(mask != NULL && mask->parent == layer, "white mask added");
		check(mask_stack_count(mask) == 1, "a new mask is the implicit stack of one paint effect");
		check(slot_layer_mask_texture(mask) == mask->texpaint, "trivial stack reads texpaint");
		check(mask->texmask == NULL, "trivial stack has no texmask");
		fill_i = script_mask_add_effect(mask, 1, -1); // Fill
		script_mask_set_effect_param(mask, fill_i, 0, 0.5);
		script_mask_set_effect(mask, fill_i, true, 1.0, 1); // Multiply
	}
	else if (step == 2) {
		check(fill_i == 1 && mask_stack_count(mask) == 2, "fill effect on top");
		gpu_texture_t *t = slot_layer_mask_texture(mask);
		check(t != mask->texpaint && t == mask->texmask, "stack reads texmask");
		int v = center(t);
		check(near(v, 128), string("fill 0.5 multiply over white (%d)", v));
		check(center(mask->texpaint) >= 250, "texpaint keeps the paint");
		inv_i = script_mask_add_effect(mask, 3, -1); // Invert
	}
	else if (step == 3) {
		int v = center(slot_layer_mask_texture(mask));
		check(near(v, 127), string("invert on top (%d)", v));
		script_mask_move_effect(mask, inv_i, 1); // Paint, invert, fill
	}
	else if (step == 4) {
		check(script_mask_effect_type(mask, 1) == 3 && script_mask_effect_type(mask, 2) == 1, "invert moved below the fill");
		int v = center(slot_layer_mask_texture(mask));
		check(v <= 3, string("reorder: inverted white times 0.5 (%d)", v));
		script_mask_set_effect(mask, 1, false, 1.0, 0); // Hide invert
	}
	else if (step == 5) {
		int v = center(slot_layer_mask_texture(mask));
		check(near(v, 128), string("hidden effect is skipped (%d)", v));
		script_mask_remove_effect(mask, 1);
		script_mask_set_effect_param(mask, 1, 0, 0.25);
		script_mask_set_effect(mask, 1, true, 1.0, 0); // Normal fill 0.25
		lev_i = script_mask_add_effect(mask, 2, -1);   // Levels
		script_mask_set_effect_param(mask, lev_i, 1, 0.5); // In high
	}
	else if (step == 6) {
		check(mask_stack_count(mask) == 3 && lev_i == 2, "paint, fill, levels");
		int v = center(slot_layer_mask_texture(mask));
		check(near(v, 128), string("levels in high 0.5 on a 0.25 fill (%d)", v));
		script_mask_set_effect(mask, lev_i, true, 0.5, 0); // Half opacity: mix(0.25, 0.5, 0.5)
	}
	else if (step == 7) {
		int v = center(slot_layer_mask_texture(mask));
		check(near(v, 96), string("filter opacity mixes with the result below (%d)", v));
		script_mask_remove_effect(mask, lev_i);
		script_mask_set_effect_param(mask, 1, 0, 0.5);
		script_mask_set_effect(mask, 1, true, 1.0, 1); // Fill 0.5 multiply
	}
	else if (step == 8) {
		int v = center(slot_layer_mask_texture(mask));
		check(near(v, 128), string("back to fill 0.5 multiply (%d)", v));
		dark = count_range(slot_layer_mask_texture(mask), 0, 0, 30);
		script_layer_set(mask);
		ctx->mask_value = 0.0; // Black stroke into the paint effect
		stroke(0.45);
	}
	else if (step == 9) {
		int d = count_range(slot_layer_mask_texture(mask), 0, 0, 30);
		check(d > dark + 500, string("stroke on the mask updates texmask (%d dark texels, %d before)", d, dark));
		int mid = count_range(slot_layer_mask_texture(mask), 0, 110, 145);
		check(mid > 1000, string("unpainted part keeps the fill (%d)", mid));
		ctx->mask_value = 1.0;
		dark            = script_layer_count();
		check(script_layer_apply_mask(mask) == layer, "apply mask");
	}
	else if (step == 10) {
		check(script_layer_count() == dark - 1, "mask is gone after apply");
		int half = count_range(layer->texpaint, 3, 110, 145);
		int none = count_range(layer->texpaint, 3, 0, 30);
		check(half > 1000, string("apply uses the evaluated stack: half alpha (%d)", half));
		check(none > 500, string("the stroke hides the layer (%d)", none));
		mask = script_layer_new_mask(layer, 1);
	}
	else if (step == 11) {
		inv_i = script_mask_add_effect(mask, 3, -1);
	}
	else if (step == 12) {
		check(mask->texmask != NULL && center(slot_layer_mask_texture(mask)) <= 3, "invert over white is black");
		script_mask_remove_effect(mask, inv_i);
	}
	else if (step == 13) {
		check(mask_stack_count(mask) == 1, "back to the paint effect alone");
		check(mask->texmask == NULL, "texmask freed once the stack is trivial");
		check(slot_layer_mask_texture(mask) == mask->texpaint, "trivial again reads texpaint");
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[mask_stack] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MASK STACK PASS" : "MASK STACK FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.4;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_brush_defaults();
	script_notify_on_next_frame(next);
}
