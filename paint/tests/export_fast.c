#include "global.h"

// A hi-res export of strokes painted with a heavy Material Maker material (Old Brick Wall, 784) replays them
// composite-fast (paint_replay.c, util_material_cache.c): per tile the material is rendered once into a replay cache,
// in bands with a commit between them, and the strokes read it. 24 strokes at 1024 export at 2048 in 1024 px tiles, over
// frames as the export dialog runs it (export_tiled_step, 40 ms): once with replay caches off (every stroke evaluates the
// material for every dab), once with them on. The files are byte-equal; with the caches the longest frame and the whole
// export take less time.

char *root = "";

int    fails  = 0;
int    checks = 0;
int    phase  = 0;
int    frame  = 0;
double t0     = 0.0;
float  max_ft = 0.0;
int    frames = 0;

float slow_max   = 0.0; // Replay caches off
float slow_total = 0.0;
int   slow_frames = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_fast] %s %s", ok ? "ok  " : "FAIL", name);
}

char *fresh_dir(char *name) {
	char *d = string("%s/%s", root, name);
	iron_create_directory(d);
	any_array_t *old = file_read_directory(d);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", d, old->buffer[i]));
	}
	return d;
}

char *file(char *dir, char *tex) {
	return string("%s/%s/untitled_%s.png", root, dir, tex);
}

void finish() {
	export_tiled_set_resolution(0, 0);
	export_tiled_test_tile(0);
	printf("[export_fast] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "EXPORT_FAST PASS" : "EXPORT_FAST FAIL");
	script_quit();
}

void start(char *dir, int fast) {
	material_cache_replay_set_enabled(fast);
	export_texture_test_options("generic", 0, 0, 8, 0);
	export_tiled_test_tile(1024);
	export_tiled_set_resolution(2048, 2048);
	check(export_tiled_start(fresh_dir(dir), 0), string("the export %s starts", dir));
	t0     = sys_time();
	max_ft = 0.0;
	frames = 0;
}

// A frame of the export: true once it is over
int step() {
	float ft = script_frame_time();
	if (frames > 0 && ft > max_ft) {
		max_ft = ft;
	}
	frames = frames + 1;
	double t = sys_time();
	int    r = export_tiled_step(40.0);
	printf("[export_fast]   frame %d: %.0f ms, its step %.0f ms, progress %.2f", frames, ft * 1000.0, (sys_time() - t) * 1000.0, export_tiled_progress());
	return r;
}

void next() {
	frame = frame + 1;
	if (phase == 0) {
		if (frame == 3) {
			project_test_set_res(1024, 1024);
			mm_library_count();
			int i = mm_library_find(784);
			printf("[export_fast] picking %s", mm_library_name(i));
			mm_library_use(i);
			t0    = sys_time();
			phase = 1;
			frame = 0;
		}
	}
	else if (phase == 1) {
		if (frame > 3 && mm_buffers_stat(11) == 0 && make_material_paint_stat(0) == 0) {
			app_layer_new(0);
			make_paint_set_batching(1);
			for (int s = 0; s < 24; ++s) {
				render_path_paint_set_stamp_seed(100 + s);
				for (int i = 0; i < 20; ++i) {
					script_paint(0.15 + i * 0.035, 0.12 + s * 0.03 + (i % 5) * 0.006);
				}
				script_paint_end();
			}
			paint_rec_flush();
			phase = 2;
			frame = 0;
		}
		else if (sys_time() - t0 > 120.0) {
			check(false, "Old Brick Wall's buffers become full");
			finish();
			return;
		}
	}
	else if (phase == 2) {
		if (frame == 3) {
			start("slow", 0);
			phase = 3;
		}
	}
	else if (phase == 3) {
		if (step()) {
			slow_total  = sys_time() - t0;
			slow_max    = max_ft;
			slow_frames = frames;
			printf("[export_fast] evaluating the material: %.2f s over %d frames, longest frame %.0f ms", slow_total, slow_frames, slow_max * 1000.0);
			phase = 4;
			frame = 0;
		}
	}
	else if (phase == 4) {
		if (frame == 3) {
			start("fast", 1);
			phase = 5;
		}
	}
	else if (phase == 5) {
		if (step()) {
			float total = sys_time() - t0;
			printf("[export_fast] composite-fast: %.2f s over %d frames, longest frame %.0f ms; %d replay caches rendered, %d bands, longest %.1f ms of GPU",
			       total, frames, max_ft * 1000.0, material_cache_replay_stat(1), material_cache_replay_stat(4), material_cache_replay_stat(5) / 1000.0);
			check(export_tiled_stat(9) > 0 && export_tiled_stat(10) == 0, string("paint layer tiles materialized (%d, %d fell back)", export_tiled_stat(9), export_tiled_stat(10)));
			check(material_cache_replay_stat(1) >= 4, string("a replay cache per tile (%d)", material_cache_replay_stat(1)));
			check(material_cache_replay_stat(0) == 0 || material_cache_replay_stat(8) == 0, "the replay caches are freed when the export ends");
			int db = script_diff_images(file("fast", "base"), file("slow", "base"), 0.0);
			int dn = script_diff_images(file("fast", "nor"), file("slow", "nor"), 0.0);
			int dr = script_diff_images(file("fast", "rough"), file("slow", "rough"), 0.0);
			check(db == 0 && dn == 0 && dr == 0, string("the files are byte-equal (%d, %d, %d values differ)", db, dn, dr));
			check(max_ft < slow_max, string("the longest frame is shorter (%.0f ms against %.0f ms)", max_ft * 1000.0, slow_max * 1000.0));
			check(total < slow_total, string("the export takes less time (%.2f s against %.2f s)", total, slow_total));
			material_cache_replay_set_enabled(1);
			finish();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	material_cache_set_enabled(0); // No live cache building on a thread when the script quits
	root = script_tmp_dir("materialstudio_export_fast");
	iron_create_directory(root);
	script_brush_defaults();
	script_project_new();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.25;
	ctx->brush_spacing = 0.05;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
