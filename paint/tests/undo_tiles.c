#include "global.h"

// Tiled undo: undo and redo bring the layer back byte for byte, 30 strokes in a row all undo, a clear layer step in between
// (a whole layer copy) still undoes.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

buffer_t *state0 = NULL; // Before painting
buffer_t *state1 = NULL; // After stroke 1
buffer_t *state2 = NULL; // After stroke 2

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[undo] %s %s", ok ? "ok  " : "FAIL", name);
}

buffer_t *copy_layer() {
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	buffer_t     *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

// Bytes that differ from the copy
int diff_layer(buffer_t *b) {
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	int           n = 0;
	for (int i = 0; i < p->length; ++i) {
		if (buffer_get_u8(p, i) != buffer_get_u8(b, i)) {
			n = n + 1;
		}
	}
	return n;
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 12; ++i) {
		float t = i / 11.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
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
		state0 = copy_layer();
		stroke(0.35, 0.45, 0.6, 0.5);
		state1 = copy_layer();
		stroke(0.45, 0.35, 0.5, 0.65);
		state2 = copy_layer();
		check(diff_layer(state0) > 1000, "strokes paint");
		history_undo();
		step = 1;
	}
	else if (step == 1) {
		check(diff_layer(state1) == 0, string("undo brings back the layer after stroke 1 (%d bytes differ)", diff_layer(state1)));
		history_undo();
		step = 2;
	}
	else if (step == 2) {
		check(diff_layer(state0) == 0, string("second undo brings back the unpainted layer (%d bytes differ)", diff_layer(state0)));
		history_redo();
		step = 3;
	}
	else if (step == 3) {
		check(diff_layer(state1) == 0, string("redo brings back stroke 1 (%d bytes differ)", diff_layer(state1)));
		history_redo();
		step = 4;
	}
	else if (step == 4) {
		check(diff_layer(state2) == 0, string("second redo brings back stroke 2 (%d bytes differ)", diff_layer(state2)));
		// 30 strokes, then all undone
		for (int i = 0; i < 30; ++i) {
			float y = 0.3 + i * 0.013;
			stroke(0.3, y, 0.7, y + 0.02);
		}
		step = 5;
	}
	else if (step == 5) {
		for (int i = 0; i < 30; ++i) {
			history_undo();
		}
		step = 6;
	}
	else if (step == 6) {
		check(diff_layer(state2) == 0, string("30 strokes all undo (%d bytes differ)", diff_layer(state2)));
		// A clear layer step between strokes keeps a whole copy, undoing across it still works
		stroke(0.3, 0.6, 0.7, 0.6);
		script_layer_clear(script_get_context()->layer);
		step = 7;
	}
	else if (step == 7) {
		stroke(0.35, 0.4, 0.65, 0.42);
		history_undo(); // The stroke
		history_undo(); // The clear
		history_undo(); // The stroke before
		step = 8;
	}
	else if (step == 8) {
		check(diff_layer(state2) == 0, string("undo across a cleared layer (%d bytes differ)", diff_layer(state2)));
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[undo] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "UNDO PASS" : "UNDO FAIL");
		script_quit();
		return;
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults(); // A plain round brush, whatever the user left picked
	script_project_new();
	slot_material_t *m   = script_material_create("undo_blue");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.3;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
