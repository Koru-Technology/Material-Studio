// app_api_materials.h, #included by main.c after app_api.c (it reuses its helpers and the script_* helpers of minic_impl.c)

#include "app_api_materials.h"

extern i32 _tab_materials_draw_slots; // ui/tab_materials.c
void       tab_materials_draw_slots_duplicate(void *_);

static uint32_t app_hash_floats(uint32_t h, f32_array_t *a) {
	if (a == NULL) {
		return app_hash(h, 0xfffffffu);
	}
	for (i32 i = 0; i < a->length; ++i) {
		h = app_hash_f32(h, a->buffer[i]);
	}
	return h;
}

static bool app_mat_project_ready(void) {
	return app_ready() && g_project->_->materials != NULL;
}

static slot_material_t *app_mat_by_id(int32_t id) {
	if (!app_mat_project_ready()) {
		return NULL;
	}
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		if (m->id == id) {
			return m;
		}
	}
	return NULL;
}

// ███╗   ███╗ █████╗ ████████╗███████╗██████╗ ██╗ █████╗ ██╗     ███████╗
// Previews are redrawn from many places, at times a frame or more after the edit: a material's version changes when its
// nodes or values change, and again a couple of frames and some more frames later, so a late redraw is read too.

typedef struct {
	slot_material_t *m;
	uint32_t         shallow; // Pointers, counts, name, readiness: every frame
	uint32_t         deep;    // Node values: every frame for the selected material, one other material per frame
	uint32_t         version;
	int32_t          settle[2]; // Frames to change the version again at, 0 none
	bool             seen;
} app_mat_track_t;

#define APP_MAT_SETTLE_SOON  2
#define APP_MAT_SETTLE_LATER 12

static app_mat_track_t *app_mat_tracks    = NULL;
static i32              app_mat_tracks_n  = 0;
static i32              app_mat_tracks_c  = 0;
static uint32_t         app_mat_versions  = 0; // Versions handed out
static uint32_t         app_mat_previews  = 0; // Changes whenever a version changes
static int32_t          app_mat_frame     = 0;
static i32              app_mat_deep_next = 0; // Round robin of the deep hash of the other materials

static uint32_t app_mat_shallow_hash(slot_material_t *m) {
	uint32_t h = APP_HASH_SEED;
	h          = app_hash_ptr(h, m->canvas);
	h          = app_hash_ptr(h, m->image);
	h          = app_hash(h, m->preview_ready);
	if (m->canvas != NULL) {
		h = app_hash(h, m->canvas->nodes != NULL ? m->canvas->nodes->length : 0);
		h = app_hash(h, m->canvas->links != NULL ? m->canvas->links->length : 0);
	}
	return h;
}

static uint32_t app_mat_deep_hash(slot_material_t *m) {
	uint32_t h = APP_HASH_SEED;
	if (m->canvas == NULL) {
		return h;
	}
	ui_node_array_t *nodes = m->canvas->nodes;
	for (i32 i = 0; nodes != NULL && i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		h            = app_hash(h, n->id);
		h            = app_hash_ptr(h, n->type);
		for (i32 k = 0; n->inputs != NULL && k < n->inputs->length; ++k) {
			h = app_hash_floats(h, n->inputs->buffer[k]->default_value);
		}
		for (i32 k = 0; n->buttons != NULL && k < n->buttons->length; ++k) {
			ui_node_button_t *b = n->buttons->buffer[k];
			h                   = app_hash_floats(h, b->default_value);
			h                   = app_hash_ptr(h, b->data);
			h                   = app_hash(h, b->data != NULL ? b->data->length : 0);
		}
	}
	ui_node_link_array_t *links = m->canvas->links;
	for (i32 i = 0; links != NULL && i < links->length; ++i) {
		ui_node_link_t *l = links->buffer[i];
		h                 = app_hash(h, l->from_id);
		h                 = app_hash(h, l->from_socket);
		h                 = app_hash(h, l->to_id);
		h                 = app_hash(h, l->to_socket);
	}
	return h;
}

static void app_mat_bump(app_mat_track_t *t, bool settle) {
	t->version = ++app_mat_versions;
	app_mat_previews++;
	if (settle) {
		t->settle[0] = app_mat_frame + APP_MAT_SETTLE_SOON;
		t->settle[1] = app_mat_frame + APP_MAT_SETTLE_LATER;
	}
}

static app_mat_track_t *app_mat_track_of(slot_material_t *m) {
	for (i32 i = 0; i < app_mat_tracks_n; ++i) {
		if (app_mat_tracks[i].m == m) {
			return &app_mat_tracks[i];
		}
	}
	return NULL;
}

// Once per frame
static void app_mat_track_update(void) {
	app_mat_frame++;
	for (i32 i = 0; i < app_mat_tracks_n; ++i) {
		app_mat_tracks[i].seen = false;
	}
	i32 count = g_project->_->materials->length;
	for (i32 i = 0; i < count; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		app_mat_track_t *t = app_mat_track_of(m);
		if (t == NULL) {
			if (app_mat_tracks_n == app_mat_tracks_c) {
				app_mat_tracks_c = app_mat_tracks_c * 2 + 16;
				app_mat_tracks   = realloc(app_mat_tracks, sizeof(app_mat_track_t) * app_mat_tracks_c);
			}
			t          = &app_mat_tracks[app_mat_tracks_n++];
			*t         = (app_mat_track_t){0};
			t->m       = m;
			t->shallow = app_mat_shallow_hash(m);
			t->deep    = app_mat_deep_hash(m);
			app_mat_bump(t, true);
		}
		t->seen     = true;
		uint32_t sh = app_mat_shallow_hash(m);
		bool     changed = sh != t->shallow;
		t->shallow  = sh;
		if (m == g_context->material || i == app_mat_deep_next % count) {
			uint32_t dh = app_mat_deep_hash(m);
			changed |= dh != t->deep;
			t->deep = dh;
		}
		if (changed) {
			app_mat_bump(t, true);
		}
		else {
			for (i32 k = 0; k < 2; ++k) {
				if (t->settle[k] != 0 && app_mat_frame >= t->settle[k]) {
					t->settle[k] = 0;
					app_mat_bump(t, false);
				}
			}
		}
	}
	app_mat_deep_next++;
	// Materials gone (deleted, another project)
	i32 kept = 0;
	for (i32 i = 0; i < app_mat_tracks_n; ++i) {
		if (app_mat_tracks[i].seen) {
			app_mat_tracks[kept++] = app_mat_tracks[i];
		}
	}
	app_mat_tracks_n = kept;
}

static uint32_t app_mat_signature(void) {
	uint32_t h = APP_HASH_SEED;
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		h                  = app_hash(h, (uint32_t)m->id);
		h                  = app_hash_ptr(h, m);
		h                  = app_hash(h, m->preview_ready);
		h                  = app_hash_str(h, m->canvas != NULL ? m->canvas->name : "");
	}
	h = app_hash(h, g_context->material != NULL ? (uint32_t)g_context->material->id : 0xffffffffu);
	return h;
}

// The layer smart materials save: the selected fill layer or group, the fill layer of a selected mask
static slot_layer_t *app_smart_target(void) {
	slot_layer_t *l = g_context->layer;
	if (l == NULL) {
		return NULL;
	}
	if (slot_layer_is_mask(l)) {
		l = l->parent;
	}
	if (l == NULL || slot_layer_is_mask(l)) {
		return NULL;
	}
	return slot_layer_is_group(l) || l->fill_material != NULL ? l : NULL;
}

void app_materials_get_state(app_materials_state_t *out) {
	*out             = (app_materials_state_t){0};
	out->selected_id = -1;
	if (!app_mat_project_ready()) {
		return;
	}
	app_mat_track_update();
	out->ready               = true;
	out->selected_id         = g_context->material != NULL ? g_context->material->id : -1;
	out->materials_signature = app_mat_signature();
	out->previews_version    = app_mat_previews;
	out->smart_can_save      = app_smart_target() != NULL;
	out->can_apply_to_fill   = tab_layers_fill_target(g_context->layer) != NULL;
}

int32_t app_material_count(void) {
	return app_mat_project_ready() ? g_project->_->materials->length : 0;
}

bool app_material_info(int32_t index, app_material_info_t *out) {
	if (index < 0 || index >= app_material_count()) {
		return false;
	}
	slot_material_t *m    = g_project->_->materials->buffer[index];
	app_mat_track_t *t    = app_mat_track_of(m);
	out->id               = m->id;
	out->preview_version  = t != NULL ? t->version : 0;
	out->selected         = m == g_context->material;
	out->preview_ready    = m->preview_ready;
	out->can_open_studio  = material_studio_can_open(m);
	out->name             = app_str(m->canvas != NULL ? m->canvas->name : NULL);
	return true;
}

bool app_material_select(int32_t id) {
	slot_material_t *m = app_mat_by_id(id);
	if (m == NULL) {
		return false;
	}
	if (m != g_context->material) {
		context_set_material(m);
	}
	return true;
}

int32_t app_material_new(void) {
	slot_material_t *m = app_mat_project_ready() ? script_material_create(NULL) : NULL;
	return m != NULL ? m->id : -1;
}

int32_t app_material_duplicate(int32_t id) {
	slot_material_t *m = app_mat_by_id(id);
	if (m == NULL) {
		return -1;
	}
	_tab_materials_draw_slots = array_index_of(g_project->_->materials, m);
	draw_saved_t gpu          = draw_suspend();
	tab_materials_draw_slots_duplicate(NULL);
	draw_resume(gpu);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	return g_context->material != NULL ? g_context->material->id : -1;
}

bool app_material_can_delete(int32_t id) {
	return app_mat_by_id(id) != NULL && g_project->_->materials->length > 1;
}

bool app_material_delete(int32_t id) {
	slot_material_t *m = app_mat_by_id(id);
	if (m == NULL || g_project->_->materials->length <= 1) {
		return false;
	}
	script_material_delete(m);
	iron_delay_idle_sleep();
	return app_mat_by_id(id) != m;
}

int32_t app_material_delete_unused(void) {
	if (!app_mat_project_ready()) {
		return 0;
	}
	i32 before = g_project->_->materials->length;
	tab_materials_delete_unused();
	iron_delay_idle_sleep();
	return before - g_project->_->materials->length;
}

bool app_material_rename(int32_t id, const char *name) {
	slot_material_t *m = app_mat_by_id(id);
	if (m == NULL || m->canvas == NULL || name == NULL || name[0] == '\0') {
		return false;
	}
	if (m->canvas->name != NULL && string_equals(m->canvas->name, (char *)name)) {
		return true;
	}
	m->canvas->name  = string_copy(slot_material_unique_name(m->canvas, (char *)name));
	project_modified = true;
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	return true;
}

// Editing waits for the next frame as in the old tab, the material is found again by id then

static void app_material_edit_next(void *data) {
	slot_material_t *m = app_mat_by_id((int32_t)(intptr_t)data);
	if (m == NULL) {
		return;
	}
	if (material_studio_can_open(m)) {
		material_studio_open(m);
	}
	else {
		ui_nodes_edit_material(m);
	}
}

bool app_material_edit(int32_t id) {
	if (!app_material_select(id)) {
		return false;
	}
	sys_notify_on_next_frame(&app_material_edit_next, (void *)(intptr_t)id);
	return true;
}

static void app_material_open_studio_next(void *data) {
	slot_material_t *m = app_mat_by_id((int32_t)(intptr_t)data);
	if (m != NULL) {
		material_studio_open(m);
	}
}

bool app_material_open_studio(int32_t id) {
	slot_material_t *m = app_mat_by_id(id);
	if (m == NULL || !material_studio_can_open(m)) {
		return false;
	}
	app_material_select(id);
	sys_notify_on_next_frame(&app_material_open_studio_next, (void *)(intptr_t)id);
	return true;
}

bool app_material_show_nodes(int32_t id) {
	if (!app_material_select(id)) {
		return false;
	}
	if (!ui_nodes_show || ui_nodes_canvas_type != CANVAS_TYPE_MATERIAL) { // It toggles
		ui_base_show_material_nodes();
	}
	iron_delay_idle_sleep();
	return true;
}

bool app_material_to_fill_layer(int32_t id) {
	if (!app_material_select(id)) {
		return false;
	}
	layers_create_fill_layer(UV_TYPE_UVMAP, mat4_nan(), -1);
	return true;
}

bool app_material_apply_to_fill(int32_t id) {
	slot_material_t *m      = app_mat_by_id(id);
	slot_layer_t    *target = g_context != NULL ? tab_layers_fill_target(g_context->layer) : NULL;
	if (m == NULL || target == NULL) {
		return false;
	}
	if (target->fill_material != m) {
		tab_layers_set_fill_material_later(target, m);
	}
	return true;
}

int32_t app_material_thumbnail(int32_t id, int32_t max_size, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	slot_material_t *m = app_mat_by_id(id);
	if (m == NULL || !m->preview_ready) {
		return 0;
	}
	return app_read_rgba8(m->image, max_size, true, false, out, capacity, w, h);
}

// ██╗     ██╗██████╗ ██████╗  █████╗ ██████╗ ██╗   ██╗
// The items of the current view, rebuilt when its signature changes

typedef struct {
	int8_t  source;
	int32_t index; // In its source
	int32_t section;
	int32_t group;
	bool    stylized;
	char   *ident; // Saved items: the name, folder items: the relative path. How they are found again
} app_lib_ref_t;

static app_lib_ref_t *app_lib_items     = NULL;
static i32            app_lib_n         = 0;
static i32            app_lib_cap       = 0;
static uint32_t       app_lib_built     = 0;
static bool           app_lib_has_built = false;
static uint32_t       app_lib_sig       = 0;

#define APP_LIB_MM_SECTIONS_MAX 64

static void app_lib_push(int8_t source, i32 index, i32 section, i32 group, bool stylized, char *ident) {
	if (app_lib_n == app_lib_cap) {
		i32            cap   = app_lib_cap * 2 + 256;
		app_lib_ref_t *items = realloc(app_lib_items, sizeof(app_lib_ref_t) * cap);
		if (items == NULL) { // Out of memory: the item is left out
			free(ident);
			return;
		}
		app_lib_items = items;
		app_lib_cap   = cap;
	}
	app_lib_items[app_lib_n++] = (app_lib_ref_t){.source = source, .index = index, .section = section, .group = group, .stylized = stylized, .ident = ident};
}

static i32 app_lib_mm_sections(void) {
	i32 n = mm_library_section_count();
	return n < APP_LIB_MM_SECTIONS_MAX ? n : APP_LIB_MM_SECTIONS_MAX;
}

static uint32_t app_lib_compute_signature(void) {
	uint32_t h     = APP_HASH_SEED;
	i32      saved = saved_materials_count();
	h         = app_hash(h, saved);
	for (i32 i = 0; i < saved; ++i) {
		h = app_hash_str(h, saved_materials_name(i));
		h = app_hash_ptr(h, saved_materials_icon(i)); // A material saved again under its name gets a new icon
	}
	i32 folder = library_entry_count();
	h          = app_hash(h, folder);
	for (i32 i = 0; i < folder; ++i) {
		library_entry_t *e = library_entry_get(i);
		h                  = app_hash_ptr(h, e);
		h                  = app_hash(h, (library_entry_ready(e) ? 1 : 0) | (library_entry_broken(e) ? 2 : 0));
	}
	h = app_hash(h, mm_library_count());
	return h;
}

static void app_lib_build(void) {
	for (i32 i = 0; i < app_lib_n; ++i) {
		free(app_lib_items[i].ident);
	}
	app_lib_n = 0;
	for (i32 i = 0; i < saved_materials_count(); ++i) {
		app_lib_push(APP_LIBRARY_SAVED, i, 0, -1, false, strdup(saved_materials_name(i)));
	}
	for (i32 i = 0; i < library_entry_count(); ++i) {
		library_entry_t *e = library_entry_get(i);
		if (library_entry_ready(e) && !library_entry_broken(e)) {
			app_lib_push(APP_LIBRARY_FOLDER, i, 1, -1, false, strdup(library_entry_rel(e)));
		}
	}
	i32 n        = mm_library_count();
	i32 sections = app_lib_mm_sections();
	for (i32 st = 0; st < 2; ++st) {
		for (i32 s = 0; s < sections; ++s) {
			for (i32 i = 0; i < n; ++i) {
				bool stylized = mm_library_stylized(i);
				if ((stylized ? 0 : 1) == st && mm_library_section(i) == s) {
					app_lib_push(APP_LIBRARY_MM, i, 2 + st * sections + s, s, stylized, NULL);
				}
			}
		}
	}
}

static void app_lib_ensure(void) {
	if (!app_lib_has_built || app_lib_built != app_lib_sig) {
		app_lib_has_built = true;
		app_lib_built     = app_lib_sig;
		app_lib_build();
	}
}

uint32_t app_library_signature(void) {
	if (!app_mat_project_ready()) {
		return 0;
	}
	app_lib_sig = app_lib_compute_signature();
	app_lib_ensure();
	return app_lib_sig;
}

int32_t app_library_count(void) {
	if (!app_lib_has_built) {
		app_library_signature();
	}
	return app_lib_n;
}

static app_lib_ref_t *app_lib_ref(int32_t index) {
	if (index < 0 || index >= app_library_count()) {
		return NULL;
	}
	app_lib_ref_t *r = &app_lib_items[index];
	// The sources keep their items while the signature stays, a stale index is not found (nor another item taken for it)
	switch (r->source) {
	case APP_LIBRARY_SAVED:
		return r->index >= 0 && r->index < saved_materials_count() && string_equals(saved_materials_name(r->index), r->ident) ? r : NULL;
	case APP_LIBRARY_FOLDER:
		return r->index >= 0 && r->index < library_entry_count() && string_equals(library_entry_rel(library_entry_get(r->index)), r->ident) ? r : NULL;
	default:
		return r->index >= 0 && r->index < mm_library_count() ? r : NULL;
	}
}

static char *app_lib_style_name(bool stylized) {
	return stylized ? tr("Stylized") : tr("Realistic");
}

bool app_library_item(int32_t index, app_library_item_t *out) {
	static char section[256];
	static char detail[512];
	app_lib_ref_t *r = app_lib_ref(index);
	if (r == NULL) {
		return false;
	}
	*out          = (app_library_item_t){0};
	out->source   = r->source;
	out->section  = r->section;
	out->group    = r->group;
	out->stylized = r->stylized;
	uint32_t key  = app_hash(APP_HASH_SEED, (uint32_t)r->source);
	switch (r->source) {
	case APP_LIBRARY_SAVED: {
		char *name      = saved_materials_name(r->index);
		out->name       = name;
		out->group_name = "";
		out->detail     = tr("Saved");
		out->can_delete = true;
		snprintf(section, sizeof(section), "%s", tr("Saved"));
		key = app_hash_ptr(app_hash_str(key, name), saved_materials_icon(r->index));
		break;
	}
	case APP_LIBRARY_FOLDER: {
		library_entry_t *e = library_entry_get(r->index);
		out->name          = library_entry_name(e);
		out->group_name    = "";
		out->detail        = library_entry_rel(e);
		snprintf(section, sizeof(section), "%s", tr("My Materials"));
		key = app_hash_str(key, library_entry_rel(e));
		break;
	}
	case APP_LIBRARY_MM: {
		out->name       = mm_library_name(r->index);
		out->group_name = mm_library_section_name(r->group);
		snprintf(detail, sizeof(detail), "%s", mm_library_credit(r->index));
		out->detail = detail;
		snprintf(section, sizeof(section), "%s %s", app_lib_style_name(r->stylized), out->group_name);
		key = app_hash(key, (uint32_t)mm_library_id(r->index));
		break;
	}
	}
	out->section_name = section;
	out->key          = key;
	out->name         = app_str(out->name);
	out->group_name   = app_str(out->group_name);
	out->detail       = app_str(out->detail);
	return true;
}

// Lower case, ASCII
static char *app_lib_lower(const char *s) {
	static char buf[256];
	i32         i = 0;
	for (; s != NULL && s[i] != '\0' && i < (i32)sizeof(buf) - 1; ++i) {
		char c = s[i];
		buf[i] = c >= 'A' && c <= 'Z' ? c + 32 : c;
	}
	buf[i] = '\0';
	return buf;
}

static bool app_lib_contains(const char *text, const char *lower_search) {
	static char hay[1024];
	i32         i = 0;
	for (; text != NULL && text[i] != '\0' && i < (i32)sizeof(hay) - 1; ++i) {
		char c = text[i];
		hay[i] = c >= 'A' && c <= 'Z' ? c + 32 : c;
	}
	hay[i] = '\0';
	return strstr(hay, lower_search) != NULL;
}

bool app_library_matches(int32_t index, const char *search) {
	app_lib_ref_t *r = app_lib_ref(index);
	if (r == NULL) {
		return false;
	}
	if (search == NULL || search[0] == '\0') {
		return true;
	}
	char *s = app_lib_lower(search);
	switch (r->source) {
	case APP_LIBRARY_SAVED:
		return app_lib_contains(saved_materials_name(r->index), s);
	case APP_LIBRARY_FOLDER:
		return app_lib_contains(library_entry_rel(library_entry_get(r->index)), s);
	default:
		return mm_library_matches(r->index, s);
	}
}

int32_t app_library_group_count(void) {
	return app_lib_mm_sections();
}

static void app_lib_use_saved_next(void *data) {
	char *name = data;
	for (i32 i = 0; i < saved_materials_count(); ++i) {
		if (string_equals(saved_materials_name(i), name)) {
			saved_materials_use(i);
			break;
		}
	}
	free(name);
}

static void app_lib_use_folder_next(void *data) {
	char *rel = data;
	for (i32 i = 0; i < library_entry_count(); ++i) {
		library_entry_t *e = library_entry_get(i);
		if (string_equals(library_entry_rel(e), rel)) {
			library_use(e);
			break;
		}
	}
	free(rel);
}

bool app_library_use(int32_t index) {
	app_lib_ref_t *r = app_lib_ref(index);
	if (r == NULL) {
		return false;
	}
	switch (r->source) {
	case APP_LIBRARY_SAVED: // Imports render: they run when the frame starts, as from the old tab
		sys_notify_on_next_frame(&app_lib_use_saved_next, strdup(saved_materials_name(r->index)));
		break;
	case APP_LIBRARY_FOLDER:
		sys_notify_on_next_frame(&app_lib_use_folder_next, strdup(r->ident));
		break;
	default:
		mm_library_use(r->index); // On the next frame
		break;
	}
	iron_delay_idle_sleep();
	return true;
}

bool app_library_delete(int32_t index) {
	app_lib_ref_t *r = app_lib_ref(index);
	if (r == NULL || r->source != APP_LIBRARY_SAVED) {
		return false;
	}
	saved_materials_delete(r->index);
	app_library_signature();
	return true;
}

const char *app_library_status(void) {
	if (!library_has_folder()) {
		return "";
	}
	return app_str(library_status());
}

int32_t app_library_icon(int32_t index, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	app_lib_ref_t *r = app_lib_ref(index);
	if (r == NULL) {
		return -1;
	}
	gpu_texture_t *icon;
	switch (r->source) {
	case APP_LIBRARY_SAVED:
		icon = saved_materials_icon(r->index);
		if (icon == NULL) {
			return -1; // Saved without a preview
		}
		break;
	case APP_LIBRARY_FOLDER: {
		library_entry_t *e = library_entry_get(r->index);
		icon               = library_entry_icon(e); // Requests it
		if (icon == NULL && library_entry_broken(e)) {
			return -1;
		}
		break;
	}
	default:
		icon = mm_library_icon(r->index); // Requests it, loaded a few per frame
		break;
	}
	if (icon == NULL) {
		return 0;
	}
	return app_read_rgba8(icon, 128, true, false, out, capacity, w, h);
}

// ███████╗███╗   ███╗ █████╗ ██████╗ ████████╗

uint32_t app_smart_signature(void) {
	uint32_t h = APP_HASH_SEED;
	if (!app_mat_project_ready()) {
		return h;
	}
	i32 n = smart_materials_count();
	h     = app_hash(h, n);
	for (i32 i = 0; i < n; ++i) {
		h = app_hash_str(h, smart_materials_name(i));
		h = app_hash_ptr(h, smart_materials_icon(i)); // Saved again under its name: a new icon
	}
	return h;
}

int32_t app_smart_count(void) {
	return app_ready() ? smart_materials_count() : 0;
}

bool app_smart_info(int32_t index, app_smart_info_t *out) {
	if (index < 0 || index >= app_smart_count()) {
		return false;
	}
	out->name = app_str(smart_materials_name(index));
	out->key  = app_hash_ptr(app_hash_str(APP_HASH_SEED, out->name), smart_materials_icon(index));
	return true;
}

int32_t app_smart_find(const char *name) {
	return name != NULL ? smart_materials_find((char *)name) : -1;
}

int32_t app_smart_icon(int32_t index, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	if (index < 0 || index >= app_smart_count()) {
		return -1;
	}
	gpu_texture_t *icon = smart_materials_icon(index);
	if (icon == NULL) {
		return -1;
	}
	return app_read_rgba8(icon, 128, true, false, out, capacity, w, h);
}

// Applying and saving draw to layers and export materials: they run when the frame starts, as from the old tab

static void app_smart_apply_next(void *name) {
	smart_materials_apply(name);
	free(name);
}

bool app_smart_apply(int32_t index) {
	char *name = index >= 0 && index < app_smart_count() ? smart_materials_name(index) : NULL;
	if (name == NULL) {
		return false;
	}
	sys_notify_on_next_frame(&app_smart_apply_next, strdup(name));
	iron_delay_idle_sleep();
	return true;
}

bool app_smart_delete(int32_t index) {
	if (index < 0 || index >= app_smart_count()) {
		return false;
	}
	smart_materials_delete(index);
	return true;
}

const char *app_smart_default_name(void) {
	slot_layer_t *l = app_mat_project_ready() ? app_smart_target() : NULL;
	if (l == NULL) {
		return "";
	}
	return app_str(l->fill_material != NULL && l->fill_material->canvas != NULL ? l->fill_material->canvas->name : l->name);
}

static void app_smart_save_next(void *name) {
	smart_materials_save(app_smart_target(), name);
	free(name);
}

bool app_smart_save(const char *name) {
	if (name == NULL || name[0] == '\0' || app_smart_target() == NULL) {
		return false;
	}
	sys_notify_on_next_frame(&app_smart_save_next, strdup(name));
	iron_delay_idle_sleep();
	return true;
}
