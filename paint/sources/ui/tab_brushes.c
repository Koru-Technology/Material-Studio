
#include "../global.h"

// Brushes: the groups of the brush library on the left, the brushes of the shown group on the right, each with its stroke.
// The + of the title makes a brush in the Brush Creator or a group. A long press on a brush or a group of your own opens its menu,
// tapping the picked brush again edits it.

static i32            tab_brushes_group      = -1; // Id of the group shown, -1 for the group of the picked brush
static brush_def_t   *tab_brushes_menu_def   = NULL;
static brush_group_t *tab_brushes_menu_group = NULL;
static char          *tab_brushes_name_text   = "";
static i32            tab_brushes_box_tab     = 0;

// Renders the stroke of brush settings s into *dst, as shown in the brush list. Not while a window draws, see sys_notify_on_next_frame.
void tab_brushes_render_row(brush_settings_t *s, gpu_texture_t *tip, gpu_texture_t **dst) {
	static slot_brush_t *row_brush = NULL; // Default brush the rows paint with, the project brush nodes do not show
	if (row_brush == NULL) {
		row_brush = slot_brush_create(NULL);
	}
	slot_brush_t *_brush = g_context->brush;
	g_context->brush     = row_brush;
	brush_settings_t saved;
	brush_library_capture_settings(&saved);
	gpu_texture_t *saved_tip = brush_library_tip_image_get();
	brush_library_apply_settings(s, tip);
	util_render_brush_wave_radius = 0.1 + 0.3 * s->preview_size;
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	util_render_make_brush_preview();
	util_render_brush_wave_radius = 0.25;

	gpu_texture_t *src = g_context->brush->image_wave;
	if (src != NULL) {
		if (*dst == NULL) {
			*dst = gpu_create_render_target(src->width, src->height, GPU_TEXTURE_FORMAT_RGBA32);
		}
		gpu_texture_t *current = _draw_current;
		bool           in_use  = gpu_in_use;
		if (in_use) {
			draw_end();
		}
		draw_begin(*dst, true, 0x00000000);
		draw_set_color(0xffffffff);
		draw_image(src, 0, 0);
		draw_end();
		if (in_use) {
			draw_begin(current, false, 0);
		}
	}

	g_context->brush = _brush;
	brush_library_apply_settings(&saved, saved_tip);
	make_material_parse_brush();
	brush_output_node_parse_inputs();
}

// Draws a stroke strip so its middle band fills the rectangle, keeping its aspect
void tab_brushes_draw_stroke(gpu_texture_t *img, f32 x, f32 y, f32 w, f32 h) {
	if (img == NULL || w <= 0 || h <= 0) {
		return;
	}
	f32 band = (h / w) / util_render_brush_wave_strip;
	f32 sh   = img->height * (band < 1.0 ? band : 1.0);
	f32 sy   = (img->height - sh) / 2.0;
	draw_set_color(0xffffffff);
	draw_scaled_sub_image(img, 0, sy, img->width, sh, x, y, w, h);
}

// A long press: iron_ui turns a still touch into a right release where it was held
static bool tab_brushes_long_pressed(f32 h) {
	return g_ui->input_released_r && ui_get_hover(h);
}

// A row: name above the stroke, the picked one highlighted. The height does not follow the width, a scrollbar
// narrowing the list would change the height and the scrollbar would come and go every frame.
static ui_state_t tab_brushes_row(char *name, gpu_texture_t *img, bool selected, bool *long_press) {
	f32 pad    = math_floor(10 * UI_SCALE());
	f32 w      = g_ui->_w;
	f32 wave_w = w - pad * 2;
	f32 wave_h = math_floor(UI_ELEMENT_H() * 1.8);
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
	*long_press = tab_brushes_long_pressed(h);

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
		tab_brushes_draw_stroke(img, x + pad, y + pad / 2 + name_h, wave_w, wave_h);
	}
	ui_end_element_of_size(h);
	return state;
}

// A group in the list on the left: icon and name on a rounded button, the shown group highlighted
static ui_state_t tab_brushes_group_button(char *name, i32 icon, bool selected, bool *long_press) {
	f32 h   = math_floor(UI_ELEMENT_H() * 1.6);
	f32 gap = math_floor(6 * UI_SCALE());

	ui_state_t state = UI_STATE_IDLE;
	if (ui_get_released(h)) {
		state = UI_STATE_RELEASED;
	}
	else if (ui_get_hover(h)) {
		state = UI_STATE_HOVERED;
	}
	*long_press = tab_brushes_long_pressed(h);

	if (ui_is_visible(h)) {
		f32 x = g_ui->_x;
		f32 y = g_ui->_y;
		f32 w = g_ui->_w;
		draw_set_color(selected ? g_theme->HIGHLIGHT_COL : g_theme->BUTTON_COL);
		ui_draw_rect(true, false, x, y, w, h);
		f32            size = math_floor(UI_ELEMENT_H() * 0.8);
		f32            pad  = math_floor(10 * UI_SCALE());
		gpu_texture_t *img  = resource_get("icons.k");
		rect_t        *r    = resource_tile50(img, icon);
		draw_set_color(g_theme->TEXT_COL);
		draw_scaled_sub_image(img, r->x, r->y, r->w, r->h, x + pad, y + (h - size) / 2.0, size, size);
		draw_set_font(g_ui->ops->font, g_ui->font_size);
		draw_string(name, x + pad * 2 + size, y + (h - draw_font_height(g_ui->ops->font, g_ui->font_size)) / 2.0);
	}
	ui_end_element_of_size(h + gap);
	return state;
}

static void tab_brushes_redraw() {
	ui_popovers_redraw(-1);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

// Name of a new group, or of the group being renamed (tab_brushes_menu_group)
static void tab_brushes_group_box() {
	brush_group_t *g = tab_brushes_menu_group;
	if (ui_tab(&tab_brushes_box_tab, g != NULL ? tr("Rename Group") : tr("New Group"), false, -1, false)) {
		ui_row2();
		char *name = ui_text_input(&tab_brushes_name_text, tr("Name"), UI_ALIGN_LEFT, true, false);
		if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
			if (!string_equals(name, "")) {
				if (g != NULL) {
					brush_group_rename(g, name);
				}
				else {
					tab_brushes_group = brush_group_add(name)->id;
				}
			}
			ui_box_hide();
			tab_brushes_redraw();
		}
	}
}

static void tab_brushes_delete_brush_box() {
	brush_def_t *d = tab_brushes_menu_def;
	if (ui_tab(&tab_brushes_box_tab, tr("Delete Brush"), false, -1, false)) {
		ui_text(string_tmp("%s \"%s\"?", tr("Delete"), d != NULL ? brush_library_name(d) : ""), UI_ALIGN_LEFT, 0x00000000);
		ui_row2();
		if (ui_button(tr("Cancel"), UI_ALIGN_CENTER, "")) {
			ui_box_hide();
		}
		if (ui_button(tr("Delete"), UI_ALIGN_CENTER, "")) {
			brush_library_delete(d);
			tab_brushes_menu_def = NULL;
			ui_box_hide();
			tab_brushes_redraw();
		}
	}
}

static void tab_brushes_delete_group_box() {
	brush_group_t *g = tab_brushes_menu_group;
	if (ui_tab(&tab_brushes_box_tab, tr("Delete Group"), false, -1, false)) {
		ui_text(string_tmp("%s \"%s\" %s?", tr("Delete"), g != NULL ? brush_group_name(g) : "", tr("and its brushes")), UI_ALIGN_LEFT, 0x00000000);
		ui_row2();
		if (ui_button(tr("Cancel"), UI_ALIGN_CENTER, "")) {
			ui_box_hide();
		}
		if (ui_button(tr("Delete"), UI_ALIGN_CENTER, "")) {
			brush_group_delete(g);
			tab_brushes_menu_group = NULL;
			tab_brushes_group      = -1;
			ui_box_hide();
			tab_brushes_redraw();
		}
	}
}

// Group shown, the group of the picked brush unless another one was picked
static i32 tab_brushes_shown_group() {
	if (brush_group_find(tab_brushes_group) == NULL) {
		tab_brushes_group = brush_library_selected()->group;
	}
	return tab_brushes_group;
}

// Shows the brushes of group id
void tab_brushes_show_group(i32 id) {
	tab_brushes_group = id;
}

// + of the title: a brush made in the Brush Creator, or a group
static void tab_brushes_plus_menu() {
	if (ui_menu_button(tr("New Brush"), "", ICON_BRUSH)) {
		ui_brush_creator_new(tab_brushes_shown_group());
	}
	if (ui_menu_button(tr("New Group"), "", ICON_FOLDER_NEW)) {
		tab_brushes_menu_group = NULL;
		tab_brushes_name_text  = string_copy(tr("New Group"));
		ui_box_show_custom(&tab_brushes_group_box, 400, 200, NULL, true, "");
	}
}

static void tab_brushes_brush_menu() {
	brush_def_t *d = tab_brushes_menu_def;
	if (d == NULL) {
		return;
	}
	if (!d->builtin && ui_menu_button(tr("Edit"), "", ICON_EDIT)) {
		ui_brush_creator_edit(d);
	}
	if (ui_menu_button(tr("Duplicate"), "", ICON_DUPLICATE)) { // A built-in brush is copied to a group of your own, where it can be edited
		i32          group = d->builtin ? brush_library_user_group() : d->group;
		brush_def_t *c     = brush_library_duplicate(d, group);
		if (c != NULL) {
			tab_brushes_group = group;
			brush_library_use(c);
		}
	}
	if (!d->builtin && ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		ui_box_show_custom(&tab_brushes_delete_brush_box, 400, 200, NULL, true, "");
	}
}

static void tab_brushes_group_menu() {
	brush_group_t *g = tab_brushes_menu_group;
	if (g == NULL) {
		return;
	}
	if (ui_menu_button(tr("Rename"), "", ICON_EDIT)) {
		tab_brushes_name_text = string_copy(g->name);
		ui_box_show_custom(&tab_brushes_group_box, 400, 200, NULL, true, "");
	}
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		ui_box_show_custom(&tab_brushes_delete_group_box, 400, 200, NULL, true, "");
	}
}

static bool tab_brushes_group_listed(brush_group_t *g) {
	return !(g->builtin && g_config->hide_builtin_brushes);
}

static void tab_brushes_draw_rows() {
	i32 shown            = tab_brushes_shown_group();
	brush_previews_group = shown; // Its strokes render first

	// Groups on the left, the brushes of the shown group on the right
	f32 x      = g_ui->_x;
	f32 y      = g_ui->_y;
	f32 w      = g_ui->_w;
	f32 gap    = math_floor(10 * UI_SCALE());
	f32 left_w = math_floor(w * 0.36);

	g_ui->_w = left_w;
	for (i32 i = 0; i < brush_group_count(); ++i) {
		brush_group_t *g = brush_group_at(i);
		if (!tab_brushes_group_listed(g)) {
			continue;
		}
		bool       long_press = false;
		ui_state_t state      = tab_brushes_group_button(brush_group_name(g), g->icon, g->id == shown, &long_press);
		if (state == UI_STATE_RELEASED) {
			tab_brushes_group = g->id;
			tab_brushes_redraw();
		}
		if (long_press && !g->builtin) {
			tab_brushes_menu_group = g;
			ui_menu_draw(&tab_brushes_group_menu, -1, -1);
		}
	}
	f32 left_end = g_ui->_y;

	g_ui->_x = x + left_w + gap;
	g_ui->_y = y;
	g_ui->_w = w - left_w - gap;
	for (i32 i = 0; i < brush_library_count(); ++i) {
		brush_def_t *d = brush_library_at(i);
		if (d->group != shown) {
			continue;
		}
		bool       selected   = g_context->brush_selected == d->id;
		bool       long_press = false;
		ui_state_t state      = tab_brushes_row(brush_library_name(d), d->image_row, selected, &long_press);
		if (state == UI_STATE_RELEASED) {
			if (selected && !d->builtin) { // Tapping the picked brush again edits it
				ui_brush_creator_edit(d);
			}
			else {
				brush_library_use(d);
			}
		}
		if (long_press) {
			tab_brushes_menu_def = d;
			ui_menu_draw(&tab_brushes_brush_menu, -1, -1);
		}
	}
	f32 right_end = g_ui->_y;

	g_ui->_x = x;
	g_ui->_w = w;
	g_ui->_y = left_end > right_end ? left_end : right_end;
}

static void tab_brushes_draw_slots(bool popover) {
	ui_begin_sticky();
	if (popover) {
		if (ui_popovers_title(tr("Brushes"), true)) {
			ui_menu_draw(&tab_brushes_plus_menu, -1, -1);
		}
	}
	else {
		f32_array_t *row = f32_array_create_from_raw_tmp((f32[]){-64}, 1);
		ui_row(row);
		if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_brushes_plus_menu, -1, -1);
		}
	}
	ui_end_sticky();
	ui_separator(3, false);
	tab_brushes_draw_rows();
}

// Brushes tab content
void tab_brushes_draw_content() {
	tab_brushes_draw_slots(false);
}

// Touch popover, the only content so no tab above it: title with the + button
void tab_brushes_draw_popover() {
	ui_set_hovered_tab_name(tr("Brushes")); // See context_in_brushes()
	tab_brushes_draw_slots(true);
}

void tab_brushes_draw(i32 *htab) {
	if (ui_tab(htab, tr("Brushes"), false, -1, false)) {
		tab_brushes_draw_content();
	}
}
