#pragma once

// app_api.h, materials: what the native materials popover needs. Self-contained, imported by Swift.
// The project's materials (by id), the Library (saved materials, the materials folder, Material Maker's library, or the
// cloud catalogs) and the smart materials (by index). Indices are valid until the matching signature changes.
// Thumbnails are RGBA8 rows top to bottom, premultiplied alpha, as app_layer_thumbnail: with out NULL the byte count needed.

#include <stdbool.h>
#include <stdint.h>

// Read once per frame. Material Maker's library and the cloud are only opened once the Library is shown
// (app_library_signature), the smart materials once app_smart_signature is called.
typedef struct {
	bool     ready;               // The engine has started, the other fields are valid
	int32_t  selected_id;         // The material painting uses, -1 none
	uint32_t materials_signature; // Ids, order, names, selection, preview readiness
	uint32_t previews_version;    // A material preview was redrawn: re-read those whose preview_version changed
	bool     smart_can_save;      // The selected layer is a fill layer or a group, or the mask of a fill layer
	bool     can_apply_to_fill;   // The selected layer is a fill layer (or its mask): Apply to Fill Layer
	bool     studio_open;         // The Material Studio is shown
} app_materials_state_t;

void app_materials_get_state(app_materials_state_t *out);

// Project materials, in the project's order

typedef struct {
	int32_t     id;
	uint32_t    preview_version; // Changes whenever its preview may have been redrawn, unique among all materials
	bool        selected;
	bool        preview_ready; // False while its preview is not rendered yet
	bool        can_open_studio; // A Material Studio material, or one with controls
	const char *name;            // Valid until the next app_* call, copy it
} app_material_info_t;

int32_t app_material_count(void);
bool    app_material_info(int32_t index, app_material_info_t *out);
int32_t app_material_id_at(int32_t index);
bool    app_material_select(int32_t id);
int32_t app_material_new(void);        // A node material, selected. Returns its id, -1 when none
void    app_material_new_studio(void); // A Material Studio material, opened in the studio on the next frame
int32_t app_material_duplicate(int32_t id); // Returns the copy's id, selected
bool    app_material_can_delete(int32_t id); // Not the project's last material
bool    app_material_delete(int32_t id);
int32_t app_material_delete_unused(void); // Materials no layer or other material uses, returns how many went
bool    app_material_rename(int32_t id, const char *name);

// Editing, on the next frame. Edit: the Material Studio when it can open the material, the node editor (with Save to
// Library and Cancel) otherwise, as the old tab's Edit. Show nodes: the node editor on it, as the old tab's Nodes button.
bool app_material_edit(int32_t id);
bool app_material_open_studio(int32_t id);
bool app_material_show_nodes(int32_t id);
bool app_material_to_fill_layer(int32_t id);    // A new fill layer of it
bool app_material_apply_to_fill(int32_t id);    // The selected fill layer takes it (state.can_apply_to_fill)

// Its preview sphere, box filtered down to max_size or less (0: full size, 256)
int32_t app_material_thumbnail(int32_t id, int32_t max_size, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h);
int32_t app_material_preview_version(int32_t id); // For scripts

// Library: the local one (saved materials, the materials folder, Material Maker's library) or the cloud catalogs.
// Ordered by section (style, then type): items of a section are next to each other.

typedef enum {
	APP_LIBRARY_SAVED  = 0, // Saved from the Material Studio
	APP_LIBRARY_FOLDER = 1, // The materials folder ("My Materials")
	APP_LIBRARY_MM     = 2, // Material Maker's library
	APP_LIBRARY_CLOUD  = 3, // Poly Haven, ambientCG, FreeStylized
} app_library_source_t;

typedef struct {
	int32_t     source;  // app_library_source_t
	uint32_t    key;     // Stable while the item does not change: caches its icon
	int32_t     section; // Same for the items of a section, see section_name
	int32_t     group;   // Type of Material Maker's library or of the cloud, -1 for the others (no style either)
	bool        stylized;
	bool        busy;       // Cloud: downloading or waiting to
	bool        downloaded; // Cloud: its textures are on the device
	bool        can_delete; // Saved materials
	float       progress;   // Cloud download, 0 to 1
	const char *name;         // These are valid until the next app_* call, copy them
	const char *section_name; // "Saved", "My Materials", "Stylized Wood", ...
	const char *group_name;   // "Wood", "" without a group
	const char *detail;       // Credit, folder or source
} app_library_item_t;

// Changes when the items change. Opens the library (or the cloud catalog) on first call, call it while the Library shows.
uint32_t    app_library_signature(void);
bool        app_library_cloud(void);
void        app_library_set_cloud(bool on);
int32_t     app_library_count(void);
bool        app_library_item(int32_t index, app_library_item_t *out);
bool        app_library_matches(int32_t index, const char *search); // Name, tags, author, style; any case
int32_t     app_library_group_count(void);                          // Types of the current view, for the type menu
const char *app_library_group_name(int32_t group);
bool        app_library_use(int32_t index); // Adds it to the project (downloads a cloud item first), selected
bool        app_library_delete(int32_t index); // Saved materials only
const char *app_library_status(void);          // Importing progress, folder state, or the cloud catalog state; "" for none
bool        app_library_has_folder(void);      // The local library has a materials folder
void        app_library_pick_folder(void);
int32_t     app_library_busy(void); // Cloud items downloading or waiting to

// Its icon, requested when it is not loaded yet: 0 until it is (ask again on a later frame), -1 when it has none
int32_t app_library_icon(int32_t index, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h);

// Smart materials: fill layers with their masks or groups of layers, saved to reuse

typedef struct {
	uint32_t    key; // Name and icon
	const char *name;
} app_smart_info_t;

uint32_t    app_smart_signature(void);
int32_t     app_smart_count(void);
bool        app_smart_info(int32_t index, app_smart_info_t *out);
int32_t     app_smart_find(const char *name);
int32_t     app_smart_icon(int32_t index, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h);
bool        app_smart_apply(int32_t index); // On top of the layers, its top layer selected (next frame), undoable
bool        app_smart_delete(int32_t index);
const char *app_smart_default_name(void);   // For the save field: the selected fill layer's material or group name
bool        app_smart_save(const char *name); // The selected fill layer or group (next frame), one of that name is replaced

// For scripts (tests/app_api_materials.c)
int32_t app_material_thumbnail_size(int32_t id, int32_t max_size);
int32_t app_library_icon_size(int32_t index);
int32_t app_library_find(const char *name); // First item of that name, -1 when none
int32_t app_smart_icon_size(int32_t index);
