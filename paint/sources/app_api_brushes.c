// app_api_brushes.h, #included by main.c after app_api.c (it reuses app_hash and the brush library, tab_brushes, tab_swatches
// and ui_quick_menu state of the same unit)

#include "app_api_brushes.h"

static bool app_brush_group_hidden(brush_group_t *g) {
	return g->builtin && g_config->hide_builtin_brushes;
}

// A user group or brush: can be renamed, moved, deleted
static brush_group_t *app_user_group(int32_t id) {
	brush_group_t *g = brush_group_find(id);
	return g != NULL && !g->builtin ? g : NULL;
}

static brush_def_t *app_user_brush(int32_t id) {
	brush_def_t *d = brush_library_find(id);
	return d != NULL && !d->builtin ? d : NULL;
}

static uint32_t app_brush_preview_key(brush_def_t *d) {
	uint32_t h = APP_HASH_SEED;
	h          = app_hash(h, (uint32_t)d->row_key);
	h          = app_hash(h, (uint32_t)(d->row_key >> 32));
	h          = app_hash(h, d->row_state == BRUSH_PREVIEW_READY);
	h          = app_hash(h, (uint32_t)(uintptr_t)d->image_row);
	h          = app_hash(h, d->row_rt);
	return h;
}

static uint32_t app_brushes_library_signature(void) {
	uint32_t h = APP_HASH_SEED;
	for (i32 i = 0; i < brush_group_count(); ++i) {
		brush_group_t *g = brush_group_at(i);
		h                = app_hash(h, (uint32_t)g->id);
		h                = app_hash(h, app_brush_group_hidden(g));
		h                = app_hash_str(h, brush_group_name(g));
	}
	for (i32 i = 0; i < brush_library_count(); ++i) {
		brush_def_t *d = brush_library_at(i);
		h              = app_hash(h, (uint32_t)d->id);
		h              = app_hash(h, (uint32_t)d->group);
		h              = app_hash_str(h, brush_library_name(d));
	}
	h = app_hash(h, (uint32_t)g_context->brush_selected);
	for (i32 i = 0; i < 3; ++i) {
		h = app_hash(h, (uint32_t)app_brush_recent(i));
	}
	return h;
}

static uint32_t app_brushes_previews_signature(void) {
	uint32_t h = APP_HASH_SEED;
	for (i32 i = 0; i < brush_library_count(); ++i) {
		h = app_hash(h, app_brush_preview_key(brush_library_at(i)));
	}
	return h;
}

static uint32_t app_hash_swatch(uint32_t h, swatch_color_t *s) {
	h = app_hash(h, (uint32_t)s->base);
	h = app_hash_f32(h, s->opacity);
	h = app_hash_f32(h, s->occlusion);
	h = app_hash_f32(h, s->roughness);
	h = app_hash_f32(h, s->metallic);
	h = app_hash_f32(h, s->height);
	return h;
}

static uint32_t app_color_signature(void) {
	uint32_t h = APP_HASH_SEED;
	if (g_context->picked_color != NULL) {
		h = app_hash_swatch(h, g_context->picked_color);
	}
	if (g_project != NULL && g_project->swatches != NULL) {
		for (i32 i = 0; i < g_project->swatches->length; ++i) {
			h = app_hash_swatch(h, g_project->swatches->buffer[i]);
		}
	}
	h = app_hash(h, (uint32_t)app_swatch_selected());
	h = app_hash(h, app_color_can_apply());
	return h;
}

void app_brushes_get_state(app_brushes_state_t *out) {
	*out = (app_brushes_state_t){0};
	if (!app_ready()) {
		return;
	}
	out->library_signature  = app_brushes_library_signature();
	out->previews_signature = app_brushes_previews_signature();
	out->color_signature    = app_color_signature();
	out->selected_brush_id  = g_context->brush_selected;
}

// Groups

int32_t app_brush_group_count(void) {
	return app_ready() ? brush_group_count() : 0;
}

bool app_brush_group_info(int32_t index, app_brush_group_info_t *out) {
	brush_group_t *g = app_ready() ? brush_group_at(index) : NULL;
	if (g == NULL) {
		return false;
	}
	out->id      = g->id;
	out->builtin = g->builtin;
	out->hidden  = app_brush_group_hidden(g);
	out->name    = app_str(brush_group_name(g));
	return true;
}

int32_t app_brush_group_add(const char *name) {
	if (!app_ready() || name == NULL || name[0] == '\0') {
		return -1;
	}
	i32            n = brush_group_count();
	brush_group_t *g = brush_group_add((char *)name);
	return brush_group_count() > n ? g->id : -1; // The library is full
}

bool app_brush_group_rename(int32_t id, const char *name) {
	brush_group_t *g = app_ready() ? app_user_group(id) : NULL;
	if (g == NULL || name == NULL || name[0] == '\0') {
		return false;
	}
	brush_group_rename(g, (char *)name);
	return true;
}

bool app_brush_group_delete(int32_t id) {
	brush_group_t *g = app_ready() ? app_user_group(id) : NULL;
	if (g == NULL) {
		return false;
	}
	brush_group_delete(g);
	iron_delay_idle_sleep();
	return true;
}

// Brushes

int32_t app_brush_count(void) {
	return app_ready() ? brush_library_count() : 0;
}

bool app_brush_info(int32_t index, app_brush_info_t *out) {
	brush_def_t *d = app_ready() ? brush_library_at(index) : NULL;
	if (d == NULL) {
		return false;
	}
	out->id            = d->id;
	out->group_id      = d->group;
	out->builtin       = d->builtin;
	out->selected      = g_context->brush_selected == d->id;
	out->preview_ready = d->row_state == BRUSH_PREVIEW_READY && d->image_row != NULL;
	out->preview_key   = app_brush_preview_key(d);
	out->name          = app_str(brush_library_name(d));
	return true;
}

bool app_brush_use(int32_t id) {
	brush_def_t *d = app_ready() ? brush_library_find(id) : NULL;
	if (d == NULL) {
		return false;
	}
	brush_library_use(d);
	iron_delay_idle_sleep();
	return true;
}

int32_t app_brush_duplicate(int32_t id) {
	brush_def_t *d = app_ready() ? brush_library_find(id) : NULL;
	if (d == NULL) {
		return -1;
	}
	// As the old Brushes tab: a built-in brush is copied to a group of your own, where it can be edited
	i32          group = d->builtin ? brush_library_user_group() : d->group;
	brush_def_t *c     = brush_library_duplicate(d, group);
	if (c == NULL) {
		return -1;
	}
	brush_library_use(c);
	iron_delay_idle_sleep();
	return c->id;
}

bool app_brush_delete(int32_t id) {
	brush_def_t *d = app_ready() ? app_user_brush(id) : NULL;
	if (d == NULL) {
		return false;
	}
	brush_library_delete(d);
	iron_delay_idle_sleep();
	return true;
}

bool app_brush_move(int32_t id, int32_t group_id) {
	brush_def_t   *d = app_ready() ? app_user_brush(id) : NULL;
	brush_group_t *g = app_ready() ? app_user_group(group_id) : NULL;
	if (d == NULL || g == NULL || d->group == g->id) {
		return false;
	}
	d->group = g->id;
	brush_library_save();
	return true;
}

bool app_brush_rename(int32_t id, const char *name) {
	brush_def_t *d = app_ready() ? app_user_brush(id) : NULL;
	if (d == NULL || name == NULL || name[0] == '\0') {
		return false;
	}
	d->name = string_copy((char *)name);
	brush_library_save();
	return true;
}

void app_brush_new(int32_t group_id) {
	if (!app_ready() || ui_brush_creator_shown()) {
		return;
	}
	ui_brush_creator_new(group_id);
	iron_delay_idle_sleep();
}

bool app_brush_edit(int32_t id) {
	brush_def_t *d = app_ready() ? app_user_brush(id) : NULL;
	if (d == NULL || ui_brush_creator_shown()) {
		return false;
	}
	ui_brush_creator_edit(d);
	iron_delay_idle_sleep();
	return true;
}

bool app_brush_creator_shown(void) {
	return app_ready() && ui_brush_creator_shown();
}

int32_t app_brush_recent(int32_t index) {
	if (index < 0 || index >= 3) {
		return -1;
	}
	i32 id = ui_quick_menu_recent[index];
	return brush_library_find(id) != NULL ? id : -1;
}

// Stroke previews

void app_brush_previews_request(int32_t group_id) {
	brush_previews_group = group_id; // brush_previews_update renders the missing strokes of this group first
	iron_delay_idle_sleep();
}

int32_t app_brush_preview(int32_t id, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	brush_def_t *d = app_ready() ? brush_library_find(id) : NULL;
	if (d == NULL || d->image_row == NULL || d->row_state != BRUSH_PREVIEW_READY) {
		return 0;
	}
	return app_read_rgba8(d->image_row, 0, false, out, capacity, w, h);
}

int32_t app_brush_preview_size(int32_t id) {
	int32_t w, h;
	return app_brush_preview(id, NULL, 0, &w, &h);
}

// Color

static void app_color_fill(swatch_color_t *s, app_color_t *out) {
	out->base      = (uint32_t)color_set_ab(s->base, 255);
	out->opacity   = s->opacity;
	out->occlusion = s->occlusion;
	out->roughness = s->roughness;
	out->metallic  = s->metallic;
	out->height    = s->height;
}

static f32 *app_color_field(swatch_color_t *s, int32_t which) {
	switch (which) {
	case APP_COLOR_OPACITY:
		return &s->opacity;
	case APP_COLOR_OCCLUSION:
		return &s->occlusion;
	case APP_COLOR_ROUGHNESS:
		return &s->roughness;
	case APP_COLOR_METALLIC:
		return &s->metallic;
	case APP_COLOR_HEIGHT:
		return &s->height;
	default:
		return NULL;
	}
}

bool app_color_get(app_color_t *out) {
	*out = (app_color_t){0};
	if (!app_ready() || g_context->picked_color == NULL) {
		return false;
	}
	app_color_fill(g_context->picked_color, out);
	return true;
}

static void app_color_changed(void) {
	g_context->ddirty         = 2; // The picker's color mask reads it
	iron_delay_idle_sleep();
}

void app_color_set_base(uint32_t rgb) {
	if (!app_ready() || g_context->picked_color == NULL) {
		return;
	}
	g_context->picked_color->base = (i32)(0xff000000u | (rgb & 0x00ffffffu));
	app_color_changed();
}

void app_color_set_value(int32_t which, float value) {
	if (!app_ready() || g_context->picked_color == NULL) {
		return;
	}
	f32 *v = app_color_field(g_context->picked_color, which);
	if (v == NULL) {
		return;
	}
	*v = math_max(0.0, math_min(1.0, value));
	app_color_changed();
}

bool app_color_can_apply(void) {
	if (!app_ready() || g_context->material == NULL || g_context->material->canvas == NULL) {
		return false;
	}
	ui_node_canvas_t *canvas = g_context->material->canvas;
	return canvas->nodes->length == 1 && string_equals(canvas->nodes->buffer[0]->type, "OUTPUT_MATERIAL_PBR");
}

// The material is made again on the next frame, as the swatches do: one apply however many asks before it
static bool app_color_apply_pending        = false;
static bool app_color_new_material_pending = false;

static void app_color_apply_on_next_frame(void *_) {
	app_color_apply_pending = false;
	if (g_context->picked_color != NULL) {
		slot_material_apply_picked_color(g_context->picked_color);
	}
}

void app_color_apply(void) {
	if (!app_ready() || app_color_apply_pending) {
		return;
	}
	app_color_apply_pending = true;
	sys_notify_on_next_frame(&app_color_apply_on_next_frame, NULL);
	iron_delay_idle_sleep();
}

static void app_color_new_material_on_next_frame(void *_) {
	app_color_new_material_pending = false;
	if (g_context->picked_color != NULL) {
		slot_material_new_from_swatch(project_clone_swatch(g_context->picked_color));
	}
}

void app_color_new_material(void) {
	if (!app_ready() || app_color_new_material_pending) {
		return;
	}
	app_color_new_material_pending = true;
	sys_notify_on_next_frame(&app_color_new_material_on_next_frame, NULL);
	iron_delay_idle_sleep();
}

// Swatches

static swatch_color_t *app_swatch_at(int32_t index) {
	if (!app_ready() || g_project == NULL || g_project->swatches == NULL || index < 0 || index >= g_project->swatches->length) {
		return NULL;
	}
	return g_project->swatches->buffer[index];
}

int32_t app_swatch_count(void) {
	return app_ready() && g_project != NULL && g_project->swatches != NULL ? g_project->swatches->length : 0;
}

bool app_swatch_info(int32_t index, app_color_t *out) {
	swatch_color_t *s = app_swatch_at(index);
	if (s == NULL) {
		return false;
	}
	app_color_fill(s, out);
	return true;
}

int32_t app_swatch_selected(void) {
	if (app_swatch_count() == 0 || g_context->swatch == NULL) {
		return -1;
	}
	return array_index_of(g_project->swatches, g_context->swatch);
}

// As a tap on a swatch of the old Swatches tab
bool app_swatch_pick(int32_t index) {
	swatch_color_t *s = app_swatch_at(index);
	if (s == NULL) {
		return false;
	}
	g_context->swatch       = s;
	g_context->picked_color = util_clone_swatch_color(s);
	app_color_changed();
	app_color_apply();
	return true;
}

// As Add Swatch of the color picker tool's header
int32_t app_swatch_add(void) {
	if (!app_ready() || g_project == NULL || g_project->swatches == NULL || g_context->picked_color == NULL) {
		return -1;
	}
	swatch_color_t *s = project_clone_swatch(g_context->picked_color);
	s->base           = color_set_ab(s->base, 255);
	g_context->swatch = s;
	any_array_push(g_project->swatches, s);
	history_new_swatch();
	iron_delay_idle_sleep();
	return g_project->swatches->length - 1;
}

bool app_swatch_delete(int32_t index) {
	swatch_color_t *s = app_swatch_at(index);
	if (s == NULL || g_project->swatches->length <= 1) {
		return false;
	}
	project_delete_swatch(s);
	iron_delay_idle_sleep();
	return true;
}
