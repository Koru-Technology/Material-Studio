#include "global.h"

// The Decal tool's image and channels (app_decal_*). The tool paints with a material of its own, not in the project:
// picking an image sets its base color and, by the alpha, its opacity; each other channel takes an image or a value. It
// paints only while the tool is selected (the project's material is back for other tools) and stays while another
// material is picked. With a decal layer selected the same calls set that layer's fill, one undo step each.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

int image     = -1;
int materials = 0;
int user_id   = -1;
int layer_id  = -1;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[decal_material] %s %s", ok ? "ok  " : "FAIL", name);
}

int painting_id() {
	return script_get_context()->material->id;
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		char *path = string("%s/koru_decal_test.png", script_save_path());
		iron_write_png(path, buffer_create(16 * 16 * 4), 16, 16, 0);
		script_import_asset(path, 0);
	}
	else if (step == 1) {
		image     = script_asset_index("koru_decal_test");
		materials = app_material_count();
		user_id   = painting_id();
		check(image >= 0, "an image to stamp");
		app_set_tool(3); // Decal
		check(painting_id() == -1, "the Decal tool paints with its own material");
		check(app_decal_image() == -1 && !app_decal_editing_layer(), "no image yet");
		check(app_decal_use_image(image), "picked");
		check(app_material_count() == materials, "no material added to the project");
		check(app_decal_image() == image && app_decal_channel_image(1) == image, "base color and opacity from the image");
		check(app_decal_channel_image(3) == -1, "roughness is a value");
		check(app_decal_set_channel_value(3, 0.25) && app_decal_channel_value(3) == 0.25, "roughness value");
		check(!app_decal_set_channel_value(5, 0.5), "the normal takes no value");
		check(!app_decal_set_channel_image(0, -1), "the base color keeps an image");
		check(app_decal_set_channel_image(5, image) && app_decal_channel_image(5) == image, "normal from an image");
		app_material_select(app_material_id_at(0)); // A project material picked while the tool is selected
	}
	else if (step == 2) {
		check(painting_id() == -1, "the tool still paints with its own material");
		check(app_decal_image() == image && app_decal_channel_value(3) == 0.25, "which keeps its image and values");
		app_set_tool(0); // Brush
		check(painting_id() >= 0, "the brush paints with the project's material");
		app_set_tool(3);
		check(painting_id() == -1 && app_decal_image() == image, "back to the decal");
		check(app_layer_new(2), "a decal layer"); // APP_LAYER_NEW_DECAL
	}
	else if (step == 3) {
		check(app_decal_editing_layer(), "selected: the bar sets it");
		check(app_material_count() == materials, "still no material added");
		int before = app_decal_image();
		check(app_decal_use_image(image) && app_decal_image() == image && app_decal_channel_image(1) == image, "the layer takes the image");
		app_undo();
		check(app_decal_image() == before, "one undo step");
		app_redo();
		check(app_decal_set_channel_value(3, 0.75) && app_decal_channel_value(3) == 0.75, "the layer's roughness");
	}
	else {
		printf("[decal_material] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "DECAL_MATERIAL PASS" : "DECAL_MATERIAL FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
