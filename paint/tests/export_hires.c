#include "global.h"

// Tiled export above the working resolution (io/export_tiled.c): from a 2048 project at 4096. Paint layers materialize
// their timelines at 4096 (strokes replay there): the export is close to the same stroke painted live at 4096. Fill
// layers and mask generators render at 4096: as the project changed to 4096 (layers_resolution_apply) exports them.
// Nothing is listed as not upscaled. Tiles larger than the working resolution (3072 px) give what 1024 px tiles give.

char *root = "";

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_layer_t *layer_fill;
slot_layer_t *layer_paint;
slot_layer_t *layer_live;

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

char *file(char *dir, char *tex) {
	return string("%s/%s/untitled_%s.png", root, dir, tex);
}

void stroke() {
	render_path_paint_set_stamp_seed(4242);
	for (int i = 0; i < 30; ++i) {
		script_paint(0.35 + i * 0.01, 0.45 + (i % 7) * 0.004);
	}
	script_paint_end();
}

slot_layer_t *paint_layer(char *name) {
	app_layer_new(0);
	slot_layer_t *l = script_get_context()->layer;
	script_layer_set_name(l, name);
	make_paint_set_batching(1); // Parses the paint material again
	return l;
}

void build_scene() {
	context_t       *ctx   = script_get_context();
	slot_material_t *m     = script_material_create("eh_noise");
	ui_node_t       *noise = script_material_create_node_at("TEX_NOISE", -600.0, 0.0);
	script_material_set_float(noise, 1, 1, 24.0); // Fine detail: lost when 2048 is scaled up
	script_material_set_float(noise, 1, 2, 6.0);
	script_material_connect(noise, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();

	layer_fill = script_layer_new_fill();
	script_layer_set_name(layer_fill, "eh_fill");
	slot_layer_t *mask = script_layer_new_mask(layer_fill, 1);
	int           g    = script_mask_add_effect(mask, 6, -1); // Position gradient
	script_mask_set_effect(mask, g, 1, 1.0, 2);

	slot_material_t *blue = script_material_create("eh_blue");
	ui_node_t       *rgb  = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	ctx->brush_radius  = 0.3;
	ctx->brush_spacing = 0.05;
	context_select_tool(TOOL_TYPE_BRUSH);
	layer_paint = paint_layer("eh_paint");
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
		stroke();
		paint_rec_flush();
	}
	else if (step == 2) {
		int n = export_tiled_not_upscaled(4096, 4096);
		for (int i = 0; i < n; ++i) {
			printf("[export_hires]   not upscaled: %s", export_tiled_not_upscaled_name(i));
		}
		check(n == 0, string("%d layers not upscaled at 4096", n));

		export_texture_test_options("generic", 0, 0, 8, 0);
		export_tiled_test_tile(1024);
		export_tiled_set_resolution(4096, 4096);
		export_texture_run(fresh_dir("t"), 0);
		check(export_tiled_stat(4) == 5 && export_tiled_stat(11) > 0, string("4096: %d files, %d fill tiles rendered", export_tiled_stat(4), export_tiled_stat(11)));
		check(export_tiled_stat(9) > 0 && export_tiled_stat(10) == 0, string("paint layer tiles materialized at 4096 (%d, %d fell back)", export_tiled_stat(9), export_tiled_stat(10)));
		export_tiled_test_tile(3072); // Tiles larger than the working resolution (4096 tiles exceed the budget)
		export_texture_run(fresh_dir("w"), 0);
		check(export_tiled_stat(2) == 4 && export_tiled_stat(11) > 0, string("3072 px tiles: %d tiles, %d fill tiles", export_tiled_stat(2), export_tiled_stat(11)));
		export_tiled_test_tile(1024);
		int d = script_diff_images(file("w", "base"), file("t", "base"), 1.0);
		check(d >= 0 && d <= 4096 * 4096 * 4 / 10000, string("base color of 3072 px tiles as 1024 px tiles (%d values over 1/255, max %.1f)", d, script_diff_max()));
		export_tiled_test_no_fill(1);
		export_texture_run(fresh_dir("bl"), 0);
		export_tiled_test_no_fill(0);
		export_tiled_set_resolution(0, 0);
		layers_resolution_apply(4096, 4096); // The project at 4096: fill layers and masks render there natively
	}
	else if (step == 3) { // The stroke painted live at 4096, in a layer of its own in place of the recorded one
		layer_paint->visible = 0;
		export_texture_run(fresh_dir("none"), 0); // Without the stroke: what it paints
		layer_live = paint_layer("eh_live");
		stroke();
		paint_rec_flush();
	}
	else if (step == 4) {
		export_texture_run(fresh_dir("l"), 0);
		int   painted = script_diff_images(file("l", "base"), file("none", "base"), 1.0);
		int   over1   = script_diff_images(file("l", "base"), file("t", "base"), 1.0);
		float mx      = script_diff_max();
		printf("[export_hires] paint replayed at 4096 against live 4096: %d of %d painted base color values over 1/255 (max %.1f)", over1, painted, mx);
		check(painted > 10000, string("the live stroke paints (%d)", painted));
		check(over1 * 100 <= painted, string("the export's stroke at 4096 is within 1/255 of live 4096 on 99 percent of what it paints (%d of %d)", over1, painted));
		float fills  = mean_diff(file("l", "rough"), file("t", "rough"));
		float scaled = mean_diff(file("l", "rough"), file("bl", "rough"));
		check(fills <= scaled, string("roughness (the fill) against the native 4096 export: mean %.3f/255, scaled up from 2048: %.3f", fills, scaled));
		float base_t  = mean_diff(file("l", "base"), file("t", "base"));
		float base_bl = mean_diff(file("l", "base"), file("bl", "base"));
		check(base_t < base_bl * 0.5, string("base color against live 4096: mean %.3f/255, fills scaled up from 2048: %.3f", base_t, base_bl));
	}
	else {
		export_tiled_test_tile(0);
		script_get_config()->paint_resolved_dabs = 0;
		script_get_config()->timeline_shadow     = 0;
		printf("[export_hires] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_HIRES PASS" : "EXPORT_HIRES FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = script_tmp_dir("materialstudio_export_hires"); // Per run: suites of several worktrees run at once
	script_get_config()->paint_resolved_dabs = 1;
	script_get_config()->timeline_shadow     = 1;
	script_brush_defaults();
	script_project_new();
	iron_create_directory(root);
	script_notify_on_next_frame(next);
}
