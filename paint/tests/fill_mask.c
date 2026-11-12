#include "global.h"

// Fill layer + mask: strokes on a fill layer go into its mask as a gray value, the layer keeps its own material settings

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_material_t *mat_checker = NULL;
slot_material_t *mat_red     = NULL;
slot_layer_t    *fill        = NULL;
slot_layer_t    *mask        = NULL;
buffer_t        *before      = NULL;
int              layers      = 0;
int              lit         = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[fill] %s %s", ok ? "ok  " : "FAIL", name);
}

buffer_t *copy_pixels(slot_layer_t *l) {
	buffer_t *p = script_get_texture_pixels(l->texpaint);
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

int diff_pixels(slot_layer_t *l, buffer_t *b) {
	buffer_t *p = script_get_texture_pixels(l->texpaint);
	int       n = 0;
	for (int i = 0; i < p->length; i += 4) {
		int d = buffer_get_u8(p, i) - buffer_get_u8(b, i);
		int e = buffer_get_u8(p, i + 2) - buffer_get_u8(b, i + 2);
		if (d > 8 || d < -8 || e > 8 || e < -8) {
			n = n + 1;
		}
	}
	return n;
}

// Mask texels at least min (red is byte 0 or 2 depending on the backend, the other one stays 0)
int mask_count(int min) {
	buffer_t *p = script_get_texture_pixels(mask->texpaint);
	int       n = 0;
	for (int i = 0; i < p->length; i += 4) {
		int r = buffer_get_u8(p, i);
		int b = buffer_get_u8(p, i + 2);
		int v = r > b ? r : b;
		if (v >= min) {
			n = n + 1;
		}
	}
	return n;
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
		script_material_set(mat_checker);
		fill = script_layer_new_fill();
	}
	else if (step == 1) {
		check(fill != NULL && fill->fill_material == mat_checker, "fill layer holds the material");
		layers = script_layer_count();
		stroke(0.45);
	}
	else if (step == 2) {
		mask = ctx->layer;
		check(mask != fill && mask->parent == fill && mask->texpaint_nor == NULL, "stroke on the fill layer selects a new mask");
		check(script_layer_count() == layers + 1, "one mask was added");
		lit       = mask_count(9);
		int white = mask_count(240);
		check(lit > 500, string("stroke painted the mask (%d)", lit));
		check(white * 10 >= lit * 8, string("mask is painted white, not the material (%d of %d)", white, lit));
		history_undo();
	}
	else if (step == 3) {
		check(mask_count(9) == 0, "undo removes the stroke");
		history_redo();
	}
	else if (step == 4) {
		check(mask_count(9) * 10 >= lit * 9, "redo brings the stroke back");
		ctx->mask_value = 0.0; // Paints black over the same place
		stroke(0.45);
	}
	else if (step == 5) {
		int left = mask_count(9);
		check(left * 4 < lit, string("value 0 hides again (%d of %d left)", left, lit));
		ctx->mask_value = 1.0;
		script_layer_set(fill); // Back to the fill layer, the next stroke reuses its mask
		stroke(0.55);
	}
	else if (step == 6) {
		check(ctx->layer == mask, "stroke on the fill layer reuses its mask");
		check(script_layer_count() == layers + 1, "no second mask");
		before      = copy_pixels(fill);
		fill->scale = 3.0;
		script_layer_update(fill);
	}
	else if (step == 7) {
		int d = diff_pixels(fill, before);
		check(d > 1000, string("uv scale changes the fill layer (%d)", d));
		before      = copy_pixels(fill);
		fill->angle = 45.0;
		script_layer_update(fill);
	}
	else if (step == 8) {
		int d = diff_pixels(fill, before);
		check(d > 1000, string("angle changes the fill layer (%d)", d));
		before = copy_pixels(fill);
		script_material_set(mat_red);
		check(script_layer_to_fill(fill), "material swap accepted");
	}
	else if (step == 9) {
		check(fill->fill_material == mat_red, "fill layer has the new material");
		check(fill->scale == 3.0 && fill->angle == 45.0, "swap keeps the layer's uv settings");
		int d = diff_pixels(fill, before);
		check(d > 1000, string("swap repaints the layer (%d)", d));
		history_undo();
	}
	else if (step == 10) {
		check(fill->fill_material == mat_checker, "undo restores the material");
		int d = diff_pixels(fill, before);
		check(d < 100, string("undo repaints the old material (%d)", d));
	}
	else {
		printf("[fill] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "FILL PASS" : "FILL FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();

	mat_red      = script_material_create("fill_red");
	ui_node_t *r = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(r, 0, 0, 0.9, 0.1, 0.1, 1.0);
	script_material_connect(r, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();

	mat_checker  = script_material_create("fill_checker");
	ui_node_t *c = script_material_create_node_at("TEX_CHECKER", -300.0, 0.0);
	ui_node_t *t = script_material_create_node_at("TEX_COORD", -600.0, 0.0); // Uv coordinates, uv scale and angle apply
	script_material_connect(t, 2, c, 0);
	script_material_set_float(c, 1, 3, 8.0); // Scale
	script_material_connect(c, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();

	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.4;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	// Hard, full strength stamps whatever the brush settings of the last session were
	ctx->brush_hardness       = 1.0;
	ctx->brush_flow           = 1.0;
	ctx->brush_spacing        = 0.0;
	ctx->brush_scatter        = 0.0;
	ctx->brush_jitter_size    = 0.0;
	ctx->brush_jitter_opacity = 0.0;
	ctx->brush_smoothing      = 0.0;
	script_notify_on_next_frame(next);
}
