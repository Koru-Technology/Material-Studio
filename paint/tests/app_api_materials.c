#include "global.h"

// The native UI's materials interface (app_api_materials.h): project materials (new, duplicate, delete with undo,
// delete unused, thumbnails and their versions), the Library (Material Maker's items, search, icons loaded as asked, use)
// and smart materials (save, icon, apply, delete).

int step    = 0;
int frame   = 0;
int wait    = 0;
int checks  = 0;
int fails   = 0;
int count0  = 0;
int created = -1;
int copy    = -1;
int version = 0;
int item    = -1;
int layers0 = 0;
int smart0  = 0;
int tries   = 0;

buffer_t *pixels = NULL;
buffer_t *size   = NULL;

// Texel x, y of the image last read into pixels: channel c
int texel(int x, int y, int c) {
	return buffer_get_u8(pixels, (y * buffer_get_i32(size, 0) + x) * 4 + c);
}

void print_texel(char *what, int x, int y) {
	printf("[app_api_materials] %s texel %d %d: %d %d %d %d", what, x, y, texel(x, y, 0), texel(x, y, 1), texel(x, y, 2), texel(x, y, 3));
}

void check(bool ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[app_api_materials] %s %s", ok ? "ok  " : "FAIL", name);
}

int selected_id() {
	slot_material_t *m = script_material_get();
	return m != NULL ? m->id : -1;
}

void next() {
	frame = frame + 1;
	if (frame < 3 || wait > 0) { // Let each step's work reach the screen before the next
		if (wait > 0) {
			wait = wait - 1;
		}
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		count0  = app_material_count();
		created = app_material_new();
		check(created >= 0, "new material");
		check(app_material_count() == count0 + 1, "material count grew by one");
		check(selected_id() == created, "the new material is selected");
		check(app_material_id_at(count0) == created, "listed last");
	}
	else if (step == 1) {
		check(app_material_thumbnail_size(created, 0) == 256 * 256 * 4, "full size thumbnail");
		check(app_material_thumbnail_size(created, 100) == 85 * 85 * 4, "thumbnail box filtered to fit 100");
		version = app_material_preview_version(created);
		check(version > 0, "has a preview version");
		ui_node_t *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
		script_material_set_color(rgb, 0, 0, 0.1, 0.8, 0.2, 1.0);
		script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
		script_material_update();
	}
	else if (step == 2) {
		int bytes = app_material_thumbnail(created, 128, pixels->buffer, pixels->length, size->buffer, size->buffer + 4);
		check(bytes == 128 * 128 * 4 && buffer_get_i32(size, 0) == 128 && buffer_get_i32(size, 4) == 128, "thumbnail read back at 128");
		print_texel("green material", 64, 64);
		print_texel("green material", 2, 2);
		check(texel(64, 64, 1) > texel(64, 64, 0) + 40 && texel(64, 64, 1) > texel(64, 64, 2) + 40 && texel(64, 64, 3) > 240,
		      "thumbnail RGBA order: the green sphere is green and opaque in the middle");
		check(texel(2, 2, 3) < 16 && texel(2, 2, 0) <= texel(2, 2, 3), "transparent corner, premultiplied");
		int v = app_material_preview_version(created);
		check(v != version, "an edit changes the preview version");
		version = v;
		wait    = 12;
	}
	else if (step == 3) {
		int v = app_material_preview_version(created);
		check(v != version, "and again a little later, for previews redrawn late");
		wait = 15;
	}
	else if (step == 4) {
		version = app_material_preview_version(created);
		wait    = 5;
	}
	else if (step == 5) {
		check(app_material_preview_version(created) == version, "settles without edits");
		copy = app_material_duplicate(created);
		check(copy >= 0 && copy != created, "duplicated");
		check(app_material_count() == count0 + 2 && selected_id() == copy, "the copy is listed and selected");
		app_undo();
		check(app_material_count() == count0 + 1, "undo removes the copy");
		app_redo();
		check(app_material_count() == count0 + 2, "redo brings it back");
		copy = app_material_id_at(count0 + 1);
	}
	else if (step == 6) {
		check(app_material_select(created) && selected_id() == created, "select by id");
		check(app_material_can_delete(copy), "can delete");
		check(app_material_delete(copy), "deleted");
		check(app_material_count() == count0 + 1, "count back");
		check(!app_material_select(copy), "a deleted id is not found");
		check(app_material_rename(created, "Native Green"), "renamed");
	}
	else if (step == 7) {
		app_material_new();
		app_material_new();
		check(app_material_count() == count0 + 3, "two unused materials");
	}
	else if (step == 8) {
		int gone = app_material_delete_unused();
		check(gone >= 3, string("delete unused removed %d", gone));
		check(app_material_count() >= 1 && app_material_count() <= count0, "unused ones gone, one kept at least");
		int one = app_material_id_at(0);
		if (app_material_count() == 1) {
			check(!app_material_can_delete(one) && !app_material_delete(one), "the last material is not deleted");
		}
	}
	else if (step == 9) {
		app_library_signature();
		int n = app_library_count();
		check(n >= 900, string("library lists %d items", n));
		check(app_library_group_count() > 5, "library types");
		item = app_library_find("Wooden Planks");
		if (item < 0) {
			item = n - 1;
		}
		check(app_library_matches(item, ""), "empty search matches");
		check(!app_library_matches(item, "zzqqxx-nothing"), "unknown search does not match");
		int found = 0;
		for (int i = 0; i < n; ++i) {
			if (app_library_matches(i, "WOOD")) {
				found = found + 1;
			}
		}
		check(found > 5 && found < n, string("search finds %d wood items, any case", found));
		app_library_icon_size(item); // Asks for it
	}
	else if (step == 10) {
		if (app_library_icon_size(item) <= 0 && tries < 300) { // Loaded a few per frame
			tries = tries + 1;
			frame = 2;
			script_notify_on_next_frame(next);
			return;
		}
		printf("[app_api_materials] icon after %d frames", tries);
		check(app_library_icon_size(item) == 128 * 128 * 4, "library icon loads");
		int bytes = app_library_icon(item, pixels->buffer, pixels->length, size->buffer, size->buffer + 4);
		check(bytes == 128 * 128 * 4, "library icon read back");
		print_texel("library icon", 64, 64);
		print_texel("library icon", 1, 1);
		check(texel(64, 64, 3) > 240 && texel(1, 1, 3) < 16, "library icon: opaque sphere, transparent corner");
		count0 = app_material_count();
		check(app_library_use(item), "library item used");
		wait = 30;
	}
	else if (step == 11) {
		check(app_material_count() == count0 + 1, "the library material is in the project");
		version = app_material_preview_version(selected_id());
		wait    = 20;
	}
	else if (step == 12) {
		check(app_material_preview_version(selected_id()) == version, "the library material's preview version stays without edits");
		check(app_material_to_fill_layer(selected_id()), "to fill layer");
		layers0 = app_layer_count();
	}
	else if (step == 13) {
		check(script_get_context()->layer->fill != NULL, "a fill layer is selected");
		int old = app_smart_find("app_api_test");
		if (old >= 0) {
			app_smart_delete(old);
		}
		smart0 = app_smart_count();
		check(app_smart_save("app_api_test"), "smart material saving");
		wait = 5;
	}
	else if (step == 14) {
		int i = app_smart_find("app_api_test");
		check(app_smart_count() == smart0 + 1 && i >= 0, "smart material listed");
		check(app_smart_icon_size(i) == 128 * 128 * 4, "smart material icon");
		layers0 = app_layer_count();
		check(app_smart_apply(i), "smart material applying");
		wait = 5;
	}
	else if (step == 15) {
		check(app_layer_count() > layers0, "smart material added layers");
		app_undo();
		check(app_layer_count() == layers0, "undo removes them");
		int i = app_smart_find("app_api_test");
		check(app_smart_delete(i), "smart material deleted");
		check(app_smart_count() == smart0 && app_smart_find("app_api_test") < 0, "smart material gone");
	}
	else {
		printf("[app_api_materials] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "APP_API_MATERIALS PASS" : "APP_API_MATERIALS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	pixels = buffer_create(256 * 256 * 4);
	size   = buffer_create(8);
	script_notify_on_next_frame(next);
}
