#include "global.h"

// Project files round trip: a save and an open keep the pixels, the layer kinds (layer, mask, group), the layer and
// material uids, the project uuid and non-square layers. A file of an older version does not open: the project stays.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *dir    = "";
char *file31 = NULL;
char *file30 = NULL;
int   kinds[32];
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
	printf("[project_roundtrip] %s %s", ok ? "ok  " : "FAIL", name);
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 12; ++i) {
		float t = i / 11.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	script_paint_end();
}

// 0 layer, 1 mask, 2 group
int kind_of(slot_layer_t *l) {
	return l->texpaint == NULL ? 2 : l->texpaint_nor == NULL ? 1 : 0;
}

int same_kinds() {
	for (int i = 0; i < count; ++i) {
		if (kind_of(script_layer_at(i)) != kinds[i]) {
			return 0;
		}
	}
	return 1;
}

void snapshot() {
	count = script_layer_count();
	for (int i = 0; i < count; ++i) {
		uids[i]  = project_test_layer_uid(i);
		kinds[i] = kind_of(script_layer_at(i));
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
			printf("[project_roundtrip] layer %d channel %d differs", i / 3, i % 3);
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
	script_layer_new_group(a);
	script_layer_new();
	stroke(0.35, 0.45, 0.6, 0.5);
	stroke(0.45, 0.35, 0.5, 0.65);
}

// A file of version 30 as builds before 31 wrote them: a map whose first key is the version
int       pos = 0;
buffer_t *v30 = NULL;

void put(int v) {
	buffer_set_u8(v30, pos, v);
	pos = pos + 1;
}

void put_u32(int v) {
	put(v);
	put(0);
	put(0);
	put(0);
}

void write_v30(char *path) {
	v30 = buffer_create(24);
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
	put(51); // 30
	put(48);
	iron_file_save_bytes(path, v30, 24);
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
		project_test_set_res(1000, 600); // Custom: tiles at the edges are partial
	}
	else if (step == 1) {
		check(project_test_res(0) == 1000 && project_test_res(1) == 600, "non-square project");
		make_layers();
	}
	else if (step == 2) {
		snapshot();
		check(uids_valid(), "uids of a new project");
		project_filepath_set(file31);
		project_save(0);
	}
	else if (step == 3) {
		check(iron_file_exists(file31), "saved");
		script_project_new();
	}
	else if (step == 4) {
		script_project_open(file31);
	}
	else if (step == 5) {
		check(project_test_res(0) == 1000 && project_test_res(1) == 600, string("non-square size kept (%d x %d)", project_test_res(0), project_test_res(1)));
		check(same_pixels(), "round trip keeps the pixels");
		check(same_kinds(), "layers, masks and groups kept");
		int same = script_layer_count() == count;
		for (int i = 0; i < count && same; ++i) {
			same = project_test_layer_uid(i) == uids[i];
		}
		check(same, "layer uids kept");
		check(project_test_material_uid(0) == mat_uid, "material uid kept");
		check(string_equals(project_uuid_get(), uuid), "project uuid kept");
		check(uids_valid(), "uids after a load");
		check(string_equals(script_get_project()->version, "32"), "version 32");
		write_v30(file30);
		script_project_open(file30);
	}
	else if (step == 6) {
		check(script_layer_count() == count && same_pixels() && string_equals(project_uuid_get(), uuid), "a version 30 file does not open, the project stays");
	}
	else {
		iron_delete_file(file31);
		iron_delete_file(file30);
		printf("[project_roundtrip] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "PROJECT_ROUNDTRIP PASS" : "PROJECT_ROUNDTRIP FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	dir = script_tmp_dir("materialstudio_project_roundtrip"); // Per run: suites of several worktrees run at once
	iron_create_directory(dir);
	file31 = string("%s/v31.arm", dir);
	file30 = string("%s/v30.arm", dir);
	script_notify_on_next_frame(next);
}
