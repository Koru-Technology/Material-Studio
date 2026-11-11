
#include "../global.h"

void import_asset_run(char *path, f32 drop_x, f32 drop_y, bool show_box, bool hdr_as_envmap, void (*done)(void)) {
	if (path_is_mesh(path)) {
		show_box ? project_import_mesh_box(path, true, true, false, NULL) : import_mesh_run(path, true, true, false);
		if (drop_x > 0) {
			ui_box_click_to_hide = false; // Prevent closing when going back to window after drag and drop
		}
	}
	else if (path_is_texture(path)) {
		import_texture_run(path, hdr_as_envmap);
		// Place image node
		i32 x0 = ui_nodes_wx;
		i32 x1 = ui_nodes_wx + ui_nodes_ww;
		if (ui_nodes_show && drop_x > x0 && drop_x < x1) {
			i32 asset_index = 0;
			for (i32 i = 0; i < g_project->_->assets->length; ++i) {
				if (string_equals(g_project->_->assets->buffer[i]->file, path)) {
					asset_index = i;
					break;
				}
			}
			ui_nodes_accept_asset_drop(asset_index);
			ui_nodes_get_nodes()->nodes_drag = false;
			ui_nodes_hwnd->redraws           = 2;
		}

		if (g_context->tool == TOOL_TYPE_COLORID && g_project->_->assets->length == 1) {
			ui_header_handle->redraws = 2;
			g_context->ddirty         = 2;
		}
	}
	else if (path_is_text(path)) {
		import_text_run(path);
	}
	else if (path_is_sound(path)) {
		import_sound_run(path);
	}
	else if (path_is_lut(path)) {
		box_preferences_lut_picked(path);
	}
	else if (path_is_project(path)) {
		import_arm_run_project(path);
	}
	else if (path_is_script(path)) { // Opened in the Scripts tab
		buffer_t *blob = data_get_blob(path);
		if (blob != NULL) {
			tab_scripts_set(sys_buffer_to_string(blob));
			data_delete_blob(path);
			g_config->show_scripts = true;
			ui_base_show_tab(TAB_SCRIPTS);
			console_info(tr("Script opened"));
		}
	}
	else if (path_is_font(path)) {
		import_font_run(path);
	}
	else if (path_is_folder(path)) {
		import_folder_run(path);
	}
	else {
		console_error(strings_unknown_asset_format());
	}

	if (done != NULL) {
		done();
	}
}
