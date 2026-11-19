#include "global.h"

// Layer cache (render/layer_cache.c): the viewport looks the same whichever layer is selected, the layers under it
// from the cache or all composited live (cached fill layers a little softer, at the layer resolution). An unchanged scene makes the cache once, a cached layer's new opacity makes it
// again and shows. Then the camera orbits with the bottom layer selected (no cache) and with the top one (the cache),
// for a GPU trace to compare. Opens a project with many layers (PROJECT) and saves nothing.

char *PROJECT = "Projects/magicbook_composite/magicbook_composite.arm";
int   SIZE    = 512;
int   SETTLE  = 120; // Frames for fill tiles to sharpen and TAA to settle
int   ORBIT   = 480; // Frames of each orbit

int            fails     = 0;
int            checks    = 0;
int            step      = 0;
int            wait      = 0;
int            frame     = 0;
gpu_texture_t *shot      = NULL;
buffer_t      *live      = NULL;
buffer_t      *live2     = NULL;
int            top_id    = -1;
int            bottom_id = -1;
int            low_id    = -1; // A cached layer that is visible
int            bakes     = 0;
int            noise     = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[layer_cache] %s %s", ok ? "ok  " : "FAIL", name);
}

// The last frame, drawn without the gizmo and decal box of the selected layer (capturing_screenshot): the next frames
// are drawn without them too, for the next capture
buffer_t *capture(char *name) {
	viewport_capture_screenshot_to(shot, 0.0, 0.0, SIZE * 1.0, SIZE * 1.0);
	script_get_context()->capturing_screenshot = 1;
	buffer_t *p = script_get_texture_pixels(shot);
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	iron_write_png(string("%slayer_cache_%s.png", script_dir(), name), c, SIZE, SIZE, 0);
	return c;
}

// Pixels of a and b with a channel more than t apart
int differ(buffer_t *a, buffer_t *b, int t) {
	int n = 0;
	for (int i = 0; i < SIZE * SIZE; ++i) {
		int off = 0;
		for (int k = 0; k < 3; ++k) {
			int d = buffer_get_u8(a, i * 4 + k) - buffer_get_u8(b, i * 4 + k);
			if (d > t || d < -t) {
				off = 1;
			}
		}
		n = n + off;
	}
	return n;
}

// Fill tiles done sharpening, fills rasterized: until then each frame can look sharper, and the cache is made again
int settled() {
	return fill_stat(1) == 0 && fill_stat(2) == 0 && mm_buffers_stat(11) == 0;
}

void next() {
	if (wait > 0 || !settled()) {
		if (wait > 0) {
			wait = wait - 1;
		}
		script_notify_on_next_frame(next);
		return;
	}

	if (step == 0) {
		int n     = app_layer_count();
		top_id    = app_layer_id_at(0);
		bottom_id = app_layer_id_at(n - 1);
		printf("[layer_cache] %d layers, ids %d (first) .. %d (last)", n, top_id, bottom_id);
		if (n < 4) {
			check(0, string("the project opened (%d layers)", n));
			printf("LAYER_CACHE FAIL");
			script_quit();
			return;
		}
		app_layer_select(top_id);
		if (!layer_cache_active()) { // The list may be bottom first
			int t     = top_id;
			top_id    = bottom_id;
			bottom_id = t;
		}
		app_layer_select(bottom_id);
		check(!layer_cache_active(), "the bottom layer selected: no cache");
		script_get_context()->capturing_screenshot = 1; // Captures compare the scene, not the selected layer's gizmo
		script_get_context()->ddirty               = 2;
		wait                                       = SETTLE;
	}
	else if (step == 1) {
		live = capture("live");
		wait = 2;
	}
	else if (step == 2) {
		live2 = capture("live2");
		noise = differ(live, live2, 12);
		printf("[layer_cache] two live frames: %d pixels differ", noise);
		app_layer_select(top_id);
		check(layer_cache_active(), "the top layer selected: the cache is in use");
		int nc = 0;
		for (int i = 0; i < script_layer_count(); ++i) {
			slot_layer_t *l = script_layer_at(i);
			if (layer_cache_has(l)) {
				nc = nc + 1;
			}
		}
		printf("[layer_cache] %d layers in the cache", nc);
		wait = SETTLE;
	}
	else if (step == 3) {
		buffer_t *c = capture("cached");
		// Cached fills are at the layer resolution, their finest grain a little softer than live: small differences
		int n = differ(live, c, 12);
		int m = differ(live, c, 40);
		printf("[layer_cache] cached against live: %d of %d pixels differ, %d by more than 40", n, SIZE * SIZE, m);
		check(m <= SIZE * SIZE / 100, "the cache shows what the live composite does");
		bakes = layer_cache_bake_count();
		wait  = 120;
	}
	else if (step == 4) {
		int b = layer_cache_bake_count() - bakes;
		check(b == 0, string("an unchanged scene makes no new bake in 120 frames (%d)", b));
		// The second cached layer from the bottom
		int cached = 0;
		low_id     = -1;
		for (int i = 0; i < script_layer_count() && low_id < 0; ++i) {
			slot_layer_t *l = script_layer_at(i);
			if (layer_cache_has(l)) {
				cached = cached + 1;
				if (cached == 2) {
					low_id = l->id;
				}
			}
		}
		check(low_id >= 0, "two layers in the cache");
		app_layer_set_opacity(low_id, 0.2); // Selects that layer, as the slider does
		app_layer_select(top_id);
		check(layer_cache_active() && layer_cache_has(script_layer_at(1)), "the top layer selected again: the cache is in use");
		bakes = layer_cache_bake_count();
		wait  = SETTLE;
	}
	else if (step == 5) {
		check(layer_cache_bake_count() > bakes, "a cached layer's opacity makes the cache again");
		live = capture("cached_opacity");
		app_layer_select(bottom_id);
		wait = SETTLE;
	}
	else if (step == 6) {
		buffer_t *c = capture("live_opacity");
		int       m = differ(live, c, 40);
		printf("[layer_cache] after the opacity change, cached against live: %d pixels differ by more than 40", m);
		check(m <= SIZE * SIZE / 100, "the new opacity shows as it does live");
		app_layer_set_opacity(low_id, 1.0);
		app_layer_select(bottom_id);
		script_get_context()->capturing_screenshot = 0;
		printf("[layer_cache] ORBIT LIVE start");
		frame = 0;
		wait  = 30;
	}
	else if (step == 7) { // Orbit, no cache
		viewport_orbit(0.01, 0.0);
		frame = frame + 1;
		if (frame < ORBIT) {
			script_notify_on_next_frame(next);
			return;
		}
		printf("[layer_cache] ORBIT CACHED start");
		app_layer_select(top_id);
		frame = 0;
		wait  = 30;
	}
	else if (step == 8) { // Orbit, the cache: the camera is not something it reads
		if (frame == 0) {
			bakes = layer_cache_bake_count();
		}
		viewport_orbit(0.01, 0.0);
		frame = frame + 1;
		if (frame < ORBIT) {
			script_notify_on_next_frame(next);
			return;
		}
		int b = layer_cache_bake_count() - bakes;
		check(b == 0, string("orbiting makes no new bake (%d)", b));
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[layer_cache] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "LAYER_CACHE PASS" : "LAYER_CACHE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	shot = gpu_create_render_target(SIZE, SIZE, 0);
	script_project_open(string("%s../%s", script_dir(), PROJECT)); // script_dir: .../Documents/.tests/
	wait = 30;
	script_notify_on_next_frame(next);
}
