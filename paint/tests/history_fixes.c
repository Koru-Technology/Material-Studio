#include "global.h"

// History fixes: image layer and image mask redo draw the image again, layers brought back by redo keep their uid, a
// clear undoes.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

int           n0      = 0;
int           texels  = 0;
int           uid     = 0;
int           painted = 0;
slot_layer_t *a       = NULL;
slot_layer_t *m       = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[history_fixes] %s %s", ok ? "ok  " : "FAIL", name);
}

char *test_path(char *name) {
	return string("%s/%s", script_save_path(), name);
}

// Texels with alpha above 128
int opaque(slot_layer_t *l) {
	buffer_t *p = script_get_texture_pixels(l->texpaint);
	int       n = 0;
	for (int i = 0; i < p->length; i += 4) {
		if (buffer_get_u8(p, i + 3) > 128) {
			n = n + 1;
		}
	}
	return n;
}

// Mask bytes above 128
int mask_set(slot_layer_t *l) {
	buffer_t *p = script_get_texture_pixels(l->texpaint);
	int       n = 0;
	for (int i = 0; i < p->length; ++i) {
		if (buffer_get_u8(p, i) > 128) {
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
	frame          = 0;
	context_t *ctx = script_get_context();

	if (step == 0) {
		int       w   = 32;
		buffer_t *buf = buffer_create(w * w * 4);
		for (int i = 0; i < w * w; ++i) {
			buffer_set_u8(buf, i * 4, 255);
			buffer_set_u8(buf, i * 4 + 3, 255);
		}
		iron_write_png(test_path("koru_history_fixes.png"), buf, w, w, 0);
		script_import_asset(test_path("koru_history_fixes.png"), 0);
	}
	else if (step == 1) {
		n0 = script_layer_count();
		layers_create_image_at(script_asset_index("koru_history_fixes.png"), 0);
	}
	else if (step == 2) {
		texels = opaque(ctx->layer);
		uid    = ctx->layer->uid;
		check(script_layer_count() == n0 + 1 && texels > 1000, "image layer created with the image");
		history_undo();
	}
	else if (step == 3) {
		check(script_layer_count() == n0, "undo removes the image layer");
		history_redo();
	}
	else if (step == 4) {
		check(script_layer_count() == n0 + 1, "redo brings the image layer back");
		check(opaque(ctx->layer) == texels, string("redo draws the image again (%d of %d texels)", opaque(ctx->layer), texels));
		check(ctx->layer->uid == uid, "the layer keeps its uid");
		// Image mask on a paint layer
		a  = script_layer_new();
		n0 = script_layer_count();
		layers_create_image_at(script_asset_index("koru_history_fixes.png"), 1);
	}
	else if (step == 5) {
		m      = ctx->layer;
		texels = mask_set(m);
		uid    = m->uid;
		check(script_layer_count() == n0 + 1 && m->parent == a && texels > 1000, "image mask created on the layer");
		history_undo();
	}
	else if (step == 6) {
		check(script_layer_count() == n0 && script_layer_index(a) >= 0, "undo removes the mask, not its layer");
		history_redo();
	}
	else if (step == 7) {
		m = ctx->layer;
		check(script_layer_count() == n0 + 1 && m->parent == a, "redo brings the mask back on its layer");
		check(mask_set(m) == texels, string("redo draws the image into the mask again (%d of %d)", mask_set(m), texels));
		check(m->uid == uid, "the mask keeps its uid");
		// A stroke on a layer that is removed and brought back
		a   = script_layer_new();
		uid = a->uid;
		stroke(0.35, 0.45, 0.6, 0.5);
		painted = opaque(a);
		history_undo(); // The stroke
		history_undo(); // The layer
	}
	else if (step == 8) {
		history_redo();
		history_redo();
	}
	else if (step == 9) {
		a = ctx->layer;
		check(a->uid == uid && painted > 100 && opaque(a) == painted, string("redo of a layer and its stroke (%d of %d)", opaque(a), painted));
		texels = opaque(a);
		script_layer_clear(a);
	}
	else if (step == 10) {
		check(opaque(a) == 0, "layer cleared");
		history_undo();
	}
	else if (step == 11) {
		check(opaque(a) == texels, "undo of a clear");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[history_fixes] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "HISTORY_FIXES PASS" : "HISTORY_FIXES FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	slot_material_t *mat = script_material_create("fixes_blue");
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
