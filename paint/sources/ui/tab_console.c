
#include "../global.h"


void tab_console_draw_export_on_file_picked(char *path) {
	char *str = string_array_join(console_last_traces, "\n");
	char *f   = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	path = string("%s%s%s", path, PATH_SEP, f);
	if (!ends_with(path, ".txt")) {
		path = string("%s.txt", path);
	}
	iron_file_save_bytes(path, sys_string_to_buffer(str), 0);
}

void tab_console_draw(i32 *htab) {
	char *title = tr("Console");
	if (console_message_timer > 0) {
		title = string_tmp("%s        ", console_message);
	}
	i32 color = console_message_timer > 0 ? console_message_color : -1;

	if (ui_tab(htab, title, false, color, false) && g_ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {

		ui_begin_sticky();
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS) // Copy
		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        -100,
		        -100,
		        -100,
		    },
		    3);
#else
		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        -100,
		        -100,
		    },
		    2);
#endif
		ui_row(row);

		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_CENTER)) {
			console_last_traces = any_array_create_from_raw((void *[]){}, 0);
		}
		if (ui_icon_button(tr("Export"), ICON_EXPORT, UI_ALIGN_CENTER)) {
			ui_files_show("txt", true, false, &tab_console_draw_export_on_file_picked);
		}
#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
		if (ui_icon_button(tr("Copy"), ICON_COPY, UI_ALIGN_CENTER)) {
			char *str = string_array_join(console_last_traces, "\n");
			iron_copy_to_clipboard(str);
		}
#endif

		ui_end_sticky();

		draw_font_t *_font           = g_font;
		i32          _font_size      = g_ui->font_size;
		i32          _element_offset = g_theme->ELEMENT_OFFSET;
		g_theme->ELEMENT_OFFSET      = 0;
		draw_font_t *f               = data_get_font("font_mono.ttf");
		ui_set_font(g_ui, f);
		g_ui->font_size = math_floor(15 * UI_SCALE());
		for (i32 i = 0; i < console_last_traces->length; ++i) {
			char    *t     = console_last_traces->buffer[i];
			i32      len   = string_length(t);
			float    max_w = g_ui->_w;
			uint32_t bg    = starts_with(t, ">") ? 0x22000000 : 0x00000000;
			if (draw_string_width(f, g_ui->font_size, t) <= max_w) {
				ui_text(t, UI_ALIGN_LEFT, bg);
				continue;
			}

			// Word wrap
			i32 start = 0;
			while (start < len) {
				i32 end        = start;
				i32 last_space = -1;
				while (end < len && draw_sub_string_width(f, g_ui->font_size, t, start, end + 1) <= max_w) {
					if (t[end] == ' ') {
						last_space = end;
					}
					end++;
				}
				if (end == len) {
					ui_text(substring(t, start, end), UI_ALIGN_LEFT, bg);
					break;
				}
				if (last_space > start) {
					ui_text(substring(t, start, last_space), UI_ALIGN_LEFT, bg);
					start = last_space + 1;
				}
				else {
					ui_text(substring(t, start, end > start ? end : start + 1), UI_ALIGN_LEFT, bg);
					start = end > start ? end : start + 1;
				}
			}
		}

		g_theme->ELEMENT_OFFSET = _element_offset;

		ui_set_font(g_ui, _font);
		g_ui->font_size = _font_size;
	}
}
