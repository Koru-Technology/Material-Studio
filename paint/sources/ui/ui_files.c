
#include "../global.h"

void ui_files_release_keys() {
	// File dialog may prevent firing key up events
	keyboard_up_listener(KEY_CODE_SHIFT);
	keyboard_up_listener(KEY_CODE_CONTROL);
#ifdef IRON_MACOS
	keyboard_up_listener(KEY_CODE_META);
#endif
}

void ui_files_show(char *filters, bool is_save, bool open_multiple, void (*files_done)(char *)) {
	if (is_save) {
		char *default_path = "";
		ui_files_path = string_copy(iron_save_dialog(filters, default_path));
		if (ui_files_path != NULL) {
			char *sep2 = string("%s%s", PATH_SEP, PATH_SEP);
			while (string_index_of(ui_files_path, sep2) >= 0) {
				ui_files_path = string_copy(string_replace_all(ui_files_path, sep2, PATH_SEP));
			}
			ui_files_path     = string_copy(string_replace_all(ui_files_path, "\r", ""));
			ui_files_filename = string_copy(substring(ui_files_path, string_last_index_of(ui_files_path, PATH_SEP) + 1, string_length(ui_files_path)));
			ui_files_path     = string_copy(substring(ui_files_path, 0, string_last_index_of(ui_files_path, PATH_SEP)));
			files_done(ui_files_path);
		}
	}
	else {
		string_array_t *paths = iron_open_dialog(filters, "", open_multiple);
		if (paths != NULL) {
			for (i32 i = 0; i < paths->length; ++i) {
				char *path = paths->buffer[i];
				char *sep2 = string("%s%s", PATH_SEP, PATH_SEP);

				while (string_index_of(path, sep2) >= 0) {
					path = string_copy(string_replace_all(path, sep2, PATH_SEP));
				}
				path              = string_copy(string_replace_all(path, "\r", ""));
				ui_files_filename = string_copy(substring(path, string_last_index_of(path, PATH_SEP) + 1, string_length(path)));
				files_done(path);
			}
		}
	}

	ui_files_release_keys();
}

// Saves a preset (theme, keymap, export preset) typed in by name into data/<folder>, the file name it got, NULL for an
// empty name or a failed save. Path separators in the name become '_', so the file stays in the folder.
char *ui_files_save_preset(char *folder, char *name, char *json) {
	char *file = string_copy(name);
	for (char *c = file; *c != '\0'; ++c) {
		if (*c == '/' || *c == '\\' || *c == ':') {
			*c = '_';
		}
	}
	while (file[0] == '.') {
		file++; // No hidden files, no ".."
	}
	if (file[0] == '\0') {
		return NULL;
	}
	if (!ends_with(file, ".json")) {
		file = string("%s.json", file);
	}
	char *path = string("%s%s%s%s%s", path_data(), PATH_SEP, folder, PATH_SEP, file);
	if (!iron_file_save_bytes(path, sys_string_to_buffer(json), 0)) {
		console_error(string("Could not save %s.", path));
		return NULL;
	}
	return file;
}

