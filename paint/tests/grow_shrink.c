#include "global.h"

// Grow / Shrink, a mask filter: the result below grown or shrunk by a distance, hard edged. A white square on black:
// grown, texels turn white only outside it and none go black, further for a larger distance, more with square corners
// than round ones, and the new border is hard (few gray texels, not a blur); shrunk, texels go black only along its edge,
// deep inside stays and nothing grows. Its shaders compile, the GPU drops nothing.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *layer = NULL;
slot_layer_t *mask  = NULL;
int           g     = -1;
buffer_t     *plain = NULL; // The square as painted
int           grown8 = 0;
int           round16 = 0;

int GROW = 19;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[grow_shrink] %s %s", ok ? "ok  " : "FAIL", name);
}

buffer_t *copy_result() {
	buffer_t *p = script_get_texture_pixels(slot_layer_mask_texture(mask));
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

// Texels black in from and white now (grown), white in from and black now (lost, deep inside or along the edge), and
// gray now among the changed ones (soft)
int grown     = 0;
int lost_in   = 0;
int lost_edge = 0;
int soft      = 0;

void compare(buffer_t *from) {
	gpu_texture_t *t = slot_layer_mask_texture(mask);
	buffer_t      *p = script_get_texture_pixels(t);
	int            w = t->width;
	grown            = 0;
	lost_in          = 0;
	lost_edge        = 0;
	soft             = 0;
	for (int y = 0; y < w; y += 2) {
		for (int x = 0; x < w; x += 2) {
			int   i    = (y * w + x) * 4;
			int   v0   = buffer_get_u8(from, i);
			int   v1   = buffer_get_u8(p, i);
			float u    = x * 1.0 / w;
			float v    = y * 1.0 / w;
			bool  deep = u > 0.36 && u < 0.64 && v > 0.36 && v < 0.64; // The square is 0.3 to 0.7
			if (v1 - v0 > 128) {
				grown = grown + 1;
			}
			if (v0 - v1 > 128) {
				if (deep) {
					lost_in = lost_in + 1;
				}
				else {
					lost_edge = lost_edge + 1;
				}
			}
			if ((v1 - v0 > 16 || v0 - v1 > 16) && v1 > 48 && v1 < 208) {
				soft = soft + 1;
			}
		}
	}
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		layer = script_layer_new_color(0xffc0a040, 1.0, 0.3, 1.0);
		mask  = script_layer_new_mask(layer, 0); // Black
		gpu_texture_t *t = mask->texpaint;
		draw_begin(t, 1, 0xff000000);
		draw_set_color(0xffffffff);
		draw_filled_rect(t->width * 0.3, t->height * 0.3, t->width * 0.4, t->height * 0.4);
		draw_end();
		mask_stack_layer_changed(mask);
	}
	else if (step == 1) {
		plain = copy_result();
		g     = script_mask_add_effect(mask, GROW, -1); // On top of its paint
		check(g >= 0 && script_mask_effect_type(mask, g) == GROW, "Grow / Shrink added on top");
		script_mask_set_effect_param(mask, g, 0, 8.0);
	}
	else if (step == 2) {
		compare(plain);
		check(grown > 50, string("grown: texels outside the square turn white (%d)", grown));
		check(lost_in + lost_edge == 0, string("none go black (%d)", lost_in + lost_edge));
		check(soft * 4 < grown, string("a hard edge, not a blur (%d gray of %d grown)", soft, grown));
		grown8 = grown;
		script_mask_set_effect_param(mask, g, 0, 16.0);
	}
	else if (step == 3) {
		compare(plain);
		check(grown > grown8 * 3 / 2, string("a larger distance grows further (%d, %d before)", grown, grown8));
		round16 = grown;
		script_mask_set_effect_param(mask, g, 2, 1.0); // Square corners
	}
	else if (step == 4) {
		compare(plain);
		check(grown > round16, string("square corners cover more than round ones (%d, %d round)", grown, round16));
		script_mask_set_effect_param(mask, g, 2, 0.0);
		script_mask_set_effect_param(mask, g, 1, 1.0); // Shrink
		script_mask_set_effect_param(mask, g, 0, 8.0);
	}
	else if (step == 5) {
		compare(plain);
		check(lost_edge > 50, string("shrunk: texels along the edge go black (%d)", lost_edge));
		check(lost_in == 0, string("deep inside stays (%d lost)", lost_in));
		check(grown == 0, string("nothing grows (%d)", grown));
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[grow_shrink] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "GROW_SHRINK PASS" : "GROW_SHRINK FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	script_notify_on_next_frame(next);
}
