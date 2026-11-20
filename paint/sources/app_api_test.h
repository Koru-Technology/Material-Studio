#pragma once

// For scripts (tests/app_api*.c), not imported by Swift: getters of single fields Swift reads from the info structs
// (app_layer_info_t, app_layer_props_t, app_mask_effect_t, ...), and older calls the tests still use.

#include <stdbool.h>
#include <stdint.h>

// Layers (app_api.h)
int32_t app_layer_id_at(int32_t index); // -1 past the last
int32_t app_layer_thumbnail_size(int32_t id);
bool    app_layer_thumbnail_ready(int32_t id);
bool    app_thumbnails_busy(void);
bool    app_layer_select(int32_t id);
bool    app_layer_add(void); // A paint layer above the selected one
bool    app_layer_can_delete(int32_t id);

// Layer properties (app_api_layers.h) and mask effects (app_api_masks.h)
uint32_t app_layers_props_signature(void); // app_state_t.layer_props_signature
bool     app_layer_expanded(int32_t id);
int32_t  app_layer_fill_projection(int32_t id);
int32_t  app_layer_fill_kind(int32_t id, int32_t channel);
int32_t  app_mask_effect_type(int32_t id, int32_t index); // -1: none
bool     app_mask_effect_visible(int32_t id, int32_t index);
int32_t  app_mask_effect_blending(int32_t id, int32_t index);
float    app_mask_effect_value(int32_t id, int32_t index, int32_t field);
bool     app_mask_effect_needs_ao(int32_t id, int32_t index);
bool     app_mask_effect_needs_thickness(int32_t id, int32_t index);
bool     app_mask_effect_needs_id_map(int32_t id, int32_t index);
uint32_t app_layer_previews_version(void); // app_state_t.previews_version

// Bake Mesh Maps (app_api_bake.h)
void  app_test_bake_options(int32_t maps, int32_t size, int32_t rays); // Bits of app_bake_map_t
float app_test_bake_progress(void);
float app_test_bake_missed(void);
uint32_t    app_test_layer_channels(void); // The selected layer's, bits of app_layer_channel_t
const char *app_test_layer_name(void);
bool        app_test_write_shell_obj(char *src, char *dst, float scale, bool only);
void        app_test_bake_max_distance(float d);
bool        app_test_write_split_obj(char *src, char *dst);
int32_t     app_test_texture_colors(int32_t texture); // Colors but black covering 1% or more, -1 when unreadable
const char *app_test_id_map(void);
int32_t     app_test_id_parts(void); // app_id_pick_t.parts                    // The project's ID map asset name, "" for none
float       app_test_texture_bright(int32_t texture); // Fraction of texels over half, -1 when unreadable
int32_t app_test_bake_preview(int32_t high); // Bytes of a 320 x 240 preview, 0 for none

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
