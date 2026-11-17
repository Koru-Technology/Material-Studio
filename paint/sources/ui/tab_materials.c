
#include "../global.h"

i32 _tab_materials_draw_slots;

static void tab_materials_duplicate_on_next_frame(void *_) {
	slot_material_duplicate(_tab_materials_draw_slots);
}
i32 tab_materials_drag_pos = -1;

void tab_materials_button_nodes() {
	if (ui_button(tr("Nodes"), UI_ALIGN_CENTER, "")) {
		ui_base_show_material_nodes();
	}
	else if (g_ui->is_hovered) {
		ui_tooltip(string_tmp("%s (%s)", tr("Show Node Editor"), (char *)any_map_get(g_keymap, "toggle_node_editor")));
	}
}

static void tab_materials_new_studio(void *_) {
	material_studio_new();
}

static void tab_materials_open_studio(void *m) {
	material_studio_open(m);
}

// Studio materials and materials with Variables (controls) open in the Material Studio, the others (node graphs) in the
// node editor. Both have Save to Library and Cancel.
static void tab_materials_edit(void *m) {
	if (material_studio_can_open(m)) {
		material_studio_open(m);
	}
	else {
		ui_nodes_edit_material(m);
	}
}

void tab_materials_draw_slots_menu() {
	i32              i = _tab_materials_draw_slots;
	slot_material_t *m = g_project->_->materials->buffer[i];

	if (ui_menu_button(tr("Edit"), "", ICON_EDIT)) {
		context_select_material(i);
		sys_notify_on_next_frame(&tab_materials_edit, m);
	}

	if (ui_menu_button(tr("To Fill Layer"), "", ICON_SPHERE)) {
		context_select_material(i);
		layers_create_fill_layer(UV_TYPE_UVMAP, mat4_nan(), -1);
	}

	// The selected fill layer (or the fill layer of the selected mask) takes this material
	slot_layer_t *fill_target = layers_fill_target(g_context->layer);
	if (fill_target != NULL && fill_target->fill_material != m && ui_menu_button(tr("Apply to Fill Layer"), "", ICON_SPHERE)) {
		layers_set_fill_material_later(fill_target, m);
	}

	if (ui_menu_button(tr("Export"), "", ICON_EXPORT)) {
		context_select_material(i);
		box_export_show_material();
	}

	if (ui_menu_button(tr("Bake"), "", ICON_BAKE)) {
		context_select_material(i);
		box_export_show_bake_material();
	}

	if (ui_menu_button(tr("Duplicate"), "ctrl+d", ICON_DUPLICATE)) {
		sys_notify_on_next_frame(&tab_materials_duplicate_on_next_frame, NULL);
	}

	if (g_project->_->materials->length > 1 && ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		slot_material_remove(m);
	}

	// What the tab's Edit button held
	ui_menu_separator();
	if (ui_menu_button(tr("New Studio Material"), "", ICON_PLUS)) {
		sys_notify_on_next_frame(&tab_materials_new_studio, NULL);
	}
	if (ui_menu_button(tr("Import"), "", ICON_IMPORT)) {
		project_import_material();
	}

	ui_menu_separator();
	ui_menu_align();
	ui_menu_label(tr("Channels"), NULL);
	ui_menu_align();
	ui_row2();
	bool channels_changed = false;
	ui_check(&m->paint_base, tr("Base Color"), "");
	channels_changed |= ui_item_changed();
	ui_check(&m->paint_opac, tr("Opacity"), "");
	channels_changed |= ui_item_changed();

	{
		ui_row2();
		ui_check(&m->paint_nor, tr("Normal"), "");
		channels_changed |= ui_item_changed();
		ui_check(&m->paint_height, tr("Height"), "");
		channels_changed |= ui_item_changed();
		ui_row2();
		ui_check(&m->paint_rough, tr("Roughness"), "");
		channels_changed |= ui_item_changed();
		ui_check(&m->paint_met, tr("Metallic"), "");
		channels_changed |= ui_item_changed();
		ui_row2();
		ui_check(&m->paint_emis, tr("Emission"), "");
		channels_changed |= ui_item_changed();
		ui_check(&m->paint_subs, tr("Subsurface"), "");
		channels_changed |= ui_item_changed();
		ui_check(&m->paint_occ, tr("Occlusion"), "");
		channels_changed |= ui_item_changed();
	}

	if (channels_changed) {
		make_material_parse_paint_material(true);
		ui_menu_keep_open = true;
	}
}

void tab_materials_draw_slots(bool mini) {
	f32 a     = mini ? 1.0 : ui_base_asset_scale() * 1.5; // Spheres larger than the other assets, they show the material
	i32 slotw = math_floor((50 * a + 1) * UI_SCALE() + g_config->window_scale * 2);
	i32 num   = math_floor(g_ui->_window_w / (float)slotw);
	if (num == 0) {
		return;
	}

	bool drag_pos_set = false;
	f32  uix          = 0.0;
	f32  uiy          = 0.0;
	i32  imgw_val     = math_floor(50 * a * UI_SCALE());

	for (i32 row = 0; row < math_floor(math_ceil(g_project->_->materials->length / (float)num)); ++row) {
		i32          mult = g_config->show_asset_names ? 2 : 1;
		f32_array_t *ar   = f32_array_create_from_raw_tmp(NULL, num * mult);
		for (i32 i = 0; i < num * mult; ++i) {
			ar->buffer[i] = 1 / (float)num;
		}
		ui_row(ar);

		g_ui->_x += mini ? 2 : 2 * UI_SCALE();
		f32 off = g_config->show_asset_names ? UI_ELEMENT_OFFSET() * 10.0 + (a - 1) * 50 * UI_SCALE() : 6;
		if (row > 0) {
			g_ui->_y += off;
		}

		for (i32 j = 0; j < num; ++j) {
			i32 imgw = math_floor(50 * a * UI_SCALE());
			i32 i    = j + row * num;
			if (i >= g_project->_->materials->length) {
				ui_end_element_of_size(imgw);
				if (g_config->show_asset_names) {
					ui_end_element_of_size(0);
				}
				continue;
			}
			gpu_texture_t *img      = UI_SCALE() * a > 1 ? g_project->_->materials->buffer[i]->image : g_project->_->materials->buffer[i]->image_icon;
			gpu_texture_t *img_full = g_project->_->materials->buffer[i]->image;

			// Highligh selected
			if (g_context->material == g_project->_->materials->buffer[i]) {
				if (mini) {
					f32 w = g_ui->_w / (float)UI_SCALE();
					ui_rect_round(0, -2, w - 2, w - 4, g_theme->HIGHLIGHT_COL, 3);
				}
				else {
					i32 w = 50 * a;
					ui_rect_round(-1, -1, w + 2, w + 2, g_theme->HIGHLIGHT_COL, 2 * UI_SCALE());
				}
			}

			// Draw material icon
			uix      = g_ui->_x;
			uiy      = g_ui->_y;
			i32 tile = UI_SCALE() > 1 ? 100 : 50;
			f32 imgh = mini ? ui_sidebar_w_mini * 0.85 : 50 * a * UI_SCALE();

			if (base_drag_material != NULL && tab_materials_drag_pos == i) {
				ui_fill(-1, -2, 2, imgw_val + 4, g_theme->HIGHLIGHT_COL);
			}

			g_ui->image_scroll_align = mini;
			ui_state_t state         = g_project->_->materials->buffer[i]->preview_ready
			                               ? ui_image(img, 0xffffffff, imgh)
			                               : ui_sub_image(resource_get("icons.k"), 0xffffffff, imgh, tile, tile, tile, tile);
			g_ui->image_scroll_align = true;

			if (state == UI_STATE_HOVERED && base_drag_material != NULL) {
				tab_materials_drag_pos = (mouse_x > uix + g_ui->_window_x + imgw_val / 2.0) ? i + 1 : i;
				drag_pos_set           = true;
			}

			// Draw material numbers when selecting a material via keyboard shortcut
			bool is_typing = g_ui->is_typing;
			if (!is_typing) {
				if (i < 9 && keymap_shortcut(any_map_get(g_keymap, "select_material"), SHORTCUT_TYPE_DOWN)) {
					char *number = i32_to_string(i + 1);
					i32   width  = draw_string_width(g_font, g_ui->font_size, number) + 10;
					i32   height = draw_font_height(g_font, g_ui->font_size);
					draw_set_color(g_theme->TEXT_COL);
					draw_filled_rect(uix, uiy, width, height);
					draw_set_color(g_theme->BUTTON_COL);
					draw_string(number, uix + 5, uiy);
				}
			}

			// Select material
			if (state == UI_STATE_STARTED && g_ui->input_y > g_ui->_window_y) {
				if (g_context->material != g_project->_->materials->buffer[i]) {
					context_select_material(i);
				}
				base_drag_off_x    = -(mouse_x - uix - g_ui->_window_x - 3);
				base_drag_off_y    = -(mouse_y - uiy - g_ui->_window_y + 1);
				base_drag_material = g_context->material;
				// Double click to show nodes, studio materials open in the studio on touch
				if (sys_time() - g_context->select_time < 0.2) {
					if (g_config->touch_ui && material_studio_can_open(g_context->material)) {
						sys_notify_on_next_frame(&tab_materials_open_studio, g_context->material);
					}
					else {
						ui_base_show_material_nodes();
					}
					base_drag_material = NULL;
					base_is_dragging   = false;
				}
				g_context->select_time = sys_time();
			}

			// Context menu
			if (g_ui->is_hovered && g_ui->input_released_r) {
				context_select_material(i);
				_tab_materials_draw_slots = i;
				ui_menu_draw(&tab_materials_draw_slots_menu, -1, -1);
			}
			if (g_ui->is_hovered) {
				ui_tooltip_image(img_full, 0);
				if (i < 9) {
					i32 i1 = i + 1;
					ui_tooltip(
					    string_tmp("%s - (%s %d)", g_project->_->materials->buffer[i]->canvas->name, (char *)any_map_get(g_keymap, "select_material"), i1));
				}
				else {
					ui_tooltip(g_project->_->materials->buffer[i]->canvas->name);
				}
			}
			if (g_config->show_asset_names) {
				g_ui->_y += slotw * 0.9 + (a - 1) * 5 * UI_SCALE();
				ui_base_asset_name(g_project->_->materials->buffer[i]->canvas->name, uix, imgh);
				if (g_ui->is_hovered) {
					if (i < 9) {
						i32 i1 = i + 1;
						ui_tooltip(
						    string_tmp("%s - (%s %d)", g_project->_->materials->buffer[i]->canvas->name, (char *)any_map_get(g_keymap, "select_material"), i1));
					}
					else {
						ui_tooltip(g_project->_->materials->buffer[i]->canvas->name);
					}
				}
				g_ui->_y -= slotw * 0.9 + (a - 1) * 5 * UI_SCALE();
				if (i == g_project->_->materials->length - 1) {
					g_ui->_y += j == num - 1 ? imgw : imgw + UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
				}
			}
		}

		g_ui->_y += mini ? 0 : 6;
	}

	if (base_drag_material != NULL && tab_materials_drag_pos == g_project->_->materials->length) {
		g_ui->_x = uix;
		g_ui->_y = uiy;
		ui_fill(imgw_val + 1, -2, 2, imgw_val + 4, g_theme->HIGHLIGHT_COL);
	}

	if (!drag_pos_set) {
		tab_materials_drag_pos = -1;
	}

	bool in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
	                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
	if (in_focus && g_ui->is_delete_down && g_project->_->materials->length > 1) {
		g_ui->is_delete_down = false;
		slot_material_remove(g_context->material);
	}
	if (in_focus && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_D) {
		_tab_materials_draw_slots = array_index_of(g_project->_->materials, g_context->material);
		sys_notify_on_next_frame(&tab_materials_duplicate_on_next_frame, NULL);
	}
	if (in_focus) {
		i32 i = array_index_of(g_project->_->materials, g_context->material);
		if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_UP) {
			if (i > 0) {
				context_select_material(i - 1);
			}
		}
		if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_DOWN) {
			if (i < g_project->_->materials->length - 1) {
				context_select_material(i + 1);
			}
		}
	}
}

void tab_materials_button_new(char *text) {
	if (ui_icon_button(text, ICON_PLUS, UI_ALIGN_CENTER)) {
		// Touch makes it in the Material Studio, the node editor is a step away with Edit Nodes
		sys_notify_on_next_frame(g_config->touch_ui ? &tab_materials_new_studio : &slot_material_new_on_next_frame, NULL);
	}
}

void tab_materials_draw_mini(i32 *htab) {
	ui_set_hovered_tab_name(tr("Materials"));

	ui_begin_sticky();
	ui_separator(5, true);

	tab_materials_button_nodes();
	tab_materials_button_new("");

	ui_end_sticky();
	ui_separator(3, false);
	tab_materials_draw_slots(true);
}

void tab_materials_draw_full(i32 *htab) {
	if (ui_tab(htab, tr("Materials"), false, -1, false)) {
		ui_begin_sticky();
		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        -64,
		        -64,
		    },
		    2);
		ui_row(row);

		tab_materials_button_new(tr("New"));
		tab_materials_button_nodes();

		ui_end_sticky();
		ui_separator(3, false);
		tab_materials_draw_slots(false);
		ui_node_params_draw(CANVAS_TYPE_MATERIAL, &g_config->material_params);
	}
}

void tab_materials_draw(i32 *htab) {
	bool mini = g_ui->_window_w <= ui_sidebar_w_mini;
	mini ? tab_materials_draw_mini(htab) : tab_materials_draw_full(htab);
}

void tab_materials_accept_material_drop(slot_material_t *material) {
	if (tab_materials_drag_pos == -1) {
		return;
	}

	i32 mat_pos = array_index_of(g_project->_->materials, material);
	if (mat_pos != -1 && math_abs(mat_pos - tab_materials_drag_pos) > 0) {
		array_remove(g_project->_->materials, material);
		i32 new_pos = tab_materials_drag_pos - mat_pos > 0 ? tab_materials_drag_pos - 1 : tab_materials_drag_pos;
		array_insert(g_project->_->materials, new_pos, material);
	}
}

