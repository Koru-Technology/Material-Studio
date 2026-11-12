#include "global.h"

// Seam fill: after a live stroke across the cube's uv seams, filling the seams again changes nothing (it ran already)

int frame = 0;
int step  = 0;
int fails = 0;
int round = 0; // 0 the layer, 1 a mask on it

buffer_t *copy_tex(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

int diff_tex(gpu_texture_t *t, buffer_t *b) {
	buffer_t *p = script_get_texture_pixels(t);
	int       n = 0;
	for (int i = 0; i < p->length; ++i) {
		if (buffer_get_u8(p, i) != buffer_get_u8(b, i)) {
			n = n + 1;
		}
	}
	return n;
}

void next() {
	frame = frame + 1;
	int cx = sys_x() + sys_w() / 2;
	int cy = sys_h() / 2;
	if (step == 0) {
		if (frame > 10) {
			step  = 1;
			frame = 0;
			script_mouse_press(cx - 400, cy - 100, 0);
		}
	}
	else if (step == 1) { // Across the visible faces, over their seams
		script_mouse_move(cx - 400 + frame * 20, cy - 100 + frame * 5);
		if (frame >= 40) {
			script_mouse_release(cx - 400 + frame * 20, cy - 100 + frame * 5, 0);
			step  = 2;
			frame = 0;
		}
	}
	else if (step == 2) {
		if (frame > 30) {
			slot_layer_t *l    = script_get_context()->layer;
			buffer_t     *base = copy_tex(l->texpaint);
			render_path_paint_dilate(1, 1);
			int db = diff_tex(l->texpaint, base);
			int dn = 0;
			bool ok = db == 0;
			if (!ok) {
				fails = fails + 1;
			}
			printf("[seam] %s %s: filling the seams again changes %d bytes", ok ? "ok  " : "FAIL", round == 0 ? "layer" : "mask", db);
			if (round == 1) {
				printf(fails == 0 ? "SEAM PASS" : "SEAM FAIL");
				script_quit();
				return;
			}
			// Again on a white mask, the eraser takes it away across the seams
			round           = 1;
			slot_layer_t *m = script_layer_new_mask(l, 1);
			script_layer_set(m);
			context_select_tool(TOOL_TYPE_ERASER);
			step  = 0;
			frame = 0;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults(); // A plain round brush, whatever the user left picked
	script_project_new();
	slot_material_t *m   = script_material_create("seam_peach");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.9, 0.6, 0.5, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	script_layer_new();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.6;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
