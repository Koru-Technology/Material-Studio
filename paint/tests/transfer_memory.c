#include "global.h"

// Transfer memory of layer readbacks and restores (timeline_tiles.c): a 4K project with painted layers is saved and
// opened again. Opening restores every layer whole a chunk at a time (64 MB at most, not the 192 MB of a layer), the
// pixels come back the same, the transfer is released once the open is done, a stroke's capture uses a chunk at most
// and a memory warning releases it. The gpu drops no work.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *dir    = "";
char *file   = NULL;
int   count  = 0;
int   hashes[96];
int   cap_mb = 64;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[transfer_memory] %s %s", ok ? "ok  " : "FAIL", name);
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 12; ++i) {
		float t = i / 11.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	script_paint_end();
}

int transfer_mb() {
	return timeline_tiles_transfer_bytes() / (1024 * 1024);
}

void snapshot() {
	count = script_layer_count();
	for (int i = 0; i < count; ++i) {
		for (int c = 0; c < 3; ++c) {
			hashes[i * 3 + c] = project_test_layer_hash(i, c);
		}
	}
}

int same_pixels() {
	if (script_layer_count() != count) {
		return 0;
	}
	for (int i = 0; i < count * 3; ++i) {
		if (project_test_layer_hash(i / 3, i % 3) != hashes[i]) {
			printf("[transfer_memory] layer %d channel %d differs", i / 3, i % 3);
			return 0;
		}
	}
	return 1;
}

void next() {
	frame = frame + 1;
	if (frame < 6) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		script_project_new();
	}
	else if (step == 1) {
		project_test_set_res(4096, 4096);
	}
	else if (step == 2) {
		check(config_get_texture_res_x() == 4096, "a 4K project");
		script_layer_new_color(0xff3366cc, 1.0, 0.4, 0.0);
		script_layer_new();
		stroke(0.3, 0.4, 0.7, 0.55);
		stroke(0.45, 0.3, 0.5, 0.7);
	}
	else if (step == 3) {
		snapshot();
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 4) {
		check(iron_file_exists(file), "saved");
		printf("[transfer_memory] transfer after the save: %d MB", transfer_mb());
		check(transfer_mb() <= cap_mb, "the save's captures use a chunk of transfer at most");
		script_project_new();
	}
	else if (step == 5) {
		script_project_open(file);
	}
	else if (step == 6) {
		check(config_get_texture_res_x() == 4096, "opened at 4K");
		check(same_pixels(), "every layer restored with the same pixels");
		printf("[transfer_memory] transfer after the open: %d bytes", timeline_tiles_transfer_bytes());
		check(timeline_tiles_transfer_bytes() == 0, "the transfer is released after the open");
		stroke(0.2, 0.2, 0.8, 0.8);
	}
	else if (step == 7) {
		printf("[transfer_memory] transfer after a stroke: %d MB", transfer_mb());
		check(transfer_mb() <= cap_mb, "a stroke's capture uses a chunk of transfer at most");
		base_on_memory_warning();
		check(timeline_tiles_transfer_bytes() == 0, "a memory warning releases the transfer");
	}
	else {
		iron_delete_file(file);
		check(gpu_errors() == 0, string("no gpu errors (%d)", gpu_errors()));
		printf("[transfer_memory] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "TRANSFER_MEMORY PASS" : "TRANSFER_MEMORY FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	dir = script_tmp_dir("materialstudio_transfer_memory");
	iron_create_directory(dir);
	file = string("%s/transfer.arm", dir);
	script_notify_on_next_frame(next);
}
