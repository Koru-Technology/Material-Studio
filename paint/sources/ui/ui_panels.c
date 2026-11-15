#include "../global.h"

// Touch: floating panels in the 3D view, undo / redo at the bottom left and the brush radius / opacity at the bottom center

static ui_window_t *ui_panels_history_handle = NULL;
static ui_window_t *ui_panels_brush_handle   = NULL;

static bool ui_panels_show() {
	return g_config->touch_ui && (base_view3d_show || ui_view2d_show) && !g_config->header_pinned;
}

// The view the bottom panels sit in: the 3D view, or the 2D view when it is shown alone
static void ui_panels_view(i32 *x, i32 *y, i32 *w, i32 *h) {
	if (base_view3d_show) {
		*x = base_view_x(0);
		*y = base_y();
		*w = base_view_w();
		*h = base_h();
	}
	else {
		*x = ui_view2d_wx;
		*y = ui_view2d_wy;
		*w = ui_view2d_ww;
		*h = ui_view2d_wh;
	}
}

static i32 ui_panels_margin() {
	return math_floor(12 * UI_SCALE());
}

static i32 ui_panels_pad() {
	return math_floor(8 * UI_SCALE());
}

// Content height of the bottom panels: a slider row, the undo / redo icons are as tall
static i32 ui_panels_item_h() {
	return UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
}

// Width of the undo / redo panel
static i32 ui_panels_history_w() {
	return ui_panels_item_h() * 2 + ui_panels_pad() * 3;
}

// Space taken at the bottom of the 3D view by the undo / redo panel, the toolbar stays above it
i32 ui_panels_history_h() {
	if (!ui_panels_show()) {
		return 0;
	}
	return ui_panels_item_h() + ui_panels_pad() * 2 + ui_panels_margin();
}

// Always shown, grayed out when not available (the default disabled fade is too faint on the panel)
static bool ui_panels_icon_button(i32 icon, bool available) {
	gpu_texture_t *img  = resource_get("icons.k");
	rect_t        *rect = resource_tile50(img, icon);
	u32            tint = available ? 0xffdddddd : 0xff555555;
	return ui_sub_image(img, tint, -1.0, rect->x, rect->y, rect->w, rect->h) == UI_STATE_RELEASED && available;
}

static void ui_panels_draw_history() {
	if (ui_panels_history_handle == NULL) {
		ui_panels_history_handle         = ui_window_create();
		ui_panels_history_handle->layout = UI_LAYOUT_HORIZONTAL;
	}
	static i32 last_undos = -1;
	static i32 last_redos = -1;
	if (history_undos != last_undos || history_redos != last_redos) { // Follows the history
		last_undos                        = history_undos;
		last_redos                        = history_redos;
		ui_panels_history_handle->redraws = 2;
	}

	i32 item_w = ui_panels_item_h(); // Same height as the brush sliders
	i32 pad    = ui_panels_pad();
	i32 w      = ui_panels_history_w();
	i32 h      = item_w + pad * 2;
	i32 vx, vy, vw, vh;
	ui_panels_view(&vx, &vy, &vw, &vh);
	i32 x      = vx + ui_panels_margin();
	i32 y      = vy + vh - h - ui_panels_margin();
	if (ui_base_panel_window(ui_panels_history_handle, x, y, w, h, UI_BASE_CORNER_TL | UI_BASE_CORNER_TR | UI_BASE_CORNER_BR | UI_BASE_CORNER_BL, false)) {
		g_ui->_x += pad;
		g_ui->_y += pad;
		g_ui->_w                 = item_w;
		g_ui->image_scroll_align = false;
		if (ui_panels_icon_button(ICON_UNDO, history_undos > 0)) {
			history_undo();
		}
		g_ui->_x += pad;
		if (ui_panels_icon_button(ICON_REDO, history_redos > 0)) {
			history_redo();
		}
		g_ui->image_scroll_align = true;
	}
}

static bool ui_panels_tool_has_opacity() {
	i32 t = g_context->tool;
	return t == TOOL_TYPE_BRUSH || t == TOOL_TYPE_ERASER || t == TOOL_TYPE_FILL || t == TOOL_TYPE_DECAL || t == TOOL_TYPE_TEXT || t == TOOL_TYPE_CLONE ||
	       t == TOOL_TYPE_BLUR || t == TOOL_TYPE_PARTICLE;
}

// Radius sliders of the brush panel (0) and the header (1): while one is dragged the view shows the brush circle
static ui_id_t ui_panels_radius_ids[2] = {0, 0};

// Called right before drawing a radius slider, in its window
void ui_panels_radius_slider_id(i32 which, f32 *radius) {
	ui_panels_radius_ids[which] = ui_widget_id(radius, UI_ID_SLIDER);
}

bool ui_panels_radius_dragging() {
	ui_id_t id = g_ui->slider_id;
	return id != 0 && (id == ui_panels_radius_ids[0] || id == ui_panels_radius_ids[1]);
}

// The view draws the circle while a radius slider is dragged, and once more to clear it on release
static void ui_panels_radius_redraw() {
	static bool was = false;
	bool        now = ui_panels_radius_dragging();
	if (now || was) {
		render_path_base_redraw();
	}
	was = now;
}

static void ui_panels_draw_brush() {
	ui_panels_radius_redraw();
	if (!ui_panels_tool_has_opacity()) {
		return;
	}
	if (ui_panels_brush_handle == NULL) {
		ui_panels_brush_handle = ui_window_create();
	}

	bool has_radius = g_context->tool != TOOL_TYPE_FILL;
	bool has_flow   = (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE) && !context_is_decal_mask();
	i32  pad        = ui_panels_pad();
	i32  slider_w   = math_floor(240 * UI_SCALE());
	i32  sliders    = (has_radius ? 2 : 1) + (has_flow ? 1 : 0);
	i32  w          = slider_w * sliders + pad * 2;
	i32 vx, vy, vw, vh;
	ui_panels_view(&vx, &vy, &vw, &vh);
	i32  max_w      = vw - (ui_panels_history_w() + ui_panels_margin() * 2) * 2; // Centered and clear of the undo / redo panel
	if (w > max_w) {
		w = math_max(max_w, math_floor(120 * UI_SCALE()));
	}
	i32 h = ui_panels_item_h() + pad * 2;
	i32 x = vx + (vw - w) / 2;
	i32 y = vy + vh - h - ui_panels_margin();

	// Redraw when the values change elsewhere (header, shortcuts)
	static f32  last_radius  = -1;
	static f32  last_opacity = -1;
	static f32  last_flow    = -1;
	static i32  last_tool    = -1;
	bool        decal_mask   = context_is_decal_mask();
	f32        *radius       = decal_mask ? &g_context->brush_decal_mask_radius : &g_context->brush_radius;
	if (*radius != last_radius || g_context->brush_opacity != last_opacity || g_context->brush_flow != last_flow || g_context->tool != last_tool) {
		ui_panels_brush_handle->redraws = 2;
	}

	if (ui_base_panel_window(ui_panels_brush_handle, x, y, w, h, UI_BASE_CORNER_TL | UI_BASE_CORNER_TR | UI_BASE_CORNER_BR | UI_BASE_CORNER_BL, false)) {
		g_ui->_x += pad;
		g_ui->_y += pad;
		g_ui->_w = w - pad * 2;
		bool changed = false;
		if (sliders == 3) {
			ui_row3();
		}
		else if (sliders == 2) {
			ui_row2();
		}
		if (has_radius) {
			ui_panels_radius_slider_id(0, radius);
			ui_slider(radius, tr("Radius"), 0.01, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
			changed |= ui_item_changed();
		}
		ui_slider(&g_context->brush_opacity, tr("Opacity"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		changed |= ui_item_changed();
		g_context->brush_opacity = math_max(0.0, math_min(1.0, g_context->brush_opacity));
		if (has_flow) {
			ui_slider(&g_context->brush_flow, tr("Flow"), 0.01, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
			changed |= ui_item_changed();
		}
		if (changed) {
			ui_header_handle->redraws = 2;
		}
	}

	last_radius  = *radius;
	last_opacity = g_context->brush_opacity;
	last_flow    = g_context->brush_flow;
	last_tool    = g_context->tool;
}

// View cube in the top right corner of the 3D view: a beveled cube turning with the camera, tap a face to look at it

typedef struct {
	vec4_t v[4];
	vec4_t n;
	i32    face; // 0..5 = +X -X +Y -Y +Z -Z, each part of the rounded cube keeps the color of its face
} ui_view_cube_quad_t;

// Rounded cube: every face is a grid, the cells near the edges bend around a radius of UI_VIEW_CUBE_ROUND
#define UI_VIEW_CUBE_STEPS 4 // Cells across each half of a rounded edge
#define UI_VIEW_CUBE_CELLS (UI_VIEW_CUBE_STEPS * 2 + 1)
#define UI_VIEW_CUBE_QUADS (6 * UI_VIEW_CUBE_CELLS * UI_VIEW_CUBE_CELLS)
#define UI_VIEW_CUBE_ROUND 0.3

static ui_view_cube_quad_t ui_view_cube_quads[UI_VIEW_CUBE_QUADS];
static bool                ui_view_cube_ready    = false;
static i32                 ui_view_cube_hovered  = -1;
static i32                 ui_view_cube_pressed  = -1;
static bool                ui_view_cube_down     = false; // Touch started on the cube
static bool                ui_view_cube_dragging = false;
static f32                 ui_view_cube_moved    = 0;

static vec4_t ui_view_cube_point(i32 a, f32 sa, f32 u, f32 v, vec4_t *normal) {
	f32 p[3];
	p[a]           = sa;
	p[(a + 1) % 3] = u;
	p[(a + 2) % 3] = v;
	f32 in         = 1.0 - UI_VIEW_CUBE_ROUND;
	f32 q[3];
	f32 d[3];
	for (i32 k = 0; k < 3; ++k) {
		q[k] = math_max(-in, math_min(in, p[k]));
		d[k] = p[k] - q[k];
	}
	vec4_t n = vec4_norm((vec4_t){d[0], d[1], d[2], 0.0});
	*normal  = n;
	return (vec4_t){q[0] + n.x * UI_VIEW_CUBE_ROUND, q[1] + n.y * UI_VIEW_CUBE_ROUND, q[2] + n.z * UI_VIEW_CUBE_ROUND, 0.0};
}

static void ui_view_cube_init() {
	if (ui_view_cube_ready) {
		return;
	}
	ui_view_cube_ready = true;
	// Grid lines: evenly spaced angles over the rounded part, one cell for the flat middle
	f32 ts[UI_VIEW_CUBE_CELLS + 1];
	for (i32 k = 0; k <= UI_VIEW_CUBE_STEPS; ++k) {
		f32 t                              = 1.0 - UI_VIEW_CUBE_ROUND + UI_VIEW_CUBE_ROUND * math_tan(k / (f32)UI_VIEW_CUBE_STEPS * math_pi() / 4.0);
		ts[UI_VIEW_CUBE_STEPS + 1 + k]     = t;
		ts[UI_VIEW_CUBE_STEPS - k]         = -t;
	}
	i32 n = 0;
	for (i32 a = 0; a < 3; ++a) {
		for (i32 si = 0; si < 2; ++si) {
			f32 sa = si == 0 ? 1.0 : -1.0;
			for (i32 iu = 0; iu < UI_VIEW_CUBE_CELLS; ++iu) {
				for (i32 iv = 0; iv < UI_VIEW_CUBE_CELLS; ++iv) {
					ui_view_cube_quad_t *q = &ui_view_cube_quads[n++];
					vec4_t               n0, n1, n2, n3;
					q->face = a * 2 + si;
					q->v[0] = ui_view_cube_point(a, sa, ts[iu], ts[iv], &n0);
					q->v[1] = ui_view_cube_point(a, sa, ts[iu + 1], ts[iv], &n1);
					q->v[2] = ui_view_cube_point(a, sa, ts[iu + 1], ts[iv + 1], &n2);
					q->v[3] = ui_view_cube_point(a, sa, ts[iu], ts[iv + 1], &n3);
					q->n    = vec4_norm((vec4_t){n0.x + n1.x + n2.x + n3.x, n0.y + n1.y + n2.y + n3.y, n0.z + n1.z + n2.z + n3.z, 0.0});
				}
			}
		}
	}
}

static char *ui_view_cube_label(i32 face) {
	char *labels[] = {"Right", "Left", "Back", "Front", "Top", "Bottom"};
	return tr(labels[face]);
}

static u32 ui_view_cube_color(i32 face, f32 shade) {
	// Washed out axis colors, the negative side a bit darker
	u32 cols[] = {0xffd98a86, 0xffb8706c, 0xff92c98c, 0xff74a86f, 0xff8daee0, 0xff6f90c2};
	u32 col    = cols[face];
	u32 r      = math_min(255, ((col >> 16) & 0xff) * shade);
	u32 g      = math_min(255, ((col >> 8) & 0xff) * shade);
	u32 b      = math_min(255, (col & 0xff) * shade);
	return 0xff000000 | (r << 16) | (g << 8) | b;
}

static void ui_view_cube_set_view(i32 face) {
	f32 pi = math_pi();
	switch (face) {
	case 0:
		viewport_set_view(1, 0, 0, pi / 2.0, 0, pi / 2.0); // Right
		break;
	case 1:
		viewport_set_view(-1, 0, 0, pi / 2.0, 0, -pi / 2.0); // Left
		break;
	case 2:
		viewport_set_view(0, 1, 0, pi / 2.0, 0, pi); // Back
		break;
	case 3:
		viewport_set_view(0, -1, 0, pi / 2.0, 0, 0); // Front
		break;
	case 4:
		viewport_set_view(0, 0, 1, 0, 0, 0); // Top
		break;
	case 5:
		viewport_set_view(0, 0, -1, pi, 0, pi); // Bottom
		break;
	}
}

static bool ui_view_cube_inside(f32 *px, f32 *py, i32 count, f32 x, f32 y) {
	bool in = false;
	for (i32 i = 0, j = count - 1; i < count; j = i++) {
		if ((py[i] > y) != (py[j] > y) && x < (px[j] - px[i]) * (y - py[i]) / (py[j] - py[i]) + px[i]) {
			in = !in;
		}
	}
	return in;
}

static i32 ui_view_cube_size() {
	return math_floor(100 * UI_SCALE());
}

static void ui_panels_draw_view_cube(i32 x, i32 y) {
	static ui_window_t *hwnd = NULL;
	if (hwnd == NULL) {
		hwnd = ui_window_create();
	}
	ui_view_cube_init();
	static quat_t last_rot = {0};
	quat_t        rot      = scene_camera->base->transform->rot;
	if (rot.x != last_rot.x || rot.y != last_rot.y || rot.z != last_rot.z || rot.w != last_rot.w) { // Turns with the camera
		last_rot      = rot;
		hwnd->redraws = 2;
	}

	i32    size  = ui_view_cube_size();
	f32    half  = size / 2.0;
	f32    scale = size * 0.3; // Cube half size, the rounded corners stay inside the window
	quat_t inv   = quat_inv(scene_camera->base->transform->rot);

	// Cells turned to the camera, the cube is convex so they never overlap
	static f32 px[UI_VIEW_CUBE_QUADS][4];
	static f32 py[UI_VIEW_CUBE_QUADS][4];
	static f32 facing[UI_VIEW_CUBE_QUADS];
	for (i32 i = 0; i < UI_VIEW_CUBE_QUADS; ++i) {
		ui_view_cube_quad_t *q = &ui_view_cube_quads[i];
		facing[i]              = vec4_apply_quat(q->n, inv).z;
		if (facing[i] <= 0.0) {
			continue;
		}
		for (i32 k = 0; k < 4; ++k) {
			vec4_t v = vec4_apply_quat(q->v[k], inv);
			px[i][k] = half + v.x * scale;
			py[i][k] = half - v.y * scale;
		}
	}

	// Tap a face to look at it, drag to orbit the camera
	ui_base_panel_add_rect(x, y, size, size);
	f32  mx     = mouse_x - x;
	f32  my     = mouse_y - y;
	bool inside = base_ui_enabled && mx >= 0 && my >= 0 && mx < size && my < size;
	i32  hit    = -1;
	if (inside) {
		for (i32 i = 0; i < UI_VIEW_CUBE_QUADS; ++i) {
			if (facing[i] > 0.0 && ui_view_cube_inside(px[i], py[i], 4, mx, my)) {
				hit = ui_view_cube_quads[i].face;
				break;
			}
		}
	}
	if (mouse_started("left") && inside) {
		ui_view_cube_down     = true;
		ui_view_cube_dragging = false;
		ui_view_cube_moved    = 0;
		ui_view_cube_pressed  = hit;
	}
	else if (ui_view_cube_down && mouse_down("left")) {
		ui_view_cube_moved += math_abs(mouse_movement_x) + math_abs(mouse_movement_y);
		if (ui_view_cube_moved > 6 * UI_SCALE()) {
			ui_view_cube_dragging = true;
		}
		if (ui_view_cube_dragging) {
			camera_orbit_drag();
		}
	}
	else if (ui_view_cube_down) {
		if (!ui_view_cube_dragging && hit >= 0 && hit == ui_view_cube_pressed) {
			ui_view_cube_set_view(hit);
		}
		ui_view_cube_down     = false;
		ui_view_cube_dragging = false;
		ui_view_cube_pressed  = -1;
	}
	ui_view_cube_hovered = ui_view_cube_dragging ? -1 : hit;

	u32  bg                = g_theme->WINDOW_BG_COL;
	g_theme->WINDOW_BG_COL = 0x00000000;
	bool draw              = ui_window(hwnd, x, y, size, size, false);
	g_theme->WINDOW_BG_COL = bg;
	if (!draw) {
		return;
	}

	for (i32 i = 0; i < UI_VIEW_CUBE_QUADS; ++i) {
		if (facing[i] <= 0.0) {
			continue;
		}
		ui_view_cube_quad_t *q     = &ui_view_cube_quads[i];
		f32                  shade = 0.62 + 0.38 * facing[i];
		if (q->face == ui_view_cube_hovered) {
			shade += 0.12;
		}
		draw_set_color(ui_view_cube_color(q->face, shade));
		draw_filled_triangle(px[i][0], py[i][0], px[i][1], py[i][1], px[i][2], py[i][2]);
		draw_filled_triangle(px[i][0], py[i][0], px[i][2], py[i][2], px[i][3], py[i][3]);
	}

	// Labels centered on the faces turned to the camera
	i32 font_size = math_floor(11 * UI_SCALE());
	draw_set_font(g_ui->ops->font, font_size);
	draw_set_color(0xff000000);
	for (i32 face = 0; face < 6; ++face) {
		f32    s[3] = {0, 0, 0};
		s[face / 2] = face % 2 == 0 ? 1.0 : -1.0;
		vec4_t c    = vec4_apply_quat((vec4_t){s[0], s[1], s[2], 0.0}, inv);
		if (c.z < 0.35) {
			continue;
		}
		char *label = ui_view_cube_label(face);
		f32   cx    = half + c.x * scale;
		f32   cy    = half - c.y * scale;
		f32   w     = draw_string_width(g_ui->ops->font, font_size, label);
		draw_string(label, cx - w / 2.0, cy - draw_font_height(g_ui->ops->font, font_size) / 2.0);
	}
	draw_set_font(g_ui->ops->font, g_ui->font_size);
	draw_set_color(0xffffffff);
}

// Frame button left of the view cube, in line with its top
static void ui_panels_draw_frame_button(i32 cube_x, i32 cube_y) {
	static ui_window_t *hwnd = NULL;
	if (hwnd == NULL) {
		hwnd = ui_window_create();
	}
	i32 size  = math_floor(30 * UI_SCALE());
	i32 cube  = ui_view_cube_size();
	f32 scale = cube * 0.3;
	i32 x     = cube_x + cube / 2 - scale * 1.5 - size;
	i32 y     = cube_y + cube / 2 - scale - size * 0.08; // Glyph top in line with the top of the cube seen from the front
	ui_base_panel_add_rect(x, y, size, size);
	u32  bg                = g_theme->WINDOW_BG_COL;
	g_theme->WINDOW_BG_COL = 0x00000000;
	bool draw              = ui_window(hwnd, x, y, size, size, false);
	g_theme->WINDOW_BG_COL = bg;
	if (draw) {
		gpu_texture_t *img       = resource_get("icons.k");
		rect_t        *rect      = resource_tile50(img, ICON_SCAN);
		g_ui->image_scroll_align = false;
		if (ui_sub_image(img, 0xffaaaaaa, size, rect->x, rect->y, rect->w, rect->h) == UI_STATE_RELEASED) {
			viewport_frame();
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Frame Model"));
		}
		g_ui->image_scroll_align = true;
	}
}

// Touch: 3D -> 2D -> both -> 3D, the icon shows what a tap switches to
static void ui_panels_draw_view_mode_button(i32 x, i32 y) {
	static ui_window_t *hwnd = NULL;
	if (hwnd == NULL) {
		hwnd = ui_window_create();
	}
	i32 size = math_floor(30 * UI_SCALE());
	ui_base_panel_add_rect(x, y, size, size);
	static i32 last_mode = -1; // The icon follows the mode however it was changed
	if (ui_base_view_mode() != last_mode) {
		last_mode     = ui_base_view_mode();
		hwnd->redraws = 2;
	}
	u32  bg                = g_theme->WINDOW_BG_COL;
	g_theme->WINDOW_BG_COL = 0x00000000;
	bool draw              = ui_window(hwnd, x, y, size, size, false);
	g_theme->WINDOW_BG_COL = bg;
	if (draw) {
		bool  only3d = base_view3d_show && !ui_view2d_show;
		bool  only2d = !base_view3d_show && ui_view2d_show;
		i32   icon   = only3d ? ICON_IMAGE : only2d ? ICON_WORKSPACE : ICON_CUBE;
		char *tip    = only3d ? tr("2D View") : only2d ? tr("2D and 3D View") : tr("3D View");
		gpu_texture_t *img       = resource_get("icons.k");
		rect_t        *rect      = resource_tile50(img, icon);
		g_ui->image_scroll_align = false;
		if (ui_sub_image(img, 0xffaaaaaa, size, rect->x, rect->y, rect->w, rect->h) == UI_STATE_RELEASED) {
			ui_base_cycle_view_mode();
			hwnd->redraws = 2;
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tip);
		}
		g_ui->image_scroll_align = true;
	}
}

static void ui_panels_draw_view_tools() {
	// A popover covers them, touches on it must not turn the view
	if (ui_popovers_current() != 0) {
		return;
	}
	i32 margin = math_floor(12 * UI_SCALE());
	i32 size   = ui_view_cube_size();
	i32 x      = base_view_x(0) + base_w() - size - margin; // Clear of the sidebar
	i32 y      = base_y() + margin;
	if (ui_popovers_bar_bottom() > 0) {
		y = ui_popovers_bar_bottom() + margin; // Below the bar of the popovers
	}
	if (g_context->show_compass) {
		ui_panels_draw_view_cube(x, y);
	}
	ui_panels_draw_frame_button(x, y);
	if (ui_popovers_enabled()) {
		// Under the frame button
		i32 bsize = math_floor(30 * UI_SCALE());
		f32 scale = size * 0.3;
		i32 fx    = x + size / 2 - scale * 1.5 - bsize;
		i32 fy    = y + size / 2 - scale - bsize * 0.08;
		ui_panels_draw_view_mode_button(fx, fy + bsize + math_floor(10 * UI_SCALE()));
	}
}

void ui_panels_render_ui() {
	if (base_view3d_show && !base_player && !base_is_split() && !g_context->capturing_screenshot) {
		ui_panels_draw_view_tools();
	}
	// 2D view alone: no cube, the view button stays at the top right
	else if (!base_view3d_show && ui_view2d_show && ui_popovers_enabled() && ui_popovers_current() == 0 && !base_player) {
		i32 margin = math_floor(12 * UI_SCALE());
		i32 bsize  = math_floor(30 * UI_SCALE());
		ui_panels_draw_view_mode_button(iron_window_width() - bsize - margin * 2, ui_popovers_bar_bottom() + margin);
	}
	if (!ui_panels_show()) {
		return;
	}
	ui_panels_draw_history();
	ui_panels_draw_brush();
}
