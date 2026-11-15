#pragma once

// The C interface the native (Swift) UI uses, imported through paint/native/Bridge/MaterialStudio-Bridging-Header.h.
// Self-contained on purpose: no engine headers, only plain functions and structs.
// Objects are named by id, never by pointer: Swift resolves an id on every call, so a deleted object is just "not found".
// Call from the main thread between frames (the frame driver's ticks), never from inside a frame.

#include <stdbool.h>
#include <stdint.h>

static inline int app_api_version(void) {
	return 1;
}

// Everything the native UI shows that changes often, read once per frame.
// The *_signature / *_version fields change whenever the matching list has to be read again.
typedef struct {
	bool     ready; // The engine has started, the other fields are valid
	int32_t  tool;  // tool_type_t
	float    brush_radius;
	float    brush_opacity;
	float    brush_hardness;
	int32_t  undos; // Steps available
	int32_t  redos;
	int32_t  selected_layer_id;
	uint32_t layers_signature; // Layer ids, order, names, visibility, opacity, selection
	uint32_t previews_version; // Layer thumbnails were redrawn
} app_state_t;

void app_get_state(app_state_t *out);

// The native UI draws the panels: the engine draws only its views (3D, 2D, nodes) and its popups
void app_set_native_chrome(bool on);

// Tool and brush
void app_set_tool(int32_t tool);
void app_set_brush_radius(float value);
void app_set_brush_opacity(float value);
void app_set_brush_hardness(float value);

// History
const char *app_undo_name(void); // Name of the step undo would revert, "" when none
void        app_undo(void);
void        app_redo(void);

// Layers, listed top of the stack first (the order the layers panel shows)
typedef enum {
	APP_LAYER_KIND_LAYER  = 0,
	APP_LAYER_KIND_GROUP  = 1,
	APP_LAYER_KIND_MASK   = 2,
	APP_LAYER_KIND_FILTER = 3,
} app_layer_kind_t;

typedef struct {
	int32_t     id;
	int32_t     parent_id; // -1 at the top level
	int32_t     depth;     // Nesting below groups and layers (masks sit one level below their layer)
	int32_t     kind;      // app_layer_kind_t
	bool        visible;
	bool        selected;
	bool        is_fill; // A fill layer (a material over the whole layer)
	float       opacity;
	const char *name; // Valid until the next app_* call, copy it
} app_layer_info_t;

int32_t app_layer_count(void);
bool    app_layer_info(int32_t index, app_layer_info_t *out);
bool    app_layer_select(int32_t id);
bool    app_layer_set_visible(int32_t id, bool visible);
bool    app_layer_rename(int32_t id, const char *name);
bool    app_layer_add(void);   // A paint layer above the selected one
bool    app_layer_delete(int32_t id);
bool    app_layer_can_delete(int32_t id);

// A slider drag: one undo step for the whole gesture. Begin, set any number of times, end.
void app_layer_opacity_begin(int32_t id);
bool app_layer_set_opacity(int32_t id, float value);
void app_edit_end(void);

// Thumbnail pixels, RGBA8 rows top to bottom. Returns the byte count written (0: none), sizes in w / h.
// With out NULL: reads the thumbnail back and returns the byte count it needs.
int32_t app_layer_thumbnail(int32_t id, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h);

// For scripts (tests/app_api.c)
int32_t app_layer_id_at(int32_t index);
int32_t app_layer_thumbnail_size(int32_t id);
