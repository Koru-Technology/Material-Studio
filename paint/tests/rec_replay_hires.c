#include "global.h"

// Recorded strokes replay at another resolution (paint_replay.c). A seeded stroke is recorded at 1024, the project goes to
// 2048 and the same stroke is painted live there: the record replayed into a new 2048 layer is close to it (the numbers
// are printed). Replayed again in 4 tiles with a halo, each tile a target of its own, the tiles are equal to the
// whole replay up to the last bit (within 1/255, all but 0.1% exact).

int fails  = 0;
int checks = 0;
int frame  = 0;
int entry  = 0;

slot_layer_t *rec   = NULL; // Recorded at 1024
slot_layer_t *live  = NULL; // Painted live at 2048
slot_layer_t *whole = NULL; // The record replayed at 2048
slot_layer_t *tiled = NULL; // and in tiles
slot_layer_t *blank = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[rec_hires] %s %s", ok ? "ok  " : "FAIL", name);
}

slot_layer_t *new_layer() {
	app_layer_new(0);
	return script_get_context()->layer;
}

void stroke() {
	render_path_paint_set_stamp_seed(4242);
	for (int i = 0; i < 30; ++i) {
		script_paint(0.35 + i * 0.01, 0.45 + (i % 7) * 0.004);
	}
	script_paint_end();
}

int diff3(slot_layer_t *a, slot_layer_t *b, float tol) {
	int n = script_diff_textures(a->texpaint, b->texpaint, tol);
	n     = n + script_diff_textures(a->texpaint_nor, b->texpaint_nor, tol);
	n     = n + script_diff_textures(a->texpaint_pack, b->texpaint_pack, tol);
	return n;
}

void next() {
	frame = frame + 1;
	if (frame == 2) {
		project_test_set_res(1024, 1024);
	}
	else if (frame == 4) {
		rec = new_layer();
		make_paint_set_batching(1); // Parses the paint material again
	}
	else if (frame == 6) {
		stroke();
		paint_rec_flush();
		entry = timeline_test_count(rec) - 1;
		check(timeline_test_kind(rec, entry) == 1, "the stroke at 1024 is a TL_STROKE");
	}
	else if (frame == 8) {
		project_test_set_res(2048, 2048);
	}
	else if (frame == 10) {
		live = new_layer();
		make_paint_set_batching(1);
	}
	else if (frame == 12) {
		stroke();
		paint_rec_flush();
		whole = new_layer();
		tiled = new_layer();
		blank = new_layer();
		int fb = paint_replay_test_fallbacks();
		check(paint_replay_test_into(rec, entry, whole), "the 1024 record replays into a 2048 layer");
		check(paint_replay_test_tiles(rec, entry, tiled, 2, 32), "and in 4 tiles");
		check(paint_replay_test_fallbacks() == fb, "no replay fell back");
	}
	else if (frame == 14) {
		int   painted = diff3(live, blank, 1.0);
		int   over1   = diff3(live, whole, 1.0);
		float mx      = script_diff_max();
		int   over8   = diff3(live, whole, 8.0);
		printf("[rec_hires] replay 1024 -> 2048 against live 2048: %d of %d painted channel values differ by more than 1/255, %d by more than 8/255",
		       over1, painted, over8);
		check(painted > 10000, string("the live stroke paints (%d)", painted));
		check(over1 * 100 <= painted, string("the replay at 2048 is within 1/255 of live 2048 on 99 percent of what it paints (%d of %d)", over1, painted));
		// Byte-equal is out of reach: the rasterizer interpolates the mesh's attributes (world position) from screen positions
		// a tile shifts, the last bit differs here and there
		int tiles = diff3(whole, tiled, 0.0);
		int tiles1 = diff3(whole, tiled, 1.0);
		printf("[rec_hires] 4 tiles against the whole replay: %d channel values differ, %d by more than 1/255", tiles, tiles1);
		check(tiles1 == 0 && tiles * 1000 <= painted, string("4 tiles are within 1/255 of the whole replay, on all but 0.1 percent exactly (%d differ)", tiles));
		check(diff3(whole, blank, 1.0) > 10000, "the whole replay paints");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[rec_hires] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "REC_HIRES PASS" : "REC_HIRES FAIL");
		script_quit();
		return;
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	slot_material_t *m   = script_material_create("rec_hires_blue");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.3;
	ctx->brush_spacing = 0.05;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
