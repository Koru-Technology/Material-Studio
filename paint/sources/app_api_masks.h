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
	APP_MASK_EFFECT_FILL         = 1,  // A value, or an image or grunge on the mesh: params[1] its source (0, 1, 2)
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
	APP_MASK_EFFECT_ANCHOR       = 14, // Another layer's mask, height, opacity or occlusion: params[0] its id, -1 none
	APP_MASK_EFFECT_MASK_BUILDER = 15, // Generator: curvature, AO, world normal and position of the model combined, and of
	                                   // a layer's material when one is picked (params[11], a layer id)
	APP_MASK_EFFECT_AO           = 16, // Generator
	APP_MASK_EFFECT_HEIGHT_BLEND = 17, // Filter: the edge moves with the layer's height against a level, the layers below or a layer
	APP_MASK_EFFECT_EDGE_BREAKUP = 18, // Filter: the edge of the result below frays with noise
	APP_MASK_EFFECT_GROW         = 19, // Filter: the result below grown or shrunk by a distance, hard edged
} app_mask_effect_type_t;

#define APP_MASK_EFFECT_PARAMS    16
#define APP_MASK_NOISE_FIRST      12 // Generators: params from here on are their noise (scale, amount, seed, detail)
#define APP_MASK_ID_SELECTION_MAX 15 // Parts one ID Selection holds

typedef enum {
	APP_MASK_PARAM_SLIDER = 0,
	APP_MASK_PARAM_CHOICE = 1, // Whole values 0..count-1 of the comma separated options
	APP_MASK_PARAM_SEED   = 2, // A Shuffle button
	APP_MASK_PARAM_LAYER  = 3, // A layer id, -1 none (named by options, "None" when empty): app_mask_anchor_layer_*
	APP_MASK_PARAM_IMAGE  = 4, // An image of the project, -1 none: app_mask_image_*
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
	bool    needs_ao;        // Dirt, Ambient Occlusion or a Mask Builder weighing it, and no AO map registered
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
	const char *options;    // APP_MASK_PARAM_CHOICE: "X,Y,Z", "" otherwise
	int32_t     shown_by;   // The choice setting (a params index) it depends on, when shown_when is not 0
	uint32_t    shown_when; // Bit v set: shown while params[shown_by] is v. 0: always shown
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
int32_t app_mask_insert_effect(int32_t id, int32_t type, int32_t index); // At index (0 the top), -1: refused
int32_t app_layer_add_effect(int32_t id, int32_t type); // Layer, group or mask: into its mask, a black one made first. The mask id, -1: refused
bool    app_mask_remove_effect(int32_t id, int32_t index); // Not the Paint effect
bool    app_mask_move_effect(int32_t id, int32_t from, int32_t to);
bool    app_mask_set_effect_visible(int32_t id, int32_t index, bool on);
bool    app_mask_set_effect_blending(int32_t id, int32_t index, int32_t blending);
bool    app_mask_reset_effect(int32_t id, int32_t index); // Settings to their defaults

// Picks the parts of an ID Selection effect on the ID map, from the next frame (asks to import an ID map when the project
// has none). The native UI lists the ID map's parts beside the canvas to pick from, a tap on the model picks the part
// under it as well. Confirm sets the parts as one undo step, Cancel leaves them.
bool app_mask_effect_pick_ids(int32_t id, int32_t index);

typedef struct {
	bool     active;  // Picking
	int32_t  parts;   // The ID map's parts, largest first: app_id_part_*
	int32_t  picked;
	int32_t  max;     // Parts one ID Selection holds
	uint32_t version; // Changes with the parts and the picked ones
} app_id_pick_t;

void        app_id_pick(app_id_pick_t *out);
uint32_t    app_id_part_color(int32_t i); // 0xrrggbb
const char *app_id_part_name(int32_t i);  // The bake's name of it (the high poly object), "Part n" for an imported map
bool        app_id_part_picked(int32_t i);
void        app_id_part_toggle(int32_t i);
void        app_id_pick_confirm(void);
void        app_id_pick_cancel(void);

// Layers an Anchor or a Mask Builder of mask id can reference, top first: paint and fill layers other than
// the mask's own
int32_t     app_mask_anchor_layer_count(int32_t id);
int32_t     app_mask_anchor_layer_id(int32_t id, int32_t i);   // -1 past the last
const char *app_mask_anchor_layer_name(int32_t id, int32_t i); // Valid until the next app_* call, copy it

// Images a fill's Image setting can take: the project's, its value is the index here
int32_t     app_mask_image_count(void);
const char *app_mask_image_name(int32_t i); // Valid until the next app_* call, copy it

// Slider drags, as app_layer_value_begin: field -1 is the opacity, otherwise a params index. Values are clamped to the
// setting's range, a layer setting takes -1 or an app_mask_anchor_layer_id, an image setting -1 or an image index. A set without a begin is its own undo step.
void app_mask_effect_value_begin(int32_t id, int32_t index, int32_t field);
bool app_mask_set_effect_value(int32_t id, int32_t index, int32_t field, float v); // app_drag_end ends the drag
