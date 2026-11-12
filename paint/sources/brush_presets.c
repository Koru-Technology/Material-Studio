// Brush presets: the brush settings and the material of a tool, saved by name and kept across projects.
// brush_presets.json in the app's save folder holds the settings as objects "p0", "p1", ..., the material of each
// preset is exported next to it as brush_presets/<id>.arm, so it can be brought into a project that does not have it.

#include "global.h"

typedef struct brush_preset {
	char *name;
	i32   id;      // Names the material file
	i32   tool;
	i32   builtin; // Built-in brush, -1 keeps the current brush
	i32   blending;
	f32   radius;
	f32   opacity;
	f32   hardness;
	f32   flow;
	f32   spacing;
	f32   scatter;
	f32   jitter_size;
	f32   jitter_angle;
	f32   jitter_opacity;
	f32   smoothing;
	f32   strength;
	f32   angle;
	i32   dyn_size;
	i32   dyn_opacity;
	i32   dyn_flow;
	i32   dyn_hardness;
	i32   dyn_angle;
	char *material; // Name, NULL without
	f32   uv_scale;
	f32   uv_angle;
} brush_preset_t;

#define BRUSH_PRESETS_MAX 256

static brush_preset_t *brush_presets[BRUSH_PRESETS_MAX];
static i32             brush_presets_n       = 0;
static i32             brush_presets_next_id = 0;
static bool            brush_presets_loaded  = false;

static char *brush_presets_file() {
	return string("%sbrush_presets.json", iron_internal_save_path());
}

static char *brush_presets_dir() {
	return string("%sbrush_presets", iron_internal_save_path());
}

static char *brush_presets_material_file(brush_preset_t *p) {
	return string("%s%s%d.arm", brush_presets_dir(), PATH_SEP, p->id);
}

static char *brush_presets_get(any_map_t *m, i32 i, char *key) {
	return any_map_get(m, string_tmp("p%d.%s", i, key));
}

static f32 brush_presets_get_f32(any_map_t *m, i32 i, char *key, f32 def) {
	char *s = brush_presets_get(m, i, key);
	return s != NULL ? (f32)atof(s) : def;
}

static i32 brush_presets_get_i32(any_map_t *m, i32 i, char *key, i32 def) {
	char *s = brush_presets_get(m, i, key);
	return s != NULL ? atoi(s) : def;
}

static void brush_presets_load() {
	if (brush_presets_loaded) {
		return;
	}
	brush_presets_loaded = true;
	char *path           = brush_presets_file();
	if (!iron_file_exists(path)) {
		return;
	}
	buffer_t *blob = data_get_blob(path);
	if (blob == NULL) {
		return;
	}
	char      *s = sys_buffer_to_string(blob);
	any_map_t *m = json_parse_to_map(s);
	free(s);
	data_delete_blob(path);

	char *count           = any_map_get(m, "count");
	char *next_id         = any_map_get(m, "next_id");
	i32   n               = count != NULL ? atoi(count) : 0;
	brush_presets_next_id = next_id != NULL ? atoi(next_id) : 0;
	for (i32 i = 0; i < n && brush_presets_n < BRUSH_PRESETS_MAX; ++i) {
		char *name = brush_presets_get(m, i, "name");
		if (name == NULL) {
			continue;
		}
		brush_preset_t *p = calloc(1, sizeof(brush_preset_t));
		p->name           = string_copy(name);
		p->id             = brush_presets_get_i32(m, i, "id", i);
		p->tool           = brush_presets_get_i32(m, i, "tool", TOOL_TYPE_BRUSH);
		p->builtin        = brush_presets_get_i32(m, i, "builtin", -1);
		p->blending       = brush_presets_get_i32(m, i, "blending", BLEND_TYPE_MIX);
		p->radius         = brush_presets_get_f32(m, i, "radius", 0.5);
		p->opacity        = brush_presets_get_f32(m, i, "opacity", 1.0);
		p->hardness       = brush_presets_get_f32(m, i, "hardness", 1.0);
		p->flow           = brush_presets_get_f32(m, i, "flow", 1.0);
		p->spacing        = brush_presets_get_f32(m, i, "spacing", 0.0);
		p->scatter        = brush_presets_get_f32(m, i, "scatter", 0.0);
		p->jitter_size    = brush_presets_get_f32(m, i, "jitter_size", 0.0);
		p->jitter_angle   = brush_presets_get_f32(m, i, "jitter_angle", 0.0);
		p->jitter_opacity = brush_presets_get_f32(m, i, "jitter_opacity", 0.0);
		p->smoothing      = brush_presets_get_f32(m, i, "smoothing", 0.0);
		p->strength       = brush_presets_get_f32(m, i, "strength", 1.0);
		p->angle          = brush_presets_get_f32(m, i, "angle", 0.0);
		p->dyn_size       = brush_presets_get_i32(m, i, "dyn_size", 0);
		p->dyn_opacity    = brush_presets_get_i32(m, i, "dyn_opacity", 0);
		p->dyn_flow       = brush_presets_get_i32(m, i, "dyn_flow", 0);
		p->dyn_hardness   = brush_presets_get_i32(m, i, "dyn_hardness", 0);
		p->dyn_angle      = brush_presets_get_i32(m, i, "dyn_angle", 0);
		char *material    = brush_presets_get(m, i, "material");
		p->material       = material != NULL && material[0] != '\0' ? string_copy(material) : NULL;
		p->uv_scale       = brush_presets_get_f32(m, i, "uv_scale", 1.0);
		p->uv_angle       = brush_presets_get_f32(m, i, "uv_angle", 0.0);
		if (p->id >= brush_presets_next_id) {
			brush_presets_next_id = p->id + 1;
		}
		brush_presets[brush_presets_n++] = p;
	}
}

static void brush_presets_save() {
	json_encode_begin();
	json_encode_i32("count", brush_presets_n);
	json_encode_i32("next_id", brush_presets_next_id);
	for (i32 i = 0; i < brush_presets_n; ++i) {
		brush_preset_t *p = brush_presets[i];
		json_encode_begin_object_key(string_tmp("p%d", i));
		json_encode_string("name", p->name);
		json_encode_i32("id", p->id);
		json_encode_i32("tool", p->tool);
		json_encode_i32("builtin", p->builtin);
		json_encode_i32("blending", p->blending);
		json_encode_f32("radius", p->radius);
		json_encode_f32("opacity", p->opacity);
		json_encode_f32("hardness", p->hardness);
		json_encode_f32("flow", p->flow);
		json_encode_f32("spacing", p->spacing);
		json_encode_f32("scatter", p->scatter);
		json_encode_f32("jitter_size", p->jitter_size);
		json_encode_f32("jitter_angle", p->jitter_angle);
		json_encode_f32("jitter_opacity", p->jitter_opacity);
		json_encode_f32("smoothing", p->smoothing);
		json_encode_f32("strength", p->strength);
		json_encode_f32("angle", p->angle);
		json_encode_i32("dyn_size", p->dyn_size);
		json_encode_i32("dyn_opacity", p->dyn_opacity);
		json_encode_i32("dyn_flow", p->dyn_flow);
		json_encode_i32("dyn_hardness", p->dyn_hardness);
		json_encode_i32("dyn_angle", p->dyn_angle);
		json_encode_string("material", p->material != NULL ? p->material : "");
		json_encode_f32("uv_scale", p->uv_scale);
		json_encode_f32("uv_angle", p->uv_angle);
		json_encode_end_object();
	}
	char     *json   = json_encode_end();
	buffer_t *buffer = sys_string_to_buffer(json);
	iron_file_save_bytes(brush_presets_file(), buffer, 0);
	array_free(buffer);
	free(buffer);
	free(json);
}

i32 brush_presets_count() {
	brush_presets_load();
	return brush_presets_n;
}

char *brush_presets_name(i32 i) {
	brush_presets_load();
	return i >= 0 && i < brush_presets_n ? brush_presets[i]->name : NULL;
}

i32 brush_presets_find(char *name) {
	brush_presets_load();
	for (i32 i = 0; i < brush_presets_n; ++i) {
		if (string_equals(brush_presets[i]->name, name)) {
			return i;
		}
	}
	return -1;
}

static void brush_presets_capture(brush_preset_t *p) {
	p->tool           = g_context->tool;
	p->builtin        = g_context->brush_builtin;
	p->blending       = g_context->brush_blending;
	p->radius         = g_context->brush_radius;
	p->opacity        = g_context->brush_opacity;
	p->hardness       = g_context->brush_hardness;
	p->flow           = g_context->brush_flow;
	p->spacing        = g_context->brush_spacing;
	p->scatter        = g_context->brush_scatter;
	p->jitter_size    = g_context->brush_jitter_size;
	p->jitter_angle   = g_context->brush_jitter_angle;
	p->jitter_opacity = g_context->brush_jitter_opacity;
	p->smoothing      = g_context->brush_smoothing;
	p->strength       = g_context->brush_strength;
	p->angle          = g_context->brush_angle;
	p->dyn_size       = g_config->dyn_size;
	p->dyn_opacity    = g_config->dyn_opacity;
	p->dyn_flow       = g_config->dyn_flow;
	p->dyn_hardness   = g_config->dyn_hardness;
	p->dyn_angle      = g_config->dyn_angle;
	slot_material_t *m = g_context->material;
	p->material        = m != NULL ? string_copy(m->canvas->name) : NULL;
	p->uv_scale        = m != NULL ? m->uv_scale : 1.0;
	p->uv_angle        = m != NULL ? m->uv_angle : 0.0;
	if (m != NULL) {
		iron_create_directory(brush_presets_dir());
		export_arm_run_material(brush_presets_material_file(p)); // Exports the current material
	}
}

// Saves the current tool, brush and material as preset name, a preset of that name is replaced
i32 brush_presets_add(char *name) {
	brush_presets_load();
	if (name == NULL || name[0] == '\0') {
		return -1;
	}
	i32             i = brush_presets_find(name);
	brush_preset_t *p = NULL;
	if (i >= 0) {
		p = brush_presets[i];
	}
	else {
		if (brush_presets_n >= BRUSH_PRESETS_MAX) {
			return -1;
		}
		p                         = calloc(1, sizeof(brush_preset_t));
		p->name                   = string_copy(name);
		p->id                     = brush_presets_next_id++;
		i                         = brush_presets_n;
		brush_presets[brush_presets_n++] = p;
	}
	brush_presets_capture(p);
	brush_presets_save();
	return i;
}

static i32 brush_presets_material_index(char *name) {
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		if (string_equals(g_project->_->materials->buffer[i]->canvas->name, name)) {
			return i;
		}
	}
	return -1;
}

// Restores the preset: brush, its settings, the material (brought in from the preset when the project lacks it) and the tool
void brush_presets_apply(i32 i) {
	brush_presets_load();
	if (i < 0 || i >= brush_presets_n) {
		return;
	}
	brush_preset_t *p = brush_presets[i];
	if (p->builtin >= 0) {
		builtin_brush_use(p->builtin);
	}
	g_context->brush_blending       = p->blending;
	g_context->brush_radius         = p->radius;
	g_context->brush_opacity        = p->opacity;
	g_context->brush_hardness       = p->hardness;
	g_context->brush_flow           = p->flow;
	g_context->brush_spacing        = p->spacing;
	g_context->brush_scatter        = p->scatter;
	g_context->brush_jitter_size    = p->jitter_size;
	g_context->brush_jitter_angle   = p->jitter_angle;
	g_context->brush_jitter_opacity = p->jitter_opacity;
	g_context->brush_smoothing      = p->smoothing;
	g_context->brush_strength       = p->strength;
	g_context->brush_angle          = p->angle;
	g_config->dyn_size              = p->dyn_size;
	g_config->dyn_opacity           = p->dyn_opacity;
	g_config->dyn_flow              = p->dyn_flow;
	g_config->dyn_hardness          = p->dyn_hardness;
	g_config->dyn_angle             = p->dyn_angle;

	if (p->material != NULL) {
		i32 m = brush_presets_material_index(p->material);
		if (m >= 0) {
			slot_material_t *mat = g_project->_->materials->buffer[m];
			mat->uv_scale        = p->uv_scale;
			mat->uv_angle        = p->uv_angle;
			context_select_material(m);
		}
		else {
			char *file = brush_presets_material_file(p);
			if (iron_file_exists(file)) {
				import_arm_run_material(file); // Comes with its scale and rotation, selected
			}
		}
	}

	context_select_tool(p->tool);
	make_material_parse_paint_material(true);
	ui_header_handle->redraws = 2;
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	config_save();
}

void brush_presets_delete(i32 i) {
	brush_presets_load();
	if (i < 0 || i >= brush_presets_n) {
		return;
	}
	brush_preset_t *p    = brush_presets[i];
	char           *file = brush_presets_material_file(p);
	if (iron_file_exists(file)) {
		iron_delete_file(file);
	}
	for (i32 j = i; j < brush_presets_n - 1; ++j) {
		brush_presets[j] = brush_presets[j + 1];
	}
	brush_presets_n--;
	brush_presets_save();
}

void brush_presets_rename(i32 i, char *name) {
	brush_presets_load();
	if (i < 0 || i >= brush_presets_n || name == NULL || name[0] == '\0' || brush_presets_find(name) >= 0) {
		return;
	}
	brush_presets[i]->name = string_copy(name);
	brush_presets_save();
}
