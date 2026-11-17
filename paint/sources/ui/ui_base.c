
#include "../global.h"

void ui_base_init_on_next_frame(void *_) {
	layers_init();
}

bool ui_base_picker_button() {
	return ui_icon_button("", ICON_PICKER, UI_ALIGN_CENTER);
}

void ui_base_init() {
	g_context->text_tool_text = string_copy(tr("Text"));

	if (g_context->empty_envmap == NULL) {
		ui_base_make_empty_envmap(g_theme->VIEWPORT_COL);
	}
	if (g_context->preview_envmap == NULL) {
		u8_array_t *b             = u8_array_create(4);
		b->buffer[0]              = 0;
		b->buffer[1]              = 0;
		b->buffer[2]              = 0;
		b->buffer[3]              = 255;
		g_context->preview_envmap = gpu_create_texture_from_bytes(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA32);
		array_free(b);
		free(b);
	}

	if (g_context->saved_envmap == NULL) {
		// raw.saved_envmap = scene_world._envmap;
		g_context->default_irradiance       = scene_world->_->irradiance;
		g_context->default_radiance         = scene_world->_->radiance;
		g_context->default_radiance_mipmaps = scene_world->_->radiance_mipmaps;
	}
	scene_world->_->envmap = g_context->show_envmap ? g_context->saved_envmap : g_context->empty_envmap;
	g_context->ddirty      = 1;

	string_array_t *resources = any_array_create_from_raw(
	    (void *[]){
	        "icons.k",
	        "icons05x.k",
	    },
	    2);
	resource_load(resources);

	f32           scale = g_config->window_scale;
	ui_options_t *ops   = ALLOC_INIT(ui_options_t, {.theme = g_theme, .font = g_font, .scale_factor = scale});

	g_ui = ui_create(ops);

	ui_dock_load();
	if (UI_SCALE() > 1) {
		ui_base_set_icon_scale();
	}

	ui_picker_button = ui_base_picker_button;

	g_context->gizmo = scene_get_child(".Gizmo");
	array_remove(scene_meshes, g_context->gizmo->ext); // Drawn by render_gizmo_draw()
	g_context->gizmo_translate_x = object_get_child(g_context->gizmo, ".TranslateX");
	g_context->gizmo_translate_y = object_get_child(g_context->gizmo, ".TranslateY");
	g_context->gizmo_translate_z = object_get_child(g_context->gizmo, ".TranslateZ");
	g_context->gizmo_scale_x     = object_get_child(g_context->gizmo, ".ScaleX");
	g_context->gizmo_scale_y     = object_get_child(g_context->gizmo, ".ScaleY");
	g_context->gizmo_scale_z     = object_get_child(g_context->gizmo, ".ScaleZ");
	g_context->gizmo_rotate_x    = object_get_child(g_context->gizmo, ".RotateX");
	g_context->gizmo_rotate_y    = object_get_child(g_context->gizmo, ".RotateY");
	g_context->gizmo_rotate_z    = object_get_child(g_context->gizmo, ".RotateZ");

	project_new(false);

	if (string_equals(g_project->_->filepath, "")) {
		sys_notify_on_next_frame(&ui_base_init_on_next_frame, NULL);
	}
}

void ui_base_menu_draw_viewport_mode() {
	i32  mode         = g_context->viewport_mode;
	bool mode_changed = false;
	ui_text(tr("Viewport Mode"), UI_ALIGN_RIGHT, 0x00000000);

	string_array_t *modes     = base_get_viewport_modes();
	string_array_t *shortcuts = base_get_viewport_mode_shortcuts();
	for (i32 i = 0; i < modes->length; ++i) {
		ui_radio(&mode, i, modes->buffer[i], shortcuts->buffer[i]);
		mode_changed |= ui_item_changed();
	}

	i32 index = string_array_index_of(shortcuts, keyboard_key_code(g_ui->key_code));
	if (g_ui->is_key_pressed && index != -1) {
		g_ui->changed = true;
		context_set_viewport_mode(index);
	}
	else if (mode_changed) {
		context_set_viewport_mode(mode);
		g_ui->changed = true;
	}
}

void ui_base_update_next_frame(void *_) {
	export_texture_run(g_context->texture_export_path, false);
}


void ui_base_update(void *_) {

	if (base_ui_enabled && !ui_brush_creator_shown()) {
		render_gizmo_update();
		util_layer_update_path();
		util_touch_update();
		util_stencil_transform();
		util_layer_check_path_grab();
		util_brush_update();
		util_layer_update_preview();
		util_shortcut_undo_redo();
		util_shortcut_global();
		util_shortcut_brush();
		util_shortcut_viewport();
		ui_id_mask_update();
		util_particle_update();
	}

	box_bake_maps_update();
	project_autosave_update();
	config_brush_save_update();

	util_cursor_render(_);

	// The native UI has the panels; the full screen editors without a native version yet still draw here
	g_ui->input_enabled = base_ui_enabled && !ui_brush_creator_shown();
	ui_begin(g_ui);
	ui_id_mask_render_ui();
	ui_material_studio_render_ui();
	ui_brush_creator_render_ui();
	ui_end();
	g_ui->input_enabled       = true;
}

static bool ui_base_is_portrait() {
	return g_config->touch_ui && iron_window_width() < iron_window_height();
}

static void ui_base_show_nodes(canvas_type_t type) {
	// Clear input state as ui receives input events even when not drawn
	ui_end_input();

	bool show = !ui_nodes_show || ui_nodes_canvas_type != type;
	if (ui_nodes_canvas_type != type) {
		ui_nodes_group_stack->length = 0;
	}
	ui_nodes_canvas_type = type;
	ui_dock_show_view(DOCK_VIEW_NODES, show);

	if (g_config->touch_ui && ui_view2d_show && base_view3d_show) {
		ui_dock_show_view(DOCK_VIEW_2D, false);
	}
	if (ui_base_is_portrait()) {
		if (show) {
			ui_dock_show_view(DOCK_VIEW_2D, false);
		}
		ui_dock_show_view(DOCK_VIEW_3D, !show);
	}

	base_resize();
}

void ui_base_show_material_nodes() {
	ui_base_show_nodes(CANVAS_TYPE_MATERIAL);
}

void ui_base_show_2d_view(view_2d_type_t type) {
	// Clear input state as ui receives input events even when not drawn
	ui_end_input();
	bool show               = ui_view2d_type != type || !ui_view2d_show;
	ui_view2d_type          = type;
	ui_view2d_hwnd->redraws = 2;
	ui_dock_show_view(DOCK_VIEW_2D, show);

	if (g_config->touch_ui && ui_nodes_show && base_view3d_show) {
		ui_dock_show_view(DOCK_VIEW_NODES, false);
	}
	if (ui_base_is_portrait()) {
		if (show) {
			ui_dock_show_view(DOCK_VIEW_NODES, false);
		}
		ui_dock_show_view(DOCK_VIEW_3D, !show);
	}

	base_resize();
}

// 0 - 3D view, 1 - 2D view alone, 2 - both
i32 ui_base_view_mode() {
	return base_view3d_show && ui_view2d_show ? 2 : ui_view2d_show ? 1 : 0;
}

// Touch: 3D view -> 2D view alone (strokes paint in it) -> 2D and 3D side by side -> 3D view
void ui_base_cycle_view_mode() {
	ui_end_input();
	bool view3d = base_view3d_show;
	bool view2d = ui_view2d_show;
	if (view3d && !view2d) {
		ui_view2d_type          = VIEW_2D_TYPE_LAYER;
		ui_view2d_hwnd->redraws = 2;
		ui_dock_show_view(DOCK_VIEW_NODES, false);
		ui_dock_show_view(DOCK_VIEW_2D, true);
		ui_dock_show_view(DOCK_VIEW_3D, false);
	}
	else if (!view3d && view2d) {
		ui_dock_show_view(DOCK_VIEW_3D, true);
	}
	else {
		ui_dock_show_view(DOCK_VIEW_2D, false);
		ui_dock_show_view(DOCK_VIEW_3D, true);
	}
	base_resize();
}

// Floating panels (top bar, toolbar): darker, rounded on the sides away from the screen edge, with a drop shadow

i32 ui_base_panel_shadow() {
	return math_floor(4 * UI_SCALE());
}

static void ui_base_fill_round_rect(f32 x, f32 y, f32 w, f32 h, f32 r, i32 corners) {
	gpu_texture_t *c = ui_round_corner((i32)r);
	draw_filled_rect(x + r, y, w - r * 2, h);
	draw_filled_rect(x, y + r, r, h - r * 2);
	draw_filled_rect(x + w - r, y + r, r, h - r * 2);
	if (corners & UI_BASE_CORNER_TL) {
		draw_scaled_image(c, x, y, r, r);
	}
	else {
		draw_filled_rect(x, y, r, r);
	}
	if (corners & UI_BASE_CORNER_TR) {
		draw_scaled_image(c, x + w, y, -r, r);
	}
	else {
		draw_filled_rect(x + w - r, y, r, r);
	}
	if (corners & UI_BASE_CORNER_BR) {
		draw_scaled_image(c, x + w, y + h, -r, -r);
	}
	else {
		draw_filled_rect(x + w - r, y + h - r, r, r);
	}
	if (corners & UI_BASE_CORNER_BL) {
		draw_scaled_image(c, x, y + h, r, -r);
	}
	else {
		draw_filled_rect(x, y + h - r, r, r);
	}
}

// Filled rect rounded on all corners, window coordinates, current color
void ui_base_fill_round(f32 x, f32 y, f32 w, f32 h, f32 r) {
	ui_base_fill_round_rect(x, y, w, h, r, UI_BASE_CORNER_TL | UI_BASE_CORNER_TR | UI_BASE_CORNER_BL | UI_BASE_CORNER_BR);
}

// Window holding a panel of size w x h, the window is larger by the shadow on the right (or left) and bottom
bool ui_base_panel_window(ui_window_t *handle, i32 x, i32 y, i32 w, i32 h, i32 corners, bool shadow_left) {
	i32 s      = ui_base_panel_shadow();
	i32 wx     = shadow_left ? x - s : x;
	u32 bg     = g_theme->WINDOW_BG_COL;
	g_theme->WINDOW_BG_COL = 0x00000000;
	bool draw  = ui_window(handle, wx, y, w + s, h + s, false);
	g_theme->WINDOW_BG_COL = bg;
	if (!draw) {
		return false;
	}

	f32 px = shadow_left ? s : 0;
	f32 r  = 12 * UI_SCALE();
	// Shadow: stacked translucent layers, growing outwards
	for (i32 i = s; i >= 1; --i) {
		draw_set_color(0x0c000000);
		ui_base_fill_round_rect(px - i, i / 2 - i, w + i * 2, h + i * 2, r + i, corners);
	}
	draw_set_color(g_theme->WINDOW_BG_COL); // Same gray as the sidebar, the viewport is darker
	ui_base_fill_round_rect(px, 0, w, h, r, corners);
	draw_set_color(0xffffffff);

	g_ui->_x += px;
	g_ui->_w = w; // Content keeps the panel width, the shadow is outside of it
	return true;
}
