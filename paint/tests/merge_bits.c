#include "global.h"

// Layer merges at 16 and 32 bit (util/util_layer_merge.c, slots/slot_layer.c): the GPU passes into 16 and 32 bit
// layers use pipelines of those formats (Metal draws nothing or garbage otherwise). The same scene at 8, 16 and 32 bit:
// the legacy texture export with a masked layer, apply mask, merge down, merge group and flatten give the 8 bit results
// within its quantization.

char *root = "/tmp/materialstudio_merge_bits";

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int depth  = 0; // 0 8 bit, 1 16, 2 32

int          bits[3] = {8, 16, 32};
f32_array_t *s8; // Every sample of a depth, in the order taken
f32_array_t *s16;
f32_array_t *s32;
slot_layer_t *layer_red;
slot_layer_t *layer_green;
slot_layer_t *layer_mask;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[merge_bits] %s %s", ok ? "ok  " : "FAIL", name);
}

f32_array_t *cur() {
	return depth == 0 ? s8 : depth == 1 ? s16 : s32;
}

// An 8 x 8 grid of the snapshot (script_texture_read, script_image_read), channels from c0 to c1
void sample(int c0, int c1) {
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			for (int c = c0; c <= c1; ++c) {
				f32_array_push(cur(), script_texel(0.06 + x * 0.125, 0.06 + y * 0.125, c));
			}
		}
	}
}

void sample_texture(gpu_texture_t *t) {
	script_texture_read(t);
	sample(0, 3);
}

void sample_file(char *name) {
	char *ext = depth == 0 ? ".png" : ".exr";
	int   ok  = script_image_read(string("%s/d%d/untitled_%s%s", root, depth, name, ext));
	check(ok, string("%d bit: %s read", bits[depth], name));
	sample(0, 2);
}

void build_scene() {
	export_texture_test_options("generic", 0, 0, bits[depth], 0);
	layer_red  = script_layer_new_color(0xffcc2020, 0.7, 0.4, 0.3);
	layer_mask = script_layer_new_mask(layer_red, 1);
	int e      = script_mask_add_effect(layer_mask, 1, -1); // Fill
	script_mask_set_effect_param(layer_mask, e, 0, 0.5);
	layer_green               = script_layer_new_color(0xff20cc20, 0.5, 0.6, 0.1);
	layer_green->mask_opacity = 0.6;
	layer_green->blending     = 2; // Multiply
	script_layer_update(layer_green);
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		script_project_new();
	}
	else if (step == 1) {
		build_scene();
	}
	else if (step == 2) { // Legacy export: the masked layer composited
		char *dir = string("%s/d%d", root, depth);
		iron_create_directory(dir);
		export_texture_run(dir, 0);
		sample_file("base");
		sample_file("occ");
		sample_file("rough");
	}
	else if (step == 3) { // Apply mask, flatten, merge down
		slot_layer_t *red = script_layer_apply_mask(layer_mask);
		sample_texture(red->texpaint);
		sample_texture(red->texpaint_pack);
		slot_layer_t *flat = layers_flatten(0, NULL);
		sample_texture(flat->texpaint);
		sample_texture(flat->texpaint_pack);
		slot_layer_t *merged = script_layer_merge_down(layer_green);
		sample_texture(merged->texpaint);
		sample_texture(merged->texpaint_pack);
	}
	else if (step == 4) { // Merge group
		slot_layer_t *a = script_layer_new_color(0xff2020cc, 0.9, 0.2, 0.8);
		slot_layer_t *g = script_layer_new_group(a);
		slot_layer_t *b = script_layer_new_color(0xffcccc20, 0.3, 0.9, 0.5);
		b->mask_opacity = 0.5;
		script_layer_update(b);
		script_layer_set_group(b, g);
		slot_layer_t *merged = script_layer_merge_group(g);
		check(merged != NULL, string("%d bit: group merged", bits[depth]));
		if (merged != NULL) {
			sample_texture(merged->texpaint);
			sample_texture(merged->texpaint_pack);
		}
		if (depth < 2) {
			depth = depth + 1;
			step  = -1; // The scene again at the next depth
		}
	}
	else {
		int   n   = s8->length;
		float d16 = 0.0;
		float d32 = 0.0;
		float d   = 0.0;
		for (int i = 0; i < n; ++i) {
			float a = fabs(s16->buffer[i] - s8->buffer[i]);
			float b = fabs(s32->buffer[i] - s8->buffer[i]);
			float c = fabs(s32->buffer[i] - s16->buffer[i]);
			d16     = a > d16 ? a : d16;
			d32     = b > d32 ? b : d32;
			d       = c > d ? c : d;
		}
		int same = s16->length == n && s32->length == n;
		check(same && d16 <= 3.0 / 255.0, string("16 bit as 8 bit: %d samples, max diff %.2f/255", n, d16 * 255.0));
		check(same && d32 <= 3.0 / 255.0, string("32 bit as 8 bit: max diff %.2f/255", d32 * 255.0));
		check(same && d <= 1.0 / 255.0, string("32 bit as 16 bit: max diff %.2f/255", d * 255.0));
		export_texture_test_options("generic", 0, 0, 8, 0);
		printf("[merge_bits] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MERGE_BITS PASS" : "MERGE_BITS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = script_tmp_dir("materialstudio_merge_bits"); // Per run: suites of several worktrees run at once
	iron_create_directory(root);
	s8  = f32_array_create(0);
	s16 = f32_array_create(0);
	s32 = f32_array_create(0);
	script_notify_on_next_frame(next);
}
