#include "global.h"

// Session heads (io/project_container.c) read back only what changed: a layer's live state is its timeline's state at
// the cursor, or its last capture while its textures were not written since (timeline_live_map, keyed by the textures'
// write stamps). On four layers, a head after strokes reads nothing back; a layer written with no step recorded is read
// back whole, once, and recovery gives its new pixels; layers loaded from the file are read back by no head; several
// written layers are read back one a frame, the head written once none is left.

int   fails   = 0;
int   checks  = 0;
int   step    = 0;
int   frame   = 0;
char *dir     = NULL;
char *file    = NULL;
int   uids[4];
int   hashes[12];
int   reads0  = 0;
int   last    = 0;
int   max_add = 0;
int   frames_reading = 0;
int   polls   = 0;
float head_ms = 0.0;
float ms_sum  = 0.0;
int   ms_n    = 0;

int layer_tiles = 64 * 64 * 3; // A paint layer read back whole: base, nor, pack

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[session_heads] %s %s", ok ? "ok  " : "FAIL", name);
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 12; ++i) {
		float t = i / 11.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	script_paint_end();
}

slot_layer_t *find_uid(int uid) {
	for (int i = 0; i < script_layer_count(); ++i) {
		if (script_layer_at(i)->uid == uid) {
			return script_layer_at(i);
		}
	}
	return NULL;
}

// A write no step records: the layer's base is cleared to a color
void write_unrecorded(int k, int color) {
	draw_begin(find_uid(uids[k])->texpaint, true, color);
	draw_end();
}

void snapshot() {
	for (int i = 0; i < 12; ++i) {
		hashes[i] = project_test_layer_hash(i / 3, i % 3);
	}
}

int same_pixels() {
	for (int i = 0; i < 12; ++i) {
		if (project_test_layer_hash(i / 3, i % 3) != hashes[i]) {
			printf("[session_heads] layer %d channel %d differs", i / 3, i % 3);
			return 0;
		}
	}
	return 1;
}

// A head written now: tiles it read back
int head() {
	timeline_finalize(); // The stroke's own tiles, not the head's
	int   r  = timeline_tiles_test_reads();
	float t0 = sys_time();
	project_session_write();
	head_ms = (sys_time() - t0) * 1000.0;
	int n   = timeline_tiles_test_reads() - r;
	printf("[session_heads] head: %.1f ms, %d tiles read back", head_ms, n);
	return n;
}

void next() {
	frame = frame + 1;
	if (frame < 4 && step != 14) { // 14 looks at every frame
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		script_project_new();
	}
	else if (step == 1) { // Four paint layers, the last one painted
		uids[0] = script_layer_at(0)->uid;
		for (int k = 1; k < 4; ++k) {
			uids[k] = script_layer_new()->uid;
		}
		stroke(0.35, 0.45, 0.6, 0.5);
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 2) {
		head(); // The first one captures the layers without a timeline
		stroke(0.4, 0.4, 0.6, 0.6);
	}
	else if (step >= 3 && step <= 5) { // Heads after a stroke on layer 3, timed
		int n  = head();
		ms_sum = ms_sum + head_ms;
		ms_n   = ms_n + 1;
		check(n == 0, string("a head after a stroke reads nothing back (%d tiles)", n));
		stroke(0.3 + step * 0.05, 0.4, 0.5, 0.6 - step * 0.03);
	}
	else if (step == 6) {
		printf("[session_heads] 4 layers: %.1f ms a head (mean of %d)", ms_sum / ms_n, ms_n);
		script_layer_set(find_uid(uids[1])); // Layer 1 painted first: its timeline is made at the stroke
		stroke(0.3, 0.3, 0.7, 0.7);
	}
	else if (step == 7) {
		int n = head();
		check(n == 0, string("a head after the first stroke of a layer reads nothing back (%d tiles)", n));
		write_unrecorded(2, 0xff3366cc);
	}
	else if (step == 8) {
		int n = head();
		check(n == layer_tiles, string("a layer written with no step is read back whole (%d of %d tiles)", n, layer_tiles));
		n = head();
		check(n == 0, string("and once (%d tiles)", n));
		snapshot();
		hstore_test_crash();
	}
	else if (step == 9) {
		script_project_open(file); // Recovers the unsaved work
	}
	else if (step == 10) {
		check(project_modified_get(), "recovered");
	}
	else if (step == 11) {
		check(same_pixels(), "recovery gives the pixels of the head, the unrecorded write too");
		script_layer_set(find_uid(uids[3]));
		stroke(0.45, 0.35, 0.5, 0.65);
	}
	else if (step == 12) {
		int n = head();
		check(n == 0, string("layers loaded are read back by no head (%d tiles)", n));
		stroke(0.55, 0.3, 0.4, 0.6);
	}
	else if (step == 13) { // Two layers written, the head due on its own: one read back a frame
		timeline_finalize();
		write_unrecorded(0, 0xff20a040);
		write_unrecorded(2, 0xffa02040);
		reads0 = timeline_tiles_test_reads();
		last   = reads0;
	}
	else if (step == 14) { // Each frame until the update loop wrote the head (due PC_SESSION_SECONDS after the last)
		int r   = timeline_tiles_test_reads();
		int add = r - last;
		last    = r;
		if (add > max_add) {
			max_add = add;
		}
		if (add > 0) {
			frames_reading = frames_reading + 1;
		}
		polls = polls + 1;
		if (project_session_changed() && polls < 2000) {
			script_notify_on_next_frame(next);
			return;
		}
		check(!project_session_changed(), string("the head was written (%d frames)", polls));
		check(max_add == layer_tiles && frames_reading == 2 && r - reads0 == 2 * layer_tiles,
		      string("two written layers read back one a frame (%d frames, %d tiles at most, %d in all)", frames_reading, max_add, r - reads0));
	}
	else if (step == 15) {
		int n = head();
		check(n == 0, string("frames later nothing is read back (%d tiles)", n));
	}
	else {
		script_project_new();
		iron_delete_file(file);
		printf("[session_heads] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "SESSION_HEADS PASS" : "SESSION_HEADS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	dir = script_tmp_dir("materialstudio_session_heads");
	iron_create_directory(dir);
	file = string("%s/heads.arm", dir);
	iron_delete_file(file);
	script_notify_on_next_frame(next);
}
