
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

// Pen pressure through the curve of the preferences
f32 util_brush_pen_pressure() {
	f32 p = util_brush_sample_pressure >= 0.0f ? util_brush_sample_pressure : pen_pressure;
	p     = p < 0.0f ? 0.0f : p > 1.0f ? 1.0f : p;
	f32 c = g_config->pressure_curve > 0.01f ? g_config->pressure_curve : 1.0f;
	p     = powf(p, c);
	p     = g_config->pressure_min + (1.0f - g_config->pressure_min) * p;
	return p * g_config->pressure_sensitivity;
}

// Factor applied to a brush property by its dynamics source, 1 when off
f32 util_brush_dynamic(i32 source) {
	if (slot_layer_is_path(g_context->layer)) {
		return 1.0f;
	}
	if (source == 1) { // Pen pressure
		return (pen_down("tip") || pen_released("tip")) ? util_brush_pen_pressure() : 1.0f;
	}
	if (source == 2) { // Fast strokes paint thinner and lighter
		return g_config->pressure_min + (1.0f - g_config->pressure_min) * (1.0f - g_context->brush_speed);
	}
	return 1.0f;
}

// Stroke speed and direction for the dynamics
static f32 util_brush_last_x = -1.0f;
static f32 util_brush_last_y = -1.0f;

static void util_brush_update_motion() {
	f32 x = g_context->paint_vec.x;
	f32 y = g_context->paint_vec.y;
	if (g_context->brush_time == 0 || util_brush_last_x < 0.0f) {
		util_brush_last_x     = x;
		util_brush_last_y     = y;
		g_context->brush_speed = 0.0f;
		return;
	}
	f32 aspect = base_paint_view_w() / (f32)base_paint_view_h();
	f32 dx     = (x - util_brush_last_x) * aspect;
	f32 dy     = y - util_brush_last_y;
	f32 d      = sqrtf(dx * dx + dy * dy);
	f32 dt     = sys_delta() > 0.0f ? sys_delta() : 1.0f / 60.0f;
	f32 s      = d / dt / 3.0f; // Three screen heights a second is full speed
	s          = s > 1.0f ? 1.0f : s;
	g_context->brush_speed += (s - g_context->brush_speed) * 0.3f;
	if (d > 0.002f) {
		g_context->brush_stroke_angle = atan2f(-dy, dx);
		util_brush_last_x             = x;
		util_brush_last_y             = y;
	}
}

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
		else if (context_finger_paint_blocked()) {
			down = false;
		}
	}

	if (g_context->tool == TOOL_TYPE_PARTICLE) {
		down = false;
	}

	// A pencil stroke starts with its first sample, whose force is final, instead of the estimated force of the touch
	static bool util_brush_pen_ready = false;
	if (pen_down("tip")) {
		if (pen_sample_count > 0) {
			util_brush_pen_ready = true;
		}
		if (!util_brush_pen_ready && g_context->brush_time == 0) {
			down = false;
		}
	}
	else if (!pen_released("tip")) {
		util_brush_pen_ready = false;
	}

	if (g_context->stencil_edit && context_is_stencil_editing()) { // Touches move the stencil
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

					// First stamp of the stroke
					g_context->brush_dab_x = -1.0;
					g_context->brush_dab_y = -1.0;
					render_path_paint_samples_reset();

					// Draw line
					if (context_is_ruler_paint()) {
						g_context->last_paint_vec_x = g_context->last_paint_x;
						g_context->last_paint_vec_y = g_context->last_paint_y;
						if (g_context->last_paint_x >= 0.0) { // Stamps continue from the end of the previous stroke
							g_context->brush_dab_x = g_context->last_paint_x;
							g_context->brush_dab_y = g_context->last_paint_y;
						}
					}

					history_push_undo = g_context->tool != TOOL_TYPE_INPAINT; // Inpaint repairs baked textures, not layers

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

				util_brush_update_motion();
				g_context->brush_time += sys_delta();
				if (g_context->tool == TOOL_TYPE_INPAINT) {
					box_bake_maps_inpaint_stroke();
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
		g_context->brush_dab_x         = -1;
		g_context->brush_dab_y         = -1;
		g_context->brush_blend_dirty   = true; // Update brush mask
		g_context->layer_preview_dirty = true; // Update layer preview

		if (g_context->tool == TOOL_TYPE_INPAINT) {
			box_bake_maps_inpaint_stroke_end();
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
