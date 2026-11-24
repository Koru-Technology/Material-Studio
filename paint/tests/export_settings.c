#include "global.h"

// An export runs with the settings it started with (export_texture_settings_t): the export sheet's Preset and Layers
// pickers changed while it runs over frames (app_export_set) leave it as it is, its files are those of the same export
// run untouched, with and without export mode. The next export takes the new settings.

int   fails   = 0;
int   checks  = 0;
int   step    = 0;
int   frame   = 0;
char *root    = "";
int   unreal  = -1;
int   generic = -1;
int   picks   = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_settings] %s %s", ok ? "ok  " : "FAIL", name);
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

int file_count(char *name) {
	any_array_t *files = file_read_directory(string("%s/%s", root, name));
	return files != NULL ? files->length : 0;
}

// Every file of a in b, the same pixels within 1/255
int same_files(char *a, char *b) {
	any_array_t *fa = file_read_directory(string("%s/%s", root, a));
	any_array_t *fb = file_read_directory(string("%s/%s", root, b));
	if (fa == NULL || fb == NULL || fa->length != fb->length || fa->length == 0) {
		return 0;
	}
	for (int i = 0; i < fa->length; ++i) {
		int d = script_diff_images(string("%s/%s/%s", root, a, fa->buffer[i]), string("%s/%s/%s", root, b, fa->buffer[i]), 1.0);
		if (d != 0) {
			printf("[export_settings]   %s differs (%d values over 1/255, max %.1f)", fa->buffer[i], d, script_diff_max());
			return 0;
		}
	}
	return 1;
}

int preset_index(char *name) {
	for (int i = 0; i < 64; ++i) {
		if (string_equals(app_export_preset_name(i), name)) {
			return i;
		}
	}
	return -1;
}

void build_scene() {
	context_t *ctx = script_get_context();
	script_layer_new_color(0xffcc2020, 0.8, 0.4, 0.3);
	script_layer_new();
	context_select_tool(TOOL_TYPE_BRUSH);
	ctx->brush_radius = 0.3;
	for (int i = 0; i < 10; ++i) {
		script_paint(0.3 + i * 0.04, 0.4 + i * 0.02);
	}
	script_paint_end();
	script_layer_new_fill();
}

// The sheet's settings: generic, visible layers
void settings_generic() {
	export_texture_test_options("generic", 0, 0, 8, 0);
	app_export_test_pick(generic, 0);
}

// The reference export, then the same one started: true while it runs (a step a frame, the pickers changed each)
void begin_export(char *ref, char *name) {
	settings_generic();
	export_texture_run(fresh_dir(ref), 0);
	export_tiled_start(fresh_dir(name), 0);
	picks = 0;
}

int step_export() {
	app_export_test_pick(picks % 2 == 0 ? unreal : generic, (picks + 1) % 4); // Unreal, selected layer first
	picks = picks + 1;
	return !export_tiled_step(5.0);
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}

	if (step == 0) {
		build_scene();
		unreal  = preset_index("unreal");
		generic = preset_index("generic");
		check(unreal >= 0 && generic >= 0, string("presets found (generic %d, unreal %d)", generic, unreal));
		export_tiled_test_tile(512);
		frame = 0;
	}
	else if (step == 1) { // Without export mode
		export_tiled_set_mode(0);
		begin_export("ref", "picked");
	}
	else if (step == 2) {
		if (step_export()) {
			script_notify_on_next_frame(next);
			return;
		}
		check(picks > 1, string("the pickers changed %d times while it ran", picks));
		check(file_count("picked") == file_count("ref") && same_files("ref", "picked"),
		      string("the export kept its preset and layers (%d files, %d without the change)", file_count("picked"), file_count("ref")));
	}
	else if (step == 3) { // In export mode, above the working resolution
		export_tiled_set_mode(1);
		export_tiled_set_resolution(config_get_texture_res_x() * 2, config_get_texture_res_y() * 2);
		begin_export("ref_hi", "picked_hi");
	}
	else if (step == 4) {
		if (step_export()) {
			script_notify_on_next_frame(next);
			return;
		}
		check(!export_tiled_frozen(), "export mode: the export ran to its end, the layers back");
		check(file_count("picked_hi") == file_count("ref_hi") && same_files("ref_hi", "picked_hi"),
		      string("export mode: the export kept its preset and layers (%d files, %d without the change)", file_count("picked_hi"), file_count("ref_hi")));
		export_tiled_set_resolution(0, 0);
		export_tiled_set_mode(2);
	}
	else if (step == 5) { // The next one takes the settings picked
		settings_generic();
		app_export_test_pick(unreal, 0);
		export_texture_run(fresh_dir("next"), 0);
		check(file_count("next") == 3, string("the next export takes the preset picked (%d files, unreal writes 3)", file_count("next")));
		frame = 0;
	}
	else {
		settings_generic();
		export_tiled_test_tile(0);
		printf("[export_settings] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_SETTINGS PASS" : "EXPORT_SETTINGS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = script_tmp_dir("materialstudio_export_settings"); // Per run: suites of several worktrees run at once
	iron_create_directory(root);
	script_brush_defaults();
	script_project_new();
	script_notify_on_next_frame(next);
}
