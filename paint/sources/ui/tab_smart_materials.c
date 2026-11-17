#include "../global.h"

// Smart materials tab: saved fill layers and groups, tap one to pick it, Add to Layers puts it on top of the layers

static i32   tab_smart_materials_i         = -1;
static char *tab_smart_materials_selected  = NULL; // Name of the picked smart material

// Applying and saving draw to layers and export materials, they run when the frame starts, not while the UI is drawn

static void tab_smart_materials_apply_next(void *name) {
	smart_materials_apply(name);
}

static void tab_smart_materials_menu() {
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		char *name = smart_materials_name(tab_smart_materials_i);
		if (name != NULL && tab_smart_materials_selected != NULL && string_equals(name, tab_smart_materials_selected)) {
			tab_smart_materials_selected = NULL;
		}
		smart_materials_delete(tab_smart_materials_i);
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
	}
}

void tab_smart_materials_draw(i32 *htab) {
	if (!ui_tab(htab, tr("Smart"), false, -1, false)) {
		return;
	}

	ui_begin_sticky();
	f32_array_t *row = f32_array_create_from_raw_tmp((f32[]){-64, -140}, 2);
	ui_row(row);
	g_ui->enabled = smart_materials_save_target() != NULL;
	if (ui_icon_button(tr("Save"), ICON_PLUS, UI_ALIGN_CENTER)) {
		smart_materials_show_save();
	}
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Saves the selected fill layer or group with its masks"));
	}
	if (tab_smart_materials_selected != NULL && smart_materials_find(tab_smart_materials_selected) < 0) {
		tab_smart_materials_selected = NULL;
	}
	g_ui->enabled = tab_smart_materials_selected != NULL;
	if (ui_icon_button(tr("Add to Layers"), ICON_LAYER_NEW, UI_ALIGN_CENTER)) {
		sys_notify_on_next_frame(&tab_smart_materials_apply_next, string_copy(tab_smart_materials_selected));
	}
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Puts the picked smart material on top of the layers"));
	}
	g_ui->enabled = true;
	ui_end_sticky();
	ui_separator(3, false);

	i32 count = smart_materials_count();
	if (count == 0) {
		ui_text(tr("Select a fill layer or group, then Save"), UI_ALIGN_LEFT, 0x00000000);
		return;
	}

	f32 a     = ui_base_asset_scale();
	i32 imgw  = math_floor(50 * a * UI_SCALE());
	i32 slotw = math_floor((50 * a + 1) * UI_SCALE());
	i32 num   = math_floor(g_ui->_window_w / (float)slotw);
	if (num < 1) {
		num = 1;
	}
	f32_array_t *ratio = f32_array_create_from_raw_tmp(NULL, num);
	for (i32 i = 0; i < num; ++i) {
		ratio->buffer[i] = 1 / (float)num;
	}
	for (i32 r = 0; r * num < count; ++r) {
		ui_row(ratio);
		for (i32 j = 0; j < num; ++j) {
			i32 i = r * num + j;
			if (i >= count) {
				ui_end_element_of_size(imgw);
				continue;
			}
			gpu_texture_t *icon = smart_materials_icon(i);
			bool           picked = tab_smart_materials_selected != NULL && string_equals(smart_materials_name(i), tab_smart_materials_selected);
			if (picked) {
				ui_rect_round(-1, -1, 50 * a + 2, 50 * a + 2, g_theme->HIGHLIGHT_COL, 2 * UI_SCALE());
			}
			ui_fill(0, 0, 50 * a, 50 * a, 0xff1b1b1b);
			ui_state_t state = UI_STATE_IDLE;
			if (icon != NULL) {
				g_ui->image_scroll_align = false;
				state                    = ui_image(icon, 0xffffffff, imgw);
				g_ui->image_scroll_align = true;
			}
			else {
				state = ui_button("", UI_ALIGN_CENTER, "") ? UI_STATE_RELEASED : UI_STATE_IDLE;
			}
			if (state == UI_STATE_RELEASED) {
				tab_smart_materials_selected = string_copy(smart_materials_name(i));
				ui_dock_redraw(TAB_AREA_SIDEBAR1);
			}
			if (g_ui->is_hovered) {
				ui_tooltip(smart_materials_name(i));
				if (g_ui->input_released_r) {
					tab_smart_materials_i = i;
					ui_menu_draw(&tab_smart_materials_menu, -1, -1);
				}
			}
		}
		ui_row(ratio);
		for (i32 j = 0; j < num; ++j) {
			i32 i = r * num + j;
			if (i >= count) {
				ui_end_element_of_size(UI_ELEMENT_H());
				continue;
			}
			ui_text(smart_materials_name(i), UI_ALIGN_CENTER, 0x00000000);
		}
		g_ui->_y += 4 * UI_SCALE();
	}
}
