// Saved materials: materials saved to the Library with "Save to Library" in the Material Studio, to reuse in any
// project. saved_materials/<name>.arm in the app's save folder is the material, <name>.png its preview.

#include "global.h"

#define SAVED_MATERIALS_MAX 512

typedef struct saved_material_entry {
	char          *name;
	gpu_texture_t *icon;
	bool           icon_tried;
} saved_material_entry_t;

static saved_material_entry_t saved_materials[SAVED_MATERIALS_MAX];
static i32                    saved_materials_n       = 0;
static bool                   saved_materials_scanned = false;

static char *saved_materials_dir() {
	return string("%ssaved_materials", iron_internal_save_path());
}

static char *saved_materials_path(char *name, char *suffix) {
	return string("%s%s%s%s", saved_materials_dir(), PATH_SEP, name, suffix);
}

void saved_materials_refresh() {
	for (i32 i = 0; i < saved_materials_n; ++i) {
		saved_materials[i].icon       = NULL; // Cached by data_get_texture
		saved_materials[i].icon_tried = false;
	}
	saved_materials_n       = 0;
	saved_materials_scanned = true;
	char *dir               = saved_materials_dir();
	if (!iron_is_directory(dir)) {
		return;
	}
	char           *files = string_copy(iron_read_directory(dir));
	string_array_t *list  = string_split(files, "\n");
	for (i32 i = 0; i < list->length && saved_materials_n < SAVED_MATERIALS_MAX; ++i) {
		char *f = list->buffer[i];
		if (!ends_with(f, ".arm")) {
			continue;
		}
		saved_material_entry_t *e = &saved_materials[saved_materials_n++];
		e->name                   = string_copy(substring(f, 0, string_length(f) - 4));
		e->icon                   = NULL;
		e->icon_tried             = false;
	}
	// Sorted by name
	for (i32 i = 1; i < saved_materials_n; ++i) {
		for (i32 j = i; j > 0 && strcmp(saved_materials[j - 1].name, saved_materials[j].name) > 0; --j) {
			saved_material_entry_t t = saved_materials[j];
			saved_materials[j]       = saved_materials[j - 1];
			saved_materials[j - 1]   = t;
		}
	}
}

i32 saved_materials_count() {
	if (!saved_materials_scanned) {
		saved_materials_refresh();
	}
	return saved_materials_n;
}

char *saved_materials_name(i32 i) {
	return i >= 0 && i < saved_materials_count() ? saved_materials[i].name : NULL;
}

gpu_texture_t *saved_materials_icon(i32 i) {
	if (i < 0 || i >= saved_materials_count()) {
		return NULL;
	}
	saved_material_entry_t *e = &saved_materials[i];
	if (!e->icon_tried) {
		e->icon_tried = true;
		char *path    = saved_materials_path(e->name, ".png");
		if (iron_file_exists(path)) {
			e->icon = data_get_texture(path);
		}
	}
	return e->icon;
}

// Saves m to the library under its name, one of that name is replaced
bool saved_materials_save(slot_material_t *m) {
	if (m == NULL || m->canvas == NULL) {
		return false;
	}
	char *name = smart_materials_clean(m->canvas->name);
	if (name[0] == '\0') {
		name = "Material";
	}
	iron_create_directory(saved_materials_dir());

	char *arm = saved_materials_path(name, ".arm");
	if (iron_file_exists(arm)) {
		data_delete_blob(arm); // Replaced, the cached one goes
	}
	slot_material_t *_material = g_context->material;
	g_context->material        = m;
	export_arm_run_material(arm);
	g_context->material = _material;

	char *icon = saved_materials_path(name, ".png");
	if (iron_file_exists(icon)) {
		data_delete_texture(icon);
	}
	smart_materials_write_icon(m, icon);
	saved_materials_refresh();
	return true;
}

// Adds saved material i to the project, the import selects it
void saved_materials_use(i32 i) {
	char *name = saved_materials_name(i);
	if (name == NULL) {
		return;
	}
	char *arm = saved_materials_path(name, ".arm");
	if (iron_file_exists(arm)) {
		import_arm_run_material(arm);
	}
}

void saved_materials_delete(i32 i) {
	char *name = saved_materials_name(i);
	if (name == NULL) {
		return;
	}
	name = string_copy(name);
	iron_delete_file(saved_materials_path(name, ".arm"));
	char *icon = saved_materials_path(name, ".png");
	if (iron_file_exists(icon)) {
		data_delete_texture(icon);
		iron_delete_file(icon);
	}
	saved_materials_refresh();
}
