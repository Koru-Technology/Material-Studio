
#include "../global.h"

i32 ui_toolbar_last_tool = 0;
i32 _ui_toolbar_i;
i32 _ui_toolbar_y      = 0; // Top of the view the toolbar is drawn in
i32 _ui_toolbar_bottom = 0;

void ui_toolbar_init() {}

static i32 ui_toolbar_sel_y   = -1; // Screen y of the selected item, its chevron floats next to the toolbar
static i32 ui_toolbar_sel_h   = 0;
static u32 ui_toolbar_sel_col = 0;

static i32 ui_toolbar_label_size() {
	return math_floor(g_ui->font_size * 0.65);
}

// Height of the label under each icon
static i32 ui_toolbar_label_h() {
	return draw_font_height(g_ui->ops->font, ui_toolbar_label_size()) + math_floor(2 * UI_SCALE());
}

// Space in between the items
static i32 ui_toolbar_gap() {
	return math_floor(8 * UI_SCALE());
}

// Touch: the toolbar is a panel, items are padded inside their highlight
static bool ui_toolbar_is_panel() {
	return context_is_floating_toolbar() && g_config->touch_ui;
}

static i32 ui_toolbar_side() {
	return ui_toolbar_is_panel() ? math_floor(8 * UI_SCALE()) : 0;
}

typedef enum {
	TOOL_GROUP_PAINT,   // Lay down paint
	TOOL_GROUP_SAMPLE,  // Read from the mesh
	TOOL_GROUP_RETOUCH, // Rework what is painted
	TOOL_GROUP_PROJECT, // Stamp images and text
} tool_group_t;

static tool_group_t ui_toolbar_tool_group(i32 tool) {
	switch (tool) {
	case TOOL_TYPE_PICKER:
		return TOOL_GROUP_SAMPLE;
	case TOOL_TYPE_CLONE:
	case TOOL_TYPE_BLUR:
	case TOOL_TYPE_INPAINT:
		return TOOL_GROUP_RETOUCH;
	case TOOL_TYPE_DECAL:
	case TOOL_TYPE_TEXT:
		return TOOL_GROUP_PROJECT;
	default:
		return TOOL_GROUP_PAINT;
	}
}

// Toolbar order, tools of a group stay together
static i32 ui_toolbar_order[] = {
    TOOL_TYPE_BRUSH, TOOL_TYPE_ERASER, TOOL_TYPE_FILL,    TOOL_TYPE_PARTICLE, // Paint
    TOOL_TYPE_PICKER,                                                         // Sample
    TOOL_TYPE_CLONE, TOOL_TYPE_BLUR,   TOOL_TYPE_INPAINT,                     // Retouch
    TOOL_TYPE_DECAL, TOOL_TYPE_TEXT,                                          // Project
};
#define UI_TOOLBAR_ORDER_COUNT (i32)(sizeof(ui_toolbar_order) / sizeof(ui_toolbar_order[0]))

// Tools of a group share a hue
static u32 ui_toolbar_item_color(i32 tool) {
	static const f32 hues[] = {
	    0.58f, // Paint: blue
	    0.80f, // Sample: violet
	    0.33f, // Retouch: green
	    0.50f, // Project: cyan
	};
	f32 h = hues[ui_toolbar_tool_group(tool)] * 6.0f;
	f32 s = 0.55f;
	f32 v = 0.98f;
	i32 k = (i32)h;
	f32 f = h - k;
	f32 p = v * (1 - s);
	f32 q = v * (1 - s * f);
	f32 t = v * (1 - s * (1 - f));
	f32 r, g, b;
	switch (k % 6) {
	case 0: r = v, g = t, b = p; break;
	case 1: r = q, g = v, b = p; break;
	case 2: r = p, g = v, b = t; break;
	case 3: r = p, g = q, b = v; break;
	case 4: r = t, g = p, b = v; break;
	default: r = v, g = p, b = q; break;
	}
	return 0xff000000 | ((u32)(r * 255) << 16) | ((u32)(g * 255) << 8) | (u32)(b * 255);
}

bool ui_toolbar_tool_visible(i32 tool) {
	if (tool == TOOL_TYPE_INPAINT) {
#ifdef WITH_PLUGINS
		return true;
#else
		return false;
#endif
	}
	// Hidden tools (Bake, ColorID) are not in the order
	for (i32 i = 0; i < UI_TOOLBAR_ORDER_COUNT; ++i) {
		if (ui_toolbar_order[i] == tool) {
			return true;
		}
	}
	return false;
}

static i32 ui_toolbar_item_count() {
	i32 count = 0;
	for (i32 i = 0; i < ui_toolbar_tool_names->length; ++i) {
		if (ui_toolbar_tool_visible(i)) {
			count++;
		}
	}
	return count;
}

void ui_toolbar_draw_tool_select_tool(void *_) {
	context_select_tool(_ui_toolbar_i);
}

void ui_toolbar_tool_properties_menu_draw() {
	g_ui->changed = false;
	ui_header_draw_tool_properties();
	if (g_ui->changed || g_ui->is_typing) {
		ui_menu_keep_open = true;
	}
	if (base_view3d_show && ui_button(tr("Pin to Header"), UI_ALIGN_LEFT, "")) {
		g_config->header_pinned = true;
	}
}

void ui_toolbar_tool_properties_menu() {
	i32 y = _ui_toolbar_y + g_ui->_y - 2 * UI_SCALE();
	if (!base_view3d_show) {
		y += ui_toolbar_w(false);
	}

#ifdef IRON_IOS
	if (config_is_iphone() && base_view3d_show) {
		y += ui_toolbar_w(false);
	}
#endif

	i32 x = g_ui->_window_x - ui_toolbar_x();
	ui_menu_draw(&ui_toolbar_tool_properties_menu_draw, x + g_ui->_x + g_ui->_w + 6 * UI_SCALE(), y);
}

static bool _ui_toolbar_right_click = false;

void ui_toolbar_update() {
	if (mouse_started("right")) {
		_ui_toolbar_right_click = context_is_floating_toolbar() && context_in_paint_area() && !g_config->touch_ui && !base_player && !ui_menu_show &&
		                          !ui_box_show && !g_ui->is_typing;
	}
	else if (mouse_down("right") && mouse_moved) {
		_ui_toolbar_right_click = false;
	}
	else if (mouse_released("right")) {
		if (_ui_toolbar_right_click && !mouse_moved && context_in_paint_area()) {
			ui_menu_draw(&ui_toolbar_tool_properties_menu_draw, -1, -1);
		}
		_ui_toolbar_right_click = false;
	}
}

void ui_toolbar_draw_highlight(u32 col) {
	i32 size = ui_toolbar_w(false) - 4;
	draw_set_color(col);
	if (ui_toolbar_is_panel()) {
		// Whole width of the panel, the side and gap become padding inside the highlight
		i32 side = ui_toolbar_side();
		i32 gap  = ui_toolbar_gap();
		ui_draw_rect(true, true, g_ui->_x - 2 - side, g_ui->_y + 2 - gap / 2, ui_toolbar_w(false) + side * 2, size + 2 + ui_toolbar_label_h() + gap);
		return;
	}
	ui_draw_rect(true, true, g_ui->_x + -1, g_ui->_y + 2, size + 2, size + 2 + ui_toolbar_label_h());
}

static bool ui_toolbar_input_visible() {
	if (context_is_floating_toolbar()) {
		return g_ui->input_y < _ui_toolbar_bottom;
	}
	return true;
}

static ui_state_t ui_toolbar_draw_icon(i32 tool, bool selected, bool visible, i32 tile_i, gpu_texture_t *img, i32 icon_accent, char *label) {
	i32 size = ui_toolbar_w(false) - 4;
	u32 color = ui_toolbar_item_color(tool);
	if (icon_accent != -1) {
		color = icon_accent; // Light theme
	}
	if (selected) {
		// Touch: a dark shade of the item color
		u32 dark = 0xff000000 | ((u32)(((color >> 16) & 0xff) * 0.35f) << 16) | ((u32)(((color >> 8) & 0xff) * 0.35f) << 8) | (u32)((color & 0xff) * 0.35f);
		ui_toolbar_draw_highlight(g_config->touch_ui ? dark : g_theme->HIGHLIGHT_COL);
	}
	else if (visible && ui_input_in_rect(g_ui->_window_x + g_ui->_x - 1, g_ui->_window_y + g_ui->_y + 2, size + 2, size + 2)) {
		ui_toolbar_draw_highlight(g_theme->HOVER_COL);
	}

	rect_t *rect = resource_tile50(img, tile_i);
	i32     _y   = g_ui->_y;
	g_ui->_x -= 2;
	ui_state_t image_state = ui_sub_image(img, color, -1.0, rect->x, rect->y, rect->w, rect->h);
	g_ui->_x += 2;

	// Name under the icon
	i32 label_size = ui_toolbar_label_size();
	draw_set_font(g_ui->ops->font, label_size);
	draw_set_color(color);
	i32 label_w = draw_string_width(g_ui->ops->font, label_size, label);
	draw_string(label, g_ui->_x + (size - label_w) / 2.0, g_ui->_y);
	draw_set_font(g_ui->ops->font, g_ui->font_size);
	g_ui->_y += ui_toolbar_label_h();

	// Touch: the chevron is drawn next to the toolbar, see ui_toolbar_draw_chevron
	if (selected && ui_toolbar_is_panel()) {
		i32 gap            = ui_toolbar_gap();
		ui_toolbar_sel_y   = g_ui->_window_y + _y + 2 - gap / 2;
		ui_toolbar_sel_h   = size + 2 + ui_toolbar_label_h() + gap;
		ui_toolbar_sel_col = color;
	}
	// Clicking the selected tool again opens its properties
	else if (selected && context_is_floating_toolbar()) {
		float aw = 4 * UI_SCALE();
		float ah = 8 * UI_SCALE();
		float ax = g_ui->_x + size - aw;
		float ay = _y + 4 + size - 2 * UI_SCALE() - ah;
		draw_set_color(g_theme->LABEL_COL);
		draw_filled_triangle(ax, ay, ax, ay + ah, ax + aw, ay + ah / 2.0);
	}
	return image_state;
}

static void ui_toolbar_handle_release(ui_state_t image_state, bool visible, i32 id) {
	if (image_state == UI_STATE_RELEASED && context_is_floating_toolbar() && visible) {
		if (ui_toolbar_last_tool == id) {
			ui_toolbar_tool_properties_menu();
		}
		ui_toolbar_last_tool = id;
	}
}

static u32 ui_toolbar_last_picker_mask_color = 0;

static u32 ui_toolbar_picker_mask_color() {
	if (g_context->picker_color_mask) {
		return color_set_ab(g_context->picked_color->base, 255);
	}
	if (g_context->picker_material_mask) {
		return g_theme->LABEL_COL;
	}
	return 0;
}

void ui_toolbar_draw_tool(i32 tool, gpu_texture_t *img, i32 icon_accent) {
	g_ui->_x += 2;

	bool visible = ui_toolbar_input_visible();
	i32  tile_y  = math_floor(tool / 12.0);
	i32  tile_x  = tile_y % 2 == 0 ? tool % 12 : (11 - (tool % 12));
	i32  _y      = g_ui->_y;
	i32  tile_i  = tile_y * 12 + tile_x;
	if (tool == TOOL_TYPE_INPAINT) {
		tile_i = ICON_ERASE;
	}

	char      *label       = tr(ui_toolbar_tool_labels->buffer[tool]);
	ui_state_t image_state = ui_toolbar_draw_icon(tool, g_context->tool == tool, visible, tile_i, img, icon_accent, label);

	if (image_state == UI_STATE_STARTED && visible) {
		_ui_toolbar_i = tool;
		sys_notify_on_next_frame(&ui_toolbar_draw_tool_select_tool, NULL);
	}
	ui_toolbar_handle_release(image_state, visible, tool);

	if (tool == TOOL_TYPE_PICKER && ui_toolbar_picker_mask_color() != 0) {
		draw_set_color(ui_toolbar_picker_mask_color());
		draw_filled_rect(0, _y + 2, 5 * UI_SCALE(), ui_toolbar_w(false) - 2);
		draw_set_color(0xffffffff);
	}

	if (g_ui->is_hovered) {
		char *tooltip  = tr(ui_toolbar_tool_labels->buffer[tool]);
		char *key_name = string_tmp("tool_%s", ui_toolbar_tool_names->buffer[tool]);
		for (char *c = key_name; *c != '\0'; ++c) {
			*c = tolower(*c);
		}
		char *key = any_map_get(g_keymap, key_name);
		if (key != NULL && !string_equals(key, "")) {
			tooltip = string_tmp("%s (%s)", tooltip, key);
		}
		char *extra = ui_toolbar_tooltip_extras->buffer[tool];
		if (!string_equals(extra, "")) {
			tooltip = string_tmp("%s - %s", tooltip, vtr(extra, g_keymap));
		}
		ui_tooltip(tooltip);
	}
	g_ui->_x -= 2;
	g_ui->_y += ui_toolbar_gap();
}

i32 ui_toolbar_w(bool screen_size_request) {
	if (screen_size_request && context_is_floating_toolbar()) {
		return 0;
	}
	if (!base_view3d_show && !ui_view2d_show) {
		return 0;
	}

	i32 w = ui_toolbar_default_w;
	if (g_config->touch_ui) {
		w = ui_toolbar_default_w + 6;
	}
	w = math_floor(w * g_config->window_scale);
	return w;
}

i32 ui_toolbar_x() {
	return 5 * UI_SCALE();
}

void ui_toolbar_draw_show_3d_view() {
	if (g_config->touch_ui) {
		i32 toolbar_w = ui_toolbar_default_w * UI_SCALE() + 14 * UI_SCALE();
		i32 y         = ui_header_h + 8 * UI_SCALE();

		if ((ui_view2d_show || ui_nodes_show) && !g_config->touch_ui) {
			y += toolbar_w;
		}

		if (ui_window(ui_toolbar_handle, ui_toolbar_x(), y, toolbar_w, toolbar_w, false)) {
			i32 _ELEMENT_H      = g_theme->ELEMENT_H;
			i32 _BUTTON_H       = g_theme->BUTTON_H;
			i32 _BUTTON_COL     = g_theme->BUTTON_COL;
			i32 _fontOffsetY    = g_ui->font_offset_y;
			g_theme->ELEMENT_H  = math_floor(g_theme->ELEMENT_H * 1.5);
			g_theme->BUTTON_H   = g_theme->ELEMENT_H;
			g_theme->BUTTON_COL = g_theme->WINDOW_BG_COL;
			i32 font_height     = draw_font_height(g_font, g_ui->font_size);
			g_ui->font_offset_y = (UI_ELEMENT_H() - font_height) / 2.0;
			i32 _w              = g_ui->_w;
			g_ui->_w            = toolbar_w;
			if (ui_icon_button("", ICON_CUBE, UI_ALIGN_CENTER)) {
				ui_base_show_3d_view();
			}
			g_ui->_w            = _w;
			g_theme->ELEMENT_H  = _ELEMENT_H;
			g_theme->BUTTON_H   = _BUTTON_H;
			g_theme->BUTTON_COL = _BUTTON_COL;
			g_ui->font_offset_y = _fontOffsetY;
		}
	}
}

void ui_toolbar_header_menu_draw() {
	if (ui_menu_button(tr("Toggle Header"), "", ICON_PROPERTIES)) {
		g_config->header_pinned = false;
	}
}

static void ui_toolbar_draw_window(ui_window_t *handle, i32 x, i32 y, i32 h) {
	bool panel = ui_toolbar_is_panel();
	i32  side  = ui_toolbar_side(); // Padding left and right of the buttons
	if (panel ? ui_base_panel_window(handle, x, y, ui_toolbar_w(false) + side * 2, h, UI_BASE_CORNER_TR | UI_BASE_CORNER_BR, false)
	          : ui_window(handle, x, y, ui_toolbar_w(false), h, false)) {
		g_ui->_x += side;
		g_ui->_w = ui_toolbar_w(false);
		ui_toolbar_sel_y = -1;
		if (panel) {
			g_ui->_y += ui_toolbar_gap(); // Room for the padding of the first highlight
		}
		g_ui->image_scroll_align   = false;
		gpu_texture_t *img         = resource_get("icons.k");
		u32            col         = g_theme->WINDOW_BG_COL;
		bool           light       = col > 0xff666666;
		i32            icon_accent = light ? 0xff666666 : -1;

		// Menu icon
		if (!context_is_floating_toolbar()) {
			i32 _w = g_ui->_w;
			g_ui->_x += 2;
			g_ui->_y += 2;
			g_ui->_w = ui_toolbar_w(false) - 4;
			if (ui_icon_button("", ICON_MENU, UI_ALIGN_CENTER)) {
				ui_menu_draw(&ui_toolbar_header_menu_draw, -1, -1);
			}
			g_ui->image_scroll_align = false; // Reset by ui_icon_button
			g_ui->_w                 = _w;
			g_ui->_x -= 2;
			g_ui->_y += 2;
		}

		for (i32 i = 0; i < UI_TOOLBAR_ORDER_COUNT; ++i) {
			if (ui_toolbar_tool_visible(ui_toolbar_order[i])) {
				ui_toolbar_draw_tool(ui_toolbar_order[i], img, icon_accent);
			}
		}

		g_ui->image_scroll_align = true;
		// The window is not redrawn once the mouse leaves, the hover of the last tool would stay set
		g_ui->is_hovered = false;
	}

	if (g_config->touch_ui) {
		// Hide scrollbar
		i32 _SCROLL_W     = g_theme->SCROLL_W;
		g_theme->SCROLL_W = 0;
		ui_end_window();
		g_theme->SCROLL_W = _SCROLL_W;
	}
}

// Chevron floating to the right of the selected tool, tapping it opens the tool properties
static void ui_toolbar_draw_chevron(i32 toolbar_x) {
	if (!ui_toolbar_is_panel() || ui_toolbar_sel_y < 0) {
		return;
	}
	static ui_window_t *hwnd = NULL;
	if (hwnd == NULL) {
		hwnd = ui_window_create();
	}
	static i32 last_key = 0;
	i32        key      = toolbar_x * 7919 + ui_toolbar_sel_y * 31 + ui_toolbar_sel_h + g_context->tool * 104729;
	if (key != last_key) { // Follows the selected tool
		last_key      = key;
		hwnd->redraws = 2;
	}
	i32 size      = math_floor(34 * UI_SCALE());
	i32 x         = toolbar_x + ui_toolbar_w(false) + ui_toolbar_side() * 2 + math_floor(2 * UI_SCALE());
	i32 y         = ui_toolbar_sel_y + (ui_toolbar_sel_h - size) / 2;
	u32 bg        = g_theme->WINDOW_BG_COL;
	ui_base_panel_add_rect(x, y, size, size);
	g_theme->WINDOW_BG_COL = 0x00000000;
	bool draw     = ui_window(hwnd, x, y, size, size, false);
	g_theme->WINDOW_BG_COL = bg;
	if (draw) {
		gpu_texture_t *img  = resource_get("icons.k");
		rect_t        *rect = resource_tile50(img, ICON_CHEVRON_RIGHT);
		g_ui->image_scroll_align = false;
		// Tap: tool properties, long press: the quick menu
		static f64  press_time = 0.0;
		static bool long_press = false;
		i32         state      = ui_sub_image(img, ui_toolbar_sel_col, size, rect->x, rect->y, rect->w, rect->h);
		if (state == UI_STATE_STARTED) {
			press_time = sys_time();
			long_press = false;
		}
		if (press_time > 0.0 && !long_press) {
			hwnd->redraws = 2; // Keeps checking the press
			if (state == UI_STATE_DOWN && sys_time() - press_time > 0.5) {
				long_press = true;
				ui_quick_menu_open(x + size * 5, ui_toolbar_sel_y + size);
			}
		}
		if (state == UI_STATE_RELEASED) {
			if (!long_press) {
				ui_menu_draw(&ui_toolbar_tool_properties_menu_draw, x + size, ui_toolbar_sel_y);
			}
			press_time = 0.0;
		}
		else if (!g_ui->input_down) {
			press_time = 0.0;
		}
		g_ui->image_scroll_align = true;
	}
}

void ui_toolbar_render_ui() {
	i32 _WINDOW_BG_COL = g_theme->WINDOW_BG_COL;

	if (!base_view3d_show && !ui_view2d_show && !ui_nodes_show) {
		ui_toolbar_draw_show_3d_view();
		return;
	}

	if (!base_view3d_show && ui_view2d_show && ui_view2d_type != VIEW_2D_TYPE_LAYER) {
		return;
	}

	if (!base_view3d_show && ui_nodes_show && !ui_view2d_show) {
		return;
	}

	// Toolbar is docked on the side of the 3D view, it floats in the view when the header is hidden
	i32 x              = base_view_x(0) - ui_toolbar_w(false);
	i32 y              = base_y() - ui_header_h;
	i32 h              = base_h() + ui_header_h;
	_ui_toolbar_y      = base_y();
	_ui_toolbar_bottom = base_y() + base_h();

	if (context_is_floating_toolbar()) {
		// 2D view takes the toolbar when there is no 3D view
		if (!base_view3d_show) {
			x                  = ui_view2d_wx + (g_config->touch_ui ? 0 : ui_toolbar_x()); // Touch: flush with the screen edge
			_ui_toolbar_y      = ui_view2d_wy;
			_ui_toolbar_bottom = ui_view2d_wy + ui_view2d_wh;
		}
		else {
			x = base_view_x(0) + (g_config->touch_ui ? 0 : ui_toolbar_x()); // Touch: flush with the screen edge
		}
		y                      = _ui_toolbar_y + ui_header_h + ui_toolbar_x() + 3 * UI_SCALE();
		h                      = ui_toolbar_item_count() * (ui_toolbar_w(false) + ui_toolbar_label_h() + ui_toolbar_gap());
		if (!g_config->touch_ui) {
			g_theme->WINDOW_BG_COL = g_theme->SEPARATOR_COL;
		}
		else {
			h += ui_toolbar_gap(); // Top padding
		}
		if (g_config->touch_ui && (base_view3d_show || ui_view2d_show)) {
			// Centered on the side of the screen, kept clear of the top bar and its shadow
			i32 top = _ui_toolbar_y + ui_header_h + ui_menubar_top_pad() + ui_menubar_side_pad() + ui_base_panel_shadow() + ui_toolbar_gap();
			i32 bottom = _ui_toolbar_bottom - ui_panels_history_h(); // Above the undo / redo panel
			y          = math_max(top, (_ui_toolbar_y + bottom - h) / 2);
		}
		if (!base_view3d_show && ui_view2d_show && !g_config->touch_ui) {
			y += ui_toolbar_w(false);
		}

#ifdef IRON_IOS
		if (config_is_iphone()) {
			y += ui_toolbar_w(false);
		}
#endif
	}

	u32 picker_mask_color = ui_toolbar_picker_mask_color();
	if (picker_mask_color != ui_toolbar_last_picker_mask_color) {
		ui_toolbar_last_picker_mask_color = picker_mask_color;
		ui_toolbar_handle->redraws        = 1;
	}

	// One toolbar per 3D view
	i32 views = base_is_split() ? 2 : 1;
	ui_window_sync_redraws(ui_toolbar_handle, ui_toolbar_split_handle, views == 2);
	for (i32 i = 0; i < views; ++i) {
		ui_toolbar_draw_window(i == 0 ? ui_toolbar_handle : ui_toolbar_split_handle, x + base_view_x(i) - base_view_x(0), y, h);
	}
	ui_toolbar_draw_chevron(x);

	if (context_is_floating_toolbar()) {
		g_theme->WINDOW_BG_COL = _WINDOW_BG_COL;
	}
}
