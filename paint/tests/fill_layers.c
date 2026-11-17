#include "global.h"

// Fill layers as Painter's (util_fill.c): a source per channel (color, image, pattern, material), evaluated live in the
// viewport, rasterized into the layer for what reads pixels; projections (uv, triplanar with hardness, planar, decal),
// non-uniform scale, offset, rotation; undo, to paint and back, fill masks, save and load.

int step   = 0;
int frame  = 0;
int checks = 0;
int fails  = 0;
int f      = -1; // The fill layer
int m      = -1; // A fill mask
int count0 = 0;

slot_material_t *mat_red   = NULL;
buffer_t        *before    = NULL;
char            *shot_dir  = NULL;

void check(bool ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[fill_layers] %s %s", ok ? "ok  " : "FAIL", name);
}

slot_layer_t *layer_by_id(int id) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->id == id) {
			return l;
		}
	}
	return NULL;
}

int selected() {
	return script_get_context()->layer->id;
}

// Average of a channel over sampled texels of t inside the uv islands (alpha above 0). Red: the larger of bytes 0 and
// 2, the backends order them differently.
float average(gpu_texture_t *t, int channel) {
	buffer_t *p      = script_get_texture_pixels(t);
	int       n      = p->length / 4;
	int       stride = n / 4096 + 1;
	float     sum    = 0.0;
	int       count  = 0;
	for (int i = 0; i < n; i += stride) {
		int q = i * 4;
		if (channel != 3 && buffer_get_u8(p, q + 3) == 0) {
			continue;
		}
		int v = 0;
		if (channel == 0) {
			int r = buffer_get_u8(p, q);
			int b = buffer_get_u8(p, q + 2);
			v     = r > b ? r : b;
		}
		else {
			v = buffer_get_u8(p, q + channel);
		}
		sum   = sum + v;
		count = count + 1;
	}
	return count > 0 ? sum / count / 255.0 : 0.0;
}

// Distinct values (the sum of a texel's color bytes, or one channel when channel >= 0) among sampled texels: 1 for a
// flat texture
int variety(gpu_texture_t *t, int channel) {
	buffer_t *p      = script_get_texture_pixels(t);
	int       n      = p->length / 4;
	int       stride = n / 512 + 1;
	int       seen[800];
	for (int i = 0; i < 800; ++i) {
		seen[i] = 0;
	}
	int kinds = 0;
	for (int i = 0; i < n; i += stride) {
		int q = i * 4;
		int v = channel >= 0 ? buffer_get_u8(p, q + channel) : buffer_get_u8(p, q) + buffer_get_u8(p, q + 1) + buffer_get_u8(p, q + 2);
		if (seen[v] == 0) {
			seen[v] = 1;
			kinds   = kinds + 1;
		}
	}
	return kinds;
}

buffer_t *copy_pixels(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

int diff_pixels(gpu_texture_t *t, buffer_t *b) {
	buffer_t *p = script_get_texture_pixels(t);
	int       n = 0;
	for (int i = 0; i < p->length; i += 16) {
		int d = buffer_get_u8(p, i) + buffer_get_u8(p, i + 1) + buffer_get_u8(p, i + 2) - buffer_get_u8(b, i) - buffer_get_u8(b, i + 1) - buffer_get_u8(b, i + 2);
		if (d > 8 || d < -8) {
			n = n + 1;
		}
	}
	return n;
}

void shot(char *name) {
	if (shot_dir != NULL) {
		script_screenshot(string("%s/fill_%s.png", shot_dir, name));
	}
}

// Value ids of app_layer_set_value: a source's value, its pattern settings
int source_value(int channel, int k) {
	return 64 + channel * 32 + k;
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;
	fill_ensure_all(); // The rasters the checks read, without waiting for the fills to settle
	slot_layer_t *l = f >= 0 ? layer_by_id(f) : NULL;

	if (step == 0) {
		count0 = script_layer_count();
		check(app_layer_new(1), "new fill layer"); // APP_LAYER_NEW_FILL, on the next frame
	}
	else if (step == 1) {
		f = selected();
		l = layer_by_id(f);
		check(script_layer_count() == count0 + 1 && l != NULL && l->fill != NULL, "a fill layer was added");
		check(app_layer_fill_projection(f) == 0 && app_layer_fill_kind(f, 0) == 0, "uv projection, color sources");
		check(string_equals(fill_test_shader(f), "ok"), string("its raster shader compiles (%s)", fill_test_shader(f)));
		check(app_layer_set_fill_color(f, 0, 0, 1.0, 0.0, 0.0), "base color red");
		app_drag_end();
	}
	else if (step == 2) {
		float r = average(l->texpaint, 0);
		float g = average(l->texpaint, 1);
		check(r > 0.9 && g < 0.1, string("the layer holds red (%.2f, %.2f)", r, g));
		shot("red");
		history_undo();
	}
	else if (step == 3) {
		check(average(l->texpaint, 1) > 0.3, string("undo gives the color back (g %.2f)", average(l->texpaint, 1)));
		history_redo();
	}
	else if (step == 4) {
		check(average(l->texpaint, 1) < 0.1, "redo makes it red again");
		check(app_layer_set_fill_kind(f, 0, 2) && app_layer_fill_kind(f, 0) == 2, "base color from a pattern");
		check(app_layer_set_fill_pattern(f, 0, 3), "bricks"); // PATTERN_BRICKS
	}
	else if (step == 5) {
		check(string_equals(fill_test_shader(f), "ok"), "pattern shader compiles");
		check(variety(l->texpaint, -1) > 4, string("the pattern varies (%d)", variety(l->texpaint, -1)));
		shot("bricks");
		before = copy_pixels(l->texpaint);
		check(app_layer_set_value(f, 6, 3.0), "scale x"); // APP_LAYER_VALUE_FILL_SCALE_X
	}
	else if (step == 6) {
		check(app_layer_value(f, 6) > 2.99 && app_layer_value(f, 7) > 0.99 && app_layer_value(f, 7) < 1.01, "only x scaled");
		int d = diff_pixels(l->texpaint, before);
		check(d > 100, string("scale changes the fill (%d)", d));
		// Linked scale and offset (APP_LAYER_VALUE_FILL_SCALE 12, _OFFSET 13): both axes, one undo step
		check(app_layer_set_value(f, 12, 2.0) && app_layer_value(f, 6) > 1.99 && app_layer_value(f, 6) < 2.01 && app_layer_value(f, 7) > 1.99 &&
		          app_layer_value(f, 7) < 2.01,
		      "linked scale sets both axes");
		history_undo();
		check(app_layer_value(f, 6) > 2.99 && app_layer_value(f, 7) > 0.99 && app_layer_value(f, 7) < 1.01, "one undo restores both scale axes");
		check(app_layer_set_value(f, 13, 0.5) && app_layer_value(f, 8) == 0.5 && app_layer_value(f, 9) == 0.5, "linked offset sets both axes");
		history_undo();
		check(app_layer_value(f, 8) == 0.0 && app_layer_value(f, 9) == 0.0, "one undo restores both offset axes");
		before = copy_pixels(l->texpaint);
		check(app_layer_set_value(f, 8, 0.25) && app_layer_set_value(f, 10, 30.0), "offset and rotation");
	}
	else if (step == 7) {
		int d = diff_pixels(l->texpaint, before);
		check(d > 100, string("offset and rotation change the fill (%d)", d));
		check(app_layer_set_value(f, source_value(0, 2), 12.0), "pattern scale");
		check(app_layer_set_fill_projection(f, 1) && app_layer_fill_projection(f) == 1, "triplanar");
	}
	else if (step == 8) {
		check(string_equals(fill_test_shader(f), "ok"), "triplanar shader compiles");
		before = copy_pixels(l->texpaint);
		check(app_layer_set_value(f, 11, 1.0), "hardness"); // APP_LAYER_VALUE_FILL_HARDNESS
	}
	else if (step == 9) {
		int d = diff_pixels(l->texpaint, before);
		check(d > 10, string("hardness changes where the planes meet (%d)", d));
		shot("triplanar");
		check(app_layer_set_fill_projection(f, 3) && app_layer_fill_projection(f) == 3, "planar");
	}
	else if (step == 10) {
		check(string_equals(fill_test_shader(f), "ok"), "planar shader compiles");
		check(variety(l->texpaint, -1) > 4, string("planar projects the pattern (%d)", variety(l->texpaint, -1)));
		shot("planar");
		check(app_layer_set_fill_projection(f, 2), "decal");
	}
	else if (step == 11) {
		check(string_equals(fill_test_shader(f), "ok"), "decal shader compiles");
		check(average(l->texpaint, 3) < 0.99, string("a decal leaves the outside of its box clear (alpha %.2f)", average(l->texpaint, 3)));
		check(app_layer_set_fill_projection(f, 0), "back to uv");
		// Every channel from a material
		int mi = -1;
		for (int i = 0; i < app_layer_material_count(); ++i) {
			if (string_equals(app_layer_material_name(i), "fill_red")) {
				mi = i;
			}
		}
		check(mi >= 0 && app_layer_set_fill_material(f, -1, mi), "fill from a material");
	}
	else if (step == 12) {
		check(app_layer_fill_kind(f, 0) == 3 && app_layer_fill_kind(f, 3) == 3, "every channel from it");
		check(fill_stat(0) == 1, string("its tile was rendered (%d tiles)", fill_stat(0)));
		float r = average(l->texpaint, 0);
		float g = average(l->texpaint, 1);
		check(r > 0.7 && g < 0.25, string("the material's red fills the layer (%.2f, %.2f)", r, g));
		shot("material");
		// Roughness from a pattern, the rest from the material
		check(app_layer_set_fill_kind(f, 3, 2), "roughness from a pattern");
	}
	else if (step == 13) {
		check(variety(l->texpaint_pack, 1) > 4, string("roughness varies (%d)", variety(l->texpaint_pack, 1)));
		check(average(l->texpaint, 0) > 0.7, "base color still the material's");
		before = copy_pixels(l->texpaint);
		check(app_layer_to_paint(f) && layer_by_id(f)->fill == NULL, "to paint layer");
	}
	else if (step == 14) {
		check(diff_pixels(l->texpaint, before) < 50, "the paint layer keeps the fill's pixels");
		history_undo();
	}
	else if (step == 15) {
		check(layer_by_id(f)->fill != NULL && app_layer_fill_kind(f, 3) == 2, "undo gives the fill back, sources and all");
		// A fill mask: a pattern from black to white
		check(app_layer_add_mask(f, 11), "fill mask"); // APP_LAYER_NEW_FILL_MASK
	}
	else if (step == 16) {
		m                = selected();
		slot_layer_t *ml = layer_by_id(m);
		check(ml != NULL && ml->fill != NULL && ml->parent == l, "the mask is a fill");
		check(ml != NULL && variety(ml->texpaint, 1) > 4, string("the mask holds the pattern (%d)", ml != NULL ? variety(ml->texpaint, 1) : 0));
		project_filepath_set(string("%s/fill_layers_test.arm", script_save_path()));
		project_save(false);
	}
	else if (step == 17) {
		script_project_new();
	}
	else if (step == 18) {
		script_project_open(string("%s/fill_layers_test.arm", script_save_path()));
	}
	else if (step == 19) {
		slot_layer_t *fl = NULL;
		for (int i = 0; i < script_layer_count(); ++i) {
			slot_layer_t *x = script_layer_at(i);
			if (x->fill != NULL && x->texpaint_nor != NULL) {
				fl = x;
			}
		}
		check(fl != NULL, "the fill layer survived save and load");
		if (fl != NULL) {
			check(app_layer_fill_kind(fl->id, 0) == 3 && app_layer_fill_kind(fl->id, 3) == 2, "its sources survived");
			check(average(fl->texpaint, 0) > 0.7, "it renders the material again");
		}
		shot("loaded");
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[fill_layers] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "FILL_LAYERS PASS" : "FILL_LAYERS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	shot_dir = script_save_path();

	mat_red      = script_material_create("fill_red");
	ui_node_t *r = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(r, 0, 0, 0.9, 0.05, 0.05, 1.0);
	script_material_connect(r, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();

	script_notify_on_next_frame(next);
}
