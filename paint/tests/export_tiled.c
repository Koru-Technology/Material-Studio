#include "global.h"

// Texture export in tiles (io/export_tiled.c): an export in forced 512 px tiles against the same export as one whole
// tile of the 2048 project, for every preset (file names as the presets name them), mode (visible, selected, per object,
// per udim tile) and bake material: pixels within 1/255 (a few fill layer edge texels apart). With padding the live
// layers are left as they were and the islands match. Above the working resolution, at 4096: the files at that size,
// the Material Maker cap raised, and tiles larger than the working resolution give what 512 px tiles give.

char *root = "";

int checks = 0;
int fails  = 0;
int step   = 0;
int frame  = 0;
int sub    = 0;

slot_layer_t    *layer_red;
slot_layer_t    *layer_mask;
slot_layer_t    *layer_paint;
slot_layer_t    *layer_fill;
slot_material_t *mat_noise;
char            *presets = "base_color,generic,minecraft_mer,specular,unigine,unity,unreal,xplane";
any_array_t     *preset_list;
char            *hashes_before;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_tiled] %s %s", ok ? "ok  " : "FAIL", name);
}

char *fresh_dir(char *name) {
	char *dir = string("%s/%s", root, name);
	iron_create_directory(dir);
	any_array_t *old = file_read_directory(dir);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", dir, old->buffer[i]));
	}
	return dir;
}

int small_tile = 512; // Tiles of the tiled export, the whole one is the output's size

// One whole tile into <name>_l, small tiles into <name>_t
void export_both(char *name, int bake, int whole) {
	export_tiled_test_tile(whole);
	export_texture_run(fresh_dir(string("%s_l", name)), bake);
	export_tiled_test_tile(small_tile);
	export_texture_run(fresh_dir(string("%s_t", name)), bake);
}

// The files of dir are exactly the comma separated names
int has_files(char *dir, char *names) {
	any_array_t *want = string_split(names, ",");
	any_array_t *have = file_read_directory(dir);
	if (have == NULL || have->length != want->length) {
		return 0;
	}
	for (int i = 0; i < want->length; ++i) {
		int found = 0;
		for (int j = 0; j < have->length; ++j) {
			found = found || string_equals(have->buffer[j], want->buffer[i]);
		}
		if (!found) {
			printf("[export_tiled]   %s missing", want->buffer[i]);
			return 0;
		}
	}
	return 1;
}

// File names of the visible export per preset, as the presets name their textures
char *expected_files(char *preset) {
	if (string_equals(preset, "base_color")) {
		return "untitled_base.png";
	}
	if (string_equals(preset, "generic")) {
		return "untitled_base.png,untitled_nor.png,untitled_occ.png,untitled_rough.png,untitled_metal.png";
	}
	if (string_equals(preset, "minecraft_mer")) {
		return "untitled.png,untitled_normal.png,untitled_mer.png";
	}
	if (string_equals(preset, "specular")) {
		return "untitled_diff.png,untitled_spec.png,untitled_nor.png,untitled_occ.png,untitled_smooth.png";
	}
	if (string_equals(preset, "unigine")) {
		return "untitled_base.png,untitled_nor.png,untitled_sh.png,untitled_occ.png";
	}
	if (string_equals(preset, "unity")) {
		return "untitled_base.png,untitled_nor.png,untitled_mos.png,untitled_height.png";
	}
	if (string_equals(preset, "unreal")) {
		return "untitled_base.png,untitled_nor.png,untitled_orm.png";
	}
	return "untitled_base.png,untitled_nor.png";
}

// Fill layers render for each tile (rasterized through the tile's uv transform): a few texels on triangle edges may
// differ from the live layer by some 1/255. Values over tol: at most this fraction, and at most edge_max apart.
float edge_fraction = 0.0001;
float edge_max      = 4.0;

// Same files in both, each within tol (1/255 units) on the texels the diff mask allows. size: of the files.
void compare_size(char *name, float tol, char *what, int size) {
	char        *l  = string("%s/%s_l", root, name);
	char        *t  = string("%s/%s_t", root, name);
	any_array_t *fl = file_read_directory(l);
	any_array_t *ft = file_read_directory(t);
	int          n  = fl != NULL ? fl->length : 0;
	int          ok = n > 0 && ft != NULL && ft->length == n;
	float        max = 0.0;
	int          over = 0;
	for (int i = 0; ok && i < n; ++i) {
		char *f     = fl->buffer[i];
		int   found = 0;
		for (int j = 0; j < ft->length; ++j) {
			if (string_equals(ft->buffer[j], f)) {
				found = 1;
			}
		}
		if (!found) {
			printf("[export_tiled]   %s missing in the small tile export", f);
			ok = 0;
		}
		else if (ends_with(f, ".png") || ends_with(f, ".jpg")) {
			int d = script_diff_images(string("%s/%s", l, f), string("%s/%s", t, f), tol);
			if (d > 0 && d <= size * size * 4 * edge_fraction && script_diff_max() <= edge_max) {
				printf("[export_tiled]   %s: %d values on fill edges over %.1f, max %.1f", f, d, tol, script_diff_max());
			}
			else if (d != 0) {
				printf("[export_tiled]   %s: %d values over %.1f, max %.1f, mean %.4f", f, d, tol, script_diff_max(), script_diff_mean());
				ok = 0;
			}
			if (script_diff_max() > max) {
				max = script_diff_max();
			}
			over = over + (d > 0 ? d : 0);
		}
	}
	check(ok, string("%s: %d files, max diff %.2f/255 (%s)", name, n, max, what));
}

void compare(char *name, float tol, char *what) {
	compare_size(name, tol, what, 2048);
}

void cfg_shadow_off() {
	config_t *cfg        = script_get_config();
	cfg->timeline_shadow = 0;
}

char *layer_hashes() {
	char *s = "";
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->texpaint != NULL) {
			s = string("%s %s", s, script_texture_hash(l->texpaint));
		}
		if (l->texpaint_nor != NULL) {
			s = string("%s %s", s, script_texture_hash(l->texpaint_nor));
		}
		if (l->texpaint_pack != NULL) {
			s = string("%s %s", s, script_texture_hash(l->texpaint_pack));
		}
	}
	return s;
}

void build_scene() {
	context_t *ctx = script_get_context();
	mat_noise      = script_material_create("et_noise");
	ui_node_t *noise = script_material_create_node_at("TEX_NOISE", -600.0, 0.0);
	ui_node_t *out   = script_material_get_node("OUTPUT_MATERIAL_PBR");
	script_material_set_float(noise, 1, 1, 6.0);
	script_material_set_float(noise, 1, 2, 3.0);
	script_material_connect(noise, 0, out, 0);
	script_material_connect(noise, 1, out, 3);
	script_material_connect(noise, 1, out, 4);
	script_material_update();

	layer_red               = script_layer_new_color(0xffcc2020, 0.8, 0.4, 0.3);
	layer_red->mask_opacity = 0.8;
	layer_red->blending     = 2; // Multiply
	script_layer_update(layer_red);

	layer_mask = script_layer_new_mask(layer_red, 1);
	int g      = script_mask_add_effect(layer_mask, 9, -1); // Noise generator
	script_mask_set_effect(layer_mask, g, 1, 1.0, 2);
	int b = script_mask_add_effect(layer_mask, 13, -1); // Blur
	script_mask_set_effect_param(layer_mask, b, 0, 6.0);

	layer_paint = script_layer_new();
	script_layer_set_name(layer_paint, "et_paint");
	context_select_tool(TOOL_TYPE_BRUSH);
	ctx->brush_radius   = 0.4;
	ctx->brush_opacity  = 1.0;
	ctx->brush_hardness = 0.6;
	for (int i = 0; i < 12; ++i) {
		script_paint(0.3 + i * 0.035, 0.35 + i * 0.025);
	}
	script_paint_end();
	for (int i = 0; i < 8; ++i) {
		script_paint(0.65 - i * 0.04, 0.3 + i * 0.05);
	}
	script_paint_end();

	layer_fill               = script_layer_new_fill();
	layer_fill->mask_opacity = 0.6;
	layer_fill->blending     = 4; // Lighten
	script_layer_update(layer_fill);
}

// Two objects for per object exports, two udim tiles for per udim tile exports
char *obj_two = "o et_a\nv -2 0 0\nv -1 0 0\nv -1 1 0\nv -2 1 0\nvt 0 0\nvt 0.5 0\nvt 0.5 0.5\nvt 0 0.5\nf 1/1 2/2 3/3 4/4\n"
                "o et_b\nv 1 0 0\nv 2 0 0\nv 2 1 0\nv 1 1 0\nvt 0.5 0.5\nvt 1 0.5\nvt 1 1\nvt 0.5 1\nf 5/5 6/6 7/7 8/8\n";
char *obj_udim = "o et_q.1001\nv -2 -2 0\nv -1 -2 0\nv -1 -1 0\nv -2 -1 0\nvt 0.1 0.1\nvt 0.9 0.1\nvt 0.9 0.9\nvt 0.1 0.9\nf 1/1 2/2 3/3 4/4\n"
                 "o et_q.1002\nv 1 -2 0\nv 2 -2 0\nv 2 -1 0\nv 1 -1 0\nvt 1.1 0.1\nvt 1.9 0.1\nvt 1.9 0.9\nvt 1.1 0.9\nf 5/5 6/6 7/7 8/8\n";

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		build_scene();
		preset_list = string_split(presets, ",");
	}
	else if (step == 1) { // Every preset, visible layers, png
		char *p = preset_list->buffer[sub];
		export_texture_test_options(p, 0, 0, 8, 0);
		export_both(string("vis_%s", p), 0, 2048);
		compare(string("vis_%s", p), 1.0, "visible");
		check(has_files(string("%s/vis_%s_t", root, p), expected_files(p)), string("%s: files named as the preset names them", p));
		sub = sub + 1;
		if (sub < preset_list->length) {
			step = step - 1;
		}
		else {
			sub = 0;
		}
	}
	else if (step == 2) { // Jpg, selected layer, 16 bit exr
		export_texture_test_options("generic", 0, 0, 8, 1);
		export_both("jpg", 0, 2048);
		compare("jpg", 3.0, "jpg");
		export_texture_test_options("generic", 1, 0, 8, 0);
		script_layer_set(layer_paint);
		export_both("selected", 0, 2048);
		compare("selected", 1.0, "selected layer");
		export_texture_test_options("generic", 0, 0, 16, 0);
		export_both("exr16", 0, 2048);
		compare("exr16", 1.0, "16 bit exr, names");
		check(has_files(string("%s/exr16_t", root), "untitled_base.exr,untitled_nor.exr,untitled_occ.exr,untitled_rough.exr,untitled_metal.exr"), "16 bit exr file names");
		// Streamed in bands of tiles, the same bytes as encoded whole
		export_tiled_test_whole(1);
		export_texture_run(fresh_dir("exr16w_t"), 0);
		export_tiled_test_whole(0);
		any_array_t *files = file_read_directory(string("%s/exr16_t", root));
		int          bad   = 0;
		for (int i = 0; files != NULL && i < files->length; ++i) {
			bad = bad + (script_diff_files(string("%s/exr16w_t/%s", root, files->buffer[i]), string("%s/exr16_t/%s", root, files->buffer[i])) != 0);
		}
		check(files != NULL && files->length == 5 && bad == 0, string("streamed exr files byte equal to whole ones (%d differ)", bad));
		export_texture_test_options("generic", 0, 0, 8, 0);
	}
	else if (step == 3) { // Bake material
		script_material_set(mat_noise);
		export_both("bake", 1, 2048);
		compare("bake", 1.0, "bake material");
	}
	else if (step == 4) {
		script_append_mesh_obj(obj_two);
	}
	else if (step == 5) { // Per object
		any_array_t *objects = script_get_mesh_objects();
		mesh_object_t *a     = objects->buffer[objects->length - 2];
		mesh_object_t *bo    = objects->buffer[objects->length - 1];
		script_layer_set_object(layer_red, a->base);
		script_layer_set_object(layer_fill, bo->base);
		export_texture_test_options("generic", 2, 0, 8, 0);
		export_both("per_object", 0, 2048);
		compare("per_object", 1.0, "per object");
		check(has_files(string("%s/per_object_t", root), "untitled_et_a_base.png,untitled_et_a_nor.png,untitled_et_a_occ.png,untitled_et_a_rough.png,untitled_et_a_metal.png,"
		                                                 "untitled_et_b_base.png,untitled_et_b_nor.png,untitled_et_b_occ.png,untitled_et_b_rough.png,untitled_et_b_metal.png"),
		      "per object file names");
		export_texture_test_options("unreal", 2, 0, 8, 0);
		export_both("per_object_unreal", 0, 2048);
		compare("per_object_unreal", 1.0, "per object, unreal");
		script_layer_set_object(layer_red, NULL);
		script_layer_set_object(layer_fill, NULL);
	}
	else if (step == 6) {
		script_append_mesh_obj(obj_udim);
	}
	else if (step == 7) { // Per udim tile, and the atlas whole
		export_texture_test_options("generic", 3, 0, 8, 0);
		export_both("udim", 0, 2048);
		compare("udim", 1.0, "per udim tile");
		any_array_t *objects = script_get_mesh_objects();
		mesh_object_t *q2    = objects->buffer[objects->length - 1];
		script_layer_set_object(layer_fill, q2->base);
		export_both("udim_tile_layer", 0, 2048);
		compare("udim_tile_layer", 1.0, "per udim tile, a layer on one tile");
		export_texture_test_options("generic", 0, 0, 8, 0);
		export_both("udim_visible", 0, 2048);
		compare("udim_visible", 1.0, "udim atlas whole");
		script_layer_set_object(layer_fill, NULL);
	}
	else if (step == 8) { // Paint layers come from their timelines (timeline_shadow), restored for each tile
		export_texture_test_options("generic", 0, 0, 8, 0);
		export_both("timeline", 0, 2048);
		compare("timeline", 1.0, "layers from their timelines");
		check(export_tiled_stat(9) > 0 && export_tiled_stat(10) == 0, string("%d layer tiles from timelines, %d fell back", export_tiled_stat(9), export_tiled_stat(10)));
	}
	else if (step == 9) { // Padding: live layers untouched, islands as one whole tile pads them
		export_texture_test_options("generic", 0, 1, 8, 0);
		hashes_before = layer_hashes();
		export_both("pad", 0, 2048);
		check(string_equals(hashes_before, layer_hashes()), "padding leaves the live layers as they were");
		check(export_tiled_stat(0) <= export_tiled_stat(1), string("peak tile target bytes %d KB within budget %d KB", export_tiled_stat(0), export_tiled_stat(1)));
		check(export_tiled_stat(2) > 4, string("%d tiles rendered, %d skipped", export_tiled_stat(2), export_tiled_stat(3)));
		script_diff_mask(export_tiled_test_coverage(), 1);
		compare("pad", 1.0, "padding, inside uv islands");
		export_tiled_test_raster(1);
		export_both("padr", 0, 2048);
		export_tiled_test_raster(0);
		compare("padr", 1.0, "padding, uv coverage rasterized per tile, inside uv islands");
		script_diff_mask(NULL, 0);
		export_texture_test_options("generic", 0, 0, 8, 0);
	}
	else if (step == 10) { // Export resolution above the working one: files at that size, the Material Maker cap raised
		export_tiled_set_resolution(4096, 4096);
		export_both("hires", 0, 3072); // Tiles larger than the working resolution (a whole 4096 one exceeds the budget)
		export_tiled_set_resolution(0, 0);
		check(export_tiled_stat(4) == 5 && export_tiled_stat(2) == 64, string("4096 export: %d files, %d tiles", export_tiled_stat(4), export_tiled_stat(2)));
		check(export_tiled_stat(5) == 4096, string("Material Maker cap %d", export_tiled_stat(5)));
		check(mm_buffers_stat(14) == 0, "Material Maker cap restored");
		// A tile larger than the working resolution covers all of itself (fill layers: the stroke coverage targets are
		// attachments of the fill pass, Metal clips the pass to the smallest one)
		compare_size("hires", 1.0, "4096: 3072 px tiles against 512 px tiles", 4096);
	}
	else if (step == 11) { // Layers an export above the working resolution resamples
		int n         = export_tiled_not_upscaled(4096, 4096);
		int has_paint = 0;
		int has_fill  = 0;
		for (int i = 0; i < n; ++i) {
			has_paint = has_paint || string_equals(export_tiled_not_upscaled_name(i), "et_paint");
			has_fill  = has_fill || string_equals(export_tiled_not_upscaled_name(i), layer_fill->name);
		}
		check(n > 0 && has_paint && has_fill, string("4096 with udim tiles: %d layers not upscaled, the fill layer too (udim fills from the live layer)", n));
		check(export_tiled_not_upscaled(0, 0) == 0, "working resolution: none");
	}
	else {
		export_tiled_test_tile(0);
		cfg_shadow_off();
		printf("[export_tiled] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_TILED PASS" : "EXPORT_TILED FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = script_tmp_dir("materialstudio_export_tiled"); // Per run: suites of several worktrees run at once
	config_t *cfg        = script_get_config();
	cfg->timeline_shadow = 1; // Paint layers record their timelines, the tiled export reads them
	script_project_new();
	iron_create_directory(root);
	script_notify_on_next_frame(next);
}
