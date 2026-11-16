#include "global.h"

// Version 31 project container (io/project_container.c, project_v31 on): round trip, opening reads only the live
// layers, saves append, a torn append opens the save before, compaction rewrites the file, history blobs are read
// from the file when used.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *dir    = "/tmp/materialstudio_project_container";
char *file   = NULL;
int   v31_was = 0;
int   count  = 0;
int   hashes[96];
int   blob_a = -1;
int   blob_b = -1;
int   saves  = 0;
int   rewrote = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[project_container] %s %s", ok ? "ok  " : "FAIL", name);
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
	for (int i = 0; i < count * 3; ++i) {
		hashes[i] = project_test_layer_hash(i / 3, i % 3);
	}
}

int same_pixels() {
	if (script_layer_count() != count) {
		printf("[project_container] %d layers, %d saved", script_layer_count(), count);
		return 0;
	}
	for (int i = 0; i < count * 3; ++i) {
		if (project_test_layer_hash(i / 3, i % 3) != hashes[i]) {
			printf("[project_container] layer %d channel %d differs", i / 3, i % 3);
			return 0;
		}
	}
	return 1;
}

// Textures of the live layers: the chunks an open reads
int live_textures() {
	int n = 0;
	for (int i = 0; i < count * 3; ++i) {
		if (hashes[i] != 0) {
			n = n + 1;
		}
	}
	return n;
}

void save() {
	project_filepath_set(file);
	project_save(0);
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
	}
	else if (step == 1) {
		slot_layer_t *a = script_layer_new_color(0xff3366cc, 1.0, 0.4, 0.0);
		script_layer_new_mask(a, 1);
		stroke(0.4, 0.4, 0.6, 0.6); // On the mask
		script_layer_new();
		stroke(0.35, 0.45, 0.6, 0.5);
		blob_a = hstore_test_put(3, 900, 70000, 0); // History blobs, saved with the project
		blob_b = hstore_test_put(3, 901, 5000, 1);
	}
	else if (step == 2) {
		snapshot();
		int unequal = 0;
		for (int i = 0; i < count; ++i) {
			if (project_test_layer_hash(i, 1) == 0 && project_test_layer_hash(i, 0) != 0) { // A mask
				unequal = unequal + project_test_mask_unequal(i);
			}
		}
		check(unequal == 0, string("mask channels are equal, R8 keeps them (%d texels differ)", unequal));
		save();
	}
	else if (step == 3) {
		check(project_container_is(file), "saved as a container");
		check(project_container_test_appended() == 0, "the first save writes it whole");
		script_project_new();
	}
	else if (step == 4) {
		int reads = project_container_test_reads();
		float disk = hstore_test_stat(8);
		script_project_open(file);
		int read = project_container_test_reads() - reads;
		check(read == live_textures(), string("opening reads the live layers only (%d chunks, %d textures)", read, live_textures()));
		check(hstore_test_stat(8) == disk, "history blobs are not read on open");
		check(same_pixels(), "round trip keeps the pixels");
		check(hstore_test_has(blob_a) && hstore_test_has(blob_b), "history blobs are indexed");
		check(hstore_test_get(blob_a, 900, 70000, 0) == 1 && hstore_test_get(blob_b, 901, 5000, 1) == 1, "history blobs read from the file");
		check(hstore_test_stat(8) == disk + 2.0, "when used");
		hstore_test_ref(blob_a); // Owned again, saved with the next saves
		hstore_test_ref(blob_b);
	}
	else if (step == 5) {
		int size = project_test_file_size(file);
		stroke(0.3, 0.3, 0.7, 0.35);
		save();
		saves = size;
	}
	else if (step == 6) {
		check(project_container_test_appended() == 1, "a save of the opened file appends");
		snapshot(); // State B
		stroke(0.3, 0.6, 0.7, 0.65);
		save();
	}
	else if (step == 7) {
		check(project_container_test_appended() == 1, "and again");
		project_test_truncate(file, 40); // The last footer torn
		script_project_new();
	}
	else if (step == 8) {
		script_project_open(file);
	}
	else if (step == 9) {
		check(same_pixels(), "a torn save opens the save before");
		check(hstore_test_get(blob_a, 900, 70000, 0) == 1, "with its history blobs");
		hstore_test_ref(blob_a); // Unreferenced blobs are not saved: the history load would ref them
		hstore_test_ref(blob_b);
		stroke(0.45, 0.2, 0.5, 0.8);
		save();
	}
	else if (step == 10) {
		// The torn save's chunks count as dead: this one may write the file whole
		snapshot();
		script_project_new();
	}
	else if (step == 11) {
		script_project_open(file);
	}
	else if (step == 12) {
		check(same_pixels(), "a save after a torn one opens");
		hstore_test_ref(blob_a);
		hstore_test_ref(blob_b);
		saves = 0;
	}
	else if (step == 13) { // Saves until one compacts
		if (project_container_test_appended() == 0 && saves > 0) {
			rewrote = 1;
		}
		if (!rewrote && saves < 12) {
			stroke(0.2 + saves * 0.05, 0.3, 0.25 + saves * 0.05, 0.7);
			save();
			saves = saves + 1;
			script_notify_on_next_frame(next);
			return;
		}
		check(rewrote, string("a mostly dead file is written whole (after %d saves)", saves));
		snapshot();
		script_project_new();
	}
	else if (step == 14) {
		script_project_open(file);
	}
	else if (step == 15) {
		check(same_pixels(), "a compacted file opens");
		check(hstore_test_get(blob_b, 901, 5000, 1) == 1, "with its history blobs");
		char *uuid = string_copy(project_uuid_get());
		check(string_equals(project_file_uuid(file), uuid), "the uuid is read from the file");
	}
	else {
		script_get_config()->project_v31 = v31_was;
		script_project_new();
		iron_delete_file(file);
		printf("[project_container] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "PROJECT_CONTAINER PASS" : "PROJECT_CONTAINER FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	v31_was                          = script_get_config()->project_v31;
	script_get_config()->project_v31 = 1;
	iron_create_directory(dir);
	file = string("%s/container.arm", dir);
	iron_delete_file(file);
	script_notify_on_next_frame(next);
}
