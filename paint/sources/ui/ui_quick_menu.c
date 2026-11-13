
#include "../global.h"

// Radial menu over the canvas, opened by a pencil squeeze or double tap, the quick menu key (space) or a long press on the toolbar.
// Items sit clockwise from the top (config quick_menu_slots). A tap on an item runs it, size and opacity are dragged sideways
// while held. A press outside closes the menu without painting.

bool ui_quick_menu_show = false;

static f32  ui_quick_menu_x;
static f32  ui_quick_menu_y;
static i32  ui_quick_menu_hover   = -1;
static i32  ui_quick_menu_pressed = -1; // Slot the press started on
static i32  ui_quick_menu_drag    = QUICK_MENU_NONE;
static f32  ui_quick_menu_drag_x;
static f32  ui_quick_menu_drag_value;
static bool ui_quick_menu_swallow = false; // The press that closed the menu stays out of the canvas until released
static i32  ui_quick_menu_recent[3] = {-1, -1, -1}; // Brush library ids, the latest first

static f32 ui_quick_menu_radius() {
	return 130 * UI_SCALE();
}

static f32 ui_quick_menu_inner() {
	return ui_quick_menu_radius() * 0.32;
}

void ui_quick_menu_brush_used(i32 id) {
	if (id < 0 || ui_quick_menu_recent[0] == id) {
		return;
	}
	i32 last = 2;
	for (i32 i = 0; i < 3; ++i) {
		if (ui_quick_menu_recent[i] == id) {
			last = i;
			break;
		}
	}
	for (i32 i = last; i > 0; --i) {
		ui_quick_menu_recent[i] = ui_quick_menu_recent[i - 1];
	}
	ui_quick_menu_recent[0] = id;
}

static i32 ui_quick_menu_item(i32 slot) {
	i32_array_t *slots = g_config->quick_menu_slots;
	if (slots == NULL || slot < 0 || slot >= slots->length) {
		return QUICK_MENU_NONE;
	}
	i32 item = slots->buffer[slot];
	return item > QUICK_MENU_NONE && item < QUICK_MENU_COUNT ? item : QUICK_MENU_NONE;
}

char *ui_quick_menu_item_name(i32 item) {
	switch (item) {
	case QUICK_MENU_BRUSH:
		return tr("Brush");
	case QUICK_MENU_ERASER:
		return tr("Eraser");
	case QUICK_MENU_PICKER:
		return tr("Picker");
	case QUICK_MENU_FILL:
		return tr("Fill");
	case QUICK_MENU_UNDO:
		return tr("Undo");
	case QUICK_MENU_REDO:
		return tr("Redo");
	case QUICK_MENU_SIZE:
		return tr("Size");
	case QUICK_MENU_OPACITY:
		return tr("Opacity");
	case QUICK_MENU_RECENT_BRUSH_1:
		return tr("Recent Brush 1");
	case QUICK_MENU_RECENT_BRUSH_2:
		return tr("Recent Brush 2");
	case QUICK_MENU_RECENT_BRUSH_3:
		return tr("Recent Brush 3");
	case QUICK_MENU_CLONE:
		return tr("Clone");
	case QUICK_MENU_BLUR:
		return tr("Blur");
	default:
		return tr("None");
	}
}

// Shown on the menu: recent brushes by their name
static char *ui_quick_menu_item_label(i32 item) {
	if (item >= QUICK_MENU_RECENT_BRUSH_1 && item <= QUICK_MENU_RECENT_BRUSH_3) {
		brush_def_t *b = brush_library_find(ui_quick_menu_recent[item - QUICK_MENU_RECENT_BRUSH_1]);
		return b != NULL ? brush_library_name(b) : "";
	}
	if (item == QUICK_MENU_SIZE) {
		return string_tmp("%s %d", tr("Size"), (i32)math_round(g_context->brush_radius * 100.0));
	}
	if (item == QUICK_MENU_OPACITY) {
		return string_tmp("%s %d%%", tr("Opacity"), (i32)math_round(g_context->brush_opacity * 100.0));
	}
	return ui_quick_menu_item_name(item);
}

static bool ui_quick_menu_item_enabled(i32 item) {
	switch (item) {
	case QUICK_MENU_NONE:
		return false;
	case QUICK_MENU_UNDO:
		return history_undos > 0;
	case QUICK_MENU_REDO:
		return history_redos > 0;
	case QUICK_MENU_PICKER:
		return ui_toolbar_tool_visible(TOOL_TYPE_PICKER);
	case QUICK_MENU_RECENT_BRUSH_1:
	case QUICK_MENU_RECENT_BRUSH_2:
	case QUICK_MENU_RECENT_BRUSH_3:
		return brush_library_find(ui_quick_menu_recent[item - QUICK_MENU_RECENT_BRUSH_1]) != NULL;
	default:
		return true;
	}
}

static bool ui_quick_menu_item_active(i32 item) {
	tool_type_t tool = g_context->tool;
	return (item == QUICK_MENU_BRUSH && tool == TOOL_TYPE_BRUSH) || (item == QUICK_MENU_ERASER && tool == TOOL_TYPE_ERASER) ||
	       (item == QUICK_MENU_PICKER && tool == TOOL_TYPE_PICKER) || (item == QUICK_MENU_FILL && tool == TOOL_TYPE_FILL) ||
	       (item == QUICK_MENU_CLONE && tool == TOOL_TYPE_CLONE) || (item == QUICK_MENU_BLUR && tool == TOOL_TYPE_BLUR);
}

void ui_quick_menu_open(f32 x, f32 y) {
	if (x < 0.0 || y < 0.0) {
		x = mouse_x;
		y = mouse_y;
	}
	// Inside the viewport, the panels around it would take the taps too
	f32 r                 = ui_quick_menu_radius() * 1.1;
	f32 left              = sys_x() + r;
	f32 right             = math_max(left, sys_x() + sys_w() - r);
	ui_quick_menu_x       = math_max(left, math_min(right, x));
	ui_quick_menu_y       = math_max(r, math_min(iron_window_height() - r, y));
	ui_quick_menu_show    = true;
	ui_quick_menu_hover   = -1;
	ui_quick_menu_pressed = -1;
	ui_quick_menu_drag    = QUICK_MENU_NONE;
	ui_menu_show          = false;
	iron_haptic_tick(ui_quick_menu_x, ui_quick_menu_y);
	render_path_base_redraw();
}

void ui_quick_menu_close() {
	ui_quick_menu_show = false;
	ui_quick_menu_drag = QUICK_MENU_NONE;
	render_path_base_redraw();
}

// Painting waits while the menu is up, and for the press that closed it
bool ui_quick_menu_blocking() {
	return ui_quick_menu_show || ui_quick_menu_swallow;
}

static i32 ui_quick_menu_slot_at(f32 px, f32 py) {
	f32 dx = px - ui_quick_menu_x;
	f32 dy = py - ui_quick_menu_y;
	f32 d  = math_sqrt(dx * dx + dy * dy);
	if (d < ui_quick_menu_inner() || d > ui_quick_menu_radius() * 1.3) {
		return -1;
	}
	f32 a = math_atan2(dx, -dy); // 0 at the top, clockwise
	if (a < 0.0) {
		a += math_pi() * 2.0;
	}
	return ((i32)math_floor((a + math_pi() / QUICK_MENU_SLOTS) / (math_pi() * 2.0 / QUICK_MENU_SLOTS))) % QUICK_MENU_SLOTS;
}

static void ui_quick_menu_run(i32 item) {
	switch (item) {
	case QUICK_MENU_BRUSH:
		context_select_tool(TOOL_TYPE_BRUSH);
		break;
	case QUICK_MENU_ERASER:
		context_select_tool(TOOL_TYPE_ERASER);
		break;
	case QUICK_MENU_PICKER:
		context_select_tool(TOOL_TYPE_PICKER);
		break;
	case QUICK_MENU_FILL:
		context_select_tool(TOOL_TYPE_FILL);
		break;
	case QUICK_MENU_CLONE:
		context_select_tool(TOOL_TYPE_CLONE);
		break;
	case QUICK_MENU_BLUR:
		context_select_tool(TOOL_TYPE_BLUR);
		break;
	case QUICK_MENU_UNDO:
		history_undo();
		break;
	case QUICK_MENU_REDO:
		history_redo();
		break;
	case QUICK_MENU_RECENT_BRUSH_1:
	case QUICK_MENU_RECENT_BRUSH_2:
	case QUICK_MENU_RECENT_BRUSH_3:
		brush_library_use(brush_library_find(ui_quick_menu_recent[item - QUICK_MENU_RECENT_BRUSH_1]));
		break;
	default:
		break;
	}
	ui_toolbar_handle->redraws = 2;
	ui_header_handle->redraws  = 2;
}

void ui_quick_menu_update() {
	bool down     = mouse_down("left") || pen_down("tip");
	bool started  = mouse_started("left") || pen_started("tip");
	bool released = mouse_released("left") || pen_released("tip");

	if (ui_quick_menu_swallow) {
		if (!down) {
			ui_quick_menu_swallow = false;
		}
		return;
	}
	if (!ui_quick_menu_show) {
		return;
	}

	if (keyboard_started("escape")) {
		ui_quick_menu_close();
		return;
	}

	// Size and opacity follow the sideways drag
	if (ui_quick_menu_drag != QUICK_MENU_NONE) {
		f32 delta = (mouse_x - ui_quick_menu_drag_x) / (300.0 * UI_SCALE());
		if (ui_quick_menu_drag == QUICK_MENU_SIZE) {
			g_context->brush_radius = math_max(0.01, math_min(2.0, ui_quick_menu_drag_value * math_pow(4.0, delta)));
		}
		else {
			g_context->brush_opacity = math_max(0.0, math_min(1.0, ui_quick_menu_drag_value + delta));
		}
		ui_header_handle->redraws = 2;
		render_path_base_redraw();
		if (!down) {
			ui_quick_menu_close();
		}
		return;
	}

	i32 slot = ui_quick_menu_slot_at(mouse_x, mouse_y);
	if (slot != ui_quick_menu_hover) {
		ui_quick_menu_hover = slot;
		render_path_base_redraw();
	}

	if (started) {
		i32 item = ui_quick_menu_item(slot);
		if (slot < 0 || !ui_quick_menu_item_enabled(item)) {
			ui_quick_menu_swallow = true; // Outside or on nothing: closes, the press does not paint
			ui_quick_menu_close();
			return;
		}
		if (item == QUICK_MENU_SIZE || item == QUICK_MENU_OPACITY) {
			ui_quick_menu_drag       = item;
			ui_quick_menu_drag_x     = mouse_x;
			ui_quick_menu_drag_value = item == QUICK_MENU_SIZE ? g_context->brush_radius : g_context->brush_opacity;
			return;
		}
		ui_quick_menu_pressed = slot;
	}
	if (released && ui_quick_menu_pressed >= 0) {
		if (slot == ui_quick_menu_pressed) {
			ui_quick_menu_run(ui_quick_menu_item(slot));
		}
		ui_quick_menu_close();
	}
}

void ui_quick_menu_render() {
	if (!ui_quick_menu_show) {
		return;
	}
	f32 r     = ui_quick_menu_radius();
	f32 inner = ui_quick_menu_inner();
	f32 cx    = ui_quick_menu_x;
	f32 cy    = ui_quick_menu_y;

	draw_begin(NULL, false, 0);
	draw_set_color(0xe0202020);
	draw_filled_circle(cx, cy, r, 64);
	draw_set_color(g_theme->SEPARATOR_COL | 0xff000000);
	draw_circle(cx, cy, r, 64, 2.0);
	draw_circle(cx, cy, inner, 48, 2.0);

	i32 font_size = g_ui->font_size;
	draw_set_font(g_ui->ops->font, font_size);
	for (i32 i = 0; i < QUICK_MENU_SLOTS; ++i) {
		i32 item    = ui_quick_menu_item(i);
		f32 a       = i * math_pi() * 2.0 / QUICK_MENU_SLOTS;
		f32 lx      = cx + math_sin(a) * (inner + r) * 0.5;
		f32 ly      = cy - math_cos(a) * (inner + r) * 0.5;
		bool active = ui_quick_menu_item_active(item);
		bool hover  = i == ui_quick_menu_hover || (ui_quick_menu_drag != QUICK_MENU_NONE && item == ui_quick_menu_drag);
		if (hover || active) {
			draw_set_color(hover ? g_theme->HIGHLIGHT_COL : g_theme->BUTTON_COL);
			draw_filled_circle(lx, ly, (r - inner) * 0.42, 32);
		}
		// Dividers between the items
		f32 b = a + math_pi() / QUICK_MENU_SLOTS;
		draw_set_color(g_theme->SEPARATOR_COL | 0xff000000);
		draw_line(cx + math_sin(b) * inner, cy - math_cos(b) * inner, cx + math_sin(b) * r, cy - math_cos(b) * r, 1.0);

		char *label = ui_quick_menu_item_label(item);
		if (item == QUICK_MENU_NONE || label[0] == '\0') {
			continue;
		}
		draw_set_color(ui_quick_menu_item_enabled(item) ? 0xffe8e8e8 : 0xff707070);
		f32 tw = draw_string_width(g_ui->ops->font, font_size, label);
		f32 maxw = (r - inner) * 0.95;
		if (tw > maxw) { // Long brush names get a smaller font
			i32 small = (i32)(font_size * maxw / tw);
			draw_set_font(g_ui->ops->font, small);
			tw = draw_string_width(g_ui->ops->font, small, label);
			draw_string(label, lx - tw / 2.0, ly - small / 2.0);
			draw_set_font(g_ui->ops->font, font_size);
		}
		else {
			draw_string(label, lx - tw / 2.0, ly - font_size / 2.0);
		}
	}
	draw_set_color(0xffffffff);
	draw_end();
}
