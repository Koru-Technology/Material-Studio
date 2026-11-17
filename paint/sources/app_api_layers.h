#pragma once

// app_api.h, layers: what the native layers panel needs. Self-contained, imported by Swift.
// Layers are named by id (app_layer_info_t.id), the rest of app_api.h's rules apply.

#include <stdbool.h>
#include <stdint.h>

// Channels a layer paints, bits of app_layer_props_t.channels
typedef enum {
	APP_LAYER_CHANNEL_BASE         = 0,
	APP_LAYER_CHANNEL_OPACITY      = 1,
	APP_LAYER_CHANNEL_NORMAL       = 2,
	APP_LAYER_CHANNEL_NORMAL_BLEND = 3,
	APP_LAYER_CHANNEL_ROUGHNESS    = 4,
	APP_LAYER_CHANNEL_METALLIC     = 5,
	APP_LAYER_CHANNEL_HEIGHT       = 6,
	APP_LAYER_CHANNEL_HEIGHT_BLEND = 7,
	APP_LAYER_CHANNEL_EMISSION     = 8,
	APP_LAYER_CHANNEL_SUBSURFACE   = 9,
	APP_LAYER_CHANNEL_OCCLUSION    = 10,
	APP_LAYER_CHANNEL_COUNT        = 11,
} app_layer_channel_t;

// What a layer is and which of the layer menu's actions apply to it, as util_layer_ops.c decides
typedef struct {
	int32_t id;
	bool    has_children; // A group with layers, or a layer with masks or filters
	bool    expanded;     // Its children show (groups and layers with masks fold)
	bool    is_path;      // Path, curve or text layer: painted along points with a material
	bool    is_text;
	bool    is_decal;     // Fill layer projected from the decal box (decal or planar: the gizmo moves the box)
	bool    is_fill;      // Fill layer or fill mask: app_layer_fill, app_layer_fill_source
	int32_t mask_id;      // The first mask of a layer, -1: none
	int32_t blending;     // blend_type_t, 0..17: app_layer_blending_name
	int32_t uv_map;       // 0 uv0, 1 uv1
	bool    has_uv1;      // The mesh has a second UV map
	uint32_t channels;    // 1 << app_layer_channel_t
	// Masks
	int32_t effect_count;         // app_mask_effect_count
	// Menu items: shown
	bool shows_blending;  // Layers, not masks, groups or filters
	bool shows_channels;  // Not groups
	bool shows_to_fill;   // "To Fill Layer" / "To Fill Mask"
	bool shows_to_paint;  // "To Paint Layer" / "To Paint Mask"
	bool shows_clear;
	bool shows_invert;
	bool shows_apply;
	bool shows_add_mask;   // The layer has no mask yet: one per layer, as Painter
	bool shows_add_effect; // Layers, groups and masks: "Add Effect" puts it in the mask, made when there is none
	bool shows_merge_group;
	bool shows_save_smart_material;
	// Menu items: enabled
	bool can_delete;
	bool can_merge_down;
	bool can_merge_group;
	bool can_id_mask; // The selected layer takes an ID Selection (not a group)
} app_layer_props_t;

bool     app_layer_props(int32_t id, app_layer_props_t *out);

const char *app_layer_blending_name(int32_t blending); // "Mix", ..., "" past the last
int32_t     app_layer_blending_count(void);

// Selects the layer, a fill or path layer also selects its material (a tap in the layers panel)
bool app_layer_pick(int32_t id);
bool app_layer_set_expanded(int32_t id, bool expanded);
bool app_layer_set_name(int32_t id, const char *name); // An undo step, a text layer repaints its text

// New layers, as the "+" menu
typedef enum {
	APP_LAYER_NEW_PAINT        = 0,
	APP_LAYER_NEW_FILL         = 1,
	APP_LAYER_NEW_DECAL        = 2,
	APP_LAYER_NEW_COLOR        = 3, // Filled with the current color
	APP_LAYER_NEW_TEXT         = 4,
	APP_LAYER_NEW_PATH         = 5,
	APP_LAYER_NEW_CURVE        = 6,
	APP_LAYER_NEW_GROUP        = 7,
	APP_LAYER_NEW_FILTER       = 8,
	APP_LAYER_NEW_BLACK_MASK   = 9,
	APP_LAYER_NEW_WHITE_MASK   = 10,
	APP_LAYER_NEW_FILL_MASK    = 11,
	APP_LAYER_NEW_ID_MASK      = 12, // An ID Selection effect in the layer's mask (a black one made first), picking its parts
} app_layer_new_t;

bool app_layer_new(int32_t kind); // On the selected layer. Fill, decal, color and filter layers appear on the next frame
bool app_layer_can_new(int32_t kind);

// Layer menu
bool app_layer_duplicate(int32_t id);
bool app_layer_merge_down(int32_t id);
bool app_layer_merge_group(int32_t id);
bool app_layer_clear(int32_t id);
bool app_layer_to_fill(int32_t id);
bool app_layer_to_paint(int32_t id);
bool app_layer_invert_mask(int32_t id); // Adds an Invert effect, the painted texture stays (Apply bakes it)
bool app_layer_apply_mask(int32_t id);
bool app_layer_add_mask(int32_t id, int32_t kind); // APP_LAYER_NEW_*_MASK
void app_layer_export(int32_t id);                 // The export box (layers) or a file picker (masks)
void app_layer_save_smart_material(int32_t id);

// Order: id goes directly below upper_id in the list (-1: to the top). Into an open group when upper_id is that group.
bool app_layer_can_move_below(int32_t id, int32_t upper_id);
bool app_layer_move_below(int32_t id, int32_t upper_id);
bool app_layer_set_group(int32_t id, int32_t group_id); // Into a group at its top, -1: out of its group, below it

// Properties. Each change is an undo step, except channels and the UV map (no history)
bool app_layer_set_blending(int32_t id, int32_t blending);
bool app_layer_set_channel(int32_t id, int32_t channel, bool on);
bool app_layer_set_uv_map(int32_t id, int32_t uv_map);

// Slider drags: one undo step for the whole gesture. Begin, set any number of times, end (app_drag_end).
// A set without a begin is its own undo step.
typedef enum {
	APP_LAYER_VALUE_FILL_SCALE_X           = 6, // Fill projection: repeats along each axis
	APP_LAYER_VALUE_FILL_SCALE_Y           = 7,
	APP_LAYER_VALUE_FILL_OFFSET_X          = 8, // -1..1
	APP_LAYER_VALUE_FILL_OFFSET_Y          = 9,
	APP_LAYER_VALUE_FILL_ANGLE             = 10, // Degrees
	APP_LAYER_VALUE_FILL_HARDNESS          = 11, // Triplanar: 0 soft .. 1 hard plane edges
	APP_LAYER_VALUE_FILL_SCALE             = 12, // Both axes at once (linked), reads as X
	APP_LAYER_VALUE_FILL_OFFSET            = 13,
	APP_LAYER_VALUE_FILL_SOURCE            = 64, // + channel * 32 + app_fill_value_t: a value of a channel's source
} app_layer_value_t;

// The values of one source, after APP_LAYER_VALUE_FILL_SOURCE + channel * 32
typedef enum {
	APP_FILL_VALUE       = 0, // A value channel's color source; a pattern's low end (a normal: its bump strength)
	APP_FILL_VALUE2      = 1, // A pattern's high end
	APP_FILL_VALUE_PARAM = 2, // + i: pattern setting i (app_fill_param_name)
} app_fill_value_t;

void app_layer_value_begin(int32_t id, int32_t value);
bool app_layer_set_value(int32_t id, int32_t value, float v);
float app_layer_value(int32_t id, int32_t value);
bool  app_layer_value_range(int32_t id, int32_t value, float *min, float *max);

// Fill layers and fill masks, as Painter's: each channel takes its own source (a color or value, a project image, a
// pattern between two colors or values, or that channel of a material), evaluated live in the viewport and projected
// by the uv map, triplanar planes, a planar or a decal box. A fill mask has one channel, APP_FILL_CHANNEL_BASE, as gray.
typedef enum {
	APP_FILL_CHANNEL_BASE      = 0,
	APP_FILL_CHANNEL_OPACITY   = 1,
	APP_FILL_CHANNEL_NORMAL    = 2,
	APP_FILL_CHANNEL_ROUGHNESS = 3,
	APP_FILL_CHANNEL_METALLIC  = 4,
	APP_FILL_CHANNEL_HEIGHT    = 5,
	APP_FILL_CHANNEL_OCCLUSION = 6,
	APP_FILL_CHANNEL_EMISSION  = 7,
	APP_FILL_CHANNEL_COUNT     = 8,
} app_fill_channel_t;

typedef enum {
	APP_FILL_SOURCE_COLOR      = 0,
	APP_FILL_SOURCE_TEXTURE    = 1,
	APP_FILL_SOURCE_PROCEDURAL = 2,
	APP_FILL_SOURCE_MATERIAL   = 3,
} app_fill_source_kind_t;

typedef enum {
	APP_FILL_PROJECTION_UV        = 0,
	APP_FILL_PROJECTION_TRIPLANAR = 1,
	APP_FILL_PROJECTION_DECAL     = 2,
	APP_FILL_PROJECTION_PLANAR    = 3,
} app_fill_projection_t;

typedef struct {
	int32_t projection; // app_fill_projection_t
	float   scale_x;
	float   scale_y;
	float   offset_x;
	float   offset_y;
	float   angle;
	float   hardness;
	bool    is_mask;
} app_fill_t;

typedef struct {
	int32_t kind;          // app_fill_source_kind_t
	float   color[3];      // Color: rgb, a value channel uses [0]. Pattern: the low end
	float   color2[3];     // Pattern: the high end
	int32_t image;         // Texture: index in the project images (app_mask_image_name), -1 none
	int32_t image_channel; // Texture into a value channel: 0 r, 1 g, 2 b, 3 a, 4 luminance
	int32_t pattern;       // Pattern: app_fill_pattern_name
	float   params[13];    // Pattern settings, app_fill_param_name
	int32_t material;      // Material: index in the project materials, -1 none
} app_fill_source_t;

bool app_layer_fill(int32_t id, app_fill_t *out);
bool app_layer_fill_source(int32_t id, int32_t channel, app_fill_source_t *out);
// Undo steps each
bool app_layer_set_fill_projection(int32_t id, int32_t projection);
bool app_layer_set_fill_kind(int32_t id, int32_t channel, int32_t kind);
bool app_layer_set_fill_image(int32_t id, int32_t channel, int32_t image);
bool app_layer_set_fill_image_channel(int32_t id, int32_t channel, int32_t image_channel);
bool app_layer_set_fill_pattern(int32_t id, int32_t channel, int32_t pattern); // Its settings at their defaults
bool app_layer_set_fill_material(int32_t id, int32_t channel, int32_t material); // channel -1: every channel
// A color of a source (which 0: color, 1: color2): changes in a row are one undo step, until app_drag_end
bool app_layer_set_fill_color(int32_t id, int32_t channel, int32_t which, float r, float g, float b);

int32_t     app_layer_material_count(void);
const char *app_layer_material_name(int32_t index); // Project materials
int32_t     app_fill_pattern_count(void);
const char *app_fill_pattern_name(int32_t pattern);
int32_t     app_fill_param_count(int32_t pattern); // The pattern's settings: 7 shared, then its own
const char *app_fill_param_name(int32_t pattern, int32_t i);
float       app_fill_param_min(int32_t pattern, int32_t i);
float       app_fill_param_max(int32_t pattern, int32_t i);
