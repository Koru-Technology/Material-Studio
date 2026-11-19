#include "global.h"

// Bake Mesh Maps (app_api_bake.h): maps bake from the paint mesh and register as the project's mesh maps; a high poly
// file is read into the ray tracer only (the scene and the project textures stay as they were), bakes against it, and
// is released when the sheet closes. A bake making a normal, occlusion or height map adds a fill layer "<mesh> Baked
// Maps" with those channels only.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int wait   = 0;

int objects_before = 0;
int textures_before = 0;
int layers_before  = 0;
char *high = "";

// app_bake_map_t bits
int NORMAL    = 1;
int HEIGHT    = 2;
int AO        = 4;
int CURVATURE = 8;
// app_layer_channel_t bits
int CH_NORMAL    = 4;
int CH_HEIGHT    = 64;
int CH_OCCLUSION = 1024;
int CH_BLENDS    = 136; // Normal Blend and Height Blend: how those channels mix, left at their defaults
// mesh_map_t
int MM_AO        = 0;
int MM_CURVATURE = 1;
int MM_NORMAL    = 6;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[bake_maps] %s %s", ok ? "ok  " : "FAIL", name);
}

int objects() {
	any_array_t *a = script_get_mesh_objects();
	return a->length;
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	// A bake runs over frames: wait for it, at most ~60 s
	if (app_bake_running()) {
		wait = wait + 1;
		if (wait < 900) {
			script_notify_on_next_frame(next);
			return;
		}
		check(0, "the bake finished in time");
		app_bake_cancel();
	}
	wait = 0;

	if (step == 0) {
		high = string("%s/koru_bake_high.obj", script_save_path());
		script_export_mesh(high);
		check(iron_file_exists(high), "the mesh exported as a high poly file");
		app_test_bake_options(NORMAL + HEIGHT + AO + CURVATURE, 1024, 16);
		check(app_test_bake_preview(0) > 0, "the low poly preview renders");
		layers_before = app_layer_count();
		check(app_bake_start(), "a bake from the paint mesh starts");
	}
	else if (step == 1) {
		check(app_layer_count() == layers_before + 1, string("the bake adds a layer (%d, was %d)", app_layer_count(), layers_before));
		check(ends_with(app_test_layer_name(), " Baked Maps"), string("named after the bake (%s)", app_test_layer_name()));
		int ch = app_test_layer_channels();
		check(ch == CH_NORMAL + CH_HEIGHT + CH_OCCLUSION + CH_BLENDS, string("normal, height and occlusion only (channels %d)", ch));
		check(app_mesh_map_texture(MM_AO) >= 0, "the occlusion is the project's AO map");
		check(app_mesh_map_texture(MM_CURVATURE) >= 0, "the curvature is the project's curvature map");
		check(app_mesh_map_texture(MM_NORMAL) >= 0, "the normal is the project's normal map");
		objects_before  = objects();
		textures_before = app_texture_count();
		int n = app_bake_set_high(high);
		check(n > 0, string("the high poly is read (%d triangles)", n));
		check(objects() == objects_before, string("the scene keeps its objects (%d, was %d)", objects(), objects_before));
		check(app_texture_count() == textures_before, "the project textures stay as they were");
		check(string_equals(app_bake_high_name(), "koru_bake_high.obj"), "its file name shows");
		check(app_test_bake_preview(1) > 0, "the high poly preview renders");
		check(app_test_bake_preview(0) > 0, "the low poly preview renders with missed rays");
		float missed = app_test_bake_missed();
		check(missed < 0.05, string("rays find the high poly, the same shape (%.3f missed)", missed));
		layers_before = app_layer_count();
		app_test_bake_options(AO + CURVATURE, 1024, 16);
		check(app_bake_start(), "a bake from the high poly starts");
	}
	else if (step == 2) {
		check(objects() == objects_before, "baking from the high poly adds no object");
		check(app_texture_count() == textures_before, "baking again replaces the maps, no new textures");
		check(app_layer_count() == layers_before + 1, "a second baked layer");
		check(app_test_layer_channels() == CH_OCCLUSION + CH_BLENDS, string("occlusion only, the one map it baked of the three (%d)", app_test_layer_channels()));
		app_bake_close();
		check(string_equals(app_bake_high_name(), ""), "closing releases the high poly");
		app_test_bake_options(0, 1024, 16);
		check(!app_bake_start(), "no map selected, no bake");
		layers_before = app_layer_count();
		app_test_bake_options(CURVATURE, 1024, 16);
		check(app_bake_start(), "a curvature bake starts");
	}
	else if (step == 3) {
		check(app_layer_count() == layers_before, "a bake without normal, occlusion or height adds no layer");
		check(objects() == objects_before, "the scene still has its objects only");
		iron_delete_file(high);
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[bake_maps] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "BAKE_MAPS PASS" : "BAKE_MAPS FAIL");
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
