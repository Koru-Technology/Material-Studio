#include "global.h"

// Height Blend, a mask effect: the result below it gains where the mask's layer is higher than what it is compared with.
// The top layer's height rises from 0 to 1 along u over a gray mask: a flat level of 0.5 cuts it at the middle, offset
// moves the cut, a layer below of height 0.8 cuts it at 0.8, as does the composite of the layers below; that composite
// follows a change of the layer below by itself. Reach lets the edge of a half white mask spread out. Undo, save and
// load keep the effect and the layer it compares with, the layer's own viewport shader compiles.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *skin  = NULL; // Below, height 0.8
slot_layer_t *fur   = NULL; // Above, its height a ramp along u
slot_layer_t *mask  = NULL;
int           g     = -1; // The Height Blend, stack index
int           f     = -1; // The fill under it
buffer_t     *a     = NULL;
char         *file  = NULL;
char         *saved = NULL;
int           r0    = 0;

int FILL = 1;
int HB   = 17;

int INFLUENCE = 0;
int SHARPNESS = 1;
int OFFSET    = 2;
int REACH     = 3;
int COMPARE   = 4;
int LAYER     = 5;

int FLAT  = 0;
int BELOW = 1;
int ONE   = 2;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[height_blend] %s %s", ok ? "ok  " : "FAIL", name);
}

char *test_path(char *name) {
	return string("%s/%s", script_save_path(), name);
}

// 256x256 white image, its alpha: kind 0 a ramp across u, 1 opaque on the left half only
void write_image(char *path, int kind) {
	int       w = 256;
	buffer_t *p = buffer_create(w * w * 4);
	for (int y = 0; y < w; ++y) {
		for (int x = 0; x < w; ++x) {
			int i = (y * w + x) * 4;
			buffer_set_u8(p, i, 255);
			buffer_set_u8(p, i + 1, 255);
			buffer_set_u8(p, i + 2, 255);
			buffer_set_u8(p, i + 3, kind == 0 ? x : x < 128 ? 255 : 0);
		}
	}
	iron_write_png(path, p, w, w, 0);
}

int asset(char *name) {
	return script_asset_index(name);
}

// Height (pack alpha) of l: cleared to color (0xaarrggbb, the height in aa), then the image whose file has name in it
void set_height(slot_layer_t *l, int color, char *name) {
	gpu_texture_t *t = l->texpaint_pack;
	draw_begin(t, 1, color);
	if (name != NULL) {
		draw_scaled_image(project_get_image(script_get_asset(asset(name))), 0.0, 0.0, t->width * 1.0, t->height * 1.0);
	}
	draw_end();
	mask_stack_layer_changed(l);
}

gpu_texture_t *result() {
	return slot_layer_mask_texture(mask);
}

// Texels with red in [lo, hi] in the columns of percents [x0, x1), every other column and fourth row. The ramp image
// drawn stretched blends its first and last columns with the other side (repeat): the checks keep off them.
int count_box(gpu_texture_t *t, int x0, int x1, int lo, int hi) {
	buffer_t *p = script_get_texture_pixels(t);
	int       w = t->width;
	int       h = t->height;
	int       n = 0;
	for (int y = 0; y < h; y += 4) {
		for (int x = w * x0 / 100; x < w * x1 / 100; x += 2) {
			int v = buffer_get_u8(p, (y * w + x) * 4);
			if (v >= lo && v <= hi) {
				n = n + 1;
			}
		}
	}
	return n;
}

int white(int x0, int x1) {
	return count_box(result(), x0, x1, 200, 255);
}

// Every sampled texel of the columns [x0, x1) white
int all_white(int x0, int x1) {
	return count_box(result(), x0, x1, 200, 255) == count_box(result(), x0, x1, 0, 255);
}

int none_white(int x0, int x1) {
	return white(x0, x1) == 0;
}

buffer_t *copy_pixels(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

int changed(buffer_t *b) {
	gpu_texture_t *t = result();
	buffer_t      *p = script_get_texture_pixels(t);
	int            n = 0;
	for (int i = 0; i < p->length; i += 32) {
		int d = buffer_get_u8(p, i) - buffer_get_u8(b, i);
		if (d > 10 || d < -10) {
			n = n + 1;
		}
	}
	return n;
}

void set(int param, float v) {
	script_mask_set_effect_param(mask, g, param, v);
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		write_image(test_path("koru_hb_ramp.png"), 0);
		write_image(test_path("koru_hb_half.png"), 1);
		script_import_asset(test_path("koru_hb_ramp.png"), 0);
		script_import_asset(test_path("koru_hb_half.png"), 0);
		skin = script_layer_new_color(0xff806040, 1.0, 0.5, 0.0);
		script_layer_set_name(skin, "skin");
		fur = script_layer_new_color(0xff302010, 1.0, 0.5, 0.0);
		script_layer_set_name(fur, "fur");
		mask = script_layer_new_mask(fur, 0); // Black
		script_layer_set_name(mask, "fur_mask");
	}
	else if (step == 1) {
		set_height(skin, 0xccffffff, NULL);         // 0.8
		set_height(fur, 0x00ffffff, "koru_hb_ramp"); // 0 .. 1 along u
		f = script_mask_add_effect(mask, FILL, -1); // Gray
		script_mask_set_effect_param(mask, f, 0, 0.5);
		g = script_mask_add_effect(mask, HB, -1);
		check(script_mask_effect_type(mask, g) == HB && mask_stack_has_height_blend(mask), "height blend on top of the stack");
		check(script_mask_get_effect_param(mask, g, COMPARE) == 1.0, "compares with the layers below by default");
		set(REACH, 0.0);
		set(COMPARE, FLAT * 1.0);
	}
	else if (step == 2) {
		check(none_white(2, 45), "flat: no white where the layer is below 0.5");
		check(all_white(60, 98), "flat: white where it is above");
		set(OFFSET, 0.3);
	}
	else if (step == 3) {
		check(all_white(30, 45), "offset 0.3: the cut moves to 0.2");
		check(none_white(2, 15), "and below it stays black");
		set(OFFSET, 0.0);
		set(COMPARE, ONE * 1.0);
		set(LAYER, skin->id * 1.0);
	}
	else if (step == 4) {
		check(none_white(60, 78), "a layer of height 0.8: black below 0.8");
		check(all_white(90, 98), "white above");
		a = copy_pixels(result());
		set(COMPARE, BELOW * 1.0);
	}
	else if (step == 5) {
		int n = changed(a);
		check(n < 50, string("the layers below (the skin alone) give what the skin gives (%d texels differ)", n));
		set_height(skin, 0x66ffffff, NULL); // 0.4, nothing else told
	}
	else if (step == 6) {
		check(all_white(50, 98) && none_white(2, 35), "the layer below lowered: the cut follows by itself");
		history_undo(); // Compare: below
	}
	else if (step == 7) {
		check(script_mask_get_effect_param(mask, g, COMPARE) == ONE * 1.0, "undo brings back the comparison with one layer");
		set(COMPARE, FLAT * 1.0);
		// Reach: a mask white on the left half, the layer high everywhere: strands reach past the edge
		script_mask_set_effect_param(mask, f, 1, 1.0);
		script_mask_set_effect_param(mask, f, 2, asset("koru_hb_half") * 1.0);
		set_height(fur, 0xffffffff, NULL);
		set(INFLUENCE, 1.0);
	}
	else if (step == 8) {
		a = copy_pixels(result());
		check(white(0, 40) > 0, "the white half shows");
		r0 = white(50, 100);
		set(REACH, 64.0);
	}
	else if (step == 9) {
		int n = changed(a);
		check(n > 100, string("reach widens the edge (%d texels change)", n));
		check(white(50, 100) > r0 + 50, string("white reaches past the mask's edge (%d from %d)", white(50, 100), r0));
		set(COMPARE, ONE * 1.0);
		set(LAYER, skin->id * 1.0);
		saved = string_copy(script_mask_effects_string(mask));
		a     = copy_pixels(result());
		file  = test_path("height_blend_test.arm");
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 10) {
		script_project_new();
	}
	else if (step == 11) {
		script_project_open(file);
	}
	else if (step == 12) {
		mask = script_layer_get("fur_mask");
		skin = script_layer_get("skin");
		check(mask != NULL && skin != NULL, "loaded");
		if (mask != NULL && skin != NULL) {
			check(script_mask_get_effect_param(mask, g, LAYER) == skin->id * 1.0, "the compared layer is found again");
			check(script_mask_get_effect_param(mask, g, REACH) == 64.0, "settings kept");
			int n = changed(a);
			check(n < 200, string("the loaded mask gives the same pixels (%d texels differ)", n));
		}
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[height_blend] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "HEIGHT_BLEND PASS" : "HEIGHT_BLEND FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	script_notify_on_next_frame(next);
}
