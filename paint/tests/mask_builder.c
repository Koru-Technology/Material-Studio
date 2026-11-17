#include "global.h"

// Mask Builder, the Ambient Occlusion generator, the position, world normal and normal maps, and fills of an image or
// grunge. The Mask Builder's parts each match the generator they stand for (Edge Wear, Dirt, Ambient Occlusion,
// Directional, Position Gradient), inverting complements it, micro details follow the normal map. A registered world
// normal or position map replaces the mesh's own. Image fills project on the uvs (scale, rotation, balance, invert) or
// triplanar, every grunge renders something, a seed changes it, deleting images keeps the references right, save and
// load keep it, a smart material drops the image.
// The maps: curvature grows from 0 to 1 along u (concave left, flat in the middle, convex right), AO along v.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *layer   = NULL;
slot_layer_t *mask    = NULL;
slot_layer_t *root    = NULL;
int           g       = -1; // The effect under test, stack index
int           covered = 0;  // Texels the mesh covers (white image fill)
int           r0      = 0;
char         *file    = NULL;
char         *saved   = NULL;
buffer_t     *cov     = NULL; // White image fill: the covered texels
buffer_t     *ref     = NULL; // A reference generator's pixels
buffer_t     *a       = NULL;
int           pattern = 0;

int FILL        = 1;
int EDGE_WEAR   = 4;
int DIRT        = 5;
int POSITION    = 6;
int DIRECTIONAL = 7;
int BUILDER     = 15;
int AO_GEN      = 16;
int AMOUNT      = 13; // Noise
int SEED        = 14;

// Mask Builder settings
int B_EDGES    = 2;
int B_CAVITY   = 3;
int B_AO       = 4;
int B_NORMAL   = 5;
int B_POSITION = 7;
int B_MICRO    = 9;
int B_INVERT   = 10;

// Fill settings
int F_SOURCE     = 1;
int F_IMAGE      = 2;
int F_PATTERN    = 3;
int F_PROJECTION = 4;
int F_SCALE      = 5;
int F_ROTATION   = 6;
int F_BALANCE    = 7;
int F_INVERT     = 9;
int F_SEED       = 10;

int WORLD_NORMAL_MAP = 5;
int POSITION_MAP     = 4;
int NORMAL_MAP       = 6;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mask_builder] %s %s", ok ? "ok  " : "FAIL", name);
}

char *test_path(char *name) {
	return string("%s/%s", script_save_path(), name);
}

// 256x256 image of kind. iron_write_png takes the byte order of render target readbacks (bgra on Apple): the colored
// ones keep r and b equal, the same either way.
void write_image(char *path, int kind) {
	int       w = 256;
	buffer_t *p = buffer_create(w * w * 4);
	for (int y = 0; y < w; ++y) {
		for (int x = 0; x < w; ++x) {
			int i  = (y * w + x) * 4;
			int r  = 0;
			int gg = 0;
			int bb = 0;
			if (kind == 0) { // Ramp across u
				r  = x;
				gg = x;
				bb = x;
			}
			else if (kind == 1) { // Ramp down v
				r  = y;
				gg = y;
				bb = y;
			}
			else if (kind == 2) { // Left half white
				r  = x < 128 ? 255 : 0;
				gg = r;
				bb = r;
			}
			else if (kind == 3) { // White
				r  = 255;
				gg = 255;
				bb = 255;
			}
			else if (kind == 4) { // Up and toward +X (+Z +X in the engine): a world normal, or a corner of a position map
				r  = 218;
				gg = 128;
				bb = 218;
			}
			else { // Normal map of saw teeth along u: slopes rising over 16 texels (x, and z alike)
				r  = 28 + (x % 16) * 13;
				gg = 128;
				bb = r;
			}
			buffer_set_u8(p, i, r);
			buffer_set_u8(p, i + 1, gg);
			buffer_set_u8(p, i + 2, bb);
			buffer_set_u8(p, i + 3, 255);
		}
	}
	iron_write_png(path, p, w, w, 0);
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

// Texels of p and q apart by more than 10, every other texel each way
int differ(buffer_t *p, buffer_t *q, int w, int h) {
	int n = 0;
	for (int y = 0; y < h; y += 2) {
		for (int x = 0; x < w; x += 2) {
			int i = (y * w + x) * 4;
			int d = buffer_get_u8(p, i) - buffer_get_u8(q, i);
			if (d > 10 || d < -10) {
				n = n + 1;
			}
		}
	}
	return n;
}

int changed(buffer_t *q) {
	gpu_texture_t *t = result();
	return differ(script_get_texture_pixels(t), q, t->width, t->height);
}

// Replaces the effect under test with a new one of type, noise off
void use(int type) {
	if (g >= 0) {
		script_mask_remove_effect(mask, g);
	}
	g = script_mask_add_effect(mask, type, -1);
	script_mask_set_effect_param(mask, g, AMOUNT, 0.0);
}

void set(int param, float v) {
	script_mask_set_effect_param(mask, g, param, v);
}

int asset(char *name) {
	return script_asset_index(name);
}

// An image fill of the asset whose file has name in it
void use_image(char *name) {
	use(FILL);
	set(F_SOURCE, 1.0);
	set(F_IMAGE, asset(name) * 1.0);
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
		write_image(test_path("koru_mb_test_curvature.png"), 0);
		write_image(test_path("koru_mb_test_ao.png"), 1);
		write_image(test_path("koru_mb_test_half.png"), 2);
		write_image(test_path("koru_mb_test_white.png"), 3);
		write_image(test_path("koru_mb_test_up_world_normal.png"), 4);
		write_image(test_path("koru_mb_test_up_position.png"), 4);
		write_image(test_path("koru_mb_test_teeth_normal.png"), 5);
		script_import_asset(test_path("koru_mb_test_curvature.png"), 0);
		script_import_asset(test_path("koru_mb_test_ao.png"), 0);
		script_import_asset(test_path("koru_mb_test_half.png"), 0);
		script_import_asset(test_path("koru_mb_test_white.png"), 0);
		layer = script_layer_new_color(0xffffffff, 1.0, 0.5, 0.0);
		mask  = script_layer_new_mask(layer, 0); // Black
		script_layer_set_name(mask, "mb_mask");
	}
	else if (step == 1) {
		check(mesh_maps_get_asset(1) != NULL && mesh_maps_get_asset(0) != NULL, "curvature and ao maps registered");
		check(asset("koru_mb_test_half") >= 0 && asset("koru_mb_test_white") >= 0, "images imported");
		use_image("koru_mb_test_white");
	}
	else if (step == 2) {
		cov     = copy_pixels(result());
		covered = white(result(), 0, 100, 0, 100);
		check(covered > 2000, string("a white image fill covers the mesh's uv islands (%d)", covered));
		use(EDGE_WEAR);
	}
	else if (step == 3) {
		ref = copy_pixels(result());
		use(BUILDER); // Edges 1
	}
	else if (step == 4) {
		int n = changed(ref);
		check(white(result(), 85, 100, 0, 100) > 500, "mask builder: convex texels white by default");
		check(n < 20, string("mask builder edges = edge wear (%d texels differ)", n));
		use(DIRT);
		script_mask_set_effect_param(mask, g, 2, 0.0); // Cavity only
	}
	else if (step == 5) {
		ref = copy_pixels(result());
		use(BUILDER);
		set(B_EDGES, 0.0);
		set(B_CAVITY, 1.0);
	}
	else if (step == 6) {
		int n = changed(ref);
		check(white(result(), 0, 20, 0, 100) > 500, "mask builder cavity: concave texels white");
		check(n < 20, string("mask builder cavity = dirt without ao (%d texels differ)", n));
		use(AO_GEN); // Occluded
	}
	else if (step == 7) {
		gpu_texture_t *t = result();
		ref              = copy_pixels(t);
		check(white(t, 0, 100, 0, 35) > 500, "ambient occlusion: occluded texels (low ao) white");
		check(white(t, 0, 100, 60, 100) < 20, "ambient occlusion: open texels black");
		set(0, 1.0); // Open
	}
	else if (step == 8) {
		gpu_texture_t *t = result();
		check(white(t, 0, 100, 0, 35) < 20 && white(t, 0, 100, 60, 100) > 500, "ambient occlusion open: the other side");
		use(BUILDER);
		set(B_EDGES, 0.0);
		set(B_AO, 1.0);
	}
	else if (step == 9) {
		int n = changed(ref);
		check(n < 20, string("mask builder ao = the ambient occlusion generator (%d texels differ)", n));
		use(DIRECTIONAL); // +Y
	}
	else if (step == 10) {
		ref = copy_pixels(result());
		use(BUILDER);
		set(B_EDGES, 0.0);
		set(B_NORMAL, 1.0); // +Y
	}
	else if (step == 11) {
		gpu_texture_t *t      = result();
		buffer_t      *p      = script_get_texture_pixels(t);
		int            up     = 0;
		int            astray = 0;
		for (int y = 0; y < t->height; y += 2) {
			for (int x = 0; x < t->width; x += 2) {
				int i = (y * t->width + x) * 4;
				if (buffer_get_u8(p, i) >= 200) {
					up = up + 1;
					if (buffer_get_u8(ref, i) < 128) {
						astray = astray + 1;
					}
				}
			}
		}
		check(up > 500, string("mask builder world normal +Y: faces up white (%d)", up));
		check(astray < 20, string("only where directional +Y is (%d astray)", astray));
		a = copy_pixels(t);
		set(B_INVERT, 1.0);
	}
	else if (step == 12) {
		gpu_texture_t *t   = result();
		buffer_t      *p   = script_get_texture_pixels(t);
		int            any = 0;
		int            off = 0;
		for (int y = 0; y < t->height; y += 2) {
			for (int x = 0; x < t->width; x += 2) {
				int i = (y * t->width + x) * 4;
				if (buffer_get_u8(cov, i) >= 200) {
					any   = any + 1;
					int s = buffer_get_u8(a, i) + buffer_get_u8(p, i) - 255;
					if (s > 6 || s < -6) {
						off = off + 1;
					}
				}
			}
		}
		check(any > 1000 && off * 100 < any, string("invert complements the mask builder (%d of %d texels off)", off, any));
		use(POSITION); // Y
	}
	else if (step == 13) {
		ref = copy_pixels(result());
		use(BUILDER);
		set(B_EDGES, 0.0);
		set(B_POSITION, 1.0); // +Y
	}
	else if (step == 14) {
		gpu_texture_t *t    = result();
		buffer_t      *p    = script_get_texture_pixels(t);
		int            high = 0;
		int            low  = 0;
		for (int y = 0; y < t->height; y += 2) {
			for (int x = 0; x < t->width; x += 2) {
				int i = (y * t->width + x) * 4;
				int v = buffer_get_u8(p, i);
				int r = buffer_get_u8(ref, i);
				if (v >= 200 && r < 128) {
					low = low + 1;
				}
				if (v >= 200) {
					high = high + 1;
				}
			}
		}
		check(high > 500, string("mask builder position +Y: the top white (%d)", high));
		check(low < 20, string("only where the position gradient is high (%d astray)", low));
		// Micro details: the normal map's teeth show in the edges
		use(BUILDER);
		script_import_asset(test_path("koru_mb_test_teeth_normal.png"), 0);
	}
	else if (step == 15) {
		check(mesh_maps_get_asset(NORMAL_MAP) != NULL, "normal map registered");
		a = copy_pixels(result());
		set(B_MICRO, 1.0);
	}
	else if (step == 16) {
		int n = changed(a);
		check(n > 500, string("micro details bring the normal map into the edges (%d texels)", n));
		mesh_maps_set(NORMAL_MAP, NULL);
		use(DIRECTIONAL); // +Y
	}
	else if (step == 17) {
		check(white(result(), 0, 100, 0, 100) < covered * 8 / 10, "directional +Y: not every face is up");
		script_import_asset(test_path("koru_mb_test_up_world_normal.png"), 0);
	}
	else if (step == 18) {
		check(mesh_maps_get_asset(WORLD_NORMAL_MAP) != NULL, "world normal map registered");
		int n = white(result(), 0, 100, 0, 100);
		check(n > covered * 95 / 100, string("a world normal map up and sideways turns every face up (%d of %d)", n, covered));
		mesh_maps_set(WORLD_NORMAL_MAP, NULL);
		use(POSITION); // Y
		set(1, 0.0);
		set(2, 1.0);
	}
	else if (step == 19) {
		a = copy_pixels(result());
		script_import_asset(test_path("koru_mb_test_up_position.png"), 0);
	}
	else if (step == 20) {
		check(mesh_maps_get_asset(POSITION_MAP) != NULL, "position map registered");
		gpu_texture_t *t  = result();
		buffer_t      *p  = script_get_texture_pixels(t);
		int            lo = 255;
		int            hi = 0;
		int            lo0 = 255;
		int            hi0 = 0;
		for (int y = 0; y < t->height; y += 2) {
			for (int x = 0; x < t->width; x += 2) {
				int i = (y * t->width + x) * 4;
				if (buffer_get_u8(cov, i) >= 200) {
					int v = buffer_get_u8(p, i);
					int w = buffer_get_u8(a, i);
					lo    = v < lo ? v : lo;
					hi    = v > hi ? v : hi;
					lo0   = w < lo0 ? w : lo0;
					hi0   = w > hi0 ? w : hi0;
				}
			}
		}
		check(hi0 - lo0 > 100, string("without it the gradient spans the mesh (%d - %d)", lo0, hi0));
		check(hi - lo < 8, string("a constant position map gives one value (%d - %d)", lo, hi));
		mesh_maps_set(POSITION_MAP, NULL);
		use_image("koru_mb_test_half");
	}
	else if (step == 21) {
		gpu_texture_t *t = result();
		check(white(t, 0, 45, 0, 100) > 500, "image fill: the white left half shows on the left");
		check(white(t, 55, 100, 0, 100) < 20, "and the right stays black");
		set(F_SCALE, 2.0);
	}
	else if (step == 22) {
		gpu_texture_t *t = result();
		check(white(t, 2, 22, 0, 100) > 200 && white(t, 52, 72, 0, 100) > 200, "scale 2: white in two stripes");
		check(white(t, 28, 47, 0, 100) < 20 && white(t, 78, 98, 0, 100) < 20, "and black between");
		set(F_SCALE, 1.0);
		set(F_ROTATION, 180.0);
	}
	else if (step == 23) {
		gpu_texture_t *t = result();
		check(white(t, 0, 45, 0, 100) < 20 && white(t, 55, 100, 0, 100) > 500, "rotation 180: the halves swap");
		set(F_ROTATION, 0.0);
		set(F_INVERT, 1.0);
	}
	else if (step == 24) {
		gpu_texture_t *t = result();
		check(white(t, 0, 45, 0, 100) < 20 && white(t, 55, 100, 0, 100) > 500, "invert: the halves swap");
		set(F_INVERT, 0.0);
		set(F_BALANCE, 0.0);
	}
	else if (step == 25) {
		check(white(result(), 0, 100, 0, 100) < 20, "balance 0: nothing white");
		set(F_BALANCE, 0.5);
		a = copy_pixels(result());
		set(F_PROJECTION, 1.0);
	}
	else if (step == 26) {
		int n = changed(a);
		check(n > 500, string("triplanar projects otherwise (%d texels)", n));
		check(white(result(), 0, 100, 0, 100) > 200, "and shows the image");
		set(F_SOURCE, 2.0); // Grunge
		pattern = 0;
		set(F_PATTERN, 0.0);
	}
	else if (step == 27) {
		gpu_texture_t *t = result();
		int            n = white(t, 0, 100, 0, 100);
		check(n > 50 && n < covered - 50, string("grunge %d: some white, not all (%d of %d)", pattern, n, covered));
		pattern = pattern + 1;
		if (pattern < 6) {
			set(F_PATTERN, pattern * 1.0);
			script_notify_on_next_frame(next);
			return; // Again for the next pattern
		}
		set(F_PATTERN, 1.0); // Scratches
	}
	else if (step == 28) {
		a = copy_pixels(result());
		set(F_SEED, 7.0);
	}
	else if (step == 29) {
		int n = changed(a);
		check(n > 50, string("another seed, other grunge (%d texels)", n));
		set(F_PROJECTION, 0.0);
	}
	else if (step == 30) {
		int n = changed(a);
		check(n > 50, string("grunge on the uvs differs from triplanar (%d texels)", n));
		// Deleting an image: the fills of later images follow, the fill of the deleted one reads none
		use_image("koru_mb_test_white");
		r0 = asset("koru_mb_test_white");
		check(util_texture_is_used(r0), "the image of a fill is in use");
		script_assets_remove("koru_mb_test_half");
	}
	else if (step == 31) {
		check(script_mask_get_effect_param(mask, g, F_IMAGE) == (r0 - 1) * 1.0 && asset("koru_mb_test_white") == r0 - 1,
		      "an earlier image deleted, the fill follows its image");
		int n = white(result(), 0, 100, 0, 100);
		check(n > covered - 50, string("and still shows it (%d of %d)", n, covered));
		script_assets_remove("koru_mb_test_white");
	}
	else if (step == 32) {
		check(script_mask_get_effect_param(mask, g, F_IMAGE) == -1.0, "its image deleted, the fill reads none");
		check(white(result(), 0, 100, 0, 100) < 20, "and is black");
		// Save and load: an image fill and a grunge fill
		write_image(test_path("koru_mb_test_half2.png"), 2);
		script_import_asset(test_path("koru_mb_test_half2.png"), 0);
	}
	else if (step == 33) {
		use_image("koru_mb_test_half2");
		int e = script_mask_add_effect(mask, FILL, -1);
		script_mask_set_effect_param(mask, e, F_SOURCE, 2.0);
		script_mask_set_effect_param(mask, e, F_PATTERN, 3.0); // Rust
		script_mask_set_effect_param(mask, e, F_PROJECTION, 1.0);
		script_mask_set_effect_param(mask, e, F_SEED, 42.0);
		script_mask_set_effect(mask, e, true, 1.0, 4); // Max
	}
	else if (step == 34) {
		a     = copy_pixels(result());
		saved = string_copy(script_mask_effects_string(mask));
		file  = test_path("mask_builder_test.arm");
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 35) {
		script_project_new();
	}
	else if (step == 36) {
		r0 = mask_gen_renders();
		script_project_open(file);
	}
	else if (step == 37) {
		mask = script_layer_get("mb_mask");
		check(mask != NULL, "the mask is loaded");
		if (mask != NULL) {
			char *e = script_mask_effects_string(mask);
			check(string_equals(e, saved), string("image and grunge fills survive save and load (%s)", e));
			int n = changed(a);
			check(n < 50, string("and render the same pixels (%d texels differ)", n));
			check(mask_gen_renders() > r0, "they rendered after load");
			int old = smart_materials_find("mb_test_smart");
			if (old >= 0) {
				smart_materials_delete(old);
			}
			check(smart_materials_save(mask->parent, "mb_test_smart"), "saved as a smart material");
		}
	}
	else if (step == 38) {
		script_project_new();
	}
	else if (step == 39) {
		root = smart_materials_apply("mb_test_smart");
	}
	else if (step == 40) {
		mask = NULL;
		for (int i = 0; root != NULL && i < script_layer_count(); ++i) {
			slot_layer_t *m = script_layer_at(i);
			if (m->parent == root && m->texpaint_nor == NULL && m->texpaint != NULL) {
				mask = m;
			}
		}
		check(mask != NULL, "the smart material brings its mask");
		if (mask != NULL) {
			check(script_mask_get_effect_param(mask, g, F_SOURCE) == 1.0 && script_mask_get_effect_param(mask, g, F_IMAGE) == -1.0,
			      "an image fill from a smart material reads none: the image stays in its project");
			check(script_mask_get_effect_param(mask, g + 1, F_SEED) == 42.0, "the grunge fill keeps its settings");
			check(white(result(), 0, 100, 0, 100) > 50, "and renders on the new project's mesh");
		}
		smart_materials_delete(smart_materials_find("mb_test_smart"));
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[mask_builder] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MASK_BUILDER PASS" : "MASK_BUILDER FAIL");
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
