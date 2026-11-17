#include "global.h"

// Camera tools record TL_STROKE entries that replay from the camera they were painted with (paint_rec.c, paint_replay.c):
// the decal's anchor and mask, the text tool's image, the stencil and the window's aspect are part of the record. Each
// case paints on a new layer at 1024, the camera turns, the state before is restored and the record replays: byte-equal.
// Then each record replays into a 2048 layer close to the same stroke painted live at 2048 (the camera turned back).
// Cases: decal, text, a brush with a stencil, blur, smudge and clone (each over a stroke of the material), a brush aligned
// to the camera, a brush mapping the material from the screen (PROJECT uvs).

int fails  = 0;
int checks = 0;
int frame  = 0;
int phase  = 0;
int kase   = 0;
int NCASES = 8;

slot_layer_t *layers[8];
slot_layer_t *pattern = NULL; // Filled with the material: the stencil
int           entries[8];
char         *hashes[8];

char *name_of(int k) {
	if (k == 0) {
		return "decal";
	}
	if (k == 1) {
		return "text";
	}
	if (k == 2) {
		return "stencil";
	}
	if (k == 3) {
		return "blur";
	}
	if (k == 4) {
		return "smudge";
	}
	if (k == 5) {
		return "clone";
	}
	if (k == 6) {
		return "camera align";
	}
	return "project";
}

void line(float y) {
	for (int i = 0; i < 20; ++i) {
		script_paint(0.35 + i * 0.015, y + (i % 5) * 0.004);
	}
	script_paint_end();
}

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[rec_camera] %s %s", ok ? "ok  " : "FAIL", name);
}

slot_layer_t *new_layer() {
	app_layer_new(0);
	return script_get_context()->layer;
}

char *hash3(slot_layer_t *l) {
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

int diff3(slot_layer_t *a, slot_layer_t *b, float tol) {
	int d = script_diff_textures(a->texpaint, b->texpaint, tol);
	d     = d + script_diff_textures(a->texpaint_nor, b->texpaint_nor, tol);
	d     = d + script_diff_textures(a->texpaint_pack, b->texpaint_pack, tol);
	return d;
}

// The case's tool and stroke
void paint_case(int k) {
	render_path_paint_set_stamp_seed(4242);
	if (k == 0) {
		context_select_tool(TOOL_TYPE_DECAL);
	}
	else if (k == 1) {
		context_select_tool(TOOL_TYPE_TEXT);
		script_set_text("Ab");
	}
	else if (k == 2) {
		context_select_tool(TOOL_TYPE_BRUSH);
		script_set_stencil(pattern->texpaint); // A layer filled with the material
		check(paint_asset_test_roundtrip(pattern->texpaint), "the stencil's texels as an asset and back are the same");
	}
	else if (k == 6) {
		context_select_tool(TOOL_TYPE_BRUSH);
		script_get_context()->brush_camera_align = 1;
	}
	else if (k == 7) {
		context_select_tool(TOOL_TYPE_BRUSH);
		script_set_uv_type(2); // UV_TYPE_PROJECT
	}
	else { // Over a stroke of the material, the entry before
		context_select_tool(TOOL_TYPE_BRUSH);
		make_paint_set_batching(1);
		line(0.45);
		context_select_tool(k == 5 ? TOOL_TYPE_CLONE : TOOL_TYPE_BLUR);
		script_set_blur_type(k == 4 ? 1 : 0);
		script_set_clone_delta(0.03, 0.02);
	}
	make_paint_set_batching(1); // Parses the paint material again
	if (k >= 2) {
		line(0.46);
	}
	else {
		script_paint(0.5, 0.5);
		script_paint_end();
	}
	paint_rec_flush();
	if (k == 2) {
		script_set_stencil(NULL);
	}
	script_get_context()->brush_camera_align = 0;
	script_set_uv_type(0);
}

void finish() {
	check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
	printf("[rec_camera] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "REC_CAMERA PASS" : "REC_CAMERA FAIL");
	script_quit();
}

void next() {
	frame = frame + 1;
	if (phase == 0) {
		if (frame == 2) {
			project_test_set_res(1024, 1024);
		}
		else if (frame == 3) {
			pattern = new_layer();
			context_select_tool(TOOL_TYPE_FILL);
			make_paint_set_batching(1);
			script_paint(0.5, 0.5);
			script_paint_end();
		}
		else if (frame > 7 && frame % 2 == 0) { // The pattern's layer settled
			if (kase == NCASES) {
				phase = 1;
				frame = 0;
				kase  = 0;
			}
			else {
				slot_layer_t *b = new_layer();
				slot_layer_t *l = new_layer();
				layers[kase]    = l;
				paint_case(kase);
				int e = timeline_test_count(l) - 1;
				int p = diff3(l, b, 1.0);
				check(timeline_test_kind(l, e) == 1 && p > 100, string("%s: a TL_STROKE, it paints (%d)", name_of(kase), p));
				entries[kase] = e;
				hashes[kase]  = hash3(l);
				if (kase == 2) { // Its stencil still the texture painted with
					int ok = paint_replay_test_entry(l, e);
					check(ok && string_equals(hash3(l), hashes[kase]), "stencil replays byte-equal right after, from the texture painted with");
				}
				kase          = kase + 1;
			}
		}
	}
	else if (phase == 1) { // The camera turned, each replayed in place
		if (frame == 2) {
			script_camera_save();
			script_camera_turn(0.3, 0.15);
		}
		else if (frame == 4) {
			int fb = paint_replay_test_fallbacks();
			for (int k = 0; k < NCASES; ++k) {
				int ok = paint_replay_test_entry(layers[k], entries[k]);
				check(ok && string_equals(hash3(layers[k]), hashes[k]), string("%s replays byte-equal from its camera", name_of(k)));
			}
			check(paint_replay_test_fallbacks() == fb, "no replay fell back");
			script_camera_restore();
			project_test_set_res(2048, 2048); // The layers scaled up: what a 1024 result looks like at 2048
		}
		else if (frame == 6) {
			phase = 2;
			frame = 0;
		}
	}
	else if (phase == 2) { // Live at 2048 against the 1024 record replayed there
		if (frame % 2 == 0) {
			if (kase == NCASES) {
				finish();
				return;
			}
			slot_layer_t *blank = new_layer();
			slot_layer_t *full  = new_layer();
			slot_layer_t *live  = new_layer();
			paint_case(kase);
			int ok      = 1;
			if (kase >= 3 && kase <= 5) { // The stroke under it first
				ok = paint_replay_test_into(layers[kase], entries[kase] - 1, full);
			}
			ok          = ok && paint_replay_test_into(layers[kase], entries[kase], full);
			int painted = diff3(live, blank, 1.0);
			int over1   = diff3(live, full, 1.0);
			printf("[rec_camera] %s at 2048: %d painted, %d over 1/255 against live", name_of(kase), painted, over1);
			check(ok && painted > 100, string("%s: replays at 2048 and paints (%d)", name_of(kase), painted));
			if (kase == 1) { // A few hundred texels of glyph edges, its 512 px image sampled at another density
				int over8 = diff3(live, full, 8.0);
				check(over8 * 20 <= painted, string("text: the 1024 record at 2048 is within 8/255 of live on 95 percent (%d of %d)", over8, painted));
			}
			else if (kase < 3 || kase > 5) {
				check(over1 * 100 <= painted, string("%s: the 1024 record at 2048 is within 1/255 of live on 99 percent (%d of %d)", name_of(kase), over1, painted));
			}
			else { // Their reach is the uv footprint painted at 1024 (blur's kernel, smudge's and clone's screen offsets): the
				   // 1024 result scaled up, not a 2048 stroke
				int scaled  = diff3(layers[kase], blank, 1.0);
				int over8   = diff3(layers[kase], full, 8.0);
				printf("[rec_camera] %s at 2048 against the 1024 result scaled up: %d of %d over 8/255", name_of(kase), over8, scaled);
				check(over8 * 20 <= scaled, string("%s: the 1024 record at 2048 is within 8/255 of the 1024 result scaled up on 95 percent (%d of %d)", name_of(kase), over8, scaled));
			}
			kase = kase + 1;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	material_cache_set_enabled(0); // No live cache building on a thread when the script quits
	script_brush_defaults();
	script_project_new();
	slot_material_t *m   = script_material_create("CameraUV");
	ui_node_t       *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
	ui_node_t       *chk = script_material_create_node("TEX_CHECKER");
	ui_node_t       *tc  = script_material_create_node("TEX_COORD");
	script_material_connect(tc, 2, chk, 0); // UV
	script_material_set_color(chk, 1, 1, 0.9, 0.2, 0.1, 1.0);
	script_material_set_color(chk, 1, 2, 0.1, 0.3, 0.8, 1.0);
	script_material_set_float(chk, 1, 3, 3.0); // Large squares: few edges where resolutions differ
	script_material_connect(chk, 0, out, 0);
	script_material_connect(chk, 1, out, 3);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.3;
	ctx->brush_spacing = 0.05;
	script_get_config()->brush_live = 1; // The camera does not jitter from frame to frame
	script_notify_on_next_frame(next);
}
