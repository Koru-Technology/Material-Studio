#include "global.h"

// Resolved dabs (paint_resolved_dabs, paint_exec.c): the same seeded strokes painted with the flag off and on agree within
// 1/255 on at least 99.9% of the channel values they paint. Each case paints a layer with the flag off, a new layer with
// it on, then compares base, normal and pack textures. Painted layers are hidden: the normals under the brush stay the mesh's.
// Cases: batched stamps, unbatched stamps (batching off), symmetry, a tip mask (Square brush, unbatched), a decal,
// a capsule leaving the mesh.

int fails  = 0;
int checks = 0;
int kase   = 0;
int frame  = 0;

slot_layer_t *blank = NULL;
slot_layer_t *la    = NULL;
slot_layer_t *lb    = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[dab_resolve] %s %s", ok ? "ok  " : "FAIL", name);
}

char *case_name(int k) {
	if (k == 0) {
		return "batched";
	}
	if (k == 1) {
		return "unbatched";
	}
	if (k == 2) {
		return "symmetry";
	}
	if (k == 3) {
		return "tip mask";
	}
	if (k == 4) {
		return "decal";
	}
	return "off mesh";
}

// The brush of a case, once for both passes: a brush picked applies over the next frames
void setup_brush(int k) {
	script_brush_defaults();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.3;
	ctx->brush_opacity = 1.0;
	ctx->brush_spacing = 0.05;
	ctx->sym_x         = k == 2;
	if (k == 3) {
		app_brush_use(12); // Square: a tip, no jitter
		ctx->brush_radius  = 0.3;
		ctx->brush_spacing = 0.05;
	}
	context_select_tool(k == 4 ? TOOL_TYPE_DECAL : TOOL_TYPE_BRUSH);
}

// A pass paints a new layer, both passes of a case in the same frame: the camera jitters by frame (TAA)
void setup(int k, int on) {
	app_layer_new(0);
	script_get_config()->paint_resolved_dabs = on;
	make_paint_set_batching(k != 1 && k != 3); // Parses the paint material again, with the flag
	render_path_paint_set_stamp_seed(4242);
}

void paint_strokes(int k) {
	if (k == 4) {
		script_paint(0.45, 0.5);
		script_paint_end();
		script_paint(0.58, 0.42);
		script_paint_end();
		return;
	}
	if (k == 5) { // Across the edge of the mesh, from its middle to past it
		for (int i = 0; i < 30; ++i) {
			script_paint(0.5 + i * 0.02, 0.5 + i * 0.004);
		}
		script_paint_end();
		return;
	}
	for (int i = 0; i < 30; ++i) {
		script_paint(0.35 + i * 0.01, 0.45 + (i % 7) * 0.004);
	}
	script_paint_end();
	for (int i = 0; i < 20; ++i) {
		script_paint(0.42 + i * 0.006, 0.38 + i * 0.012);
	}
	script_paint_end();
}

// Paints the strokes, then hides the layer: decals, tips and angle reject read the normal under the brush, painted
// layers change it for the passes after
void paint(int k) {
	paint_strokes(k);
	app_layer_set_visible(script_get_context()->layer->id, 0);
}

int diff(gpu_texture_t *a, gpu_texture_t *b) {
	return script_diff_textures(a, b, 1.0);
}

void compare(int k) {
	int painted = diff(la->texpaint, blank->texpaint) + diff(la->texpaint_nor, blank->texpaint_nor) + diff(la->texpaint_pack, blank->texpaint_pack);
	int over    = diff(la->texpaint, lb->texpaint);
	float max   = script_diff_max();
	over        = over + diff(la->texpaint_nor, lb->texpaint_nor);
	if (script_diff_max() > max) {
		max = script_diff_max();
	}
	over = over + diff(la->texpaint_pack, lb->texpaint_pack);
	if (script_diff_max() > max) {
		max = script_diff_max();
	}
	float frac = painted > 0 ? over * 100.0 / painted : 100.0;
	check(painted > 2000, string("%s: the strokes paint (%d channel values)", case_name(k), painted));
	check(over * 1000 <= painted, string("%s: off and on agree within 1/255 (%d of %d differ, %.4f percent, max %.1f/255)", case_name(k), over, painted, frac, max));
}

void next() {
	frame = frame + 1;
	if (frame == 2) {
		setup_brush(kase);
	}
	else if (frame == 8) {
		setup(kase, 0); // A first stroke leaves state the next ones start from (the stroke angle), painted over
		paint(kase);
		setup(kase, 0);
		la = script_get_context()->layer;
		paint(kase);
		setup(kase, 1);
		lb = script_get_context()->layer;
		paint(kase);
	}
	else if (frame == 12) {
		compare(kase);
		kase  = kase + 1;
		frame = 0;
		if (kase == 6) {
			script_get_config()->paint_resolved_dabs = 0;
			check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
			printf("[dab_resolve] %d checks, %d failures", checks, fails);
			printf(fails == 0 ? "DAB_RESOLVE PASS" : "DAB_RESOLVE FAIL");
			script_quit();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	slot_material_t *m   = script_material_create("dab_resolve_blue");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	app_layer_new(0);
	blank = script_get_context()->layer; // A new layer, left empty
	script_notify_on_next_frame(next);
}
