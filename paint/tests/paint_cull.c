#include "global.h"

// Paint culling: on a dense mesh a stroke draws only the chunks near the brush, and paints the same texels as the whole mesh.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

buffer_t *before = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[cull] %s %s", ok ? "ok  " : "FAIL", name);
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

int changed_texels() {
	slot_layer_t *l = script_get_context()->layer;
	buffer_t     *p = script_get_texture_pixels(l->texpaint);
	int           n = 0;
	for (int i = 0; i < p->length; i += 16) {
		int d = buffer_get_u8(p, i) - buffer_get_u8(before, i);
		int e = buffer_get_u8(p, i + 2) - buffer_get_u8(before, i + 2);
		if (d > 8 || d < -8 || e > 8 || e < -8) {
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

int iabs(int v) {
	return v < 0 ? -v : v;
}

int counts[4];
float kept[4];

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;
	context_t *ctx = script_get_context();
	if (step == 0) {
		any_array_t *objects = util_mesh_get_hierarchy(ctx->paint_object);
		for (int i = 0; i < 5; ++i) {
			util_mesh_subdivide(objects);
		}
		step = 1;
	}
	else if (step >= 1 && step <= 4) {
		// Strokes 1 and 3 culled, 2 and 4 the whole mesh, the same place each pair
		int culled = step % 2 == 1;
		paint_cull_set_enabled(culled);
		if (step == 3) {
			ctx->brush_radius = 0.15; // Small brush, few chunks
		}
		before = copy_layer();
		stroke(step <= 2 ? 0.45 : 0.55);
		counts[step - 1] = changed_texels();
		kept[step - 1]   = paint_cull_last_kept();
		printf("[cull] stroke %d culled %d: %d texels, kept %.3f of the mesh", step, culled, counts[step - 1], kept[step - 1]);
		if (step % 2 == 1) {
			history_undo(); // The same stroke again without culling
		}
		step = step + 1;
	}
	else if (step == 5) {
		paint_cull_set_enabled(1);
		check(counts[0] > 500, "culled stroke paints");
		check(iabs(counts[0] - counts[1]) * 50 <= counts[1], string("culled and whole mesh strokes paint the same (%d, %d)", counts[0], counts[1]));
		check(iabs(counts[2] - counts[3]) * 50 <= counts[3], string("small brush the same (%d, %d)", counts[2], counts[3]));
		check(kept[2] < 0.5, string("small brush draws part of the mesh (%.3f)", kept[2]));
		printf("[cull] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "CULL PASS" : "CULL FAIL");
		script_quit();
		return;
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	slot_material_t *m   = script_material_create("cull_blue");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.4;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
