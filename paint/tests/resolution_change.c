#include "global.h"

// Working resolution changes that keep detail (util/util_layer_resolution.c), a layer a frame: 2K to 4K and back to 2K
// gives the layers back byte for byte, a change is a history step undone and redone exactly, and undo works across it
// (a stroke before and one after the change). Undo through timelines: the legacy tile pool holds one size, a stroke
// at the new size drops the tiles of strokes at the old one. Up, recorded strokes replay at the new size: a stroke
// painted at 2K and taken to 4K is close to the same stroke painted live at 4K.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *layer_paint;
slot_layer_t *layer_mask;
char         *h2k;   // Layer hashes at 2K before any change
char         *h4k;   // after 2K to 4K
char         *hpaint; // after a stroke at 2K
int           steps_before;
int           progress_seen = 0;
slot_layer_t *layer_rec;   // A stroke at 2K, taken to 4K
slot_layer_t *layer_live;  // The stroke painted live at 4K
slot_layer_t *layer_blank;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[resolution_change] %s %s", ok ? "ok  " : "FAIL", name);
}

char *layer_hashes() {
	char *s = "";
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->fill_material != NULL) {
			continue; // Fill layers render again at each size
		}
		if (l->texpaint != NULL) {
			s = string("%s %s", s, script_texture_hash(l->texpaint));
		}
		if (l->texpaint_nor != NULL) {
			s = string("%s %s", s, script_texture_hash(l->texpaint_nor));
		}
		if (l->texpaint_pack != NULL) {
			s = string("%s %s", s, script_texture_hash(l->texpaint_pack));
		}
	}
	return s;
}

int width_of(slot_layer_t *l) {
	gpu_texture_t *t = l->texpaint;
	return t->width;
}

int size_is(int s) {
	return width_of(script_layer_at(0)) == s && width_of(layer_paint) == s && width_of(layer_mask) == s;
}

void stroke(float y) {
	for (int i = 0; i < 10; ++i) {
		script_paint(0.3 + i * 0.04, y + i * 0.01);
	}
	script_paint_end();
}

void seeded_stroke() {
	render_path_paint_set_stamp_seed(4242);
	for (int i = 0; i < 30; ++i) {
		script_paint(0.35 + i * 0.01, 0.45 + (i % 7) * 0.004);
	}
	script_paint_end();
	paint_rec_flush();
}

slot_layer_t *new_layer() {
	app_layer_new(0);
	make_paint_set_batching(1); // Parses the paint material again
	return script_get_context()->layer;
}

int diff3(slot_layer_t *a, slot_layer_t *b, float tol) {
	int n = script_diff_textures(a->texpaint, b->texpaint, tol);
	n     = n + script_diff_textures(a->texpaint_nor, b->texpaint_nor, tol);
	n     = n + script_diff_textures(a->texpaint_pack, b->texpaint_pack, tol);
	return n;
}

void build_scene() {
	context_t       *ctx   = script_get_context();
	slot_material_t *m     = script_material_create("rc_noise");
	ui_node_t       *noise = script_material_create_node_at("TEX_NOISE", -600.0, 0.0);
	script_material_set_float(noise, 1, 1, 30.0); // Detail a bilinear round trip would blur
	script_material_connect(noise, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	layer_paint = script_layer_new();
	context_select_tool(TOOL_TYPE_BRUSH);
	ctx->brush_radius = 0.4;
	stroke(0.4);
	layer_mask = script_layer_new_mask(layer_paint, 1);
	script_layer_set(layer_paint);
	script_layer_new_fill();
	script_layer_set(layer_paint);
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;
	if (layers_resolution_running()) { // A layer a frame
		float p = layers_resolution_progress();
		progress_seen = progress_seen || (p > 0.0 && p < 1.0);
		script_notify_on_next_frame(next);
		return;
	}

	if (step == 0) {
		build_scene();
	}
	else if (step == 1) {
		h2k          = layer_hashes();
		steps_before = history_test_undos();
		layers_resolution_begin(4096, 4096);
	}
	else if (step == 2) {
		check(size_is(4096) && config_get_texture_res_x() == 4096, "2K to 4K: layers and the project at 4096");
		check(history_test_undos() == steps_before + 1, "the change is a history step, pushed once done");
		check(progress_seen, "it ran over frames, with progress");
		h4k = layer_hashes();
		layers_resolution_begin(2048, 2048);
	}
	else if (step == 3) {
		check(size_is(2048), "back at 2048");
		check(string_equals(layer_hashes(), h2k), string("2K to 4K to 2K: every layer byte for byte (%d restored)", layers_resolution_test_restores()));
		history_undo(); // To 4K again
	}
	else if (step == 4) {
		check(size_is(4096) && string_equals(layer_hashes(), h4k), "undo: the 4K layers exactly");
		history_undo(); // To 2K
	}
	else if (step == 5) {
		check(size_is(2048) && string_equals(layer_hashes(), h2k), "undo again: the 2K layers exactly");
		history_redo();
	}
	else if (step == 6) {
		check(size_is(4096) && string_equals(layer_hashes(), h4k), "redo: 4K exactly");
		history_redo();
	}
	else if (step == 7) {
		check(size_is(2048) && string_equals(layer_hashes(), h2k), "redo again: 2K exactly");
		script_layer_set(layer_paint);
		stroke(0.6); // A stroke at 2K, the change, a stroke at 4K
	}
	else if (step == 8) {
		hpaint = layer_hashes();
		layers_resolution_begin(4096, 4096);
	}
	else if (step == 9) {
		h4k = layer_hashes();
		script_layer_set(layer_paint);
		stroke(0.2);
	}
	else if (step == 10) {
		check(!string_equals(layer_hashes(), h4k), "a stroke at 4K");
		history_undo(); // The 4K stroke
	}
	else if (step == 11) {
		check(size_is(4096) && string_equals(layer_hashes(), h4k), "undo the 4K stroke");
		history_undo(); // The change
	}
	else if (step == 12) {
		check(size_is(2048) && string_equals(layer_hashes(), hpaint), "undo the change: the 2K layers with the 2K stroke, exactly");
		history_undo(); // The 2K stroke
	}
	else if (step == 13) {
		check(size_is(2048) && string_equals(layer_hashes(), h2k), "undo the 2K stroke: as before it");
		history_redo();
		history_redo();
	}
	else if (step == 14) {
		check(size_is(4096) && string_equals(layer_hashes(), h4k), "redo the stroke and the change");
	}
	else if (step == 15) { // Up against live: a new project, a seeded stroke at 2K
		script_get_config()->paint_resolved_dabs = 1; // Strokes record world space dabs: they replay at any resolution
		script_brush_defaults();
		script_project_new();
	}
	else if (step == 16) {
		slot_material_t *m   = script_material_create("rc_blue");
		ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
		script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
		script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
		script_material_update();
		context_t *ctx     = script_get_context();
		ctx->brush_radius  = 0.3;
		ctx->brush_spacing = 0.05;
		context_select_tool(TOOL_TYPE_BRUSH);
		layer_rec = new_layer();
		seeded_stroke();
	}
	else if (step == 17) {
		layers_resolution_begin(4096, 4096);
	}
	else if (step == 18) {
		layer_live = new_layer();
		seeded_stroke();
		layer_blank = new_layer();
	}
	else if (step == 19) {
		int painted = diff3(layer_live, layer_blank, 1.0);
		int over1   = diff3(layer_live, layer_rec, 1.0);
		printf("[resolution_change] 2K stroke taken to 4K against live 4K: %d of %d painted channel values over 1/255 (max %.1f)", over1, painted, script_diff_max());
		check(width_of(layer_rec) == 4096 && painted > 10000, string("the live stroke at 4K paints (%d)", painted));
		check(over1 * 100 <= painted, string("2K to 4K replays the stroke: within 1/255 of live 4K on 99 percent of what it paints (%d of %d)", over1, painted));
	}
	else {
		layers_resolution_apply(2048, 2048);
		script_get_config()->paint_resolved_dabs = 0;
		printf("[resolution_change] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "RESOLUTION_CHANGE PASS" : "RESOLUTION_CHANGE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	script_notify_on_next_frame(next);
}
