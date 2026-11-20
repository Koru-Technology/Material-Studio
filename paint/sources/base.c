
#include "global.h"


f32     base_drop_x             = 0.0;
f32     base_drop_y             = 0.0;

void base_on_shutdown() {
	// iOS saved when the app resigned active, the gpu can not read the layers back from the background
	config_save();
}

void gpu_release_staging_buffers(void);

// Low memory: drop what is created again when needed
void base_on_memory_warning() {
	if (g_context->brush_time == 0) {
		history_on_memory_warning(); // Not in the middle of a stroke
		render_path_paint_release_scratch();
	}
	mm_buffers_trim(); // Material Maker buffer textures the last render did not use, they render again when needed
	material_cache_trim(); // Material caches but the current material's
	mask_gen_trim();       // Mask generator outputs but the selected mask's, texmask keeps the result
	gpu_release_staging_buffers();
	gpu_execute_and_wait(); // Free deleted textures now instead of on the next present
	gpu_cleanup();
}

// Home gesture or app switcher, save while the gpu can still read the layers back
void base_on_resign_active() {
	project_save_now();
	config_save();
}

// iPadOS usually ends apps in the background without a shutdown event
void base_on_background() {
	config_save();
}

void base_on_foreground() {
	library_rescan(); // Pick up materials added while away
	g_context->foreground_event = true;
	g_context->last_paint_x     = -1;
	g_context->last_paint_y     = -1;
}

void base_on_drop_files(char *drop_path) {
	drop_path = string_copy(trim_end(drop_path));
	any_array_push(base_drop_paths, drop_path);
}

void base_save_window_rect() {
	g_config->window_w = iron_window_width();
	g_config->window_h = iron_window_height();
	g_config->window_x = iron_window_x();
	g_config->window_y = iron_window_y();
	config_save();
}

void base_save_and_quit_callback(bool save) {
	base_save_window_rect();
	if (save) {
		project_save(true);
	}
	else {
		hstore_journal_delete(project_uuid_get()); // Changes discarded: nothing to recover
		iron_stop();
	}
}

// Black like the launch screen while the last project is reopened on startup, so the default cube and the
// first frames of the loaded project are not seen
static bool base_startup_cover        = false;
static i32  base_startup_cover_frames = -1;  // Frames left once the project is loaded, -1 while loading
static i32  base_startup_cover_max    = 600; // Gives up when the load never finishes

bool base_startup_covering() {
	return base_startup_cover;
}

// Work spread over frames that is under way without input: the engine keeps drawing frames until it is done
// (iron_idle_busy), otherwise it sleeps a moment after the last change. One that stays busy for long is logged.
static const char *base_idle_busy_work() {
	if (_sys_on_next_frames->length > 0) {
		return "next frame callbacks";
	}
	if (script_is_running()) {
		return "script";
	}
	if (gpu_transfer_pending() > 0) {
		return "gpu readbacks";
	}
	if (history_busy()) {
		return "undo restores";
	}
	if (paint_refine_stat(1) > 0 || (paint_refine_stat(0) > 0 && mm_buffers_stat(11) > 0)) {
		return "stroke sharpening";
	}
	if (mm_buffers_stat(11) > 0) {
		return "material maker renders";
	}
	if (material_cache_building()) {
		return "material cache";
	}
	if (make_material_paint_pending()) {
		return "paint shader";
	}
	if (fill_stat(2) > 0) {
		return "fill layers";
	}
	if (layers_resolution_running()) {
		return "layer resolution";
	}
	if (export_tiled_running() || app_bake_running() || bake_texture_node_baking) {
		return "export or bake";
	}
	if (project_session_due()) {
		return "session journal";
	}
	if (brush_previews_stat(3) > 0) {
		return "brush previews";
	}
	if (util_particle_alive() || brush_pad_busy()) {
		return "particles";
	}
	if (base_startup_covering() || base_drop_paths->length > 0) {
		return "project load";
	}
	if (library_busy() || mm_library_icons_loading()) {
		return "library";
	}
	if (material_studio_busy()) {
		return "material studio";
	}
	if (tween_active()) {
		return "animations";
	}
	return NULL;
}

static bool base_idle_busy() {
	static f64         since = 0.0;
	static const char *last  = NULL;
	const char        *work  = base_idle_busy_work();
	if (work != last) {
		last  = work;
		since = iron_time();
	}
	else if (work != NULL && since > 0.0 && iron_time() - since > 30.0) {
		iron_log("Busy for 30 s without input: %s", work);
		since = 0.0;
	}
	return work != NULL;
}

// The project reopened on startup has not loaded yet
bool base_startup_loading() {
	return base_startup_cover && base_startup_cover_frames < 0;
}

void base_startup_cover_done() {
	if (base_startup_cover && base_startup_cover_frames < 0) {
		base_startup_cover_frames = 10;
	}
}

static void base_draw_startup_cover() {
	if (!base_startup_cover) {
		return;
	}
	if (base_startup_cover_frames > 0) {
		base_startup_cover_frames--;
	}
	if (base_startup_cover_frames == 0 || --base_startup_cover_max <= 0) {
		base_startup_cover = false;
		base_redraw_ui();
		return;
	}
	draw_begin(NULL, false, 0);
	draw_set_color(0xff000000);
	draw_filled_rect(0, 0, iron_window_width(), iron_window_height());
	draw_set_color(0xffffffff);
	draw_end();
}

// ███████╗██╗  ██╗ █████╗ ██████╗ ███████╗██████╗
// Models shared with the app from another one (share sheet, Open in) start a new project

static string_array_t *base_open_paths = NULL;

void base_on_open_file(char *path) {
	if (base_open_paths == NULL) {
		base_open_paths = any_array_create_from_raw((void *[]){}, 0);
	}
	any_array_push(base_open_paths, string_copy(path));
}

static void base_open_new_project(void *path) {
	project_new(true);
	sys_title_set(substring(path, string_last_index_of(path, PATH_SEP) + 1, string_last_index_of(path, "."))); // The model names it
	path = project_take_import(path); // iOS: into the project's Sources folder, named "<model> 2"... when taken
	import_mesh_run(path, true, true, false);
	console_info(tr("Model opened in a new project"));
}

static void base_open_after_save(void *path) {
	sys_notify_on_next_frame(&base_open_new_project, path);
}

static void base_handle_open_paths() {
	if (base_open_paths == NULL || base_open_paths->length == 0) {
		return;
	}
	char *path = array_shift(base_open_paths);
	if (!path_is_mesh(path)) {
		console_error(strings_unknown_asset_format());
		return;
	}
	box_projects_cancel_load(); // Instead of reopening the last project on startup
	box_projects_native_shown   = false;
	box_projects_import_pending = false;
	ui_box_hide();
	ui_menu_show = false;
	// The open project is kept: saved first when it has a file
	if (!string_equals(g_project->_->filepath, "")) {
		project_save(false);
	}
	sys_notify_on_next_frame(&base_open_after_save, path);
}

void base_handle_drop_paths() {
	if (base_drop_paths->length > 0) {
		bool wait = false;
#if defined(IRON_MACOS)
		wait = !mouse_moved; // Mouse coords not updated during drag
#endif
		if (!wait) {
			base_drop_x     = mouse_x;
			base_drop_y     = mouse_y;
			char *drop_path = array_shift(base_drop_paths);
			if (box_projects_import_pending && box_projects_native_shown) { // Picked from the projects screen's Import
				box_projects_import_pending = false;
				base_on_open_file(drop_path);
				return;
			}
			import_asset_run(project_take_import(drop_path), base_drop_x, base_drop_y, true, true, NULL);
		}
	}
}

void base_update(void *_) {
	util_pencil_update();
	brush_previews_update();
	ui_quick_menu_update();

	if (g_project->_->translucency && any_map_get(render_path_render_targets, "main_paint") == NULL) {
		render_path_base_init_paint_depth();
	}

	if (mouse_movement_x != 0 || mouse_movement_y != 0) {
		iron_mouse_set_cursor(IRON_CURSOR_ARROW);
	}

	if (g_context->color_picker_callback != NULL && (mouse_released("left") || mouse_released("right"))) {
		g_context->color_picker_callback = NULL;
		context_select_tool(g_context->color_picker_previous_tool);
	}

	base_handle_drop_paths();
	base_handle_open_paths();

	if (base_workspace_pending) {
		base_apply_workspace();
	}
	ui_dock_layout();
	ui_view2d_update(NULL);
	ui_nodes_update(NULL);
	ui_base_update(NULL);
	camera_update(NULL);
	history_settle_after_stroke();

	bool in_3d               = mouse_view_x() >= 0 && mouse_view_x() <= sys_w() && mouse_view_y() >= 0 && mouse_view_y() <= sys_h();
	iron_mouse_trackpad_drag = base_ui_enabled && (in_3d || context_in_2d_view(ui_view2d_type) || context_in_nodes());

	if (g_context->frame == 2) {
		util_render_make_material_preview();
		make_material_parse_mesh_material();
		make_material_parse_paint_material(true);
		g_context->ddirty = 0;
	}
	else if (g_context->frame == 3) {
		g_context->ddirty = 3;
	}

	g_context->frame++;

	bool using_menu = ui_menu_show || ui_quick_menu_blocking();
	base_ui_enabled = !ui_box_show && !using_menu && g_ui->combo_selected_id == 0 && !(ui_nodes_show && ui_nodes_popup_hovered()) && !material_studio_show;

	ui_nodes_render_popup();
	if (ui_box_show) {
		ui_box_render();
	}
	if (ui_menu_show) {
		ui_menu_render();
	}
	ui_quick_menu_render();

	base_draw_startup_cover();

#if defined(IRON_IOS)
	// No mouse move events for touch, re-init last paint position on touch start
	if (!mouse_down("left")) {
		g_context->last_paint_x = -1;
		g_context->last_paint_y = -1;
	}
#endif
}

// A trackpad or mouse pointer over the canvas hides, the brush cursor shows instead
bool base_pointer_hidden(float x, float y) {
	tool_type_t tool   = g_context->tool;
	bool        cursor = tool == TOOL_TYPE_BRUSH || tool == TOOL_TYPE_ERASER || tool == TOOL_TYPE_CLONE || tool == TOOL_TYPE_BLUR ||
	              tool == TOOL_TYPE_PARTICLE || tool == TOOL_TYPE_DECAL || tool == TOOL_TYPE_TEXT;
	return cursor && base_ui_enabled && !ui_quick_menu_show && x >= sys_x() && x < sys_x() + sys_w() && y >= sys_y() && y < sys_y() + sys_h();
}

// UI font: the system's on Apple platforms (SF), the bundled Roboto elsewhere or when it can not be read
char *base_ui_font_file() {
#if defined(IRON_MACOS) || defined(IRON_IOS)
	static char *system_font = NULL;
	static bool  looked      = false;
	if (!looked) {
		looked         = true;
		const char *p  = iron_system_font_path();
		if (p != NULL && iron_file_exists((char *)p)) {
			system_font = string_copy((char *)p);
		}
	}
	if (system_font != NULL) {
		return system_font;
	}
#endif
	return "font.ttf";
}

void base_init() {
	sys_notify_on_drop_files(&base_on_drop_files);
	iron_set_open_file_callback(&base_on_open_file);
	sys_notify_on_app_state(&base_on_foreground, &base_on_background, &base_on_shutdown);
	iron_set_resign_active_callback(&base_on_resign_active);
	iron_set_memory_warning_callback(&base_on_memory_warning);
	iron_set_pencil_gesture_callback(&util_pencil_on_gesture);
	iron_set_pointer_hidden_callback(&base_pointer_hidden);
	iron_set_save_and_quit_callback(base_save_and_quit_callback);
	project_autosave_init();
	iron_idle_busy = base_idle_busy;

	g_font = data_get_font(base_ui_font_file());

	config_load_theme(g_config->theme, false);
	base_default_element_w = g_theme->ELEMENT_W;
	base_default_element_h = g_theme->ELEMENT_H;
	base_default_font_size = g_theme->FONT_SIZE;
	translator_load_translations(g_config->locale);

	ui_files_filename = string_copy(tr("untitled"));
#if defined(IRON_IOS)
	sys_title_set(tr("untitled"));
#endif

	// Baked font for fast startup, it is the bundled one
	if (string_equals(g_config->locale, "en") && string_equals(base_ui_font_file(), "font.ttf")) {
		draw_font_13(g_font);
	}
	else {
		draw_font_init(g_font);
	}

	ui_nodes_enum_texts    = base_combo_enum_texts;
	ui_nodes_enum_textures = base_combo_enum_textures;

	if (!g_config->texture_filter) {
		gpu_use_linear_sampling(false);
	}

	args_parse();
	camera_init();
	ui_base_init();
	ui_viewnodes_init();
	ui_view2d_init();

	sys_notify_on_update(base_update, NULL);

	scene_camera->data->fov = math_floor(scene_camera->data->fov * 100) / 100.0;
	camera_object_build_proj(scene_camera, -1.0);

	args_run();

	g_context->camera_pivot    = g_config->camera_pivot;
	g_context->camera_controls = g_config->camera_controls;

	library_init();
	brush_previews_start(); // Read from disk while the project loads under the startup cover

	bool  has_projects = g_config->recent_projects->length > 0;
	char *reload       = NULL;
	bool  shared       = base_open_paths != NULL && base_open_paths->length > 0; // Launched by sharing a model
	if (g_config->reload_project && has_projects && !args_use && !shared) {
		reload = box_projects_recent_path(0);
		if (!iron_file_exists(reload)) {
			reload = NULL;
		}
	}
	if (reload != NULL) {
		base_startup_cover = true;
		box_projects_load(reload);
	}
	else if (g_config->splash_screen && has_projects && !args_use) { // Launched with a file or a script, show it right away
		box_projects_show();
		box_projects_native_closable = false; // No project behind it yet
	}
}

i32 base_w() {
	// Drawing material preview
	if (g_context->material_preview) {
		return util_render_material_preview_size;
	}

	// Drawing decal preview
	if (g_context->decal_preview) {
		return util_render_decal_preview_size;
	}

	if (g_context->paint2d_view) {
		return ui_view2d_ww;
	}

	// 3D view is hidden
	if (!base_view3d_show) {
		return 1;
	}

	i32 res = base_view_w();
	return res > 0 ? res : 1; // App was minimized, force render path resize
}

// Width of the 3D view tab next to the toolbar
i32 base_view3d_w() {
	if (g_context->paint2d_view) {
		return ui_view2d_ww;
	}
	return ui_dock_view3d_w();
}

// Split view draws two 3D views next to each other, each one with its own toolbar and header
bool base_is_split() {
	return g_context->split_view && base_view3d_show;
}

// View being rendered, the view under the mouse cursor otherwise
i32 base_view_index() {
	if (!base_is_split()) {
		return 0;
	}
	if (g_context->view_index > -1) {
		return g_context->view_index;
	}
	return g_context->view_index_last > 0 ? 1 : 0;
}

i32 base_view_w() {
	i32 res = base_view3d_w();
	if (base_is_split()) {
		res = math_floor(res / 2.0);
	}
	return res;
}

i32 base_view_x(i32 view_index) {
	return view_index == 1 ? base_appx + base_view3d_w() - base_view_w() : base_appx;
}

i32 base_view_at_mouse() {
	if (!base_is_split()) {
		return -1;
	}
	if (mouse_y >= base_appy && mouse_y < base_appy + base_h()) {
		for (i32 i = 0; i < 2; ++i) {
			if (mouse_x >= base_view_x(i) && mouse_x < base_view_x(i) + base_view_w()) {
				return i;
			}
		}
	}
	return -1;
}

i32 base_h() {
	// Drawing material preview
	if (g_context->material_preview) {
		return util_render_material_preview_size;
	}

	// Drawing decal preview
	if (g_context->decal_preview) {
		return util_render_decal_preview_size;
	}

	// 2D view paints into the area below its header
	i32 res = ui_dock_view3d_h();
	if (g_context->paint2d_view || (!base_view3d_show && ui_view2d_show)) {
		res = ui_view2d_wh;
	}

	return res > 0 ? res : 1; // App was minimized, force render path resize
}

// The cursor hovering the 2D view is mapped the same way as while painting in it,
// the stroke begins at the last hovered position
bool base_paint_in_2d() {
	// The brush inputs are read before the 2D view updates paint2d for the frame,
	// keep the view the stroke was started in
	static bool started_in_2d = false;
	bool        in_2d         = context_in_2d_view(VIEW_2D_TYPE_LAYER);
	if (mouse_started("left") || pen_started("tip")) {
		started_in_2d = in_2d;
	}
	if (mouse_down("left") || pen_down("tip")) {
		return g_context->paint2d || started_in_2d;
	}
	return in_2d;
}

// Paint coordinates are relative to the view being painted, the 2D view is a 3D view of the layer plane
i32 base_paint_view_x() {
	return base_paint_in_2d() ? ui_view2d_wx : base_x();
}

i32 base_paint_view_y() {
	return base_paint_in_2d() ? ui_view2d_wy : base_y();
}

i32 base_paint_view_w() {
	return base_paint_in_2d() ? ui_view2d_ww : base_w();
}

i32 base_paint_view_h() {
	return base_paint_in_2d() ? ui_view2d_wh : base_h();
}

// Width over height of the window, 1 while it has no height
f32 base_window_aspect() {
	return sys_aspect();
}

// Width over height of the view painted in (3D or 2D), 1 while it has no height
f32 base_paint_view_aspect() {
	i32 h = base_paint_view_h();
	return h > 0 ? base_paint_view_w() / (f32)h : 1.0;
}

// Screen space brush scale of a view h pixels high
f32 base_scale2d(i32 h) {
	return (900 / (f32)(h > 0 ? h : 1)) * g_config->window_scale;
}

f32 base_paint_x(f32 screen_x) {
	return (screen_x - base_paint_view_x()) / (f32)base_paint_view_w();
}

f32 base_paint_y(f32 screen_y) {
	return (screen_y - base_paint_view_y()) / (f32)base_paint_view_h();
}

f32 base_paint_screen_x(f32 paint_x) {
	return base_paint_view_x() + paint_x * base_paint_view_w();
}

f32 base_paint_screen_y(f32 paint_y) {
	return base_paint_view_y() + paint_y * base_paint_view_h();
}

i32 base_x() {
	if (g_context->paint2d_view) {
		return ui_view2d_wx;
	}
	return base_view_x(base_view_index());
}

i32 base_y() {
	if (g_context->paint2d_view) {
		return ui_view2d_wy;
	}
	return base_appy;
}

void base_on_resize() {
	if (iron_window_width() == 0 || iron_window_height() == 0) {
		return;
	}

	base_resize();
	base_save_window_rect();
}

void base_resize() {
	render_path_base_redraw(); // The 3D view shows the change
	if (iron_window_width() == 0 || iron_window_height() == 0) {
		return;
	}

	ui_dock_layout();

	camera_object_t *cam = scene_camera;
	if (cam->data->ortho != NULL) {
		cam->data->ortho->buffer[2] = -2 * (sys_h() / (float)sys_w());
		cam->data->ortho->buffer[3] = 2 * (sys_h() / (float)sys_w());
	}
	camera_object_build_proj(cam, -1.0);
	scene_camera->frame = 0;

	if (g_context->camera_type == CAMERA_TYPE_ORTHOGRAPHIC) {
		viewport_update_camera_type(g_context->camera_type);
	}

	g_context->ddirty = 2;

	ui_nodes_grid_redraw = true;

	base_redraw_ui();
}

string_array_t *base_combo_enum_texts(char *node_type) {
	static string_array_t texts = {0};
	texts.length                = 0;

	if (string_equals(node_type, "TEX_IMAGE") || string_equals(node_type, "image_texture_node")) {
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			string_array_push(&texts, g_project->_->assets->buffer[i]->name);
		}
		if (texts.length == 0) {
			string_array_push(&texts, "");
		}
		return &texts;
	}

	if (string_equals(node_type, "LAYER") || string_equals(node_type, "LAYER_MASK") || string_equals(node_type, "MESH_DISPLACE")) {
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			string_array_push(&texts, l->name);
		}
		return &texts;
	}

	if (string_equals(node_type, "MATERIAL")) {
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t *m = g_project->_->materials->buffer[i];
			string_array_push(&texts, m->canvas->name);
		}
		return &texts;
	}

	if (string_equals(node_type, "MESH_OBJECT")) {
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			string_array_push(&texts, g_project->_->paint_objects->buffer[i]->base->name);
		}
		return &texts;
	}

	return NULL;
}

any_array_t *base_combo_enum_textures(char *node_type) {
	if (string_equals(node_type, "TEX_IMAGE")) {
		any_array_t *ar = any_array_create(0);
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			asset_t       *asset = g_project->_->assets->buffer[i];
			gpu_texture_t *img   = project_get_image(asset);
			any_array_push(ar, img);
		}
		return ar;
	}

	if (string_equals(node_type, "LAYER") || string_equals(node_type, "LAYER_MASK") || string_equals(node_type, "MESH_DISPLACE")) {
		any_array_t *ar = any_array_create(0);
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			any_array_push(ar, l->texpaint_preview);
		}
		return ar;
	}

	if (string_equals(node_type, "MATERIAL")) {
		any_array_t *ar = any_array_create(0);
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t *m = g_project->_->materials->buffer[i];
			any_array_push(ar, m->image_icon);
		}
		return ar;
	}

	if (string_equals(node_type, "image_texture_node")) {
		any_array_t *ar = any_array_create(0);
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			asset_t       *asset = g_project->_->assets->buffer[i];
			gpu_texture_t *img   = project_get_image(asset);
			any_array_push(ar, img);
		}
		return ar;
	}

	return NULL;
}

i32 base_get_asset_index_by_file(char *file) {
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		if (string_equals(g_project->_->assets->buffer[i]->file, file)) {
			return i;
		}
	}
	return 0;
}

bool base_is_decal_layer() {
	return g_context->layer->fill != NULL &&
	       (g_context->layer->fill->projection == FILL_PROJECTION_DECAL || g_context->layer->fill->projection == FILL_PROJECTION_PLANAR);
}

void base_redraw_ui() {
	ui_nodes_hwnd->redraws          = 2;
	ui_box_hwnd->redraws            = 2;
	ui_view2d_hwnd->redraws         = 2;
	if (g_context->ddirty < 0) {
		g_context->ddirty = 0;
	}
	if (g_context->split_view) {
		g_context->ddirty = 1;
	}
}

// The views are laid out anew once the windows of the frame are done
void base_set_workspace(workspace_preset_t preset) {
	base_workspace_preset  = preset;
	base_workspace_pending = true;
}

void base_apply_workspace() {
	base_workspace_pending = false;
	if (base_workspace_preset == WORKSPACE_PRESET_NONE) {
		ui_dock_load();
		base_resize();
		return;
	}
	ui_dock_reset();
	if (base_workspace_preset == WORKSPACE_PRESET_VIEW_2D) {
		ui_dock_show_view(DOCK_VIEW_3D, false);
		ui_dock_show_view(DOCK_VIEW_2D, true);
	}
	else if (base_workspace_preset == WORKSPACE_PRESET_NODES) {
		ui_dock_show_view(DOCK_VIEW_3D, false);
		ui_dock_show_view(DOCK_VIEW_NODES, true);
	}
	base_resize();
}

void base_update_tools() {
	if (g_context->tool >= TOOL_TYPE_MATERIAL && g_context->tool <= TOOL_TYPE_CURVE) {
		context_select_tool(TOOL_TYPE_BRUSH); // No longer a tool
	}

	for (i32 i = g_project->_->layers->length - 1; i >= 0; --i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_layer(l)) {
			context_set_layer(l);
			break;
		}
	}
}

uint32_t base_darker(uint32_t x, uint32_t y) {
	uint32_t r  = ((x >> 16) & 0xff);
	uint32_t g  = ((x >> 8) & 0xff);
	uint32_t b  = ((x) & 0xff);
	uint32_t ry = ((y >> 16) & 0xff);
	uint32_t gy = ((y >> 8) & 0xff);
	uint32_t by = ((y) & 0xff);
	r           = r > ry ? r - ry : 0;
	g           = g > gy ? g - gy : 0;
	b           = b > by ? b - by : 0;
	return (x & 0xff000000) | (r << 16) | (g << 8) | b;
}

string_array_t *base_get_viewport_modes() {
	string_array_t *modes = any_array_create_from_raw(
	    (void *[]){
	        tr("Lit"),
	        tr("Base Color"),
	        tr("Normal"),
	        tr("Occlusion"),
	        tr("Roughness"),
	        tr("Metallic"),
	        tr("Opacity"),
	        tr("Height"),
	        tr("Emission"),
	        tr("Subsurface"),
	        tr("TexCoord"),
	        tr("Object Normal"),
	        tr("World Normal"),
	        tr("Material ID"),
	        tr("Object ID"),
	        tr("Mask"),
	    },
	    16);

	if (gpu_raytrace_supported()) {
		any_array_push(modes, tr("Path Traced"));
	}

	return modes;
}

string_array_t *base_get_viewport_mode_shortcuts() {
	string_array_t *shortcuts = any_array_create_from_raw(
	    (void *[]){
	        "l",
	        "b",
	        "n",
	        "o",
	        "r",
	        "m",
	        "a",
	        "h",
	        "e",
	        "s",
	        "t",
	        "1",
	        "w",
	        "2",
	        "3",
	        "4",
	    },
	    16);

	if (gpu_raytrace_supported()) {
		any_array_push(shortcuts, "p");
	}

	return shortcuts;
}

// Full screen progress bar of the project import and export, drawn and presented on its own
static f64 base_progress_last = 0.0;

void base_draw_progress(f32 progress) {
	if (gpu_in_use) {
		return;
	}
	f64 t = iron_time();
	if (progress > 0.0 && progress < 1.0 && t - base_progress_last < 1.0 / 30.0) {
		return;
	}
	base_progress_last = t;
	i32  w                  = iron_window_width();
	i32  h                  = iron_window_height();
	i32  bw                 = w / 4;
	i32  bh                 = math_floor(16 * UI_SCALE()); // Centered on the screen
	i32  bx                 = (w - bw) / 2;
	i32  by                 = (h - bh) / 2;
	// The frame is presented on its own: a drawable not cleared holds undefined content (it flashed pink during loads)
	draw_begin(NULL, true, base_startup_covering() ? 0xff000000 : 0xff202020);
	draw_set_color(g_theme->BUTTON_COL);
	draw_filled_rect(bx, by, bw, bh);
	draw_set_color(0xffe0393e); // Red
	draw_filled_rect(bx, by, bw * progress, bh);
	draw_end();
	gpu_present();
}
