#pragma once

// For scripts (tests/app_api*.c), not imported by Swift: getters of single fields Swift reads from the info structs
// (app_layer_info_t, app_layer_props_t, app_mask_effect_t, ...), and older calls the tests still use.

#include <stdbool.h>
#include <stdint.h>

// Layers (app_api.h)
int32_t app_layer_id_at(int32_t index); // -1 past the last
int32_t app_layer_thumbnail_size(int32_t id);
bool    app_layer_select(int32_t id);
bool    app_layer_add(void); // A paint layer above the selected one
bool    app_layer_can_delete(int32_t id);

// Layer properties (app_api_layers.h) and mask effects (app_api_masks.h)
uint32_t app_layers_props_signature(void); // app_state_t.layer_props_signature
bool     app_layer_expanded(int32_t id);
int32_t  app_layer_uv_type(int32_t id);
int32_t  app_mask_effect_type(int32_t id, int32_t index); // -1: none
bool     app_mask_effect_visible(int32_t id, int32_t index);
int32_t  app_mask_effect_blending(int32_t id, int32_t index);
float    app_mask_effect_value(int32_t id, int32_t index, int32_t field);
bool     app_mask_effect_needs_ao(int32_t id, int32_t index);
bool     app_mask_effect_needs_thickness(int32_t id, int32_t index);
bool     app_mask_effect_needs_id_map(int32_t id, int32_t index);
uint32_t app_layer_previews_version(void); // app_state_t.previews_version

// Materials (app_api_materials.h)
int32_t app_material_id_at(int32_t index);
int32_t app_material_preview_version(int32_t id);
int32_t app_material_thumbnail_size(int32_t id, int32_t max_size);
int32_t app_library_icon_size(int32_t index);
int32_t app_library_find(const char *name); // First item of that name, -1 when none
int32_t app_smart_icon_size(int32_t index);

// Brushes and color (app_api_brushes.h): "" / -1 when not found
int32_t     app_brush_id_at(int32_t index);
int32_t     app_brush_group_of(int32_t id);
const char *app_brush_name(int32_t id);
const char *app_brush_group_name(int32_t id);
bool        app_brush_group_builtin(int32_t id);
uint32_t    app_color_base(void);
float       app_color_value(int32_t which); // app_color_value_t
uint32_t    app_swatch_base(int32_t index);
