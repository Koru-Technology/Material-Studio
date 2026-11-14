#include "global.h"

// Golden images: renders material node previews, Material Studio presets, library materials and the viewport in every
// mode to <save>/golden/*.png. Run once to make the baseline, again after a change, then compare the two folders
// (pixel differences show what the change did to the rendering). Shader compile failures are counted.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   sub    = 0;
int   wait   = 0;
int   saved  = 0;
char *dir    = NULL;

gpu_texture_t   *shot    = NULL;
slot_material_t *mat     = NULL;
any_array_t     *types   = NULL;
int              PRESETS = 16;
int              MODES   = 16; // Path trace (16) accumulates noise, left out
int              LIBRARY = 8;
int              library_ids[8] = {2, 4, 129, 171, 290, 542, 836, 1535}; // Material Maker library materials of different kinds

char *node_types = "RGB,TEX_BRICK,TEX_CHECKER,TEX_GABOR,TEX_GRADIENT,TEX_MAGIC,TEX_NOISE,TEX_VORONOI,TEX_WAVE,BRIGHTCONTRAST,"
                   "VALTORGB,COMBINE_COLOR,GAMMA,HUE_SAT,INVERT_COLOR,MIX_RGB,QUANTIZE,CURVE_RGB,RGBTOBW,REPLACECOL,"
                   "SEPARATE_COLOR,BUMP,CLAMP,COMBXYZ,FLOAT_CURVE,MAPRANGE,MAPPING,MATH,MIX_NORMAL_MAP,NORMAL,NORMAL_MAP,"
                   "SEPXYZ,CURVE_VEC,VECT_MATH,VECT_ROTATE,VECT_TRANSFORM,TEX_COORD,UVMAP,ATTRIBUTE,NEW_GEOMETRY,OBJECT_INFO,"
                   "VALUE,WIREFRAME,CURVATURE_MASK,HEIGHT_MASK,DIRECTION_MASK,MATERIAL_MIX,"
                   "PATTERN_ARCS,PATTERN_BRICKS,PATTERN_CELLS,PATTERN_CHAINMAIL,PATTERN_CRATERS,PATTERN_DOTS,PATTERN_FACETS,"
                   "PATTERN_FEATHERS,PATTERN_FIBERS,PATTERN_GRAIN,PATTERN_HERRINGBONE,PATTERN_HEX,PATTERN_MESH,PATTERN_MOTIF,"
                   "PATTERN_NOISE,PATTERN_PARQUET,PATTERN_PEBBLES,PATTERN_PLANKS,PATTERN_RADIAL,PATTERN_RINGS,PATTERN_ROPE,"
                   "PATTERN_SCATTER,PATTERN_SCRATCHES,PATTERN_SEIGAIHA,PATTERN_SHINGLES,PATTERN_STARS,PATTERN_STITCHES,"
                   "PATTERN_STREAKS,PATTERN_STRIPES,PATTERN_THATCH,PATTERN_TILES,PATTERN_TREAD,PATTERN_TRUCHET,PATTERN_TUFTED,"
                   "PATTERN_UNEVEN,PATTERN_WEAVE,PATTERN_WEB";

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[golden] %s %s", ok ? "ok  " : "FAIL", name);
}

// Any texture format, scaled into the 256 x 256 rgba8 target and written as png
void save(gpu_texture_t *tex, char *name) {
	if (tex == NULL) {
		printf("[golden]   missing %s", name);
		return;
	}
	draw_begin(shot, 1, 0xff000000);
	draw_scaled_image(tex, 0.0, 0.0, 256.0, 256.0);
	draw_end();
	iron_write_png(string("%s/%s.png", dir, name), script_get_texture_pixels(shot), 256, 256, 0);
	saved = saved + 1;
}

void save_viewport(char *name) {
	viewport_capture_screenshot_to(shot, 0.0, 0.0, 256.0, 256.0);
	iron_write_png(string("%s/%s.png", dir, name), script_get_texture_pixels(shot), 256, 256, 0);
	saved = saved + 1;
}

void next() {
	if (wait > 0) {
		wait = wait - 1;
		script_notify_on_next_frame(next);
		return;
	}

	if (step == 0) {
		// Material node previews: each node alone into base color
		if (sub == 0) {
			types = string_split(node_types, ",");
		}
		if (sub > 0) {
			save(mat->image, string("node_%s", types->buffer[sub - 1]));
			script_material_delete(mat);
		}
		if (sub < types->length) {
			char *type = types->buffer[sub];
			mat        = script_material_create(string("golden_%s", type));
			script_material_set(mat);
			ui_node_t *n = script_material_create_node_at(type, -400.0, 0.0);
			if (n != NULL && n->outputs->length > 0) {
				script_material_connect(n, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
			}
			script_material_update();
			script_material_set(mat);
			sub  = sub + 1;
			wait = 2;
		}
		else {
			step = 1;
			sub  = 0;
			printf("[golden] node previews done");
		}
	}
	else if (step == 1) {
		// Material Studio presets, the studio preview and the material icon after closing
		if (sub > 0) {
			save(material_studio_preview(), string("studio_%d", sub - 1));
			material_studio_close();
		}
		if (sub < PRESETS) {
			mat = material_studio_new();
			material_studio_preset(sub);
			sub  = sub + 1;
			wait = 12;
		}
		else {
			step = 2;
			sub  = 0;
			wait = 12;
			printf("[golden] studio presets done");
		}
	}
	else if (step == 2) {
		// Material Maker library materials, their sphere icon
		if (sub > 0) {
			save(mat->image, string("library_%d", library_ids[sub - 1]));
		}
		if (sub < LIBRARY) {
			mat  = mm_import_library(library_ids[sub]);
			sub  = sub + 1;
			wait = 6;
		}
		else {
			step = 3;
			sub  = 0;
			printf("[golden] library materials done");
		}
	}
	else if (step == 3) {
		// Viewport: a fill layer with a built-in material and a painted stroke, in every view mode
		if (sub == 0) {
			script_project_new();
			wait = 10;
			sub  = 1;
		}
		else if (sub == 1) {
			script_layer_new_fill();
			script_material_set(mm_import_library(1535));
			script_fill_layer();
			slot_layer_t *l = script_layer_new();
			script_material_set(material_studio_new());
			material_studio_preset(3);
			material_studio_close();
			script_paint(0.4, 0.5);
			script_paint(0.5, 0.5);
			script_paint(0.6, 0.55);
			script_paint_end();
			wait = 30;
			sub  = 2;
		}
		else if (sub - 2 < MODES) {
			if (sub > 2) {
				save_viewport(string("viewport_%d", sub - 3));
			}
			context_set_viewport_mode(sub - 2);
			wait = 30;
			sub  = sub + 1;
		}
		else {
			save_viewport(string("viewport_%d", MODES - 1));
			context_set_viewport_mode(0);
			step = 4;
		}
	}
	else {
		check(saved > 100, string("%d golden images in %s", saved, dir));
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[golden] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "GOLDEN PASS" : "GOLDEN FAIL");
		script_quit();
		return;
	}
	script_notify_on_next_frame(next);
}

void main() {
	dir = string("%sgolden", iron_internal_save_path());
	iron_create_directory(dir);
	shot = gpu_create_render_target(256, 256, 0);
	script_brush_defaults();
	script_project_new();
	script_notify_on_next_frame(next);
}
