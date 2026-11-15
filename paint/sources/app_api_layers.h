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

// What a layer is and which of the layer menu's actions apply to it, as the engine's layers tab decides
typedef struct {
	int32_t id;
	bool    has_children; // A group with layers, or a layer with masks or filters
	bool    expanded;     // Its children show (groups and layers with masks fold)
	bool    is_path;      // Path, curve or text layer: painted along points with a material
	bool    is_text;
	bool    is_decal;     // Fill layer projected from the view
	int32_t mask_id;      // The first mask of a layer, -1: none
	int32_t blending;     // blend_type_t, 0..17: app_layer_blending_name
	int32_t uv_type;      // Fill layers: 0 UV map, 1 triplanar, 2 project
	int32_t uv_map;       // 0 uv0, 1 uv1
	bool    has_uv1;      // The mesh has a second UV map
	float   scale;        // Fill layers: UV scale, 0..5
	float   angle;        // Fill layers: degrees
	int32_t fill_material; // Index of the fill material in the project materials, -1: not a fill layer
	uint32_t channels;     // 1 << app_layer_channel_t
	// Masks
	bool  height_blend;
	bool  height_blend_below;
	float height_blend_reach;     // 0..0.1
	float height_blend_influence; // 0..4
	float height_blend_sharpness; // 1..50
	float height_blend_offset;    // -1..1
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
uint32_t app_layers_props_signature(void); // Changes with any app_layer_props_t field of any layer

const char *app_layer_blending_name(int32_t blending); // "Mix", ..., NULL past the last
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
	APP_LAYER_NEW_HEIGHT_MASK  = 11, // White mask with height blending
	APP_LAYER_NEW_FILL_MASK    = 12,
	APP_LAYER_NEW_ID_MASK      = 13, // An ID Selection effect in the layer's mask (a black one made first), picking its parts
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

// Properties. Each change is an undo step, except channels and the UV map (no history, as the layers tab)
bool app_layer_set_blending(int32_t id, int32_t blending);
bool app_layer_set_channel(int32_t id, int32_t channel, bool on);
bool app_layer_set_uv_map(int32_t id, int32_t uv_map);
bool app_layer_set_uv_type(int32_t id, int32_t uv_type);
bool app_layer_set_fill_material(int32_t id, int32_t material_index);
int32_t     app_layer_material_count(void);
const char *app_layer_material_name(int32_t index); // Project materials, for the fill material picker
bool app_layer_set_height_blend(int32_t id, bool on);
bool app_layer_set_height_blend_below(int32_t id, bool on);

// Slider drags: one undo step for the whole gesture. Begin, set any number of times, end.
// A set without a begin is its own undo step.
typedef enum {
	APP_LAYER_VALUE_SCALE                  = 0,
	APP_LAYER_VALUE_ANGLE                  = 1,
	APP_LAYER_VALUE_HEIGHT_BLEND_REACH     = 2,
	APP_LAYER_VALUE_HEIGHT_BLEND_INFLUENCE = 3,
	APP_LAYER_VALUE_HEIGHT_BLEND_SHARPNESS = 4,
	APP_LAYER_VALUE_HEIGHT_BLEND_OFFSET    = 5,
} app_layer_value_t;

void app_layer_value_begin(int32_t id, int32_t value);
bool app_layer_set_value(int32_t id, int32_t value, float v);
void app_layer_value_end(void);

// For scripts (tests/app_api_layers.c), Swift reads app_layer_props
bool    app_layer_expanded(int32_t id);
int32_t app_layer_uv_type(int32_t id);

// Mask effects: a mask is a stack of effects evaluated bottom to top (util_mask_stack.c). An effect is named by its
// mask's id and its index in the list as shown, 0 is the top. A mask nothing was added to is the stack of its one
// Paint effect, which brush strokes paint. Every edit is one undo step.

typedef enum {
	APP_MASK_EFFECT_PAINT        = 0,
	APP_MASK_EFFECT_FILL         = 1,
	APP_MASK_EFFECT_LEVELS       = 2,  // Filter
	APP_MASK_EFFECT_INVERT       = 3,  // Filter
	APP_MASK_EFFECT_EDGE_WEAR    = 4,  // Generators: computed from the mesh
	APP_MASK_EFFECT_DIRT         = 5,
	APP_MASK_EFFECT_POSITION     = 6,
	APP_MASK_EFFECT_DIRECTIONAL  = 7,
	APP_MASK_EFFECT_ID_SELECTION = 8,  // Parts of the ID map: params[0] their count, params[1..] their 0xrrggbb
	APP_MASK_EFFECT_NOISE        = 9,  // Generator
	APP_MASK_EFFECT_LIGHT        = 10, // Generator
	APP_MASK_EFFECT_THICKNESS    = 11, // Generator
	APP_MASK_EFFECT_HISTOGRAM    = 12, // Filter
	APP_MASK_EFFECT_BLUR         = 13, // Filter
	APP_MASK_EFFECT_ANCHOR       = 14, // Another layer's mask, height or opacity: params[0] its id, -1 none
} app_mask_effect_type_t;

#define APP_MASK_EFFECT_PARAMS    16
#define APP_MASK_NOISE_FIRST      12 // Generators: params from here on are their noise (scale, amount, seed, detail)
#define APP_MASK_ID_SELECTION_MAX 15 // Parts one ID Selection holds

typedef enum {
	APP_MASK_PARAM_SLIDER = 0,
	APP_MASK_PARAM_CHOICE = 1, // Whole values 0..count-1 of the comma separated options
	APP_MASK_PARAM_SEED   = 2, // A Shuffle button
	APP_MASK_PARAM_LAYER  = 3, // A layer id, -1 none: app_mask_anchor_layer_*
} app_mask_param_kind_t;

typedef struct {
	int32_t type; // app_mask_effect_type_t
	bool    visible;
	float   opacity;  // 0..1
	int32_t blending; // app_mask_blending_name
	float   params[APP_MASK_EFFECT_PARAMS];
	bool    is_paint;        // Brush strokes paint it, it can not be deleted
	bool    is_filter;       // Transforms the result below (Levels, Invert): no blending
	bool    is_generator;    // Computed from the mesh, with a noise block
	bool    needs_ao;        // Dirt and no AO map registered: cavity only
	bool    needs_thickness; // Thickness and no thickness map registered
	bool    needs_id_map;    // ID Selection and the project has no ID map
} app_mask_effect_t;

// One setting of an effect type, what the effect's settings show
typedef struct {
	const char *name;
	int32_t     index; // Into app_mask_effect_t.params
	int32_t     kind;  // app_mask_param_kind_t
	float       min;
	float       max;
	float       def;
	const char *options; // APP_MASK_PARAM_CHOICE: "X,Y,Z", "" otherwise
} app_mask_param_t;

int32_t     app_mask_effect_count(int32_t id); // 0 for anything but a mask
bool        app_mask_effect(int32_t id, int32_t index, app_mask_effect_t *out);
int32_t     app_mask_effect_type_count(void);
const char *app_mask_effect_type_name(int32_t type); // "Fill", "Edge Wear", ..., NULL past the last
bool        app_mask_effect_type_is_filter(int32_t type);    // The Add Effect menu: sources (neither), filters, generators
bool        app_mask_effect_type_is_generator(int32_t type);
int32_t     app_mask_effect_param_count(int32_t type);
bool        app_mask_effect_param(int32_t type, int32_t p, app_mask_param_t *out);
int32_t     app_mask_blending_count(void);
const char *app_mask_blending_name(int32_t blending); // "Normal", ..., NULL past the last

// An ID Selection starts picking its parts (app_mask_effect_pick_ids) when added
int32_t app_mask_add_effect(int32_t id, int32_t type);  // On top. Its index (0), -1: refused
int32_t app_layer_add_effect(int32_t id, int32_t type); // Layer, group or mask: into its mask, a black one made first. The mask id, -1: refused
bool    app_mask_remove_effect(int32_t id, int32_t index); // Not the Paint effect
bool    app_mask_move_effect(int32_t id, int32_t from, int32_t to);
bool    app_mask_set_effect_visible(int32_t id, int32_t index, bool on);
bool    app_mask_set_effect_blending(int32_t id, int32_t index, int32_t blending);
bool    app_mask_reset_effect(int32_t id, int32_t index); // Settings to their defaults

// Picks the parts of an ID Selection effect on the ID map, from the next frame: the engine's bar takes over (engine_modal)
// and asks to import an ID map when the project has none. Done sets the parts as one undo step, Cancel leaves them.
bool app_mask_effect_pick_ids(int32_t id, int32_t index);

// Layers an Anchor effect of mask id can reference, top first: paint and fill layers other than the mask's own
int32_t     app_mask_anchor_layer_count(int32_t id);
int32_t     app_mask_anchor_layer_id(int32_t id, int32_t i);   // -1 past the last
const char *app_mask_anchor_layer_name(int32_t id, int32_t i); // Valid until the next app_* call, copy it

// Slider drags, as app_layer_value_begin: field -1 is the opacity, otherwise a params index. Values are clamped to the
// setting's range, a layer setting takes -1 or an app_mask_anchor_layer_id. A set without a begin is its own undo step.
void app_mask_effect_value_begin(int32_t id, int32_t index, int32_t field);
bool app_mask_set_effect_value(int32_t id, int32_t index, int32_t field, float v);
void app_mask_effect_value_end(void);

// For scripts (tests/app_api_masks.c), Swift reads app_mask_effect
int32_t  app_mask_effect_type(int32_t id, int32_t index); // -1: none
bool     app_mask_effect_visible(int32_t id, int32_t index);
int32_t  app_mask_effect_blending(int32_t id, int32_t index);
float    app_mask_effect_value(int32_t id, int32_t index, int32_t field);
bool     app_mask_effect_needs_ao(int32_t id, int32_t index);
bool     app_mask_effect_needs_thickness(int32_t id, int32_t index);
bool     app_mask_effect_needs_id_map(int32_t id, int32_t index);
uint32_t app_layer_previews_version(void); // app_state_t.previews_version
