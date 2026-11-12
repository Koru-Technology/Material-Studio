
#include "../global.h"

i32 _tab_brushes_draw_i;
i32 tab_brushes_drag_pos = -1;

void tab_brushes_draw_make_brush_preview(void *_) {
	i32           i      = _tab_brushes_draw_i;
	slot_brush_t *_brush = g_context->brush;
	g_context->brush     = g_project->_->brushes->buffer[i];
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	util_render_make_brush_preview();
	g_context->brush = _brush;
}

static bool tab_brushes_preview_pending = false;

void tab_brushes_draw_make_selected_preview(void *_) {
	// Wait for the startup init on frame 2
	if (g_context->frame < 3) {
		sys_notify_on_next_frame(&tab_brushes_draw_make_selected_preview, NULL);
		return;
	}
	tab_brushes_preview_pending = false;
	if (g_context->brush->preview_ready) {
		return;
	}
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	util_render_make_brush_preview();
	if (g_context->brush->preview_ready) {
		ui_dock_redraw(-1);
	}
}

void tab_brushes_draw_duplicate(void *_) {
	i32 i            = _tab_brushes_draw_i;
	g_context->brush = slot_brush_create(NULL);
	any_array_push(g_project->_->brushes, g_context->brush);
	ui_node_canvas_t *cloned = util_clone_canvas(g_project->_->brushes->buffer[i]->canvas);
	cloned->name             = string_copy(slot_brush_unique_name(cloned, cloned->name));
	g_context->brush->canvas = cloned;
	context_set_brush(g_context->brush);
	util_render_make_brush_preview();
	history_duplicate_brush();
}

void tab_brushes_delete_brush(slot_brush_t *b) {
	i32 i            = array_index_of(g_project->_->brushes, b);
	g_context->brush = b;
	history_delete_brush();
	context_select_brush(i == g_project->_->brushes->length - 1 ? i - 1 : i + 1);
	array_splice(g_project->_->brushes, i, 1);
	ui_dock_redraw(1);
}

void tab_brushes_draw_context_menu() {
	i32 i = _tab_brushes_draw_i;
	// let b: slot_brush_t = brushes[i];

	if (ui_menu_button(tr("Export"), "", ICON_EXPORT)) {
		context_select_brush(i);
		box_export_show_brush();
	}

	if (ui_menu_button(tr("Duplicate"), "ctrl+d", ICON_DUPLICATE)) {
		sys_notify_on_next_frame(&tab_brushes_draw_duplicate, NULL);
	}

	if (g_project->_->brushes->length > 1 && ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		tab_brushes_delete_brush(g_project->_->brushes->buffer[i]);
	}
}

void tab_brushes_draw_edit() {
	if (ui_menu_button(tr("Import"), "", ICON_IMPORT)) {
		project_import_brush();
	}
}

// Brush presets: the tool, brush settings and material saved by name, kept across projects

static i32   tab_brushes_preset_i    = -1; // Preset being renamed, -1 while saving a new one
static char *tab_brushes_preset_name = "";

static i32 tab_brushes_preset_tab = 0;

// Saving exports the material and applying may bring one in, they run when the frame starts, not while the UI is drawn

static void tab_brushes_preset_add_next(void *name) {
	brush_presets_add(name);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

static void tab_brushes_preset_apply_next(void *name) {
	brush_presets_apply(brush_presets_find(name));
}

static void tab_brushes_preset_name_box() {
	if (ui_tab(&tab_brushes_preset_tab, tab_brushes_preset_i < 0 ? tr("Save Preset") : tr("Rename Preset"), false, -1, false)) {
		ui_row2();
		char *name = ui_text_input(&tab_brushes_preset_name, tr("Name"), UI_ALIGN_LEFT, true, false);
		if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
			if (tab_brushes_preset_i < 0) {
				sys_notify_on_next_frame(&tab_brushes_preset_add_next, string_copy(name));
			}
			else {
				brush_presets_rename(tab_brushes_preset_i, name);
			}
			ui_box_hide();
			ui_dock_redraw(TAB_AREA_SIDEBAR1);
		}
	}
}

static void tab_brushes_preset_save() {
	tab_brushes_preset_i = -1;
	char *material       = g_context->material != NULL ? g_context->material->canvas->name : tr("Brush");
	char *name           = string_copy(material);
	for (i32 n = 2; brush_presets_find(name) >= 0; ++n) {
		name = string("%s %d", material, n);
	}
	tab_brushes_preset_name = name;
	ui_box_show_custom(&tab_brushes_preset_name_box, 400, 200, NULL, true, "");
}

static void tab_brushes_preset_menu() {
	i32 i = tab_brushes_preset_i;
	if (ui_menu_button(tr("Update"), "", ICON_SAVE)) {
		sys_notify_on_next_frame(&tab_brushes_preset_add_next, string_copy(brush_presets_name(i))); // Same name, the current settings
	}
	if (ui_menu_button(tr("Rename"), "", ICON_EDIT)) {
		tab_brushes_preset_name = string_copy(brush_presets_name(i));
		ui_box_show_custom(&tab_brushes_preset_name_box, 400, 200, NULL, true, "");
	}
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		brush_presets_delete(i);
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
	}
}

static void tab_brushes_draw_presets() {
	ui_separator(3, false);
	ui_row2();
	ui_text(tr("Presets"), UI_ALIGN_LEFT, g_theme->SEPARATOR_COL);
	if (ui_icon_button(tr("Save"), ICON_PLUS, UI_ALIGN_CENTER)) {
		tab_brushes_preset_save();
	}
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Saves the tool, the brush settings and the material"));
	}
	i32 count = brush_presets_count();
	for (i32 i = 0; i < count; ++i) {
		char *name = brush_presets_name(i);
		if (ui_button(name, UI_ALIGN_LEFT, "")) {
			sys_notify_on_next_frame(&tab_brushes_preset_apply_next, string_copy(name));
		}
		if (g_ui->is_hovered && g_ui->input_released_r) {
			tab_brushes_preset_i = i;
			ui_menu_draw(&tab_brushes_preset_menu, -1, -1);
		}
	}
}

// Built-in brushes: tip and stroke settings, tap to use
static void tab_brushes_draw_builtin() {
	ui_separator(3, false);
	ui_text(tr("Built-in"), UI_ALIGN_LEFT, g_theme->SEPARATOR_COL);
	f32 a     = ui_base_asset_scale();
	i32 imgw  = math_floor(50 * a * UI_SCALE());
	i32 slotw = math_floor((50 * a + 1) * UI_SCALE());
	i32 num   = math_floor(g_ui->_window_w / (float)slotw);
	if (num < 1) {
		num = 1;
	}
	i32          count = builtin_brush_count();
	f32_array_t *ratio = f32_array_create_from_raw_tmp(NULL, num);
	for (i32 i = 0; i < num; ++i) {
		ratio->buffer[i] = 1 / (float)num;
	}
	for (i32 row = 0; row * num < count; ++row) {
		ui_row(ratio);
		for (i32 j = 0; j < num; ++j) {
			i32 i = row * num + j;
			if (i >= count) {
				ui_end_element_of_size(imgw);
				continue;
			}
			if (g_context->brush_builtin == i) {
				ui_rect_round(-1, -1, 50 * a + 2, 50 * a + 2, g_theme->HIGHLIGHT_COL, 2 * UI_SCALE());
			}
			gpu_texture_t *icon      = builtin_brush_icon(i);
			g_ui->image_scroll_align = false;
			ui_fill(0, 0, 50 * a, 50 * a, 0xff1b1b1b);
			ui_state_t state         = ui_image(icon, 0xffffffff, imgw);
			g_ui->image_scroll_align = true;
			if (state == UI_STATE_RELEASED) {
				builtin_brush_use(i);
			}
			if (g_ui->is_hovered) {
				ui_tooltip(builtin_brush_name(i));
			}
		}
		if (g_config->show_asset_names) {
			ui_row(ratio);
			for (i32 j = 0; j < num; ++j) {
				i32 i = row * num + j;
				if (i >= count) {
					ui_end_element_of_size(UI_ELEMENT_H());
					continue;
				}
				ui_text(builtin_brush_name(i), UI_ALIGN_CENTER, 0x00000000);
			}
		}
		g_ui->_y += 4 * UI_SCALE();
	}
}

// Touch brush list: one brush per row, its name above its stroke, like a brush library.
// Rows are the project brushes, then the built-in brushes. Their strokes render one per frame and are kept.

static gpu_texture_t *tab_brushes_builtin_rows[64];
static i32            tab_brushes_row_pending = -1; // Row whose stroke renders on the next frame
static i32            tab_brushes_row_refresh = -1; // Rows from this one render again, the pen settings changed; -1 when current

// The pen settings that shape the strokes: they are painted as a Pencil stroke
typedef struct {
	i32 dyn[5];
	f32 curve;
	f32 min;
	f32 sensitivity;
} tab_brushes_pen_t;

static tab_brushes_pen_t tab_brushes_pen;
static bool              tab_brushes_pen_set = false;

static void tab_brushes_check_pen() {
	tab_brushes_pen_t p;
	memset(&p, 0, sizeof(p));
	p.dyn[0]      = g_config->dyn_size;
	p.dyn[1]      = g_config->dyn_opacity;
	p.dyn[2]      = g_config->dyn_flow;
	p.dyn[3]      = g_config->dyn_hardness;
	p.dyn[4]      = g_config->dyn_angle;
	p.curve       = g_config->pressure_curve;
	p.min         = g_config->pressure_min;
	p.sensitivity = g_config->pressure_sensitivity;
	if (tab_brushes_pen_set && memcmp(&p, &tab_brushes_pen, sizeof(p)) != 0) {
		tab_brushes_row_refresh         = 0;
		g_context->brush->preview_ready = false; // The parameters panel stroke too
	}
	tab_brushes_pen     = p;
	tab_brushes_pen_set = true;
}

static gpu_texture_t *tab_brushes_row_image(i32 r) {
	i32 n = g_project->_->brushes->length;
	return r < n ? g_project->_->brushes->buffer[r]->image_row : tab_brushes_builtin_rows[r - n];
}

static void tab_brushes_make_row_image(void *_) {
	i32 r                   = tab_brushes_row_pending;
	tab_brushes_row_pending = -1;
	i32 n                   = g_project->_->brushes->length;
	if (r < 0 || r >= n + builtin_brush_count() || r - n >= 64) {
		return;
	}

	slot_brush_t *_brush          = g_context->brush;
	i32           _builtin        = g_context->brush_builtin;
	f32           _hardness       = g_context->brush_hardness;
	f32           _spacing        = g_context->brush_spacing;
	f32           _scatter        = g_context->brush_scatter;
	f32           _jitter_size    = g_context->brush_jitter_size;
	f32           _jitter_angle   = g_context->brush_jitter_angle;
	f32           _jitter_opacity = g_context->brush_jitter_opacity;
	f32           _flow           = g_context->brush_flow;
	if (r < n) {
		g_context->brush         = g_project->_->brushes->buffer[r];
		g_context->brush_builtin = -1;
	}
	else {
		builtin_brush_settings(r - n);
	}
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	util_render_make_brush_preview();

	gpu_texture_t  *src = g_context->brush->image_wave;
	gpu_texture_t **dst = r < n ? &g_project->_->brushes->buffer[r]->image_row : &tab_brushes_builtin_rows[r - n];
	if (src != NULL) {
		if (*dst == NULL) {
			*dst = gpu_create_render_target(src->width, src->height, GPU_TEXTURE_FORMAT_RGBA32);
		}
		gpu_texture_t *current = _draw_current;
		bool           in_use  = gpu_in_use;
		if (in_use)
			draw_end();
		draw_begin(*dst, true, 0x00000000);
		draw_set_color(0xffffffff);
		draw_image(src, 0, 0);
		draw_end();
		if (in_use)
			draw_begin(current, false, 0);
	}
	g_context->brush->preview_ready = false; // Its previews show this row's stroke now, the parameters panel renders them again
	if (tab_brushes_row_refresh >= 0 && r >= tab_brushes_row_refresh) {
		tab_brushes_row_refresh = r + 1 < n + builtin_brush_count() ? r + 1 : -1;
	}

	g_context->brush                = _brush;
	g_context->brush_builtin        = _builtin;
	g_context->brush_hardness       = _hardness;
	g_context->brush_spacing        = _spacing;
	g_context->brush_scatter        = _scatter;
	g_context->brush_jitter_size    = _jitter_size;
	g_context->brush_jitter_angle   = _jitter_angle;
	g_context->brush_jitter_opacity = _jitter_opacity;
	g_context->brush_flow           = _flow;
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	ui_popovers_redraw(-1);
}

// A row: name above the stroke, the selected one highlighted
static ui_state_t tab_brushes_row(char *name, gpu_texture_t *img, bool selected) {
	f32 pad    = math_floor(10 * UI_SCALE());
	f32 w      = g_ui->_w;
	f32 wave_w = w - pad * 2;
	f32 wave_h = math_floor(wave_w * util_render_brush_wave_strip);
	f32 name_h = UI_ELEMENT_H();
	f32 h      = pad / 2 + name_h + wave_h + pad / 2;

	ui_state_t state = UI_STATE_IDLE;
	if (ui_get_started(h)) {
		state = UI_STATE_STARTED;
	}
	else if (ui_get_released(h)) {
		state = UI_STATE_RELEASED;
	}
	else if (ui_get_hover(h)) {
		state = UI_STATE_HOVERED;
	}

	if (ui_is_visible(h)) {
		f32 x = g_ui->_x;
		f32 y = g_ui->_y;
		if (selected) {
			draw_set_color(g_theme->HIGHLIGHT_COL);
			ui_draw_rect(true, false, x, y, w, h);
		}
		draw_set_color(g_theme->TEXT_COL);
		draw_set_font(g_ui->ops->font, g_ui->font_size);
		f32 ty = y + pad / 2 + (name_h - draw_font_height(g_ui->ops->font, g_ui->font_size)) / 2.0;
		draw_string(name, x + pad, ty);
		if (img != NULL) {
			draw_set_color(0xffffffff);
			draw_scaled_image(img, x + pad, y + pad / 2 + name_h, wave_w, wave_h);
		}
	}
	ui_end_element_of_size(h);
	return state;
}

static void tab_brushes_select_project_brush(i32 i) {
	if (g_context->brush_builtin >= 0) { // A project brush replaces the built-in tip
		g_context->brush_builtin = -1;
		brush_output_node_parse_inputs();
		g_context->brush->preview_ready = false;
		config_save();
	}
	if (g_context->brush != g_project->_->brushes->buffer[i]) {
		context_select_brush(i);
	}
}

static void tab_brushes_draw_rows() {
	tab_brushes_check_pen();
	i32 n     = g_project->_->brushes->length;
	i32 count = n + builtin_brush_count();
	for (i32 r = 0; r < count; ++r) {
		gpu_texture_t *img   = tab_brushes_row_image(r);
		bool           stale = img == NULL || (tab_brushes_row_refresh >= 0 && r >= tab_brushes_row_refresh);
		if (stale && tab_brushes_row_pending == -1) {
			tab_brushes_row_pending = r;
			sys_notify_on_next_frame(&tab_brushes_make_row_image, NULL);
		}
		if (r < n) {
			slot_brush_t *b     = g_project->_->brushes->buffer[r];
			bool          sel   = g_context->brush == b && g_context->brush_builtin < 0;
			ui_state_t    state = tab_brushes_row(b->canvas->name, img, sel);
			if (state == UI_STATE_RELEASED) {
				tab_brushes_select_project_brush(r);
				if (sys_time() - g_context->select_time < 0.4) {
					ui_base_show_brush_nodes(); // Double tap
				}
				g_context->select_time = sys_time();
			}
			if (g_ui->is_hovered && g_ui->input_released_r) {
				context_select_brush(r);
				_tab_brushes_draw_i = r;
				ui_menu_draw(&tab_brushes_draw_context_menu, -1, -1);
			}
		}
		else {
			i32 i = r - n;
			if (tab_brushes_row(builtin_brush_name(i), img, g_context->brush_builtin == i) == UI_STATE_RELEASED) {
				builtin_brush_use(i);
			}
		}
	}
}

static void tab_brushes_new() {
	g_context->brush = slot_brush_create(NULL);
	any_array_push(g_project->_->brushes, g_context->brush);
	make_material_parse_brush();
	history_new_brush();
	ui_nodes_hwnd->redraws = 2;
}

static void tab_brushes_draw_slots(bool popover);

// Brushes tab content
void tab_brushes_draw_content() {
	tab_brushes_draw_slots(false);
}

// Touch popover, the only content so no tab above it: title with Nodes, Edit and + buttons
void tab_brushes_draw_popover() {
	ui_set_hovered_tab_name(tr("Brushes")); // See context_in_brushes()
	tab_brushes_draw_slots(true);
}

static void tab_brushes_draw_slots(bool popover) {
	// The parameters panel shows the stroke preview of the selected brush
	if (!g_context->brush->preview_ready && !tab_brushes_preview_pending) {
		tab_brushes_preview_pending = true;
		sys_notify_on_next_frame(&tab_brushes_draw_make_selected_preview, NULL);
	}

	ui_begin_sticky();
	if (popover) {
		f32          button = UI_ELEMENT_H() / (f32)g_ui->_w;
		f32_array_t *row    = f32_array_create_from_raw_tmp(
            (f32[]){
                1.0 - button * 3,
                button,
                button,
                button,
            },
            4);
		ui_row(row);
		ui_text(tr("Brushes"), UI_ALIGN_LEFT, 0x00000000);
		if (ui_icon_button("", ICON_NODES, UI_ALIGN_CENTER)) {
			ui_base_show_brush_nodes();
		}
		if (ui_icon_button("", ICON_EDIT, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_brushes_draw_edit, -1, -1);
		}
		if (ui_icon_button("", ICON_PLUS, UI_ALIGN_CENTER)) {
			tab_brushes_new();
		}
	}
	else {
		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        -64,
		        -64,
		        -64,
		    },
		    3);
		ui_row(row);
		if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
			tab_brushes_new();
		}
		if (ui_button(tr("Nodes"), UI_ALIGN_CENTER, "")) {
			ui_base_show_brush_nodes();
		}
		if (ui_icon_button(tr("Edit"), ICON_EDIT, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_brushes_draw_edit, -1, -1);
		}
	}
	ui_end_sticky();
	ui_separator(3, false);

	if (popover) {
		tab_brushes_draw_rows();
		tab_brushes_draw_presets();
		ui_node_params_draw(CANVAS_TYPE_BRUSH, &g_config->brush_params);
		return;
	}

	f32 a     = ui_base_asset_scale();
	i32 slotw = math_floor((50 * a + 1) * UI_SCALE());
	i32 num   = math_floor(g_ui->_window_w / (float)slotw);
	if (num == 0) {
		return;
	}

	bool drag_pos_set = false;
	f32  uix          = 0.0;
	f32  uiy          = 0.0;
	i32  imgw_val     = math_floor(50 * a * UI_SCALE());

	for (i32 row = 0; row < math_floor(math_ceil(g_project->_->brushes->length / (float)num)); ++row) {
		i32          mult = g_config->show_asset_names ? 2 : 1;
		f32_array_t *ar   = f32_array_create_from_raw((f32[]){}, 0);
		for (i32 i = 0; i < num * mult; ++i) {
			f32_array_push(ar, 1 / (float)num);
		}
		ui_row(ar);

		g_ui->_x += 2 * UI_SCALE();
		f32 off = g_config->show_asset_names ? UI_ELEMENT_OFFSET() * 10.0 + (a - 1) * 50 * UI_SCALE() : 6;
		if (row > 0) {
			g_ui->_y += off;
		}

		for (i32 j = 0; j < num; ++j) {
			i32 imgw = math_floor(50 * a * UI_SCALE());
			i32 i    = j + row * num;
			if (i >= g_project->_->brushes->length) {
				ui_end_element_of_size(imgw);
				if (g_config->show_asset_names) {
					ui_end_element_of_size(0);
				}
				continue;
			}
			gpu_texture_t *img      = UI_SCALE() > 1 ? g_project->_->brushes->buffer[i]->image : g_project->_->brushes->buffer[i]->image_icon;
			gpu_texture_t *img_full = g_project->_->brushes->buffer[i]->image;

			if (g_context->brush == g_project->_->brushes->buffer[i]) {
				// ui_fill(1, -2, img.width + 3, img.height + 3, ui.ops.theme.HIGHLIGHT_COL); // TODO
				i32 w = 50 * a;
				ui_rect_round(-1, -1, w + 2, w + 2, g_theme->HIGHLIGHT_COL, 2 * UI_SCALE());
			}

			uix      = g_ui->_x;
			uiy      = g_ui->_y;
			i32 tile = UI_SCALE() > 1 ? 100 : 50;

			if (base_drag_brush != NULL && tab_brushes_drag_pos == i) {
				ui_fill(-1, -2, 2, imgw_val + 4, g_theme->HIGHLIGHT_COL);
			}

			g_ui->image_scroll_align = false;
			ui_state_t state         = g_project->_->brushes->buffer[i]->preview_ready
			                               ? ui_image(img, 0xffffffff, imgw)
			                               : ui_sub_image(resource_get("icons.k"), -1, imgw, tile * 5, tile, tile, tile);
			g_ui->image_scroll_align = true;

			if (state == UI_STATE_HOVERED && base_drag_brush != NULL) {
				tab_brushes_drag_pos = (mouse_x > uix + g_ui->_window_x + imgw_val / 2.0) ? i + 1 : i;
				drag_pos_set         = true;
			}

			if (state == UI_STATE_STARTED) {
				if (g_context->brush_builtin >= 0) { // A project brush replaces the built-in tip
					g_context->brush_builtin = -1;
					brush_output_node_parse_inputs();
					g_context->brush->preview_ready = false;
					config_save();
				}
				if (g_context->brush != g_project->_->brushes->buffer[i]) {
					context_select_brush(i);
				}
				base_drag_off_x = -(mouse_x - uix - g_ui->_window_x - 3);
				base_drag_off_y = -(mouse_y - uiy - g_ui->_window_y + 1);
				base_drag_brush = g_context->brush;
				if (sys_time() - g_context->select_time < 0.2) {
					ui_base_show_brush_nodes();
					base_drag_brush  = NULL;
					base_is_dragging = false;
				}
				g_context->select_time = sys_time();
			}
			if (g_ui->is_hovered && g_ui->input_released_r) {
				context_select_brush(i);

				_tab_brushes_draw_i = i;

				ui_menu_draw(&tab_brushes_draw_context_menu, -1, -1);
			}

			if (g_ui->is_hovered) {
				if (img_full == NULL) {
					_tab_brushes_draw_i = i;
					sys_notify_on_next_frame(&tab_brushes_draw_make_brush_preview, NULL);
				}
				else {
					ui_tooltip_image(img_full, 0);
					ui_tooltip(g_project->_->brushes->buffer[i]->canvas->name);
				}
			}

			if (g_config->show_asset_names) {
				g_ui->_y += slotw * 0.9 + (a - 1) * 5 * UI_SCALE();
				ui_base_asset_name(g_project->_->brushes->buffer[i]->canvas->name, uix, imgw);
				if (g_ui->is_hovered) {
					ui_tooltip(g_project->_->brushes->buffer[i]->canvas->name);
				}
				g_ui->_y -= slotw * 0.9 + (a - 1) * 5 * UI_SCALE();
				if (i == g_project->_->brushes->length - 1) {
					g_ui->_y += j == num - 1 ? imgw : imgw + UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
				}
			}
		}

		g_ui->_y += 6;
	}

	if (base_drag_brush != NULL && tab_brushes_drag_pos == g_project->_->brushes->length) {
		g_ui->_x = uix;
		g_ui->_y = uiy;
		ui_fill(imgw_val + 1, -2, 2, imgw_val + 4, g_theme->HIGHLIGHT_COL);
	}

	if (!drag_pos_set) {
		tab_brushes_drag_pos = -1;
	}

	bool in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
	                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
	if (in_focus && g_ui->is_delete_down && g_project->_->brushes->length > 1) {
		g_ui->is_delete_down = false;
		tab_brushes_delete_brush(g_context->brush);
	}
	if (in_focus && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_D) {
		_tab_brushes_draw_i = array_index_of(g_project->_->brushes, g_context->brush);
		sys_notify_on_next_frame(&tab_brushes_draw_duplicate, NULL);
	}
	if (in_focus) {
		i32 i = array_index_of(g_project->_->brushes, g_context->brush);
		if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_UP) {
			if (i > 0) {
				context_select_brush(i - 1);
			}
		}
		if (g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_DOWN) {
			if (i < g_project->_->brushes->length - 1) {
				context_select_brush(i + 1);
			}
		}
	}

	tab_brushes_draw_presets();

	if (!g_config->hide_builtin_brushes) {
		tab_brushes_draw_builtin();
	}

	ui_node_params_draw(CANVAS_TYPE_BRUSH, &g_config->brush_params);
}

void tab_brushes_draw(i32 *htab) {
	if (ui_tab(htab, tr("Brushes"), false, -1, false)) {
		tab_brushes_draw_content();
	}
}

void tab_brushes_accept_brush_drop(slot_brush_t *brush) {
	if (tab_brushes_drag_pos == -1) {
		return;
	}

	i32 brush_pos = array_index_of(g_project->_->brushes, brush);
	if (brush_pos != -1 && math_abs(brush_pos - tab_brushes_drag_pos) > 0) {
		array_remove(g_project->_->brushes, brush);
		i32 new_pos = tab_brushes_drag_pos - brush_pos > 0 ? tab_brushes_drag_pos - 1 : tab_brushes_drag_pos;
		array_insert(g_project->_->brushes, new_pos, brush);
	}
}
