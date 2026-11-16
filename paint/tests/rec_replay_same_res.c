#include "global.h"

// Recorded strokes replay byte-equal at the resolution they were painted at (paint_rec.c, paint_replay.c). Each case paints
// a stroke on a new layer with resolved dabs and timelines on, notes the hash of the layer's textures, then restores the
// state before the stroke and replays its record into the layer: the hashes must match, no replay may fall back.
// Cases: batched stamps, unbatched stamps, flow below 1, jitter (seeded), symmetry, a continuous brush, the eraser over a
// stroke, a stroke through the per frame path (mouse events, tail and in-stroke dilation), a mask. Then a stroke replays
// the same after the project is saved (project_v31) and opened again.

int fails  = 0;
int checks = 0;
int kase   = 0;
int frame  = 0;

char *h0 = NULL;
char *h1 = NULL;
char *h2 = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[rec_replay] %s %s", ok ? "ok  " : "FAIL", name);
}

char *case_name(int k) {
	if (k == 0) {
		return "batched";
	}
	if (k == 1) {
		return "unbatched";
	}
	if (k == 2) {
		return "flow 0.4";
	}
	if (k == 3) {
		return "jitter";
	}
	if (k == 4) {
		return "symmetry";
	}
	if (k == 5) {
		return "continuous";
	}
	if (k == 6) {
		return "eraser";
	}
	if (k == 7) {
		return "live stroke";
	}
	if (k == 8) {
		return "mask";
	}
	return "saved and opened";
}

int   uid   = 0;
char *dir   = "/tmp/materialstudio_rec_replay";
int   v31   = 0;

char *hash_of(gpu_texture_t *t) {
	return t != NULL ? script_texture_hash(t) : "";
}

slot_layer_t *find_uid(int u) {
	for (int i = 0; i < script_layer_count(); ++i) {
		if (script_layer_at(i)->uid == u) {
			return script_layer_at(i);
		}
	}
	return NULL;
}

void setup(int k) {
	script_brush_defaults();
	context_t *ctx            = script_get_context();
	ctx->brush_radius         = 0.3;
	ctx->brush_opacity        = 1.0;
	ctx->brush_spacing        = k == 5 ? 0.0 : 0.05;
	ctx->brush_flow           = k == 2 ? 0.4 : 1.0;
	ctx->brush_jitter_lateral = k == 3 ? 0.5 : 0.0;
	ctx->brush_jitter_size    = k == 3 ? 0.4 : 0.0;
	ctx->brush_jitter_opacity = k == 3 ? 0.3 : 0.0;
	ctx->sym_x                = k == 4;
	context_select_tool(TOOL_TYPE_BRUSH);
	app_layer_new(0);
	if (k == 8) {
		script_layer_set(script_layer_new_mask(script_get_context()->layer, 0)); // Black, the stroke reveals
	}
	make_paint_set_batching(k != 1); // Parses the paint material again
	render_path_paint_set_stamp_seed(4242);
}

void stroke(float y) {
	for (int i = 0; i < 30; ++i) {
		script_paint(0.35 + i * 0.01, y + (i % 7) * 0.004);
	}
	script_paint_end();
}

void note() {
	slot_layer_t *l = script_get_context()->layer;
	h0              = hash_of(l->texpaint);
	h1              = hash_of(l->texpaint_nor);
	h2              = hash_of(l->texpaint_pack);
	uid             = l->uid;
}

void compare(int k) {
	slot_layer_t *l    = find_uid(uid);
	int           n    = timeline_test_count(l);
	int           kind = timeline_test_kind(l, n - 1);
	check(kind == 1, string("%s: the stroke is a TL_STROKE (kind %d, %d entries)", case_name(k), kind, n));
	timeline_test_materialize(l, n - 1); // The state before the stroke: the replay has to paint it
	check(!string_equals(h0, script_texture_hash(l->texpaint)), string("%s: the state before the stroke differs", case_name(k)));
	int fb  = paint_replay_test_fallbacks();
	int ok  = paint_replay_test_entry(l, n - 1);
	int eq0 = string_equals(h0, hash_of(l->texpaint));
	int eq1 = string_equals(h1, hash_of(l->texpaint_nor));
	int eq2 = string_equals(h2, hash_of(l->texpaint_pack));
	check(ok && paint_replay_test_fallbacks() == fb, string("%s: it replays", case_name(k)));
	check(eq0 && eq1 && eq2, string("%s: the replay is byte-equal to the live stroke (base %d, nor %d, pack %d)", case_name(k), eq0, eq1, eq2));
}

void next() {
	frame  = frame + 1;
	int cx = sys_x() + sys_w() / 2;
	int cy = sys_h() / 2;
	if (frame == 2) {
		setup(kase);
		if (kase == 6) { // A stroke to erase
			stroke(0.47);
			context_select_tool(TOOL_TYPE_ERASER);
			make_paint_set_batching(1);
		}
	}
	else if (kase == 9 && frame == 7) { // Saved, a new project, opened again
		iron_create_directory(dir);
		project_filepath_set(string("%s/rec.arm", dir));
		project_save(0);
	}
	else if (kase == 9 && frame == 9) {
		script_project_new();
	}
	else if (kase == 9 && frame == 11) {
		script_project_open(string("%s/rec.arm", dir));
	}
	else if (kase == 9 && frame == 14) {
		check(find_uid(uid) != NULL, "the layer is there after opening");
		check(string_equals(h0, hash_of(find_uid(uid)->texpaint)), "the layer as saved");
	}
	else if (frame == 6 && kase != 7) {
		stroke(0.45);
		paint_rec_flush();
		note();
	}
	else if (kase == 7 && frame == 6) {
		script_mouse_press(cx - 150, cy, 0);
	}
	else if (kase == 7 && frame > 6 && frame < 30) {
		script_mouse_move(cx - 150 + (frame - 6) * 12, cy + (frame % 5) * 3);
	}
	else if (kase == 7 && frame == 30) {
		script_mouse_release(cx + 130, cy, 0);
	}
	else if (kase == 7 && frame == 40) {
		paint_rec_flush();
		note();
	}
	else if ((kase != 7 && kase != 9 && frame == 10) || (kase == 7 && frame == 44) || (kase == 9 && frame == 16)) {
		compare(kase);
		kase  = kase + 1;
		frame = 0;
		if (kase == 10) {
			check(paint_rec_test_stored() >= 9, string("strokes stored (%d)", paint_rec_test_stored()));
			check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
			script_get_config()->paint_resolved_dabs = 0;
			script_get_config()->timeline_shadow     = 0;
			script_get_config()->project_v31         = v31;
			printf("[rec_replay] %d checks, %d failures", checks, fails);
			printf(fails == 0 ? "REC_REPLAY PASS" : "REC_REPLAY FAIL");
			script_quit();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_get_config()->paint_resolved_dabs = 1;
	script_get_config()->timeline_shadow     = 1;
	v31                                      = script_get_config()->project_v31;
	script_get_config()->project_v31         = 1;
	script_brush_defaults();
	script_project_new();
	slot_material_t *m   = script_material_create("rec_replay_blue");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	script_notify_on_next_frame(next);
}
