#include "../global.h"

// Smart materials tab: saved fill layers and groups, tap to put one on top of the layers

static i32   tab_smart_materials_i         = -1;
static char *tab_smart_materials_save_name = "";
static i32   tab_smart_materials_box_tab   = 0;

// The layer Save takes: the selected fill layer or group, the fill layer of a selected mask
static slot_layer_t *tab_smart_materials_target() {
	slot_layer_t *l = g_context->layer;
	if (l == NULL) {
		return NULL;
	}
	if (slot_layer_is_mask(l)) {
		l = l->parent;
	}
	if (l == NULL || slot_layer_is_mask(l)) {
		return NULL;
	}
	return slot_layer_is_group(l) || l->fill_material != NULL ? l : NULL;
}

static void tab_smart_materials_save_box() {
	if (ui_tab(&tab_smart_materials_box_tab, tr("Save Smart Material"), false, -1, false)) {
		ui_row2();
		char *name = ui_text_input(&tab_smart_materials_save_name, tr("Name"), UI_ALIGN_LEFT, true, false);
		if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
			smart_materials_save(tab_smart_materials_target(), name);
			ui_box_hide();
			ui_dock_redraw(TAB_AREA_SIDEBAR1);
		}
	}
}

// Asks for a name and saves the selected fill layer or group
void tab_smart_materials_show_save() {
	slot_layer_t *l = tab_smart_materials_target();
	if (l == NULL) {
		return;
	}
	tab_smart_materials_save_name = string_copy(l->fill_material != NULL ? l->fill_material->canvas->name : l->name);
	ui_box_show_custom(&tab_smart_materials_save_box, 400, 200, NULL, true, "");
}

static void tab_smart_materials_menu() {
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		smart_materials_delete(tab_smart_materials_i);
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
	}
}

void tab_smart_materials_draw(i32 *htab) {
	if (!ui_tab(htab, tr("Smart"), false, -1, false)) {
		return;
	}

	ui_begin_sticky();
	f32_array_t *row = f32_array_create_from_raw_tmp((f32[]){-64, -64}, 2);
	ui_row(row);
	g_ui->enabled = tab_smart_materials_target() != NULL;
	if (ui_icon_button(tr("Save"), ICON_PLUS, UI_ALIGN_CENTER)) {
		tab_smart_materials_show_save();
	}
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Saves the selected fill layer or group with its masks"));
	}
	g_ui->enabled = true;
	if (ui_button(tr("Refresh"), UI_ALIGN_CENTER, "")) {
		smart_materials_refresh();
	}
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
				smart_materials_apply(smart_materials_name(i));
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
