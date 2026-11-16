#include "global.h"

// Project files: version 31 keeps the pixels, the layer and material uids, the project uuid and non-square layers. A
// version 30 file (Save as v30, what builds before 31 read) opens with the same pixels and new unique uids.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *dir    = "/tmp/materialstudio_legacy_upgrade";
char *file31 = NULL;
char *file30 = NULL;
int   count  = 0;
int   uids[32];
int   hashes[96];
int   mat_uid = 0;
char *uuid    = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[legacy_upgrade] %s %s", ok ? "ok  " : "FAIL", name);
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 12; ++i) {
		float t = i / 11.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	script_paint_end();
}

void snapshot() {
	count = script_layer_count();
	for (int i = 0; i < count; ++i) {
		uids[i] = project_test_layer_uid(i);
		for (int c = 0; c < 3; ++c) {
			hashes[i * 3 + c] = project_test_layer_hash(i, c);
		}
	}
	mat_uid = project_test_material_uid(0);
	uuid    = string_copy(project_uuid_get());
}

int same_pixels() {
	if (script_layer_count() != count) {
		return 0;
	}
	for (int i = 0; i < count * 3; ++i) {
		if (project_test_layer_hash(i / 3, i % 3) != hashes[i]) {
			printf("[legacy_upgrade] layer %d channel %d differs", i / 3, i % 3);
			return 0;
		}
	}
	return 1;
}

// Uids unique, non zero and below the next one handed out
int uids_valid() {
	int next = project_test_uid_next();
	for (int i = 0; i < script_layer_count(); ++i) {
		int u = project_test_layer_uid(i);
		if (u == 0 || u >= next) {
			return 0;
		}
		for (int j = 0; j < i; ++j) {
			if (project_test_layer_uid(j) == u) {
				return 0;
			}
		}
	}
	int m = project_test_material_uid(0);
	return m != 0 && m < next;
}

void make_layers() {
	slot_layer_t *a = script_layer_new_color(0xff3366cc, 1.0, 0.4, 0.0);
	script_layer_new_mask(a, 1);
	script_layer_new();
	stroke(0.35, 0.45, 0.6, 0.5);
	stroke(0.45, 0.35, 0.5, 0.65);
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		script_project_new();
		project_test_set_res(1024, 512);
	}
	else if (step == 1) {
		check(project_test_res(0) == 1024 && project_test_res(1) == 512, "non-square project");
		make_layers();
	}
	else if (step == 2) {
		snapshot();
		check(uids_valid(), "uids of a new project");
		project_filepath_set(file31);
		project_save(0);
	}
	else if (step == 3) {
		check(iron_file_exists(file31), "version 31 saved");
		script_project_new();
	}
	else if (step == 4) {
		script_project_open(file31);
	}
	else if (step == 5) {
		check(project_test_res(0) == 1024 && project_test_res(1) == 512, string("non-square size kept (%d x %d)", project_test_res(0), project_test_res(1)));
		check(same_pixels(), "version 31 round trip keeps the pixels");
		int same = script_layer_count() == count;
		for (int i = 0; i < count && same; ++i) {
			same = project_test_layer_uid(i) == uids[i];
		}
		check(same, "layer uids kept");
		check(project_test_material_uid(0) == mat_uid, "material uid kept");
		check(string_equals(project_uuid_get(), uuid), "project uuid kept");
		check(uids_valid(), "uids after a load");

		script_project_new();
		project_test_set_res(2048, 2048); // Version 30 has one size for both
	}
	else if (step == 6) {
		make_layers();
	}
	else if (step == 7) {
		snapshot();
		export_arm_run_project_v30(file30);
		script_project_new();
	}
	else if (step == 8) {
		check(iron_file_exists(file30), "version 30 saved");
		script_project_open(file30);
	}
	else if (step == 9) {
		check(same_pixels(), "a version 30 file opens with the same pixels");
		check(uids_valid(), "a version 30 file gets new unique uids");
		check(!string_equals(project_uuid_get(), uuid), "and a new uuid");
		check(string_equals(script_get_project()->version, "31"), "upgraded to 31");
	}
	else {
		iron_delete_file(file31);
		iron_delete_file(file30);
		printf("[legacy_upgrade] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "LEGACY_UPGRADE PASS" : "LEGACY_UPGRADE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	iron_create_directory(dir);
	file31 = string("%s/v31.arm", dir);
	file30 = string("%s/v30.arm", dir);
	script_notify_on_next_frame(next);
}
