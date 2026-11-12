#include "../global.h"

// Touch: no sidebar, a floating bar at the top right opens its tabs as popovers.
// Layers (with Meshes), Brushes, and the current material's sphere (Materials, Library, Smart).
// A popover stays open while it is used and closes on a touch outside of it, that touch still paints or turns the view.

typedef enum {
	POPOVER_NONE      = 0,
	POPOVER_LAYERS    = 1,
	POPOVER_BRUSHES   = 2,
	POPOVER_MATERIALS = 3,
} popover_t;

static ui_window_t *ui_popovers_bar_handle = NULL;
static ui_window_t *ui_popovers_handle     = NULL;
static ui_window_t *ui_popovers_back_handle = NULL; // Rounded background and shadow, the tabs are drawn inset over it
static popover_t    ui_popovers_open       = POPOVER_NONE;
static i32          ui_popovers_tab[4]     = {0, 0, 0, 0}; // Picked tab of each popover
static i32          ui_popovers_bar_w      = 0;            // Icons width measured last frame
static i32          ui_popovers_rect[4]    = {0, 0, 0, 0}; // Bar and popover, last frame
static i32          ui_popovers_pop_rect[4] = {0, 0, 0, 0};
static i32          ui_popovers_content_h[4] = {0, 0, 0, 0}; // Height each popover's content took last frame, it fits it

bool ui_popovers_enabled() {
	return g_config->touch_ui;
}

// Sidebar tabs the popovers hold, the dock leaves them out
bool ui_popovers_has_tab(i32 id) {
	return id == TAB_LAYERS || id == TAB_DEBUG || id == TAB_MATERIALS || id == TAB_LIBRARY ||
	       id == TAB_BRUSHES || id == TAB_SMART;
}

// The views shown: 3D, 2D alone, or both side by side
static bool ui_popovers_view_shown() {
	return base_view3d_show || ui_view2d_show;
}

// Top of the area the bar sits in: the 3D view's top, or below the 2D view's header when it is shown alone
static i32 ui_popovers_top() {
	return base_view3d_show ? base_y() : ui_view2d_wy + ui_view2d_top();
}

static i32 ui_popovers_margin() {
	return math_floor(12 * UI_SCALE());
}

// Same padding as the top left bar in the 3D view, whichever views are shown
static i32 ui_popovers_top_pad() {
	return math_floor(10 * UI_SCALE());
}

static i32 ui_popovers_bar_h() {
	return ui_header_h + ui_popovers_top_pad() + math_floor(7 * UI_SCALE());
}

// Lowest screen y of the bar, the view cube goes below it
i32 ui_popovers_bar_bottom() {
	if (!ui_popovers_enabled() || !ui_popovers_view_shown()) {
		return 0;
	}
	return ui_popovers_top() + ui_popovers_bar_h();
}

void ui_popovers_redraw(i32 area) {
	if (ui_popovers_handle == NULL) {
		return;
	}
	if (area == -1 || area == TAB_AREA_SIDEBAR0 || area == TAB_AREA_SIDEBAR1) {
		ui_popovers_handle->redraws      = 2;
		ui_popovers_back_handle->redraws = 2;
		ui_popovers_bar_handle->redraws = 2;
	}
}

i32 ui_popovers_current() {
	return ui_popovers_open;
}

// Opens popover p (1 layers, 2 brushes, 3 materials), 0 closes
void ui_popovers_show(i32 p) {
	ui_popovers_open = p >= POPOVER_NONE && p <= POPOVER_MATERIALS ? (popover_t)p : POPOVER_NONE;
	ui_popovers_redraw(-1);
	base_redraw_ui();
}

void ui_popovers_close() {
	if (ui_popovers_open != POPOVER_NONE) {
		ui_popovers_open = POPOVER_NONE;
		if (ui_popovers_bar_handle != NULL) {
			ui_popovers_bar_handle->redraws = 2;
		}
		base_redraw_ui();
	}
}

static void ui_popovers_toggle(popover_t p) {
	ui_popovers_open = ui_popovers_open == p ? POPOVER_NONE : p;
	ui_popovers_redraw(-1);
}

static bool ui_popovers_in(i32 *r, f32 x, f32 y) {
	return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
}

// A touch that starts outside the bar and the popover closes it, and goes on to do what it does (paint, orbit, ...)
void ui_popovers_update() {
	if (!ui_popovers_enabled() || ui_popovers_open == POPOVER_NONE) {
		return;
	}
	if (!(mouse_started("left") || pen_started("tip"))) {
		return;
	}
	// Menus, combos and boxes opened from the popover are drawn outside of it
	if (ui_menu_show || ui_box_show || g_ui->combo_selected_id != 0 || base_is_dragging) {
		return;
	}
	f32 x = mouse_x;
	f32 y = mouse_y;
	if (ui_popovers_in(ui_popovers_rect, x, y) || ui_popovers_in(ui_popovers_pop_rect, x, y)) {
		return;
	}
	ui_popovers_close();
}

// Where an icon of the bar is drawn, as in the top left bar: inset by the button offset and half a scrollbar
static void ui_popovers_icon_rect(i32 *x, i32 *size) {
	f32 iw = 50 * UI_SCALE();
	*size  = math_floor(fmin(iw, g_ui->_w - g_ui->button_offset_y * 2.0) - UI_SCROLL_W());
	*x     = math_floor(g_ui->_x + g_ui->button_offset_y + UI_SCROLL_W() / 2.0);
}

// Rounded highlight around the icon of the open popover's button, window coordinates
static void ui_popovers_highlight(popover_t p) {
	if (ui_popovers_open != p) {
		return;
	}
	i32 x, size;
	ui_popovers_icon_rect(&x, &size);
	i32 pad = math_floor(4 * UI_SCALE());
	draw_set_color(g_theme->HIGHLIGHT_COL);
	ui_base_fill_round(x - pad, g_ui->_y - pad, size + pad * 2, size + pad * 2, 6 * UI_SCALE());
	draw_set_color(0xffffffff);
}

static bool ui_popovers_icon(popover_t p, i32 icon) {
	ui_popovers_highlight(p);
	return ui_menubar_icon_button(icon);
}

static void ui_popovers_draw_bar() {
	i32 item_w = ui_toolbar_w(false);
	i32 xpad   = math_floor(3 * UI_SCALE());
	i32 w      = (ui_popovers_bar_w > 0 ? ui_popovers_bar_w : item_w * 3) + xpad * 2;
	i32 h      = ui_popovers_bar_h();
	i32 x      = iron_window_width() - w; // Flush with the screen edge, whichever views are shown
	i32 y      = ui_popovers_top();
	ui_popovers_rect[0] = x;
	ui_popovers_rect[1] = y;
	ui_popovers_rect[2] = w;
	ui_popovers_rect[3] = h;

	if (ui_base_panel_window(ui_popovers_bar_handle, x, y, w, h, UI_BASE_CORNER_BL, true)) {
		g_ui->_y += ui_popovers_top_pad(); // Same placement as the top left bar
		g_ui->_x += xpad;
		i32 start = g_ui->_x;
		// The menu fill would cover the rounded panel
		u32 sep                = g_theme->SEPARATOR_COL;
		g_theme->SEPARATOR_COL = 0x00000000;
		ui_begin_menu();
		g_theme->SEPARATOR_COL = sep;
		g_ui->_w               = item_w;

		i32 cell_x = g_ui->_x;
		if (ui_popovers_icon(POPOVER_LAYERS, ICON_LAYERS)) {
			ui_popovers_toggle(POPOVER_LAYERS);
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Layers"));
		}
		i32 step = g_ui->_x - cell_x; // Advance of one button

		if (ui_popovers_icon(POPOVER_BRUSHES, ICON_PAINT)) {
			ui_popovers_toggle(POPOVER_BRUSHES);
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Brushes"));
		}

		// The current material, its preview sphere centered in the cell
		gpu_texture_t *sphere = g_context->material != NULL ? g_context->material->image_icon : NULL;
		bool           tapped = false;
		if (sphere != NULL) {
			// Same size and place as the icons, a little smaller as the icon glyphs leave a margin
			i32 cx = g_ui->_x;
			i32 cy = g_ui->_y;
			ui_popovers_highlight(POPOVER_MATERIALS);
			i32 ix, size;
			ui_popovers_icon_rect(&ix, &size);
			i32 inset                = math_floor(2 * UI_SCALE());
			i32 _w                   = g_ui->_w;
			g_ui->_x                 = ix + inset;
			g_ui->_y                 = cy + inset;
			g_ui->_w                 = size - inset * 2;
			g_ui->image_scroll_align = false;
			tapped                   = ui_image(sphere, 0xffffffff, size - inset * 2) == UI_STATE_RELEASED;
			g_ui->image_scroll_align = true;
			g_ui->_x                 = cx + step;
			g_ui->_y                 = cy;
			g_ui->_w                 = _w;
		}
		else {
			tapped = ui_popovers_icon(POPOVER_MATERIALS, ICON_SPHERE);
		}
		if (tapped) {
			ui_popovers_toggle(POPOVER_MATERIALS);
		}
		if (g_ui->is_hovered && g_context->material != NULL) {
			ui_tooltip(g_context->material->canvas->name);
		}

		if (g_ui->_x - start != ui_popovers_bar_w) {
			ui_popovers_bar_w                = g_ui->_x - start;
			ui_popovers_bar_handle->redraws = 2;
		}
		ui_end_menu();
	}
	ui_end_window();
}

static void ui_popovers_draw_popover() {
	if (ui_popovers_open == POPOVER_NONE) {
		ui_popovers_pop_rect[2] = 0;
		return;
	}
	i32 margin = ui_popovers_margin();
	i32 w      = math_floor(ui_base_sidebar_full_w(g_config->large_asset_previews) * g_config->window_scale);
	i32 x      = iron_window_width() - w - margin;
	i32 y      = ui_popovers_top() + ui_popovers_bar_h() + margin / 2;
	// As tall as its content (the layers popover grows with the layers), at most half of the screen
	i32 max_h = math_floor((iron_window_height() - ui_popovers_top()) * 0.5);
	i32 min_h = math_floor(UI_ELEMENT_H() * 4);
	i32 h     = ui_popovers_content_h[ui_popovers_open] > 0 ? ui_popovers_content_h[ui_popovers_open] : max_h;
	h         = h < min_h ? min_h : h > max_h ? max_h : h;
	if (w > iron_window_width() - margin * 2) {
		w = iron_window_width() - margin * 2;
		x = margin;
	}
	ui_popovers_pop_rect[0] = x;
	ui_popovers_pop_rect[1] = y;
	ui_popovers_pop_rect[2] = w;
	ui_popovers_pop_rect[3] = h;

	i32 *htab = &ui_popovers_tab[ui_popovers_open];

	// Background: rounded on all corners with the shadow around it
	if (ui_base_panel_window(ui_popovers_back_handle, x, y, w, h, UI_BASE_CORNER_TL | UI_BASE_CORNER_TR | UI_BASE_CORNER_BL | UI_BASE_CORNER_BR, true)) {
	}
	ui_end_window();

	// Content inset so its square edges (tab strip, rows) stay inside the rounded corners
	i32 inset = math_floor(4 * UI_SCALE());
	if (ui_window(ui_popovers_handle, x + inset, y + inset, w - inset * 2, h - inset * 2, false)) {
		if (ui_popovers_open == POPOVER_LAYERS) {
			tab_layers_draw_popover();
		}
		else if (ui_popovers_open == POPOVER_BRUSHES) {
			tab_brushes_draw_popover();
		}
		else {
			tab_materials_draw(htab);
			tab_library_draw(htab);
			tab_smart_materials_draw(htab);
		}
		i32 content = math_floor(g_ui->_y - ui_popovers_handle->scroll_offset + inset * 2 + 4 * UI_SCALE());
		i32 *last   = &ui_popovers_content_h[ui_popovers_open];
		if (content < *last - 1 || content > *last + 1) {
			*last                            = content;
			ui_popovers_handle->redraws      = 2;
			ui_popovers_back_handle->redraws = 2;
		}
	}
	// Tabs are picked and the scrollbar is drawn when the window ends
	ui_end_window();
}

void ui_popovers_render_ui() {
	if (!ui_popovers_enabled() || !ui_popovers_view_shown() || base_player || g_context->capturing_screenshot) {
		ui_popovers_pop_rect[2] = 0;
		ui_popovers_rect[2]     = 0;
		return;
	}
	if (ui_popovers_bar_handle == NULL) {
		ui_popovers_bar_handle         = ui_window_create();
		ui_popovers_bar_handle->layout = UI_LAYOUT_HORIZONTAL;
		ui_popovers_handle             = ui_window_create();
		ui_popovers_back_handle        = ui_window_create();
	}
	ui_popovers_draw_popover();
	ui_popovers_draw_bar();
}
