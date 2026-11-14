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

// Opening fades and grows the popover in from the bar, closing fades and shrinks it out
#define UI_POPOVERS_ANIM_TIME 0.18
static popover_t ui_popovers_drawn      = POPOVER_NONE; // Popover drawn, the closing one while it shrinks out
static f64       ui_popovers_anim_start = 0.0;
static bool      ui_popovers_closing    = false;

static void ui_popovers_set_open(popover_t p) {
	popover_t old    = ui_popovers_open;
	ui_popovers_open = p;
	if (p == old) {
		return;
	}
	ui_popovers_anim_start = iron_time();
	if (p != POPOVER_NONE) {
		ui_popovers_drawn   = p;
		ui_popovers_closing = false;
	}
	else {
		ui_popovers_drawn   = old;
		ui_popovers_closing = true;
	}
}

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

// Popover title: large and left aligned, with an optional + button at the right end of its row. True when + is tapped.
bool ui_popovers_title(char *title, bool plus) {
	draw_font_t *font = g_ui->ops->font;
	i32          size = g_ui->font_size * 4;
	f32          th   = draw_font_height(font, size);
	f32          h    = math_floor(th + 8 * UI_SCALE());
	f32          x    = g_ui->_x;
	f32          y    = g_ui->_y;
	f32          w    = g_ui->_w;
	if (ui_is_visible(h)) {
		draw_set_color(g_theme->TEXT_COL);
		draw_set_font(font, size);
		draw_string(title, x + 6 * UI_SCALE(), y + (h - th) / 2.0);
		draw_set_font(font, g_ui->font_size);
	}
	bool pressed = false;
	if (plus) {
		f32 bw   = math_floor(UI_ELEMENT_H() * 1.4);
		g_ui->_x = x + w - bw;
		g_ui->_y = y + (h - UI_ELEMENT_H()) / 2.0;
		g_ui->_w = bw;
		pressed  = ui_icon_button("", ICON_PLUS, UI_ALIGN_CENTER);
		g_ui->_x = x;
		g_ui->_w = w;
	}
	g_ui->_y = y;
	ui_end_element_of_size(h);
	return pressed;
}

// Rectangle of the open popover, 0 x, 1 y, 2 w, 3 h (w is 0 when none is shown), for scripts
i32 ui_popovers_rect_get(i32 i) {
	return i >= 0 && i < 4 ? ui_popovers_pop_rect[i] : 0;
}

// A menu (context menu, combo list) is open, for scripts
bool ui_menu_shown() {
	return ui_menu_show;
}

// Opens popover p (1 layers, 2 brushes, 3 materials), 0 closes
void ui_popovers_show(i32 p) {
	ui_popovers_set_open(p >= POPOVER_NONE && p <= POPOVER_MATERIALS ? (popover_t)p : POPOVER_NONE);
	ui_popovers_redraw(-1);
	base_redraw_ui();
}

// Opens the materials popover on its tab i: 0 materials, 1 library, 2 smart materials
void ui_popovers_show_materials_tab(i32 i) {
	ui_popovers_tab[POPOVER_MATERIALS] = i;
	ui_popovers_show(POPOVER_MATERIALS);
}

void ui_popovers_close() {
	if (ui_popovers_open != POPOVER_NONE) {
		ui_popovers_set_open(POPOVER_NONE);
		if (ui_popovers_bar_handle != NULL) {
			ui_popovers_bar_handle->redraws = 2;
		}
		base_redraw_ui();
	}
}

static void ui_popovers_toggle(popover_t p) {
	ui_popovers_set_open(ui_popovers_open == p ? POPOVER_NONE : p);
	ui_popovers_redraw(-1);
}

static bool ui_popovers_in(i32 *r, f32 x, f32 y) {
	return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
}

// A touch that starts outside the bar and the popover closes it, and goes on to do what it does (paint, orbit, ...)
void ui_popovers_update() {
	if (!ui_popovers_enabled() || ui_popovers_open == POPOVER_NONE || ui_brush_creator_shown() || material_studio_show) {
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

// The icon of the open popover's button turns the highlight color
static bool ui_popovers_icon(popover_t p, i32 icon) {
	return ui_menubar_icon_button_select(icon, ui_popovers_open == p);
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
			i32 ix, size;
			ui_popovers_icon_rect(&ix, &size);
			i32       inset          = math_floor(2 * UI_SCALE());
			i32       _w             = g_ui->_w;
			uintptr_t key            = ui_bounce_here(-1);
			f32       bounce         = ui_bounce_scale(key);
			g_ui->_x                 = ix + inset;
			g_ui->_y                 = cy + inset;
			g_ui->_w                 = size - inset * 2;
			g_ui->image_scroll_align = false;
			ui_image_draw_scale      = bounce;
			ui_state_t state         = ui_image(sphere, 0xffffffff, size - inset * 2);
			ui_image_draw_scale      = 1.0;
			g_ui->image_scroll_align = true;
			tapped                   = state == UI_STATE_RELEASED;
			if (state == UI_STATE_STARTED) {
				ui_bounce_start(key);
			}
			if (ui_popovers_open == POPOVER_MATERIALS) { // A ring in the highlight color, the sphere keeps its colors
				f32 r = (size / 2.0 + inset) * bounce;
				draw_set_color(g_theme->HIGHLIGHT_COL);
				draw_circle(ix + size / 2.0, cy + size / 2.0, r, 48, 2.5 * UI_SCALE());
				draw_set_color(0xffffffff);
			}
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

// Frosted glass: the 3D view under the popover, halved down a few times and scaled back up, smooth filtering blurs it

#define UI_POPOVERS_BLUR_LEVELS 6
#define UI_POPOVERS_BLUR_SHOWN  2 // Level drawn, an eighth of the size: scaled up it is soft
static gpu_texture_t *ui_popovers_blur[UI_POPOVERS_BLUR_LEVELS];
static i32            ui_popovers_blur_count = -1; // Image of the view the blur was made from

static gpu_texture_t *ui_popovers_blur_level(i32 i, i32 w, i32 h) {
	w = w < 1 ? 1 : w;
	h = h < 1 ? 1 : h;
	if (ui_popovers_blur[i] == NULL || ui_popovers_blur[i]->width != w || ui_popovers_blur[i]->height != h) {
		if (ui_popovers_blur[i] != NULL) {
			gpu_delete_texture(ui_popovers_blur[i]);
		}
		ui_popovers_blur[i]    = gpu_create_render_target(w, h, GPU_TEXTURE_FORMAT_RGBA32);
		ui_popovers_blur_count = -1;
	}
	return ui_popovers_blur[i];
}

// Blurred view under the rectangle, NULL when the 3D view is not behind it. *changed tells whether it was made again.
gpu_texture_t *ui_popovers_backdrop(i32 x, i32 y, i32 w, i32 h, bool *changed) {
	*changed = false;
	i32            count;
	gpu_texture_t *src = render_path_base_presented_image(&count);
	if (src == NULL || !base_view3d_show || sys_w() <= 0 || sys_h() <= 0) {
		return NULL;
	}
	gpu_texture_t *levels[UI_POPOVERS_BLUR_LEVELS];
	for (i32 i = 0; i < UI_POPOVERS_BLUR_LEVELS; ++i) {
		levels[i] = ui_popovers_blur_level(i, w >> (i + 1), h >> (i + 1));
	}
	if (count == ui_popovers_blur_count) {
		return levels[UI_POPOVERS_BLUR_SHOWN];
	}
	ui_popovers_blur_count = count;
	*changed               = true;

	f32 kx = src->width / (f32)sys_w();
	f32 ky = src->height / (f32)sys_h();

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}
	draw_begin(levels[0], true, 0xff000000);
	draw_set_color(0xffffffff);
	draw_scaled_sub_image(src, (x - sys_x()) * kx, (y - sys_y()) * ky, w * kx, h * ky, 0, 0, levels[0]->width, levels[0]->height);
	draw_end();
	for (i32 i = 1; i < UI_POPOVERS_BLUR_LEVELS; ++i) { // Down
		draw_begin(levels[i], true, 0xff000000);
		draw_set_color(0xffffffff);
		draw_scaled_image(levels[i - 1], 0, 0, levels[i]->width, levels[i]->height);
		draw_end();
	}
	for (i32 i = UI_POPOVERS_BLUR_LEVELS - 2; i >= UI_POPOVERS_BLUR_SHOWN; --i) { // Back up
		draw_begin(levels[i], true, 0xff000000);
		draw_set_color(0xffffffff);
		draw_scaled_image(levels[i + 1], 0, 0, levels[i]->width, levels[i]->height);
		draw_end();
	}
	if (in_use) {
		draw_begin(current, false, 0);
	}
	return levels[UI_POPOVERS_BLUR_SHOWN];
}

static void ui_popovers_draw_popover() {
	// Animation: 0 hidden, 1 shown
	f64 t = (iron_time() - ui_popovers_anim_start) / UI_POPOVERS_ANIM_TIME;
	t     = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
	if (ui_popovers_closing && t >= 1.0) {
		ui_popovers_closing = false;
		ui_popovers_drawn   = POPOVER_NONE;
	}
	if (ui_popovers_drawn == POPOVER_NONE) {
		ui_popovers_pop_rect[2] = 0;
		return;
	}
	f32 k = ui_popovers_closing ? 1.0 - t * t : 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t); // Ease out in, ease in out
	if (t < 1.0) {
		base_redraw_ui(); // Every frame until it is done
		iron_delay_idle_sleep();
	}
	popover_t shown = ui_popovers_drawn;
	i32 margin = ui_popovers_margin();
	i32 w      = math_floor(ui_base_sidebar_full_w(g_config->large_asset_previews) * g_config->window_scale);
	if (shown == POPOVER_BRUSHES) {
		w = math_floor(w * 1.5); // Groups beside the brushes
	}
	i32 x      = iron_window_width() - w - margin;
	i32 y      = ui_popovers_top() + ui_popovers_bar_h() + margin / 2;
	// As tall as its content (the layers popover grows with the layers), at most half of the screen
	i32 max_h = math_floor((iron_window_height() - ui_popovers_top()) * 0.5);
	i32 min_h = math_floor(UI_ELEMENT_H() * 4);
	i32 h     = ui_popovers_content_h[shown] > 0 ? ui_popovers_content_h[shown] : max_h;
	h         = h < min_h ? min_h : h > max_h ? max_h : h;
	if (w > iron_window_width() - margin * 2) {
		w = iron_window_width() - margin * 2;
		x = margin;
	}
	ui_popovers_pop_rect[0] = x;
	ui_popovers_pop_rect[1] = y;
	ui_popovers_pop_rect[2] = ui_popovers_closing ? 0 : w; // A closing popover takes no input
	ui_popovers_pop_rect[3] = h;

	i32 *htab = &ui_popovers_tab[shown];

	bool input = g_ui->input_enabled;
	if (ui_popovers_closing) {
		g_ui->input_enabled = false;
	}

	// Background: rounded on all corners with the shadow around it, frosted glass over the 3D view
	bool backdrop_changed  = false;
	ui_base_panel_backdrop = ui_popovers_backdrop(x, y, w, h, &backdrop_changed);
	if (backdrop_changed) {
		ui_popovers_back_handle->redraws = 2;
	}
	if (ui_base_panel_window(ui_popovers_back_handle, x, y, w, h, UI_BASE_CORNER_TL | UI_BASE_CORNER_TR | UI_BASE_CORNER_BL | UI_BASE_CORNER_BR, true)) {
	}
	ui_base_panel_backdrop = NULL;
	// Faded and grown from the corner under the bar. Set once the popover's window has begun: beginning it ends the window
	// drawn before, which is composited then and must not fade.
	ui_window_composite_alpha = k;
	ui_window_composite_scale = 0.9 + 0.1 * k;
	ui_window_composite_ox    = x + w;
	ui_window_composite_oy    = y;
	ui_end_window();

	// Content inset so its square edges (tab strip, rows) stay inside the rounded corners
	i32 inset = math_floor(4 * UI_SCALE());
	u32 window_bg          = g_theme->WINDOW_BG_COL;
	g_theme->WINDOW_BG_COL = 0x00000000; // The panel shows through
	bool content           = ui_window(ui_popovers_handle, x + inset, y + inset, w - inset * 2, h - inset * 2, false);
	g_theme->WINDOW_BG_COL = window_bg;
	if (content) {
		if (shown == POPOVER_LAYERS) {
			tab_layers_draw_popover();
		}
		else if (shown == POPOVER_BRUSHES) {
			tab_brushes_draw_popover();
		}
		else {
			tab_materials_draw(htab);
			tab_library_draw(htab);
			tab_smart_materials_draw(htab);
		}
		i32 content = math_floor(g_ui->_y - ui_popovers_handle->scroll_offset + inset * 2 + 4 * UI_SCALE());
		i32 *last   = &ui_popovers_content_h[shown];
		if (content < *last - 1 || content > *last + 1) {
			*last                            = content;
			ui_popovers_handle->redraws      = 2;
			ui_popovers_back_handle->redraws = 2;
		}
	}
	// Tabs are picked and the scrollbar is drawn when the window ends
	ui_end_window();

	ui_window_composite_alpha = 1.0;
	ui_window_composite_scale = 1.0;
	g_ui->input_enabled       = input;
}

void ui_popovers_render_ui() {
	// Under the Brush Creator nothing shows, the popover is back as it was once the Creator closes
	if (!ui_popovers_enabled() || !ui_popovers_view_shown() || base_player || g_context->capturing_screenshot || ui_brush_creator_shown()) {
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
