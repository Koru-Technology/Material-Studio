
#include "../global.h"

char *_box_projects_load_path;
i32   _box_projects_load_frames;

// Full path of a recent project, iOS stores only the file name inside the documents folder
char *box_projects_recent_path(i32 i) {
	return box_projects_path_of(g_config->recent_projects->buffer[i]);
}

// The file of an entry of the recent list: on iOS the list has file names, in the documents folder
char *box_projects_path_of(char *path) {
#ifdef IRON_IOS
	char *document_directory = iron_save_dialog("", "");
	document_directory       = string_copy(substring(document_directory, 0, string_length(document_directory) - 8)); // Strip /"untitled"
	path                     = string("%s%s", document_directory, path);
#endif
	return path;
}

static bool _box_projects_load_cancelled = false;

void box_projects_load_on_next_frame(void *_) {
	if (_box_projects_load_cancelled) {
		_box_projects_load_cancelled = false;
		base_startup_cover_done();
		return;
	}
	if (--_box_projects_load_frames > 0) {
		sys_notify_on_next_frame(&box_projects_load_on_next_frame, NULL);
		return;
	}
	import_arm_run_project(_box_projects_load_path);
	base_startup_cover_done();
}

// A project load that was asked for and has not started yet is dropped (a shared file opens instead)
void box_projects_cancel_load() {
	if (_box_projects_load_frames > 0) {
		_box_projects_load_cancelled = true;
	}
}

void box_projects_load(char *path) {
	_box_projects_load_cancelled = false;
#ifdef IRON_IOS
	box_projects_native_shown   = false;
	box_projects_import_pending = false;
#endif
	if (ui_box_show) {
		ui_box_hide();
	}
	_box_projects_load_path   = string_copy(path);
	_box_projects_load_frames = GPU_FRAMES_IN_FLIGHT + 1;
	sys_notify_on_next_frame(&box_projects_load_on_next_frame, NULL);
}

// The files of a project: the project, its icon, its data folder and its history journal
void box_projects_delete_files(char *path) {
	char *base = substring(path, 0, string_length(path) - 4);
	hstore_journal_delete_for_file(path); // Named by the uuid in the file
	iron_delete_file(path);
	iron_delete_file(string("%s_icon.png", base));
	iron_delete_file(base);
}

// Removes a recent project with its files
void box_projects_delete(i32 i) {
	if (i < 0 || i >= g_config->recent_projects->length) {
		return;
	}
	box_projects_delete_files(box_projects_recent_path(i));
	array_splice(g_config->recent_projects, i, 1);
	config_save();
}

void box_projects_new() {
	project_new(true);
#ifdef IRON_IOS
	box_projects_native_shown   = false;
	box_projects_import_pending = false;
#endif
	if (ui_box_show) {
		ui_box_hide();
	}
	// Pick unique name
	i32   i     = 0;
	i32   j     = 0;
	char *title = string("%s%s", tr("untitled"), i32_to_string(i));
	while (j < g_config->recent_projects->length) {
		char *base = g_config->recent_projects->buffer[j];
		base       = string_copy(substring(base, string_last_index_of(base, PATH_SEP) + 1, string_last_index_of(base, ".")));
		j++;
		if (string_equals(title, base)) {
			i++;
			title = string("%s%s", tr("untitled"), i32_to_string(i));
			j     = 0;
		}
	}
	sys_title_set(title);
}

static void box_projects_import_picked(char *path) { // Desktop: the dialog returns the path
	box_projects_import_pending = false;
	base_on_open_file(path);
}

// Picks a model, it opens in a new project as a model shared with the app (base_on_open_file). The iPad's picker
// hands the file over as a drop: base_handle_drop_paths sends it there while the pick is pending
void box_projects_import() {
	box_projects_import_pending = true;
	ui_files_show(string_array_join(path_mesh_formats(), ","), false, false, &box_projects_import_picked);
}

#ifndef IRON_IOS
static void box_projects_show_box() {
	box_projects_recent_tab();
}
#endif

// The iPad shows its native projects screen (paint/native/Panels/ProjectsView.swift), the Mac a box listing them
void box_projects_show() {
#ifdef IRON_IOS
	box_projects_native_shown = true;
#else
	ui_box_show_custom(&box_projects_show_box, 600, 400, NULL, true, "");
#endif
}

void box_projects_recent_load(char *path) {
	if (!iron_file_exists(path)) {
		return;
	}
	box_projects_load(path);
}

void box_projects_recent_tab() {
	if (ui_tab(&box_projects_tab_index, tr("Recent"), true, -1, false)) {


		g_ui->enabled       = g_config->recent_projects->length > 0;
		bool was_typing     = g_ui->is_typing;
		box_projects_search = string_copy(ui_text_input(&box_projects_search, tr("Search"), UI_ALIGN_LEFT, true, true));
		g_ui->enabled       = true;
		char *first_path    = NULL; // Most recent project that passes the search filter
		for (i32 i = 0; i < g_config->recent_projects->length; ++i) {
			char *path = g_config->recent_projects->buffer[i];
			path = string_copy(string_replace_all(path, "\\", "/"));
			char *file = substring(path, string_last_index_of(path, PATH_SEP) + 1, string_length(path));

			if (string_index_of(to_lower_case(file), to_lower_case(box_projects_search)) < 0) {
				continue; // Search filter
			}
			if (first_path == NULL) {
				first_path = path;
			}
			if (ui_button(file, UI_ALIGN_LEFT, "")) {
				box_projects_recent_load(path);
			}
			if (g_ui->is_hovered) {
				ui_tooltip(path);
			}
		}

		if (first_path != NULL && !was_typing && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_RETURN) {
			box_projects_recent_load(first_path);
		}

		g_ui->enabled = g_config->recent_projects->length > 0;
		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_LEFT)) {
			g_config->recent_projects = any_array_create_from_raw((void *[]){}, 0);
			config_save();
		}
		g_ui->enabled = true;

		ui_end_element();
		if (ui_icon_button(tr("New Project..."), ICON_FILE_NEW, UI_ALIGN_LEFT)) {
			project_new_box();
		}
		if (ui_icon_button(tr("Open..."), ICON_FOLDER_OPEN, UI_ALIGN_LEFT)) {
			project_open();
			ui_box_hide();
		}
	}
}
