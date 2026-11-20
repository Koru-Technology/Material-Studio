#include "global.h"

// Fill tiles on disk (util_fill.c, util_disk_cache.c): the tile of a heavy Material Maker library material (Medieval
// Stone Brick Wall, 1403), once its buffers are full, is written to Library/Caches; when the tiles go (as with an open)
// it comes back from there, the same texels, without rendering it again nor its Material Maker passes.

int fails  = 0;
int checks = 0;
int phase  = 0;
int frame  = 0;

slot_material_t *m0     = NULL;
slot_material_t *mat    = NULL;
slot_layer_t    *fill1  = NULL;
double           t0     = 0.0;
int              saves0 = 0;
int              loads0 = 0;
int              renders = 0;
int              loads  = 0;
char            *hash[3];

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[fill_tiles_disk] %s %s", ok ? "ok  " : "FAIL", name);
}

void finish() {
	printf("[fill_tiles_disk] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "FILL_TILES_DISK PASS" : "FILL_TILES_DISK FAIL");
	script_quit();
}

int full() {
	return mm_buffers_stat(11) == 0 && fill_stat(1) == 0 && fill_stat(0) > 0;
}

void next() {
	frame = frame + 1;
	if (phase == 0) {
		if (frame == 3) {
			m0 = script_get_context()->material;
			mm_library_count();
			mm_library_use(mm_library_find(1403));
			saves0 = fill_stat(5);
			loads0 = fill_stat(4);
			phase  = 1;
			frame  = 0;
		}
	}
	else if (phase == 1) {
		if (script_get_context()->material != m0 && mm_buffers_stat(11) > 0) {
			mat   = script_get_context()->material;
			fill1 = script_layer_new_fill();
			t0    = sys_time();
			phase = 2;
			frame = 0;
		}
		else if (frame > 900) {
			check(false, "the picked material compiles");
			finish();
			return;
		}
	}
	else if (phase == 2) { // Full, then saved (a moment after, and written on a thread)
		if (fill_stat(5) > saves0 || fill_stat(4) > loads0) { // Saved now, or by an earlier run of this build
			check(1, string("the full tile is on disk (%.0f s after the fill layer)", sys_time() - t0));
			phase = 25; // Its file is written on a thread
			frame = 0;
		}
		else if (sys_time() - t0 > 150.0) {
			check(false, string("the tile is saved (full %d, saved %d)", full(), fill_stat(5) - saves0));
			finish();
			return;
		}
	}
	else if (phase == 25) {
		if (frame > 120) {
			for (int k = 0; k < 3; ++k) {
				hash[k] = script_texture_hash(fill_tile_texture(mat->uid, k));
			}
			renders = fill_stat(3);
			loads   = fill_stat(4);
			fill_reset(); // The tiles go, as with an open
			phase = 3;
			frame = 0;
		}
	}
	else if (phase == 3) {
		if (fill_stat(0) > 0 && fill_stat(1) == 0 && frame > 2) {
			check(fill_stat(4) == loads + 1, string("it comes back from disk (%d frames)", frame));
			check(fill_stat(3) == renders, "without rendering it again");
			int same = 1;
			for (int k = 0; k < 3; ++k) {
				same = same && string_equals(script_texture_hash(fill_tile_texture(mat->uid, k)), hash[k]);
			}
			check(same, "the same texels");
			finish();
			return;
		}
		if (frame > 300) {
			check(false, string("it comes back (tiles %d, not full %d, loads %d)", fill_stat(0), fill_stat(1), fill_stat(4) - loads));
			finish();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	script_notify_on_next_frame(next);
}
