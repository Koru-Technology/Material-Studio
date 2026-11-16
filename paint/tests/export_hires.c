#include "global.h"

// Tiled export above the working resolution (io/export_tiled.c): from a 2048 project at 4096, fill layers and mask
// generators render at 4096 and come out as the same project resized to 4096 exports them; paint layers come from their
// timelines restored at 2048 and scaled up, and are listed as not upscaled.

char *root = "/tmp/materialstudio_export_hires";

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *layer_fill;
slot_layer_t *layer_paint;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_hires] %s %s", ok ? "ok  " : "FAIL", name);
}

char *fresh_dir(char *name) {
	char *d = string("%s/%s", root, name);
	iron_create_directory(d);
	any_array_t *old = file_read_directory(d);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", d, old->buffer[i]));
	}
	return d;
}

void build_scene() {
	context_t       *ctx   = script_get_context();
	slot_material_t *m     = script_material_create("eh_noise");
	ui_node_t       *noise = script_material_create_node_at("TEX_NOISE", -600.0, 0.0);
	script_material_set_float(noise, 1, 1, 24.0); // Fine detail: lost when 2048 is scaled up
	script_material_set_float(noise, 1, 2, 6.0);
	script_material_connect(noise, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();

	layer_paint = script_layer_new();
	script_layer_set_name(layer_paint, "eh_paint");
	context_select_tool(TOOL_TYPE_BRUSH);
	ctx->brush_radius = 0.3;
	for (int i = 0; i < 10; ++i) {
		script_paint(0.3 + i * 0.04, 0.4 + i * 0.02);
	}
	script_paint_end();

	layer_fill = script_layer_new_fill();
	script_layer_set_name(layer_fill, "eh_fill");
	slot_layer_t *mask = script_layer_new_mask(layer_fill, 1);
	int           g    = script_mask_add_effect(mask, 6, -1); // Position gradient
	script_mask_set_effect(mask, g, 1, 1.0, 2);
}

float mean_diff(char *a, char *b) {
	int d = script_diff_images(a, b, 255.0);
	return d < 0 ? 999.0 : script_diff_mean();
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		build_scene();
	}
	else if (step == 1) {
		int n         = export_tiled_not_upscaled(4096, 4096);
		int has_paint = 0;
		int has_fill  = 0;
		for (int i = 0; i < n; ++i) {
			has_paint = has_paint || string_equals(export_tiled_not_upscaled_name(i), "eh_paint");
			has_fill  = has_fill || string_equals(export_tiled_not_upscaled_name(i), "eh_fill");
		}
		check(has_paint && !has_fill, string("%d layers not upscaled: the paint layer, not the fill layer", n));

		config_t *cfg     = script_get_config();
		cfg->export_tiled = 1;
		export_texture_test_options("generic", 0, 0, 8, 0);
		export_tiled_test_tile(1024);
		export_tiled_set_resolution(4096, 4096);
		export_texture_run(fresh_dir("t"), 0);
		check(export_tiled_stat(4) == 5 && export_tiled_stat(11) > 0, string("4096: %d files, %d fill tiles rendered", export_tiled_stat(4), export_tiled_stat(11)));
		check(export_tiled_stat(9) > 0 && export_tiled_stat(10) == 0, string("paint layer tiles from the timeline at 2048, scaled (%d, %d fell back)", export_tiled_stat(9), export_tiled_stat(10)));
		export_tiled_test_no_fill(1);
		export_texture_run(fresh_dir("bl"), 0);
		export_tiled_test_no_fill(0);
		export_tiled_set_resolution(0, 0);
		cfg->export_tiled = 0;
		config_set_texture_res(1); // The project at 4096: fill layers and masks render there natively
		layers_on_resized();
	}
	else if (step == 2) {
		export_texture_run(fresh_dir("l"), 0);
		char *native = string("%s/l/untitled_base.png", root);
		float tiled  = mean_diff(native, string("%s/t/untitled_base.png", root));
		float scaled = mean_diff(native, string("%s/bl/untitled_base.png", root));
		check(tiled < scaled * 0.5, string("base color against the native 4096 export: mean %.3f/255, fills scaled up from 2048: %.3f", tiled, scaled));
		native = string("%s/l/untitled_rough.png", root);
		tiled  = mean_diff(native, string("%s/t/untitled_rough.png", root));
		scaled = mean_diff(native, string("%s/bl/untitled_rough.png", root));
		check(tiled <= scaled, string("roughness: mean %.3f/255, scaled up: %.3f", tiled, scaled));
	}
	else {
		export_tiled_test_tile(0);
		config_t *cfg        = script_get_config();
		cfg->timeline_shadow = 0;
		printf("[export_hires] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_HIRES PASS" : "EXPORT_HIRES FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	config_t *cfg        = script_get_config();
	cfg->timeline_shadow = 1;
	script_project_new();
	iron_create_directory(root);
	script_notify_on_next_frame(next);
}
