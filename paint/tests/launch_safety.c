#include "global.h"

// Leaving a project never drops its work (project_leave): a new project or another one opened saves it first, the open
// project picked again stays as it is, opening the open project's file reads what was just saved. Imports never replace
// a different file of the same name in Sources (project_take_import), a project file picked gets a folder and a uuid of
// its own. The projects screen names projects by entry (a stale one does nothing). Projects in Documents/Projects that
// the list lost come back (project_scan_folders). A save that fails tells the user and keeps the project modified.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   wait   = 0;
char *docs   = NULL;
char *title  = NULL;
char *file   = NULL;
char *entry  = NULL;
int   layers = 0;
int   alerts = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[launch_safety] %s %s", ok ? "ok  " : "FAIL", name);
}

void write_png(char *path, int value) {
	buffer_t *b = buffer_create(4 * 4 * 4);
	for (int i = 0; i < 64; ++i) {
		buffer_set_u8(b, i, value);
	}
	iron_write_png(path, b, 4, 4, 0);
}

int listed(char *e) {
	config_t *c = script_get_config();
	return string_array_index_of(c->recent_projects, e) >= 0;
}

void next() {
	if (step == 0) {
		script_project_new();
	}
	else if (step == 1) {
		title = string_copy(sys_title());
		file  = string("%sProjects/%s/%s.arm", docs, title, title);
		entry = string("Projects/%s/%s.arm", title, title);
		project_save(0);
	}
	else if (step == 2) {
		if (!iron_file_exists(file) && wait < 300) {
			wait = wait + 1;
			script_notify_on_next_frame(next);
			return;
		}
		wait = 0;
		check(iron_file_exists(file), "saved");
		script_layer_new();
		layers = script_layer_count();
		check(project_modified_get(), "a new layer is an unsaved change");
		app_project_new(); // From the projects screen
	}
	else if (step == 3) {
		check(string_equals(project_filepath_get(), ""), "a new project is open");
		script_project_open(file);
		check(script_layer_count() == layers, string("the project left was saved first (%d layers of %d)", script_layer_count(), layers));
		check(!project_modified_get(), "and opens unmodified");
	}
	else if (step == 4) {
		script_layer_new();
		layers = script_layer_count();
		app_project_open(entry); // The open project picked again
	}
	else if (step == 5) {
		check(script_layer_count() == layers && project_modified_get(), "picking the open project again keeps it as it is");
		script_project_open(file); // Its file opened again: saved first, then read
	}
	else if (step == 6) {
		check(script_layer_count() == layers, string("opening the open project's file reads what was just saved (%d of %d)", script_layer_count(), layers));
		// Imports of the same name
		char *imports = string("%s.imports", docs);
		iron_create_directory(imports);
		char *src = string("%s/safety_tex.png", imports);
		write_png(src, 100);
		char *a = project_take_import(src);
		check(ends_with(a, "/Sources/safety_tex.png"), string("an import goes into Sources (%s)", a));
		write_png(src, 200);
		char *b = project_take_import(src);
		check(ends_with(b, "/Sources/safety_tex 2.png"), string("a different file of the same name is numbered (%s)", b));
		check(iron_file_exists(a) && iron_file_exists(b), "the first file is kept");
		write_png(src, 100);
		char *c = project_take_import(src);
		check(string_equals(c, a) && !iron_file_exists(src), "the same file picked again is the one there");
		char *holder = string("%s/0F0F0F0F-SAFETY", imports);
		iron_create_directory(holder);
		write_png(string("%s/safety_nested.png", holder), 50);
		char *d = project_take_import(string("%s/safety_nested.png", holder));
		check(ends_with(d, "/Sources/safety_nested.png"), string("a file in its own folder in .imports is taken (%s)", d));
		check(!iron_file_exists(holder), "its folder goes");
	}
	else if (step == 7) {
		// A project file picked: a folder and a uuid of its own
		project_save(0);
	}
	else if (step == 8) {
		char     *src  = string("%s.imports/%s.arm", docs, title);
		buffer_t *blob = data_get_blob(file);
		iron_file_save_bytes(src, blob, blob->length);
		char *taken = project_take_import(src);
		char *copy  = string("%sProjects/%s 2/%s 2.arm", docs, title, title);
		check(string_equals(taken, copy), string("a project file picked gets a folder of its own (%s)", taken));
		char *u0 = project_file_uuid(file);
		char *u1 = project_file_uuid(taken);
		check(u0 != NULL && u1 != NULL && !string_equals(u0, u1), "and a uuid of its own");
		iron_delete_file(string("%sProjects/%s 2", docs, title));
		// A stale entry does nothing
		int n = app_project_count();
		app_project_delete("Projects/safety gone/safety gone.arm");
		check(app_project_count() == n && iron_file_exists(file), "deleting an entry no longer listed does nothing");
		// The list lost the project: it comes back
		config_t *c = script_get_config();
		array_splice(c->recent_projects, string_array_index_of(c->recent_projects, entry), 1);
		string_array_push(c->recent_projects, "Projects/safety gone/safety gone.arm");
		project_scan_folders();
		check(listed(entry), "a project in Documents/Projects the list lost is listed again");
		check(!listed("Projects/safety gone/safety gone.arm"), "an entry whose project is gone leaves");
		// A save that fails
		alerts = script_alert_serial();
		script_layer_new();
		project_filepath_set(string("%snot a folder/x/x.arm", docs));
		project_save(0);
	}
	else if (step == 9) {
		check(script_alert_serial() == alerts + 1, "a failed save tells the user");
		check(project_modified_get(), "and the project stays modified");
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 10) {
		check(!project_modified_get(), "saved again");
		script_project_new();
	}
	else {
		printf("[launch_safety] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "LAUNCH_SAFETY PASS" : "LAUNCH_SAFETY FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	char *dir = script_dir(); // .../Documents/.tests/
	docs      = substring(dir, 0, string_length(dir) - 7);
	script_notify_on_next_frame(next);
}
