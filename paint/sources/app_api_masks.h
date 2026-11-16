#pragma once

// app_api.h, mask effects: what the native layers panel needs for a mask's settings. Self-contained, imported by Swift.
// Masks are named by their layer id (app_layer_info_t.id), the rest of app_api.h's rules apply.

#include <stdbool.h>
#include <stdint.h>

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
const char *app_mask_effect_type_name(int32_t type); // "Fill", "Edge Wear", ..., "" past the last
bool        app_mask_effect_type_is_filter(int32_t type);    // The Add Effect menu: sources (neither), filters, generators
bool        app_mask_effect_type_is_generator(int32_t type);
int32_t     app_mask_effect_param_count(int32_t type);
bool        app_mask_effect_param(int32_t type, int32_t p, app_mask_param_t *out);
int32_t     app_mask_blending_count(void);
const char *app_mask_blending_name(int32_t blending); // "Normal", ..., "" past the last

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
bool app_mask_set_effect_value(int32_t id, int32_t index, int32_t field, float v); // app_drag_end ends the drag
