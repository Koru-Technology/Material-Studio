// The native UI's interface to the app, see app_api.h. #included by main.c after minic_impl.c (it reuses the script_* helpers).

#include "app_api.h"

static bool     app_engine_ready     = false;
static uint32_t app_previews_counter = 0;

// util_layer_update_preview redrew layer thumbnails
void app_notify_previews(void) {
	app_previews_counter++;
}

void app_set_ready(void) {
	app_engine_ready = true;
}

static uint32_t app_hash(uint32_t h, uint32_t v) {
	return (h ^ v) * 16777619u; // FNV-1a step
}

static uint32_t app_hash_str(uint32_t h, const char *s) {
	for (; s != NULL && *s != '\0'; ++s) {
		h = app_hash(h, (uint8_t)*s);
	}
	return h;
}

static slot_layer_t *app_layer_by_id(int32_t id) {
	if (g_project == NULL || g_project->_ == NULL) {
		return NULL;
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->id == id) {
			return l;
		}
	}
	return NULL;
}

static uint32_t app_layers_signature(void) {
	uint32_t h = 2166136261u;
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
	if (!app_engine_ready || g_context == NULL) {
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
}

bool app_native_chrome = false;

void app_set_native_chrome(bool on) {
	app_native_chrome = on;
	ui_base_show      = !on;
	base_resize();
}

// Tool and brush

void app_set_tool(int32_t tool) {
	if (tool < 0 || tool > TOOL_TYPE_INPAINT || tool == g_context->tool) {
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
	g_context->brush_radius = math_max(0.01, math_min(2.0, value));
	app_brush_changed();
}

void app_set_brush_opacity(float value) {
	g_context->brush_opacity = math_max(0.0, math_min(1.0, value));
	app_brush_changed();
}

void app_set_brush_hardness(float value) {
	g_context->brush_hardness = math_max(0.0, math_min(1.0, value));
	app_brush_changed();
}

// History

const char *app_undo_name(void) {
	if (history_undos <= 0) {
		return "";
	}
	history_step_t *step = history_steps->buffer[history_steps->length - 1 - history_redos];
	return step->name != NULL ? step->name : "";
}

void app_undo(void) {
	history_undo();
	iron_delay_idle_sleep();
}

void app_redo(void) {
	history_redo();
	iron_delay_idle_sleep();
}

// Layers

int32_t app_layer_count(void) {
	return script_layer_count();
}

static i32 app_layer_depth(slot_layer_t *l) {
	i32 depth = 0;
	for (slot_layer_t *p = l->parent; p != NULL; p = p->parent) {
		depth++;
	}
	return depth;
}

bool app_layer_info(int32_t index, app_layer_info_t *out) {
	i32 n = script_layer_count();
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
	out->name       = l->name;
	return true;
}

int32_t app_layer_id_at(int32_t index) {
	app_layer_info_t info;
	return app_layer_info(index, &info) ? info.id : -1;
}

int32_t app_layer_thumbnail_size(int32_t id) {
	int32_t w, h;
	return app_layer_thumbnail(id, NULL, 0, &w, &h);
}

bool app_layer_select(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return false;
	}
	script_layer_set(l);
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

bool app_layer_rename(int32_t id, const char *name) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || name == NULL || name[0] == '\0') {
		return false;
	}
	script_layer_set_name(l, string_copy((char *)name));
	return true;
}

bool app_layer_add(void) {
	return script_layer_new() != NULL;
}

bool app_layer_can_delete(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && tab_layers_can_delete(l);
}

bool app_layer_delete(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || !tab_layers_can_delete(l)) {
		return false;
	}
	script_layer_delete(l);
	iron_delay_idle_sleep();
	return true;
}

static slot_layer_t *app_edit_layer = NULL; // Layer of the slider drag in progress

void app_layer_opacity_begin(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return;
	}
	if (l != g_context->layer) {
		context_set_layer(l);
	}
	history_layer_opacity();
	app_edit_layer = l;
}

bool app_layer_set_opacity(int32_t id, float value) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return false;
	}
	if (l != app_edit_layer) { // A change without a drag: its own undo step
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

void app_edit_end(void) {
	app_edit_layer = NULL;
}

int32_t app_layer_thumbnail(int32_t id, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || l->texpaint_preview == NULL) {
		return 0;
	}
	gpu_texture_t *t      = l->texpaint_preview;
	buffer_t      *pixels = script_get_texture_pixels(t);
	if (pixels == NULL) {
		return 0;
	}
	i32 bytes = t->width * t->height * 4;
	if (out == NULL) {
		gpu_release_texture_pixels(t);
		return pixels->length >= bytes ? bytes : 0;
	}
	if (bytes > capacity || pixels->length < bytes) {
		return 0;
	}
	memcpy(out, pixels->buffer, bytes);
	gpu_release_texture_pixels(t);
	*w = t->width;
	*h = t->height;
	return bytes;
}
