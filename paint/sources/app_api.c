// The native UI's interface to the app, see app_api.h. #included by main.c after minic_impl.c (it reuses the script_* helpers).

#include "app_api.h"
#include "app_api_test.h"

#define APP_SAME(a, b) ((i32)(a) == (i32)(b))
_Static_assert(APP_SAME(APP_TOOL_BRUSH, TOOL_TYPE_BRUSH) && APP_SAME(APP_TOOL_ERASER, TOOL_TYPE_ERASER) && APP_SAME(APP_TOOL_FILL, TOOL_TYPE_FILL) &&
                   APP_SAME(APP_TOOL_DECAL, TOOL_TYPE_DECAL) && APP_SAME(APP_TOOL_TEXT, TOOL_TYPE_TEXT) && APP_SAME(APP_TOOL_CLONE, TOOL_TYPE_CLONE) &&
                   APP_SAME(APP_TOOL_BLUR, TOOL_TYPE_BLUR) && APP_SAME(APP_TOOL_PARTICLE, TOOL_TYPE_PARTICLE) && APP_SAME(APP_TOOL_PICKER, TOOL_TYPE_PICKER),
               "app_api.h mirrors tool_type_t");
_Static_assert(APP_SAME(APP_VIEWPORT_LIT, VIEWPORT_MODE_LIT) && APP_SAME(APP_VIEWPORT_NORMAL_MAP, VIEWPORT_MODE_NORMAL_MAP) &&
                   APP_SAME(APP_VIEWPORT_TEXCOORD, VIEWPORT_MODE_TEXCOORD) && APP_SAME(APP_VIEWPORT_MATERIAL_ID, VIEWPORT_MODE_MATERIAL_ID) &&
                   APP_SAME(APP_VIEWPORT_MASK, VIEWPORT_MODE_MASK) && APP_SAME(APP_VIEWPORT_PATH_TRACE + 1, VIEWPORT_MODE_PLUGIN),
               "app_api.h mirrors viewport_mode_t");

static bool     app_engine_ready     = false;
static uint32_t app_previews_counter = 0;

// util_layer_update_preview redrew layer thumbnails
void app_notify_previews(void) {
	app_previews_counter++;
}

void app_set_ready(void) {
	app_engine_ready = true;
}

// The engine started and has a project. Every call needs it: the native UI can call before the first frame.
static bool app_ready(void) {
	return app_engine_ready && g_context != NULL && g_project != NULL && g_project->_ != NULL;
}

// Strings handed to Swift are never NULL: String(cString:) crashes on one
static const char *app_str(const char *s) {
	return s != NULL ? s : "";
}

// Signatures and keys: FNV-1a over the values a list or an item shows

#define APP_HASH_SEED 2166136261u

static uint32_t app_hash(uint32_t h, uint32_t v) {
	return (h ^ v) * 16777619u; // FNV-1a step
}

static uint32_t app_hash_str(uint32_t h, const char *s) {
	for (; s != NULL && *s != '\0'; ++s) {
		h = app_hash(h, (uint8_t)*s);
	}
	return h;
}

static uint32_t app_hash_ptr(uint32_t h, const void *p) {
	uint64_t v = (uint64_t)(uintptr_t)p;
	return app_hash(app_hash(h, (uint32_t)v), (uint32_t)(v >> 32));
}

// Bit for bit
static uint32_t app_hash_f32(uint32_t h, float v) {
	uint32_t u;
	memcpy(&u, &v, sizeof(u));
	return app_hash(h, u);
}

// To 1 / 10000: slider values that only drift below what shows do not change it
static uint32_t app_hash_fq(uint32_t h, float v) {
	return app_hash(h, (uint32_t)(int32_t)(v * 10000.0f));
}

static slot_layer_t *app_layer_by_id(int32_t id) {
	return app_ready() ? mask_stack_layer_by_id(id) : NULL;
}

static uint32_t app_layers_signature(void) {
	uint32_t h = APP_HASH_SEED;
	if (g_project == NULL || g_project->_ == NULL) {
		return h;
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		h               = app_hash(h, (uint32_t)l->id);
		h               = app_hash(h, l->parent != NULL ? (uint32_t)l->parent->id : 0xffffffffu);
		h               = app_hash(h, l->visible);
		h               = app_hash(h, (uint32_t)(l->mask_opacity * 1000.0f));
		h               = app_hash(h, l->fill_material != NULL);
		h               = app_hash_str(h, l->name);
	}
	h = app_hash(h, g_context->layer != NULL ? (uint32_t)g_context->layer->id : 0xffffffffu);
	return h;
}

void app_get_state(app_state_t *out) {
	*out = (app_state_t){0};
	if (!app_ready()) {
		return;
	}
	out->ready             = true;
	out->tool              = g_context->tool;
	out->brush_radius      = g_context->brush_radius;
	out->brush_opacity     = g_context->brush_opacity;
	out->brush_hardness    = g_context->brush_hardness;
	out->undos             = history_undos;
	out->redos             = history_redos;
	out->selected_layer_id = g_context->layer != NULL ? g_context->layer->id : -1;
	out->layers_signature  = app_layers_signature();
	out->previews_version  = app_previews_counter;
	out->layer_props_signature = app_layers_props_signature();
	out->view_mode         = ui_base_view_mode();
	out->viewport_mode     = g_context->viewport_mode;
	out->brush_angle       = g_context->brush_angle;
	out->sym_x             = g_context->sym_x;
	out->sym_y             = g_context->sym_y;
	out->sym_z             = g_context->sym_z;
	out->engine_modal      = ui_box_show || ui_menu_show || ui_brush_creator_shown() || material_studio_show || ui_id_mask_active;
	if (scene_camera != NULL) {
		out->camera_fov = scene_camera->data->fov;
	}
	out->camera_pivot    = g_context->camera_pivot;
	out->camera_controls = g_context->camera_controls;
	out->camera_type     = g_context->camera_type;
	out->projects_shown  = box_projects_native_shown;
	uint32_t h           = 2166136261u;
	for (i32 i = 0; i < g_config->recent_projects->length; ++i) {
		h = app_hash_str(h, g_config->recent_projects->buffer[i]);
	}
	out->projects_signature = h;
	if (scene_camera != NULL) {
		quat_t rot         = scene_camera->base->transform->rot;
		out->camera_rot[0] = rot.x;
		out->camera_rot[1] = rot.y;
		out->camera_rot[2] = rot.z;
		out->camera_rot[3] = rot.w;
	}
}

// Thumbnails, read back from the GPU


static float app_clamp01(float v) {
	return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

// RGBA of texel i as 0..1, straight alpha
static void app_texel(gpu_texture_t *t, uint8_t *px, i32 i, float *c) {
	switch (t->format) {
	case GPU_TEXTURE_FORMAT_RGBA64: { // Reads back in RGBA order
		uint16_t *s = (uint16_t *)px + i * 4;
		for (i32 k = 0; k < 4; ++k) {
			f32 v = half_to_float(s[k]);
			c[k]  = app_clamp01(v == v ? v : 0.0f); // NaN reads as 0
		}
		break;
	}
	case GPU_TEXTURE_FORMAT_RGBA128: {
		float *s = (float *)px + i * 4;
		for (i32 k = 0; k < 4; ++k) {
			c[k] = app_clamp01(s[k]);
		}
		break;
	}
	case GPU_TEXTURE_FORMAT_R8:
		c[0] = c[1] = c[2] = px[i] / 255.0f;
		c[3]               = 1.0f;
		break;
	default: // RGBA32 (render targets swapped by gpu_get_texture_pixels_rgba8) and BC7 ones (decompressed) in RGBA order
		for (i32 k = 0; k < 4; ++k) {
			c[k] = px[i * 4 + k] / 255.0f;
		}
		break;
	}
}

// Pixels of t as RGBA8 rows top to bottom, box filtered down by an integer factor to max_size or less (0: full size).
// premultiply: alpha multiplied in, so the transparent background does not darken the edges; straight alpha otherwise.
// With out NULL: the byte count needed, nothing read back. Returns the byte count written, 0 when none.
static int32_t app_read_rgba8(gpu_texture_t *t, int32_t max_size, bool premultiply, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	if (t == NULL || t->width <= 0 || t->height <= 0 || t->format == GPU_TEXTURE_FORMAT_D32 || t->format == GPU_TEXTURE_FORMAT_R16 ||
	    t->format == GPU_TEXTURE_FORMAT_R32) {
		return 0;
	}
	i32 f = 1;
	if (max_size > 0) {
		i32 big = t->width > t->height ? t->width : t->height;
		f       = (big + max_size - 1) / max_size;
	}
	i32 ow    = t->width / f;
	i32 oh    = t->height / f;
	i32 bytes = ow * oh * 4;
	if (ow <= 0 || oh <= 0) {
		return 0;
	}
	if (out == NULL) {
		return bytes; // Known from the size, nothing read back
	}
	if (capacity < bytes) {
		return 0;
	}
	draw_saved_t saved  = draw_suspend();
	buffer_t    *pixels = gpu_get_texture_pixels_rgba8(t); // RGBA order, render targets included
	draw_resume(saved);
	i32 need = t->width * t->height * (t->format == GPU_TEXTURE_FORMAT_R8 ? 1 : t->format == GPU_TEXTURE_FORMAT_RGBA64 ? 8 : t->format == GPU_TEXTURE_FORMAT_RGBA128 ? 16 : 4);
	if (pixels == NULL || pixels->buffer == NULL || pixels->length < need) {
		gpu_release_texture_pixels(t);
		return 0;
	}
	if (f == 1 && !premultiply && t->format == GPU_TEXTURE_FORMAT_RGBA32) { // As it is
		memcpy(out, pixels->buffer, bytes);
	}
	else {
		float n = (float)(f * f);
		for (i32 y = 0; y < oh; ++y) {
			for (i32 x = 0; x < ow; ++x) {
				float s[4] = {0, 0, 0, 0}; // Sums, premultiplied or not
				for (i32 j = 0; j < f; ++j) {
					for (i32 i = 0; i < f; ++i) {
						float c[4];
						app_texel(t, pixels->buffer, (y * f + j) * t->width + x * f + i, c);
						float a = premultiply ? c[3] : 1.0f;
						s[0] += c[0] * a;
						s[1] += c[1] * a;
						s[2] += c[2] * a;
						s[3] += c[3];
					}
				}
				uint8_t *o = &out[(y * ow + x) * 4];
				for (i32 k = 0; k < 4; ++k) {
					o[k] = (uint8_t)(app_clamp01(s[k] / n) * 255.0f + 0.5f);
				}
			}
		}
	}
	gpu_release_texture_pixels(t);
	*w = ow;
	*h = oh;
	return bytes;
}

bool app_native_chrome     = false;
i32  app_overlay_top_inset = 0;

void app_set_overlay_top_inset(int32_t pixels) {
	app_overlay_top_inset = pixels > 0 ? pixels : 0;
	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

void app_set_native_chrome(bool on) {
	app_native_chrome = on;
	ui_base_show      = !on;
	base_resize();
}

// Tool and brush

void app_set_tool(int32_t tool) {
	if (!app_ready() || tool < 0 || tool > TOOL_TYPE_INPAINT || tool == g_context->tool) {
		return;
	}
	context_select_tool(tool);
}

static void app_brush_changed(void) {
	config_brush_save_update();
	g_context->ddirty = 2;
	iron_delay_idle_sleep();
}

void app_set_brush_radius(float value) {
	if (!app_ready()) {
		return;
	}
	g_context->brush_radius = math_max(0.01, math_min(2.0, value));
	app_brush_changed();
}

void app_set_brush_opacity(float value) {
	if (!app_ready()) {
		return;
	}
	g_context->brush_opacity = math_max(0.0, math_min(1.0, value));
	app_brush_changed();
}

void app_set_brush_hardness(float value) {
	if (!app_ready()) {
		return;
	}
	g_context->brush_hardness = math_max(0.0, math_min(1.0, value));
	app_brush_changed();
}

void app_set_brush_angle(float degrees) {
	if (!app_ready()) {
		return;
	}
	g_context->brush_angle = math_max(0.0, math_min(360.0, degrees));
	make_material_parse_paint_material(true);
	app_brush_changed();
}

void app_set_symmetry(bool x, bool y, bool z) {
	if (!app_ready()) {
		return;
	}
	g_context->sym_x = x;
	g_context->sym_y = y;
	g_context->sym_z = z;
	app_brush_changed();
}

// Views and camera

void app_cycle_view_mode(void) {
	if (!app_ready()) {
		return;
	}
	ui_base_cycle_view_mode();
	iron_delay_idle_sleep();
}

void app_set_viewport_mode(int32_t mode) {
	if (!app_ready() || mode < APP_VIEWPORT_LIT || mode > APP_VIEWPORT_PATH_TRACE) {
		return;
	}
	context_set_viewport_mode(mode);
	iron_delay_idle_sleep();
}

void app_view_face(int32_t face) {
	if (!app_ready()) {
		return;
	}
	ui_view_cube_set_view(face); // ui_panels.c
	iron_delay_idle_sleep();
}

void app_view_orbit(float x, float y) {
	if (!app_ready()) {
		return;
	}
	viewport_orbit(x, y);
	iron_delay_idle_sleep();
}

void app_view_roll(float radians, bool held) {
	if (!app_ready()) {
		return;
	}
	camera_roll(radians, held);
	iron_delay_idle_sleep();
}

// As the old Camera menu's FoV slider: a perspective camera moves along its view to keep the pivot's framing
void app_set_camera_fov(float fov) {
	if (!app_ready() || scene_camera == NULL) {
		return;
	}
	fov         = math_max(0.3, math_min(1.4, fov));
	f32 old_fov = scene_camera->data->fov;
	scene_camera->data->fov = fov;
	if (g_context->camera_type == CAMERA_TYPE_PERSPECTIVE && fov != old_fov) {
		transform_t *t    = scene_camera->base->transform;
		vec4_t       look = camera_object_look(scene_camera);
		f32          d    = vec4_dot(vec4_sub(camera_origins[camera_index()], t->loc), look);
		f32          d2   = d * math_tan(old_fov / 2.0) / math_tan(fov / 2.0);
		if (d > 0.0) {
			transform_move(t, look, d - d2);
		}
	}
	viewport_update_camera_type(g_context->camera_type);
	g_context->ddirty = 2;
	iron_delay_idle_sleep();
}

void app_set_camera_pivot(int32_t pivot) {
	if (!app_ready()) {
		return;
	}
	g_context->camera_pivot = pivot == 1 ? 1 : 0;
}

void app_set_camera_controls(int32_t controls) {
	if (app_ready() && controls >= CAMERA_CONTROLS_ORBIT && controls <= CAMERA_CONTROLS_FLY) {
		g_context->camera_controls = controls;
	}
}

void app_set_camera_type(int32_t type) {
	if (!app_ready() || (type != CAMERA_TYPE_PERSPECTIVE && type != CAMERA_TYPE_ORTHOGRAPHIC)) {
		return;
	}
	g_context->camera_type = type;
	viewport_update_camera_type(type);
	g_context->ddirty = 2;
	iron_delay_idle_sleep();
}

static void app_capture_screenshot(void *_) {
	viewport_capture_screenshot();
}

// Projects screen

int32_t app_project_count(void) {
	return app_ready() ? g_config->recent_projects->length : 0;
}

const char *app_project_path(int32_t index) {
	if (index < 0 || index >= app_project_count()) {
		return "";
	}
	return app_str(box_projects_recent_path(index));
}

void app_project_open(int32_t index) {
	if (index < 0 || index >= app_project_count()) {
		return;
	}
	char *path = box_projects_recent_path(index);
	if (!iron_file_exists(path)) {
		return;
	}
#ifdef IRON_IOS
	console_toast(tr("Opening project"));
#endif
	box_projects_load(path);
}

void app_project_delete(int32_t index) {
	if (index < 0 || index >= app_project_count()) {
		return;
	}
	box_projects_delete(index);
}

void app_project_new(void) {
	if (!app_ready()) {
		return;
	}
	box_projects_new();
}

void app_project_import(void) {
	if (!app_ready()) {
		return;
	}
	box_projects_import();
}

void app_command(int32_t command) {
	if (!app_ready()) {
		return;
	}
	switch (command) {
	case APP_COMMAND_NEW_PROJECT:
		project_new_box();
		break;
	case APP_COMMAND_OPEN:
		project_open();
		break;
	case APP_COMMAND_OPEN_RECENT:
		box_projects_show();
		break;
	case APP_COMMAND_SAVE:
		project_save(false);
		break;
	case APP_COMMAND_SAVE_AS:
		project_save_as(false);
		break;
	case APP_COMMAND_IMPORT_TEXTURE:
		project_import_asset(string_array_join(path_texture_formats(), ","), false);
		break;
	case APP_COMMAND_IMPORT_FONT:
		project_import_asset("ttf,ttc,otf", true);
		break;
	case APP_COMMAND_IMPORT_MATERIAL:
		project_import_material();
		break;
	case APP_COMMAND_IMPORT_BRUSH:
		project_import_brush();
		break;
	case APP_COMMAND_IMPORT_SWATCHES:
		project_import_swatches(false);
		break;
	case APP_COMMAND_IMPORT_MESH:
		project_import_mesh(true, NULL);
		break;
	case APP_COMMAND_REIMPORT_MESH:
		project_reimport_mesh();
		break;
	case APP_COMMAND_REIMPORT_TEXTURES:
		project_reimport_textures();
		break;
	case APP_COMMAND_EXPORT_TEXTURES:
		g_context->layers_export = EXPORT_MODE_VISIBLE;
		box_export_tab           = 0;
		box_export_show_textures();
		break;
	case APP_COMMAND_EXPORT_MESH:
		g_context->export_mesh_index = 0; // All
		box_export_show_mesh();
		break;
	case APP_COMMAND_EXPORT_SWATCHES:
		project_export_swatches();
		break;
	case APP_COMMAND_BAKE_MATERIAL:
		box_export_show_bake_material();
		break;
	case APP_COMMAND_BAKE_MESH_MAPS:
		box_bake_maps_show();
		break;
	case APP_COMMAND_PREFERENCES:
		box_preferences_show();
		break;
	case APP_COMMAND_VIEW_RESET:
		viewport_reset();
		viewport_scale_to_bounds(2.0);
		break;
	case APP_COMMAND_VIEW_FRAME:
		viewport_frame();
		break;
	case APP_COMMAND_ZOOM_IN:
		viewport_zoom(0.2);
		break;
	case APP_COMMAND_ZOOM_OUT:
		viewport_zoom(-0.2);
		break;
	case APP_COMMAND_ORBIT_OPPOSITE:
		viewport_orbit_opposite();
		break;
	case APP_COMMAND_SCREENSHOT:
		sys_notify_on_next_frame(app_capture_screenshot, NULL); // After the frame, with the ui hidden
		break;
	case APP_COMMAND_DELETE_UNUSED:
		tab_materials_delete_unused();
		tab_textures_delete_unused();
		break;
	}
	g_context->ddirty = 2;
	iron_delay_idle_sleep();
}

// History

const char *app_undo_name(void) {
	if (!app_ready() || history_undos <= 0) {
		return "";
	}
	history_step_t *step = history_steps->buffer[history_steps->length - 1 - history_redos];
	return app_str(step->name);
}

void app_undo(void) {
	if (!app_ready()) {
		return;
	}
	history_undo();
	iron_delay_idle_sleep();
}

void app_redo(void) {
	if (!app_ready()) {
		return;
	}
	history_redo();
	iron_delay_idle_sleep();
}

// Layers

int32_t app_layer_count(void) {
	return app_ready() ? layers_op_count() : 0;
}

static i32 app_layer_depth(slot_layer_t *l) {
	i32 depth = 0;
	for (slot_layer_t *p = l->parent; p != NULL; p = p->parent) {
		depth++;
	}
	return depth;
}

bool app_layer_info(int32_t index, app_layer_info_t *out) {
	i32 n = app_layer_count();
	if (index < 0 || index >= n) {
		return false;
	}
	slot_layer_t *l = g_project->_->layers->buffer[n - 1 - index]; // Stored bottom to top
	out->id         = l->id;
	out->parent_id  = l->parent != NULL ? l->parent->id : -1;
	out->depth      = app_layer_depth(l);
	out->kind       = slot_layer_is_group(l) ? APP_LAYER_KIND_GROUP
	                  : slot_layer_is_mask(l) ? APP_LAYER_KIND_MASK
	                  : slot_layer_is_filter(l) ? APP_LAYER_KIND_FILTER
	                                            : APP_LAYER_KIND_LAYER;
	out->visible    = l->visible;
	out->selected   = l == g_context->layer;
	out->is_fill    = l->fill_material != NULL;
	out->opacity    = l->mask_opacity;
	out->name       = app_str(l->name);
	return true;
}

bool app_layer_set_visible(int32_t id, bool visible) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || l->visible == visible) {
		return false;
	}
	tab_layers_layer_toggle_visible(l);
	iron_delay_idle_sleep();
	return true;
}

bool app_layer_delete(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || !tab_layers_can_delete(l)) {
		return false;
	}
	layers_op_delete(l);
	iron_delay_idle_sleep();
	return true;
}

// The slider drag in progress, one undo step for the whole gesture: the opacity of a layer, a layer value
// (app_layer_value_begin) or a mask effect value (app_mask_effect_value_begin). Named by ids, not pointers: a layer
// deleted during the drag is not taken for a new one at the same address.
typedef enum {
	APP_DRAG_NONE,
	APP_DRAG_OPACITY,
	APP_DRAG_LAYER_VALUE,
	APP_DRAG_MASK_VALUE,
} app_drag_kind_t;

typedef struct {
	app_drag_kind_t kind;
	i32             id;
	i32             index; // The value, or the mask effect (stack index)
	i32             field; // Mask effects: the opacity (-1) or a param
} app_drag_t;

static app_drag_t app_drag = {.kind = APP_DRAG_NONE};

static void app_drag_begin(app_drag_kind_t kind, i32 id, i32 index, i32 field) {
	app_drag = (app_drag_t){.kind = kind, .id = id, .index = index, .field = field};
}

// A set that is part of the drag in progress, otherwise it is its own undo step
static bool app_dragging(app_drag_kind_t kind, i32 id, i32 index, i32 field) {
	return app_drag.kind == kind && app_drag.id == id && app_drag.index == index && app_drag.field == field;
}

void app_drag_end(void) {
	app_drag = (app_drag_t){.kind = APP_DRAG_NONE};
}

void app_layer_opacity_begin(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return;
	}
	if (l != g_context->layer) {
		context_set_layer(l);
	}
	history_layer_opacity();
	app_drag_begin(APP_DRAG_OPACITY, id, 0, 0);
}

bool app_layer_set_opacity(int32_t id, float value) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return false;
	}
	if (!app_dragging(APP_DRAG_OPACITY, id, 0, 0)) {
		app_layer_opacity_begin(id);
	}
	l->mask_opacity = math_max(0.0, math_min(1.0, value));
	if (l == g_project->_->layers->buffer[0] && l->mask_opacity < 1.0 && !g_project->_->translucency) {
		layers_enable_translucency();
		make_material_parse_mesh_material();
	}
	else { // A shader constant, see make_mesh_opacity
		g_context->ddirty = 2;
		render_path_base_redraw();
	}
	iron_delay_idle_sleep();
	return true;
}

int32_t app_layer_thumbnail(int32_t id, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL ? app_read_rgba8(l->texpaint_preview, 0, false, out, capacity, w, h) : 0;
}
