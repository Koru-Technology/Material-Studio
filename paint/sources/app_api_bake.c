// app_api_bake.h, #included by main.c after app_api.c. Runs the bake map by map over the frames (app_bake_update), the ray tracer
// works on threads of its own (plugins/bake_maps/bake_maps.c).

#include "app_api_bake.h"

#define APP_BAKE_TYPE_CAVITY 14 // The ray tracer's own, not a bake_type_t

typedef struct {
	char *name;
	char *suffix;
	i32   type;     // bake_type_t
	i32   mesh_map; // mesh_map_t, -1 for none
} app_bake_map_info_t;

static const app_bake_map_info_t app_bake_maps[APP_BAKE_MAP_COUNT] = {
    [APP_BAKE_MAP_NORMAL]       = {"Normal", "normal", BAKE_TYPE_NORMAL, MESH_MAP_NORMAL},
    [APP_BAKE_MAP_HEIGHT]       = {"Height", "height", BAKE_TYPE_HEIGHT, -1},
    [APP_BAKE_MAP_AO]           = {"Ambient Occlusion", "ao", BAKE_TYPE_OCCLUSION, MESH_MAP_AO},
    [APP_BAKE_MAP_CURVATURE]    = {"Curvature", "curvature", BAKE_TYPE_CURVATURE, MESH_MAP_CURVATURE},
    [APP_BAKE_MAP_CAVITY]       = {"Cavity", "cavity", APP_BAKE_TYPE_CAVITY, MESH_MAP_CAVITY},
    [APP_BAKE_MAP_THICKNESS]    = {"Thickness", "thickness", BAKE_TYPE_THICKNESS, MESH_MAP_THICKNESS},
    [APP_BAKE_MAP_POSITION]     = {"Position", "position", BAKE_TYPE_POSITION, MESH_MAP_POSITION},
    [APP_BAKE_MAP_WORLD_NORMAL] = {"World Space Normal", "world_normal", BAKE_TYPE_NORMAL_OBJECT, MESH_MAP_WORLD_NORMAL},
};

static app_bake_options_t app_bake_opts = {
    .maps               = 1u << APP_BAKE_MAP_NORMAL | 1u << APP_BAKE_MAP_AO | 1u << APP_BAKE_MAP_CURVATURE | 1u << APP_BAKE_MAP_THICKNESS |
                          1u << APP_BAKE_MAP_POSITION,
    .size               = 2048,
    .rays               = 64,
    .ao_strength        = 1.0,
    .ao_radius          = 1.0,
    .ao_offset          = 1.0,
    .curvature_strength = 1.0,
    .curvature_radius   = 1.0,
    .curvature_offset   = 0.0,
    .curvature_smooth   = 1,
    .cavity_size        = 2.0,
    .cavity_contrast    = 1.0,
    .max_distance       = 0.02,
    .average_normals    = true,
};

static char *app_bake_high_path  = ""; // Read into the ray tracer, "" for none
static i32   app_bake_high_count = 0;

static bool  app_bake_running_  = false;
static bool  app_bake_cancelled = false;
static bool  app_bake_release_pending = false; // The sheet closed while baking
static i32   app_bake_queue[APP_BAKE_MAP_COUNT];
static i32   app_bake_queue_len = 0;
static i32   app_bake_queue_i   = 0;
static i32   app_bake_done      = 0;
static char *app_bake_folder_   = "";
static char *app_bake_name_     = "";
static bool  app_bake_baked_types[MESH_MAP_COUNT]; // Mesh maps baked by this run

static f32 app_bake_clamp(f32 v, f32 lo, f32 hi) {
	return v < lo ? lo : v > hi ? hi : v;
}

void app_bake_get(app_bake_options_t *out) {
	*out                     = app_bake_opts;
	app_bake_release_pending = false; // The sheet is up again
}

void app_bake_set(const app_bake_options_t *in) {
	app_bake_options_t o = *in;
	o.maps &= (1u << APP_BAKE_MAP_COUNT) - 1;
	o.size                = o.size <= 1024 ? 1024 : o.size <= 2048 ? 2048 : o.size <= 4096 ? 4096 : 8192;
	o.rays                = o.rays < 4 ? 4 : o.rays > 512 ? 512 : o.rays;
	o.ao_strength         = app_bake_clamp(o.ao_strength, 0.0, 2.0);
	o.ao_radius           = app_bake_clamp(o.ao_radius, 0.0, 2.0);
	o.ao_offset           = app_bake_clamp(o.ao_offset, 0.0, 2.0);
	o.curvature_strength  = app_bake_clamp(o.curvature_strength, 0.0, 2.0);
	o.curvature_radius    = app_bake_clamp(o.curvature_radius, 0.0, 2.0);
	o.curvature_offset    = app_bake_clamp(o.curvature_offset, -2.0, 2.0);
	o.curvature_smooth    = o.curvature_smooth < 0 ? 0 : o.curvature_smooth > 5 ? 5 : o.curvature_smooth;
	o.cavity_size         = app_bake_clamp(o.cavity_size, 1.0, 16.0);
	o.cavity_contrast     = app_bake_clamp(o.cavity_contrast, 0.5, 6.0);
	o.max_distance        = app_bake_clamp(o.max_distance, 0.001, 0.2);
	app_bake_opts         = o;
	bake_maps_set_cage(o.max_distance, o.average_normals);
}

const char *app_bake_map_name(int32_t map) {
	return map >= 0 && map < APP_BAKE_MAP_COUNT ? app_bake_maps[map].name : "";
}

// Maps are named after the paint object
static char *app_bake_mesh_name() {
	if (g_context->paint_object != NULL && g_context->paint_object->base->name != NULL) {
		return g_context->paint_object->base->name;
	}
	return "bake";
}

// The project's Mesh Maps folder, NULL when the project is not saved yet (a folder is asked for)
static char *app_bake_folder() {
#ifdef IRON_IOS
	return project_subfolder("Mesh Maps");
#else
	char *path = project_filepath_get();
	i32   i    = string_last_index_of(path, PATH_SEP);
	if (i < 0) {
		return NULL;
	}
	char *dir = string("%s%sMesh Maps", substring(path, 0, i), PATH_SEP);
	iron_create_directory(dir);
	return dir;
#endif
}

static char *app_bake_map_path(char *folder, char *name, i32 map) {
	return string("%s%s%s_%s.png", folder, PATH_SEP, name, app_bake_maps[map].suffix);
}

// Releases the high poly and the bake data
static void app_bake_release() {
	bake_maps_end();
	app_bake_high_path       = "";
	app_bake_high_count      = 0;
	app_bake_release_pending = false;
}

static void app_bake_finish() {
	app_bake_running_ = false;
	bake_maps_end_images();
	console_info(string("%s: %d", tr("Baked maps"), app_bake_done));
	// Registered maps all baked again fit the mesh
	bool fresh = !app_bake_cancelled && app_bake_done > 0;
	for (i32 t = 0; t < MESH_MAP_COUNT; ++t) {
		if (mesh_maps_get_asset(t) != NULL && !app_bake_baked_types[t]) {
			fresh = false;
		}
	}
	if (fresh) {
		mesh_maps_stale = false;
	}
	if (app_bake_release_pending) {
		app_bake_release();
	}
}

static void app_bake_start_next() {
	while (app_bake_queue_i < app_bake_queue_len) {
		i32 type = app_bake_maps[app_bake_queue[app_bake_queue_i]].type;
		if (bake_maps_start(type, app_bake_opts.rays, app_bake_high_count > 0, app_bake_opts.size, app_bake_opts.size)) {
			return;
		}
		console_error(tr("Unable to bake, check that the mesh has a UV map"));
		app_bake_queue_i = app_bake_queue_len;
	}
	app_bake_finish();
}

static void app_bake_run() {
	if (bake_maps_set_low() == 0) {
		console_error(tr("Unable to bake, check that the mesh has a UV map"));
		return;
	}
	bake_maps_set_cage(app_bake_opts.max_distance, app_bake_opts.average_normals);
	bake_maps_set_ao(app_bake_opts.ao_strength, app_bake_opts.ao_radius, app_bake_opts.ao_offset);
	bake_maps_set_curvature(app_bake_opts.curvature_strength, app_bake_opts.curvature_radius, app_bake_opts.curvature_offset,
	                        app_bake_opts.curvature_smooth);
	bake_maps_set_cavity(app_bake_opts.cavity_size, app_bake_opts.cavity_contrast);
	app_bake_name_     = string_copy(app_bake_mesh_name());
	app_bake_running_  = true;
	app_bake_cancelled = false;
	app_bake_queue_i   = 0;
	app_bake_done      = 0;
	memset(app_bake_baked_types, 0, sizeof(app_bake_baked_types));
	app_bake_start_next();
}

static void app_bake_folder_picked(char *path) {
	app_bake_folder_ = string_copy(path);
	app_bake_run();
}

bool app_bake_start(void) {
	if (!app_ready() || app_bake_running_) {
		return false;
	}
	app_bake_queue_len = 0;
	for (i32 i = 0; i < APP_BAKE_MAP_COUNT; ++i) {
		if (app_bake_opts.maps & (1u << i)) {
			app_bake_queue[app_bake_queue_len++] = i;
		}
	}
	if (app_bake_queue_len == 0) {
		return false;
	}
	char *folder = app_bake_folder();
	if (folder == NULL) {
		ui_files_show("png", true, false, &app_bake_folder_picked);
		return true;
	}
	app_bake_folder_ = string_copy(folder);
	app_bake_run();
	return true;
}

void app_bake_cancel(void) {
	if (app_bake_running_) {
		bake_maps_cancel();
		app_bake_cancelled = true;
		app_bake_finish();
	}
}

void app_bake_close(void) {
	if (app_bake_running_) {
		app_bake_release_pending = true;
	}
	else {
		app_bake_release();
	}
}

// Every update: steps the bake
void app_bake_update() {
	mesh_maps_update();
	if (!app_bake_running_) {
		return;
	}
	iron_delay_idle_sleep();
	if (!bake_maps_step()) {
		return;
	}
	i32   map  = app_bake_queue[app_bake_queue_i];
	char *path = app_bake_map_path(app_bake_folder_, app_bake_name_, map);
	if (bake_maps_save(app_bake_folder_, string("%s_%s.png", app_bake_name_, app_bake_maps[map].suffix))) {
		app_bake_done++;
		// The baked map becomes the project's mesh map of its kind
		i32 type = app_bake_maps[map].mesh_map;
		i32 a    = script_asset_index(path);
		if (type >= 0 && a >= 0) {
			mesh_maps_set(type, g_project->_->assets->buffer[a]);
			app_bake_baked_types[type] = true;
		}
	}
	app_bake_queue_i++;
	app_bake_start_next();
}

bool app_bake_running() {
	return app_bake_running_;
}

// High poly

int32_t app_bake_set_high(const char *path) {
	if (!app_ready() || app_bake_running_) {
		return 0;
	}
	app_bake_high_path  = "";
	app_bake_high_count = bake_maps_load_high(path != NULL ? (char *)path : "");
	if (app_bake_high_count > 0) {
		app_bake_high_path = string_copy((char *)path);
	}
	return app_bake_high_count;
}

const char *app_bake_high_name(void) {
	if (!app_ready() || app_bake_high_path[0] == '\0') {
		return "";
	}
	i32 i = string_last_index_of(app_bake_high_path, PATH_SEP);
	return app_str(substring(app_bake_high_path, i + 1, string_length(app_bake_high_path)));
}

int32_t app_bake_preview(float yaw, float pitch, float zoom, bool high, bool fast, int32_t width, int32_t height, uint8_t *out, int32_t capacity,
                         int32_t *w, int32_t *h) {
	*w = 0;
	*h = 0;
	if (!app_ready() || app_bake_running_ || width <= 0 || height <= 0 || capacity < width * height * 4 ||
	    g_project->_->paint_objects->length == 0) {
		return 0;
	}
	bool show_high = high && app_bake_high_count > 0;
	u8  *pixels    = bake_maps_preview(width, height, yaw, pitch, zoom, !show_high, show_high, app_bake_high_count > 0, fast);
	if (pixels == NULL) {
		return 0;
	}
	memcpy(out, pixels, width * height * 4);
	*w = width;
	*h = height;
	return width * height * 4;
}

// Baked maps, layer

static char *app_bake_layer_maps[3]  = {"ao", "normal", "height"};
static i32   app_bake_layer_sockets[3] = {2, 5, 7}; // Occlusion, Normal Map, Height of OUTPUT_MATERIAL_PBR

static i32 app_bake_layer_asset(i32 k) {
	char *folder = app_bake_folder();
	if (folder == NULL) {
		folder = app_bake_folder_;
	}
	return script_asset_index(string("%s%s%s_%s.png", folder, PATH_SEP, app_bake_mesh_name(), app_bake_layer_maps[k]));
}

void app_bake_state(app_bake_state_t *out) {
	*out = (app_bake_state_t){.high_fit = 1.0};
	if (!app_ready()) {
		return;
	}
	out->high_triangles = app_bake_high_count;
	if (app_bake_high_count > 0 && !app_bake_running_) {
		bake_maps_set_cage(app_bake_opts.max_distance, app_bake_opts.average_normals);
		out->high_fit = bake_maps_high_fit();
		out->missed   = bake_maps_miss_ratio();
	}
	out->running = app_bake_running_;
	if (app_bake_running_ && app_bake_queue_len > 0) {
		i32 i         = app_bake_queue_i < app_bake_queue_len ? app_bake_queue_i : app_bake_queue_len - 1;
		out->map      = app_bake_queue[i];
		out->progress = (app_bake_queue_i + bake_maps_progress()) / app_bake_queue_len;
	}
	for (i32 k = 0; k < 3 && !app_bake_running_; ++k) {
		out->has_baked = out->has_baked || app_bake_layer_asset(k) >= 0;
	}
	out->stale = mesh_maps_stale;
}

// "<mesh> Baked": a material with the baked maps linked to the output, on a new fill layer
bool app_bake_add_layer(void) {
	if (!app_ready() || app_bake_running_) {
		return false;
	}
	i32  assets[3];
	bool any = false;
	for (i32 k = 0; k < 3; ++k) {
		assets[k] = app_bake_layer_asset(k);
		any       = any || assets[k] >= 0;
	}
	if (!any) {
		return false;
	}
	char            *name = string("%s Baked", app_bake_mesh_name());
	slot_material_t *old  = script_get_material(name);
	if (old != NULL) {
		script_material_delete(old);
	}
	if (script_material_create(name) == NULL) {
		return false;
	}
	// Image nodes go left of the default nodes
	ui_node_t *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
	ui_node_t *rgb = script_material_get_node("RGB");
	f32        x   = (rgb != NULL && rgb->x < out->x ? rgb->x : out->x) - 260;
	for (i32 k = 0; k < 3; ++k) {
		if (assets[k] < 0) {
			continue;
		}
		ui_node_t *n = script_material_create_node_at("TEX_IMAGE", x, 100 + 180 * k);
		script_material_set_button(n, 0, assets[k]);
		script_material_connect(n, 0, out, app_bake_layer_sockets[k]);
	}
	script_material_update();
	layers_create_fill_layer(FILL_PROJECTION_UV, mat4_nan(), -1, true);
	return true;
}

// Mesh maps

int32_t app_mesh_map_count(void) {
	return MESH_MAP_COUNT;
}

const char *app_mesh_map_name(int32_t type) {
	return app_str(type >= 0 && type < MESH_MAP_COUNT ? mesh_map_name(type) : "");
}

int32_t app_mesh_map_texture(int32_t type) {
	if (!app_ready() || type < 0 || type >= MESH_MAP_COUNT) {
		return -1;
	}
	asset_t *a = mesh_maps_get_asset(type);
	return a != NULL ? array_index_of(g_project->_->assets, a) : -1;
}

bool app_mesh_map_set(int32_t type, int32_t texture) {
	if (!app_ready() || type < 0 || type >= MESH_MAP_COUNT || texture >= (i32)g_project->_->assets->length) {
		return false;
	}
	mesh_maps_set(type, texture >= 0 ? g_project->_->assets->buffer[texture] : NULL);
	return true;
}

int32_t app_texture_count(void) {
	return app_ready() ? g_project->_->assets->length : 0;
}

const char *app_texture_name(int32_t i) {
	return app_ready() && i >= 0 && i < (i32)g_project->_->assets->length ? app_str(g_project->_->assets->buffer[i]->name) : "";
}
