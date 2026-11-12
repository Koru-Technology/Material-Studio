#include "global.h"

// Built-in brush strokes are kept on disk: rendered once, read back on the next lookup, rendered again for other pen settings

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int rows   = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[brush_previews_cache] %s %s", ok ? "ok  " : "FAIL", name);
}

// All rows shown and written, the threads are idle
int settled() {
	return brush_previews_stat(2) == 0 && brush_previews_stat(3) == 0;
}

void report() {
	printf("[brush_previews_cache] loaded %d rendered %d not ready %d unwritten %d", brush_previews_stat(0), brush_previews_stat(1), brush_previews_stat(2),
	       brush_previews_stat(3));
}

void next() {
	frame = frame + 1;
	if (frame > 1200) {
		report();
		check(0, "the rows settle");
		step = 99;
	}
	if (step < 99 && (frame < 4 || !settled())) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;
	report();

	if (step == 0) {
		// Whatever the cache held, start from an empty one
		brush_previews_forget(1);
	}
	else if (step == 1) {
		check(brush_previews_stat(1) == rows, "an empty cache renders every row");
		check(brush_previews_stat(0) == 0, "an empty cache loads no row");
		brush_previews_forget(0);
	}
	else if (step == 2) {
		check(brush_previews_stat(0) == rows, "the written rows are read back");
		check(brush_previews_stat(1) == 0, "no row renders again");
		config_t *cfg = script_get_config();
		cfg->dyn_size = cfg->dyn_size == 1 ? 0 : 1;
		brush_previews_forget(0);
	}
	else if (step == 3) {
		check(brush_previews_stat(0) + brush_previews_stat(1) == rows, "other pen settings show every row");
	}
	else {
		printf("[brush_previews_cache] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "BRUSH_PREVIEWS_CACHE PASS" : "BRUSH_PREVIEWS_CACHE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	script_brush_defaults();
	rows = brush_previews_stat(4);
	script_notify_on_next_frame(next);
}
