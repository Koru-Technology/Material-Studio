#include "global.h"

// Timelines saved with v31 projects (timeline_io.c, timeline_shadow and project_v31 on): after a save and an open, the
// timelines have their entries, cursor and kept states, each position materializes to the pixels it had before the
// save, and the cursor's state is the live layer. An appending save reads the children of the new tile maps only, a
// whole rewrite of every reachable one.

int   fails      = 0;
int   checks     = 0;
int   step       = 0;
int   frame      = 0;
char *dir        = "/tmp/materialstudio_timeline_persist";
char *file       = NULL;
int   v31_was    = 0;
int   shadow_was = 0;
int   uid_a      = 0;
int   uid_m      = 0;
int   count_a    = 0;
int   cursor_a   = 0;
int   count_m    = 0;
int   states[64];
char *pixels[64];
char *live_a     = NULL;
char *live_m     = NULL;
int   count2     = 0;
char *pixels2[64];
float decodes    = 0.0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[timeline_persist] %s %s", ok ? "ok  " : "FAIL", name);
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 12; ++i) {
		float t = i / 11.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	script_paint_end();
}

char *layer_hash(slot_layer_t *l) {
	if (l->texpaint_nor == NULL) {
		return script_texture_hash(l->texpaint);
	}
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

slot_layer_t *find_uid(int uid) {
	for (int i = 0; i < script_layer_count(); ++i) {
		if (script_layer_at(i)->uid == uid) {
			return script_layer_at(i);
		}
	}
	return NULL;
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
		slot_layer_t *a = script_layer_new();
		uid_a           = a->uid;
		stroke(0.35, 0.45, 0.6, 0.5);
	}
	else if (step == 2) {
		slot_layer_t *m = script_layer_new_mask(find_uid(uid_a), 0);
		uid_m           = m->uid;
		stroke(0.4, 0.4, 0.6, 0.6); // On the mask
	}
	else if (step == 3) {
		script_layer_set(find_uid(uid_a));
		stroke(0.45, 0.35, 0.5, 0.65);
	}
	else if (step == 4) {
		stroke(0.3, 0.6, 0.7, 0.62);
	}
	else if (step == 5) {
		history_undo(); // A redo branch: the cursor is not at the end
	}
	else if (step == 6) {
		timeline_finalize();
		slot_layer_t *a = find_uid(uid_a);
		slot_layer_t *m = find_uid(uid_m);
		count_a         = timeline_test_count(a);
		cursor_a        = timeline_test_cursor(a);
		count_m         = timeline_test_count(m);
		live_a          = string_copy(layer_hash(a));
		live_m          = string_copy(layer_hash(m));
		check(count_a >= 3 && cursor_a == count_a - 1, string("a has a redo branch (%d entries, cursor %d)", count_a, cursor_a));
		check(count_m >= 1, string("the mask has a timeline (%d entries)", count_m));
		for (int p = 0; p <= count_a; ++p) {
			states[p] = timeline_io_test_state(a, p);
			timeline_test_materialize(a, p);
			pixels[p] = string_copy(layer_hash(a));
		}
		timeline_test_materialize(a, cursor_a);
		check(string_equals(layer_hash(a), live_a), "materialize at the cursor is the live layer before saving");
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 7) {
		check(project_container_is(file), "saved as a container");
		script_project_new();
	}
	else if (step == 8) {
		script_project_open(file);
		slot_layer_t *a = find_uid(uid_a);
		slot_layer_t *m = find_uid(uid_m);
		check(a != NULL && m != NULL, "layers found by uid");
		check(string_equals(layer_hash(a), live_a) && string_equals(layer_hash(m), live_m), "live layers as saved");
		check(timeline_test_count(a) == count_a && timeline_test_cursor(a) == cursor_a, string("a's timeline as saved (%d entries, cursor %d)", timeline_test_count(a), timeline_test_cursor(a)));
		check(timeline_test_count(m) == count_m, "the mask's timeline as saved");
		int same = 1;
		for (int p = 0; p <= count_a; ++p) {
			same = same && timeline_io_test_state(a, p) == states[p];
		}
		check(same, "kept states as saved");
	}
	else if (step == 9) {
		slot_layer_t *a  = find_uid(uid_a);
		int           ok = 1;
		for (int p = 0; p <= count_a; ++p) {
			if (!timeline_test_materialize(a, p) || !string_equals(layer_hash(a), pixels[p])) {
				printf("[timeline_persist] position %d differs", p);
				ok = 0;
			}
		}
		check(ok, "every position materializes as before the save");
		timeline_test_materialize(a, cursor_a);
		check(string_equals(layer_hash(a), live_a), "materialize at the cursor is the live layer");
		slot_layer_t *m = find_uid(uid_m);
		timeline_test_materialize(m, timeline_test_cursor(m));
		check(string_equals(layer_hash(m), live_m), "and the mask's");
		hstore_test_trim(0); // References rebuilt at the load: nothing the timelines hold goes
		ok = 1;
		for (int p = 0; p <= count_a; ++p) {
			if (!timeline_test_materialize(a, p) || !string_equals(layer_hash(a), pixels[p])) {
				ok = 0;
			}
		}
		check(ok, "after a trim too");
	}
	else if (step == 10) { // Saved again: the timelines' blobs are written again (whole file)
		project_filepath_set(string("%s/copy.arm", dir));
		project_save(0);
	}
	else if (step == 11) {
		script_project_new();
	}
	else if (step == 12) {
		script_project_open(string("%s/copy.arm", dir));
		slot_layer_t *a  = find_uid(uid_a);
		int           ok = timeline_test_count(a) == count_a;
		for (int p = 0; ok && p <= count_a; ++p) {
			ok = timeline_test_materialize(a, p) && string_equals(layer_hash(a), pixels[p]);
		}
		check(ok, "a project saved from an opened one keeps its timelines");
		timeline_test_materialize(a, cursor_a);
		script_layer_set(a);
		stroke(0.35, 0.55, 0.65, 0.58); // On the mesh
	}
	else if (step == 13) {
		stroke(0.4, 0.42, 0.62, 0.44);
	}
	else if (step == 14) {
		timeline_finalize();
		slot_layer_t *a = find_uid(uid_a);
		count2          = timeline_test_count(a);
		for (int p = 0; p <= count2; ++p) {
			timeline_test_materialize(a, p);
			pixels2[p] = string_copy(layer_hash(a));
		}
		timeline_test_materialize(a, timeline_test_cursor(a));
		decodes = hstore_test_stat(9);
		project_filepath_set(string("%s/copy.arm", dir)); // The opened file: appended
		project_save(0);
	}
	else if (step == 15) {
		int read = (int)(hstore_test_stat(9) - decodes);
		int maps = project_container_test_index_count(2);
		check(project_container_test_appended() == 1, "saved by appending");
		check(read >= 1 && read <= 6 && maps >= 6, string("an append reads the new blobs only (%d, %d maps in the file)", read, maps)); // Maps, strokes
		decodes = hstore_test_stat(9);
		project_filepath_set(string("%s/copy2.arm", dir)); // Save As: whole
		project_save(0);
	}
	else if (step == 16) {
		int read = (int)(hstore_test_stat(9) - decodes);
		int maps = project_container_test_index_count(2);
		check(project_container_test_appended() == 0, "a new file is written whole");
		check(read >= maps, string("a whole rewrite reads every reachable tile map (%d blobs, %d maps)", read, maps)); // And strokes
		script_project_new();
	}
	else if (step == 17) {
		script_project_open(string("%s/copy2.arm", dir));
		slot_layer_t *a  = find_uid(uid_a);
		int           ok = timeline_test_count(a) == count2;
		for (int p = 0; ok && p <= count2; ++p) {
			ok = timeline_test_materialize(a, p) && string_equals(layer_hash(a), pixels2[p]);
		}
		check(ok, string("it opens with every position (%d entries)", count2));
		script_project_open(string("%s/copy.arm", dir));
		a  = find_uid(uid_a);
		ok = timeline_test_count(a) == count2;
		for (int p = 0; ok && p <= count2; ++p) {
			ok = timeline_test_materialize(a, p) && string_equals(layer_hash(a), pixels2[p]);
		}
		check(ok, "and the appended file too");
		live_a = string_copy(layer_hash(a));
		char *path = string_copy(project_filepath_get());
		char *uuid = string_copy(project_uuid_get());
		check(project_container_save_copy(string("%s/nohistory.arm", dir)), "copy without history saved");
		check(string_equals(project_filepath_get(), path) && string_equals(project_uuid_get(), uuid), "the session keeps its file and uuid");
		int copy_size = project_test_file_size(string("%s/nohistory.arm", dir));
		int full_size = project_test_file_size(string("%s/copy2.arm", dir));
		check(copy_size < full_size, string("smaller than with history (%d KB, %d KB)", copy_size / 1024, full_size / 1024));
		check(!string_equals(project_file_uuid(string("%s/nohistory.arm", dir)), uuid), "with its own uuid");
		script_project_new();
	}
	else if (step == 18) {
		script_project_open(string("%s/nohistory.arm", dir));
		slot_layer_t *a = find_uid(uid_a);
		check(a != NULL && string_equals(layer_hash(a), live_a), "it opens with the live layers");
		check(timeline_test_count(a) == -1, "and no timeline");
		int maps = project_container_test_index_count(2);
		check(maps <= script_layer_count(), string("only the live layers' tile maps, full (%d)", maps));
	}
	else {
		script_get_config()->project_v31     = v31_was;
		script_get_config()->timeline_shadow = shadow_was;
		script_project_new();
		iron_delete_file(file);
		iron_delete_file(string("%s/copy.arm", dir));
		iron_delete_file(string("%s/copy2.arm", dir));
		iron_delete_file(string("%s/nohistory.arm", dir));
		printf("[timeline_persist] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "TIMELINE_PERSIST PASS" : "TIMELINE_PERSIST FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	dir = script_tmp_dir("materialstudio_timeline_persist"); // Per run: suites of several worktrees run at once
	v31_was                              = script_get_config()->project_v31;
	shadow_was                           = script_get_config()->timeline_shadow;
	script_get_config()->project_v31     = 1;
	script_get_config()->timeline_shadow = 1;
	iron_create_directory(dir);
	file = string("%s/timelines.arm", dir);
	script_notify_on_next_frame(next);
}
