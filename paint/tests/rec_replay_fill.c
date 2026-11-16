#include "global.h"

// The fill tool on paint layers records TL_STROKE entries that replay at any resolution (paint_rec.c, paint_replay.c).
// Fills that pick (face, angle, uv island, mesh, submesh) keep the paint object and triangle picked with the live camera
// in their paint state; uv island fill paints the triangles of the picked one's island in the uv layout (its topology,
// util_mesh_uv_island_indices), not a flood at a fixed size.
// - each fill type at 1024 replays byte-equal into its layer (the state before restored)
// - each record replays into a 2048 layer close to the same fill painted live at 2048, and composite-fast (a replay
//   cache) byte-equal to evaluating the material

int fails  = 0;
int checks = 0;
int frame  = 0;
int phase  = 0;
int kase   = 0;

slot_layer_t *layers[6];
int           entries[6];
char         *hashes[6];

// FILL_TYPE_ order
char *name_of(int k) {
	if (k == 0) {
		return "object";
	}
	if (k == 1) {
		return "face";
	}
	if (k == 2) {
		return "angle";
	}
	if (k == 3) {
		return "uv island";
	}
	if (k == 4) {
		return "mesh";
	}
	return "submesh";
}

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[rec_fill] %s %s", ok ? "ok  " : "FAIL", name);
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

void paint_fill(int t) {
	context_set_fill_type(t);
	script_paint(0.52, 0.48);
	script_paint_end();
	paint_rec_flush();
}

void finish() {
	check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
	printf("[rec_fill] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "REC_FILL PASS" : "REC_FILL FAIL");
	script_quit();
}

void next() {
	frame = frame + 1;
	if (phase == 0) { // Each type recorded at 1024
		if (frame == 2) {
			project_test_set_res(1024, 1024);
		}
		else if (frame > 3 && frame % 2 == 0) {
			if (kase == 6) {
				phase = 1;
				frame = 0;
				kase  = 0;
			}
			else {
				slot_layer_t *b = new_layer();
				slot_layer_t *l = new_layer();
				paint_fill(kase);
				int e = timeline_test_count(l) - 1;
				int p = diff3(l, b, 1.0);
				check(timeline_test_kind(l, e) == 1 && p > 100, string("%s fill: a TL_STROKE, it paints (%d)", name_of(kase), p));
				layers[kase]  = l;
				entries[kase] = e;
				hashes[kase]  = hash3(l);
				kase          = kase + 1;
			}
		}
	}
	else if (phase == 1) { // Replayed in place
		if (frame == 4) {
			int fb = paint_replay_test_fallbacks();
			for (int k = 0; k < 6; ++k) {
				int ok = paint_replay_test_entry(layers[k], entries[k]);
				check(ok && string_equals(hash3(layers[k]), hashes[k]), string("%s fill replays byte-equal at 1024", name_of(k)));
			}
			check(paint_replay_test_fallbacks() == fb, "no replay fell back");
			project_test_set_res(2048, 2048);
		}
		else if (frame == 6) {
			phase = 2;
			frame = 0;
		}
	}
	else if (phase == 2) { // Each type live at 2048 against its record replayed there
		if (frame % 2 == 0) {
			if (kase == 6) {
				finish();
				return;
			}
			slot_layer_t *blank = new_layer();
			slot_layer_t *full  = new_layer();
			slot_layer_t *fast  = new_layer();
			slot_layer_t *live  = new_layer();
			paint_fill(kase);
			int ok = paint_replay_test_into(layers[kase], entries[kase], full);
			paint_replay_test_set_fast(1);
			ok = ok && paint_replay_test_into(layers[kase], entries[kase], fast);
			paint_replay_test_set_fast(0);
			int painted = diff3(live, blank, 1.0);
			int over1   = diff3(live, full, 1.0);
			int fastd   = diff3(full, fast, 0.0);
			printf("[rec_fill] %s at 2048: %d painted, %d over 1/255 against live, %d differ composite-fast", name_of(kase), painted, over1, fastd);
			check(ok && painted > 100, string("%s fill: replays at 2048 and paints (%d)", name_of(kase), painted));
			check(over1 * 100 <= painted, string("%s fill: the 1024 record at 2048 is within 1/255 of live on 99 percent (%d of %d)", name_of(kase), over1, painted));
			check(fastd == 0, string("%s fill: composite-fast is byte-equal (%d differ)", name_of(kase), fastd));
			kase = kase + 1;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	material_cache_set_enabled(0); // No live cache building on a thread when the script quits
	script_brush_defaults();
	script_project_new();
	slot_material_t *m   = script_material_create("FillUV");
	ui_node_t       *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
	ui_node_t       *chk = script_material_create_node("TEX_CHECKER");
	ui_node_t       *tc  = script_material_create_node("TEX_COORD");
	script_material_connect(tc, 2, chk, 0); // UV
	script_material_set_color(chk, 1, 1, 0.9, 0.2, 0.1, 1.0);
	script_material_set_color(chk, 1, 2, 0.1, 0.3, 0.8, 1.0);
	script_material_set_float(chk, 1, 3, 23.0);
	script_material_connect(chk, 0, out, 0);
	script_material_connect(chk, 1, out, 3);
	script_material_update();
	context_select_tool(TOOL_TYPE_FILL);
	script_notify_on_next_frame(next);
}
