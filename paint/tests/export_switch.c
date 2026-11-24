#include "global.h"

// What changes the project during an export in export mode cancels it first (export_tiled_abort_for_project_change):
// its layers are back before the change, no placeholder is freed twice, nothing is restored into a project that is
// gone. A new project, an open, a layer deleted, an undo, a change of resolution; a layer deleted during an export
// without export mode. A change of layer properties leaves it running.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *root   = "";
char *file   = "";
int   nlayers = 0;
int   hashes[24];
int   half = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_switch] %s %s", ok ? "ok  " : "FAIL", name);
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 12; ++i) {
		float t = i / 11.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	script_paint_end();
}

void snapshot() {
	nlayers = script_layer_count();
	for (int i = 0; i < nlayers * 3 && i < 24; ++i) {
		hashes[i] = project_test_layer_hash(i / 3, i % 3);
	}
}

// Every layer at the working resolution, its pixels those of the snapshot
int same_layers(char *what) {
	int ok = script_layer_count() == nlayers;
	if (!ok) {
		printf("[export_switch]   %s: %d layers, %d before", what, script_layer_count(), nlayers);
		return 0;
	}
	for (int i = 0; i < nlayers; ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->texpaint != NULL && (l->texpaint->width != config_get_texture_res_x() || l->texpaint->height != config_get_texture_res_y())) {
			printf("[export_switch]   %s: layer %d is %d x %d", what, i, l->texpaint->width, l->texpaint->height);
			ok = 0;
		}
	}
	for (int i = 0; i < nlayers * 3 && i < 24; ++i) {
		if (project_test_layer_hash(i / 3, i % 3) != hashes[i]) {
			printf("[export_switch]   %s: layer %d channel %d differs", what, i / 3, i % 3);
			ok = 0;
		}
	}
	return ok;
}

void build_scene() {
	context_t    *ctx = script_get_context();
	slot_layer_t *red = script_layer_new_color(0xffcc2020, 0.8, 0.4, 0.3);
	script_layer_new_mask(red, 1);
	context_select_tool(TOOL_TYPE_BRUSH);
	ctx->brush_radius = 0.3;
	script_layer_new();
	stroke(0.3, 0.4, 0.7, 0.6);
}

// An export above the working resolution, a few steps in: in export mode (the layers away, mode 1) or not (0)
void start_export(int mode) {
	char *d = string("%s/e", root);
	iron_create_directory(d);
	export_texture_test_options("generic", 0, 0, 8, 0);
	export_tiled_set_mode(mode);
	export_tiled_test_tile(512);
	export_tiled_set_resolution(config_get_texture_res_x() * 2, config_get_texture_res_y() * 2);
	export_tiled_start(d, 0);
	for (int i = 0; i < 3; ++i) {
		export_tiled_step(1.0);
	}
}

int cancelled() {
	return !export_tiled_running() && !export_tiled_frozen();
}

slot_layer_t *top() {
	return script_layer_at(script_layer_count() - 1);
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		script_project_new();
	}
	else if (step == 1) {
		build_scene();
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 2) { // A new project
		timeline_finalize();
		snapshot();
		start_export(1);
		check(export_tiled_running() && export_tiled_frozen(), "the export runs in export mode");
		script_project_new();
		check(cancelled(), "a new project cancelled the export");
		slot_layer_t *l = script_layer_at(0);
		check(script_layer_count() == 1 && l->texpaint->width == config_get_texture_res_x(), "the new project has its layer at the working resolution");
	}
	else if (step == 3) { // An open
		start_export(1);
		check(export_tiled_frozen(), "export mode in the new project");
		script_project_open(file);
		check(cancelled(), "an open cancelled the export");
	}
	else if (step == 4) {
		check(same_layers("opened"), "the project opened during the export has its layers");
	}
	else if (step == 5) { // A layer property: the export goes on. A layer deleted: cancelled first.
		start_export(1);
		app_layer_set_blending(top()->id, 2);
		check(export_tiled_running() && export_tiled_frozen(), "a layer's blending changed: the export goes on");
		script_layer_delete(top());
		check(cancelled(), "a layer deleted cancelled the export");
		check(script_layer_count() == nlayers - 1, string("the layer is gone (%d layers)", script_layer_count()));
	}
	else if (step == 6) { // Undo during the export
		start_export(1);
		app_undo(); // The delete
		check(cancelled(), "an undo cancelled the export");
		app_undo(); // The blending
	}
	else if (step == 7) {
		check(same_layers("undone"), "the deleted layer came back by undo, every layer as it was");
		start_export(0); // Without export mode: the layers stay, a deleted one is freed
		check(export_tiled_running() && !export_tiled_frozen(), "an export without export mode");
		script_layer_delete(top());
		check(cancelled(), "a layer deleted cancelled the export without export mode too");
		app_undo();
	}
	else if (step == 8) { // A change of resolution
		check(same_layers("undone again"), "every layer as it was");
		start_export(1);
		half = config_get_texture_res_x() / 2;
		layers_resolution_begin(half, half);
		check(cancelled(), "a change of resolution cancelled the export");
	}
	else if (step == 9) {
		if (layers_resolution_running()) {
			frame = 3;
			script_notify_on_next_frame(next);
			return;
		}
		check(top()->texpaint->width == half, string("the layers at the new resolution (%d of %d)", top()->texpaint->width, half));
	}
	else {
		export_tiled_set_resolution(0, 0);
		export_tiled_set_mode(2);
		export_tiled_test_tile(0);
		layers_resolution_apply(half * 2, half * 2); // A new project keeps the size of the one before
		script_project_new();
		iron_delete_file(file);
		printf("[export_switch] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_SWITCH PASS" : "EXPORT_SWITCH FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = script_tmp_dir("materialstudio_export_switch"); // Per run: suites of several worktrees run at once
	iron_create_directory(root);
	file = string("%s/export_switch.arm", root);
	iron_delete_file(file);
	script_brush_defaults();
	script_notify_on_next_frame(next);
}
