#include "global.h"

// Height raise of a layer (slot_layer_t.height_raise): height added where the layer has paint, times its coverage, so
// its strokes bulge. A square of paint (u and v 0.25 to 0.75) of height 0 over a base of height 0: at raise 0 the
// composite is flat; raised, its height is the raise inside and 0 outside, the normals tilt at its edge in the flattened
// layers and in the exported normal map (height folded into it, at any resolution the same slope), and the viewport
// draws it (soft stripes over the whole layer). A soft edge, a ramp of coverage over 1/64 of the width, tilts the exported normals as much at 4096 as at
// 2048. Undo and redo, save and load keep it; merge down bakes it into the merged layer's height.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *base   = NULL;
slot_layer_t *thread = NULL;
char         *root   = NULL;
char         *file   = NULL;

float RAISE = 0.5;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[height_raise] %s %s", ok ? "ok  " : "FAIL", name);
}

char *dir(char *name) {
	char *d = string("%s/%s", root, name);
	iron_create_directory(d);
	return d;
}

// Height of the flattened layers at u, v
float flat_height(float u, float v) {
	slot_layer_t *flat = layers_flatten(0, NULL);
	script_texture_read(flat->texpaint_pack);
	return script_texel(u, v, 3);
}

// Largest tilt (|x - 0.5| of the read normals) on the row v across the square's left edge, and at its middle
float edge_tilt() {
	float m = 0.0;
	for (int i = -12; i <= 12; ++i) {
		float d = fabs(script_texel(0.25 + i / 2048.0, 0.5, 0) - 0.5);
		m       = d > m ? d : m;
	}
	return m;
}

float middle_tilt() {
	return fabs(script_texel(0.5, 0.5, 0) - 0.5);
}

// Paint of the thread from u 0.5 on, its coverage a ramp over the first 1/parts of the width
void draw_ramp(int parts) {
	gpu_texture_t *t = thread->texpaint;
	int            n = t->width / parts;
	draw_begin(t, 1, 0x00000000);
	for (int i = 0; i < n; ++i) {
		draw_set_color(((i * 255 / n) << 24) | 0xc0a080);
		draw_filled_rect(t->width * 0.5 + i, 0.0, 1.0, t->height * 1.0);
	}
	draw_set_color(0xffc0a080);
	draw_filled_rect(t->width * 0.5 + n, 0.0, t->width * 0.5 - n, t->height * 1.0);
	draw_end();
	script_layer_update(thread);
}

// Paint of the thread in soft stripes over the whole layer, its coverage a triangle wave along u of period width / 16:
// every face of the mesh shows slopes
void draw_stripes() {
	gpu_texture_t *t    = thread->texpaint;
	int            half = t->width / 32;
	draw_begin(t, 1, 0x00000000);
	for (int x = 0; x < t->width; ++x) {
		int k = x % (half * 2);
		int a = k < half ? k * 255 / half : (half * 2 - k) * 255 / half;
		draw_set_color((a << 24) | 0xc0a080);
		draw_filled_rect(x * 1.0, 0.0, 1.0, t->height * 1.0);
	}
	draw_end();
	script_layer_update(thread);
}

float export_ramp_tilt(char *name) {
	char *d = dir(name);
	export_texture_run(d, 0);
	int ok = script_image_read(string("%s/untitled_nor.png", d));
	check(ok, string("%s: normal map exported", name));
	return ok ? fabs(script_texel(0.5 + 1.0 / 128.0, 0.5, 0) - 0.5) : -1.0;
}

float flat_edge_tilt() {
	slot_layer_t *flat = layers_flatten(1, NULL);
	script_texture_read(flat->texpaint_nor);
	return edge_tilt();
}

float export_edge_tilt(char *name) {
	char *d = dir(name);
	export_texture_run(d, 0);
	int ok = script_image_read(string("%s/untitled_nor.png", d));
	check(ok, string("%s: normal map exported", name));
	return ok ? edge_tilt() : -1.0;
}

void next() {
	frame = frame + 1;
	if (frame < 5) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		base = script_layer_new_color(0xff808080, 1.0, 0.5, 0.0);
		script_layer_set_name(base, "base");
		thread = script_layer_new_color(0xffc0a080, 1.0, 0.5, 0.0);
		script_layer_set_name(thread, "thread");
		draw_begin(base->texpaint_pack, 1, 0x00ff8000); // Height 0
		draw_end();
		draw_begin(thread->texpaint_pack, 1, 0x00ff8000);
		draw_end();
		gpu_texture_t *t = thread->texpaint; // Paint in the square only
		draw_begin(t, 1, 0x00000000);
		draw_set_color(0xffc0a080);
		draw_filled_rect(t->width * 0.25, t->height * 0.25, t->width * 0.5, t->height * 0.5);
		draw_end();
		script_layer_update(base);
		script_layer_update(thread);
		export_texture_test_options("generic", 0, 0, 8, 0);
	}
	else if (step == 1) {
		check(thread->height_raise == 0.0, "a new layer has no raise");
		check(flat_height(0.5, 0.5) < 0.02, "not raised: no height in the square");
		check(export_edge_tilt("flat") < 0.02, "not raised: the exported normals stay flat at its edge");
	}
	else if (step == 2) {
		check(app_layer_set_raise(thread->id, RAISE), "raise set");
		check(!app_layer_set_raise(base->id - 100000, RAISE), "no layer: refused");
	}
	else if (step == 3) {
		float h = flat_height(0.5, 0.5);
		check(fabs(h - RAISE) < 0.02, string("raised: the square's height is the raise (%.3f)", h));
		check(flat_height(0.1, 0.1) < 0.02, "outside the square: no height");
		float t = flat_edge_tilt();
		check(t > 0.2, string("the flattened normals tilt at the square's edge (%.3f)", t));
		script_texture_read(layers_flatten(1, NULL)->texpaint_nor);
		check(middle_tilt() < 0.02, "and stay flat in its middle");
		float e = export_edge_tilt("raised");
		check(e > 0.2, string("the exported normal map tilts at the edge (%.3f)", e));
		check(middle_tilt() < 0.02, "and is flat in the middle");
	}
	else if (step == 4) {
		draw_ramp(64);
	}
	else if (step == 5) { // Twice the export resolution: the same slope per uv, not per texel
		float a = export_ramp_tilt("ramp2k");
		export_tiled_set_resolution(4096, 4096);
		float b = export_ramp_tilt("ramp4k");
		export_tiled_set_resolution(0, 0);
		check(a > 0.1 && a < 0.4, string("a soft edge tilts the normals without flattening them out (%.3f)", a));
		check(fabs(a - b) < 0.03, string("as much at 4096 as at 2048 (%.3f, %.3f)", b, a));
		gpu_texture_t *t = thread->texpaint; // The square again
		draw_begin(t, 1, 0x00000000);
		draw_set_color(0xffc0a080);
		draw_filled_rect(t->width * 0.25, t->height * 0.25, t->width * 0.5, t->height * 0.5);
		draw_end();
		script_layer_update(thread);
		history_undo();
	}
	else if (step == 6) {
		check(thread->height_raise == 0.0, "undo: no raise");
		check(flat_height(0.5, 0.5) < 0.02, "undo: no height");
		history_redo();
	}
	else if (step == 7) {
		check(thread->height_raise == RAISE, "redo: raised again");
		file = string("%s/height_raise_test.arm", root);
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 8) {
		script_project_new();
	}
	else if (step == 9) {
		script_project_open(file);
	}
	else if (step == 10) {
		thread = script_layer_get("thread");
		base   = script_layer_get("base");
		check(thread != NULL && base != NULL, "loaded");
		check(thread != NULL && thread->height_raise == RAISE, "the raise is saved");
		check(base != NULL && base->height_raise == 0.0, "the other layer has none");
	}
	else if (step == 11) {
		float before        = flat_height(0.5, 0.5);
		slot_layer_t *merged = script_layer_merge_down(thread);
		check(merged != NULL, "merged down");
		if (merged != NULL) {
			check(merged->height_raise == 0.0, "the merged layer has no raise left");
			script_texture_read(merged->texpaint_pack);
			float in  = script_texel(0.5, 0.5, 3);
			float out = script_texel(0.1, 0.1, 3);
			check(fabs(in - RAISE) < 0.02, string("the raise is baked into its height (%.3f)", in));
			check(out < 0.02, string("only where the raised layer had paint (%.3f)", out));
			float after = flat_height(0.5, 0.5);
			check(fabs(after - before) < 0.02, string("the composite height is kept (%.3f, before %.3f)", after, before));
		}
		history_undo();
	}
	else if (step == 12) {
		thread = script_layer_get("thread");
		check(thread != NULL && thread->height_raise == RAISE, "undo of the merge: the layer and its raise are back");
		draw_stripes();
		app_layer_set_raise(thread->id, 1.0);
	}
	else if (step == 13) {
		script_screenshot(string("%s/raised.png", root));
	}
	else if (step == 14) {
		app_layer_set_raise(thread->id, 0.0);
	}
	else if (step == 15) {
		script_screenshot(string("%s/flat.png", root));
	}
	else if (step == 16) {
		int n = script_diff_images(string("%s/flat.png", root), string("%s/raised.png", root), 6.0);
		check(n > 20000, string("the viewport shows the raise (%d values change)", n));
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[height_raise] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "HEIGHT_RAISE PASS" : "HEIGHT_RAISE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = script_tmp_dir("materialstudio_height_raise");
	iron_create_directory(root);
	script_brush_defaults();
	script_project_new();
	script_notify_on_next_frame(next);
}
