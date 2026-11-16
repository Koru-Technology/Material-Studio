
#include "global.h"
#include <float.h>

typedef struct version {
	char *sha;
	char *date;
} version_t;

bool config_loaded = false;

#ifdef IRON_MACOS
float iron_display_backing_scale(void);
#endif

void config_load() {
	char *path = "";
	if (path_is_protected()) {
		path = iron_internal_save_path();
	}
	path           = string("%sconfig.json", path);
	buffer_t *blob = data_get_blob(path);


	if (blob != NULL) {
		char *config_string = sys_buffer_to_string(blob);
		g_config            = config_from_json(config_string);
		config_loaded       = g_config != NULL;
		free(config_string);
	}
	free(path);
}

static void config_save_now();

void config_save() {
	script_brush_swap(); // A running script's test brush is not saved
	config_save_now();
	script_brush_swap();
}

// Saves the brush settings once a change is let go of (a slider released, a shortcut). iOS saves the config when the
// app goes to the background, but an app ended without that (an install, a debugger, a crash) would start again with
// the brush of an older session.
void config_brush_save_update() {
	if (g_context == NULL || script_brush_taken()) {
		return;
	}
	if (g_context->brush_radius == g_config->brush_radius && g_context->brush_opacity == g_config->brush_opacity &&
	    g_context->brush_strength == g_config->brush_strength && g_context->brush_selected == g_config->brush_selected) {
		return;
	}
	if (g_context->brush_time > 0 || mouse_down("left") || pen_down("tip")) {
		return; // Still dragging
	}
	config_save();
}

// Every key of config.json in the order of config_t: X(kind, field, min, max). A loaded number outside min..max gets
// the default value, CONFIG_ANY takes any.
#define CONFIG_FIELDS                                                                            \
	X(STRING, version, CONFIG_ANY)                                                               \
	X(STRING, sha, CONFIG_ANY)                                                                   \
	X(STRING, locale, CONFIG_ANY)                                                                \
	X(I32, window_mode, 0, 1)                                                                    \
	X(I32, window_w, CONFIG_ANY)                                                                 \
	X(I32, window_h, CONFIG_ANY)                                                                 \
	X(I32, window_x, CONFIG_ANY)                                                                 \
	X(I32, window_y, CONFIG_ANY)                                                                 \
	X(BOOL, window_resizable, CONFIG_ANY)                                                        \
	X(BOOL, window_maximizable, CONFIG_ANY)                                                      \
	X(BOOL, window_minimizable, CONFIG_ANY)                                                      \
	X(BOOL, window_vsync, CONFIG_ANY)                                                            \
	X(I32, window_frequency, CONFIG_ANY)                                                         \
	X(F32, window_scale, 0.25, 10)                                                               \
	X(F32, rp_supersample, 0.25, 4)                                                              \
	X(F32, rp_ssao, CONFIG_ANY)                                                                  \
	X(F32, rp_bloom, CONFIG_ANY)                                                                 \
	X(F32, rp_vignette, CONFIG_ANY)                                                              \
	X(F32, rp_grain, CONFIG_ANY)                                                                 \
	X(F32, rp_contrast, CONFIG_ANY)                                                              \
	X(F32, rp_gamma, CONFIG_ANY)                                                                 \
	X(STRING, lut_path, CONFIG_ANY)                                                              \
	X(BOOL, texture_filter, CONFIG_ANY)                                                          \
	X(STRINGS, recent_projects, CONFIG_ANY)                                                      \
	X(STRINGS, bookmarks, CONFIG_ANY)                                                            \
	X(STRING, keymap, CONFIG_ANY)                                                                \
	X(STRING, theme, CONFIG_ANY)                                                                 \
	X(I32, undo_steps, 1, 1000)                                                                  \
	X(F32, camera_fov, 0.01, 3.1)                                                                \
	X(F32, camera_pan_speed, CONFIG_ANY)                                                         \
	X(F32, camera_zoom_speed, CONFIG_ANY)                                                        \
	X(F32, camera_rotation_speed, CONFIG_ANY)                                                    \
	X(BOOL, camera_upside_down, CONFIG_ANY)                                                      \
	X(I32, zoom_direction, ZOOM_DIRECTION_VERTICAL, ZOOM_DIRECTION_VERTICAL_HORIZONTAL_INVERTED) \
	X(BOOL, wrap_mouse, CONFIG_ANY)                                                              \
	X(BOOL, show_asset_names, CONFIG_ANY)                                                        \
	X(BOOL, material_params, CONFIG_ANY)                                                         \
	X(BOOL, touch_ui, CONFIG_ANY)                                                                \
	X(BOOL, splash_screen, CONFIG_ANY)                                                           \
	X(BOOL, header_pinned, CONFIG_ANY)                                                           \
	X(I32S, layout_tabs, CONFIG_ANY)                                                             \
	X(I32, camera_pivot, CAMERA_PIVOT_CURSOR, CAMERA_PIVOT_CENTER)                               \
	X(I32, camera_controls, CAMERA_CONTROLS_ORBIT, CAMERA_CONTROLS_FLY)                          \
	X(I32, viewport_mode, VIEWPORT_MODE_LIT, VIEWPORT_MODE_PLUGIN)                               \
	X(I32, pathtrace_mode, PATHTRACE_MODE_FAST, PATHTRACE_MODE_FULL)                             \
	X(I32, pathtrace_frames, 1, 100000)                                                          \
	X(F32, pressure_sensitivity, CONFIG_ANY)                                                     \
	X(I32, layer_res, TEXTURE_RES_RES2048, TEXTURE_RES_RES16384)                                 \
	X(BOOL, brush_live, CONFIG_ANY)                                                              \
	X(BOOL, node_previews, CONFIG_ANY)                                                           \
	X(BOOL, material_live, CONFIG_ANY)                                                           \
	X(BOOL, brush_depth_reject, CONFIG_ANY)                                                      \
	X(BOOL, brush_angle_reject, CONFIG_ANY)                                                      \
	X(F32, brush_alpha_discard, CONFIG_ANY)                                                      \
	X(I32, dilate_radius, 0, 64)                                                                 \
	X(STRING, blender, CONFIG_ANY)                                                               \
	X(BOOL, grid_snap, CONFIG_ANY)                                                               \
	X(BOOL, experimental, CONFIG_ANY)                                                            \
	X(I32, neural_res, 1, 16384)                                                                 \
	X(BOOL, view2d_grid_show, CONFIG_ANY)                                                        \
	X(I32, view2d_grid_cell, 1, 16384)                                                           \
	X(BOOL, view2d_grid_snap, CONFIG_ANY)                                                        \
	X(BOOL, pixel_snap, CONFIG_ANY)                                                              \
	X(BOOL, large_asset_previews, CONFIG_ANY)                                                    \
	X(BOOL, tooltips, CONFIG_ANY)                                                                \
	X(STRING, materials_folder, CONFIG_ANY)                                                      \
	X(BOOL, reload_project, CONFIG_ANY)                                                          \
	X(I32, cloud_resolution, CONFIG_ANY)                                                         \
	X(BOOL, library_cloud, CONFIG_ANY)                                                           \
	X(I32, library_cloud_type, CONFIG_ANY)                                                       \
	X(F32, library_scroll, CONFIG_ANY)                                                           \
	X(F32, brush_radius, 0, 1)                                                                   \
	X(F32, brush_opacity, 0, 1)                                                                  \
	X(F32, brush_strength, 0, 1)                                                                 \
	X(F32, pressure_curve, 0.01, 100)                                                            \
	X(F32, pressure_min, 0, 1)                                                                   \
	X(I32, brush_selected, CONFIG_ANY)                                                           \
	X(BOOL, hide_builtin_brushes, CONFIG_ANY)                                                    \
	X(I32, rp_view_transform, 0, 3)                                                              \
	X(BOOL, autosave, CONFIG_ANY)                                                                \
	X(I32, autosave_interval, 1, 1440)                                                           \
	X(I32, finger_paint, 0, 2)                                                                   \
	X(I32, pencil_double_tap, PENCIL_ACTION_SYSTEM, PENCIL_ACTION_OFF)                           \
	X(I32, pencil_squeeze, PENCIL_ACTION_SYSTEM, PENCIL_ACTION_OFF)                              \
	X(I32S, quick_menu_slots, CONFIG_ANY)                                                        \
	X(I32, undo_memory_mb, 1, 1048576)

#define CONFIG_ANY -FLT_MAX, FLT_MAX

typedef enum {
	CONFIG_STRING,
	CONFIG_STRINGS, // string_array_t
	CONFIG_I32S,    // i32_array_t, null when not set
	CONFIG_I32,
	CONFIG_F32,
	CONFIG_BOOL,
} config_kind_t;

typedef struct {
	char         *key;
	config_kind_t kind;
	i32           offset;
	f32           min;
	f32           max;
} config_field_t;

#define X(kind, name, ...) {#name, CONFIG_##kind, (i32)offsetof(config_t, name), __VA_ARGS__},
static const config_field_t config_fields[] = {CONFIG_FIELDS};
#undef X
#define CONFIG_FIELD_COUNT (i32)ARRAY_LENGTH(config_fields)

// The config as JSON
static char *config_to_json(config_t *c) {
	json_encode_begin();
	for (i32 i = 0; i < CONFIG_FIELD_COUNT; ++i) {
		const config_field_t *f = &config_fields[i];
		void                 *p = (u8 *)c + f->offset;
		switch (f->kind) {
		case CONFIG_STRING:
			json_encode_string(f->key, *(char **)p);
			break;
		case CONFIG_STRINGS:
			json_encode_string_array(f->key, *(string_array_t **)p);
			break;
		case CONFIG_I32S:
			if (*(i32_array_t **)p != NULL) {
				json_encode_i32_array(f->key, *(i32_array_t **)p);
			}
			else {
				json_encode_null(f->key);
			}
			break;
		case CONFIG_I32:
			json_encode_i32(f->key, *(i32 *)p);
			break;
		case CONFIG_F32:
			json_encode_f32(f->key, *(f32 *)p);
			break;
		case CONFIG_BOOL:
			json_encode_bool(f->key, *(bool *)p);
			break;
		}
	}
	return json_encode_end();
}

static void config_save_now() {
	if (g_context != NULL) { // Brush settings kept across launches
		g_config->brush_radius   = g_context->brush_radius;
		g_config->brush_opacity  = g_context->brush_opacity;
		g_config->brush_strength = g_context->brush_strength;
		g_config->brush_selected = g_context->brush_selected;
	}

	// Use system application data folder
	// when running from protected path like "Program Files"
	char *path;
	if (path_is_protected()) {
		path = string("%sconfig.json", iron_internal_save_path());
	}
	else {
		path = string("%s%sconfig.json", path_data(), PATH_SEP);
	}

	char *config_json = config_to_json(g_config);

	buffer_t *buffer = sys_string_to_buffer(config_json);
	iron_file_save_bytes(path, buffer, 0);
	array_free(buffer);
	free(buffer);
	free(config_json);
}

i32_array_t *config_default_quick_menu_slots() {
	i32 slots[] = {QUICK_MENU_BRUSH, QUICK_MENU_ERASER, QUICK_MENU_PICKER, QUICK_MENU_FILL,
	               QUICK_MENU_UNDO,  QUICK_MENU_REDO,   QUICK_MENU_SIZE,   QUICK_MENU_OPACITY};
	return i32_array_create_from_raw(slots, QUICK_MENU_SLOTS);
}

config_t *config_create_default() {
	config_t *c           = ALLOC_INIT(config_t, {0});
	c->version            = string_copy(manifest_version_config);
	c->sha                = string_copy(config_get_sha());
	c->locale             = "en"; // "system";
	c->window_mode        = 0;
	c->window_resizable   = true;
	c->window_minimizable = true;
	c->window_maximizable = true;
	c->window_w           = 1720;
	c->window_h           = 960;
#ifdef IRON_MACOS
	c->window_w *= iron_display_backing_scale();
	c->window_h *= iron_display_backing_scale();
#endif
	c->window_x     = -1;
	c->window_y     = -1;
	c->window_scale = 1.0;
	if (sys_display_width() >= 2560 && sys_display_height() >= 1600) {
		c->window_scale = 2.0;
	}
#if defined(IRON_IOS)
	c->window_scale = 2.0;
#endif
#ifdef IRON_MACOS
	c->window_scale = iron_display_backing_scale();
#endif
#if defined(IRON_IOS)
	if (sys_display_ppi() > 330) {
		c->window_scale = 2.5;
	}
	if (sys_display_ppi() > 400) {
		c->window_scale = 3.0;
	}
#endif
	c->window_vsync     = true;
	c->window_frequency = sys_display_frequency();
	c->rp_bloom         = 0.0;
	c->rp_vignette      = 1.0;
	c->rp_grain         = 0.09;
	c->rp_contrast      = 1.0;
	c->rp_gamma         = 1.0;
	c->lut_path         = "";
	c->texture_filter   = true;
	c->rp_ssao          = 1.0;
	c->rp_supersample   = 1.0;
	c->recent_projects = any_array_create_from_raw((void *[]){}, 0);
	c->bookmarks       = any_array_create_from_raw((void *[]){}, 0);
#if defined(IRON_IOS)
	c->keymap = "touch.json";
#else
	c->keymap = "default.json";
#endif
	c->theme  = "default.json";
#if defined(IRON_IOS)
	c->undo_steps = 4;
#else
	c->undo_steps = 6;
#endif
	c->pressure_sensitivity = 1.0;
	c->camera_fov           = 0.69;
#if defined(IRON_IOS)
	c->camera_zoom_speed     = 0.5;
	c->camera_pan_speed      = 0.5;
	c->camera_rotation_speed = 0.5;
#else
	c->camera_zoom_speed     = 1.0;
	c->camera_pan_speed      = 1.0;
	c->camera_rotation_speed = 1.0;
#endif
	c->camera_upside_down = false;
	c->zoom_direction     = ZOOM_DIRECTION_VERTICAL;
	c->wrap_mouse         = false;
	c->camera_pivot       = CAMERA_PIVOT_CENTER;
	c->camera_controls    = CAMERA_CONTROLS_ORBIT;
	c->layer_res          = TEXTURE_RES_RES2048;
#if defined(IRON_IOS)
	c->touch_ui      = true;
	c->header_pinned = false;
	c->splash_screen = true;
#else
	c->touch_ui      = false;
	c->header_pinned = true;
	c->splash_screen = false;
#endif
	c->node_previews     = false;
#if defined(IRON_IOS) || defined(IRON_MACOS)
	c->material_live = false;
#else
	c->material_live = true;
#endif
	c->brush_depth_reject  = true;
	c->brush_angle_reject  = true;
	c->brush_alpha_discard = 0.1;
	c->brush_live          = false;
	c->show_asset_names    = false;
#if defined(IRON_IOS)
	c->material_params = false;
#else
	c->material_params = true;
#endif
	c->dilate_radius    = 2;
	c->blender          = "";
	c->pathtrace_mode   = PATHTRACE_MODE_FAST;
	c->pathtrace_frames = 8;
	c->grid_snap        = false;
	c->view2d_grid_show = false;
	c->view2d_grid_cell = 64;
	c->view2d_grid_snap = false;
	c->pixel_snap       = false;
#if defined(IRON_IOS)
	c->large_asset_previews = false;
	c->tooltips             = false;
#else
	c->large_asset_previews = true;
	c->tooltips             = true;
#endif
	c->experimental     = false;
	c->neural_res       = 512;
	c->materials_folder = "";
	c->reload_project   = true;
	c->cloud_resolution   = 1;
	c->library_cloud      = false;
	c->library_cloud_type = -1;
	c->library_scroll     = 0.0;
	c->brush_radius = 0.5;
	c->brush_opacity = 1.0;
	c->brush_strength = 0.5;
	c->pressure_curve = 1.0;
	c->pressure_min = 0.0;
	c->brush_selected = 1; // Round Hard
	c->hide_builtin_brushes = false;
	c->rp_view_transform = 0;
	c->autosave = true;
	c->autosave_interval = 5;
	c->finger_paint = 1;
	c->pencil_double_tap = PENCIL_ACTION_SYSTEM;
	c->pencil_squeeze = PENCIL_ACTION_SYSTEM;
	c->quick_menu_slots = config_default_quick_menu_slots();
#if defined(IRON_IOS)
	c->undo_memory_mb = 256;
#else
	c->undo_memory_mb = 1024;
#endif
	return c;
}

void config_init() {
	if (!config_loaded || g_config == NULL) {
		g_config = config_create_default();
	}
	else {
		g_config->sha = string_copy(config_get_sha());
	}

	ui_touch_control = g_config->touch_ui;
	ui_tooltips      = g_config->tooltips;
	ui_touch_speed   = 1.0;
#if defined(IRON_IOS)
	if (sys_display_ppi() > 400) {
		ui_touch_speed = 0.5;
	}
#endif
	base_res = g_config->layer_res;
	config_set_texture_res(g_config->layer_res);
	keymap_load();
}

// A config from the keys of a loaded one: missing keys and numbers out of their range get the default value, unknown
// keys are ignored. The keys are read by name, not by json_parse's key order: a config of the same version written
// by a build with other keys (another branch) would shift every later field.
config_t *config_from_map_to_config(any_map_t *old) {
	config_t    *c    = config_create_default();
	any_array_t *have = map_keys(old);
	for (i32 i = 0; i < CONFIG_FIELD_COUNT; ++i) {
		const config_field_t *f = &config_fields[i];
		if (string_equals(f->key, "version") || string_equals(f->key, "sha")) {
			continue; // This build's
		}
		bool found = false;
		for (i32 j = 0; j < have->length && !found; ++j) {
			found = string_equals(have->buffer[j], f->key);
		}
		if (!found) {
			continue;
		}
		void *p = (u8 *)c + f->offset;
		switch (f->kind) {
		case CONFIG_STRING: {
			char *v = any_map_get(old, f->key);
			if (v != NULL) {
				*(char **)p = string_copy(v);
			}
			break;
		}
		case CONFIG_STRINGS: {
			string_array_t *v = any_map_get(old, f->key);
			if (v != NULL) {
				*(string_array_t **)p = v;
			}
			break;
		}
		case CONFIG_I32S:
			*(i32_array_t **)p = any_map_get(old, f->key);
			break;
		case CONFIG_I32: {
			i32 v = armpack_map_get_i32(old, f->key);
			if (v >= f->min && v <= f->max) {
				*(i32 *)p = v;
			}
			break;
		}
		case CONFIG_F32: {
			f32 v = armpack_map_get_f32(old, f->key);
			if (v >= f->min && v <= f->max) { // false for NaN too
				*(f32 *)p = v;
			}
			break;
		}
		case CONFIG_BOOL:
			*(bool *)p = armpack_map_get_i32(old, f->key) > 0;
			break;
		}
	}
	return c;
}

// The built-in materials were replaced by Material Maker's library, hide_builtin_materials is dropped
config_t *config_from_version_18(any_map_t *old) {
	return config_from_map_to_config(old);
}

// Stroke settings and pen dynamics moved into the brushes (brush library), their keys are dropped
config_t *config_from_version_17(any_map_t *old) {
	return config_from_map_to_config(old);
}

// Object mode, scripts and the console agent were removed, their keys are dropped
config_t *config_from_version_16(any_map_t *old) {
	return config_from_map_to_config(old);
}

config_t *config_from_version_15(any_map_t *old) {
	config_t *def = config_create_default();
	armpack_map_set_i32(old, "tilt_shading", 0);
	armpack_map_set_i32(old, "pencil_double_tap", def->pencil_double_tap);
	armpack_map_set_i32(old, "pencil_squeeze", def->pencil_squeeze);
	any_map_set(old, "quick_menu_slots", def->quick_menu_slots);
	armpack_map_set_i32(old, "undo_memory_mb", def->undo_memory_mb);
	return config_from_version_16(old);
}

config_t *config_from_version_14(any_map_t *old) {
	armpack_map_set_i32(old, "finger_paint", 1);
	return config_from_version_15(old);
}

config_t *config_from_version_13(any_map_t *old) {
	armpack_map_set_i32(old, "autosave", 1);
	armpack_map_set_i32(old, "autosave_interval", 5);
	return config_from_version_14(old);
}

config_t *config_from_version_12(any_map_t *old) {
	armpack_map_set_i32(old, "rp_view_transform", 0);
	return config_from_version_13(old);
}

config_t *config_from_version_11(any_map_t *old) {
	// Pen pressure is now 0-1 on iOS too, the sensitivity no longer makes up for a larger range
	armpack_map_set_f32(old, "pressure_sensitivity", 1.0);
	armpack_map_set_f32(old, "pressure_curve", 1.0);
	armpack_map_set_f32(old, "pressure_min", 0.0);
	// Pressure switches become brush dynamics
	armpack_map_set_i32(old, "dyn_size", armpack_map_get_i32(old, "pressure_radius") > 0 ? 1 : 0);
	armpack_map_set_i32(old, "dyn_opacity", armpack_map_get_i32(old, "pressure_opacity") > 0 ? 1 : 0);
	armpack_map_set_i32(old, "dyn_flow", 0);
	armpack_map_set_i32(old, "dyn_hardness", armpack_map_get_i32(old, "pressure_hardness") > 0 ? 1 : 0);
	armpack_map_set_i32(old, "dyn_angle", armpack_map_get_i32(old, "pressure_angle") > 0 ? 1 : 0);
	armpack_map_set_i32(old, "brush_builtin", -1);
	armpack_map_set_i32(old, "hide_builtin_brushes", 0);
	return config_from_version_12(old);
}

config_t *config_from_version_10(any_map_t *old) {
	armpack_map_set_f32(old, "brush_radius", 0.5);
	armpack_map_set_f32(old, "brush_opacity", 1.0);
	armpack_map_set_f32(old, "brush_hardness", 1.0);
	armpack_map_set_f32(old, "brush_flow", 1.0);
	armpack_map_set_f32(old, "brush_spacing", 0.0);
	armpack_map_set_f32(old, "brush_scatter", 0.0);
	armpack_map_set_f32(old, "brush_jitter_size", 0.0);
	armpack_map_set_f32(old, "brush_jitter_angle", 0.0);
	armpack_map_set_f32(old, "brush_jitter_opacity", 0.0);
	armpack_map_set_f32(old, "brush_smoothing", 0.0);
	armpack_map_set_f32(old, "brush_strength", 0.5);
	return config_from_version_11(old);
}

config_t *config_from_version_9(any_map_t *old) {
	armpack_map_set_i32(old, "hide_builtin_materials", false);
	return config_from_version_10(old);
}

config_t *config_from_version_8(any_map_t *old) {
	armpack_map_set_i32(old, "library_cloud", false);
	armpack_map_set_i32(old, "library_cloud_type", -1);
	armpack_map_set_f32(old, "library_scroll", 0.0);
	return config_from_version_9(old);
}

config_t *config_from_version_7(any_map_t *old) {
	armpack_map_set_i32(old, "cloud_resolution", 1);
	return config_from_version_8(old);
}

config_t *config_from_version_6(any_map_t *old) {
	return config_from_version_7(old);
}

config_t *config_from_version_5(any_map_t *old) {
	armpack_map_set_i32(old, "reload_project", true);
	return config_from_version_6(old);
}

config_t *config_from_version_4(any_map_t *old) {
	any_map_set(old, "materials_folder", "");
	return config_from_version_5(old);
}

config_t *config_from_version_3(any_map_t *old) {
	config_t *def = config_create_default();
	armpack_map_set_i32(old, "tooltips", def->tooltips);
	return config_from_version_4(old);
}

config_t *config_from_version_2(any_map_t *old) {
	config_t *def = config_create_default();
	armpack_map_set_i32(old, "large_asset_previews", def->large_asset_previews);
	return config_from_version_3(old);
}

config_t *config_from_version_1(any_map_t *old) {
	config_t *def = config_create_default();
	armpack_map_set_i32(old, "material_params", def->material_params);
	armpack_map_set_i32(old, "header_pinned", def->header_pinned);
	armpack_map_set_i32(old, "pixel_snap", def->pixel_snap);
	any_map_set(old, "layout_tabs", def->layout_tabs);
	armpack_map_set_f32(old, "rp_vignette", def->rp_vignette);
	armpack_map_set_i32(old, "viewport_mode", def->viewport_mode);
	armpack_map_set_i32(old, "pathtrace_mode", def->pathtrace_mode);
	return config_from_version_2(old);
}

static bool config_is_version(char *s, char *version) {
	char *prefix = string("{\"version\":\"%s\"", version);
	bool  r      = starts_with(s, prefix);
	free(prefix);
	return r;
}

config_t *config_from_old(char *s) {
	config_t *(*fns[])(any_map_t *) = {
	    config_from_version_1,
	    config_from_version_2,
	    config_from_version_3,
	    config_from_version_4,
	    config_from_version_5,
	    config_from_version_6,
	    config_from_version_7,
	    config_from_version_8,
	    config_from_version_9,
	    config_from_version_10,
	    config_from_version_11,
	    config_from_version_12,
	    config_from_version_13,
	    config_from_version_14,
	    config_from_version_15,
	    config_from_version_16,
	    config_from_version_17,
	    config_from_version_18,
	};
	for (i32 v = ARRAY_LENGTH(fns) - 1; v >= 0; --v) {
		if (config_is_version(s, i32_to_string(v + 1))) {
			buffer_t *b = json_encode_to_armpack(s);
			return b != NULL ? fns[v](armpack_decode_to_map(b)) : NULL;
		}
	}
	return NULL;
}

static config_t *config_from_current(char *s) {
	buffer_t *b = json_encode_to_armpack(s);
	if (b == NULL) {
		return NULL;
	}
	return config_from_map_to_config(armpack_decode_to_map(b));
}

config_t *config_from_json(char *s) {
	if (s == NULL) {
		return NULL;
	}
	if (config_is_version(s, manifest_version_config)) {
		return config_from_current(s);
	}
	return config_from_old(s);
}

char *config_get_sha() {
	buffer_t *blob = data_get_blob("version.json");
	if (blob == NULL) {
		return "undefined";
	}
	version_t *v = json_parse(sys_buffer_to_string(blob));
	return v != NULL && v->sha != NULL ? v->sha : "undefined";
}

char *config_get_date() {
	buffer_t *blob = data_get_blob("version.json");
	if (blob == NULL) {
		return "undefined";
	}
	version_t *v = json_parse(sys_buffer_to_string(blob));
	return v != NULL && v->date != NULL ? v->date : "undefined";
}

iron_window_options_t *config_get_options() {
	iron_window_mode_t     window_mode = g_config->window_mode == 0 ? IRON_WINDOW_MODE_WINDOW : IRON_WINDOW_MODE_FULLSCREEN;
	iron_window_features_t features    = IRON_WINDOW_FEATURES_NONE;
	if (g_config->window_resizable) {
		features |= IRON_WINDOW_FEATURES_RESIZABLE;
	}
	if (g_config->window_maximizable) {
		features |= IRON_WINDOW_FEATURES_MAXIMIZABLE;
	}
	if (g_config->window_minimizable) {
		features |= IRON_WINDOW_FEATURES_MINIMIZABLE;
	}
	char                  *title = string("untitled - %s", manifest_title);
	iron_window_options_t *ops   = ALLOC_INIT(iron_window_options_t, {.title     = title,
	                                                                  .width     = g_config->window_w,
	                                                                  .height    = g_config->window_h,
	                                                                  .x         = g_config->window_x,
	                                                                  .y         = g_config->window_y,
	                                                                  .mode      = window_mode,
	                                                                  .features  = features,
	                                                                  .vsync     = g_config->window_vsync,
	                                                                  .frequency = g_config->window_frequency});
	return ops;
}

void config_restore() {
	config_loaded = false;
	config_init();
	ui_dock_reset();
	translator_load_translations(g_config->locale);
	config_apply();
	config_load_theme(g_config->theme, true);
}

void config_import_from(config_t *from) {
	char *_sha        = g_config->sha;
	char *_version    = g_config->version;
	g_config          = from;
	g_config->sha     = string_copy(_sha);
	g_config->version = string_copy(_version);
	keymap_load();
	base_set_workspace(WORKSPACE_PRESET_NONE);
	translator_load_translations(g_config->locale);
	config_apply();
	config_load_theme(g_config->theme, true);
}

void config_apply() {
	config_save();
	ui_tooltips             = g_config->tooltips;
	g_context->ddirty       = 2;
	draw_saved_t saved_draw = draw_suspend();
	render_path_base_apply_config();
	draw_resume(saved_draw);
}

i32 config_get_super_sample_quality(f32 f) {
	return f == 0.25 ? 0 : f == 0.5 ? 1 : f == 1.0 ? 2 : f == 1.5 ? 3 : f == 2.0 ? 4 : 5;
}

f32 config_get_super_sample_size(i32 i) {
	return i == 0 ? 0.25 : i == 1 ? 0.5 : i == 2 ? 1.0 : i == 3 ? 1.5 : i == 4 ? 2.0 : 4.0;
}

i32 config_texture_res_size(i32 pos) {
	return pos == TEXTURE_RES_RES2048 ? 2048 : pos == TEXTURE_RES_RES4096 ? 4096 : pos == TEXTURE_RES_RES8192 ? 8192 : pos == TEXTURE_RES_RES16384 ? 16384 : 0;
}

void config_set_texture_res(i32 pos) {
	if (pos != TEXTURE_RES_CUSTOM) {
		f32 res    = (f32)config_texture_res_size(pos);
		base_res_x = res;
		base_res_y = res;
	}
}

i32 config_get_texture_res_x() {
	return (i32)base_res_x;
}

i32 config_get_texture_res_y() {
	return (i32)base_res_y;
}

i32 config_get_texture_res_pos(i32 i) {
	return i == 2048    ? TEXTURE_RES_RES2048
	       : i == 4096  ? TEXTURE_RES_RES4096
	       : i == 8192  ? TEXTURE_RES_RES8192
	       : i == 16384 ? TEXTURE_RES_RES16384
	                    : TEXTURE_RES_CUSTOM;
}

void config_load_theme(char *theme, bool tag_redraw) {
	g_theme = ui_theme_create();

	if (!string_equals(theme, "default.json")) {
		buffer_t   *b      = data_get_blob(string("themes/%s", theme));
		ui_theme_t *parsed = b != NULL ? json_parse(sys_buffer_to_string(b)) : NULL;
		if (parsed != NULL) {
			g_theme = parsed;
		}
		else { // The theme file is gone, the default theme stays
			g_config->theme = "default.json";
		}
	}

	if (tag_redraw) {
		g_ui->ops->theme = g_theme;
		base_redraw_ui();
	}

	if (g_config->touch_ui) {
		// Enlarge elements
		g_theme->FULL_TABS         = true;
		g_theme->ELEMENT_H         = 24 + 6;
		g_theme->BUTTON_H          = 22 + 6;
		g_theme->FONT_SIZE         = 13 + 2;
		g_theme->ARROW_SIZE        = 5 + 2;
		g_theme->CHECK_SIZE        = 15 + 4;
		g_theme->CHECK_SELECT_SIZE = 8 + 2;
	}
	else {
		g_theme->FULL_TABS = false;
	}
}

#ifdef IRON_IOS
bool config_is_iphone() {
	return sys_display_ppi() > 330;
}
#endif

bool config_is_raytrace_fast() {
	return g_config->pathtrace_mode == PATHTRACE_MODE_FAST;
}
