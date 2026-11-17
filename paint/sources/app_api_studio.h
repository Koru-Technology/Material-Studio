#pragma once

// app_api.h, the Material Studio: what the native editor shows and edits. Self-contained, imported by Swift.
// The engine draws the background and the live preview, in the rectangle the native editor leaves for it, and turns
// the preview with a drag on it. Two modes: a studio graph (presets, layers, pattern, colors, surface, mask) or the
// Variables of any other material (Material Maker materials and their controls).
// Indices are valid until the signature changes. Controls are found again by key, a drag keeps its key while what
// shows around it changes.

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool        open;          // The studio is up: the native editor shows
	int32_t     material_id;
	bool        variables;     // Variables mode
	uint32_t    signature;     // Anything the editor shows changed: read it again
	uint32_t    tiles_version; // The layer tiles were drawn again
	bool        sphere;        // The preview shows the sphere, else the cube
	const char *name;          // The material's name, valid until the next app_* call
} app_studio_state_t;

void app_studio_get_state(app_studio_state_t *out);

// Window pixels of the canvas, the engine draws the preview there
void app_studio_set_preview_rect(float x, float y, float w, float h);
void app_studio_toggle_sphere(void);
void app_studio_save(void);       // Closes, the edits stay in the project's material
void app_studio_cancel(void);     // Closes, the material goes back to how it was when the studio opened (undoable)
void app_studio_edit_nodes(void); // Closes, the node editor opens on the material

// Undo: begin when a slider or a drag starts, end when it lets go, the edits between are one step.
// A change outside of begin and end is a step of its own.
void app_studio_edit_begin(void);
void app_studio_edit_end(void);

// Studio graph mode

int32_t     app_studio_preset_count(void);
const char *app_studio_preset_name(int32_t i);
void        app_studio_apply_preset(int32_t i);

// Layers, 0 is the base at the bottom. The edits below apply to the selected layer.
typedef struct {
	const char *name;
	const char *subtitle; // Its pattern, and where it shows
	bool        selected;
} app_studio_layer_t;

int32_t     app_studio_layer_count(void);
int32_t     app_studio_layer_max(void);
bool        app_studio_layer(int32_t k, app_studio_layer_t *out);
void        app_studio_layer_select(int32_t k);
int32_t     app_studio_layer_preset_count(void);
const char *app_studio_layer_preset_name(int32_t i);
int32_t     app_studio_layer_add(int32_t preset); // -1 an empty layer. Returns the new layer, selected, or -1
void        app_studio_layer_remove(int32_t k);   // Not the base
void        app_studio_layer_move(int32_t k, int32_t to); // The base stays at 0
// The layer's colors: its ramp over its pattern, RGBA8 as app_material_thumbnail. 0 while not drawn yet.
int32_t app_studio_layer_thumbnail(int32_t k, int32_t max_size, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h);

// Patterns: 0 none (a plain layer), then the pattern nodes
int32_t     app_studio_pattern_count(void);
const char *app_studio_pattern_name(int32_t i);
int32_t     app_studio_pattern_current(void);
void        app_studio_pattern_set(int32_t i);
// The pattern at its defaults. 0 while the tiles are not made yet (the studio makes them once it shows).
int32_t app_studio_pattern_thumbnail(int32_t i, int32_t max_size, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h);

// Colors of the selected layer: its ramp, from the recesses to the surface. Stops by position, each with its index in
// the ramp, which stays while the stop is dragged past another.
typedef struct {
	int32_t index;
	float   r, g, b;
	float   pos;
	bool    selected;
} app_studio_stop_t;

int32_t app_studio_ramp_count(void);
int32_t app_studio_ramp_max(void);
bool    app_studio_ramp_stop(int32_t i, app_studio_stop_t *out);
bool    app_studio_ramp_plain(void); // A plain layer: one color, the stop at the surface
bool    app_studio_ramp_hard(void);  // Hard steps between the stops, else smooth
void    app_studio_ramp_set_hard(bool hard);
void    app_studio_ramp_select(int32_t index);
void    app_studio_ramp_set_color(int32_t index, float r, float g, float b);
void    app_studio_ramp_set_pos(int32_t index, float pos);
void    app_studio_ramp_add(void);    // Half way from the selected stop to the next one, selected
void    app_studio_ramp_remove(void); // The selected stop

// Controls of both modes, in order, by section: sliders, switches, choices, buttons, colors and gradients

typedef enum {
	APP_STUDIO_SLIDER   = 0,
	APP_STUDIO_TOGGLE   = 1,
	APP_STUDIO_CHOICE   = 2, // options, one per line
	APP_STUDIO_BUTTON   = 3, // app_studio_control_press
	APP_STUDIO_COLOR    = 4,
	APP_STUDIO_GRADIENT = 5, // stops, app_studio_control_stop
} app_studio_control_kind_t;

typedef enum { // Graph mode sections, the editor adds its own views to some. Variables mode: 100 + the group.
	APP_STUDIO_SECTION_PATTERN = 0,
	APP_STUDIO_SECTION_COLORS  = 1,
	APP_STUDIO_SECTION_SURFACE = 2,
	APP_STUDIO_SECTION_MASK    = 3,
	APP_STUDIO_SECTION_CHANGES = 4,
} app_studio_section_t;

typedef struct {
	uint32_t    key;
	int32_t     kind;    // app_studio_control_kind_t
	int32_t     section; // app_studio_section_t, or 100 + the group of a Variable
	const char *section_name;
	const char *name;
	float       min, max;
	float       step;    // 0 any value, 1 whole numbers
	bool        percent; // Shown 0 to 100
	float       value;   // Slider, toggle (0, 1), choice (the index)
	float       color[3];
	const char *options; // Choice labels, one per line
	int32_t     stops;   // Gradient
} app_studio_control_t;

int32_t app_studio_control_count(void);
bool    app_studio_control(int32_t i, app_studio_control_t *out);
void    app_studio_control_set(uint32_t key, float value);
void    app_studio_control_set_color(uint32_t key, float r, float g, float b);
void    app_studio_control_press(uint32_t key);
bool    app_studio_control_stop(uint32_t key, int32_t i, float *rgb_pos); // Gradient stop i by position: r, g, b, pos
void    app_studio_control_set_stop(uint32_t key, int32_t i, float r, float g, float b);
