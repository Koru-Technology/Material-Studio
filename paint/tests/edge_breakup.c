#include "global.h"

// Edge Breakup, a mask filter: the edge of the result below frays with noise. A white square on black: at amount 0 the
// edge stays as it is; frayed, texels go black only along the edge (deep inside stays white, nothing outside turns
// white); a wider width frays further in; another seed frays elsewhere. Its shader compiles, the GPU drops nothing.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *layer = NULL;
slot_layer_t *mask  = NULL;
int           eb    = -1;
buffer_t     *plain = NULL; // The square as painted
buffer_t     *a     = NULL;
int           lost1 = 0;

int EB = 18;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[edge_breakup] %s %s", ok ? "ok  " : "FAIL", name);
}

buffer_t *copy_result() {
	buffer_t *p = script_get_texture_pixels(slot_layer_mask_texture(mask));
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

// Sampled texels white in from and black now (lost), black in from and white now (grown); inside the inner square,
// along the edge band or outside
int lost_in    = 0;
int lost_edge  = 0;
int grown      = 0;
int differ     = 0;

void compare(buffer_t *from) {
	gpu_texture_t *t = slot_layer_mask_texture(mask);
	buffer_t      *p = script_get_texture_pixels(t);
	int            w = t->width;
	lost_in          = 0;
	lost_edge        = 0;
	grown            = 0;
	differ           = 0;
	for (int y = 0; y < w; y += 2) {
		for (int x = 0; x < w; x += 2) {
			int   i  = (y * w + x) * 4;
			int   v0 = buffer_get_u8(from, i);
			int   v1 = buffer_get_u8(p, i);
			float u  = x * 1.0 / w;
			float v  = y * 1.0 / w;
			bool  deep = u > 0.36 && u < 0.64 && v > 0.36 && v < 0.64; // The square is 0.3 to 0.7
			if (v0 - v1 > 128) {
				if (deep) {
					lost_in = lost_in + 1;
				}
				else {
					lost_edge = lost_edge + 1;
				}
			}
			if (v1 - v0 > 128) {
				grown = grown + 1;
			}
			if (v0 - v1 > 32 || v1 - v0 > 32) {
				differ = differ + 1;
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
		eb = script_mask_add_effect(mask, EB, -1); // Makes the stack: its paint, then the breakup
		check(eb >= 0 && script_mask_effect_type(mask, eb) == EB, "Edge Breakup added on top");
		script_mask_set_effect_param(mask, eb, 1, 0.0); // Amount 0
	}
	else if (step == 2) {
		plain = copy_result();
		script_mask_set_effect_param(mask, eb, 1, 1.0);
		script_mask_set_effect_param(mask, eb, 0, 12.0);
	}
	else if (step == 3) {
		compare(plain);
		check(lost_edge > 50, string("frayed: texels along the edge go (%d)", lost_edge));
		check(lost_in == 0, string("deep inside stays (%d lost)", lost_in));
		check(grown == 0, string("nothing outside grows (%d)", grown));
		lost1 = lost_edge;
		a     = copy_result();
		script_mask_set_effect_param(mask, eb, 0, 32.0);
	}
	else if (step == 4) {
		compare(plain);
		check(lost_edge > lost1, string("a wider width frays further in (%d, %d before)", lost_edge, lost1));
		script_mask_set_effect_param(mask, eb, 0, 12.0);
		script_mask_set_effect_param(mask, eb, 14, 7.0); // Seed
	}
	else if (step == 5) {
		compare(a);
		check(differ > 20, string("another seed frays elsewhere (%d texels differ)", differ));
		script_mask_set_effect_param(mask, eb, 14, 0.0);
	}
	else if (step == 6) {
		compare(a);
		check(differ < 5, string("the same seed frays the same (%d texels differ)", differ));
		script_mask_set_effect_param(mask, eb, 1, 0.0);
	}
	else if (step == 7) {
		compare(plain);
		check(differ < 5, string("amount 0 again: the edge as it is (%d texels differ)", differ));
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[edge_breakup] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EDGE_BREAKUP PASS" : "EDGE_BREAKUP FAIL");
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
