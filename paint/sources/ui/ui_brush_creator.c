
#include "../global.h"

// Brush Creator: makes and edits the brushes of the brush library. Full screen, over everything: the tabs on the left, the
// settings of the picked tab in the middle, the drawing pad on the right to try the brush on (brush_pad.c). The brush being edited
// is a copy, its settings paint the pad, Cancel puts the settings from before back and the check mark saves the brush.

typedef enum {
	BC_TAB_STROKE,
	BC_TAB_STABILIZATION,
	BC_TAB_TAPER,
	BC_TAB_SHAPE,
	BC_TAB_PENCIL,
	BC_TAB_PREVIEW,
	BC_TAB_COUNT,
} bc_tab_t;

typedef enum {
	BC_FORMAT_PERCENT, // 0 shows None
	BC_FORMAT_PERCENT_MAX, // 0 shows None, the end Max
	BC_FORMAT_DEGREES,
} bc_format_t;

#define BC_ANIM_TIME 0.22

static bool             bc_show       = false;
static bool             bc_closing    = false;
static f64              bc_anim_start = 0.0;
static i32              bc_tab        = BC_TAB_STROKE;
static brush_def_t      bc_edit; // Copy being edited
static brush_def_t     *bc_target = NULL; // Brush being edited, NULL for a new one
static i32              bc_group  = 0;    // Group of a new brush
static char            *bc_name   = "";
static gpu_texture_t   *bc_tip    = NULL; // Imported tip of the brush being edited
static brush_settings_t bc_saved;         // Settings in the context before the Creator opened
static gpu_texture_t   *bc_saved_tip = NULL;
static gpu_texture_t   *bc_row       = NULL; // Stroke as the brush list shows it, on the Preview tab
static bool             bc_row_stale = true;
static bool             bc_row_pending = false;
static ui_window_t     *bc_main        = NULL;
static ui_window_t     *bc_list        = NULL;
static i32              bc_slider_next   = 0;  // Id of the next slider drawn
static i32              bc_slider_active = -1; // Slider being dragged
static bool             bc_released      = false; // A slider was let go this frame

bool ui_brush_creator_shown() {
	return bc_show;
}

void ui_brush_creator_redraw() {
	if (bc_main != NULL) {
		bc_main->redraws = 2;
		bc_list->redraws = 2;
	}
}

// The settings being edited paint with: the pad and the preview follow. shader: the paint shader is made again, the tip,
// roundness or rotation may have changed what it is compiled with
static void bc_changed(bool shader) {
	brush_library_apply_settings(&bc_edit.s, bc_tip);
	if (shader) {
		brush_library_applied();
	}
	brush_pad_set_size(bc_edit.s.preview_size);
	brush_pad_changed();
	bc_row_stale = true;
	ui_brush_creator_redraw();
}

static void bc_open(brush_def_t *d, brush_def_t *target, i32 group) {
	brush_library_capture_settings(&bc_saved);
	bc_saved_tip     = brush_library_tip_image_get();
	bc_edit          = *d;
	bc_target        = target;
	bc_group         = group;
	bc_tip           = target != NULL ? brush_library_tip_of(target) : NULL;
	bc_name          = string_copy(brush_library_name(d));
	bc_tab           = BC_TAB_STROKE;
	bc_show          = true;
	bc_closing       = false;
	bc_anim_start    = iron_time();
	bc_slider_active = -1;
	ui_popovers_redraw(-1);
	brush_pad_reset();
	bc_changed(true);
}

// Opens the Creator on a new brush, saved to group (a group of your own when that one is built-in)
void ui_brush_creator_new(i32 group) {
	brush_def_t *d = brush_library_new_def();
	bc_open(d, NULL, group);
	free(d);
}

void ui_brush_creator_edit(brush_def_t *d) {
	if (d != NULL && !d->builtin) {
		bc_open(d, d, d->group);
	}
}

static void bc_close() {
	bc_closing    = true;
	bc_anim_start = iron_time();
	// The pad painted on the plane, the view draws the meshes again
	g_context->brush_dab_x = -1.0;
	g_context->brush_dab_y = -1.0;
	g_context->ddirty      = 3;
	ui_popovers_redraw(-1);
	base_redraw_ui();
}

static void bc_cancel() {
	brush_library_apply_settings(&bc_saved, bc_saved_tip);
	brush_library_applied();
	bc_close();
}

static void bc_save() {
	char *name = string_equals(bc_name, "") ? tr("Brush") : bc_name;
	if (bc_target == NULL) {
		brush_group_t *g = brush_group_find(bc_group);
		brush_def_t   *d = brush_library_new_def();
		d->name          = string_copy(name);
		d->s             = bc_edit.s;
		d->group         = g != NULL && !g->builtin ? g->id : brush_library_user_group();
		brush_library_add(d, bc_tip);
		tab_brushes_show_group(d->group);
		brush_library_use(d);
	}
	else {
		bc_target->name      = string_copy(name);
		bc_target->s         = bc_edit.s;
		bc_target->row_stale = true;
		brush_library_set_tip(bc_target, bc_tip);
		brush_library_save();
		brush_library_use(bc_target);
	}
	bc_close();
}

// Saves the brush and closes, or closes as Cancel does
void ui_brush_creator_close(bool save) {
	if (!bc_show || bc_closing) {
		return;
	}
	if (save) {
		bc_save();
	}
	else {
		bc_cancel();
	}
}

static void bc_tip_picked(char *path) {
	gpu_texture_t *tip = brush_library_load_tip(path);
	if (tip == NULL) {
		console_error(tr("Could not read the image"));
		return;
	}
	bc_tip       = tip;
	bc_edit.s.tip = TIP_IMAGE;
	bc_changed(true);
}

static void bc_render_row(void *_) {
	bc_row_pending = false;
	if (!bc_show || brush_pad_busy()) {
		return;
	}
	tab_brushes_render_row(&bc_edit.s, bc_tip, &bc_row);
	bc_row_stale = false;
	ui_brush_creator_redraw();
}

// Colors, close to the system's dark look
#define BC_COL_BG      0xff1c1c1e
#define BC_COL_PANEL   0xff262628
#define BC_COL_PILL    0xff3a3a3c
#define BC_COL_TRACK   0xff48484a
#define BC_COL_DIVIDER 0xff2c2c2e
#define BC_COL_DIM     0xff98989f

static f32 bc_scale() {
	return UI_SCALE();
}

static void bc_text(char *text, f32 x, f32 y, i32 size, u32 color) {
	draw_set_font(g_ui->ops->font, size);
	draw_set_color(color);
	draw_string(text, x, y);
	draw_set_font(g_ui->ops->font, g_ui->font_size);
}

static f32 bc_text_w(char *text, i32 size) {
	return draw_string_width(g_ui->ops->font, size, text);
}

static f32 bc_text_h(i32 size) {
	return draw_font_height(g_ui->ops->font, size);
}

// An icon of the atlas, tinted
static void bc_icon(i32 icon, f32 x, f32 y, f32 size, u32 color) {
	gpu_texture_t *img = resource_get("icons.k");
	rect_t        *r   = resource_tile50(img, icon);
	draw_set_color(color);
	draw_scaled_sub_image(img, r->x, r->y, r->w, r->h, x, y, size, size);
}

// A tap on the rectangle of the current window, released over it
static ui_state_t bc_hit(f32 x, f32 y, f32 w, f32 h) {
	f32 _x = g_ui->_x;
	f32 _y = g_ui->_y;
	f32 _w = g_ui->_w;
	g_ui->_x         = x;
	g_ui->_y         = y;
	g_ui->_w         = w;
	ui_state_t state = UI_STATE_IDLE;
	if (ui_get_released(h)) {
		state = UI_STATE_RELEASED;
	}
	else if (g_ui->input_down && ui_get_hover(h)) {
		state = UI_STATE_DOWN;
	}
	g_ui->_x = _x;
	g_ui->_y = _y;
	g_ui->_w = _w;
	return state;
}

// A rounded button with a label, and an icon when icon is not ICON_NONE
static bool bc_pill_button(char *label, i32 icon, f32 x, f32 y, f32 h, bool right_aligned, f32 *w_out) {
	f32 s    = bc_scale();
	f32 pad  = 16 * s;
	f32 is   = icon != ICON_NONE ? h * 0.55 : 0;
	f32 w    = pad * 2 + bc_text_w(label, g_ui->font_size) + (is > 0 ? is + 8 * s : 0);
	x        = right_aligned ? x - w : x;
	*w_out   = w;
	ui_state_t state = bc_hit(x, y, w, h);
	draw_set_color(BC_COL_PILL);
	ui_base_fill_round(x, y, w, h, h / 2);
	draw_set_color(state == UI_STATE_DOWN ? 0x20ffffff : 0x00000000);
	ui_base_fill_round(x, y, w, h, h / 2);
	f32 tx = x + pad;
	if (is > 0) {
		bc_icon(icon, tx, y + (h - is) / 2, is, g_theme->TEXT_COL);
		tx += is + 8 * s;
	}
	bc_text(label, tx, y + (h - bc_text_h(g_ui->font_size)) / 2, g_ui->font_size, g_theme->TEXT_COL);
	return state == UI_STATE_RELEASED;
}

// Round accent button with a check mark
static bool bc_check_button(f32 x, f32 y, f32 h) {
	ui_state_t state = bc_hit(x, y, h, h);
	draw_set_color(g_theme->HIGHLIGHT_COL);
	draw_filled_circle(x + h / 2, y + h / 2, h / 2, 48);
	if (state == UI_STATE_DOWN) {
		draw_set_color(0x30000000);
		draw_filled_circle(x + h / 2, y + h / 2, h / 2, 48);
	}
	f32 is = h * 0.55;
	bc_icon(ICON_CHECK, x + (h - is) / 2, y + (h - is) / 2, is, 0xffffffff);
	return state == UI_STATE_RELEASED;
}

static char *bc_tab_name(i32 tab) {
	char *names[] = {tr("Stroke path"), tr("Stabilization"), tr("Taper"), tr("Shape"), tr("Apple Pencil"), tr("Preview")};
	return names[tab];
}

static i32 bc_tab_icon(i32 tab) {
	i32 icons[] = {ICON_PATH, ICON_GESTURE, ICON_CURVE, ICON_STAR, ICON_EDIT, ICON_IMAGE};
	return icons[tab];
}

static char *bc_tab_title(i32 tab) {
	char *titles[] = {tr("Stroke properties"), tr("Stabilization"), tr("Taper properties"), tr("Shape properties"), tr("Apple Pencil"), tr("Preview")};
	return titles[tab];
}

static void bc_draw_sidebar(f32 x, f32 y, f32 w, f32 h) {
	f32 s = bc_scale();
	draw_set_color(BC_COL_PANEL);
	ui_base_fill_round(x, y, w, h, 16 * s);

	f32 pad  = 14 * s;
	g_ui->_x = x + pad;
	g_ui->_y = y + pad;
	g_ui->_w = w - pad * 2;
	ui_popovers_title(tr("Brush Creator"), false);
	g_ui->_y += 8 * s;

	f32 row_h = math_floor(UI_ELEMENT_H() * 1.7);
	for (i32 i = 0; i < BC_TAB_COUNT; ++i) {
		f32        ry       = g_ui->_y;
		bool       selected = bc_tab == i;
		ui_state_t state    = bc_hit(g_ui->_x, ry, g_ui->_w, row_h);
		if (state == UI_STATE_RELEASED && !selected) {
			bc_tab = i;
			ui_brush_creator_redraw();
			selected = true;
		}
		if (selected) {
			draw_set_color(g_theme->HIGHLIGHT_COL);
			ui_base_fill_round(g_ui->_x, ry, g_ui->_w, row_h, row_h / 2);
		}
		u32 color = selected ? 0xffffffff : BC_COL_DIM;
		f32 is    = row_h * 0.45;
		bc_icon(bc_tab_icon(i), g_ui->_x + 16 * s, ry + (row_h - is) / 2, is, color);
		bc_text(bc_tab_name(i), g_ui->_x + 16 * s + is + 16 * s, ry + (row_h - bc_text_h(g_ui->font_size)) / 2, g_ui->font_size, color);
		g_ui->_y += row_h + 4 * s;
	}
}

// Value of a slider as its pill shows it
static char *bc_format(f32 v, f32 min, f32 max, bc_format_t format) {
	if (format == BC_FORMAT_DEGREES) {
		return string_tmp("%d°", (i32)roundf(v));
	}
	i32 p = (i32)roundf((v - min) / (max - min) * 100.0);
	if (p <= 0) {
		return tr("None");
	}
	if (p >= 100 && format == BC_FORMAT_PERCENT_MAX) {
		return tr("Max");
	}
	return string_tmp("%d%%", p);
}

// Label and value pill above a track: dragging anywhere on the row sets the value. True while it changes.
static bool bc_slider(char *label, f32 *v, f32 min, f32 max, bc_format_t format) {
	i32 id   = bc_slider_next++;
	f32 s    = bc_scale();
	f32 x    = g_ui->_x;
	f32 y    = g_ui->_y;
	f32 w    = g_ui->_w;
	f32 lh   = UI_ELEMENT_H() * 1.2;
	f32 h    = lh + 30 * s;
	f32 pad  = 8 * s;
	f32 tx   = x + pad;
	f32 tw   = w - pad * 2;
	f32 ty   = y + lh + 14 * s;
	bool changed = false;

	if (ui_get_started(h)) {
		bc_slider_active = id;
	}
	if (bc_slider_active == id) {
		if (g_ui->input_down) {
			f32 t  = (g_ui->input_x - g_ui->_window_x - tx) / tw;
			t      = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
			f32 nv = min + t * (max - min);
			if (nv != *v) {
				*v      = nv;
				changed = true;
			}
		}
		else {
			bc_slider_active = -1;
			bc_released      = true;
		}
	}

	if (ui_is_visible(h)) {
		i32 fs = g_ui->font_size;
		bc_text(label, tx, y + (lh - bc_text_h(fs)) / 2, fs, g_theme->TEXT_COL);
		char *value = bc_format(*v, min, max, format);
		f32   pw    = bc_text_w(value, fs) + 32 * s;
		f32   ph    = lh * 0.85;
		draw_set_color(BC_COL_PILL);
		ui_base_fill_round(x + w - pad - pw, y + (lh - ph) / 2, pw, ph, ph / 2);
		bc_text(value, x + w - pad - pw + (pw - bc_text_w(value, fs)) / 2, y + (lh - bc_text_h(fs)) / 2, fs, g_theme->TEXT_COL);

		f32 t      = (*v - min) / (max - min);
		t          = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
		f32 th     = 4 * s;
		f32 kw     = 26 * s;
		f32 kh     = 16 * s;
		f32 kx     = tx + t * (tw - kw);
		draw_set_color(BC_COL_TRACK);
		ui_base_fill_round(tx, ty - th / 2, tw, th, th / 2);
		draw_set_color(g_theme->HIGHLIGHT_COL);
		ui_base_fill_round(tx, ty - th / 2, kx + kw / 2 - tx, th, th / 2);
		draw_set_color(0xffffffff);
		ui_base_fill_round(kx, ty - kh / 2, kw, kh, kh / 2);
	}
	ui_end_element_of_size(h + 8 * s);
	return changed;
}

// Label with a switch at the right end, tapping the row turns it
static bool bc_toggle(char *label, bool *v) {
	f32 s   = bc_scale();
	f32 h   = UI_ELEMENT_H() * 1.6;
	f32 pad = 8 * s;
	bool changed = false;
	if (ui_get_released(h)) {
		*v      = !*v;
		changed = true;
	}
	if (ui_is_visible(h)) {
		f32 x  = g_ui->_x;
		f32 y  = g_ui->_y;
		f32 w  = g_ui->_w;
		i32 fs = g_ui->font_size;
		bc_text(label, x + pad, y + (h - bc_text_h(fs)) / 2, fs, g_theme->TEXT_COL);
		f32 sw = 50 * s;
		f32 sh = 30 * s;
		f32 sx = x + w - pad - sw;
		f32 sy = y + (h - sh) / 2;
		draw_set_color(*v ? g_theme->HIGHLIGHT_COL : BC_COL_TRACK);
		ui_base_fill_round(sx, sy, sw, sh, sh / 2);
		f32 r = sh / 2 - 2 * s;
		draw_set_color(0xffffffff);
		draw_filled_circle(*v ? sx + sw - sh / 2 : sx + sh / 2, sy + sh / 2, r, 32);
	}
	ui_end_element_of_size(h + 4 * s);
	return changed;
}

// A choice among a few, as a row of pills under its label
static bool bc_segment(char *label, i32 *v, char **options, i32 count) {
	f32 s   = bc_scale();
	f32 lh  = UI_ELEMENT_H() * 1.2;
	f32 ph  = UI_ELEMENT_H() * 1.2;
	f32 h   = lh + ph + 6 * s;
	f32 pad = 8 * s;
	f32 x   = g_ui->_x;
	f32 y   = g_ui->_y;
	f32 w   = g_ui->_w;
	i32 fs  = g_ui->font_size;
	bool changed = false;
	bc_text(label, x + pad, y + (lh - bc_text_h(fs)) / 2, fs, g_theme->TEXT_COL);
	f32 cw = (w - pad * 2) / count;
	draw_set_color(BC_COL_PILL);
	ui_base_fill_round(x + pad, y + lh, w - pad * 2, ph, ph / 2);
	for (i32 i = 0; i < count; ++i) {
		f32 cx = x + pad + cw * i;
		if (bc_hit(cx, y + lh, cw, ph) == UI_STATE_RELEASED && *v != i) {
			*v      = i;
			changed = true;
		}
		if (*v == i) {
			draw_set_color(g_theme->HIGHLIGHT_COL);
			ui_base_fill_round(cx + 2 * s, y + lh + 2 * s, cw - 4 * s, ph - 4 * s, (ph - 4 * s) / 2);
		}
		bc_text(options[i], cx + (cw - bc_text_w(options[i], fs)) / 2, y + lh + (ph - bc_text_h(fs)) / 2, fs, *v == i ? 0xffffffff : g_theme->TEXT_COL);
	}
	ui_end_element_of_size(h + 8 * s);
	return changed;
}

static void bc_section(char *title) {
	f32 s  = bc_scale();
	i32 fs = g_ui->font_size;
	f32 h  = bc_text_h(fs) + 12 * s;
	bc_text(title, g_ui->_x + 8 * s, g_ui->_y + 6 * s, fs, BC_COL_DIM);
	ui_end_element_of_size(h);
}

// Tips to pick from: round, the generated ones and the imported image, then Import
static void bc_tip_grid() {
	f32 s     = bc_scale();
	f32 x     = g_ui->_x + 8 * s;
	f32 y     = g_ui->_y;
	f32 w     = g_ui->_w - 16 * s;
	f32 gap   = 10 * s;
	f32 cell  = 64 * s;
	i32 cols  = (i32)((w + gap) / (cell + gap));
	cols      = cols < 1 ? 1 : cols;
	i32 tips[TIP_COUNT + 2];
	i32 count = 0;
	for (i32 t = TIP_NONE; t < TIP_COUNT; ++t) {
		tips[count++] = t;
	}
	if (bc_tip != NULL) {
		tips[count++] = TIP_IMAGE;
	}
	tips[count++] = -1; // Import

	for (i32 i = 0; i < count; ++i) {
		f32  cx       = x + (i % cols) * (cell + gap);
		f32  cy       = y + (i / cols) * (cell + gap);
		i32  tip      = tips[i];
		bool selected = tip == bc_edit.s.tip;
		if (selected) {
			draw_set_color(g_theme->HIGHLIGHT_COL);
			ui_base_fill_round(cx - 3 * s, cy - 3 * s, cell + 6 * s, cell + 6 * s, 12 * s);
		}
		draw_set_color(BC_COL_PILL);
		ui_base_fill_round(cx, cy, cell, cell, 10 * s);
		f32 in = 8 * s;
		if (tip == TIP_NONE) {
			draw_set_color(0xffffffff);
			draw_filled_circle(cx + cell / 2, cy + cell / 2, cell / 2 - in, 48);
		}
		else if (tip == -1) {
			bc_icon(ICON_IMPORT, cx + cell * 0.25, cy + cell * 0.25, cell * 0.5, g_theme->TEXT_COL);
		}
		else {
			gpu_texture_t *img = tip == TIP_IMAGE ? bc_tip : brush_tip_texture(tip);
			if (img != NULL) {
				draw_set_color(0xffffffff);
				draw_scaled_image(img, cx + in, cy + in, cell - in * 2, cell - in * 2);
			}
		}
		if (bc_hit(cx, cy, cell, cell) == UI_STATE_RELEASED) {
			if (tip == -1) {
				ui_files_show("png,jpg,jpeg", false, false, &bc_tip_picked);
			}
			else if (!selected) {
				bc_edit.s.tip = tip;
				bc_changed(true);
			}
		}
	}
	i32 rows = (count + cols - 1) / cols;
	ui_end_element_of_size(rows * (cell + gap) + 8 * s);
}

// Settings of the picked tab. A slider moving changes the pad as it goes, letting it go makes the paint shader again.
static void bc_draw_settings() {
	brush_settings_t *b       = &bc_edit.s;
	bool              changed = false;
	bool              shader  = false;
	bc_slider_next            = 0;
	bc_released               = false;

	if (bc_tab == BC_TAB_STROKE) {
		changed |= bc_slider(tr("Spacing"), &b->spacing, 0.0, 2.0, BC_FORMAT_PERCENT);
		changed |= bc_slider(tr("Spacing Jitter"), &b->spacing_jitter, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Jitter Lateral"), &b->jitter_lateral, 0.0, 2.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Jitter Linear"), &b->jitter_linear, 0.0, 2.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Fall Off"), &b->falloff, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Flow"), &b->flow, 0.01, 1.0, BC_FORMAT_PERCENT_MAX);
	}
	else if (bc_tab == BC_TAB_STABILIZATION) {
		bc_section(tr("StreamLine"));
		changed |= bc_slider(tr("Amount"), &b->streamline, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		bc_section(tr("Stabilization"));
		changed |= bc_slider(tr("Amount"), &b->stabilization, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
	}
	else if (bc_tab == BC_TAB_TAPER) {
		bc_section(tr("Pressure Taper"));
		changed |= bc_slider(tr("Start"), &b->taper_start, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("End"), &b->taper_end, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		bc_section(tr("Touch Taper"));
		changed |= bc_slider(tr("Start"), &b->touch_taper_start, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("End"), &b->touch_taper_end, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		bc_section(tr("Taper Properties"));
		changed |= bc_slider(tr("Size"), &b->taper_size, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Opacity"), &b->taper_opacity, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
	}
	else if (bc_tab == BC_TAB_SHAPE) {
		bc_section(tr("Shape Source"));
		bc_tip_grid();
		bc_section(tr("Shape Behavior"));
		char *rotations[] = {tr("Fixed"), tr("Follow Stroke"), tr("Pencil")};
		if (bc_segment(tr("Rotation"), &b->rotation, rotations, 3)) {
			changed = true;
			shader  = true;
		}
		changed |= bc_slider(tr("Angle"), &b->angle, 0.0, 360.0, BC_FORMAT_DEGREES);
		changed |= bc_slider(tr("Scatter"), &b->jitter_angle, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Roundness"), &b->roundness, 0.05, 1.0, BC_FORMAT_PERCENT);
		if (b->tip == TIP_NONE) {
			changed |= bc_slider(tr("Hardness"), &b->hardness, 0.0, 1.0, BC_FORMAT_PERCENT);
		}
		bc_section(tr("Randomize"));
		changed |= bc_slider(tr("Size Jitter"), &b->jitter_size, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Opacity Jitter"), &b->jitter_opacity, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
	}
	else if (bc_tab == BC_TAB_PENCIL) {
		bc_section(tr("Pressure"));
		changed |= bc_slider(tr("Size"), &b->pressure_size, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Opacity"), &b->pressure_opacity, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Flow"), &b->pressure_flow, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Hardness"), &b->pressure_hardness, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		bc_section(tr("Tilt"));
		changed |= bc_slider(tr("Size"), &b->tilt_size, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		changed |= bc_slider(tr("Opacity"), &b->tilt_opacity, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		if (bc_toggle(tr("Tilt Shading"), &b->tilt_shading)) {
			changed = true;
			shader  = true;
		}
	}
	else if (bc_tab == BC_TAB_PREVIEW) {
		f32 s = bc_scale();
		ui_text_input(&bc_name, tr("Name"), UI_ALIGN_LEFT, true, false);
		g_ui->_y += 8 * s;
		changed |= bc_slider(tr("Preview Size"), &b->preview_size, 0.0, 1.0, BC_FORMAT_PERCENT_MAX);
		bc_section(tr("In the brush list"));
		f32 h = math_floor(UI_ELEMENT_H() * 3.0);
		if (ui_is_visible(h)) {
			draw_set_color(BC_COL_PILL);
			ui_base_fill_round(g_ui->_x + 8 * s, g_ui->_y, g_ui->_w - 16 * s, h, 12 * s);
			tab_brushes_draw_stroke(bc_row, g_ui->_x + 20 * s, g_ui->_y + 8 * s, g_ui->_w - 40 * s, h - 16 * s);
		}
		ui_end_element_of_size(h);
		if (bc_row_stale && !bc_row_pending && !brush_pad_busy()) {
			bc_row_pending = true;
			sys_notify_on_next_frame(&bc_render_row, NULL);
		}
	}

	if (bc_slider_active >= 0) { // Moving a slider does not scroll the settings
		g_ui->input_dy = 0;
	}
	if (changed) {
		bc_changed(shader);
	}
	if (bc_released) { // The tip, roundness or angle jitter may have changed what the paint shader is compiled with
		bc_changed(true);
	}
}

static void bc_pad_menu() {
	if (ui_menu_button(tr("Clear Drawing Pad"), "", ICON_ERASE)) {
		brush_pad_clear();
	}
	if (ui_menu_button(tr("Reset Drawing Pad"), "", ICON_REFRESH)) {
		brush_pad_reset();
	}
}

void ui_brush_creator_render_ui() {
	if (!bc_show) {
		return;
	}
	// Animation: 0 hidden, 1 shown
	f64 t = (iron_time() - bc_anim_start) / BC_ANIM_TIME;
	t     = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
	if (bc_closing && t >= 1.0) {
		bc_show    = false;
		bc_closing = false;
		ui_popovers_redraw(-1);
		base_redraw_ui();
		return;
	}
	f32 k = bc_closing ? 1.0 - t * t : 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
	if (t < 1.0) {
		base_redraw_ui(); // Every frame until it is done
		iron_delay_idle_sleep();
	}

	if (bc_main == NULL) {
		bc_main = ui_window_create();
		bc_list = ui_window_create();
	}

	f32 s  = bc_scale();
	f32 ww = iron_window_width();
	f32 wh = iron_window_height();
	f32 m  = math_floor(12 * s);
	f32 sw = math_floor(fmin(ww * 0.26, 340 * s)); // Sidebar
	f32 cx = m + sw + m * 2;                       // Settings column
	f32 cw = math_floor((ww - cx) * 0.45);
	f32 px = cx + cw + m * 2; // Drawing pad
	f32 pw = ww - px - m;
	f32 hh = math_floor(UI_ELEMENT_H() * 1.8); // Pad header
	f32 py = m + hh + m;
	f32 ph = wh - py - m;

	ui_base_panel_add_rect(0, 0, ww, wh); // Touches neither paint nor turn the view
	bool input          = g_ui->input_enabled;
	g_ui->input_enabled = base_ui_enabled && !bc_closing;
	bool interactive    = g_ui->input_enabled && !ui_menu_show && !ui_box_show;

	bc_main->redraws = 2; // The pad changes as it is drawn on
	if (ui_window(bc_main, 0, 0, ww, wh, false)) {
		draw_set_color(BC_COL_BG);
		draw_filled_rect(0, 0, ww, wh);

		bc_draw_sidebar(m, m, sw, wh - m * 2);

		// Divider between the settings and the pad
		draw_set_color(BC_COL_DIVIDER);
		draw_filled_rect(px - m, 0, 1 * s, wh);

		// Pad header: its menu on the left, Cancel and save on the right
		f32 bh = math_floor(hh * 0.8);
		f32 by = m + (hh - bh) / 2;
		f32 bw;
		if (bc_pill_button(tr("Drawing Pad"), ICON_EDIT, px, by, bh, false, &bw)) {
			ui_menu_draw(&bc_pad_menu, (i32)px, (i32)(by + bh + 4 * s));
		}
		if (bc_check_button(ww - m - bh, by, bh)) {
			bc_save();
		}
		if (bc_pill_button(tr("Cancel"), ICON_NONE, ww - m - bh - 12 * s, by, bh, true, &bw)) {
			bc_cancel();
		}

		brush_pad_draw(px, py, pw, ph);
		if (interactive && !bc_closing) {
			brush_pad_input(px, py, pw, ph);
		}
	}
	// Faded and grown in from the center. Set once the window has begun: beginning it ends the window drawn before.
	ui_window_composite_alpha = k;
	ui_window_composite_scale = 0.96 + 0.04 * k;
	ui_window_composite_ox    = ww / 2;
	ui_window_composite_oy    = wh / 2;
	ui_end_window();

	u32 window_bg          = g_theme->WINDOW_BG_COL;
	g_theme->WINDOW_BG_COL = 0x00000000;
	bool content           = ui_window(bc_list, cx, m, cw, wh - m * 2, false);
	g_theme->WINDOW_BG_COL = window_bg;
	if (content) {
		f32 title_size = g_ui->font_size * 2;
		f32 th         = bc_text_h(title_size);
		bc_text(bc_tab_title(bc_tab), g_ui->_x + 8 * s, g_ui->_y + 8 * s, title_size, BC_COL_DIM);
		g_ui->_y += th + 24 * s;
		bc_draw_settings();
	}
	ui_end_window();

	ui_window_composite_alpha = 1.0;
	ui_window_composite_scale = 1.0;
	g_ui->input_enabled       = input;
}
