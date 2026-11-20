#include "global.h"

// The Decal tool's image (app_decal_*): picking an image makes a material of it, its color the base color and its alpha
// the opacity, selected; picking it again selects the same material. Each other channel takes an image or a value, one
// undo step each; the base color always keeps an image, the normal takes no value.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

int image     = -1;
int materials = 0;
int decal_id  = -1;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[decal_material] %s %s", ok ? "ok  " : "FAIL", name);
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
		check(image >= 0, "an image to stamp");
		check(app_decal_image() == -1, "the default material is not a decal");
		check(app_decal_use_image(image), "picked");
	}
	else if (step == 2) {
		check(app_material_count() == materials + 1, "a decal material is made");
		check(app_decal_image() == image, "selected, its base color from the image");
		check(app_decal_channel_image(1) == image, "its opacity too (the alpha)");
		check(app_decal_channel_image(3) == -1, "roughness is a value");
		decal_id = script_get_context()->material->id;
		app_material_select(app_material_id_at(0));
		check(app_decal_image() == -1, "another material selected");
		check(app_decal_use_image(image) && script_get_context()->material->id == decal_id && app_material_count() == materials + 1,
		      "picked again: the same material");
	}
	else if (step == 3) {
		check(app_decal_set_channel_value(3, 0.25), "roughness value");
		check(app_decal_channel_value(3) == 0.25, "set");
		check(!app_decal_set_channel_value(5, 0.5), "the normal takes no value");
		check(!app_decal_set_channel_image(0, -1), "the base color keeps an image");
		check(app_decal_set_channel_image(5, image), "normal from an image");
		check(app_decal_channel_image(5) == image, "linked");
		check(app_decal_image() == image, "still a decal of its image");
	}
	else if (step == 4) {
		app_undo();
		check(app_decal_channel_image(5) == -1, "undo: no normal image");
		app_undo();
		check(app_decal_channel_value(3) != 0.25, "undo: the roughness before");
		check(app_decal_set_channel_image(1, -1) && app_decal_channel_image(1) == -1 && app_decal_channel_image(0) == image,
		      "opacity back to a value, the image stays for the base color");
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
