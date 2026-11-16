#include "global.h"

// Apply mask and merge down replay from their timelines: the op re-rendered over the state before it is byte equal to
// what the live op made, neither is a floor, and both render at twice the resolution.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *a = NULL;
slot_layer_t *b = NULL;
slot_layer_t *m = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[tl_op_replay] %s %s", ok ? "ok  " : "FAIL", name);
}

char *layer_hash(slot_layer_t *l) {
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

char *out_hash() {
	return string("%s %s %s", script_texture_hash(timeline_test_out(0)), script_texture_hash(timeline_test_out(1)), script_texture_hash(timeline_test_out(2)));
}

void stroke(slot_layer_t *l, float x, float y) {
	script_layer_set(l);
	for (int k = 0; k < 8; ++k) {
		float t = k / 7.0;
		script_paint(x + 0.3 * t, y + 0.1 * t);
	}
	script_paint_end();
}

// The last entry of l is kind, its replay gives l's pixels, the layer stays upscalable
void check_last(slot_layer_t *l, int kind, char *name) {
	timeline_finalize();
	int n = timeline_test_count(l);
	check(n > 0 && timeline_test_kind(l, n - 1) == kind, string("%s: the last entry (kind %d)", name, n > 0 ? timeline_test_kind(l, n - 1) : -1));
	check(timeline_test_floor(l) < n, string("%s: no floor (floor %d of %d)", name, timeline_test_floor(l), n));
	char *live = layer_hash(l);
	check(timeline_test_replay_entry(l, n - 1) && string_equals(out_hash(), live), string("%s: replay equals live", name));
	int res = config_get_texture_res_x() * 2;
	check(timeline_test_render(l, -1, res, res, 0.0, 0.0, 1.0, 1.0), string("%s: renders at %d", name, res));
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;
	if (step == 0) {
		a = script_layer_new();
		b = script_layer_new();
		stroke(a, 0.2, 0.3);
		stroke(b, 0.3, 0.5);
		m = script_layer_new_mask(b, 1);
	}
	else if (step == 1) {
		stroke(m, 0.25, 0.45);
	}
	else if (step == 2) {
		script_layer_apply_mask(m);
	}
	else if (step == 3) {
		check_last(b, 9, "apply mask");
		b->mask_opacity = 0.7; // Merged at less than full opacity
		script_layer_set(b);
		script_layer_merge_down(b);
	}
	else if (step == 4) {
		check_last(a, 10, "merge down");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[tl_op_replay] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "TL_OP_REPLAY PASS" : "TL_OP_REPLAY FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	slot_material_t *mat = script_material_create("replay_green");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.8, 0.3, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.12;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
