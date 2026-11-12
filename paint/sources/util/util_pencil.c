
#include "../global.h"

// Apple Pencil double tap and squeeze. The gesture arrives with the input events and runs with the next update.

static i32 util_pencil_pending       = -1; // Gesture waiting for the update
static f32 util_pencil_pending_x     = -1.0;
static f32 util_pencil_pending_y     = -1.0;
static i32 util_pencil_tool_seen     = -1; // Tool of the last update
static i32 util_pencil_tool_previous = -1; // Tool before it
static i32 util_pencil_toggle_from   = -1; // Tool to go back to after toggling the eraser or picker

void util_pencil_on_gesture(int gesture, float x, float y) {
	util_pencil_pending   = gesture;
	util_pencil_pending_x = x;
	util_pencil_pending_y = y;
}

// The action of a gesture, the system setting resolved
static i32 util_pencil_action(i32 gesture) {
	i32 action = gesture == IRON_PENCIL_GESTURE_SQUEEZE ? g_config->pencil_squeeze : g_config->pencil_double_tap;
	if (action != PENCIL_ACTION_SYSTEM) {
		return action;
	}
	switch (iron_pencil_preferred_action(gesture)) {
	case IRON_PENCIL_PREFERS_SWITCH_ERASER:
		return PENCIL_ACTION_TOGGLE_ERASER;
	case IRON_PENCIL_PREFERS_SWITCH_PREVIOUS:
		return PENCIL_ACTION_PREVIOUS_TOOL;
	case IRON_PENCIL_PREFERS_COLOR_PALETTE:
		return PENCIL_ACTION_COLOR_PICKER;
	case IRON_PENCIL_PREFERS_INK_ATTRIBUTES: // Brush size and opacity are in the quick menu
	case IRON_PENCIL_PREFERS_CONTEXTUAL_PALETTE:
		return PENCIL_ACTION_QUICK_MENU;
	default:
		return PENCIL_ACTION_OFF;
	}
}

static void util_pencil_toggle(i32 tool) {
	if (g_context->tool == tool) {
		i32 back = util_pencil_toggle_from >= 0 && util_pencil_toggle_from != tool ? util_pencil_toggle_from : TOOL_TYPE_BRUSH;
		context_select_tool(back);
	}
	else {
		util_pencil_toggle_from = g_context->tool;
		context_select_tool(tool);
	}
}

static void util_pencil_run(i32 gesture, f32 x, f32 y) {
	switch (util_pencil_action(gesture)) {
	case PENCIL_ACTION_TOGGLE_ERASER:
		util_pencil_toggle(TOOL_TYPE_ERASER);
		break;
	case PENCIL_ACTION_COLOR_PICKER:
		if (ui_toolbar_tool_visible(TOOL_TYPE_PICKER)) {
			util_pencil_toggle(TOOL_TYPE_PICKER);
		}
		break;
	case PENCIL_ACTION_PREVIOUS_TOOL:
		if (util_pencil_tool_previous >= 0) {
			context_select_tool(util_pencil_tool_previous);
		}
		break;
	case PENCIL_ACTION_UNDO:
		history_undo();
		break;
	case PENCIL_ACTION_QUICK_MENU:
		if (ui_quick_menu_show) {
			ui_quick_menu_close();
		}
		else {
			ui_quick_menu_open(x, y);
		}
		break;
	default:
		return;
	}
	ui_toolbar_handle->redraws = 2;
	ui_header_handle->redraws  = 2;
}

void util_pencil_update() {
	if (g_context->tool != util_pencil_tool_seen) {
		util_pencil_tool_previous = util_pencil_tool_seen;
		util_pencil_tool_seen     = g_context->tool;
	}
	if (util_pencil_pending >= 0) {
		i32 gesture         = util_pencil_pending;
		util_pencil_pending = -1;
		if (!ui_box_show && !g_ui->is_typing) {
			util_pencil_run(gesture, util_pencil_pending_x, util_pencil_pending_y);
		}
	}
}
