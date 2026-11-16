#include "global.h"

// Tiled export over frames (io/export_tiled.c): progress, and cancel mid-tiles and mid-files leaves the live layers, the
// Material Maker cap and the export's memory as they were, and removes the files it wrote. An export run to its end
// writes every file.

char *root = "/tmp/materialstudio_export_cancel";

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *hashes;
char *dir;
float last_progress = 0.0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_cancel] %s %s", ok ? "ok  " : "FAIL", name);
}

char *fresh_dir(char *name) {
	char *d = string("%s/%s", root, name);
	iron_create_directory(d);
	any_array_t *old = file_read_directory(d);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", d, old->buffer[i]));
	}
	return d;
}

int file_count(char *d) {
	any_array_t *files = file_read_directory(d);
	return files != NULL ? files->length : 0;
}

char *layer_hashes() {
	char *s = "";
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
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

void build_scene() {
	context_t       *ctx   = script_get_context();
	slot_material_t *m     = script_material_create("ec_noise");
	ui_node_t       *noise = script_material_create_node_at("TEX_NOISE", -600.0, 0.0);
	script_material_connect(noise, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	slot_layer_t *red = script_layer_new_color(0xffcc2020, 0.8, 0.4, 0.3);
	script_layer_new_mask(red, 1);
	slot_layer_t *paint = script_layer_new();
	context_select_tool(TOOL_TYPE_BRUSH);
	ctx->brush_radius = 0.4;
	for (int i = 0; i < 10; ++i) {
		script_paint(0.3 + i * 0.04, 0.4 + i * 0.02);
	}
	script_paint_end();
	slot_layer_t *fill = script_layer_new_fill();
}

void cancel_checks(char *when) {
	export_tiled_cancel();
	check(!export_tiled_running(), string("%s: cancelled", when));
	check(string_equals(hashes, layer_hashes()), string("%s: live layers as they were", when));
	check(mm_buffers_stat(14) == 0 && mm_buffers_stat(15) == 0, string("%s: Material Maker cap restored, nothing pinned", when));
	check(export_tiled_stat(6) == 0 && export_tiled_stat(7) == 0, string("%s: tile targets freed (%d KB), spool closed", when, export_tiled_stat(6)));
	check(file_count(dir) == 0, string("%s: no files left (%d)", when, file_count(dir)));
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}

	if (step == 0) {
		build_scene();
		frame = 0;
	}
	else if (step == 1) { // Start, a few units of work over frames, cancel during the tiles
		hashes = layer_hashes();
		config_t *cfg     = script_get_config();
		cfg->export_tiled = 1;
		export_texture_test_options("generic", 0, 1, 8, 0);
		export_tiled_test_tile(256);
		export_tiled_test_mm(1);
		dir    = fresh_dir("a");
		int ok = export_tiled_start(dir, 0);
		check(ok && export_tiled_running(), "export started");
		check(mm_buffers_stat(14) > 0, string("Material Maker export cap set (%d)", mm_buffers_stat(14)));
		for (int i = 0; i < 6; ++i) {
			export_tiled_step(1.0);
		}
		float p = export_tiled_progress();
		check(p > 0.0 && p < 0.5 && export_tiled_stat(6) > 0, string("tiles in progress (%.2f, %d KB of targets)", p, export_tiled_stat(6)));
		cancel_checks("mid-tiles");
		frame = 0;
	}
	else if (step == 2) { // Cancel once files are being written
		dir = fresh_dir("b");
		export_tiled_start(dir, 0);
		int guard = 0;
		while (export_tiled_stat(8) < 2 && export_tiled_running() && guard < 1000) {
			export_tiled_step(1.0);
			guard = guard + 1;
		}
		check(export_tiled_stat(8) >= 2 && file_count(dir) >= 2, string("files being written (%d)", file_count(dir)));
		cancel_checks("mid-files");
		frame = 0;
	}
	else if (step == 3) { // Over frames to the end
		if (frame == 3) {
			dir = fresh_dir("c");
			export_tiled_start(dir, 0);
		}
		float p = export_tiled_progress();
		check(p >= last_progress, string("progress grows (%.2f)", p));
		last_progress = p;
		if (!export_tiled_step(5.0)) {
			script_notify_on_next_frame(next);
			return;
		}
		check(file_count(dir) == 5, string("export over frames writes every file (%d)", file_count(dir)));
		check(string_equals(hashes, layer_hashes()), "live layers as they were");
		check(mm_buffers_stat(14) == 0 && export_tiled_stat(6) == 0 && export_tiled_stat(7) == 0, "cap restored, targets freed, spool closed");
		frame = 0;
	}
	else {
		config_t *cfg     = script_get_config();
		cfg->export_tiled = 0;
		export_tiled_test_mm(0);
		export_tiled_test_tile(0);
		printf("[export_cancel] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_CANCEL PASS" : "EXPORT_CANCEL FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	iron_create_directory(root);
	script_notify_on_next_frame(next);
}
