#include "global.h"

// Paints with raw mouse events, through the same per frame path as a finger, pencil or mouse stroke.
// Stroke 1 right away, stroke 2 after an idle wait (the live layer is released after 5 s idle).

int frame  = 0;
int phase  = 0;
int stroke = 0;
int fails  = 0;

buffer_t *before = NULL;

buffer_t *copy_pixels() {
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	buffer_t     *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

int minx = 0;
int maxx = 0;
int miny = 0;
int maxy = 0;

// Texels changed since the copy, every 16th texel sampled, and their x extent in the texture
int changed_texels() {
	minx = 99999;
	maxx = -1;
	miny = 99999;
	maxy = -1;
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	int           n = 0;
	int           w = 1;
	while (w * w * 4 < p->length) {
		w = w * 2;
	}
	for (int i = 0; i < p->length; i += 64) {
		int d = buffer_get_u8(p, i) - buffer_get_u8(before, i);
		int e = buffer_get_u8(p, i + 2) - buffer_get_u8(before, i + 2);
		if (d > 8 || d < -8 || e > 8 || e < -8) {
			n     = n + 1;
			int x = (i / 4) % w;
			if (x < minx) {
				minx = x;
			}
			if (x > maxx) {
				maxx = x;
			}
			int y = (i / 4) / w;
			if (y < miny) {
				miny = y;
			}
			if (y > maxy) {
				maxy = y;
			}
		}
	}
	return n;
}

void next() {
	frame = frame + 1;
	int cx = sys_x() + sys_w() / 2;
	int cy = sys_h() / 2;
	int y  = cy + stroke * 40 - 60;
	if (phase == 0) { // Settle, or idle before the second stroke
		if (frame > (stroke % 2 == 0 ? 10 : 600)) {
			phase  = 1;
			frame  = 0;
			before = copy_pixels();
			script_mouse_press(cx - 150, y, 0);
		}
	}
	else if (phase == 1) { // Drag
		script_mouse_move(cx - 150 + frame * 10, y);
		if (frame >= 30) {
			script_mouse_release(cx - 150 + frame * 10, y, 0);
			phase = 2;
			frame = 0;
		}
	}
	else if (phase == 2) {
		if (frame > 10) {
			int n  = changed_texels();
			int ok = n > 200 && (maxx - minx) + (maxy - miny) > 150;
			if (!ok) {
				fails = fails + 1;
			}
			printf("[live] stroke %d: %d texels changed, x %d - %d, y %d - %d %s", stroke, n, minx, maxx, miny, maxy, ok ? "ok" : "FAIL");
			slot_layer_t *l = script_get_context()->layer;
			buffer_t     *px = script_get_texture_pixels(l->texpaint);
			int           w  = 1;
			while (w * w * 4 < px->length) {
				w = w * 2;
			}
			iron_write_png(string("%s/live_%d.png", script_save_path(), stroke), px, w, w, 0);
			stroke     = stroke + 1;
			phase      = 0;
			frame      = 0;
			if (stroke == 4) {
				printf(fails == 0 ? "LIVE PASS" : "LIVE FAIL");
				return;
			}
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	slot_material_t *m   = script_material_create("live_blue");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.5;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	printf("[live] view %d %d %d, batched %d", sys_x(), sys_w(), sys_h(), make_paint_is_batched());
	script_notify_on_next_frame(next);
}
