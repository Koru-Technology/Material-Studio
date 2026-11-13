#include "global.h"

// ID mask: only a real ID map is accepted, picked parts become a mask of the layer, undo and redo restore it

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

int           layers = 0;
slot_layer_t *base   = NULL;
slot_layer_t *mask   = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[id_mask] %s %s", ok ? "ok  " : "FAIL", name);
}

// 256x256 image, f picks the color of each texel
void write_image(char *path, int gradient) {
	int       w = 256;
	buffer_t *b = buffer_create(w * w * 4);
	for (int y = 0; y < w; ++y) {
		for (int x = 0; x < w; ++x) {
			int i = (y * w + x) * 4;
			int r = 0;
			int g = 0;
			int bl = 0;
			if (gradient) {
				r  = x;
				g  = y;
				bl = (x + y) / 2;
			}
			else if (x < w / 2 && y < w / 2) {
				r = 255; // Top left red
			}
			else if (x >= w / 2 && y < w / 2) {
				g = 255; // Top right green
			}
			else if (x < w / 2) {
				bl = 255; // Bottom left blue
			}
			else {
				r = 255; // Bottom right yellow
				g = 255;
			}
			buffer_set_u8(b, i, r);
			buffer_set_u8(b, i + 1, g);
			buffer_set_u8(b, i + 2, bl);
			buffer_set_u8(b, i + 3, 255);
		}
	}
	iron_write_png(path, b, w, w, 0);
}

// White texels of the mask in the left and right half
int left_white  = 0;
int right_white = 0;
int total       = 0;
void count_halves() {
	buffer_t *p = script_get_texture_pixels(mask->texpaint);
	left_white  = 0;
	right_white = 0;
	total       = p->length / 4;
	int w       = 1; // Layers are square, the width is not exposed to scripts
	while (w * w < total) {
		w = w * 2;
	}
	for (int i = 0; i < p->length; i += 4) {
		int r = buffer_get_u8(p, i);
		int b = buffer_get_u8(p, i + 2);
		int v = r > b ? r : b;
		if (v >= 200) {
			int x = (i / 4) % w;
			if (x < w / 2) {
				left_white = left_white + 1;
			}
			else {
				right_white = right_white + 1;
			}
		}
	}
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
		char *id_map   = string("%s/koru_test_id_map.png", script_save_path());
		char *gradient = string("%s/koru_test_gradient.png", script_save_path());
		write_image(id_map, 0);
		write_image(gradient, 1);
		script_import_asset(id_map, 0);
		script_import_asset(gradient, 0);
	}
	else if (step == 1) {
		int gi = script_asset_index("koru_test_gradient.png");
		int ii = script_asset_index("koru_test_id_map.png");
		check(gi >= 0 && ii >= 0, "both images imported");
		check(script_set_id_map(gi) == 0, "a gradient is not accepted as ID map");
		check(script_set_id_map(ii) == 1, "flat colors are accepted as ID map");
		base   = ctx->layer;
		layers = script_layer_count();
		ui_id_mask_begin();
	}
	else if (step == 2) {
		check(ctx->tool == TOOL_TYPE_COLORID, "picking shows the ID map");
		check(ui_id_mask_selected() == 0, "nothing picked yet");
		ui_id_mask_toggle(0xff0000);
		ui_id_mask_toggle(0x0000ff);
		ui_id_mask_toggle(0x00ff00);
		ui_id_mask_toggle(0x00ff00); // Picked twice is removed
		check(ui_id_mask_selected() == 2, "tapping a picked part again removes it");
	}
	else if (step == 3) {
		ui_id_mask_done();
	}
	else if (step == 4) {
		mask = ctx->layer;
		check(script_layer_count() == layers + 1, "done adds one mask");
		check(mask != base && mask->parent == base, "the mask belongs to the layer");
		check(ctx->tool == TOOL_TYPE_BRUSH, "done goes back to the brush");
		count_halves();
		check(left_white > total * 45 / 100, string("red and blue parts are white (%d of %d)", left_white, total / 2));
		check(right_white == 0, string("green and yellow parts stay black (%d)", right_white));
		history_undo();
	}
	else if (step == 5) {
		count_halves();
		check(left_white == 0, "undo takes the picked parts back");
		history_undo();
	}
	else if (step == 6) {
		check(script_layer_count() == layers, "undo removes the mask");
		history_redo();
	}
	else if (step == 7) {
		history_redo();
	}
	else if (step == 8) {
		mask = ctx->layer;
		check(script_layer_count() == layers + 1, "redo adds the mask back");
		count_halves();
		check(left_white > total * 45 / 100 && right_white == 0, "redo brings the picked parts back");
		ui_id_mask_begin();
	}
	else if (step == 9) {
		ui_id_mask_cancel();
	}
	else if (step == 10) {
		check(script_layer_count() == layers + 1, "cancel adds nothing");
		check(ctx->tool != TOOL_TYPE_COLORID, "cancel leaves the ID map view");
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[id_mask] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "ID_MASK PASS" : "ID_MASK FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
