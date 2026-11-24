#include "global.h"

// A working resolution change saved with the project's history (history_io.c keeps its sizes): after a save and an
// open, undo goes back to the size before and redo forth, every layer at a valid size with its paint, and a save after
// that keeps the paint. Sizes below 1 (or past the platform's largest) change nothing.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *dir    = "";
char *file   = NULL;

int painted_hi = 0; // Opaque texels of the layer at 1024
int painted_lo = 0; // at 512
int layer_uid  = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[undo_resolution_io] %s %s", ok ? "ok  " : "FAIL", name);
}

slot_layer_t *find_uid(int uid) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->uid == uid) {
			return l;
		}
	}
	return NULL;
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

// Every paint layer and mask, all three textures, at s x s
int all_at(int s) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		gpu_texture_t *t = l->texpaint;
		if (t != NULL && (t->width != s || t->height != s)) {
			return 0;
		}
		t = l->texpaint_nor;
		if (t != NULL && (t->width != s || t->height != s)) {
			return 0;
		}
		t = l->texpaint_pack;
		if (t != NULL && (t->width != s || t->height != s)) {
			return 0;
		}
	}
	return config_get_texture_res_x() == s;
}

int near(int v, int ref) {
	int d = v - ref;
	d     = d < 0 ? -d : d;
	return ref > 0 && d * 100 <= ref * 15; // Within 15%
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
	frame = 0;
	if (layers_resolution_running()) {
		script_notify_on_next_frame(next);
		return;
	}

	if (step == 0) {
		layers_resolution_apply(1024, 1024); // A new project keeps the size of the one before
		slot_layer_t *l = script_layer_new();
		layer_uid       = l->uid;
		stroke(0.3, 0.4, 0.7, 0.5);
		stroke(0.4, 0.3, 0.5, 0.7);
	}
	else if (step == 1) {
		slot_layer_t *l = find_uid(layer_uid);
		painted_hi      = opaque(l);
		check(all_at(1024) && painted_hi > 1000, string("painted at 1024 (%d texels)", painted_hi));
		check(!layers_resolution_begin(0, 0) && !layers_resolution_begin(1024, -5), "a size below 1 is refused");
		check(!layers_resolution_begin(1024, 100000), "a size past the largest is refused");
		layers_resolution_apply(0, 0);
		check(all_at(1024), "applying 0 x 0 changes nothing");
		layers_resolution_begin(512, 512);
	}
	else if (step == 2) {
		slot_layer_t *l = find_uid(layer_uid);
		painted_lo      = opaque(l);
		check(all_at(512) && near(painted_lo * 4, painted_hi), string("at 512 (%d texels)", painted_lo));
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 3) {
		check(iron_file_exists(file), "saved");
		script_project_new();
	}
	else if (step == 4) {
		script_project_open(file);
	}
	else if (step == 5) {
		slot_layer_t *l = find_uid(layer_uid);
		check(l != NULL && all_at(512) && opaque(l) == painted_lo, "opened at 512 with its paint");
		check(history_test_undos() >= 3, string("the history is there (%d undos)", history_test_undos()));
		history_undo(); // The resolution change
	}
	else if (step == 6) {
		slot_layer_t *l = find_uid(layer_uid);
		check(all_at(1024), "undo after the open: every layer at 1024 again");
		check(near(opaque(l), painted_hi), string("with its paint (%d of %d texels)", opaque(l), painted_hi));
		history_redo();
	}
	else if (step == 7) {
		slot_layer_t *l = find_uid(layer_uid);
		check(all_at(512) && near(opaque(l), painted_lo), string("redo: 512 with its paint (%d of %d texels)", opaque(l), painted_lo));
		history_undo();
	}
	else if (step == 8) {
		slot_layer_t *l = find_uid(layer_uid);
		check(all_at(1024) && near(opaque(l), painted_hi), "undo again: 1024");
		history_undo(); // A stroke, at 1024
	}
	else if (step == 9) {
		history_redo();
	}
	else if (step == 10) {
		slot_layer_t *l = find_uid(layer_uid);
		painted_hi      = opaque(l);
		check(all_at(1024) && painted_hi > 1000, string("a stroke undone and redone at 1024 (%d texels)", painted_hi));
		project_save(0); // As saved after the undo
	}
	else if (step == 11) {
		script_project_new();
	}
	else if (step == 12) {
		script_project_open(file);
	}
	else if (step == 13) {
		slot_layer_t *l = find_uid(layer_uid);
		check(l != NULL && all_at(1024) && opaque(l) == painted_hi, "saved after the undo and opened: 1024 with the paint");
		history_redo(); // The change again, from this file's history
	}
	else if (step == 14) {
		slot_layer_t *l = find_uid(layer_uid);
		check(all_at(512) && near(opaque(l), painted_lo), "redo after the second open: 512 with its paint");
		int len = history_io_test_roundtrip(0);
		check(len > 0, "the history encodes and decodes");
		history_undo();
	}
	else if (step == 15) {
		slot_layer_t *l = find_uid(layer_uid);
		check(all_at(1024) && near(opaque(l), painted_hi), "undo after a roundtrip: 1024 with its paint");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
	}
	else {
		script_project_new();
		iron_delete_file(file);
		printf("[undo_resolution_io] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "UNDO_RESOLUTION_IO PASS" : "UNDO_RESOLUTION_IO FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	dir = script_tmp_dir("materialstudio_undo_resolution_io"); // Per run: suites of several worktrees run at once
	iron_create_directory(dir);
	file = string("%s/resolution.arm", dir);
	script_brush_defaults();
	script_project_new();
	slot_material_t *mat = script_material_create("uri_blue");
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
