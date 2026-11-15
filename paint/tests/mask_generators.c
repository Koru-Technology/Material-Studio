#include "global.h"

// Mask generators: Edge Wear and Dirt follow the registered curvature and AO maps, Position Gradient and Directional the
// mesh, noise follows its seed and amount, outputs are cached (a stroke or a blending edit does not render them again, a
// param edit renders once), undo, dilation, save and load, a mesh transform renders Directional again, and a smart
// material brings a generator to another mesh.
// The maps: curvature grows from 0 to 1 along u (concave left, flat in the middle, convex right), AO along v.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *layer = NULL;
slot_layer_t *mask  = NULL;
slot_layer_t *root  = NULL; // Smart material applied
int           g     = -1; // The generator's index in the stack
int           r0    = 0;  // mask_gen_renders() before an edit
int           ew50  = 0;
int           w1    = 0;
char         *file  = NULL;
char         *saved = NULL;
buffer_t     *snap  = NULL; // Edge Wear amount 0.5
buffer_t     *dirp  = NULL; // Directional +Y
buffer_t     *dirn  = NULL; // Directional -Y
buffer_t     *posy  = NULL;
buffer_t     *posx  = NULL;
buffer_t     *noisy = NULL;

int EDGE_WEAR   = 4;
int DIRT        = 5;
int POSITION    = 6;
int DIRECTIONAL = 7;
int AMOUNT      = 13; // Noise
int SEED        = 14;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mask_generators] %s %s", ok ? "ok  " : "FAIL", name);
}

char *test_path(char *name) {
	return string("%s/%s", script_save_path(), name);
}

// 256x256 gray image, u_ramp: x / 255 across, otherwise y / 255 down
void write_ramp(char *path, int u_ramp) {
	int       w = 256;
	buffer_t *b = buffer_create(w * w * 4);
	for (int y = 0; y < w; ++y) {
		for (int x = 0; x < w; ++x) {
			int i = (y * w + x) * 4;
			int v = u_ramp ? x : y;
			buffer_set_u8(b, i, v);
			buffer_set_u8(b, i + 1, v);
			buffer_set_u8(b, i + 2, v);
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

buffer_t *copy_pixels(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
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
	g = script_mask_add_effect(mask, type, -1);
	script_mask_set_effect_param(mask, g, AMOUNT, 0.0);
}

void rotate_main(float x, float w) {
	mesh_object_t *o         = context_main_object();
	o->base->transform->rot.x = x;
	o->base->transform->rot.y = 0.0;
	o->base->transform->rot.z = 0.0;
	o->base->transform->rot.w = w;
	transform_build_matrix(o->base->transform);
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
		write_ramp(test_path("koru_mg_test_curvature.png"), 1);
		write_ramp(test_path("koru_mg_test_ao.png"), 0);
		script_import_asset(test_path("koru_mg_test_curvature.png"), 0);
		script_import_asset(test_path("koru_mg_test_ao.png"), 0);
		layer = script_layer_new_color(0xffffffff, 1.0, 0.5, 0.0);
		mask  = script_layer_new_mask(layer, 0); // Black
		script_layer_set_name(mask, "gen_mask");
	}
	else if (step == 1) {
		check(mesh_maps_get_asset(1) != NULL && mesh_maps_get_asset(0) != NULL, "curvature and ao maps registered");
		r0 = mask_gen_renders();
		use_generator(EDGE_WEAR);
	}
	else if (step == 2) {
		gpu_texture_t *t = result();
		check(mask_stack_count(mask) == 2 && script_mask_effect_type(mask, g) == EDGE_WEAR, "edge wear on top of the paint effect");
		check(mask_gen_renders() == r0 + 1, string("edge wear rendered once (%d)", mask_gen_renders() - r0));
		int flat   = white(t, 0, 60, 0, 100);
		int convex = white(t, 85, 100, 0, 100);
		check(flat < 20, string("edge wear 0.5: flat and concave texels stay black (%d white)", flat));
		check(convex > 500, string("edge wear 0.5: convex texels are white (%d)", convex));
		ew50 = white(t, 0, 100, 0, 100);
		snap = copy_pixels(t);
		r0   = mask_gen_renders();
		script_mask_set_effect_param(mask, g, 0, 0.8); // Amount
	}
	else if (step == 3) {
		gpu_texture_t *t = result();
		check(mask_gen_renders() == r0 + 1, string("a param edit renders exactly once (%d)", mask_gen_renders() - r0));
		int ew80 = white(t, 0, 100, 0, 100);
		check(ew80 > ew50 + 500, string("raising the amount grows the white area (%d from %d)", ew80, ew50));
		check(white(t, 0, 55, 0, 100) < 20, "edge wear 0.8: still nothing on the concave side");
		ew50 = ew80;
		r0   = mask_gen_renders();
		check(app_mask_effect_blending(mask->id, 0) == 4, "generators start on max"); // Index 0: the top effect
		script_mask_set_effect(mask, g, true, 1.0, 2); // Add over the paint
		script_layer_set(mask);
		context_select_tool(TOOL_TYPE_BRUSH);
		ctx->mask_value = 1.0;
		stroke(0.45);
	}
	else if (step == 4) {
		gpu_texture_t *t = result();
		check(mask_gen_renders() == r0, string("a blending edit and a stroke reuse the cached output (%d renders)", mask_gen_renders() - r0));
		int n = white(t, 0, 100, 0, 100);
		check(n > ew50 + 200, string("the stroke shows through add (%d white, %d before)", n, ew50));
		history_undo(); // Stroke
	}
	else if (step == 5) {
		history_undo(); // Blending
	}
	else if (step == 6) {
		history_undo(); // Amount
	}
	else if (step == 7) {
		check(script_mask_get_effect_param(mask, g, 0) == 0.5, "undo brings back amount 0.5");
		int n = changed(result(), snap);
		check(n < 20, string("undo restores the pixels (%d texels differ)", n));
		use_generator(DIRT);
		script_mask_set_effect_param(mask, g, 2, 0.0); // AO weight: cavity only
	}
	else if (step == 8) {
		gpu_texture_t *t = result();
		int cavity = white(t, 0, 20, 0, 100);
		int other  = white(t, 40, 100, 0, 100);
		check(cavity > 500, string("dirt: concave texels are white (%d)", cavity));
		check(other < 20, string("dirt: flat and convex texels stay black (%d)", other));
		int e = script_mask_add_effect(mask, EDGE_WEAR, -1); // Over the dirt, its default max keeps both
		script_mask_set_effect_param(mask, e, AMOUNT, 0.0);
		t = result();
		check(white(t, 0, 20, 0, 100) > 500 && white(t, 85, 100, 0, 100) > 500, "edge wear stacked over dirt: both show");
		script_mask_remove_effect(mask, e);
		script_mask_set_effect_param(mask, g, 2, 1.0); // AO only
	}
	else if (step == 9) {
		gpu_texture_t *t = result();
		int top    = white(t, 0, 100, 0, 35);
		int bottom = white(t, 0, 100, 60, 100);
		check(top > 500, string("dirt ao weight 1: occluded texels (low ao) are white (%d)", top));
		check(bottom < 20, string("dirt ao weight 1: open texels stay black (%d)", bottom));
		use_generator(DIRECTIONAL); // +Y
	}
	else if (step == 10) {
		dirp = copy_pixels(result());
		script_mask_set_effect_param(mask, g, 0, 1.0); // -Y
	}
	else if (step == 11) {
		gpu_texture_t *t = result();
		dirn             = copy_pixels(t);
		int w            = t->width;
		int h            = t->height;
		int covered      = 0;
		int off          = 0;
		int up           = 0;
		for (int y = 0; y < h; y += 2) {
			for (int x = 0; x < w; x += 2) {
				int i = (y * w + x) * 4;
				int a = buffer_get_u8(dirp, i);
				int b = buffer_get_u8(dirn, i);
				if (a > 0 || b > 0) {
					covered = covered + 1;
					int s   = a + b - 255;
					if (s > 6 || s < -6) {
						off = off + 1;
					}
				}
				if (a >= 230) {
					up = up + 1;
				}
			}
		}
		check(up > 500, string("directional +Y: the faces up are white (%d)", up));
		check(covered > 1000 && off * 100 < covered, string("+Y and -Y are complementary (%d of %d texels off)", off, covered));
		use_generator(POSITION); // Y, 0 - 1
	}
	else if (step == 12) {
		gpu_texture_t *t = result();
		posy             = copy_pixels(t);
		int w            = t->width;
		int h            = t->height;
		int sum_up       = 0;
		int n_up         = 0;
		int sum_down     = 0;
		int n_down       = 0;
		for (int y = 0; y < h; y += 2) {
			for (int x = 0; x < w; x += 2) {
				int i = (y * w + x) * 4;
				if (buffer_get_u8(dirp, i) >= 230) {
					sum_up = sum_up + buffer_get_u8(posy, i);
					n_up   = n_up + 1;
				}
				if (buffer_get_u8(dirn, i) >= 230) {
					sum_down = sum_down + buffer_get_u8(posy, i);
					n_down   = n_down + 1;
				}
			}
		}
		int avg_up   = n_up > 0 ? sum_up / n_up : 0;
		int avg_down = n_down > 0 ? sum_down / n_down : 255;
		check(avg_up > 200, string("position Y: high on the faces up (%d)", avg_up));
		check(avg_down < 55, string("position Y: low on the faces down (%d)", avg_down));
		script_mask_set_effect_param(mask, g, 0, 0.0); // X
	}
	else if (step == 13) {
		posx  = copy_pixels(result());
		int n = differ(posx, posy, result()->width, result()->height);
		check(n > 1000, string("changing the axis changes the result (%d texels)", n));
		script_mask_set_effect_param(mask, g, AMOUNT, 0.3);
	}
	else if (step == 14) {
		noisy = copy_pixels(result());
		int n = changed(result(), posx);
		check(n > 1000, string("noise changes texels (%d)", n));
		script_mask_set_effect_param(mask, g, SEED, 7.0);
	}
	else if (step == 15) {
		int n = changed(result(), noisy);
		check(n > 1000, string("another seed changes texels (%d)", n));
		script_mask_set_effect_param(mask, g, AMOUNT, 0.0);
	}
	else if (step == 16) {
		int n = changed(result(), posx);
		check(n < 20, string("noise amount 0 is the same as no noise, whatever the seed (%d texels differ)", n));
		// Every covered texel white: start = end = 0
		script_mask_set_effect_param(mask, g, 2, 0.0);
		script_get_config()->dilate_radius = 1;
	}
	else if (step == 17) {
		gpu_texture_t *t = result();
		buffer_t      *p = script_get_texture_pixels(t);
		w1               = count_box(p, t->width, t->height, 0, 100, 0, 100, 128, 255);
		int mid          = count_box(p, t->width, t->height, 0, 100, 0, 100, 20, 235);
		check(w1 > 1000, string("covered texels are white (%d)", w1));
		check(mid < 20, string("and the rest black (%d between)", mid));
		r0                                 = mask_gen_renders();
		script_get_config()->dilate_radius = 6;
	}
	else if (step == 18) {
		gpu_texture_t *t = result();
		buffer_t      *p = script_get_texture_pixels(t);
		int            w6  = count_box(p, t->width, t->height, 0, 100, 0, 100, 128, 255);
		int            mid = count_box(p, t->width, t->height, 0, 100, 0, 100, 20, 235);
		check(mask_gen_renders() == r0 + 1, "a new dilation radius renders again");
		check(w6 > w1 + 200, string("texels outside the uv islands take their neighbour's value (%d white, %d at radius 1)", w6, w1));
		check(mid < 20, string("dilation copies values, it does not blend them (%d between)", mid));
		script_get_config()->dilate_radius = 2;
		use_generator(DIRECTIONAL); // +Y
	}
	else if (step == 19) {
		int n = changed(result(), dirp);
		check(n < 20, string("directional +Y again (%d texels differ)", n));
		r0 = mask_gen_renders();
		rotate_main(1.0, 0.0); // Upside down
	}
	else if (step == 20) {
		check(mask_gen_renders() > r0, "a mesh transform renders directional again");
		int n = changed(result(), dirn);
		check(n < 200, string("upside down, +Y gives what -Y gave (%d texels differ)", n));
		rotate_main(0.0, 1.0);
	}
	else if (step == 21) {
		int n = changed(result(), dirp);
		check(n < 20, string("transform back, the result is back (%d texels differ)", n));
		// Every param set, the unused ones too
		use_generator(EDGE_WEAR);
		for (int i = 0; i < 16; ++i) {
			script_mask_set_effect_param(mask, g, i, 0.05 * (i + 1));
		}
		script_mask_set_effect_param(mask, g, 0, 0.7);       // Amount
		script_mask_set_effect_param(mask, g, 12, 6.0);      // Noise scale
		script_mask_set_effect_param(mask, g, AMOUNT, 0.25); // Noise amount
		script_mask_set_effect_param(mask, g, SEED, 42.0);
		script_mask_set_effect_param(mask, g, 15, 4.0); // Detail
	}
	else if (step == 22) {
		snap  = copy_pixels(result());
		saved = string_copy(script_mask_effects_string(mask));
		check(white(result(), 0, 100, 0, 100) > 500, "edge wear with noise is not empty");
		file = test_path("mask_generators_test.arm");
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 23) {
		script_project_new();
	}
	else if (step == 24) {
		check(script_layer_get("gen_mask") == NULL, "a new project has no masks");
		r0 = mask_gen_renders();
		script_project_open(file);
	}
	else if (step == 25) {
		mask = script_layer_get("gen_mask");
		check(mask != NULL, "the mask is loaded");
		if (mask != NULL) {
			char *e = script_mask_effects_string(mask);
			check(string_equals(e, saved), string("all 16 params survive save and load (%s)", e));
			int n = changed(result(), snap);
			check(n < 50, string("the loaded generator renders the same pixels (%d texels differ)", n));
			check(mask_gen_renders() > r0, "and it rendered after load");
			int old = smart_materials_find("mg_test_smart");
			if (old >= 0) {
				smart_materials_delete(old);
			}
			check(smart_materials_save(mask->parent, "mg_test_smart"), "saved as a smart material");
		}
	}
	else if (step == 26) {
		script_project_new_template("cylinder");
	}
	else if (step == 27) {
		root = smart_materials_apply("mg_test_smart");
	}
	else if (step == 28) {
		mask = NULL;
		for (int i = 0; root != NULL && i < script_layer_count(); ++i) {
			slot_layer_t *m = script_layer_at(i);
			if (m->parent == root && m->texpaint_nor == NULL && m->texpaint != NULL) {
				mask = m;
			}
		}
		check(mask != NULL, "the smart material brings its mask");
		if (mask != NULL) {
			check(string_equals(script_mask_effects_string(mask), saved), "with the generator's params");
			check(mesh_maps_get_asset(1) == NULL && mesh_maps_auto_curvature() != NULL, "the other mesh has its own baked curvature");
			int n = white(result(), 0, 100, 0, 100);
			int d = changed(result(), snap);
			check(n > 200, string("edge wear on the other mesh is not empty (%d white)", n));
			check(d > 1000, string("and differs from the first mesh's (%d texels)", d));
		}
		smart_materials_delete(smart_materials_find("mg_test_smart"));
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[mask_generators] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MASK_GEN PASS" : "MASK_GEN FAIL");
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
