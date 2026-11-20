#include "global.h"

// Delete Unused Assets (APP_COMMAND_DELETE_UNUSED): images a fill layer channel shows stay, the others go.

int step   = 0;
int frame  = 0;
int checks = 0;
int fails  = 0;
int f      = -1;
int count0 = 0;

void check(bool ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[delete_unused] %s %s", ok ? "ok  " : "FAIL", name);
}

char *test_path(char *name) {
	return string("%s/%s", script_save_path(), name);
}

int texture_named(char *name) {
	for (int i = 0; i < app_texture_count(); ++i) {
		if (string_equals(app_texture_name(i), name)) {
			return i;
		}
	}
	return -1;
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		count0 = app_texture_count();
		iron_write_png(test_path("koru_delete_unused_used.png"), buffer_create(16 * 16 * 4), 16, 16, 0);
		iron_write_png(test_path("koru_delete_unused_unused.png"), buffer_create(16 * 16 * 4), 16, 16, 0);
		script_import_asset(test_path("koru_delete_unused_used.png"), 0);
		script_import_asset(test_path("koru_delete_unused_unused.png"), 0);
		check(app_layer_new(1), "new fill layer");
	}
	else if (step == 1) {
		f = script_get_context()->layer->id;
		check(app_texture_count() == count0 + 2, string("two images imported (%d)", app_texture_count() - count0));
		int used = texture_named("koru_delete_unused_used.png");
		check(used >= 0 && app_layer_set_fill_image(f, 0, used), "the fill's base color from the first one");
		app_drag_end();
	}
	else if (step == 2) {
		app_command(21); // APP_COMMAND_DELETE_UNUSED
	}
	else if (step == 3) {
		check(texture_named("koru_delete_unused_used.png") >= 0, "the image the fill shows stays");
		check(texture_named("koru_delete_unused_unused.png") < 0, "the unused image went");
		check(app_layer_fill_kind(f, 0) == 1, "the fill's base color is still that image");
	}
	else {
		printf("[delete_unused] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "DELETE_UNUSED PASS" : "DELETE_UNUSED FAIL");
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
