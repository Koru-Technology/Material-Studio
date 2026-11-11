
#include "../global.h"

// Floating panels drawn last frame, see ui_base_panel_add_rect
#define UI_BASE_PANEL_RECTS_MAX 16
static f32 ui_base_panel_rects[UI_BASE_PANEL_RECTS_MAX * 4];
static i32 ui_base_panel_rects_count = 0;
static i32 ui_base_panel_rects_drawn = 0;

void ui_base_init_on_next_frame(void *_) {
	layers_init();
}

static tab_draw_t *ui_base_all_tabs[TAB_COUNT];

tab_draw_t *ui_base_get_tab(i32 id) {
	return id >= 0 && id < TAB_COUNT ? ui_base_all_tabs[id] : NULL;
}

// Windows drawn once per 3D view display the same content, keep them redrawing together
void ui_window_sync_redraws(ui_window_t *handle, ui_window_t *split_handle, bool split) {
	if (split) {
		i32 redraws           = math_max(handle->redraws, split_handle->redraws);
		handle->redraws       = redraws;
		split_handle->redraws = redraws;
	}
	else {
		split_handle->redraws = 2;
	}
}

bool ui_base_picker_button() {
	return ui_icon_button("", ICON_PICKER, UI_ALIGN_CENTER);
}

void ui_base_init() {
	ui_toolbar_init();
	g_context->text_tool_text = string_copy(tr("Text"));
	ui_header_init();
	ui_menubar_init();

	ui_header_h  = math_floor(ui_header_default_h * g_config->window_scale);
	ui_menubar_w = math_floor(ui_menubar_default_w * g_config->window_scale);

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

#if !defined(IRON_IOS) && !defined(IRON_ANDROID)
	ui_on_tab_drop = ui_dock_on_tab_drop;
#endif
	ui_on_tab_dock         = ui_dock_on_tab_dock;
	ui_on_tab_context_menu = ui_dock_on_tab_context_menu;
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

void ui_base_update_ui_on_next_frame(void *_) {
	layers_update_fill_layer(true);
	make_material_parse_paint_material(false);
}

// Nothing docked is visible
static void ui_base_draw_empty() {
	draw_begin(NULL, true, g_theme->SEPARATOR_COL);
	gpu_texture_t *img = data_get_texture("badge_bw.k");
	draw_set_color(0x22ffffff);
	draw_image(img, iron_window_width() / 2.0 - img->width / 2.0, iron_window_height() / 2.0 - img->height / 2.0);
	draw_set_color(0xffffffff);
	draw_end();
}

static void ui_base_panel_update_grab();

void ui_base_update(void *_) {

	if (console_message_timer > 0) {
		console_message_timer -= sys_delta();
		if (console_message_timer <= 0) {
			ui_dock_redraw(TAB_AREA_STATUS);
		}
	}

	ui_sidebar_w_mini = math_floor(ui_base_sidebar_mini_w(g_config->large_asset_previews) * UI_SCALE());

	ui_base_panel_update_grab();
	if (base_ui_enabled && !ui_base_panel_grab) {
		render_gizmo_update();
		util_layer_update_path();
		util_touch_update();
		util_stencil_transform();
		util_select_update();
		util_shape_tool_update();
		util_text_tool_update();
		util_path_tool_update();
		util_layer_check_path_grab();
		util_brush_update();
		util_layer_update_preview();
		util_shortcut_undo_redo();
		util_shortcut_global();
		util_shortcut_brush();
		util_shortcut_viewport();
		ui_toolbar_update();
		util_particle_update();
	}

	box_bake_maps_update();
	project_autosave_update();

	util_cursor_render(_);

	if (!ui_base_show && g_config->touch_ui) {
		g_ui->input_enabled = true;
		ui_begin(g_ui);
		static ui_window_t window = {0};
		if (ui_window(&window, 0, 0, 150, math_floor(UI_ELEMENT_H() + UI_ELEMENT_OFFSET() + 1), false)) {
			if (ui_button(tr("Close"), UI_ALIGN_CENTER, "")) {
				ui_base_toggle_distract_free();
			}
		}
		ui_end();
	}

	if (!ui_base_show) {
		ui_base_panel_rects_count = 0;
		return;
	}

	g_ui->input_enabled = base_ui_enabled;

	// Nothing to display
	if (ui_dock_is_empty()) {
		ui_base_draw_empty();
	}

	ui_base_panel_rects_drawn = 0;
	ui_begin(g_ui);
	ui_menubar_update_w();
	ui_dock_render_ui();
	ui_toolbar_render_ui();
	ui_menubar_render_ui();
	ui_header_render_ui();
	ui_panels_render_ui();
	ui_end();
	ui_base_panel_rects_count = ui_base_panel_rects_drawn;
	g_ui->input_enabled = true;
}

tab_draw_t *_draw_callback_create(void (*f)(i32 *), i32 id, char *name, i32 area) {
	tab_draw_t *cb       = ALLOC_INIT(tab_draw_t, {.f = f, .id = id, .name = name, .area = area});
	ui_base_all_tabs[id] = cb;
	return cb;
}

void ui_base_init_tabs() {
	_draw_callback_create(tab_layers_draw, TAB_LAYERS, "Layers", TAB_AREA_SIDEBAR0);
	_draw_callback_create(tab_meshes_draw, TAB_MESHES, "Meshes", TAB_AREA_SIDEBAR0);
	_draw_callback_create(tab_scripts_draw, TAB_SCRIPTS, "Scripts", TAB_AREA_SIDEBAR0);
	_draw_callback_create(tab_materials_draw, TAB_MATERIALS, "Materials", TAB_AREA_SIDEBAR1);
	_draw_callback_create(tab_library_draw, TAB_LIBRARY, "Library", TAB_AREA_SIDEBAR1);
	_draw_callback_create(tab_brushes_draw, TAB_BRUSHES, "Brushes", TAB_AREA_SIDEBAR1);
	_draw_callback_create(tab_browser_draw, TAB_BROWSER, "Browser", TAB_AREA_STATUS);
	_draw_callback_create(tab_textures_draw, TAB_TEXTURES, "Textures", TAB_AREA_STATUS);
	_draw_callback_create(tab_fonts_draw, TAB_FONTS, "Fonts", TAB_AREA_STATUS);
	_draw_callback_create(tab_sounds_draw, TAB_SOUNDS, "Sounds", TAB_AREA_STATUS);
	_draw_callback_create(tab_swatches_draw, TAB_SWATCHES, "Swatches", TAB_AREA_STATUS);
	_draw_callback_create(tab_timeline_draw, TAB_TIMELINE, "Timeline", TAB_AREA_STATUS);
	_draw_callback_create(tab_console_draw, TAB_CONSOLE, "Console", TAB_AREA_STATUS);
#ifndef NDEBUG
	_draw_callback_create(tab_debug_draw, TAB_DEBUG, "Debug", TAB_AREA_SIDEBAR0)->closed_default = true;
#endif
	// Views draw their own windows
	_draw_callback_create(tab_view3d_draw, TAB_VIEW3D, "3D View", -1);
	_draw_callback_create(tab_view2d_draw, TAB_VIEW2D, "2D View", -1)->closed_default = true;
	_draw_callback_create(tab_nodes_draw, TAB_NODES, "Nodes", -1)->closed_default     = true;

#ifdef IRON_IOS
	if (config_is_iphone()) {
		ui_base_all_tabs[TAB_TIMELINE]->closed_default = true;
		ui_base_all_tabs[TAB_SWATCHES]->closed_default = true;
		ui_base_all_tabs[TAB_SOUNDS]->closed_default   = true;
	}
#endif

#ifdef IRON_ANDROID
	if (iron_window_width() <= 1080) {
		ui_base_all_tabs[TAB_TIMELINE]->closed_default = true;
		ui_base_all_tabs[TAB_SOUNDS]->closed_default   = true;
	}
#endif
}

void ui_base_toggle_distract_free() {
	if (player_in_editor) {
		player_stop();
		return;
	}
	if (base_player_lock) {
		return;
	}

	ui_base_show = !ui_base_show;
	if (ui_base_show && base_player) {
		base_player = false;
		base_set_workspace(WORKSPACE_PRESET_NONE);
	}
	base_resize();
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
	ui_dock_show_view(TAB_NODES, show);

	if (g_config->touch_ui && ui_view2d_show && base_view3d_show) {
		ui_dock_show_view(TAB_VIEW2D, false);
	}
	if (ui_base_is_portrait()) {
		if (show) {
			ui_dock_show_view(TAB_VIEW2D, false);
		}
		ui_dock_show_view(TAB_VIEW3D, !show);
	}

	base_resize();
}

void ui_base_show_material_nodes() {
	ui_base_show_nodes(CANVAS_TYPE_MATERIAL);
}

void ui_base_show_brush_nodes() {
	ui_base_show_nodes(CANVAS_TYPE_BRUSH);
}

void ui_base_show_mesh_nodes() {
	ui_base_show_nodes(CANVAS_TYPE_MESH);
}

void ui_base_show_2d_view(view_2d_type_t type) {
	// Clear input state as ui receives input events even when not drawn
	ui_end_input();
	bool show               = ui_view2d_type != type || !ui_view2d_show;
	ui_view2d_type          = type;
	ui_view2d_hwnd->redraws = 2;
	ui_dock_show_view(TAB_VIEW2D, show);

	if (g_config->touch_ui && ui_nodes_show && base_view3d_show) {
		ui_dock_show_view(TAB_NODES, false);
	}
	if (ui_base_is_portrait()) {
		if (show) {
			ui_dock_show_view(TAB_NODES, false);
		}
		ui_dock_show_view(TAB_VIEW3D, !show);
	}

	base_resize();
}

void ui_base_show_3d_view() {
	if (!base_view3d_show) {
		if (g_config->touch_ui && ui_nodes_show && ui_view2d_show) {
			ui_dock_show_view(TAB_VIEW2D, false);
		}
		if (ui_base_is_portrait()) {
			ui_dock_show_view(TAB_NODES, false);
			ui_dock_show_view(TAB_VIEW2D, false);
		}
	}

	ui_dock_show_view(TAB_VIEW3D, !base_view3d_show);
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

// A touch starting on a floating panel neither paints nor moves the camera

void ui_base_panel_add_rect(i32 x, i32 y, i32 w, i32 h) {
	if (ui_base_panel_rects_drawn >= UI_BASE_PANEL_RECTS_MAX) {
		return;
	}
	f32 *r = &ui_base_panel_rects[ui_base_panel_rects_drawn++ * 4];
	r[0]   = x;
	r[1]   = y;
	r[2]   = w;
	r[3]   = h;
}

static bool ui_base_panel_hit(f32 x, f32 y) {
	for (i32 i = 0; i < ui_base_panel_rects_count; ++i) {
		f32 *r = &ui_base_panel_rects[i * 4];
		if (x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]) {
			return true;
		}
	}
	return false;
}

static void ui_base_panel_update_grab() {
	if (mouse_started("left") || pen_started("tip")) {
		ui_base_panel_grab = ui_base_panel_hit(mouse_x, mouse_y);
	}
	else if (!mouse_down("left") && !pen_down("tip")) {
		ui_base_panel_grab = false;
	}
}

// Window holding a panel of size w x h, the window is larger by the shadow on the right (or left) and bottom
bool ui_base_panel_window(ui_window_t *handle, i32 x, i32 y, i32 w, i32 h, i32 corners, bool shadow_left) {
	ui_base_panel_add_rect(x, y, w, h);
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
