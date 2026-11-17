#include "global.h"

// Mask effects: 3D Noise patterns, coverage and seed; Light follows its direction; Thickness follows a registered map and
// is black without one; Histogram Scan values; Blur softens a hard edge; ID Selection picks parts of the ID map; Anchor
// reads another layer's mask or opacity, follows its strokes, goes black when it is deleted and back on undo, settles in
// a cycle, and keeps its layer through save and load and within a smart material.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *layer = NULL;
slot_layer_t *mask  = NULL;
slot_layer_t *src   = NULL; // Anchored: a layer with a painted mask
slot_layer_t *srcm  = NULL;
slot_layer_t *paint = NULL; // Anchored: an empty paint layer
slot_layer_t *root  = NULL;
int           g     = -1; // Generator index in the stack
int           e1    = -1;
int           e2    = -1;
int           w0    = 0;
int           id0   = 0;
int           rev0  = 0;
int           rev1  = 0;
char         *file  = NULL;
buffer_t     *clouds = NULL;
buffer_t     *cells  = NULL;
buffer_t     *scr    = NULL;
buffer_t     *streak = NULL;
buffer_t     *dirp   = NULL;
buffer_t     *dirn   = NULL;
buffer_t     *dirz   = NULL;
buffer_t     *dirmz  = NULL;
buffer_t     *snap   = NULL;

int FILL        = 1;
int POSITION    = 6;
int DIRECTIONAL = 7;
int ID_SEL      = 8;
int NOISE       = 9;
int LIGHT       = 10;
int THICKNESS   = 11;
int HISTOGRAM   = 12;
int BLUR        = 13;
int ANCHOR      = 14;
int AMOUNT      = 13; // Noise amount, 3D Noise distortion
int SEED        = 14;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mask_effects] %s %s", ok ? "ok  " : "FAIL", name);
}

char *test_path(char *name) {
	return string("%s/%s", script_save_path(), name);
}

// 256x256 gray image, x / 255 across
void write_ramp(char *path) {
	int       w = 256;
	buffer_t *b = buffer_create(w * w * 4);
	for (int y = 0; y < w; ++y) {
		for (int x = 0; x < w; ++x) {
			int i = (y * w + x) * 4;
			buffer_set_u8(b, i, x);
			buffer_set_u8(b, i + 1, x);
			buffer_set_u8(b, i + 2, x);
			buffer_set_u8(b, i + 3, 255);
		}
	}
	iron_write_png(path, b, w, w, 0);
}

// 256x256 ID map: red top left, green top right, blue bottom left, yellow bottom right
void write_id_map(char *path) {
	int       w = 256;
	buffer_t *b = buffer_create(w * w * 4);
	for (int y = 0; y < w; ++y) {
		for (int x = 0; x < w; ++x) {
			int i  = (y * w + x) * 4;
			int r  = 0;
			int gr = 0;
			int bl = 0;
			if (x < w / 2 && y < w / 2) {
				r = 255;
			}
			else if (y < w / 2) {
				gr = 255;
			}
			else if (x < w / 2) {
				bl = 255;
			}
			else {
				r  = 255;
				gr = 255;
			}
			buffer_set_u8(b, i, r);
			buffer_set_u8(b, i + 1, gr);
			buffer_set_u8(b, i + 2, bl);
			buffer_set_u8(b, i + 3, 255);
		}
	}
	iron_write_png(path, b, w, w, 0);
}

gpu_texture_t *result() {
	return slot_layer_mask_texture(mask);
}

// Texels with red in [lo, hi] inside the box of percents [x0, x1) x [y0, y1), every other texel each way
int count_box(buffer_t *p, int w, int h, int x0, int x1, int y0, int y1, int lo, int hi) {
	int n = 0;
	for (int y = h * y0 / 100; y < h * y1 / 100; y += 2) {
		for (int x = w * x0 / 100; x < w * x1 / 100; x += 2) {
			int v = buffer_get_u8(p, (y * w + x) * 4);
			if (v >= lo && v <= hi) {
				n = n + 1;
			}
		}
	}
	return n;
}

int white(gpu_texture_t *t, int x0, int x1, int y0, int y1) {
	return count_box(script_get_texture_pixels(t), t->width, t->height, x0, x1, y0, y1, 200, 255);
}

int between(gpu_texture_t *t) {
	return count_box(script_get_texture_pixels(t), t->width, t->height, 0, 100, 0, 100, 20, 235);
}

int center(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	return buffer_get_u8(p, ((t->height / 2) * t->width + t->width / 2) * 4);
}

buffer_t *copy_pixels(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

// A painted mask's value, its readback may be bgra (as id_mask.c reads it)
buffer_t *painted_pixels(gpu_texture_t *t) {
	buffer_t *c = copy_pixels(t);
	for (int i = 0; i < c->length; i += 4) {
		int r = buffer_get_u8(c, i);
		int b = buffer_get_u8(c, i + 2);
		buffer_set_u8(c, i, r > b ? r : b);
	}
	return c;
}

// Texels of a and b apart by more than 10, every other texel each way
int differ(buffer_t *a, buffer_t *b, int w, int h) {
	int n = 0;
	for (int y = 0; y < h; y += 2) {
		for (int x = 0; x < w; x += 2) {
			int i = (y * w + x) * 4;
			int d = buffer_get_u8(a, i) - buffer_get_u8(b, i);
			if (d > 10 || d < -10) {
				n = n + 1;
			}
		}
	}
	return n;
}

int changed(gpu_texture_t *t, buffer_t *b) {
	return differ(script_get_texture_pixels(t), b, t->width, t->height);
}

// Mean of t over the texels where ref is white, -1 when there are none
int mean_where(gpu_texture_t *t, buffer_t *ref) {
	buffer_t *p   = script_get_texture_pixels(t);
	int       sum = 0;
	int       n   = 0;
	for (int y = 0; y < t->height; y += 2) {
		for (int x = 0; x < t->width; x += 2) {
			int i = (y * t->width + x) * 4;
			if (buffer_get_u8(ref, i) >= 230) {
				sum = sum + buffer_get_u8(p, i);
				n   = n + 1;
			}
		}
	}
	return n > 100 ? sum / n : -1;
}

void stroke(float y) {
	for (int i = 0; i < 20; ++i) {
		script_paint(0.35 + i * 0.015, y + i * 0.004);
	}
	script_paint_end();
}

// Replaces the generator with a new one of type, noise off
void use_generator(int type) {
	if (g >= 0) {
		script_mask_remove_effect(mask, g);
	}
	g = type < 0 ? -1 : script_mask_add_effect(mask, type, -1);
	if (g >= 0 && type != NOISE) {
		script_mask_set_effect_param(mask, g, AMOUNT, 0.0);
	}
}

// Directional facing dir (+Y, -Y, +X, -X, +Z, -Z) white only within ~35 degrees
void use_directional(int dir) {
	script_mask_set_effect_param(mask, g, 0, dir);
	script_mask_set_effect_param(mask, g, 1, 0.2); // Spread
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame          = 0;
	context_t *ctx = script_get_context();

	if (step == 0) {
		write_ramp(test_path("koru_me_thick_ramp.png"));
		write_id_map(test_path("koru_me_id_map.png"));
		script_import_asset(test_path("koru_me_thick_ramp.png"), 0);
		script_import_asset(test_path("koru_me_id_map.png"), 0);
		layer = script_layer_new_color(0xffffffff, 1.0, 0.5, 0.0);
		script_layer_set_name(layer, "fx_layer");
		mask = script_layer_new_mask(layer, 0); // Black
		script_layer_set_name(mask, "fx_mask");
	}
	else if (step == 1) {
		check(script_asset_index("koru_me_thick_ramp") >= 0 && script_asset_index("koru_me_id_map") >= 0, "both images imported");
		check(mesh_maps_get_asset(3) == NULL, "no thickness map registered yet");
		use_generator(NOISE); // Clouds, coverage 0.5
	}
	else if (step == 2) {
		gpu_texture_t *t = result();
		printf("[mask_effects] mask %dx%d", t->width, t->height);
		clouds = copy_pixels(t);
		w0     = white(t, 0, 100, 0, 100);
		check(w0 > 1000, string("3D noise clouds: some white (%d)", w0));
		check(between(t) > 1000, string("3D noise clouds: soft at sharpness 0.5 (%d between)", between(t)));
		script_mask_set_effect_param(mask, g, 1, 0.8); // Coverage
	}
	else if (step == 3) {
		int w = white(result(), 0, 100, 0, 100);
		check(w > w0 + 1000, string("coverage 0.8 grows the white area (%d from %d)", w, w0));
		script_mask_set_effect_param(mask, g, 1, 0.0);
	}
	else if (step == 4) {
		check(white(result(), 0, 100, 0, 100) == 0, "coverage 0 is black");
		script_mask_set_effect_param(mask, g, 1, 0.5);
		script_mask_set_effect_param(mask, g, 0, 1.0); // Cells
	}
	else if (step == 5) {
		cells = copy_pixels(result());
		int n = changed(result(), clouds);
		check(n > 1000 && white(result(), 0, 100, 0, 100) > 500, string("cells differ from clouds (%d texels)", n));
		script_mask_set_effect_param(mask, g, 0, 2.0); // Scratches
	}
	else if (step == 6) {
		gpu_texture_t *t = result();
		scr              = copy_pixels(t);
		int w            = white(t, 0, 100, 0, 100);
		check(changed(t, clouds) > 1000 && changed(t, cells) > 1000, "scratches differ from clouds and cells");
		check(w > 50 && w < w0, string("scratches: thin lines, less white than clouds (%d, clouds %d)", w, w0));
		script_mask_set_effect_param(mask, g, 0, 3.0); // Streaks
	}
	else if (step == 7) {
		gpu_texture_t *t = result();
		streak           = copy_pixels(t);
		check(changed(t, clouds) > 1000 && changed(t, cells) > 1000 && changed(t, scr) > 1000, "streaks differ from the others");
		script_mask_set_effect_param(mask, g, SEED, 7.0);
	}
	else if (step == 8) {
		int n = changed(result(), streak);
		check(n > 1000, string("another seed changes the noise (%d texels)", n));
		script_mask_set_effect_param(mask, g, SEED, 0.0);
		script_mask_set_effect_param(mask, g, AMOUNT, 0.6); // Distortion
	}
	else if (step == 9) {
		int n = changed(result(), streak);
		check(n > 1000, string("distortion warps the noise (%d texels)", n));
		use_generator(DIRECTIONAL);
		use_directional(0); // +Y
	}
	else if (step == 10) {
		dirp = copy_pixels(result());
		use_directional(1);
	}
	else if (step == 11) {
		dirn = copy_pixels(result());
		use_directional(4); // +Z
	}
	else if (step == 12) {
		dirz = copy_pixels(result());
		use_directional(5);
	}
	else if (step == 13) {
		dirmz = copy_pixels(result());
		use_generator(LIGHT);
		script_mask_set_effect_param(mask, g, 1, 90.0); // Straight above
		script_mask_set_effect_param(mask, g, 2, 0.0);  // Softness
		script_mask_set_effect_param(mask, g, 3, 0.0);  // AO weight
	}
	else if (step == 14) {
		int up   = mean_where(result(), dirp);
		int down = mean_where(result(), dirn);
		check(up > 200, string("light from above: faces up are lit (%d)", up));
		check(down >= 0 && down < 10, string("light from above: faces down are dark (%d)", down));
		script_mask_set_effect_param(mask, g, 0, 0.0); // Azimuth 0, level: from the front (+Z)
		script_mask_set_effect_param(mask, g, 1, 0.0);
	}
	else if (step == 15) {
		int front = mean_where(result(), dirz);
		int back  = mean_where(result(), dirmz);
		int up    = mean_where(result(), dirp);
		check(front > 200, string("light from +Z: faces +Z are lit (%d)", front));
		check(back >= 0 && back < 10, string("light from +Z: faces -Z are dark (%d)", back));
		check(up >= 0 && up < 60, string("light from +Z: faces up are about unlit (%d)", up));
		script_mask_set_effect_param(mask, g, 2, 1.0); // Softness wraps around
	}
	else if (step == 16) {
		int back = mean_where(result(), dirmz);
		int up   = mean_where(result(), dirp);
		check(back >= 0 && back < 30, string("softness 1: -Z stays about dark (%d)", back));
		check(up > 90 && up < 165, string("softness 1 wraps the light: faces up get about half (%d)", up));
		use_generator(THICKNESS); // Thin
	}
	else if (step == 17) {
		check(white(result(), 0, 100, 0, 100) == 0 && between(result()) == 0, "thickness without a map is black (thin)");
		script_mask_set_effect_param(mask, g, 0, 1.0); // Thick
	}
	else if (step == 18) {
		check(white(result(), 0, 100, 0, 100) == 0 && between(result()) == 0, "thickness without a map is black (thick)");
		mesh_maps_set(3, script_get_asset(script_asset_index("koru_me_thick_ramp")));
	}
	else if (step == 19) {
		gpu_texture_t *t = result();
		check(mesh_maps_get_asset(3) != NULL, "thickness map registered");
		check(white(t, 80, 100, 0, 100) > 500, string("thick side: thick texels white (%d)", white(t, 80, 100, 0, 100)));
		check(white(t, 0, 40, 0, 100) < 20, string("thick side: thin texels black (%d)", white(t, 0, 40, 0, 100)));
		script_mask_set_effect_param(mask, g, 0, 0.0); // Thin
	}
	else if (step == 20) {
		gpu_texture_t *t = result();
		check(white(t, 0, 20, 0, 100) > 500, string("thin side: thin texels white (%d)", white(t, 0, 20, 0, 100)));
		check(white(t, 60, 100, 0, 100) < 20, string("thin side: thick texels black (%d)", white(t, 60, 100, 0, 100)));
		use_generator(-1);
		e1 = script_mask_add_effect(mask, FILL, -1);
		script_mask_set_effect_param(mask, e1, 0, 0.3);
		e2 = script_mask_add_effect(mask, HISTOGRAM, -1);
		script_mask_set_effect_param(mask, e2, 0, 0.5);
		script_mask_set_effect_param(mask, e2, 1, 1.0); // Hard threshold at 0.5
	}
	else if (step == 21) {
		int v = center(result());
		check(v < 3, string("histogram scan, contrast 1: 0.3 under 0.5 is black (%d)", v));
		script_mask_set_effect_param(mask, e1, 0, 0.7);
	}
	else if (step == 22) {
		int v = center(result());
		check(v > 252, string("histogram scan, contrast 1: 0.7 over 0.5 is white (%d)", v));
		script_mask_set_effect_param(mask, e1, 0, 0.3);
		script_mask_set_effect_param(mask, e2, 1, 0.0);
	}
	else if (step == 23) {
		int v = center(result());
		check(v >= 73 && v <= 81, string("histogram scan, contrast 0, position 0.5 keeps 0.3 (%d)", v));
		script_mask_set_effect_param(mask, e2, 1, 0.5);
	}
	else if (step == 24) {
		int v = center(result());
		check(v >= 22 && v <= 30, string("histogram scan, contrast 0.5: 0.3 becomes 0.1 (%d)", v));
		script_mask_set_effect_param(mask, e2, 0, 0.7);
	}
	else if (step == 25) {
		int v = center(result());
		check(v >= 123 && v <= 132, string("histogram scan, position 0.7: 0.3 becomes 0.5 (%d)", v));
		script_mask_remove_effect(mask, e2);
		script_mask_remove_effect(mask, e1);
		use_generator(POSITION); // Y, start = end: a hard step
		script_mask_set_effect_param(mask, g, 1, 0.5);
		script_mask_set_effect_param(mask, g, 2, 0.5);
	}
	else if (step == 26) {
		snap  = copy_pixels(result());
		w0    = between(result());
		e1    = script_mask_add_effect(mask, BLUR, -1);
		check(w0 < 50, string("the position step is hard (%d between)", w0));
		script_mask_set_effect_param(mask, e1, 0, 8.0);
	}
	else if (step == 27) {
		int n = between(result());
		check(n > w0 + 1000, string("blur softens the hard edge (%d between, %d before)", n, w0));
		script_mask_set_effect(mask, e1, true, 0.0, 0);
	}
	else if (step == 28) {
		int n = changed(result(), snap);
		check(n < 20, string("blur opacity 0 leaves the result (%d texels differ)", n));
		script_mask_remove_effect(mask, e1);
		use_generator(-1);
		check(script_set_id_map(script_asset_index("koru_me_id_map")) == 1, "the ID map is set");
		e1 = script_mask_add_effect(mask, ID_SEL, -1);
	}
	else if (step == 29) {
		gpu_texture_t *t = result();
		check(white(t, 0, 100, 0, 100) == 0, "ID selection of no parts is black");
		script_mask_set_effect_param(mask, e1, 1, 16711680.0); // 0xff0000
		script_mask_set_effect_param(mask, e1, 2, 255.0);      // 0x0000ff
		script_mask_set_effect_param(mask, e1, 0, 2.0);
	}
	else if (step == 30) {
		gpu_texture_t *t    = result();
		int            all  = count_box(script_get_texture_pixels(t), t->width, t->height, 0, 50, 0, 100, 0, 255);
		int            left = white(t, 0, 50, 0, 100);
		check(left == all, string("red and blue: the left half is white (%d of %d)", left, all));
		check(white(t, 50, 100, 0, 100) == 0 && between(t) == 0, "and nothing else, no blending at the borders");
		script_mask_set_effect_param(mask, e1, 0, 1.0);
	}
	else if (step == 31) {
		gpu_texture_t *t    = result();
		int            all  = count_box(script_get_texture_pixels(t), t->width, t->height, 0, 50, 0, 100, 0, 255);
		int            left = white(t, 0, 50, 0, 100);
		check(left * 2 == all, string("red alone: the top or bottom left quarter (%d of %d)", left, all));
		check(white(t, 50, 100, 0, 100) == 0, "red alone: nothing on the right");
		script_mask_remove_effect(mask, e1);
		// Layer ids with gaps: ids after load differ
		for (int i = 0; i < 3; ++i) {
			script_layer_delete(script_layer_new());
		}
		src = script_layer_new_color(0xffffffff, 1.0, 0.5, 0.0);
		script_layer_set_name(src, "anchor_src");
		srcm = script_layer_new_mask(src, 0);
		script_layer_set_name(srcm, "anchor_src_mask");
		paint = script_layer_new();
		script_layer_set_name(paint, "anchor_paint");
	}
	else if (step == 32) {
		script_layer_set(srcm);
		context_select_tool(TOOL_TYPE_BRUSH);
		ctx->mask_value = 1.0;
		stroke(0.45);
	}
	else if (step == 33) {
		e1 = script_mask_add_effect(mask, ANCHOR, -1);
		script_mask_set_effect_param(mask, e1, 0, src->id); // Mask channel
	}
	else if (step == 34) {
		int n = changed(result(), painted_pixels(slot_layer_mask_texture(srcm)));
		w0    = white(result(), 0, 100, 0, 100);
		check(w0 > 200 && n < 20, string("anchor reads the mask of the other layer (%d white, %d texels differ)", w0, n));
		script_layer_set(srcm);
		stroke(0.65);
	}
	else if (step == 35) {
		int n = changed(result(), painted_pixels(slot_layer_mask_texture(srcm)));
		int w = white(result(), 0, 100, 0, 100);
		check(w > w0 + 200 && n < 20, string("painting the anchored mask updates the anchor (%d white from %d, %d differ)", w, w0, n));
		script_mask_set_effect_param(mask, e1, 0, paint->id);
		script_mask_set_effect_param(mask, e1, 1, 2.0); // Opacity
	}
	else if (step == 36) {
		check(white(result(), 0, 100, 0, 100) == 0, "anchor on an empty layer's opacity is black");
		script_layer_set(paint);
		for (int i = 0; i < 10; ++i) { // The stroke stays open over the next frames
			script_paint(0.35 + i * 0.015, 0.45 + i * 0.004);
		}
	}
	else if (step == 37) {
		w0 = white(result(), 0, 100, 0, 100);
		check(w0 > 50, string("the anchor follows the anchored layer while a stroke paints it (%d white)", w0));
		for (int i = 10; i < 20; ++i) {
			script_paint(0.35 + i * 0.015, 0.45 + i * 0.004);
		}
		script_paint_end();
	}
	else if (step == 38) {
		int w = white(result(), 0, 100, 0, 100);
		check(w > w0 + 50, string("painting the anchored layer updates the anchor (%d white, %d mid stroke)", w, w0));
		script_mask_set_effect_param(mask, e1, 0, src->id);
		script_mask_set_effect_param(mask, e1, 1, 0.0);
	}
	else if (step == 39) {
		snap = copy_pixels(result());
		id0  = src->id;
		script_layer_delete(src);
	}
	else if (step == 40) {
		check(script_layer_get("anchor_src") == NULL, "the anchored layer is deleted");
		check(white(result(), 0, 100, 0, 100) == 0, "an anchor on a deleted layer is black");
		history_undo();
	}
	else if (step == 41) {
		src  = script_layer_get("anchor_src");
		srcm = script_layer_get("anchor_src_mask");
		check(src != NULL && src->id == id0, "undo brings the layer back with its id");
		int n = changed(result(), snap);
		check(n < 20, string("and the anchor reads it again (%d texels differ)", n));
		// A cycle: the anchored mask anchors this one
		e2 = script_mask_add_effect(srcm, ANCHOR, -1);
		script_mask_set_effect_param(srcm, e2, 0, layer->id);
	}
	else if (step == 42) {
		check(white(result(), 0, 100, 0, 100) >= white(slot_layer_mask_texture(srcm), 0, 100, 0, 100) - 20, "a cycle evaluates");
		rev0 = mask->revision;
		rev1 = srcm->revision;
	}
	else if (step == 43) {
		check(mask->revision == rev0 && srcm->revision == rev1,
		      string("and settles, no evaluation every frame (%d %d revisions on)", mask->revision - rev0, srcm->revision - rev1));
		script_mask_remove_effect(srcm, e2);
		// Its own layer, inside a smart material of it: hidden, the parent is opaque
		e2 = script_mask_add_effect(mask, ANCHOR, -1);
		script_mask_set_effect_param(mask, e2, 0, layer->id);
		script_mask_set_effect_param(mask, e2, 1, 2.0);
		script_mask_set_effect(mask, e2, false, 1.0, 4);
	}
	else if (step == 44) {
		snap = copy_pixels(result());
		id0  = src->id;
		file = test_path("mask_effects_test.arm");
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 45) {
		script_project_new();
	}
	else if (step == 46) {
		script_project_open(file);
	}
	else if (step == 47) {
		mask  = script_layer_get("fx_mask");
		src   = script_layer_get("anchor_src");
		layer = script_layer_get("fx_layer");
		check(mask != NULL && src != NULL && layer != NULL, "the layers are loaded");
		if (mask != NULL && src != NULL && layer != NULL) {
			int a = (int)script_mask_get_effect_param(mask, e1, 0);
			int b = (int)script_mask_get_effect_param(mask, e2, 0);
			check(src->id != id0, string("layer ids differ after load (%d, %d before)", src->id, id0));
			check(a == src->id, string("the anchor keeps its layer (%d, anchor_src %d)", a, src->id));
			check(b == layer->id, string("and the second its own layer (%d, fx_layer %d)", b, layer->id));
			int n = changed(result(), snap);
			check(n < 20, string("the loaded anchor reads the same pixels (%d texels differ)", n));
			int old = smart_materials_find("me_test_smart");
			if (old >= 0) {
				smart_materials_delete(old);
			}
			check(smart_materials_save(layer, "me_test_smart"), "saved as a smart material");
		}
	}
	else if (step == 48) {
		root = smart_materials_apply("me_test_smart");
	}
	else if (step == 49) {
		slot_layer_t *m = NULL;
		for (int i = 0; root != NULL && i < script_layer_count(); ++i) {
			slot_layer_t *l = script_layer_at(i);
			if (l->parent == root && l->texpaint_nor == NULL && l->texpaint != NULL) {
				m = l;
			}
		}
		check(m != NULL && m != mask, "the smart material brings its mask");
		if (m != NULL) {
			int a = (int)script_mask_get_effect_param(m, e1, 0);
			int b = (int)script_mask_get_effect_param(m, e2, 0);
			check(a == -1, string("an anchor outside the smart material is none (%d)", a));
			check(b == root->id && root != layer, string("an anchor inside it follows the new layer (%d, root %d)", b, root->id));
		}
		smart_materials_delete(smart_materials_find("me_test_smart"));
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[mask_effects] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MASK_EFFECTS PASS" : "MASK_EFFECTS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.4;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_brush_defaults();
	script_notify_on_next_frame(next);
}
