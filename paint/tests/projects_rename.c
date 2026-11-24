#include "global.h"

// Renaming a project from the iPad's projects screen (app_project_rename): its folder in Documents/Projects and the
// files named after it move, its recent list entry follows in place. Renamed while open, the project's file and the
// texture imported into its Sources folder follow, and it saves there. Renamed while closed, it opens again with the
// texture found in the new folder (import_arm_abs), not pink. iOS only: elsewhere the rename is refused.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

char *docs     = NULL;
char *old_path = NULL;
char *new_path = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[projects_rename] %s %s", ok ? "ok  " : "FAIL", name);
}

char *project_path(char *name) {
	return string("%s/Projects/%s/%s.arm", docs, name, name);
}

int tex_index() {
	return script_asset_index("koru_rename_tex");
}

void next() {
	frame = frame + 1;
	if (frame < 8) { // Saves and loads take a few frames
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		docs = script_documents_dir();
		script_project_new();
	}
	else if (step == 1) {
		// Picked files wait in .imports: the import moves the texture into the project's Sources folder
		char *picked = string("%s/.imports/koru_rename_tex.png", docs);
		iron_write_png(picked, buffer_create(16 * 16 * 4), 16, 16, 0);
		char *taken = project_take_import(picked);
		check(string_index_of(taken, "/Sources/koru_rename_tex.png") >= 0, string("the texture is in the project's folder (%s)", taken));
		script_import_asset(taken, 0);
	}
	else if (step == 2) {
		check(tex_index() >= 0, "imported");
		project_save(0);
	}
	else if (step == 3) {
		old_path = project_filepath_get();
		check(string_equals(app_project_path(0), old_path), string("saved and listed first (%s)", old_path));
		char *entry = app_project_rename(app_project_entry(0), "koru rename one");
		new_path    = project_path("koru rename one");
		check(string_equals(entry, "Projects/koru rename one/koru rename one.arm"), string("the entry follows (%s)", entry));
		check(iron_file_exists(new_path) && !iron_file_exists(old_path), "the file moved with its folder");
		check(iron_file_exists(project_icon_path(new_path)), "its preview too");
		check(string_equals(project_filepath_get(), new_path), "the open project follows");
		check(script_asset_index("/Projects/koru rename one/Sources/koru_rename_tex") >= 0 &&
		      iron_file_exists(string("%s/Projects/koru rename one/Sources/koru_rename_tex.png", docs)), "its texture's file follows");
		check(string_equals(app_project_rename(app_project_entry(0), "a/b"), ""), "a name with a slash is refused");
		project_save(0);
	}
	else if (step == 4) {
		check(iron_file_exists(new_path), "saved under the new name");
		script_project_new();
	}
	else if (step == 5) {
		char *entry = app_project_rename(app_project_entry(0), "koru rename two");
		old_path    = new_path;
		new_path    = project_path("koru rename two");
		check(string_equals(entry, "Projects/koru rename two/koru rename two.arm"), "renamed while closed");
		check(iron_file_exists(new_path) && !iron_file_exists(old_path), "moved");
		script_project_open(new_path);
	}
	else if (step == 6) {
		check(string_equals(project_filepath_get(), new_path), "opened");
		int i = script_asset_index("/Projects/koru rename two/Sources/koru_rename_tex");
		check(i >= 0, "the texture is found in the new folder");
		gpu_texture_t *t = i >= 0 ? project_get_image(script_get_asset(i)) : NULL;
		check(t != NULL && t->width == 16, "and loaded, not pink");
		app_project_delete(app_project_entry(0));
	}
	else if (step == 7) {
		check(!iron_file_exists(new_path), "deleted");
		printf("[projects_rename] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "PROJECTS_RENAME PASS" : "PROJECTS_RENAME FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
