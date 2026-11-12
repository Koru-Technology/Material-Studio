
#include "../global.h"

bool                 ui_view2d_text_input_hover = false;
bool                 ui_view2d_uvmap_show       = false;
paint_tex_t          ui_view2d_tex_type         = PAINT_TEX_BASE;
view_2d_layer_mode_t ui_view2d_layer_mode       = VIEW_2D_LAYER_MODE_SELECTED;
bool                 ui_view2d_controls_down    = false;
gpu_texture_t       *_ui_view2d_render_tex;
f32                  _ui_view2d_render_x;
f32                  _ui_view2d_render_y;
f32                  _ui_view2d_render_tw;
f32                  _ui_view2d_render_th;
i32                  _ui_view2d_tex_w          = 0;
bool                 ui_view2d_layer_touched   = false;
gpu_texture_t       *ui_view2d_tex             = NULL;
i32                  ui_view2d_tex_channel     = 0;
bool                 ui_view2d_tex_bgra        = false;
char                *ui_view2d_layer_name_prev = NULL;

void ui_view2d_init() {
	ui_view2d_pipe = gpu_create_pipeline();

	ui_view2d_pipe->vertex_shader   = sys_get_shader("layer_view.vert");
	ui_view2d_pipe->fragment_shader = sys_get_shader("layer_view.frag");
	gpu_vertex_structure_t *vs      = ALLOC_INIT(gpu_vertex_structure_t, {0});
	gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_F32_2X);
	gpu_vertex_structure_add(vs, "tex", GPU_VERTEX_DATA_F32_2X);
	gpu_vertex_structure_add(vs, "col", GPU_VERTEX_DATA_F32_4X);
	ui_view2d_pipe->input_layout              = vs;
	ui_view2d_pipe->blend_source              = GPU_BLEND_ONE;
	ui_view2d_pipe->blend_destination         = GPU_BLEND_ZERO;
	ui_view2d_pipe->color_write_mask_alpha[0] = false;
	gpu_pipeline_compile(ui_view2d_pipe);
	pipes_offset = 0;
	pipes_get_constant_location("float4"); // empty
	ui_view2d_channel_loc = pipes_get_constant_location("int");
}

void ui_view2d_capture_output(void *_) {
	if (ui_view2d_tex == NULL || ui_view2d_tex_channel == 0) {
		util_texture_capture_output(ui_view2d_tex, "tex_capture", ui_view2d_tex_bgra);
		return;
	}
	// Single channel views carry no usable alpha, bake the displayed channel into an opaque image
	gpu_texture_t *rt = gpu_create_render_target(ui_view2d_tex->width, ui_view2d_tex->height, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(rt, true, 0xff000000);
	draw_set_pipeline(ui_view2d_pipe);
	gpu_set_int(ui_view2d_channel_loc, ui_view2d_tex_channel);
	draw_scaled_image(ui_view2d_tex, 0, 0, rt->width, rt->height);
	draw_end();
	draw_set_pipeline(NULL);
	util_texture_capture_output(rt, "tex_capture", false);
	gpu_delete_texture(rt);
}

ui_node_t *ui_view2d_get_selected_node() {
	ui_nodes_t *nodes = ui_nodes_get_nodes();
	if (nodes->nodes_selected_id->length == 0) {
		return NULL;
	}
	ui_node_canvas_t *c = ui_nodes_get_canvas(true);
	return ui_get_node(c->nodes, nodes->nodes_selected_id->buffer[0]);
}

void ui_view2d_draw_edit() {
	if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
		ui_check(&ui_view2d_uvmap_show, tr("UV Map"), "");
		if (ui_item_changed()) {
			ui_view2d_hwnd->redraws = 2;
			ui_menu_keep_open       = true;
		}
	}

	ui_check(&ui_view2d_tiled_show, tr("Tiled"), "");
	if (ui_item_changed()) {
		ui_view2d_hwnd->redraws = 2;
		ui_menu_keep_open       = true;
	}

	ui_menu_separator();

	ui_check(&g_config->view2d_grid_snap, tr("Grid Snap"), any_map_get(g_keymap, "grid_snap"));
	if (ui_item_changed()) {
		ui_menu_keep_open = true;
	}

	ui_check(&g_config->view2d_grid_show, tr("Show Grid"), "");
	if (ui_item_changed()) {
		ui_view2d_hwnd->redraws = 2;
		ui_menu_keep_open       = true;
	}

	ui_slider_int(&g_config->view2d_grid_cell, tr("Grid Cell"), 1.0, 256.0, true, true, UI_ALIGN_RIGHT, true);
	bool cell_changed = ui_item_changed();
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Cell size in pixels"));
	}
	if (cell_changed) {
		ui_view2d_hwnd->redraws = 2;
		ui_menu_keep_open       = true;
	}

	ui_menu_separator();

	if (ui_menu_button(tr("Zoom to Fit"), "", ICON_NONE)) {
		ui_view2d_pan_x         = 0.0;
		ui_view2d_pan_y         = 0.0;
		ui_view2d_pan_scale     = 1.0;
		ui_view2d_hwnd->redraws = 2;
	}

	g_ui->enabled = ui_view2d_tex != NULL;
	if (ui_menu_button(tr("Capture Output"), "", ICON_PHOTO)) {
		sys_notify_on_next_frame(&ui_view2d_capture_output, NULL);
	}
	g_ui->enabled = true;

	if (ui_view2d_tex != NULL) {
		ui_menu_separator();
		g_ui->enabled = false;
		ui_text(string_tmp("%dx%d", ui_view2d_tex->width, ui_view2d_tex->height), UI_ALIGN_LEFT, 0x00000000);
		if (ui_view2d_type == VIEW_2D_TYPE_ASSET) {
			asset_t *asset     = g_context->texture;
			bool     is_packed = g_project->packed_assets != NULL && project_packed_asset_exists(g_project->packed_assets, asset->file);
			if (is_packed) {
				ui_text(tr("packed"), UI_ALIGN_LEFT, 0x00000000);
			}
		}
		g_ui->enabled = true;
	}

	char *view_type = ui_view2d_type == VIEW_2D_TYPE_ASSET  ? tr("Asset")
	                  : ui_view2d_type == VIEW_2D_TYPE_NODE ? tr("Node")
	                  : ui_view2d_type == VIEW_2D_TYPE_FONT ? tr("Font")
	                                                        : tr("Layer");

	if (ui_view2d_type == VIEW_2D_TYPE_NODE) {
		ui_node_t *sel = ui_view2d_get_selected_node();
		if (sel != NULL) {
			view_type = string_tmp("%s %s", sel->type, view_type);
		}
	}

	g_ui->enabled = false;
	ui_text(view_type, UI_ALIGN_LEFT, 0x00000000);
	g_ui->enabled = true;
}

// Twisting with two fingers turns the layer while painting. The uv map and pixel snapping work on the upright image,
// the view turns back for them.
bool ui_view2d_rotation_allowed() {
	return ui_view2d_type == VIEW_2D_TYPE_LAYER && !ui_view2d_uvmap_show && !context_is_pixel_snap() && !g_config->view2d_grid_snap;
}

// Center of the shown image the rotation turns around, relative to the window
static f32 ui_view2d_rotation_cx = 0.0;
static f32 ui_view2d_rotation_cy = 0.0;

void ui_view2d_draw_image(gpu_texture_t *image, f32 dx, f32 dy, f32 dw, f32 dh, i32 channel) {
	if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
		gpu_set_int(ui_view2d_channel_loc, channel);
	}
	if (ui_view2d_rotation != 0.0) {
		draw_rotated_scaled_image(image, dx, dy, dw, dh, ui_view2d_rotation_cx, ui_view2d_rotation_cy, ui_view2d_rotation);
		return;
	}
	draw_scaled_image(image, dx, dy, dw, dh);
}

void ui_view2d_render_color_pick(void *_) {
	render_target_t *rt              = any_map_get(render_path_render_targets, "texpaint_picker");
	gpu_texture_t   *texpaint_picker = rt->_image;
	draw_begin(texpaint_picker, false, 0);
	draw_scaled_image(_ui_view2d_render_tex, -_ui_view2d_render_x, -_ui_view2d_render_y, _ui_view2d_render_tw, _ui_view2d_render_th);
	draw_end();
	buffer_t *a = gpu_get_texture_pixels(texpaint_picker);
#ifdef IRON_BGRA
	i32 i0 = 2;
	i32 i1 = 1;
	i32 i2 = 0;
#else
	i32 i0 = 0;
	i32 i1 = 1;
	i32 i2 = 2;
#endif

	g_context->picked_color->base = color_set_rb(g_context->picked_color->base, buffer_get_u8(a, i0));
	g_context->picked_color->base = color_set_gb(g_context->picked_color->base, buffer_get_u8(a, i1));
	g_context->picked_color->base = color_set_bb(g_context->picked_color->base, buffer_get_u8(a, i2));
	ui_header_handle->redraws     = 2;

	if (g_context->color_picker_callback != NULL) {
		g_context->color_picker_callback(g_context->picked_color);
	}
	else {
		tab_materials_apply_picked_color(g_context->picked_color);
	}
}

// Height of the tab names and the header drawn on top of the image, the image is centered in the area below
i32 ui_view2d_top() {
	return ui_base_show && !ui_popovers_enabled() ? ui_header_h * 2 : 0;
}

// Rect of the shown image relative to the window, the layer plane fills it when painting
void ui_view2d_tex_rect(f32 aspect, f32 *x, f32 *y, f32 *w, f32 *h) {
	f32 wm = fmin(ui_view2d_ww, ui_view2d_wh);
	*w     = wm * 0.9 * ui_view2d_pan_scale;
	*h     = *w * aspect;
	*x     = ui_view2d_ww / 2.0 - *w / 2.0 + ui_view2d_pan_x;
	*y     = (ui_view2d_wh + ui_view2d_top()) / 2.0 - *h / 2.0 + ui_view2d_pan_y;
}

// Pixels per world unit, the layer plane is 2 units wide like the default 3D mesh to share the brush radius
f32 ui_view2d_world_scale() {
	return fmin(ui_view2d_ww, ui_view2d_wh) * 0.9 * ui_view2d_pan_scale / 2.0;
}

// Cursor tool drag of the selected layer
static slot_layer_t *_ui_view2d_move_layer = NULL;
static f32           _ui_view2d_move_x     = 0.0; // Layer pixels
static f32           _ui_view2d_move_y     = 0.0;
static bool          _ui_view2d_moving     = false;
static bool          _ui_view2d_move_apply = false;

static bool ui_view2d_layer_movable(slot_layer_t *l) {
	return (slot_layer_is_layer(l) || slot_layer_is_mask(l)) && l->fill_material == NULL;
}

// Shift the texture contents, pixels moved out of the texture are discarded
static void ui_view2d_shift_texture(gpu_texture_t *t, gpu_pipeline_t *pipe, i32 dx, i32 dy) {
	gpu_pipeline_t *temp_pipe = base_bits == TEXTURE_BITS_BITS8 ? pipes_copy : base_bits == TEXTURE_BITS_BITS16 ? pipes_copy64 : pipes_copy128;
	draw_begin(layers_temp_image, false, 0);
	draw_set_pipeline(temp_pipe);
	draw_image(t, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();

	draw_begin(t, true, 0x00000000);
	draw_set_pipeline(pipe);
	draw_image(layers_temp_image, dx, dy);
	draw_set_pipeline(NULL);
	draw_end();
}

static void ui_view2d_layer_move_apply_on_next_frame(void *_) {
	slot_layer_t *l       = _ui_view2d_move_layer;
	i32           dx      = math_round(_ui_view2d_move_x);
	i32           dy      = math_round(_ui_view2d_move_y);
	_ui_view2d_move_layer = NULL;
	_ui_view2d_move_x     = 0.0;
	_ui_view2d_move_y     = 0.0;
	_ui_view2d_move_apply = false;
	if (l == NULL || (dx == 0 && dy == 0) || array_index_of((any_array_t *)g_project->_->layers, l) == -1) {
		return;
	}

	slot_layer_t *_layer = g_context->layer;
	g_context->layer     = l;
	if (history_undo_layers != NULL) {
		history_paint();
		history_steps->buffer[history_steps->length - 1]->name = tr("Move");
	}
	g_context->layer = _layer;

	layers_make_temp_img();
	if (slot_layer_is_mask(l)) {
		ui_view2d_shift_texture(l->texpaint, pipes_copy8, dx, dy);
	}
	else {
		gpu_pipeline_t *pipe = base_bits == TEXTURE_BITS_BITS8 ? pipes_copy : base_bits == TEXTURE_BITS_BITS16 ? pipes_copy64 : pipes_copy128;
		ui_view2d_shift_texture(l->texpaint, pipe, dx, dy);
		ui_view2d_shift_texture(l->texpaint_nor, pipe, dx, dy);
		ui_view2d_shift_texture(l->texpaint_pack, pipe, dx, dy);
	}

	g_context->layer_preview_dirty = true;
	g_context->ddirty              = 2;
	ui_view2d_hwnd->redraws        = 2;
}

static void ui_view2d_layer_move_update() {
	if (_ui_view2d_move_layer == NULL || _ui_view2d_move_apply) {
		return;
	}
	if (g_context->tool != TOOL_TYPE_CURSOR || g_context->layer != _ui_view2d_move_layer) {
		// The drawn offset stays until the pixels are moved
		_ui_view2d_moving     = false;
		_ui_view2d_move_apply = true;
		sys_notify_on_next_frame(&ui_view2d_layer_move_apply_on_next_frame, NULL);
		return;
	}
	if (_ui_view2d_moving) {
		if (mouse_down("left")) {
			f32 tx, ty, tw, th;
			ui_view2d_tex_rect(config_get_texture_res_y() / (float)config_get_texture_res_x(), &tx, &ty, &tw, &th);
			_ui_view2d_move_x += mouse_movement_x / tw * config_get_texture_res_x();
			_ui_view2d_move_y += mouse_movement_y / th * config_get_texture_res_y();
			ui_view2d_hwnd->redraws = 2;
		}
		else {
			_ui_view2d_moving = false;
		}
	}
}

// Layer with a pending move and its offset in layer pixels, NULL when there is none
slot_layer_t *ui_view2d_move_layer(i32 *dx, i32 *dy) {
	*dx = math_round(_ui_view2d_move_x);
	*dy = math_round(_ui_view2d_move_y);
	return *dx != 0 || *dy != 0 ? _ui_view2d_move_layer : NULL;
}

// Pending move offset of the layer drawn in the 2D view, in screen pixels
static void ui_view2d_layer_move_offset(slot_layer_t *l, f32 tw, f32 th, f32 *ox, f32 *oy) {
	*ox = 0.0;
	*oy = 0.0;
	if (l == _ui_view2d_move_layer && l != NULL) {
		*ox = math_round(_ui_view2d_move_x) / (float)config_get_texture_res_x() * tw;
		*oy = math_round(_ui_view2d_move_y) / (float)config_get_texture_res_y() * th;
	}
}

void ui_view2d_update(void *_) {
	f32 headerh = UI_ELEMENT_H() * 1.4;

	ui_view2d_layer_move_update();

	g_context->paint2d = false;

	// 2D view takes the tab it is docked in
	ui_dock_layout();
	if (!ui_base_show || ui_popovers_enabled()) {
		headerh = 0;
	}

	ui_nodes_wrap_mouse(ui_view2d_controls_down, ui_view2d_wx, ui_view2d_wy, ui_view2d_ww, ui_view2d_wh);

	// Dragging the split keeps the image the same size on screen, the fit to the view would scale it
	static f32 last_wm = 0.0;
	f32        wm      = fmin(ui_view2d_ww, ui_view2d_wh);
	if (base_is_resizing && last_wm > 0.0 && wm > 0.0 && wm != last_wm) {
		ui_view2d_pan_scale *= last_wm / wm;
	}
	last_wm = wm;

	if (!base_ui_enabled || !ui_view2d_show || base_is_resizing || mouse_x < ui_view2d_wx || mouse_x > ui_view2d_wx + ui_view2d_ww || mouse_y < ui_view2d_wy + headerh ||
	    mouse_y > ui_view2d_wy + ui_view2d_wh) {
		if (ui_view2d_controls_down) {
			ui_canvas_control_t *control = ui_nodes_get_canvas_control(ui_view2d_controls_down, false);
			ui_view2d_controls_down      = control->controls_down;
		}
	}
	else {

		ui_canvas_control_t *control = ui_nodes_get_canvas_control(ui_view2d_controls_down, false);
		ui_view2d_pan_x += control->pan_x;
		ui_view2d_pan_y += control->pan_y;
		ui_view2d_controls_down = control->controls_down;
		if (control->zoom != 0.0) {
			f32 old_scale = ui_view2d_pan_scale;
			f32 max_scale = _ui_view2d_tex_w > 0 ? fmax(16.0 * _ui_view2d_tex_w / (fmin(ui_view2d_ww, ui_view2d_wh) * 0.9), old_scale) : 6.0;
			ui_view2d_pan_scale *= math_exp(control->zoom);
			if (ui_view2d_pan_scale < 0.1) {
				ui_view2d_pan_scale = 0.1;
			}
			if (ui_view2d_pan_scale > max_scale) {
				ui_view2d_pan_scale = max_scale;
			}
			f32 ratio = ui_view2d_pan_scale / old_scale;
			ui_view2d_pan_x *= ratio;
			ui_view2d_pan_y *= ratio;

			if (ui_touch_control) {
				// Zoom to finger location
				ui_view2d_pan_x -= (g_ui->input_x - ui_view2d_wx - ui_view2d_ww / 2.0) * (ratio - 1.0);
				ui_view2d_pan_y -= (g_ui->input_y - ui_view2d_wy - ui_view2d_wh / 2.0) * (ratio - 1.0);
			}
		}

#if defined(IRON_IOS) || defined(IRON_ANDROID)
		// Twist with two fingers or the trackpad turns the layer, it snaps upright near 0
		static f64 twist_time = 0.0;
		if (mouse_twist_delta != 0.0 && ui_view2d_rotation_allowed()) {
			twist_time = sys_time();
			ui_view2d_rotation += mouse_twist_delta;
			if (ui_view2d_rotation > math_pi()) {
				ui_view2d_rotation -= math_pi() * 2.0;
			}
			else if (ui_view2d_rotation < -math_pi()) {
				ui_view2d_rotation += math_pi() * 2.0;
			}
			ui_view2d_twisting      = true;
			ui_view2d_hwnd->redraws = 2;
		}
		if (ui_view2d_twisting && !mouse_down("right") && sys_time() - twist_time > 0.25) { // Fingers lifted or the trackpad stopped
			ui_view2d_twisting = false;
			if (math_abs(ui_view2d_rotation) < 0.07) {
				ui_view2d_rotation      = 0.0;
				ui_view2d_hwnd->redraws = 2;
			}
		}
#endif

		bool decal_mask = context_is_decal_mask_paint();
		bool set_clone_source =
		    g_context->tool == TOOL_TYPE_CLONE &&
		    keymap_shortcut(string_tmp("%s+%s", any_map_get(g_keymap, "set_clone_source"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN);

		if (!g_ui->input_down) {
			ui_view2d_layer_touched = false;
		}

		bool over_toolbar = !base_view3d_show && mouse_x < ui_view2d_wx + ui_toolbar_x() + ui_toolbar_w(false);
		if (ui_view2d_type == VIEW_2D_TYPE_LAYER && g_context->tool == TOOL_TYPE_CURSOR && mouse_started("left") && !over_toolbar &&
		    ui_view2d_layer_movable(g_context->layer)) {
			_ui_view2d_move_layer = g_context->layer;
			_ui_view2d_moving     = true;
		}

		// The cursor tool moves the layer
		if (ui_view2d_type == VIEW_2D_TYPE_LAYER && !ui_view2d_text_input_hover && g_context->tool != TOOL_TYPE_CURSOR &&
		    (keymap_shortcut(any_map_get(g_keymap, "action_paint"), SHORTCUT_TYPE_DOWN) ||
		     keymap_shortcut(string_tmp("%s+%s", any_map_get(g_keymap, "brush_ruler"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN) ||
		     decal_mask || set_clone_source || g_config->brush_live)) {

			if (g_config->touch_ui) {
				// Paint only when clicking on the layer rect
				gpu_texture_t *tex = g_context->layer->texpaint;
				f32            tx, ty, tw, th;
				ui_view2d_tex_rect(tex->height / (float)tex->width, &tx, &ty, &tw, &th);
				f32 mx = mouse_x - ui_view2d_wx;
				f32 my = mouse_y - ui_view2d_wy;
				if (mx > tx && mx < tx + tw && my > ty && my < ty + th) {
					ui_view2d_layer_touched = true;
				}
				if (ui_view2d_layer_touched) {
					g_context->paint2d = true;
				}
			}
			else {
				g_context->paint2d = true;
			}
		}

		if (!g_ui->is_typing) {
			if (keyboard_started("left")) {
				ui_view2d_pan_x -= 5;
			}
			else if (keyboard_started("right")) {
				ui_view2d_pan_x += 5;
			}
			if (keyboard_started("up")) {
				ui_view2d_pan_y -= 5;
			}
			else if (keyboard_started("down")) {
				ui_view2d_pan_y += 5;
			}

			if (!g_context->paint2d && g_config->touch_ui && g_ui->input_down) {
				ui_view2d_pan_x += g_ui->input_dx;
				ui_view2d_pan_y += g_ui->input_dy;
			}

			// Limit panning to keep texture in viewport
			i32 border = 32;
			f32 wm     = fmin(ui_view2d_ww, ui_view2d_wh);
			f32 tw     = ui_view2d_ww * 0.9 * ui_view2d_pan_scale;
			f32 tx     = ui_view2d_ww / 2.0 - tw / 2.0 + ui_view2d_pan_x;
			f32 hh     = sys_h();
			f32 ty     = hh / 2.0 - tw / 2.0 + ui_view2d_pan_y;

			if (tx + border > ui_view2d_ww) {
				ui_view2d_pan_x = ui_view2d_ww / 2.0 + tw / 2.0 - border;
			}
			else if (tx - border < -tw) {
				ui_view2d_pan_x = -tw / 2.0 - ui_view2d_ww / 2.0 + border;
			}
			if (ty + border > hh) {
				ui_view2d_pan_y = hh / 2.0 + tw / 2.0 - border;
			}
			else if (ty - border < -tw) {
				ui_view2d_pan_y = -tw / 2.0 - hh / 2.0 + border;
			}

			if (keymap_shortcut(any_map_get(g_keymap, "view_reset"), SHORTCUT_TYPE_STARTED)) {
				ui_view2d_pan_x     = 0.0;
				ui_view2d_pan_y     = 0.0;
				ui_view2d_pan_scale = 1.0;
			}
		}
	}

	// Render
	if (!ui_view2d_show) {
		return;
	}

	if (g_context->pdirty >= 0) {
		ui_view2d_hwnd->redraws = 2; // Paint was active
	}

	// Ensure UV map is drawn
	if (ui_view2d_uvmap_show) {
		util_uv_cache_uv_map();
	}

	// Ensure font image is drawn
	if (g_context->font->image == NULL) {
		util_render_make_font_preview();
	}

	g_ui->input_enabled = base_ui_enabled;

	ui_begin(g_ui);

	if (ui_window(ui_view2d_hwnd, ui_view2d_wx, ui_view2d_wy, ui_view2d_ww, ui_view2d_wh, false)) {

		if (!g_config->touch_ui && ui_base_show) {
			ui_dock_view_tabs(TAB_VIEW2D);
		}

		draw_set_color(g_theme->SEPARATOR_COL + 0x00020202);
		draw_filled_rect(0, 0, ui_view2d_ww, ui_view2d_wh);
		draw_set_color(0xffffffff);

		// Texture
		gpu_texture_t *tex     = NULL;
		slot_layer_t  *l       = g_context->layer;
		i32            channel = 0;

		if (ui_view2d_type == VIEW_2D_TYPE_ASSET) {
			tex = project_get_image(g_context->texture);
		}
		else if (ui_view2d_type == VIEW_2D_TYPE_NODE) {
			ui_node_t *sel = ui_view2d_get_selected_node();
			if (sel != NULL) {
				gpu_texture_t *img = ui_nodes_get_node_preview_image(sel);
				if (img != NULL) {
					tex = img;
				}
			}
		}
		else if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
			slot_layer_t *layer = l;

			if (g_config->brush_live && render_path_paint_live_layer_drawn > 0) {
				layer = render_path_paint_live_layer;
			}

			if (ui_view2d_layer_mode == VIEW_2D_LAYER_MODE_VISIBLE) {
				gpu_texture_t *current = _draw_current;
				bool           in_use  = gpu_in_use;
				if (in_use)
					draw_end();
				layer = layers_flatten(false, NULL);
				if (in_use)
					draw_begin(current, false, 0);
			}
			else if (slot_layer_is_group(layer)) {
				gpu_texture_t *current = _draw_current;
				bool           in_use  = gpu_in_use;
				if (in_use)
					draw_end();
				layer = layers_flatten(false, slot_layer_get_children(layer));
				if (in_use)
					draw_begin(current, false, 0);
			}
			else {
				draw_set_color(color_from_floats(layer->mask_opacity, layer->mask_opacity, layer->mask_opacity, 1.0));
			}

			tex = slot_layer_is_mask(g_context->layer)      ? layer->texpaint
			      : ui_view2d_tex_type == PAINT_TEX_BASE    ? layer->texpaint
			      : ui_view2d_tex_type == PAINT_TEX_OPACITY ? layer->texpaint
			      : ui_view2d_tex_type == PAINT_TEX_NORMAL  ? layer->texpaint_nor
			                                                : layer->texpaint_pack;

			channel = slot_layer_is_mask(g_context->layer)        ? 1
			          : ui_view2d_tex_type == PAINT_TEX_OCCLUSION ? 1
			          : ui_view2d_tex_type == PAINT_TEX_ROUGHNESS ? 2
			          : ui_view2d_tex_type == PAINT_TEX_METALLIC  ? 3
			          : ui_view2d_tex_type == PAINT_TEX_OPACITY   ? 4
			          : ui_view2d_tex_type == PAINT_TEX_HEIGHT    ? 4
			          : ui_view2d_tex_type == PAINT_TEX_NORMAL    ? 5
			                                                      : 0;
		}
		else if (ui_view2d_type == VIEW_2D_TYPE_FONT) {
			tex = g_context->font->image;
		}

		_ui_view2d_tex_w = tex != NULL ? tex->width : 0;

		f32 tx, ty, tw, th;
		ui_view2d_tex_rect(tex != NULL ? tex->height / (float)tex->width : 1.0, &tx, &ty, &tw, &th);
		bool pixel_view = tex != NULL && math_round(tw / (float)tex->width * 100) >= 1600;
		if (tex != NULL) {
			if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
				draw_set_pipeline(ui_view2d_pipe);
			}

			if (pixel_view) {
				draw_flush();
				gpu_use_linear_sampling(false);
			}

			f32 ox, oy;
			ui_view2d_layer_move_offset(ui_view2d_type == VIEW_2D_TYPE_LAYER && ui_view2d_layer_mode != VIEW_2D_LAYER_MODE_VISIBLE ? l : NULL, tw, th, &ox,
			                            &oy);
			f32 mx = tx + ox;
			f32 my = ty + oy;
			if (!ui_view2d_rotation_allowed()) {
				ui_view2d_rotation = 0.0;
			}
			ui_view2d_rotation_cx = tx + tw / 2.0;
			ui_view2d_rotation_cy = ty + th / 2.0;
			ui_view2d_draw_image(tex, mx, my, tw, th, channel);
			if (ui_view2d_tiled_show) {
				ui_view2d_draw_image(tex, mx - tw, my, tw, th, channel);
				ui_view2d_draw_image(tex, mx - tw, my - th, tw, th, channel);
				ui_view2d_draw_image(tex, mx - tw, my + th, tw, th, channel);
				ui_view2d_draw_image(tex, mx + tw, my, tw, th, channel);
				ui_view2d_draw_image(tex, mx + tw, my - th, tw, th, channel);
				ui_view2d_draw_image(tex, mx + tw, my + th, tw, th, channel);
				ui_view2d_draw_image(tex, mx, my - th, tw, th, channel);
				ui_view2d_draw_image(tex, mx, my + th, tw, th, channel);
			}

			if (pixel_view) {
				draw_flush();
				gpu_use_linear_sampling(g_config->texture_filter);
			}

			if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
				draw_set_pipeline(NULL);
			}
			draw_set_color(0xffffffff);

			// Texture and node preview color picking
			if ((context_in_2d_view(VIEW_2D_TYPE_ASSET) || context_in_2d_view(VIEW_2D_TYPE_NODE)) && g_context->tool == TOOL_TYPE_PICKER && g_ui->input_down) {
				_ui_view2d_render_tex = tex;
				_ui_view2d_render_x   = g_ui->input_x - tx - ui_view2d_wx;
				;
				_ui_view2d_render_y  = g_ui->input_y - ty - ui_view2d_wy;
				_ui_view2d_render_tw = tw;
				_ui_view2d_render_th = th;
				sys_notify_on_next_frame(&ui_view2d_render_color_pick, NULL);
			}
		}

		// UV map
		if (ui_view2d_type == VIEW_2D_TYPE_LAYER && ui_view2d_uvmap_show) {
			draw_scaled_image(util_uv_uvmap, tx, ty, tw, th);
		}

		// Pixel grid
		if (g_config->view2d_grid_show && tex != NULL) {
			i32 cell  = g_config->view2d_grid_cell;
			f32 stepx = cell / (float)tex->width * tw;
			f32 stepy = cell / (float)tex->height * th;
			draw_set_color(0x55ffffff);
			for (f32 gx = tx; gx <= tx + tw + 0.5; gx += stepx) {
				draw_filled_rect(math_floor(gx), ty, 1, th);
			}
			for (f32 gy = ty; gy <= ty + th + 0.5; gy += stepy) {
				draw_filled_rect(tx, math_floor(gy), tw, 1);
			}
			draw_set_color(0xffffffff);
		}

		if (pixel_view) {
			i32 ix    = (i32)tx;
			i32 iy    = (i32)ty;
			f32 stepx = (i32)tw / (float)tex->width;
			f32 stepy = (i32)th / (float)tex->height;
			f32 x0    = fmax(ix, 0);
			f32 y0    = fmax(iy, 0);
			f32 x1    = fmin(ix + (i32)tw, ui_view2d_ww);
			f32 y1    = fmin(iy + (i32)th, ui_view2d_wh);
			i32 i0    = math_ceil((x0 - ix) / stepx);
			i32 i1    = math_floor((x1 - ix) / stepx);
			i32 j0    = math_ceil((y0 - iy) / stepy);
			i32 j1    = math_floor((y1 - iy) / stepy);
			draw_set_color(0x55ffffff);
			for (i32 i = i0; i <= i1; ++i) {
				draw_filled_rect(math_floor(ix + i * stepx), y0, 1, y1 - y0);
			}
			for (i32 j = j0; j <= j1; ++j) {
				draw_filled_rect(x0, math_floor(iy + j * stepy), x1 - x0, 1);
			}
			draw_set_color(0xffffffff);
		}

		// Menu, none on touch: the popovers and the view button by the cube do its work
		if (ui_base_show && !ui_popovers_enabled()) {
			i32 top_y = ui_menu_top_y();
			i32 ew    = math_floor(UI_ELEMENT_W());
			draw_set_color(g_theme->WINDOW_BG_COL);
			draw_filled_rect(0, top_y, ui_view2d_ww, UI_ELEMENT_H() + UI_ELEMENT_OFFSET() * 2);
			draw_set_color(0xffffffff);

			f32 start_y = top_y + UI_ELEMENT_OFFSET();
			g_ui->_x    = 2;
			g_ui->_y    = 2 + start_y;

			// Back button
			if (g_config->touch_ui && !base_view3d_show) {
				g_ui->_w = math_floor(ew * 0.7 + 3);
				if (ui_icon_button(tr("Back"), ICON_ARROW_LEFT, UI_ALIGN_CENTER)) {
					g_ui->input_released = false;
					base_set_workspace(WORKSPACE_PRESET_VIEW_3D);
				}
				g_ui->_x += ew * 0.7 + 3;
				g_ui->_y = 2 + start_y;
			}

			bool full = true;
#ifdef IRON_IOS
			if (config_is_iphone()) {
				full = false;
			}
#endif

			// Editable layer name
			if (full) {
				g_ui->_w   = ew;
				char *text = "";
				if (ui_view2d_type == VIEW_2D_TYPE_NODE) {
					text = g_context->node_preview_name;
				}
				else if (ui_view2d_type == VIEW_2D_TYPE_ASSET && g_context->texture != NULL) {
					text = g_context->texture->name;
				}
				else if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
					text = l->name;
				}
				else if (ui_view2d_type == VIEW_2D_TYPE_FONT) {
					text = g_context->font->name;
				}

				g_ui->_w = math_floor(math_min(draw_string_width(g_font, g_ui->font_size, text) + 15 * UI_SCALE(), 100 * UI_SCALE()));

				bool name_changed = false;
				if (ui_view2d_type == VIEW_2D_TYPE_ASSET) {
					asset_t *asset = g_context->texture;
					if (asset != NULL) {
						ui_text_input(&asset->name, "", UI_ALIGN_LEFT, true, false);
						name_changed = ui_item_changed();
					}
				}
				else if (ui_view2d_type == VIEW_2D_TYPE_NODE) {
					ui_node_t *sel = ui_view2d_get_selected_node();
					if (sel != NULL && !string_equals(sel->type, "GROUP")) {
						ui_text_input(&sel->name, "", UI_ALIGN_LEFT, true, false);
						name_changed                 = ui_item_changed();
						g_context->node_preview_name = string_copy(sel->name);
						ui_view2d_text_input_hover   = g_ui->is_hovered;
					}
					else {
						ui_text(g_context->node_preview_name, UI_ALIGN_LEFT, 0x00000000);
					}
				}
				else if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
					ui_id_t name_id     = ui_widget_id(&l->name, UI_ID_TEXT);
					bool    was_editing = g_ui->text_selected_id == name_id;
					ui_text_input(&l->name, "", UI_ALIGN_LEFT, true, false);
					name_changed               = ui_item_changed();
					ui_view2d_text_input_hover = g_ui->is_hovered;

					if (!was_editing && g_ui->text_selected_id == name_id) {
						ui_view2d_layer_name_prev = string_copy(l->name);
					}
					else if (was_editing && g_ui->text_selected_id != name_id && ui_view2d_layer_name_prev != NULL) {
						char *name = string_equals(l->name, "") ? ui_view2d_layer_name_prev : l->name;
						l->name    = string_copy(slot_layer_unique_name(l, name));
						if (!string_equals(ui_view2d_layer_name_prev, l->name)) {
							history_layer_name(l, ui_view2d_layer_name_prev);
						}
					}
				}
				else if (ui_view2d_type == VIEW_2D_TYPE_FONT) {
					ui_text_input(&g_context->font->name, "", UI_ALIGN_LEFT, true, false);
					name_changed = ui_item_changed();
				}

				if (name_changed) {
					ui_dock_redraw(0);
				}
				g_ui->_x += g_ui->_w + 3;
				g_ui->_y = 2 + start_y;
			}

			g_ui->_w = ew;

			if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
				string_array_t *layer_mode_combo = any_array_create_from_raw_tmp(
				    (void *[]){
				        tr("Visible"),
				        tr("Selected"),
				    },
				    2);
				ui_combo((int *)&ui_view2d_layer_mode, layer_mode_combo, tr("Layers"), false, UI_ALIGN_LEFT, true);
				g_ui->_x += ew + 3;
				g_ui->_y = 2 + start_y;

				if (!slot_layer_is_mask(g_context->layer)) {
					string_array_t *tex_type_combo = any_array_create_from_raw_tmp(
					    (void *[]){
					        tr("Base Color"),
					        tr("Opacity"),
					        tr("Normal Map"),
					        tr("Occlusion"),
					        tr("Roughness"),
					        tr("Metallic"),
					        tr("Height"),
					    },
					    7);

					ui_combo((int *)&ui_view2d_tex_type, tex_type_combo, tr("Texture"), false, UI_ALIGN_LEFT, true);
					g_ui->_x += ew + 3;
					g_ui->_y = 2 + start_y;
				}
			}

			// Zoom slider
			if (full && tex != NULL) {
				i32 scale_percent = math_round((tw / (float)tex->width) * 100);
				f32 zoom          = scale_percent;
				g_ui->_w          = math_floor(ew + 3);
				ui_set_next_id((ui_id_t)&ui_view2d_pan_scale);
				ui_slider(&zoom, string_tmp("%%", scale_percent), 1, 100, true, 1, true, UI_ALIGN_RIGHT, true);
				if (ui_item_changed()) {
					ui_view2d_pan_scale     = zoom / 100.0 * tex->width / (fmin(ui_view2d_ww, ui_view2d_wh) * 0.9);
					ui_view2d_hwnd->redraws = 2;
				}
				g_ui->_x += ew + 3;
				g_ui->_y = 2 + start_y;
			}

			if (full && ui_view2d_rotation != 0.0) {
				g_ui->_w = math_floor(ew + 3);
				if (ui_button(tr("Reset Rotation"), UI_ALIGN_CENTER, "")) {
					ui_view2d_rotation      = 0.0;
					ui_view2d_hwnd->redraws = 2;
				}
				g_ui->_x += ew + 3;
				g_ui->_y = 2 + start_y;
			}

			g_ui->_w = math_floor(ew * 0.7 + 3);
			if (ui_icon_button("Edit", ICON_EDIT, UI_ALIGN_CENTER)) {
				ui_view2d_tex         = tex;
				ui_view2d_tex_channel = ui_view2d_type == VIEW_2D_TYPE_LAYER ? channel : 0;
#ifdef IRON_BGRA
				// Layers are render targets and read back as bgra, assets and node images are textures from pixels
				ui_view2d_tex_bgra = ui_view2d_type == VIEW_2D_TYPE_ASSET  ? true
				                     : ui_view2d_type == VIEW_2D_TYPE_NODE ? ui_nodes_node_preview_is_bgra(ui_view2d_get_selected_node())
				                                                           : false;
#endif
				ui_menu_draw(&ui_view2d_draw_edit, -1, -1);
			}
			g_ui->_x += ew * 0.7 + 3;
			g_ui->_y = 2 + start_y;
		}

		// Picked position
		if (g_context->tool == TOOL_TYPE_PICKER && (ui_view2d_type == VIEW_2D_TYPE_LAYER || ui_view2d_type == VIEW_2D_TYPE_ASSET)) {
			util_cursor_draw_2d(tx + tw * g_context->uvx_picked, ty + th * g_context->uvy_picked, 15 * UI_SCALE(), 0x99ffffff);
		}
	}
	ui_end();

	g_ui->input_enabled = true;
}
