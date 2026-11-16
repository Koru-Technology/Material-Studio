#include "global.h"

// Export mode (io/export_tiled.c, export_tiled_set_mode): the live layers leave the GPU during a tiled export, their
// states in the blob store, and come back exactly. The files are the same as without it, at the working resolution and
// above it; a cancel brings the layers back too.

char *root = "";

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *hashes;
int   layers_before = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_mode] %s %s", ok ? "ok  " : "FAIL", name);
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

// Every file of a in b, byte for byte
int same_files(char *a, char *b) {
	any_array_t *fa = file_read_directory(string("%s/%s", root, a));
	any_array_t *fb = file_read_directory(string("%s/%s", root, b));
	if (fa == NULL || fb == NULL || fa->length != fb->length || fa->length == 0) {
		return 0;
	}
	for (int i = 0; i < fa->length; ++i) {
		if (script_diff_files(string("%s/%s/%s", root, a, fa->buffer[i]), string("%s/%s/%s", root, b, fa->buffer[i])) != 0) {
			printf("[export_mode]   %s differs", fa->buffer[i]);
			return 0;
		}
	}
	return 1;
}

void build_scene() {
	context_t       *ctx   = script_get_context();
	slot_material_t *m     = script_material_create("em_noise");
	ui_node_t       *noise = script_material_create_node_at("TEX_NOISE", -600.0, 0.0);
	script_material_connect(noise, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	slot_layer_t *red  = script_layer_new_color(0xffcc2020, 0.8, 0.4, 0.3);
	slot_layer_t *mask = script_layer_new_mask(red, 1);
	int           g    = script_mask_add_effect(mask, 6, -1); // Position gradient
	script_mask_set_effect(mask, g, 1, 1.0, 2);
	slot_layer_t *paint = script_layer_new();
	context_select_tool(TOOL_TYPE_BRUSH);
	ctx->brush_radius = 0.4;
	for (int i = 0; i < 10; ++i) {
		script_paint(0.3 + i * 0.04, 0.4 + i * 0.02);
	}
	script_paint_end();
	script_layer_new_fill();
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;
	config_t *cfg = script_get_config();

	if (step == 0) {
		build_scene();
	}
	else if (step == 1) { // The same export without and with export mode
		cfg->export_tiled = 1;
		export_texture_test_options("generic", 0, 1, 8, 0);
		export_tiled_test_tile(512);
		hashes = layer_hashes();
		export_tiled_set_mode(0);
		export_texture_run(fresh_dir("off"), 0);
		check(export_tiled_stat(12) == 0, "mode off: nothing evicted");
		export_tiled_set_mode(1);
		export_texture_run(fresh_dir("on"), 0);
		check(export_tiled_stat(12) > 0, string("mode on: %d KB of live layers evicted", export_tiled_stat(12)));
		check(same_files("off", "on"), "the files are the same with export mode");
		check(string_equals(hashes, layer_hashes()), "the live layers came back exactly");
	}
	else if (step == 2) { // Above the working resolution
		export_tiled_set_resolution(4096, 4096);
		export_tiled_set_mode(0);
		export_texture_run(fresh_dir("off4k"), 0);
		export_tiled_set_mode(1);
		export_texture_run(fresh_dir("on4k"), 0);
		export_tiled_set_resolution(0, 0);
		check(same_files("off4k", "on4k"), "4096: the files are the same with export mode");
		check(string_equals(hashes, layer_hashes()), "4096: the live layers came back exactly");
	}
	else if (step == 3) { // While it runs the layers are away; a cancel brings them back
		export_tiled_set_mode(1);
		int before = export_tiled_stat(13);
		layers_before = before;
		export_tiled_start(fresh_dir("cancel"), 0);
		export_tiled_step(1.0);
		int during = export_tiled_stat(13);
		check(during * 100 < before, string("during the export the live layers hold %d KB (%d KB before)", during, before));
		check(export_tiled_frozen(), "the viewport keeps its last frame");
	}
	else if (step == 4) { // Frames rendered meanwhile (frozen), then a cancel
		check(export_tiled_running() && export_tiled_frozen(), "frames went by during the export");
		export_tiled_cancel();
		check(!export_tiled_frozen(), "the viewport renders again");
		check(string_equals(hashes, layer_hashes()) && export_tiled_stat(13) == layers_before, "cancelled: the live layers came back exactly");
	}
	else {
		export_tiled_set_mode(2);
		export_tiled_test_tile(0);
		cfg->export_tiled = 0;
		printf("[export_mode] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_MODE PASS" : "EXPORT_MODE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = script_tmp_dir("materialstudio_export_mode"); // Per run: suites of several worktrees run at once
	iron_create_directory(root);
	script_project_new();
	script_notify_on_next_frame(next);
}
