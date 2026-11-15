#include "global.h"

// Mask stack rows in the layers panel: effects list under their mask, the selected one shows its parameters

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

char *shot      = "/tmp/mask_stack_ui.png";
char *shot_fill = "/tmp/mask_stack_ui_fill.png";

slot_layer_t *layer = NULL;
slot_layer_t *mask  = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mask_ui] %s %s", ok ? "ok  " : "FAIL", name);
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
		layer = script_layer_new();
		mask  = script_layer_new_mask(layer, 0);
		check(mask != NULL && mask->parent == layer, "mask on a new layer");
		check(ctx->mask_effect == -1, "no effect selected");
	}
	else if (step == 1) {
		check(script_mask_add_effect(mask, 1, -1) == 1, "fill added on top");
		check(script_mask_add_effect(mask, 2, -1) == 2, "levels added on top");
		check(script_mask_add_effect(mask, 3, -1) == 3, "invert added on top");
		check(mask_stack_count(mask) == 4, "four effects");
		script_mask_set_effect(mask, 1, true, 0.5, 1); // Fill: multiply at half
		script_mask_set_effect_param(mask, 1, 0, 0.75);
		script_mask_set_effect(mask, 3, false, 1.0, 0); // Invert hidden
		script_layer_set(layer);
	}
	else if (step == 2) {
		check(ctx->layer == layer, "layer selected");
		script_layer_set(mask);
		ctx->mask_effect = 2; // Levels, its parameters show under its row
	}
	else if (step == 3) {
		check(ctx->layer == mask && ctx->mask_effect == 2, "levels stays selected while the panel draws");
		stroke(0.45); // Brushes keep painting the paint effect
	}
	else if (step == 4) {
		check(ctx->layer == mask, "stroke keeps the mask selected");
		check(mask_stack_count(mask) == 4, "stroke leaves the stack alone");
		script_screenshot(shot);
	}
	else if (step < 21 && (step == 20 || iron_file_exists(shot))) {
		step = 20;
		check(iron_file_exists(shot), "screenshot saved");
		script_layer_set(layer); // Selecting again redraws the panel
	}
	else if (step < 21) {
		frame = 0; // Waiting for the screenshot
	}
	else if (step == 21) {
		script_layer_set(mask);
		ctx->mask_effect = 1; // Fill: opacity, blending and value
	}
	else if (step == 22) {
		script_screenshot(shot_fill);
	}
	else if (step < 41 && (step == 40 || iron_file_exists(shot_fill))) {
		step = 40;
		check(script_mask_remove_effect(mask, 2), "levels removed");
		check(mask_stack_count(mask) == 3, "three effects");
		// The selection points past the end now, rows must not read it
		ctx->mask_effect = 7;
	}
	else if (step < 41) {
		frame = 0; // Waiting for the screenshot
	}
	else if (step == 41) {
		script_layer_set(layer); // Selection goes with the layer
		check(ctx->mask_effect == -1, "changing layer clears the effect selection");
		mask->show_panel = false; // Collapsed: no effect rows
	}
	else if (step == 42) {
		mask->show_panel = true;
		script_layer_set(mask);
		ctx->mask_effect = 0;
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[mask_ui] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MASK_UI PASS" : "MASK_UI FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	char *save = script_save_path();
	if (string_index_of(save, "/var/mobile/") >= 0) { // ios, /tmp is outside the sandbox
		shot      = string("%s/mask_stack_ui.png", save);
		shot_fill = string("%s/mask_stack_ui_fill.png", save);
	}
	if (iron_file_exists(shot)) {
		iron_delete_file(shot);
	}
	if (iron_file_exists(shot_fill)) {
		iron_delete_file(shot_fill);
	}
	script_brush_defaults();
	script_project_new();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.4;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
