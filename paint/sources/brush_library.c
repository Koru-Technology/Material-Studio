
#include "global.h"

// Brush library: the brushes of the Brushes popover in groups. Built-in brushes and groups come from the tables below and can not
// be changed, brushes made in the Brush Creator and the groups made for them are kept across projects in brush_library.json
// in the app's save folder, their imported tips as brush_library/<id>.png next to it.
// Picking a brush puts its settings in the context (brush_library_apply), the paint pipeline reads them from there.

#define BRUSH_LIBRARY_MAX        1024
#define BRUSH_LIBRARY_GROUPS_MAX 128
#define BRUSH_LIBRARY_USER_ID    1000 // Ids of user brushes and groups start here, built-in ones stay below

typedef struct brush_library_builtin {
	i32   id;
	char *name;
	i32   group;
	i32   tip;
	f32   hardness;
	f32   spacing;
	f32   scatter;
	f32   jitter_size;
	f32   jitter_angle;
	f32   jitter_opacity;
	f32   flow;
} brush_library_builtin_t;

static brush_group_t brush_library_builtin_groups[] = {
    {1, "Basic", ICON_BRUSH, true},
    {2, "Painting", ICON_PAINT, true},
    {3, "Texture", ICON_CHECKER, true},
    {4, "Effects", ICON_STAR, true},
};

// clang-format off
static brush_library_builtin_t brush_library_builtins[] = {
	{1,  "Round Hard", 1, TIP_NONE,     1.0, 0.0,  0.0, 0.0, 0.0,  0.0, 1.0 },
	{2,  "Round Soft", 1, TIP_NONE,     0.0, 0.0,  0.0, 0.0, 0.0,  0.0, 1.0 },
	{3,  "Airbrush",   1, TIP_NONE,     0.0, 0.08, 0.0, 0.0, 0.0,  0.0, 0.08},
	{4,  "Chalk",      2, TIP_CHALK,    1.0, 0.15, 0.0, 0.0, 1.0,  0.3, 1.0 },
	{5,  "Bristle",    2, TIP_BRISTLE,  1.0, 0.04, 0.0, 0.0, 0.02, 0.0, 1.0 },
	{6,  "Dry Brush",  2, TIP_DRY,      1.0, 0.05, 0.0, 0.0, 0.0,  0.2, 1.0 },
	{7,  "Splatter",   4, TIP_SPLATTER, 1.0, 1.2,  0.8, 0.7, 1.0,  0.2, 1.0 },
	{8,  "Speckle",    3, TIP_SPECKLE,  1.0, 0.7,  1.5, 0.8, 1.0,  0.5, 1.0 },
	{9,  "Star",       4, TIP_STAR,     1.0, 1.5,  0.0, 0.4, 1.0,  0.0, 1.0 },
	{10, "Fur",        3, TIP_STRANDS,  1.0, 0.35, 0.3, 0.3, 0.1,  0.2, 1.0 },
	{11, "Scales",     3, TIP_SCALE,    1.0, 0.8,  0.0, 0.1, 0.0,  0.0, 1.0 },
	{12, "Square",     1, TIP_SQUARE,   1.0, 0.0,  0.0, 0.0, 0.0,  0.0, 1.0 },
};
// clang-format on

#define BRUSH_LIBRARY_BUILTIN_COUNT       ((i32)(sizeof(brush_library_builtins) / sizeof(brush_library_builtins[0])))
#define BRUSH_LIBRARY_BUILTIN_GROUP_COUNT ((i32)(sizeof(brush_library_builtin_groups) / sizeof(brush_library_builtin_groups[0])))

static brush_def_t   *brush_library_defs[BRUSH_LIBRARY_MAX];
static i32            brush_library_n = 0;
static brush_group_t *brush_library_groups[BRUSH_LIBRARY_GROUPS_MAX];
static i32            brush_library_groups_n = 0;
static i32            brush_library_next_id  = BRUSH_LIBRARY_USER_ID;
static bool           brush_library_loaded   = false;

// Tip painted with: the context's brush settings may come from the Brush Creator instead of the picked brush
static gpu_texture_t *brush_library_tip_image = NULL;

brush_settings_t brush_settings_default() {
	brush_settings_t s;
	memset(&s, 0, sizeof(s));
	s.flow          = 1.0;
	s.tip           = TIP_NONE;
	s.hardness      = 1.0;
	s.rotation      = BRUSH_ROTATION_FIXED;
	s.roundness     = 1.0;
	s.taper_size    = 0.7;
	s.taper_opacity = 0.0;
	s.pressure_size = 1.0; // As the pen dynamics defaulted to before they were per brush
	s.preview_size  = 0.5;
	return s;
}

static char *brush_library_file() {
	return string("%sbrush_library.json", iron_internal_save_path());
}

static char *brush_library_dir() {
	return string("%sbrush_library", iron_internal_save_path());
}

static char *brush_library_tip_path(i32 id) {
	return string("%s%s%d.png", brush_library_dir(), PATH_SEP, id);
}

static void brush_library_add_builtins() {
	for (i32 i = 0; i < BRUSH_LIBRARY_BUILTIN_GROUP_COUNT; ++i) {
		brush_library_groups[brush_library_groups_n++] = &brush_library_builtin_groups[i];
	}
	for (i32 i = 0; i < BRUSH_LIBRARY_BUILTIN_COUNT; ++i) {
		brush_library_builtin_t *b = &brush_library_builtins[i];
		brush_def_t             *d = calloc(1, sizeof(brush_def_t));
		d->id                      = b->id;
		d->name                    = b->name;
		d->group                   = b->group;
		d->builtin                 = true;
		d->s                       = brush_settings_default();
		d->s.tip                   = b->tip;
		d->s.hardness              = b->hardness;
		d->s.spacing               = b->spacing;
		d->s.jitter_lateral        = b->scatter;
		d->s.jitter_linear         = b->scatter;
		d->s.jitter_size           = b->jitter_size;
		d->s.jitter_angle          = b->jitter_angle;
		d->s.jitter_opacity        = b->jitter_opacity;
		d->s.flow                  = b->flow;
		d->row_stale               = true;
		brush_library_defs[brush_library_n++] = d;
	}
}

static char *brush_library_get(any_map_t *m, char *prefix, i32 i, char *key) {
	return any_map_get(m, string_tmp("%s%d.%s", prefix, i, key));
}

static f32 brush_library_get_f32(any_map_t *m, i32 i, char *key, f32 def) {
	char *s = brush_library_get(m, "b", i, key);
	return s != NULL ? (f32)atof(s) : def;
}

static i32 brush_library_get_i32(any_map_t *m, char *prefix, i32 i, char *key, i32 def) {
	char *s = brush_library_get(m, prefix, i, key);
	return s != NULL ? atoi(s) : def;
}

static void brush_library_load_file() {
	char *path = brush_library_file();
	if (!iron_file_exists(path)) {
		return;
	}
	buffer_t *blob = data_get_blob(path);
	if (blob == NULL) {
		return;
	}
	char      *str = sys_buffer_to_string(blob);
	any_map_t *m   = json_parse_to_map(str);
	free(str);
	data_delete_blob(path);

	char *next_id = any_map_get(m, "next_id");
	if (next_id != NULL && atoi(next_id) > brush_library_next_id) {
		brush_library_next_id = atoi(next_id);
	}

	char *groups = any_map_get(m, "groups");
	i32   ng     = groups != NULL ? atoi(groups) : 0;
	for (i32 i = 0; i < ng && brush_library_groups_n < BRUSH_LIBRARY_GROUPS_MAX; ++i) {
		char *name = brush_library_get(m, "g", i, "name");
		if (name == NULL) {
			continue;
		}
		brush_group_t *g = calloc(1, sizeof(brush_group_t));
		g->id            = brush_library_get_i32(m, "g", i, "id", brush_library_next_id++);
		g->name          = string_copy(name);
		g->icon          = ICON_FOLDER;
		brush_library_groups[brush_library_groups_n++] = g;
	}

	char *brushes = any_map_get(m, "brushes");
	i32   nb      = brushes != NULL ? atoi(brushes) : 0;
	for (i32 i = 0; i < nb && brush_library_n < BRUSH_LIBRARY_MAX; ++i) {
		char *name = brush_library_get(m, "b", i, "name");
		if (name == NULL) {
			continue;
		}
		brush_def_t *d      = calloc(1, sizeof(brush_def_t));
		brush_settings_t df = brush_settings_default();
		d->id               = brush_library_get_i32(m, "b", i, "id", brush_library_next_id++);
		d->name             = string_copy(name);
		d->group            = brush_library_get_i32(m, "b", i, "group", 0);
		d->s                = df;
#define X(k) d->s.k = brush_library_get_f32(m, i, #k, df.k);
		BRUSH_LIBRARY_SETTINGS(X)
#undef X
		d->s.tip          = brush_library_get_i32(m, "b", i, "tip", df.tip);
		d->s.rotation     = brush_library_get_i32(m, "b", i, "rotation", df.rotation);
		d->s.tilt_shading = brush_library_get_i32(m, "b", i, "tilt_shading", 0) > 0;
		if (d->s.tip == TIP_IMAGE) {
			char *file = brush_library_tip_path(d->id);
			if (iron_file_exists(file)) {
				d->tip_file = file;
			}
			else {
				d->s.tip = TIP_NONE;
			}
		}
		d->row_stale                          = true;
		brush_library_defs[brush_library_n++] = d;
	}
}

static void brush_library_load() {
	if (brush_library_loaded) {
		return;
	}
	brush_library_loaded = true;
	brush_library_add_builtins();
	brush_library_load_file();
}

void brush_library_save() {
	brush_library_load();
	json_encode_begin();
	json_encode_i32("next_id", brush_library_next_id);
	i32 ng = 0;
	for (i32 i = 0; i < brush_library_groups_n; ++i) {
		brush_group_t *g = brush_library_groups[i];
		if (g->builtin) {
			continue;
		}
		json_encode_begin_object_key(string_tmp("g%d", ng++));
		json_encode_i32("id", g->id);
		json_encode_string("name", g->name);
		json_encode_end_object();
	}
	json_encode_i32("groups", ng);
	i32 nb = 0;
	for (i32 i = 0; i < brush_library_n; ++i) {
		brush_def_t *d = brush_library_defs[i];
		if (d->builtin) {
			continue;
		}
		json_encode_begin_object_key(string_tmp("b%d", nb++));
		json_encode_i32("id", d->id);
		json_encode_string("name", d->name);
		json_encode_i32("group", d->group);
#define X(k) json_encode_f32(#k, d->s.k);
		BRUSH_LIBRARY_SETTINGS(X)
#undef X
		json_encode_i32("tip", d->s.tip);
		json_encode_i32("rotation", d->s.rotation);
		json_encode_i32("tilt_shading", d->s.tilt_shading ? 1 : 0);
		json_encode_end_object();
	}
	json_encode_i32("brushes", nb);
	char     *json   = json_encode_end();
	buffer_t *buffer = sys_string_to_buffer(json);
	iron_file_save_bytes(brush_library_file(), buffer, 0);
	array_free(buffer);
	free(buffer);
	free(json);
}

i32 brush_library_count() {
	brush_library_load();
	return brush_library_n;
}

brush_def_t *brush_library_at(i32 i) {
	brush_library_load();
	return i >= 0 && i < brush_library_n ? brush_library_defs[i] : NULL;
}

brush_def_t *brush_library_find(i32 id) {
	brush_library_load();
	for (i32 i = 0; i < brush_library_n; ++i) {
		if (brush_library_defs[i]->id == id) {
			return brush_library_defs[i];
		}
	}
	return NULL;
}

// Built-in names are translated, user names are shown as typed
char *brush_library_name(brush_def_t *d) {
	return d->builtin ? tr(d->name) : d->name;
}

i32 brush_group_count() {
	brush_library_load();
	return brush_library_groups_n;
}

brush_group_t *brush_group_at(i32 i) {
	brush_library_load();
	return i >= 0 && i < brush_library_groups_n ? brush_library_groups[i] : NULL;
}

brush_group_t *brush_group_find(i32 id) {
	brush_library_load();
	for (i32 i = 0; i < brush_library_groups_n; ++i) {
		if (brush_library_groups[i]->id == id) {
			return brush_library_groups[i];
		}
	}
	return NULL;
}

char *brush_group_name(brush_group_t *g) {
	return g->builtin ? tr(g->name) : g->name;
}

brush_def_t *brush_library_selected() {
	brush_def_t *d = brush_library_find(g_context != NULL ? g_context->brush_selected : 1);
	return d != NULL ? d : brush_library_defs[0];
}

// Imported tip of a brush, loaded on first use
gpu_texture_t *brush_library_tip_of(brush_def_t *d) {
	if (d == NULL || d->s.tip != TIP_IMAGE) {
		return NULL;
	}
	if (d->tip_image == NULL && d->tip_file != NULL) {
		buffer_t *blob = data_get_blob(d->tip_file);
		if (blob != NULL) {
			d->tip_image = gpu_create_texture_from_encoded_bytes(blob, ".png");
			data_delete_blob(d->tip_file);
		}
	}
	return d->tip_image;
}

// Tip painted with, NULL for a round brush. A mask image wired in the brush nodes wins over it.
gpu_texture_t *brush_library_tip() {
	if (g_context->brush_tip == TIP_IMAGE) {
		if (brush_library_tip_image == NULL) { // Loaded once painting needs it, the gpu is not up when the context is made
			brush_library_tip_image = brush_library_tip_of(brush_library_selected());
		}
		return brush_library_tip_image;
	}
	return brush_tip_texture(g_context->brush_tip);
}

// Puts settings in the context, the paint pipeline reads them from there. tip_image: the imported tip for TIP_IMAGE.
void brush_library_apply_settings(brush_settings_t *s, gpu_texture_t *tip_image) {
	g_context->brush_spacing           = s->spacing;
	g_context->brush_spacing_jitter    = s->spacing_jitter;
	g_context->brush_jitter_lateral    = s->jitter_lateral;
	g_context->brush_jitter_linear     = s->jitter_linear;
	g_context->brush_falloff           = s->falloff;
	g_context->brush_flow              = s->flow;
	g_context->brush_smoothing         = s->streamline;
	g_context->brush_stabilization     = s->stabilization;
	g_context->brush_taper_start       = s->taper_start;
	g_context->brush_taper_end         = s->taper_end;
	g_context->brush_taper_size        = s->taper_size;
	g_context->brush_taper_opacity     = s->taper_opacity;
	g_context->brush_touch_taper_start = s->touch_taper_start;
	g_context->brush_touch_taper_end   = s->touch_taper_end;
	g_context->brush_tip               = s->tip;
	g_context->brush_hardness          = s->hardness;
	g_context->brush_rotation          = s->rotation;
	g_context->brush_angle             = s->angle;
	g_context->brush_jitter_angle      = s->jitter_angle;
	g_context->brush_jitter_size       = s->jitter_size;
	g_context->brush_jitter_opacity    = s->jitter_opacity;
	g_context->brush_roundness         = s->roundness;
	g_context->brush_pressure_size     = s->pressure_size;
	g_context->brush_pressure_opacity  = s->pressure_opacity;
	g_context->brush_pressure_flow     = s->pressure_flow;
	g_context->brush_pressure_hardness = s->pressure_hardness;
	g_context->brush_tilt_size         = s->tilt_size;
	g_context->brush_tilt_opacity      = s->tilt_opacity;
	g_context->brush_tilt_shading      = s->tilt_shading;
	brush_library_tip_image            = tip_image;
}

// The settings in the context, as brush_library_apply_settings put them
void brush_library_capture_settings(brush_settings_t *s) {
	s->spacing           = g_context->brush_spacing;
	s->spacing_jitter    = g_context->brush_spacing_jitter;
	s->jitter_lateral    = g_context->brush_jitter_lateral;
	s->jitter_linear     = g_context->brush_jitter_linear;
	s->falloff           = g_context->brush_falloff;
	s->flow              = g_context->brush_flow;
	s->streamline        = g_context->brush_smoothing;
	s->stabilization     = g_context->brush_stabilization;
	s->taper_start       = g_context->brush_taper_start;
	s->taper_end         = g_context->brush_taper_end;
	s->taper_size        = g_context->brush_taper_size;
	s->taper_opacity     = g_context->brush_taper_opacity;
	s->touch_taper_start = g_context->brush_touch_taper_start;
	s->touch_taper_end   = g_context->brush_touch_taper_end;
	s->tip               = g_context->brush_tip;
	s->hardness          = g_context->brush_hardness;
	s->rotation          = g_context->brush_rotation;
	s->angle             = g_context->brush_angle;
	s->jitter_angle      = g_context->brush_jitter_angle;
	s->jitter_size       = g_context->brush_jitter_size;
	s->jitter_opacity    = g_context->brush_jitter_opacity;
	s->roundness         = g_context->brush_roundness;
	s->pressure_size     = g_context->brush_pressure_size;
	s->pressure_opacity  = g_context->brush_pressure_opacity;
	s->pressure_flow     = g_context->brush_pressure_flow;
	s->pressure_hardness = g_context->brush_pressure_hardness;
	s->tilt_size         = g_context->brush_tilt_size;
	s->tilt_opacity      = g_context->brush_tilt_opacity;
	s->tilt_shading      = g_context->brush_tilt_shading;
}

gpu_texture_t *brush_library_tip_image_get() {
	return brush_library_tip_image;
}

// Settings of brush d in the context, it becomes the picked brush
void brush_library_apply(brush_def_t *d) {
	if (d == NULL) {
		return;
	}
	g_context->brush_selected = d->id;
	brush_library_apply_settings(&d->s, d->tip_image); // An imported tip not loaded yet loads with brush_library_tip
}

// After the settings in the context changed: the tip and the paint shader follow, panels redraw
void brush_library_applied() {
	brush_output_node_parse_inputs(); // Picks up the tip, recompiles the paint shader when it changed
	make_material_parse_paint_material(true);
	g_context->brush->preview_ready = false;
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	ui_popovers_redraw(-1);
	ui_header_handle->redraws = 2;
}

// Picks brush d to paint with
void brush_library_use(brush_def_t *d) {
	if (d == NULL) {
		return;
	}
	brush_library_apply(d);
	ui_quick_menu_brush_used(d->id);
	if (g_context->tool != TOOL_TYPE_BRUSH && g_context->tool != TOOL_TYPE_ERASER) {
		context_select_tool(TOOL_TYPE_BRUSH);
	}
	brush_library_applied();
	config_save();
}

// The first user group, made when there is none: where duplicated and new brushes go from a built-in group
i32 brush_library_user_group() {
	brush_library_load();
	for (i32 i = 0; i < brush_library_groups_n; ++i) {
		if (!brush_library_groups[i]->builtin) {
			return brush_library_groups[i]->id;
		}
	}
	return brush_group_add(tr("My Brushes"))->id;
}

brush_group_t *brush_group_add(char *name) {
	brush_library_load();
	if (brush_library_groups_n >= BRUSH_LIBRARY_GROUPS_MAX) {
		return brush_library_groups[brush_library_groups_n - 1];
	}
	brush_group_t *g = calloc(1, sizeof(brush_group_t));
	g->id            = brush_library_next_id++;
	g->name          = string_copy(name != NULL && name[0] != '\0' ? name : tr("Group"));
	g->icon          = ICON_FOLDER;
	brush_library_groups[brush_library_groups_n++] = g;
	brush_library_save();
	return g;
}

void brush_group_rename(brush_group_t *g, char *name) {
	if (g == NULL || g->builtin || name == NULL || name[0] == '\0') {
		return;
	}
	g->name = string_copy(name);
	brush_library_save();
}

static void brush_library_remove_at(i32 i) {
	brush_def_t *d = brush_library_defs[i];
	if (d->tip_file != NULL && iron_file_exists(d->tip_file)) {
		iron_delete_file(d->tip_file);
	}
	for (i32 j = i; j < brush_library_n - 1; ++j) {
		brush_library_defs[j] = brush_library_defs[j + 1];
	}
	brush_library_n--;
	if (g_context->brush_selected == d->id) {
		brush_library_use(brush_library_defs[0]);
	}
}

// Deletes a user group and its brushes
void brush_group_delete(brush_group_t *g) {
	if (g == NULL || g->builtin) {
		return;
	}
	for (i32 i = brush_library_n - 1; i >= 0; --i) {
		if (brush_library_defs[i]->group == g->id) {
			brush_library_remove_at(i);
		}
	}
	for (i32 i = 0; i < brush_library_groups_n; ++i) {
		if (brush_library_groups[i] == g) {
			for (i32 j = i; j < brush_library_groups_n - 1; ++j) {
				brush_library_groups[j] = brush_library_groups[j + 1];
			}
			brush_library_groups_n--;
			break;
		}
	}
	brush_library_save();
}

// A brush not in the library yet, to be filled in the Brush Creator
brush_def_t *brush_library_new_def() {
	brush_def_t *d = calloc(1, sizeof(brush_def_t));
	d->name        = string_copy(tr("New Brush"));
	d->s           = brush_settings_default();
	d->row_stale   = true;
	return d;
}

// Copies the tip image file of from to brush to, when it has one
static void brush_library_copy_tip(brush_def_t *from, brush_def_t *to) {
	if (from->s.tip != TIP_IMAGE || from->tip_file == NULL || !iron_file_exists(from->tip_file)) {
		return;
	}
	iron_create_directory(brush_library_dir());
	char *file = brush_library_tip_path(to->id);
	if (!string_equals(file, from->tip_file)) {
		file_copy(from->tip_file, file);
	}
	to->tip_file  = file;
	to->tip_image = NULL;
}

// Adds a brush made in the Brush Creator, a user brush with its own id. The tip image, when imported, is saved with it.
void brush_library_add(brush_def_t *d, gpu_texture_t *tip_image) {
	brush_library_load();
	if (brush_library_n >= BRUSH_LIBRARY_MAX) {
		return;
	}
	d->id        = brush_library_next_id++;
	d->builtin   = false;
	d->row_stale = true;
	brush_library_set_tip(d, tip_image);
	brush_library_defs[brush_library_n++] = d;
	brush_library_save();
}

// Saves the imported tip of user brush d, NULL keeps the file it has
void brush_library_set_tip(brush_def_t *d, gpu_texture_t *tip_image) {
	if (d->s.tip != TIP_IMAGE || tip_image == NULL || tip_image == d->tip_image) {
		return;
	}
	iron_create_directory(brush_library_dir());
	char     *file   = brush_library_tip_path(d->id);
	buffer_t *pixels = gpu_get_texture_pixels(tip_image);
	iron_write_png(file, pixels, tip_image->width, tip_image->height, 0);
	gpu_release_texture_pixels(tip_image);
	d->tip_file  = file;
	d->tip_image = tip_image;
}

// An editable copy of brush d in group, right after the original when they share the group
brush_def_t *brush_library_duplicate(brush_def_t *d, i32 group) {
	brush_library_load();
	if (d == NULL || brush_library_n >= BRUSH_LIBRARY_MAX) {
		return NULL;
	}
	brush_def_t *c = calloc(1, sizeof(brush_def_t));
	*c             = *d;
	c->id          = brush_library_next_id++;
	c->name        = string_copy(string_tmp("%s %s", brush_library_name(d), tr("Copy")));
	c->group       = group;
	c->builtin     = false;
	c->tip_file    = NULL;
	c->tip_image   = NULL;
	c->image_row   = NULL;
	c->row_stale   = true;
	brush_library_copy_tip(d, c);
	i32 at = brush_library_n;
	for (i32 i = 0; i < brush_library_n; ++i) {
		if (brush_library_defs[i] == d && d->group == group) {
			at = i + 1;
		}
	}
	for (i32 j = brush_library_n; j > at; --j) {
		brush_library_defs[j] = brush_library_defs[j - 1];
	}
	brush_library_defs[at] = c;
	brush_library_n++;
	brush_library_save();
	return c;
}

void brush_library_delete(brush_def_t *d) {
	if (d == NULL || d->builtin) {
		return;
	}
	for (i32 i = 0; i < brush_library_n; ++i) {
		if (brush_library_defs[i] == d) {
			brush_library_remove_at(i);
			break;
		}
	}
	brush_library_save();
}

// The stroke of every brush in the list renders again, the pen settings changed
void brush_library_rows_stale() {
	brush_library_load();
	for (i32 i = 0; i < brush_library_n; ++i) {
		brush_library_defs[i]->row_stale = true;
	}
}

// An image picked as a brush tip: white, its shape in the alpha. Images without transparency give their brightness as the shape,
// dark shapes on a light background are turned around.
gpu_texture_t *brush_library_load_tip(char *path) {
	buffer_t *blob = data_get_blob(path);
	if (blob == NULL) {
		return NULL;
	}
	iron_image_pixels_t px;
	bool                ok = iron_decode_image(blob, path, &px);
	data_delete_blob(path);
	if (!ok || px.format != GPU_TEXTURE_FORMAT_RGBA32 || px.width <= 0 || px.height <= 0) {
		return NULL;
	}
	i32  n        = px.width * px.height;
	bool opaque   = true;
	f64  lum_mean = 0.0;
	for (i32 i = 0; i < n; ++i) {
		u8 *p = px.data + i * 4;
		if (p[3] < 250) {
			opaque = false;
		}
		lum_mean += (p[0] * 0.299 + p[1] * 0.587 + p[2] * 0.114) / 255.0;
	}
	lum_mean /= n;
	for (i32 i = 0; i < n; ++i) {
		u8 *p = px.data + i * 4;
		if (opaque) {
			f32 l = (p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f);
			p[3]  = (u8)(lum_mean > 0.5 ? 255.0f - l : l);
		}
		p[0] = p[1] = p[2] = 255;
	}
	return gpu_create_texture_from_pixels(&px);
}
