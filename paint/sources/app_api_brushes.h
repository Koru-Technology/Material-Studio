#pragma once

// app_api.h, brushes and color: what the native Brushes and Color popovers need. Self-contained, imported by Swift.
// Brushes and groups are named by id, swatches by index. Call between frames on the main thread, as app_api.h.

#include <stdbool.h>
#include <stdint.h>

// Read once per frame: each signature changes when the matching list has to be read again
typedef struct {
	uint32_t library_signature;  // Groups, brushes, names, order, the picked brush, the recent brushes
	uint32_t previews_signature; // A stroke preview row arrived or changed, see app_brush_info_t preview_key
	uint32_t color_signature;    // The picked color, the swatches, whether the color applies to the material
	int32_t  selected_brush_id;
} app_brushes_state_t;

void app_brushes_get_state(app_brushes_state_t *out);

// Brush groups, in library order. Built-in groups come first and can not be renamed or deleted.
typedef struct {
	int32_t     id;
	bool        builtin;
	bool        hidden; // A built-in group the settings hide (hide_builtin_brushes)
	const char *name;   // Valid until the next app_* call, copy it
} app_brush_group_info_t;

int32_t app_brush_group_count(void);
bool    app_brush_group_info(int32_t index, app_brush_group_info_t *out);
int32_t app_brush_group_add(const char *name); // Id of the new group, -1 when none
bool    app_brush_group_rename(int32_t id, const char *name);
bool    app_brush_group_delete(int32_t id); // A group of your own and its brushes

// Brushes, in library order (a group's brushes are the ones whose group_id is the group's id)
typedef struct {
	int32_t     id;
	int32_t     group_id;
	bool        builtin;  // Can not be edited, deleted or moved, a duplicate can
	bool        selected; // The brush painted with
	bool        preview_ready;
	uint32_t    preview_key; // Changes whenever the stroke preview pixels change
	const char *name;        // Valid until the next app_* call, copy it
} app_brush_info_t;

int32_t app_brush_count(void);
bool    app_brush_info(int32_t index, app_brush_info_t *out);
int32_t app_brush_id_at(int32_t index);
bool    app_brush_use(int32_t id);         // Paint with it, selects the brush tool unless the eraser is
int32_t app_brush_duplicate(int32_t id);   // An editable copy (in a group of your own for a built-in brush), its id or -1
bool    app_brush_delete(int32_t id);      // A brush of your own
bool    app_brush_move(int32_t id, int32_t group_id); // A brush of your own to a group of your own
bool    app_brush_rename(int32_t id, const char *name);
void    app_brush_new(int32_t group_id);   // Opens the Brush Creator on a new brush, saved to the group (a group of your own)
bool    app_brush_edit(int32_t id);        // Opens the Brush Creator on a brush of your own
bool    app_brush_creator_shown(void);
int32_t app_brush_recent(int32_t index);   // Brushes painted with lately, the latest first, index 0..2, -1 when none

// For scripts (tests/app_api_brushes.c): "" / -1 when not found
int32_t     app_brush_group_of(int32_t id);
const char *app_brush_name(int32_t id);
const char *app_brush_group_name(int32_t id);
bool        app_brush_group_builtin(int32_t id);

// Stroke previews: render one per frame in the engine, the group asked for first. preview_key tells when to read again.
void app_brush_previews_request(int32_t group_id);
// RGBA8 rows top to bottom (straight alpha, white stroke). Returns the byte count written (0: not ready), sizes in w / h.
// With out NULL: returns the byte count it needs.
int32_t app_brush_preview(int32_t id, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h);
int32_t app_brush_preview_size(int32_t id);

// Color: the picked color is what the color picker tool picks and what the popover edits. Applied, it becomes the color of
// the selected material when that material is a plain color (a single PBR output node), as the swatches and picker do.
typedef enum {
	APP_COLOR_OPACITY   = 0,
	APP_COLOR_OCCLUSION = 1,
	APP_COLOR_ROUGHNESS = 2,
	APP_COLOR_METALLIC  = 3,
	APP_COLOR_HEIGHT    = 4,
} app_color_value_t;

typedef struct {
	uint32_t base; // 0xAARRGGBB, alpha always 0xff, see opacity
	float    opacity;
	float    occlusion;
	float    roughness;
	float    metallic;
	float    height;
} app_color_t;

bool     app_color_get(app_color_t *out);
uint32_t app_color_base(void);
float    app_color_value(int32_t which); // app_color_value_t
void     app_color_set_base(uint32_t rgb);  // 0xRRGGBB, the alpha is ignored
void     app_color_set_value(int32_t which, float value);
bool     app_color_can_apply(void); // The selected material takes the picked color
void     app_color_apply(void);     // The picked color to the selected material, on the next frame (after a drag ends)
void     app_color_new_material(void); // A new plain material of the picked color, on the next frame

// Swatches of the project
int32_t  app_swatch_count(void);
bool     app_swatch_info(int32_t index, app_color_t *out);
uint32_t app_swatch_base(int32_t index);
int32_t  app_swatch_selected(void); // Index of the swatch picked last, -1 when none
bool     app_swatch_pick(int32_t index); // Becomes the picked color, applied to the material
int32_t  app_swatch_add(void);           // The picked color as a new swatch, its index
bool     app_swatch_delete(int32_t index); // The last swatch stays
