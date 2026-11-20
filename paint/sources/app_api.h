#pragma once

// The C interface the native (Swift) UI uses, imported through paint/native/Bridge/MaterialStudio-Bridging-Header.h.
// Self-contained on purpose: no engine headers, only plain functions and structs.
// Objects are named by id, never by pointer: Swift resolves an id on every call, so a deleted object is just "not found".
// Call from the main thread between frames (the frame driver's ticks), never from inside a frame.

#include <stdbool.h>
#include <stdint.h>

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
	uint32_t layer_props_signature; // Any app_layer_props_t field of any layer (app_api_layers.h)
	int32_t  view_mode;        // 0 3D, 1 2D alone, 2 both side by side (ui_base_view_mode)
	int32_t  viewport_mode;    // viewport_mode_t
	float    brush_angle;
	bool     sym_x;
	bool     sym_y;
	bool     sym_z;
	float    camera_rot[4]; // Quaternion x, y, z, w: the view cube turns with it
	bool     engine_modal;  // An engine drawn dialog, menu or full screen editor is up: the native controls step aside
	float    camera_fov;      // 0.3 to 1.4
	int32_t  camera_pivot;    // 0 cursor, 1 center
	int32_t  camera_controls; // 0 orbit, 1 rotate, 2 fly
	int32_t  camera_type;     // 0 perspective, 1 orthographic
	bool     projects_shown;     // iPad: the projects screen is up, the native UI shows it
	bool     projects_closable;  // It was opened from a project and can close back to it (app_projects_close)
	uint32_t projects_signature; // The recent projects list
	int32_t  finger_paint;       // 0 auto (fingers paint until the pencil is used), 1 always, 2 never
	bool     curve_editing;      // A stroke held still became a curve: its points (app_curve_point) and Confirm show
	int32_t  curve_point_count;
	float    curve_width;        // The whole curve's, 0.05 to 10 (app_curve_set_width)
} app_state_t;

void app_get_state(app_state_t *out);

// The native UI draws the panels: the engine draws only its views (3D, 2D, nodes) and its popups

// Height of the native controls floating over the top of the canvas, in canvas pixels: the node editor's menubar goes
// below them
void app_set_overlay_top_inset(int32_t pixels);

// Tool and brush. The tools the native UI offers, values of tool_type_t
typedef enum {
	APP_TOOL_BRUSH    = 0,
	APP_TOOL_ERASER   = 1,
	APP_TOOL_FILL     = 2,
	APP_TOOL_DECAL    = 3,
	APP_TOOL_TEXT     = 4,
	APP_TOOL_CLONE    = 5,
	APP_TOOL_BLUR     = 6,
	APP_TOOL_PARTICLE = 7,
	APP_TOOL_PICKER   = 9,
} app_tool_t;

void app_set_tool(int32_t tool);
void app_set_brush_radius(float value);
void app_set_brush_opacity(float value);
void app_set_brush_hardness(float value);

void app_set_brush_angle(float degrees);
void app_set_symmetry(bool x, bool y, bool z);

// Views and camera. Viewport modes, values of viewport_mode_t
typedef enum {
	APP_VIEWPORT_LIT           = 0,
	APP_VIEWPORT_BASE_COLOR    = 1,
	APP_VIEWPORT_NORMAL_MAP    = 2,
	APP_VIEWPORT_OCCLUSION     = 3,
	APP_VIEWPORT_ROUGHNESS     = 4,
	APP_VIEWPORT_METALLIC      = 5,
	APP_VIEWPORT_OPACITY       = 6,
	APP_VIEWPORT_HEIGHT        = 7,
	APP_VIEWPORT_EMISSION      = 8,
	APP_VIEWPORT_SUBSURFACE    = 9,
	APP_VIEWPORT_TEXCOORD      = 10,
	APP_VIEWPORT_OBJECT_NORMAL = 11,
	APP_VIEWPORT_WORLD_NORMAL  = 12,
	APP_VIEWPORT_MATERIAL_ID   = 13,
	APP_VIEWPORT_OBJECT_ID     = 14,
	APP_VIEWPORT_MASK          = 15,
	APP_VIEWPORT_PATH_TRACE    = 16,
} app_viewport_mode_t;

void app_wake(void); // Keeps the engine's frames running: work an action queued for the next frame happens, not after the next touch
void app_cycle_view_mode(void);       // 3D, 2D alone, both side by side
void app_set_viewport_mode(int32_t mode); // app_viewport_mode_t
void app_view_face(int32_t face);     // 0 right, 1 left, 2 back, 3 front, 4 top, 5 bottom
void app_view_orbit(float x, float y); // Turns the camera by these angles (radians) around its own up and right axes, the view cube drag
void app_view_roll(float radians, bool held); // Rolls the view clockwise on screen as a two finger twist, held: a finger is still down
void app_set_camera_fov(float fov);    // The view keeps its framing: the camera moves to make up for it
void app_set_camera_pivot(int32_t pivot);
void app_set_camera_controls(int32_t controls);
void app_set_camera_type(int32_t type);
void app_set_finger_paint(int32_t mode); // A setting, saved
void app_set_brush_preview(bool on);     // The brush circle in the middle of the view, while a native slider changes its size

// Menu commands, run like the old menus run them (some open the engine's dialogs)
typedef enum {
	APP_COMMAND_OPEN,
	APP_COMMAND_OPEN_RECENT,
	APP_COMMAND_SAVE,
	APP_COMMAND_SAVE_AS,
	APP_COMMAND_IMPORT_TEXTURE,
	APP_COMMAND_IMPORT_FONT,
	APP_COMMAND_IMPORT_MATERIAL,
	APP_COMMAND_IMPORT_BRUSH,
	APP_COMMAND_IMPORT_SWATCHES,
	APP_COMMAND_IMPORT_MESH,
	APP_COMMAND_REIMPORT_MESH,
	APP_COMMAND_REIMPORT_TEXTURES,
	APP_COMMAND_EXPORT_MESH,
	APP_COMMAND_EXPORT_SWATCHES,
	APP_COMMAND_BAKE_MATERIAL,
	APP_COMMAND_VIEW_RESET,
	APP_COMMAND_VIEW_FRAME,
	APP_COMMAND_ZOOM_IN,
	APP_COMMAND_ZOOM_OUT,
	APP_COMMAND_ORBIT_OPPOSITE,
	APP_COMMAND_SCREENSHOT,
	APP_COMMAND_DELETE_UNUSED,
} app_command_t;

void app_command(int32_t command);

// Projects screen (iPad), the recent projects, most recent first
int32_t     app_project_count(void);
const char *app_project_path(int32_t index); // Full path of the .arm file, valid until the next app_* call
const char *app_project_icon_path(int32_t index); // Its preview, hidden next to it (project_icon_path)
void        app_project_open(int32_t index); // Closes the screen, the project loads over the next frames
void        app_project_delete(int32_t index); // With its files
void        app_project_new(void);           // Closes the screen
void        app_projects_close(void);        // Back to the open project
const char *app_project_entry(int32_t index); // Its entry in the recent list: on iOS the path relative to Documents, kept
                                             // across installs (the projects screen's folders are keyed by it)
const char *app_project_rename(int32_t index, const char *name); // Its folder and files, the open project follows.
                                                                // Returns the new entry, "" when it could not be renamed
const char *app_project_duplicate(int32_t index); // A copy in a folder of its own ("<name> copy"), first in the list. Returns
                                                  // its entry, "" when it could not be made

// New project templates: the meshes and shapes a new project starts on
int32_t     app_template_count(void);
const char *app_template_id(int32_t index);              // cube_bevel, sphere, ...
int32_t     app_template_selected(void);                 // The one the last new project used
void        app_project_new_from_template(int32_t index); // Closes the projects screen
bool        app_take_new_project_request(void);           // Once true after the engine asked for the new project sheet
void        app_project_import(void);        // Picks a model, it opens in a new project and the screen closes

// History
const char *app_undo_name(void); // Name of the step undo would revert, "" when none
const char *app_redo_name(void); // Name of the step redo would apply, "" when none

// The last undo or redo the user asked for, from any source (buttons, shortcuts, gestures, the quick menu): a new
// serial is a new notice. Serial 0: none yet. name is the step's, valid until the next app_* call.
typedef struct {
	uint32_t    serial;
	bool        redo;
	const char *name;
} app_history_notice_t;
app_history_notice_t app_history_notice(void);
void        app_undo(void); // Returns at once: layers to restore from history follow over the next frames
void        app_redo(void);
bool        app_history_busy(void); // Undo or redo still restores layers, the counts are already the new ones

// A brush stroke held still for a second at its end becomes a curve (util_curve_stroke.c): its points can be dragged,
// Confirm (or a new touch on the canvas) paints it as a stroke, undo drops it. Positions in canvas pixels.
bool app_curve_point(int32_t index, float *x, float *y); // Through the current camera, false behind it or off the view
void app_curve_move(int32_t index, float x, float y);    // To the surface under it, off the mesh it stays
bool app_curve_remove(int32_t index);                    // Not the first or the last; a tap on the curve adds one (engine side)
float app_curve_point_size(int32_t index);               // Width of the stroke at a point, 0 to 10, 1 the brush's
void app_curve_set_point_size(int32_t index, float size); // Clamped
void app_curve_set_width(float width);                   // Of the whole curve, clamped: the points keep their proportions
void app_curve_confirm(void);
void app_curve_cancel(void); // Drops it: nothing painted, the stroke it came from stays taken back (as undo)

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
	bool        is_fill; // A fill layer or fill mask (sources per channel, app_layer_fill)
	float       opacity;
	const char *name; // Valid until the next app_* call, copy it
} app_layer_info_t;

int32_t app_layer_count(void);
bool    app_layer_info(int32_t index, app_layer_info_t *out);
bool    app_layer_set_visible(int32_t id, bool visible);
bool    app_layer_delete(int32_t id);

// A slider drag: one undo step for the whole gesture. Begin, set any number of times, end.
// A set without a begin is its own undo step. app_drag_end ends any drag (opacity, layer values, mask effect values).
void app_layer_opacity_begin(int32_t id);
bool app_layer_set_opacity(int32_t id, float value);
// Height raise of a layer that paints height, -1..1: height added where it has paint, times its coverage, so its strokes
// bulge from the surface, or sink into it below 0 (slot_layer_t.height_raise)
void app_layer_raise_begin(int32_t id);
bool app_layer_set_raise(int32_t id, float value);
void app_drag_end(void);

// Thumbnail pixels, RGBA8 rows top to bottom. Returns the byte count written (0: none), sizes in w / h.
// With out NULL: the byte count it needs, nothing read back.
int32_t app_layer_thumbnail(int32_t id, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h);
