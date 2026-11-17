#include "global.h"

// Projects of older versions open (import_arm_upgrade). fixtures/project_v31.arm was saved by a version 31 build: a
// triplanar fill layer (scale 2.5, angle 45), a decal fill layer, a painted layer whose mask has height blend on (compared
// with the layers below, influence 2, sharpness 20, offset 0.25, reach 0.05 uv). Its fills become fill sources of
// the material, the height blend a Height Blend effect on top of the mask's stack. Saved again, it opens as version 33.
// A file of a newer version does not open. On the iPad, copy fixtures/ next to the script.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *dir    = "";
char *old    = NULL;
char *saved  = NULL;
char *newer  = NULL;
int   count  = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[project_upgrade] %s %s", ok ? "ok  " : "FAIL", name);
}

int near(float a, float b) {
	return a > b - 0.001 && a < b + 0.001;
}

int id_at(int i) {
	return script_layer_at(i)->id;
}

// The layers as the version 31 build left them, bottom first: paint, fill, decal, mask, painted layer
void check_layers(char *when) {
	check(script_layer_count() == 5, string("%s: five layers (%d)", when, script_layer_count()));
	if (script_layer_count() != 5) {
		return;
	}
	int f = id_at(1);
	int d = id_at(2);
	int m = id_at(3);
	check(script_layer_at(0)->fill == NULL && script_layer_at(4)->fill == NULL, string("%s: paint layers stay paint layers", when));
	check(script_layer_at(1)->fill != NULL, string("%s: fill layer has a fill", when));
	check(app_layer_fill_projection(f) == 1, string("%s: triplanar kept (%d)", when, app_layer_fill_projection(f)));
	check(app_layer_fill_kind(f, 0) == 3 && app_layer_fill_kind(f, 3) == 3, string("%s: channels from the material", when));
	check(fill_main_material(script_layer_at(1)->fill) != NULL, string("%s: the material found", when));
	check(near(app_layer_value(f, 6), 2.5) && near(app_layer_value(f, 7), 2.5), string("%s: scale 2.5 (%f)", when, app_layer_value(f, 6)));
	check(near(app_layer_value(f, 10), 45.0), string("%s: angle 45 (%f)", when, app_layer_value(f, 10)));
	check(script_layer_at(2)->fill != NULL && app_layer_fill_projection(d) == 2, string("%s: decal fill layer projects from its box", when));

	check(app_mask_effect_count(m) == 2, string("%s: mask stack of two effects (%d)", when, app_mask_effect_count(m)));
	check(app_mask_effect_type(m, 0) == 17, string("%s: Height Blend on top (%d)", when, app_mask_effect_type(m, 0))); // Top first
	check(app_mask_effect_type(m, 1) == 0, string("%s: the paint effect below", when));
	check(app_mask_effect_value(m, 0, 4) == 1.0, string("%s: compared with the layers below", when));
	check(near(app_mask_effect_value(m, 0, 0), 2.0), string("%s: influence 2 (%f)", when, app_mask_effect_value(m, 0, 0)));
	check(near(app_mask_effect_value(m, 0, 1), 20.0), string("%s: sharpness 20 (%f)", when, app_mask_effect_value(m, 0, 1)));
	check(near(app_mask_effect_value(m, 0, 2), 0.25), string("%s: offset 0.25 (%f)", when, app_mask_effect_value(m, 0, 2)));
	check(app_mask_effect_value(m, 0, 3) > 51.1 && app_mask_effect_value(m, 0, 3) < 51.3,
	      string("%s: reach 0.05 uv is 51.2 texels (%f)", when, app_mask_effect_value(m, 0, 3)));
}

// A file whose first key is the version, as each version writes them
int       pos = 0;
buffer_t *buf = NULL;

void put(int v) {
	buffer_set_u8(buf, pos, v);
	pos = pos + 1;
}

void put_u32(int v) {
	put(v);
	put(0);
	put(0);
	put(0);
}

void write_version(char *path, int tens, int ones) {
	buf = buffer_create(24);
	pos = 0;
	put(223); // 0xdf map
	put_u32(1);
	put(219); // 0xdb string
	put_u32(7);
	put(118); // version
	put(101);
	put(114);
	put(115);
	put(105);
	put(111);
	put(110);
	put(219);
	put_u32(2);
	put(48 + tens);
	put(48 + ones);
	iron_file_save_bytes(path, buf, 24);
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		script_project_open(old);
	}
	else if (step == 1) {
		fill_ensure_all();
		check_layers("version 31");
		check(string_equals(script_get_project()->version, "33"), "read as version 33");
		project_filepath_set(saved);
		project_save(0);
	}
	else if (step == 2) {
		script_project_new();
	}
	else if (step == 3) {
		script_project_open(saved);
	}
	else if (step == 4) {
		check_layers("saved again");
		count = script_layer_count();
		write_version(newer, 9, 9);
		script_project_open(newer);
	}
	else if (step == 5) {
		check(script_layer_count() == count, "a version 99 file does not open, the project stays");
	}
	else {
		iron_delete_file(old);
		iron_delete_file(saved);
		iron_delete_file(newer);
		printf("[project_upgrade] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "PROJECT_UPGRADE PASS" : "PROJECT_UPGRADE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	dir = script_tmp_dir("materialstudio_project_upgrade");
	iron_create_directory(dir);
	old   = string("%s/v31.arm", dir); // A copy: opening a project writes into its file
	saved = string("%s/v33.arm", dir);
	newer = string("%s/v99.arm", dir);
	file_copy(string("%sfixtures/project_v31.arm", script_dir()), old);
	script_notify_on_next_frame(next);
}
