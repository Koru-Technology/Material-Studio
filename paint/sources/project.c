
#include "global.h"

bool _project_save_and_quit;
bool _project_import_mesh_replace_existing;
void (*_project_import_mesh_done)(void);
bool     _project_import_asset_hdr_as_envmap;
bool     _project_import_swatches_replace_existing;
asset_t *_project_reimport_texture_asset;

void project_open_on_file_picked(char *path) {
	if (!path_has_ext(path, "arm")) {
		console_error(strings_arm_file_expected());
		return;
	}

	draw_saved_t saved_draw = draw_suspend();

	import_arm_run_project(path);
	draw_resume(saved_draw);
}

void project_open() {
	ui_files_show("arm", false, false, &project_open_on_file_picked);
}

void project_save_on_next_frame(void *_) {
	export_arm_run_project(g_project->_->filepath);
	if (_project_save_and_quit) {
		iron_stop();
	}
}

// The preview of a project file, hidden next to it: .<name>_icon.png
char *project_icon_path(char *path) {
	i32   slash = string_last_index_of(path, "/");
	char *name  = substring(path, slash + 1, string_length(path) - 4); // Without .arm
	return string("%s/.%s_icon.png", substring(path, 0, slash), name);
}

#ifdef IRON_IOS
// The iPad's Documents folder, what the Files app shows:
//   Projects/<name>/              one folder per project
//     <name>.arm                  the project, .<name>_icon.png its preview (hidden)
//     Sources/                    models and textures imported into it (project_take_import)
//     Exports/                    texture, mesh and layer exports (ui_files_show)
//     Mesh Maps/                  baked maps (box_bake_maps.c)
//   .imports/                     files picked or shared with the app until a project takes them (ios_system.m)
//   .tests/                       test scripts (README)
// The container is /var/mobile/... in the paths iOS hands over (pickers, launch arguments) and /private/var/mobile/...
// in HOME, the same folder: paths are compared in the /var form
static char *project_canonical(char *path) {
	return starts_with(path, "/private/var/") ? substring(path, 8, string_length(path)) : path;
}

char *project_documents_dir() {
	return project_canonical(string("%s/Documents", getenv("HOME")));
}

static char *project_folder_of(char *name) {
	return string("%s/Projects/%s", project_documents_dir(), name);
}

static char *project_file_of(char *name) {
	return string("%s/%s.arm", project_folder_of(name), name);
}

static char *project_name_of_folder(char *dir) {
	return substring(dir, string_last_index_of(dir, "/") + 1, string_length(dir));
}

// Folder of the unsaved project, made when it first needs one (an import, an export, its first save), NULL before
static char *project_unsaved_folder = NULL;

// "name", or "name 2", "name 3"... when a project folder with the name exists
static char *project_free_name(char *name) {
	char *free_name = name;
	for (i32 i = 2; iron_file_exists(project_folder_of(free_name)); ++i) {
		free_name = string("%s %d", name, i);
	}
	return string_copy(free_name);
}

// The current project's folder. An unsaved project's is made on first use and named after its title, the title then
// follows the folder: its files are saved there whatever the project is later called
char *project_folder() {
	if (!string_equals(g_project->_->filepath, "")) {
		return substring(g_project->_->filepath, 0, string_last_index_of(g_project->_->filepath, "/"));
	}
	if (project_unsaved_folder == NULL) {
		char *name             = project_free_name(sys_title());
		project_unsaved_folder = string_copy(project_folder_of(name));
		iron_create_directory(project_unsaved_folder);
		sys_title_set(name);
	}
	return project_unsaved_folder;
}

// A folder of the current project (Sources, Exports, Mesh Maps), made when missing
char *project_subfolder(char *name) {
	char *dir = string("%s/%s", project_folder(), name);
	iron_create_directory(dir);
	return dir;
}

// An unsaved project is left (new project, another one opened): its folder goes, unless a save put the project in it
static void project_drop_unsaved_folder() {
	char *dir = project_unsaved_folder;
	project_unsaved_folder = NULL;
	if (dir != NULL && string_equals(g_project->_->filepath, "") &&
	    !iron_file_exists(string("%s/%s.arm", dir, project_name_of_folder(dir)))) {
		iron_delete_file(dir);
	}
}

// A file picked or shared with the app lands in Documents/.imports (ios_system.m): it moves into the project's
// Sources folder as the project imports it. A model imported into a new project names it.
char *project_take_import(char *path) {
	char *imports = string("%s/.imports/", project_documents_dir());
	if (!starts_with(project_canonical(path), imports)) {
		return path;
	}
	path       = project_canonical(path);
	char *name = substring(path, string_length(imports), string_length(path));
	if (string_equals(g_project->_->filepath, "") && project_unsaved_folder == NULL && path_is_mesh(path)) {
		sys_title_set(substring(name, 0, string_last_index_of(name, ".")));
	}
	char *dst = string("%s/%s", project_subfolder("Sources"), name);
	iron_delete_file(dst); // Picked again: the new file replaces it
	if (rename(path, dst) != 0) {
		return path;
	}
	return string_copy(dst);
}
#else
char *project_take_import(char *path) {
	return path;
}
#endif

// The recent projects list entry of a project file: on iOS relative to Documents, the container moves between installs
char *project_recent_entry(char *path) {
#ifdef IRON_IOS
	char *docs = string("%s/", project_documents_dir());
	if (starts_with(project_canonical(path), docs)) {
		path = project_canonical(path);
		return string_copy(substring(path, string_length(docs), string_length(path)));
	}
#endif
	return path;
}

// Untitled projects are saved in their folder on mobile, false when a save dialog is needed
static bool project_set_default_path() {
#ifdef IRON_IOS
	char *dir              = project_folder();
	char *name             = project_name_of_folder(dir);
	g_project->_->filepath = string("%s/%s.arm", dir, name);
	sys_title_set(name);
	return true;
#else
	return false;
#endif
}

static bool project_autosave_busy() {
	return g_context->brush_time > 0 || box_bake_maps_running() || g_context->paint_object == NULL;
}

// Wall clock time of the first change since the last save, 0 when unchanged.
// Updates stop while the app is idle, the time keeps running and the idle wake checks it.
static f64 project_autosave_since = 0.0;

static bool project_autosave_due() {
	if (!g_config->autosave || !project_modified) {
		project_autosave_since = 0.0;
		return false;
	}
	if (project_autosave_since == 0.0) {
		project_autosave_since = iron_time();
	}
	i32 minutes = g_config->autosave_interval < 1 ? 1 : g_config->autosave_interval;
	return iron_time() - project_autosave_since >= minutes * 60.0;
}

// Saves modified projects every few minutes, from the update loop. Journals the session head for crash recovery.
void project_autosave_update() {
	project_session_update();
	if (!project_autosave_due() || project_autosave_busy()) {
		return;
	}
#ifndef IRON_IOS
	if (string_equals(g_project->_->filepath, "")) {
		return; // No surprise save dialog
	}
#endif
	project_autosave_since = 0.0;
	project_save(false);
	console_info(tr("Auto saved"));
}

static bool (*project_idle_wake_next)(void) = NULL;

static bool project_idle_wake(void) {
	return project_autosave_due() || project_session_due() || (project_idle_wake_next != NULL && project_idle_wake_next());
}

// Wakes the idle app up when an auto save is due
void project_autosave_init() {
	project_idle_wake_next = iron_idle_wake;
	iron_idle_wake         = project_idle_wake;
}

// Saves right away instead of on the next frame, for when the app is about to be suspended or closed
void project_save_now() {
	if (!g_config->autosave || !project_modified || project_autosave_busy()) {
		if (project_modified && project_session_changed()) {
			project_session_write(); // Not saved: the session head keeps the work for a crash
		}
		return;
	}
	if (string_equals(g_project->_->filepath, "") && !project_set_default_path()) {
		return;
	}
	export_arm_run_project(g_project->_->filepath);
	project_autosave_since = 0.0;
}

void project_save(bool save_and_quit) {
	if (string_equals(g_project->_->filepath, "") && !project_set_default_path()) {
		project_save_as(save_and_quit);
		return;
	}

#ifdef IRON_MACOS
	char *filename = substring(g_project->_->filepath, string_last_index_of(g_project->_->filepath, PATH_SEP) + 1, string_length(g_project->_->filepath) - 4);
	sys_title_set(string("%s - %s", filename, manifest_title));
#endif

	_project_save_and_quit = save_and_quit;

	sys_notify_on_next_frame(&project_save_on_next_frame, NULL);
}

// Saved as another file: the copy gets its own uuid, the session's history journal follows it
static void project_saved_as(char *old_path) {
	if (string_equals(old_path, "") || string_equals(old_path, g_project->_->filepath)) {
		return;
	}
	char *old = project_uuid_get();
	project_uuid_new();
	hstore_journal_rebind(old);
}

void project_save_as_on_file_picked(char *path) {
	char *f = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	char *old_path         = g_project->_->filepath;
	g_project->_->filepath = string("%s%s%s", path, PATH_SEP, f);
	if (!ends_with(g_project->_->filepath, ".arm")) {
		g_project->_->filepath = string("%s.arm", g_project->_->filepath);
	}
	project_saved_as(old_path);
	project_save(_project_save_and_quit);
}

#ifdef IRON_IOS
// No save dialog on iOS: the project is named here and saved in its folder
static char *project_save_as_name = "";

static void project_save_as_box() {
	ui_text_input(&project_save_as_name, tr("Name"), UI_ALIGN_LEFT, true, false);
	char *name = string_copy(string_replace_all(trim_end(project_save_as_name), "/", "-"));
	bool  ok   = !string_equals(name, "") && !starts_with(name, ".");
	if (ok && iron_file_exists(project_file_of(name))) {
		ui_text(tr("A project with this name exists, it will be replaced."), UI_ALIGN_LEFT, 0x00000000);
	}
	ui_row2();
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
	g_ui->enabled = ok;
	bool save     = ui_icon_button(tr("Save"), ICON_SAVE, UI_ALIGN_CENTER);
	g_ui->enabled = true;
	if (ok && (save || g_ui->is_return_down)) {
		char *old_path         = g_project->_->filepath;
		iron_create_directory(project_folder_of(name));
		g_project->_->filepath = project_file_of(name);
		project_saved_as(old_path);
		sys_title_set(name);
		ui_box_hide();
		project_save(_project_save_and_quit);
	}
}
#endif

void project_save_as(bool save_and_quit) {
	_project_save_and_quit = save_and_quit;
#ifdef IRON_IOS
	project_save_as_name = string_copy(sys_title());
	ui_box_show_custom(&project_save_as_box, 400, 200, NULL, true, tr("Save As"));
#else
	ui_files_show("arm", true, false, &project_save_as_on_file_picked);
#endif
}

void project_cleanup() {
	util_mesh_remove_merged();

	if (g_project->_->paint_objects != NULL) {
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *p = g_project->_->paint_objects->buffer[i];
			object_set_parent(p->base, NULL);
		}
		util_mesh_remove_objects(g_project->_->paint_objects, g_context->paint_object);
	}

	if (g_context->paint_object != NULL) {
		util_mesh_delete_data_uncache(g_context->paint_object->data);
	}

	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		data_delete_texture(a->file);
	}


}

void project_new_on_next_frame(void *_) {
	// Once layers and meshes are populated on project open
	util_render_make_material_preview();
	g_context->ddirty = 4;
}

void project_new_reset_layers(void *_) {
	layers_init();
}

void project_new_resize_layers(void *_) {
	layers_resize();
}

u32 project_uid_take() {
	return project_uid_next++;
}

void project_uuid_new() {
	u8 b[16];
	arc4random_buf(b, sizeof(b));
	char *s = malloc(33);
	for (i32 i = 0; i < 16; ++i) {
		snprintf(s + i * 2, 3, "%02x", b[i]);
	}
	project_uuid = s;
}

char *project_uuid_get() {
	if (project_uuid == NULL) {
		project_uuid_new();
	}
	return project_uuid;
}

// The templates of a new project: the meshes in data/meshes and the procedural shapes (util_shape_make_data, which
// also makes the dense plane_2048 and sphere_2048 scripts ask for by name)
void project_fetch_default_meshes() {
	if (project_default_mesh_list == NULL) {
		project_default_mesh_list = file_read_directory(string("%s%smeshes", path_data(), PATH_SEP));
		for (i32 i = 0; i < project_default_mesh_list->length; ++i) {
			char *s                              = project_default_mesh_list->buffer[i];
			project_default_mesh_list->buffer[i] = substring(project_default_mesh_list->buffer[i], 0, string_length(s) - 4); // Trim .arm
		}
		// Procedural
		any_array_push(project_default_mesh_list, "empty");
		any_array_push(project_default_mesh_list, "plane");
		any_array_push(project_default_mesh_list, "cube");
		any_array_push(project_default_mesh_list, "sphere");
		any_array_push(project_default_mesh_list, "cone");
		any_array_push(project_default_mesh_list, "cylinder");
		any_array_push(project_default_mesh_list, "capsule");
		any_array_push(project_default_mesh_list, "torus");
#ifdef WITH_EMBED
		any_array_push(project_default_mesh_list, "cube_bevel");
#endif

		if (g_context->project_type == -1) {
			g_context->project_type = string_array_index_of(project_default_mesh_list, "cube_bevel");
		}
	}
}

void project_new(bool reset_layers) {
#ifdef IRON_IOS
	project_drop_unsaved_folder();
#endif
	project_uid_next = 1;
	if (g_context->paint_object != NULL) {
		project_cleanup();
		g_project->_->filepath = "";
	}
	g_project->linear_blending = true; // New projects blend colors in linear space
	g_project->id_map          = NULL;
	g_project->mesh_maps       = NULL;
	g_project->uuid            = NULL;
	g_project->next_uid        = 0;
	g_project->timelines       = NULL;
	g_project->history         = NULL;
	g_project->_->translucency = false;

	if (g_project->_->layers->length == 0) {
		any_array_push(g_project->_->layers, slot_layer_create("", LAYER_SLOT_TYPE_LAYER, NULL));
		g_context->layer = g_project->_->layers->buffer[0];
	}

	g_context->layer_preview_dirty = true;
	g_context->layer_filter        = 0;
	g_context->texture             = NULL;
	g_project->mesh_assets         = any_array_create_from_raw((void *[]){}, 0);

	bool  no_default = project_default_mesh_list == NULL || g_context->project_type == -1;
	char *mesh_name  = project_new_template != NULL ? project_new_template
	                   : no_default                 ? "cube_bevel"
	                                                : project_default_mesh_list->buffer[g_context->project_type];

	mesh_data_t *md = util_shape_make_data(mesh_name);
	if (md != NULL) {
		md->name = "Tessellated";
	}
	else {
		buffer_t *b         = data_get_blob(string("meshes/%s.arm", mesh_name));
		scene_t  *scene_raw = armpack_decode(b);
		md                  = mesh_data_create(scene_raw->mesh_datas->buffer[0]);
	}

	draw_saved_t saved_draw = draw_suspend();

	shader_data_t *m = data_get_shader("Scene", "Material");
	if (g_context->paint_object == NULL) {
		g_context->paint_object             = scene_add_mesh_object(md, m, NULL);
		g_context->paint_object->base->name = "paint_object";
	}
	else {
		mesh_object_set_data(g_context->paint_object, md);
	}

	g_project->_->paint_objects = any_array_create_from_raw(
	    (void *[]){
	        g_context->paint_object,
	    },
	    1);
	g_context->paint_object = context_main_object();
	context_select_paint_object(context_main_object());

	g_context->paint_object->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
	transform_build_matrix(g_context->paint_object->base->transform);
	g_context->paint_object->base->name    = "Tessellated";
	g_context->paint_object->base->visible = true;

	while (g_project->_->materials->length > 0) {
		slot_material_unload(array_pop(g_project->_->materials));
	}
	any_array_push(g_project->_->materials, slot_material_create(m, NULL));

	g_context->picker_material_mask        = false;
	g_context->picker_color_mask           = false;
	g_context->picker_hide_other_materials = false;
	g_context->material                    = g_project->_->materials->buffer[0];
	ui_nodes_hwnd->redraws                 = 2;
	ui_nodes_group_stack                   = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->material_groups          = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->brushes                  = any_array_create_from_raw((void *[]){}, 0);
	any_array_push(g_project->_->brushes, slot_brush_create(NULL));
	g_context->brush    = g_project->_->brushes->buffer[0];
	draw_font_t *default_font = data_get_font("font.ttf"); // Text layers keep the bundled font, the same on every platform
	draw_font_init(default_font);
	g_project->_->fonts = any_array_create_from_raw(
	    (void *[]){
	        slot_font_create("default.ttf", default_font, ""),
	    },
	    1);
	g_context->font      = g_project->_->fonts->buffer[0];
	project_set_default_swatches();
	g_context->swatch                = g_project->swatches->buffer[0];
	g_context->picked_color          = project_make_swatch(0xffffffff);
	g_context->color_picker_callback = NULL;
	history_reset();
	hstore_reset(); // Deletes the closed project's journal
	project_container_forget();
	project_session_forget();
	project_uuid = NULL;

	make_material_parse_paint_material(true);
	make_material_parse_brush();

	g_project->_->assets        = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->next_asset_id = 0;
	g_project->packed_assets    = any_array_create_from_raw((void *[]){}, 0);
	g_context->ddirty           = 4;
	if (reset_layers) {
		bool res_changed = g_project->_->layers->buffer[0]->texpaint->width != config_get_texture_res_x() ||
		                   g_project->_->layers->buffer[0]->texpaint->height != config_get_texture_res_y();
		while (g_project->_->layers->length > 0) {
			slot_layer_unload(array_pop(g_project->_->layers));
		}
		slot_layer_t *layer = slot_layer_create("", LAYER_SLOT_TYPE_LAYER, NULL);
		any_array_push(g_project->_->layers, layer);
		context_set_layer(layer);
		if (res_changed) {
			sys_notify_on_next_frame(&project_new_resize_layers, NULL);
		}
		sys_notify_on_next_frame(&project_new_reset_layers, NULL);
	}

	draw_resume(saved_draw);

	base_update_tools();
	project_set_default_envmap();
	context_select_tool(TOOL_TYPE_BRUSH);
	context_parse_mesh_material(); // The new project's
	scene_camera->data->near_plane      = 0.01;
	scene_camera->data->far_plane       = 100.0;
	scene_camera->data->frustum_culling = false;
	viewport_reset();
	viewport_scale_to_bounds(1.8);
	render_path_raytrace_ready = false;

	sys_notify_on_next_frame(&project_new_on_next_frame, NULL);
}

void project_set_default_envmap() {
	g_context->saved_envmap          = NULL;
	g_context->envmap_loaded         = false;
	scene_world->_->envmap           = g_context->empty_envmap;
	scene_world->envmap              = util_render_default_envmap_name;
	g_context->show_envmap           = false;
	scene_world->_->radiance         = g_context->default_radiance;
	scene_world->_->radiance_mipmaps = g_context->default_radiance_mipmaps;
	scene_world->_->irradiance       = g_context->default_irradiance;
	scene_world->strength            = 1.0;
	g_context->envmap_angle          = 0.0;
	g_context->show_envmap_blur      = false;
	g_project->envmap                = NULL;
}

void project_import_material_on_file_picked(char *path) {
	if (path_has_ext(path, "json")) {
		import_json_material_run(path);
	}
	else {
		import_arm_run_material(path);
	}
}

void project_import_material() {
	ui_files_show("arm,json", false, true, &project_import_material_on_file_picked);
}

ui_node_link_t *project_create_node_link(ui_node_link_t_array_t *links, i32 from_id, i32 from_socket, i32 to_id, i32 to_socket) {
	ui_node_link_t *link =
	    ALLOC_INIT(ui_node_link_t, {.id = ui_next_link_id(links), .from_id = from_id, .from_socket = from_socket, .to_id = to_id, .to_socket = to_socket});
	return link;
}

void project_import_brush_make_brush_preview(void *_) {
	util_render_make_brush_preview();
}

void project_import_brush_on_file_picked(char *path) {
	// Create brush from texture
	if (path_is_texture(path)) {
		// Import texture
		import_asset_run(path, -1.0, -1.0, true, true, NULL);
		i32 asset_index = 0;
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			if (string_equals(g_project->_->assets->buffer[i]->file, path)) {
				asset_index = i;
				break;
			}
		}

		// Create a new brush
		g_context->brush = slot_brush_create(NULL);
		any_array_push(g_project->_->brushes, g_context->brush);

		// Create and link image node
		ui_node_t *n                         = nodes_brush_create_node("TEX_IMAGE");
		n->x                                 = 83;
		n->y                                 = 340;
		n->buttons->buffer[0]->default_value = f32_array_create_x(asset_index);
		ui_node_link_t_array_t *links        = g_context->brush->canvas->links;
		ui_node_link_t         *link         = project_create_node_link(links, n->id, 0, 0, 4);
		any_array_push(links, link);

		// Parse brush
		make_material_parse_brush();
		ui_nodes_hwnd->redraws = 2;
		sys_notify_on_next_frame(&project_import_brush_make_brush_preview, NULL);
	}
	// Import from project file
	else {
		import_arm_run_brush(path);
	}
}

void project_import_brush() {
	char *formats = string_array_join(path_texture_formats(), ",");
	ui_files_show(string("arm,%s", formats), false, true, &project_import_brush_on_file_picked);
}

void project_import_mesh_on_file_picked(char *path) {
	project_import_mesh_box(path, _project_import_mesh_replace_existing, true, false, _project_import_mesh_done);
}

void project_import_mesh(bool replace_existing, void (*done)(void)) {
	_project_import_mesh_replace_existing = replace_existing;
	_project_import_mesh_done             = done;
	char *formats                         = string_array_join(path_mesh_formats(), ",");
	ui_files_show(formats, false, false, &project_import_mesh_on_file_picked);
}

void project_reimport_mesh() {
	if (g_project->mesh_assets != NULL && g_project->mesh_assets->length > 0 && iron_file_exists(g_project->mesh_assets->buffer[0])) {
		project_import_mesh_box(g_project->mesh_assets->buffer[0], true, false, true, NULL);
	}
	else {
		project_import_asset(NULL, true);
	}
}

void project_unwrap_mesh_box_draw() {
	ui_end_element();
	ui_row2();
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
	if (ui_icon_button(tr("Unwrap"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		ui_box_hide();

#ifdef IRON_IOS
		console_toast(tr("Unwrapping mesh"));
#endif

		util_mesh_uv_unwrap(NULL);
	}
}

void project_unwrap_mesh_box() {
	ui_box_show_custom(&project_unwrap_mesh_box_draw, 400, 200, NULL, true, tr("Unwrap Mesh"));
}

void project_import_asset_on_file_picked(char *path) {
	import_asset_run(path, -1.0, -1.0, true, _project_import_asset_hdr_as_envmap, NULL);
}

void project_import_asset(char *filters, bool hdr_as_envmap) {
	if (filters == NULL) {
		filters = string("%s,%s,ptex", string_array_join(path_texture_formats(), ","), string_array_join(path_mesh_formats(), ",")); // ptex: Material Maker
	}

	_project_import_asset_hdr_as_envmap = hdr_as_envmap;

	ui_files_show(filters, false, true, &project_import_asset_on_file_picked);
}

void project_import_swatches_on_file_picked(char *path) {
	import_arm_run_swatches(path, _project_import_swatches_replace_existing);
}

void project_import_swatches(bool replace_existing) {
	_project_import_swatches_replace_existing = replace_existing;
	ui_files_show("arm", false, false, &project_import_swatches_on_file_picked);
}

void project_reimport_textures() {
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *asset = g_project->_->assets->buffer[i];
		project_reimport_texture(asset);
	}
}

void project_reimport_texture_load_on_next_frame(void *_) {
	render_path_base_redraw(); // The 3D view shows the change
	make_material_parse_paint_material(true);
	util_render_make_material_preview();
}

void project_reimport_texture_load(char *path, asset_t *asset) {
	i32 i = array_index_of(g_project->_->assets, asset);
	if (i < 0) { // Removed while the file was picked
		return;
	}
	asset->file = string_copy(path);
	data_delete_texture(asset->file);
	asset_t *old_asset = g_project->_->assets->buffer[i];
	array_splice(g_project->_->assets, i, 1);
	import_texture_run(asset->file, true);
	array_insert(g_project->_->assets, i, array_pop(g_project->_->assets));

	if (g_context->texture == old_asset) {
		g_context->texture = g_project->_->assets->buffer[i];
	}

	sys_notify_on_next_frame(&project_reimport_texture_load_on_next_frame, NULL);
}

void project_reimport_texture_on_file_picked(char *path) {
	project_reimport_texture_load(path, _project_reimport_texture_asset);
}

void project_reimport_texture(asset_t *asset) {
	if (!iron_file_exists(asset->file)) {
		char *filters                   = string_array_join(path_texture_formats(), ",");
		_project_reimport_texture_asset = asset;
		ui_files_show(filters, false, false, &project_reimport_texture_on_file_picked);
	}
	else {
		project_reimport_texture_load(asset->file, asset);
	}
}

gpu_texture_t *project_get_image(asset_t *asset) {
	return asset != NULL ? asset->image : NULL;
}

asset_t_array_t *project_get_assets() {
	return g_project->_->assets;
}

string_array_t *project_get_used_atlases() {
	if (g_project->atlas_objects == NULL) {
		return NULL;
	}

	static i32_array_t    used = {0};
	static string_array_t res  = {0};
	used.length                = 0;
	for (i32 i = 0; i < g_project->atlas_objects->length; ++i) {
		i32 ao = g_project->atlas_objects->buffer[i];
		if (i32_array_index_of(&used, ao) == -1) {
			i32_array_push(&used, ao);
		}
	}
	if (used.length > 1) {
		res.length = 0;
		for (i32 i = 0; i < used.length; ++i) {
			i32 a = used.buffer[i];
			string_array_push(&res, g_project->atlas_names != NULL && a >= 0 && a < g_project->atlas_names->length ? g_project->atlas_names->buffer[a] : "");
		}
		return &res;
	}
	return NULL;
}

bool project_is_atlas_object(mesh_object_t *p) {
	if (g_context->layer_filter <= g_project->_->paint_objects->length) {
		return false;
	}
	string_array_t *atlases = project_get_used_atlases();
	i32             i       = g_context->layer_filter - g_project->_->paint_objects->length - 1;
	i32             o       = array_index_of(g_project->_->paint_objects, p);
	if (atlases == NULL || i >= atlases->length || o < 0 || o >= g_project->atlas_objects->length) {
		return false;
	}
	i32 atlas_i = string_array_index_of(g_project->atlas_names, atlases->buffer[i]);
	return atlas_i == g_project->atlas_objects->buffer[o];
}

mesh_object_t_array_t *project_get_atlas_objects(i32 object_mask) {
	string_array_t *atlases = project_get_used_atlases();
	i32             i       = object_mask - g_project->_->paint_objects->length - 1;
	if (atlases == NULL || i >= atlases->length) {
		return g_project->_->paint_objects;
	}
	char                  *atlas_name = atlases->buffer[i];
	i32                    atlas_i    = string_array_index_of(g_project->atlas_names, atlas_name);
	mesh_object_t_array_t *visibles   = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length && i < g_project->atlas_objects->length; ++i) {
		if (g_project->atlas_objects->buffer[i] == atlas_i) {
			any_array_push(visibles, g_project->_->paint_objects->buffer[i]);
		}
	}
	return visibles;
}

bool project_packed_asset_exists(packed_asset_t_array_t *packed_assets, char *name) {
	for (i32 i = 0; i < packed_assets->length; ++i) {
		packed_asset_t *pa = packed_assets->buffer[i];
		if (string_equals(pa->name, name)) {
			return true;
		}
	}
	return false;
}

void project_export_swatches_on_file_picked(char *path) {
	char *f = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	export_arm_run_swatches(string("%s%s%s", path, PATH_SEP, f));
}

void project_export_swatches() {
	ui_files_show("arm", true, false, &project_export_swatches_on_file_picked);
}

swatch_color_t *project_make_swatch(i32 base) {
	swatch_color_t *s = ALLOC_INIT(swatch_color_t, {.base       = base,
	                                                .opacity    = 1.0,
	                                                .occlusion  = 1.0,
	                                                .roughness  = 0.8,
	                                                .metallic   = 0.0,
	                                                .normal     = 0xff8080ff,
	                                                .emission   = 0.0,
	                                                .height     = 0.0,
	                                                .subsurface = 0.0});
	return s;
}

swatch_color_t *project_clone_swatch(swatch_color_t *swatch) {
	swatch_color_t *s = ALLOC_INIT(swatch_color_t, {.base       = swatch->base,
	                                                .opacity    = swatch->opacity,
	                                                .occlusion  = swatch->occlusion,
	                                                .roughness  = swatch->roughness,
	                                                .metallic   = swatch->metallic,
	                                                .normal     = swatch->normal,
	                                                .emission   = swatch->emission,
	                                                .height     = swatch->height,
	                                                .subsurface = swatch->subsurface});
	return s;
}

void project_set_default_swatches() {
	// 32-Color Palette by Andrew Kensler
	// http://eastfarthing.com/blog/2016-05-06-palette/
	g_project->swatches = any_array_create_from_raw((void *[]){}, 0);
	i32_array_t *colors = i32_array_create_from_raw(
	    (i32[]){
	        0xffffffff, 0xff000000, 0xffd6a090, 0xffa12c32, 0xfffa2f7a, 0xfffb9fda, 0xffe61cf7, 0xff992f7c, 0xff47011f, 0xff051155, 0xff4f02ec,
	        0xff2d69cb, 0xff00a6ee, 0xff6febff, 0xff08a29a, 0xff2a666a, 0xff063619, 0xff4a4957, 0xff8e7ba4, 0xffb7c0ff, 0xffacbe9c, 0xff827c70,
	        0xff5a3b1c, 0xffae6507, 0xfff7aa30, 0xfff4ea5c, 0xff9b9500, 0xff566204, 0xff11963b, 0xff51e113, 0xff08fdcc,
	    },
	    31);
	for (i32 i = 0; i < colors->length; ++i) {
		i32 c = colors->buffer[i];
		any_array_push(g_project->swatches, project_make_swatch(c));
	}
}

node_group_t *project_get_material_group_by_name(char *group_name) {
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *g = g_project->_->material_groups->buffer[i];
		if (string_equals(g->canvas->name, group_name)) {
			return g;
		}
	}
	return NULL;
}

bool project_is_material_group_in_use(node_group_t *group) {
	ui_node_canvas_t_array_t *canvases = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		any_array_push(canvases, m->canvas);
	}
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *m = g_project->_->material_groups->buffer[i];
		any_array_push(canvases, m->canvas);
	}
	for (i32 i = 0; i < canvases->length; ++i) {
		ui_node_canvas_t *canvas = canvases->buffer[i];
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *n     = canvas->nodes->buffer[i];
			char      *cname = group->canvas->name;
			if (string_equals(n->type, "GROUP") && string_equals(n->name, cname)) {
				return true;
			}
		}
	}
	return false;
}

void project_delete_swatch(swatch_color_t *swatch) {
	i32 i = array_index_of(g_project->swatches, swatch);
	if (i < 0) {
		return;
	}
	history_delete_swatch(swatch);
	g_context->swatch = g_project->swatches->buffer[i == g_project->swatches->length - 1 ? i - 1 : i + 1];
	array_splice(g_project->swatches, i, 1);
}
