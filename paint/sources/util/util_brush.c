
#include "../global.h"

static bool util_brush_clone_source_down = false;

#if defined(IRON_IOS) || defined(IRON_ANDROID)
// A finger stroke starts right away, a second finger landing shortly after (pinch, two finger drag) undoes it
#define UTIL_BRUSH_FINGER_CANCEL 0.35
static f64             util_brush_finger_time    = 0;
static bool            util_brush_finger_blocked = false;
static history_step_t *util_brush_finger_step    = NULL; // Last history step before the finger stroke
static bool            util_brush_finger_stroke  = false;

// Returns false once a finger touch turned into a two finger gesture
static bool util_brush_finger_allowed(bool paint_down) {
	if (pen_down("tip") || pen_in_use) {
		return true;
	}
	if (mouse_started("left")) {
		util_brush_finger_time    = sys_time();
		util_brush_finger_blocked = false;
	}
	if (mouse_started("right") || mouse_down("right")) {
		// Second finger: undo the stroke the first one just started
		if (util_brush_finger_stroke && sys_time() - util_brush_finger_time < UTIL_BRUSH_FINGER_CANCEL) {
			history_step_t *last = history_steps->length > 0 ? history_steps->buffer[history_steps->length - 1] : NULL;
			if (last != NULL && last != util_brush_finger_step && last->action == HISTORY_ACTION_PAINT && history_undos > 0) {
				history_undo();
			}
		}
		util_brush_finger_stroke  = false;
		util_brush_finger_blocked = true;
	}
	if (!paint_down) {
		util_brush_finger_stroke = false;
		return true;
	}
	if (util_brush_finger_blocked) {
		return false;
	}
	if (g_context->brush_time == 0 && !util_brush_finger_stroke) {
		util_brush_finger_stroke = true;
		util_brush_finger_step   = history_steps->length > 0 ? history_steps->buffer[history_steps->length - 1] : NULL;
	}
	return true;
}
#endif

void util_brush_update() {
	bool paint_down = keymap_shortcut(any_map_get(g_keymap, "action_paint"), SHORTCUT_TYPE_DOWN);
#if defined(IRON_IOS) || defined(IRON_ANDROID)
	if (!util_brush_finger_allowed(paint_down)) {
		paint_down = false;
	}
#endif

	bool set_clone_source =
	    g_context->tool == TOOL_TYPE_CLONE &&
	    (keymap_shortcut(string_tmp("%s+%s", any_map_get(g_keymap, "set_clone_source"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN) ||
	     (g_context->clone_set_source && (paint_down || pen_down("tip"))));

	bool decal_mask = context_is_decal_mask_paint();

	bool down = paint_down || decal_mask || set_clone_source ||
	            keymap_shortcut(string_tmp("%s+%s", any_map_get(g_keymap, "brush_ruler"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN) ||
	            (pen_down("tip") && !keyboard_down("alt") && !string_equals(any_map_get(g_keymap, "action_paint"), "")); // "" while a touch rotates

	if (g_config->touch_ui) {
		if (pen_down("tip")) {
			g_context->pen_painting_only = true;
		}
		else if (g_context->pen_painting_only) {
			down = false;
		}
	}

	if (g_context->tool == TOOL_TYPE_PARTICLE) {
		down = false;
	}

	if (g_context->tool == TOOL_TYPE_SELECT || util_tool_is_object_tool(g_context->tool)) {
		down = false;
	}

	if (down) {
		i32 mx = mouse_x - base_paint_view_x();
		i32 my = mouse_y - base_paint_view_y();
		i32 ww = base_paint_view_w();
		i32 wh = base_paint_view_h();

		if (mx < ww && mx > 0 && my < wh && my > 0) {

			if (set_clone_source) {
				g_context->clone_start_x     = mx;
				g_context->clone_start_y     = my;
				util_brush_clone_source_down = true;
			}
			else {
				if (g_context->brush_time == 0 && !base_is_dragging && !base_is_resizing && !ui_dock_is_dragging() &&
				    g_ui->combo_selected_id == 0) { // Paint started

					// Draw line
					if (keymap_shortcut(string_tmp("%s+%s", any_map_get(g_keymap, "brush_ruler"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN)) {
						g_context->last_paint_vec_x = g_context->last_paint_x;
						g_context->last_paint_vec_y = g_context->last_paint_y;
					}

					history_push_undo = g_context->tool < TOOL_TYPE_PLUGIN; // Plugin tools do not paint layers

					if (g_context->tool == TOOL_TYPE_CLONE && g_context->clone_start_x >= 0.0) { // Clone delta
						g_context->clone_delta_x = (g_context->clone_start_x - mx) / (float)ww;
						g_context->clone_delta_y = (g_context->clone_start_y - my) / (float)wh;
						g_context->clone_start_x = -1;
					}
					else if (g_context->tool == TOOL_TYPE_FILL && g_context->fill_type == FILL_TYPE_UV_ISLAND) {
						util_uv_uvislandmap_cached = false;
					}
					else if (g_context->tool == TOOL_TYPE_FILL && (g_context->fill_type == FILL_TYPE_MESH || g_context->fill_type == FILL_TYPE_SUBMESH)) {
						render_path_paint_fill_pick = true;
					}
				}

				g_context->brush_time += sys_delta();
				if (g_context->tool >= TOOL_TYPE_PLUGIN) {
					plugin_tool_t *t = plugin_tool_get(g_context->tool);
					plugin_tool_call(t, t->on_stroke);
				}
				else {
					brush_output_node_run();
				}
			}
		}
	}
	else if (g_context->brush_time > 0) { // Brush released
		g_context->brush_time          = 0;
		g_context->prev_paint_vec_x    = -1;
		g_context->prev_paint_vec_y    = -1;
		g_context->brush_blend_dirty   = true; // Update brush mask
		g_context->layer_preview_dirty = true; // Update layer preview

		if (g_context->tool >= TOOL_TYPE_PLUGIN) {
			plugin_tool_t *t = plugin_tool_get(g_context->tool);
			plugin_tool_call(t, t->on_stroke_end);
		}

		// New color id picked, update fill layer
		if (g_context->tool == TOOL_TYPE_COLORID && g_context->layer->fill_material != NULL) {
			sys_notify_on_next_frame(&ui_base_update_ui_on_next_frame, NULL);
		}
	}

	if (util_brush_clone_source_down && !paint_down && !pen_down("tip")) {
		util_brush_clone_source_down = false;
		g_context->clone_set_source  = false;
		ui_header_handle->redraws    = 2;
	}
}
