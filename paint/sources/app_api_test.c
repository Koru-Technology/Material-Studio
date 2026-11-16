// app_api_test.h, #included by main.c after the other app_api*.c files (it uses their helpers)

#include "app_api_test.h"

// Layers

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
	layers_op_set(l);
	return true;
}

bool app_layer_add(void) {
	return app_ready() && layers_op_new() != NULL;
}

bool app_layer_can_delete(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && tab_layers_can_delete(l);
}

// Layer properties and mask effects

bool app_layer_expanded(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && l->show_panel;
}

int32_t app_layer_uv_type(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL ? (int32_t)l->uv_type : -1;
}

int32_t app_mask_effect_type(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) ? e.type : -1;
}

bool app_mask_effect_visible(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) && e.visible;
}

int32_t app_mask_effect_blending(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) ? e.blending : -1;
}

float app_mask_effect_value(int32_t id, int32_t index, int32_t field) {
	app_mask_effect_t e;
	if (!app_mask_effect(id, index, &e) || field < -1 || field >= APP_MASK_EFFECT_PARAMS) {
		return 0.0;
	}
	return field < 0 ? e.opacity : e.params[field];
}

bool app_mask_effect_needs_ao(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) && e.needs_ao;
}

bool app_mask_effect_needs_thickness(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) && e.needs_thickness;
}

bool app_mask_effect_needs_id_map(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) && e.needs_id_map;
}

uint32_t app_layer_previews_version(void) {
	return app_previews_counter;
}

// Materials

int32_t app_material_id_at(int32_t index) {
	app_material_info_t info;
	return app_material_info(index, &info) ? info.id : -1;
}

int32_t app_material_preview_version(int32_t id) {
	slot_material_t *m = app_mat_by_id(id);
	app_mat_track_t *t = m != NULL ? app_mat_track_of(m) : NULL;
	return t != NULL ? (int32_t)t->version : -1;
}

int32_t app_material_thumbnail_size(int32_t id, int32_t max_size) {
	int32_t w, h;
	return app_material_thumbnail(id, max_size, NULL, 0, &w, &h);
}

int32_t app_library_icon_size(int32_t index) {
	int32_t w, h;
	return app_library_icon(index, NULL, 0, &w, &h);
}

int32_t app_library_find(const char *name) {
	app_library_item_t item;
	for (i32 i = 0; i < app_library_count(); ++i) {
		if (app_library_item(i, &item) && name != NULL && strcmp(item.name, name) == 0) {
			return i;
		}
	}
	return -1;
}

int32_t app_smart_icon_size(int32_t index) {
	int32_t w, h;
	return app_smart_icon(index, NULL, 0, &w, &h);
}

// Brushes and color

int32_t app_brush_id_at(int32_t index) {
	app_brush_info_t info;
	return app_brush_info(index, &info) ? info.id : -1;
}

int32_t app_brush_group_of(int32_t id) {
	brush_def_t *d = app_ready() ? brush_library_find(id) : NULL;
	return d != NULL ? d->group : -1;
}

const char *app_brush_name(int32_t id) {
	brush_def_t *d = app_ready() ? brush_library_find(id) : NULL;
	return app_str(d != NULL ? brush_library_name(d) : NULL);
}

const char *app_brush_group_name(int32_t id) {
	brush_group_t *g = app_ready() ? brush_group_find(id) : NULL;
	return app_str(g != NULL ? brush_group_name(g) : NULL);
}

bool app_brush_group_builtin(int32_t id) {
	brush_group_t *g = app_ready() ? brush_group_find(id) : NULL;
	return g != NULL && g->builtin;
}

uint32_t app_color_base(void) {
	app_color_t c;
	return app_color_get(&c) ? c.base : 0;
}

float app_color_value(int32_t which) {
	if (!app_ready() || g_context->picked_color == NULL) {
		return 0.0f;
	}
	f32 *v = app_color_field(g_context->picked_color, which);
	return v != NULL ? *v : 0.0f;
}

uint32_t app_swatch_base(int32_t index) {
	app_color_t c;
	return app_swatch_info(index, &c) ? c.base : 0;
}
