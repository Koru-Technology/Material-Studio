#include "global.h"

// Tiled export below the working resolution (io/export_tiled.c): from a 2048 project at 512 and 256. Paint layers are
// their live pixels box-filtered (no stroke replays): the 512 export is what the project changed to 512
// (layers_resolution_apply, box-filtered down) exports at its own resolution, within rounding (the stroke's edge read
// bilinear from 2048 is up to 106/255 off). Nothing is listed as not upscaled.

char *root = "";

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_lowres] %s %s", ok ? "ok  " : "FAIL", name);
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

void build_scene() {
	context_t       *ctx   = script_get_context();
	slot_material_t *m     = script_material_create("el_noise");
	ui_node_t       *noise = script_material_create_node_at("TEX_NOISE", -600.0, 0.0);
	script_material_set_float(noise, 1, 1, 24.0);
	script_material_set_float(noise, 1, 2, 6.0);
	script_material_connect(noise, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();

	slot_layer_t *fill = script_layer_new_fill();
	script_layer_set_name(fill, "el_fill");
	slot_layer_t *mask = script_layer_new_mask(fill, 1);
	int           g    = script_mask_add_effect(mask, 6, -1); // Position gradient
	script_mask_set_effect(mask, g, 1, 1.0, 2);

	slot_material_t *blue = script_material_create("el_blue");
	ui_node_t       *rgb  = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	ctx->brush_radius  = 0.05; // A thin stroke: its edge aliases when 2048 is read bilinear at 512
	ctx->brush_spacing = 0.05;
	context_select_tool(TOOL_TYPE_BRUSH);
	app_layer_new(0);
	script_layer_set_name(script_get_context()->layer, "el_paint");
	make_paint_set_batching(1);
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
		check(export_tiled_not_upscaled(512, 512) == 0, "nothing listed as not upscaled at 512");
		export_texture_test_options("generic", 0, 0, 8, 0);
		export_tiled_set_resolution(512, 512);
		export_texture_run(fresh_dir("lo"), 0);
		check(export_tiled_stat(4) == 5 && export_tiled_stat(9) == 0, string("512: %d files, %d tiles from timelines", export_tiled_stat(4), export_tiled_stat(9)));
		export_tiled_set_resolution(256, 256);
		export_texture_run(fresh_dir("lo256"), 0);
		check(export_tiled_stat(4) == 5 && export_tiled_stat(9) == 0, string("256: %d files, %d tiles from timelines", export_tiled_stat(4), export_tiled_stat(9)));
		export_tiled_set_resolution(0, 0);
		layers_resolution_apply(512, 512);
	}
	else if (step == 3) {
		if (layers_resolution_running()) {
			script_notify_on_next_frame(next);
			return;
		}
		export_texture_run(fresh_dir("ref"), 0);
		int   d  = script_diff_images(file("lo", "base"), file("ref", "base"), 1.0);
		float mx = script_diff_max();
		check(d >= 0 && d <= 512 * 512 * 4 / 100 && mx <= 8.0, string("base color at 512 as the project at 512 exports it (%d values over 1/255, max %.1f)", d, mx)); // Read bilinear: max 106
		d = script_diff_images(file("lo", "rough"), file("ref", "rough"), 1.0);
		check(d >= 0 && d <= 512 * 512 * 4 / 1000, string("roughness at 512 as the project at 512 exports it (%d values over 1/255)", d));
		check(script_diff_images(file("lo256", "base"), file("lo256", "base"), 1.0) == 0 && script_diff_images(file("lo256", "base"), file("ref", "base"), 255.0) < 0,
		      "the 256 export is there, not at 512");
	}
	else {
		printf("[export_lowres] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_LOWRES PASS" : "EXPORT_LOWRES FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = script_tmp_dir("materialstudio_export_lowres");
	script_brush_defaults();
	script_project_new();
	iron_create_directory(root);
	script_notify_on_next_frame(next);
}
