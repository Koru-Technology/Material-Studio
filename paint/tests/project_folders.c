#include "global.h"

// The iPad's Documents folder (project.c): a project is saved in Projects/<name>/ with its preview hidden next to it,
// listed relative to Documents. Files picked or shared with the app wait in .imports until a project takes them into
// its Sources folder, exports go to Exports, a copy gets a folder of its own. A model shared with the app names the new
// project; an unsaved project's folder goes when it is left, a deleted project's folder goes with it.

int   fails    = 0;
int   checks   = 0;
int   step     = 0;
int   wait     = 0;
char *docs     = NULL;
char *title    = NULL;
char *folder   = NULL;
char *file     = NULL;
char *exported = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[folders] %s %s", ok ? "ok  " : "FAIL", name);
}

void finish() {
	printf("[folders] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "FOLDERS PASS" : "FOLDERS FAIL");
	script_quit();
}

void export_done(char *path) {
	exported = string_copy(path);
}

void write_png(char *path) {
	buffer_t *b = buffer_create(4 * 4 * 4);
	for (int i = 0; i < 64; ++i) {
		buffer_set_u8(b, i, 200);
	}
	iron_write_png(path, b, 4, 4, 0);
}

void write_obj(char *path) {
	char     *obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n";
	buffer_t *b   = sys_string_to_buffer(obj);
	iron_file_save_bytes(path, b, b->length);
}

void next() {
	if (step == 0) {
		script_project_new();
	}
	else if (step == 1) {
		title  = string_copy(sys_title());
		folder = string("%sProjects/%s", docs, title);
		file   = string("%s/%s.arm", folder, title);
		check(!iron_file_exists(folder), "a new project has no folder until it needs one");
		project_save(0);
	}
	else if (step == 2) {
		if (!iron_file_exists(file) && wait < 300) {
			wait = wait + 1;
			script_notify_on_next_frame(next);
			return;
		}
		wait = 0;
		check(string_equals(project_filepath_get(), file), string("saved in Projects/<name>/<name>.arm (%s)", project_filepath_get()));
		char *icon = string("%s/.%s_icon.png", folder, title);
		check(string_equals(project_icon_path(file), icon), "the preview is hidden next to it");
		check(iron_file_exists(icon), "the preview is written");
		config_t *c = script_get_config();
		check(string_equals(c->recent_projects->buffer[0], string("Projects/%s/%s.arm", title, title)), "listed relative to Documents");
		check(string_equals(app_project_path(0), file), "the projects screen finds the file");
		check(string_equals(app_project_icon_path(0), icon), "and its preview");
	}
	else if (step == 3) {
		iron_create_directory(string("%s.imports", docs));
		char *src = string("%s.imports/folders_tex.png", docs);
		write_png(src);
		char *taken = project_take_import(src);
		check(string_equals(taken, string("%s/Sources/folders_tex.png", folder)), string("an import moves into Sources (%s)", taken));
		check(iron_file_exists(taken) && !iron_file_exists(src), "the file is moved, not copied");
		char *outside = string("%sfolders_elsewhere.png", docs);
		check(string_equals(project_take_import(outside), outside), "a file outside .imports stays where it is");
	}
	else if (step == 4) {
		ui_files_show2("png", 1, 0, export_done);
		check(exported != NULL && string_equals(exported, string("%s/Exports", folder)), "exports go to the project's Exports folder");
		check(iron_is_directory(string("%s/Exports", folder)), "the Exports folder is made");
	}
	else if (step == 5) {
		check(app_history_save_copy(), "a copy is saved");
		char *copy_dir = string("%sProjects/%s copy", docs, title);
		check(iron_file_exists(string("%s/%s copy.arm", copy_dir, title)), "the copy gets a folder of its own");
		iron_delete_file(copy_dir);
	}
	else if (step == 6) {
		script_project_new();
	}
	else if (step == 7) {
		check(string_equals(app_project_path(0), file), "the saved project is still listed");
		app_project_delete(0);
		check(!iron_file_exists(folder), "deleting a project removes its folder");
		char *model = string("%s.imports/folders_model.obj", docs);
		write_obj(model);
		base_on_open_file(model);
	}
	else if (step == 8) {
		if (!string_equals(sys_title(), "folders_model") && wait < 120) {
			wait = wait + 1;
			script_notify_on_next_frame(next);
			return;
		}
		wait          = 0;
		char *mfolder = string("%sProjects/folders_model", docs);
		check(string_equals(sys_title(), "folders_model"), string("a shared model names the new project (%s)", sys_title()));
		check(iron_file_exists(string("%s/Sources/folders_model.obj", mfolder)), "the model is kept in its Sources folder");
		check(!iron_file_exists(string("%s.imports/folders_model.obj", docs)), "and taken out of .imports");
		check(string_equals(project_filepath_get(), ""), "the new project is not saved yet");
		script_project_new();
		check(!iron_file_exists(mfolder), "an unsaved project's folder goes when it is left");
	}
	else {
		finish();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	if (script_get_platform() != PLATFORM_IOS) {
		printf("[folders] the folders are the iPad's, nothing to check here");
		printf("FOLDERS PASS");
		script_quit();
		return;
	}
	char *dir = script_dir(); // .../Documents/.tests/
	docs      = substring(dir, 0, string_length(dir) - 7);
	script_notify_on_next_frame(next);
}
