#include "global.h"

// Crash recovery (io/project_container.c session heads, timeline_shadow on): work after the last save is
// journaled as a session head; after a crash, opening the file gives the saved state and the journal stays (also through
// another crash with no edits after the open), recovering gives the unsaved layers, pixels and timelines back. Saving
// ends the pending state.

int   fails      = 0;
int   checks     = 0;
int   step       = 0;
int   frame      = 0;
char *dir        = NULL;
char *file       = NULL;
int   shadow_was = 0;
int   uid_a      = 0;
int   uid_b      = 0;
char *saved_a    = NULL;
char *live_a     = NULL;
char *live_b     = NULL;
int   count_a    = 0;
int   cursor_a   = 0;
int   layers     = 0;
int   states[64];
char *before_a   = NULL; // a before its second stroke
int   v2_was     = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[session_recovery] %s %s", ok ? "ok  " : "FAIL", name);
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
		saved_a = string_copy(layer_hash(find_uid(uid_a)));
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 3) { // Unsaved work: strokes and a new layer
		before_a = string_copy(layer_hash(find_uid(uid_a)));
		script_layer_set(find_uid(uid_a));
		stroke(0.45, 0.35, 0.5, 0.65);
	}
	else if (step == 4) {
		slot_layer_t *b = script_layer_new();
		uid_b           = b->uid;
		stroke(0.3, 0.6, 0.7, 0.62);
	}
	else if (step == 5) {
		timeline_finalize();
		slot_layer_t *a = find_uid(uid_a);
		live_a          = string_copy(layer_hash(a));
		live_b          = string_copy(layer_hash(find_uid(uid_b)));
		count_a         = timeline_test_count(a);
		cursor_a        = timeline_test_cursor(a);
		layers          = script_layer_count();
		for (int p = 0; p <= count_a; ++p) {
			states[p] = timeline_io_test_state(a, p);
		}
		check(!string_equals(live_a, saved_a), "a changed since the save");
		check(project_session_write(), "session head journaled");
		check(project_session_pending(file), "unsaved work pending");
		hstore_test_crash(); // The app dies: the journal stays as it is on disk
	}
	else if (step == 6) {
		script_project_open(file);
		check(string_equals(layer_hash(find_uid(uid_a)), saved_a) && find_uid(uid_b) == NULL, "opening gives the saved state");
		check(project_session_pending(file), "and keeps the unsaved work");
	}
	else if (step == 7) { // Frames went by after the open, nothing edited: the opened state is no session head
		check(!project_session_changed() && !project_session_due(), "opening journals no session head of its own");
		hstore_test_crash(); // And the app dies again
	}
	else if (step == 8) {
		script_project_open(file);
		check(project_session_pending(file), "a crash with no edits keeps the unsaved work of the session before");
	}
	else if (step == 9) {
		check(project_session_recover(file), "recovered");
	}
	else if (step == 10) {
		slot_layer_t *a = find_uid(uid_a);
		slot_layer_t *b = find_uid(uid_b);
		check(a != NULL && b != NULL && script_layer_count() == layers, string("layers as before the crash (%d of %d)", script_layer_count(), layers));
		check(a != NULL && string_equals(layer_hash(a), live_a), "pixels of a as before the crash");
		check(b != NULL && string_equals(layer_hash(b), live_b), "pixels of the new layer");
		check(a != NULL && timeline_test_count(a) == count_a && timeline_test_cursor(a) == cursor_a,
		      string("a's timeline as before the crash (%d entries, cursor %d)", a != NULL ? timeline_test_count(a) : -1, a != NULL ? timeline_test_cursor(a) : -1));
		int same = a != NULL;
		for (int p = 0; same && p <= count_a; ++p) {
			same = timeline_io_test_state(a, p) == states[p];
		}
		check(same, "and its kept states");
		int ok = a != NULL;
		for (int p = 0; ok && p <= count_a; ++p) {
			ok = timeline_test_materialize(a, p);
		}
		timeline_test_materialize(a, cursor_a);
		check(ok && string_equals(layer_hash(a), live_a), "every position materializes");
		// Undo across the crash: b's stroke, b, a's second stroke
		history_undo();
		history_undo();
		history_undo();
	}
	else if (step == 9) {
		history_flush_restores(NULL);
		slot_layer_t *a = find_uid(uid_a);
		check(find_uid(uid_b) == NULL, "undo after recovery: the new layer goes");
		check(a != NULL && string_equals(layer_hash(a), before_a), "undo after recovery: a before its second stroke");
		history_redo();
		history_redo();
		history_redo();
	}
	else if (step == 10) {
		history_flush_restores(NULL);
		slot_layer_t *a = find_uid(uid_a);
		check(a != NULL && string_equals(layer_hash(a), live_a) && find_uid(uid_b) != NULL, "redo after recovery");
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 11) {
		check(!project_session_pending(file), "saving ends the pending state");
	}
	else {
		script_get_config()->timeline_shadow = shadow_was;
		script_get_config()->history_v2      = v2_was;
		script_project_new();
		iron_delete_file(file);
		printf("[session_recovery] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "SESSION_RECOVERY PASS" : "SESSION_RECOVERY FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	shadow_was                           = script_get_config()->timeline_shadow;
	script_get_config()->timeline_shadow = 1;
	v2_was                               = script_get_config()->history_v2;
	script_get_config()->history_v2      = 1; // Saved steps undo from timelines
	dir = script_tmp_dir("materialstudio_session_recovery"); // Per run: suites of several worktrees run at once
	iron_create_directory(dir);
	file = string("%s/recover.arm", dir);
	iron_delete_file(file);
	script_notify_on_next_frame(next);
}
