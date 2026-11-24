#include "global.h"

// Saves during a hi-res export in export mode (io/export_tiled.c): the layers' live textures are 1 x 1 placeholders
// meanwhile, a save, a copy and a session head read the states export mode keeps of them instead. Each reopens with
// every layer at the working resolution and its pixels as they were, a layer without a timeline (written with no step)
// too. The history budget of the save counts the layers held away: the undo steps stay.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *root   = "";
char *file   = "";
char *copy   = "";
int   nlayers = 0;
int   hashes[24];
int   undos = 0;


// minic reads members of a local texture, not through l->texpaint->
int tex_width(slot_layer_t *l) {
	gpu_texture_t *t = l->texpaint;
	return t != NULL ? t->width : 0;
}
int tex_height(slot_layer_t *l) {
	gpu_texture_t *t = l->texpaint;
	return t != NULL ? t->height : 0;
}
int tex_format(slot_layer_t *l) {
	gpu_texture_t *t = l->texpaint;
	return t != NULL ? t->format : -1;
}

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_save] %s %s", ok ? "ok  " : "FAIL", name);
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
		printf("[export_save]   %s: %d layers, %d before", what, script_layer_count(), nlayers);
		return 0;
	}
	for (int i = 0; i < nlayers; ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->texpaint != NULL && (tex_width(l) != config_get_texture_res_x() || tex_height(l) != config_get_texture_res_y())) {
			printf("[export_save]   %s: layer %d is %d x %d", what, i, tex_width(l), tex_height(l));
			ok = 0;
		}
	}
	for (int i = 0; i < nlayers * 3 && i < 24; ++i) {
		if (project_test_layer_hash(i / 3, i % 3) != hashes[i]) {
			printf("[export_save]   %s: layer %d channel %d differs", what, i / 3, i % 3);
			ok = 0;
		}
	}
	return ok;
}

void build_scene() {
	context_t    *ctx  = script_get_context();
	slot_layer_t *red  = script_layer_new_color(0xffcc2020, 0.8, 0.4, 0.3);
	slot_layer_t *mask = script_layer_new_mask(red, 1);
	context_select_tool(TOOL_TYPE_BRUSH);
	ctx->brush_radius = 0.3;
	script_layer_new();
	stroke(0.3, 0.4, 0.7, 0.6);
	stroke(0.4, 0.3, 0.5, 0.7);
	// A layer with no timeline, written with no step: export mode captures it when it starts
	slot_layer_t *plain = script_layer_new();
	draw_begin(plain->texpaint, true, 0xff3366cc);
	draw_end();
}

// An export in export mode above the working resolution, a few steps in: the layers are away
void start_export(char *name) {
	char *d = string("%s/%s", root, name);
	iron_create_directory(d);
	export_texture_test_options("generic", 0, 0, 8, 0);
	export_tiled_set_mode(1);
	export_tiled_test_tile(512);
	export_tiled_set_resolution(config_get_texture_res_x() * 2, config_get_texture_res_y() * 2);
	export_tiled_start(d, 0);
	for (int i = 0; i < 3; ++i) {
		export_tiled_step(1.0);
	}
}

void end_export() {
	export_tiled_cancel();
	export_tiled_set_resolution(0, 0);
	export_tiled_set_mode(2);
	export_tiled_test_tile(0);
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
	}
	else if (step == 2) {
		timeline_finalize();
		snapshot();
		undos = history_test_undos();
		project_filepath_set(file);
		start_export("a");
		check(export_tiled_running() && export_tiled_frozen(), "the export runs in export mode");
		check(project_test_layer_hash(nlayers - 1, 0) != hashes[(nlayers - 1) * 3], "the layers are away (placeholders)");
		project_save(0); // Next frame, the export still running
		check(project_container_save_copy(copy), "a copy saved during the export");
	}
	else if (step == 3) {
		check(export_tiled_running() && export_tiled_frozen(), "the export ran through the save");
		check(history_test_undos() == undos, string("the save kept the undo steps (%d of %d)", history_test_undos(), undos));
		slot_layer_t *renamed = script_layer_at(0);
		check(app_layer_set_name(renamed->id, "renamed in export"), "an unsaved change during the export (a rename)");
		check(export_tiled_running() && export_tiled_frozen(), "a rename leaves the export running");
		check(project_session_write(), "a session head written during the export");
		export_tiled_step(1.0);
		end_export();
		check(same_layers("after the export"), "the layers came back as they were");
		hstore_test_crash(); // The head is the newest state: recovery reads it
	}
	else if (step == 4) {
		script_project_open(file);
		check(same_layers("the save"), "the project saved during the export reopens with its layers at their size and pixels");
	}
	else if (step == 5) {
		slot_layer_t *renamed = script_layer_at(0);
		check(project_modified_get() && string_equals(renamed->name, "renamed in export"), "opening recovered the head: the rename, a modified project");
	}
	else if (step == 6) {
		check(same_layers("the head"), "the session head written during the export recovers the layers");
		script_project_open(copy);
	}
	else if (step == 7) {
		check(same_layers("the copy"), "the copy saved during the export reopens with its layers");
	}
	else {
		script_project_new();
		iron_delete_file(file);
		iron_delete_file(copy);
		printf("[export_save] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_SAVE PASS" : "EXPORT_SAVE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = script_tmp_dir("materialstudio_export_save"); // Per run: suites of several worktrees run at once
	iron_create_directory(root);
	file = string("%s/export_save.arm", root);
	copy = string("%s/export_save_copy.arm", root);
	iron_delete_file(file);
	iron_delete_file(copy);
	script_brush_defaults();
	script_notify_on_next_frame(next);
}
