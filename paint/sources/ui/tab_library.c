#include "../global.h"

// Materials of the library folder (library.c): previews only, tapping one adds it to the project

static char *tab_library_search = "";

static bool tab_library_matches(library_entry_t *e) {
	if (string_equals(tab_library_search, "")) {
		return true;
	}
	char *search = to_lower_case(tab_library_search);
	return string_index_of(to_lower_case(library_entry_rel(e)), search) >= 0;
}

void tab_library_draw(i32 *htab) {
	if (!ui_tab(htab, tr("Library"), false, -1, false)) {
		return;
	}

	ui_begin_sticky();
	tab_library_search = string_copy(ui_text_input(&tab_library_search, tr("Search"), UI_ALIGN_LEFT, true, true));
	ui_text(library_status(), UI_ALIGN_LEFT, 0x00000000);
	if (!library_has_folder() && ui_icon_button(tr("Choose Folder..."), ICON_FOLDER_OPEN, UI_ALIGN_CENTER)) {
		library_pick_folder();
	}
	ui_end_sticky();
	ui_separator(3, false);

	// Ready items matching the search
	i32  count   = library_entry_count();
	i32 *visible = malloc(sizeof(i32) * (count > 0 ? count : 1));
	i32  shown   = 0;
	for (i32 i = 0; i < count; ++i) {
		library_entry_t *e = library_entry_get(i);
		if (library_entry_ready(e) && !library_entry_broken(e) && tab_library_matches(e)) {
			visible[shown++] = i;
		}
	}

	i32 slotw = math_floor(80 * UI_SCALE());
	i32 num   = math_floor(g_ui->_window_w / (float)slotw);
	if (num < 1) {
		num = 1;
	}
	f32 imgh   = 64 * UI_SCALE();
	i32 text_h = UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
	i32 row_h  = math_floor(imgh + text_h + 8 * UI_SCALE());

	f32_array_t *ar = f32_array_create_from_raw_tmp(NULL, num);
	for (i32 i = 0; i < num; ++i) {
		ar->buffer[i] = 1 / (float)num;
	}

	for (i32 row = 0; row * num < shown; ++row) {
		// Rows out of view only take their space, their previews are not loaded
		if (g_ui->_y + row_h < 0 || g_ui->_y > g_ui->_window_h) {
			g_ui->_y += row_h;
			continue;
		}

		ui_row(ar);
		for (i32 j = 0; j < num; ++j) {
			i32 k = row * num + j;
			if (k >= shown) {
				ui_end_element_of_size(imgh);
				continue;
			}
			library_entry_t *e    = library_entry_get(visible[k]);
			gpu_texture_t   *icon = library_entry_icon(e);
			ui_state_t       state;
			g_ui->image_scroll_align = false;
			if (icon != NULL) {
				state = ui_image(icon, 0xffffffff, imgh);
			}
			else {
				rect_t *r = resource_tile50(resource_get("icons.k"), ICON_SPHERE);
				state     = ui_sub_image(resource_get("icons.k"), 0xff555555, imgh, r->x, r->y, r->w, r->h);
			}
			g_ui->image_scroll_align = true;
			if (state == UI_STATE_RELEASED && !g_ui->is_scrolling) {
				library_use(e);
			}
			if (g_ui->is_hovered) {
				ui_tooltip(library_entry_rel(e));
			}
		}

		ui_row(ar);
		for (i32 j = 0; j < num; ++j) {
			i32 k = row * num + j;
			if (k >= shown) {
				ui_end_element_of_size(text_h);
				continue;
			}
			ui_text(library_entry_name(library_entry_get(visible[k])), UI_ALIGN_CENTER, 0x00000000);
		}
		g_ui->_y += 8 * UI_SCALE();
	}
	free(visible);
}
