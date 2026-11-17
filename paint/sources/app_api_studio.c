// app_api_studio.h, #included by main.c after app_api.c and ui/ui_material_studio.c: it reads the studio's state
// (studio_material, studio_sel, studio_stop, its tiles) and edits through its undo (studio_script_canvas).

#include "app_api_studio.h"

static ui_node_canvas_t *app_studio_canvas() {
	return material_studio_show ? studio_canvas() : NULL;
}

// Graph mode: the canvas when it is a studio graph, the selected layer kept in range
static ui_node_canvas_t *app_studio_graph() {
	ui_node_canvas_t *c = app_studio_canvas();
	if (c == NULL || studio_vars) {
		return NULL;
	}
	i32 count = studio_graph_layers(c);
	if (studio_sel >= count) {
		studio_sel = count - 1;
	}
	if (studio_sel < 0) {
		studio_sel = 0;
	}
	return c;
}

static uint32_t app_hash_f64(uint32_t h, f64 v) {
	uint64_t u;
	memcpy(&u, &v, sizeof(u));
	return app_hash(app_hash(h, (uint32_t)u), (uint32_t)(u >> 32));
}

void app_studio_get_state(app_studio_state_t *out) {
	*out                = (app_studio_state_t){0};
	ui_node_canvas_t *c = app_studio_canvas();
	if (!app_mat_project_ready() || c == NULL) {
		return;
	}
	out->open          = true;
	out->material_id   = studio_material->id;
	out->variables     = studio_vars;
	out->tiles_version = studio_tiles_version;
	out->sphere        = studio_sphere;
	out->name          = app_str(c->name);
	uint32_t h         = APP_HASH_SEED;
	h                  = app_hash(h, studio_hash_struct(c));
	h                  = app_hash_f64(h, studio_hash_values(c));
	h                  = app_hash(h, (uint32_t)studio_sel);
	h                  = app_hash(h, (uint32_t)studio_stop);
	h                  = app_hash(h, studio_vars ? 1 : 0);
	h                  = app_hash(h, studio_thumbs_made ? 1 : 0);
	h                  = app_hash_str(h, c->name);
	out->signature     = h;
}

void app_studio_set_preview_rect(float x, float y, float w, float h) {
	if (studio_view[0] != x || studio_view[1] != y || studio_view[2] != w || studio_view[3] != h) {
		studio_view[0] = x;
		studio_view[1] = y;
		studio_view[2] = w;
		studio_view[3] = h;
		studio_redraw();
		iron_delay_idle_sleep();
	}
}

void app_studio_toggle_sphere(void) {
	studio_sphere      = !studio_sphere;
	studio_need_render = true;
	iron_delay_idle_sleep();
}

void app_studio_save(void) {
	material_studio_save();
}

void app_studio_cancel(void) {
	material_studio_cancel();
}

void app_studio_edit_nodes(void) {
	material_studio_edit_nodes();
}

void app_studio_edit_begin(void) {
	if (app_studio_canvas() == NULL) {
		return;
	}
	if (studio_need_commit && !studio_native_drag) {
		studio_commit(); // A tap of this frame stays its own step
	}
	studio_begin_edit();
	studio_native_drag = true;
}

void app_studio_edit_end(void) {
	studio_native_drag = false;
	if (!studio_need_commit) {
		studio_undo_canvas = NULL; // Nothing changed, the next edit takes its own snapshot
	}
	iron_delay_idle_sleep();
}

// Presets and layers

int32_t app_studio_preset_count(void) {
	return studio_preset_count();
}

const char *app_studio_preset_name(int32_t i) {
	return i >= 0 && i < studio_preset_count() ? app_str(studio_preset_name(i)) : "";
}

void app_studio_apply_preset(int32_t i) {
	if (app_studio_graph() != NULL && i >= 0 && i < studio_preset_count()) {
		material_studio_preset(i);
		studio_stop = 0;
	}
}

int32_t app_studio_layer_count(void) {
	ui_node_canvas_t *c = app_studio_graph();
	return c != NULL ? studio_graph_layers(c) : 0;
}

int32_t app_studio_layer_max(void) {
	return STUDIO_LAYERS;
}

bool app_studio_layer(int32_t k, app_studio_layer_t *out) {
	ui_node_canvas_t *c = app_studio_graph();
	if (c == NULL || k < 0 || k >= studio_graph_layers(c)) {
		return false;
	}
	out->name     = app_str(studio_layer_name(c, k));
	out->subtitle = app_str(studio_layer_subtitle(c, k));
	out->selected = k == studio_sel;
	return true;
}

void app_studio_layer_select(int32_t k) {
	if (app_studio_graph() != NULL && k >= 0 && k < app_studio_layer_count() && k != studio_sel) {
		studio_sel  = k;
		studio_stop = 0;
	}
}

int32_t app_studio_layer_preset_count(void) {
	return studio_layer_preset_count();
}

const char *app_studio_layer_preset_name(int32_t i) {
	return i >= 0 && i < studio_layer_preset_count() ? app_str(studio_layer_preset_name(i)) : "";
}

int32_t app_studio_layer_add(int32_t preset) {
	if (app_studio_graph() == NULL || app_studio_layer_count() >= STUDIO_LAYERS) {
		return -1;
	}
	i32 k = material_studio_add_layer(preset < 0 ? -1 : preset);
	studio_stop = 0;
	return k;
}

void app_studio_layer_remove(int32_t k) {
	if (app_studio_graph() == NULL || k <= 0 || k >= app_studio_layer_count()) {
		return;
	}
	ui_node_canvas_t *c = studio_script_canvas();
	studio_graph_remove_layer(c, k);
	if (studio_sel >= k) {
		studio_sel = studio_sel - 1 < 0 ? 0 : studio_sel - 1;
	}
	studio_stop = 0;
}

void app_studio_layer_move(int32_t k, int32_t to) {
	i32 count = app_studio_layer_count();
	if (app_studio_graph() == NULL || k <= 0 || k >= count) {
		return;
	}
	to = to < 1 ? 1 : to > count - 1 ? count - 1 : to;
	if (to == k) {
		return;
	}
	ui_node_canvas_t *c   = studio_script_canvas();
	bool              sel = studio_sel == k;
	while (k != to) {
		i32 dir = to > k ? 1 : -1;
		studio_graph_move_layer(c, k, dir);
		k += dir;
	}
	if (sel) {
		studio_sel = to;
	}
}

int32_t app_studio_layer_thumbnail(int32_t k, int32_t max_size, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	if (app_studio_graph() == NULL || k < 0 || k >= STUDIO_LAYERS || studio_layer_tile[k] == NULL) {
		return 0;
	}
	return app_read_rgba8(studio_layer_tile[k], max_size, true, out, capacity, w, h);
}

// Patterns

int32_t app_studio_pattern_count(void) {
	return pattern_node_count() + 1;
}

const char *app_studio_pattern_name(int32_t i) {
	if (i == 0) {
		return app_str(tr("None"));
	}
	return i > 0 && i <= pattern_node_count() ? app_str(pattern_node_name(i - 1)) : "";
}

int32_t app_studio_pattern_current(void) {
	ui_node_canvas_t *c   = app_studio_graph();
	ui_node_t        *pat = c != NULL ? studio_graph_node(c, studio_sel, STUDIO_ROLE_PATTERN) : NULL;
	for (i32 i = 0; pat != NULL && i < pattern_node_count(); ++i) {
		if (string_equals(pat->type, pattern_node_type(i))) {
			return i + 1;
		}
	}
	return 0;
}

void app_studio_pattern_set(int32_t i) {
	if (app_studio_graph() == NULL || i < 0 || i > pattern_node_count() || i == app_studio_pattern_current()) {
		return;
	}
	ui_node_canvas_t *c = studio_script_canvas();
	studio_graph_set_pattern(c, studio_sel, i == 0 ? NULL : pattern_node_type(i - 1));
}

int32_t app_studio_pattern_thumbnail(int32_t i, int32_t max_size, uint8_t *out, int32_t capacity, int32_t *w, int32_t *h) {
	if (!studio_thumbs_made) {
		return 0;
	}
	gpu_texture_t *t = i == 0 ? studio_thumb_none : i > 0 && i - 1 < STUDIO_THUMBS ? studio_thumbs[i - 1] : NULL;
	return t != NULL ? app_read_rgba8(t, max_size, true, out, capacity, w, h) : 0;
}

// Colors

static ui_node_t *app_studio_ramp_node() {
	ui_node_canvas_t *c = app_studio_graph();
	return c != NULL ? studio_graph_node(c, studio_sel, STUDIO_ROLE_RAMP) : NULL;
}

static f32_array_t *app_studio_ramp_values() {
	ui_node_t *ramp = app_studio_ramp_node();
	return ramp != NULL ? ramp->buttons->buffer[0]->default_value : NULL;
}

int32_t app_studio_ramp_count(void) {
	f32_array_t *v = app_studio_ramp_values();
	i32          sorted[STUDIO_RAMP_MAX];
	return v != NULL ? studio_ramp_sorted(v, sorted) : 0;
}

int32_t app_studio_ramp_max(void) {
	return STUDIO_RAMP_MAX;
}

bool app_studio_ramp_stop(int32_t i, app_studio_stop_t *out) {
	f32_array_t *v = app_studio_ramp_values();
	if (v == NULL) {
		return false;
	}
	i32 sorted[STUDIO_RAMP_MAX];
	i32 len = studio_ramp_sorted(v, sorted);
	if (i < 0 || i >= len) {
		return false;
	}
	i32 s         = sorted[i];
	out->index    = s;
	out->r        = v->buffer[s * 5 + 0];
	out->g        = v->buffer[s * 5 + 1];
	out->b        = v->buffer[s * 5 + 2];
	out->pos      = v->buffer[s * 5 + 4];
	out->selected = s == studio_stop;
	return true;
}

bool app_studio_ramp_plain(void) {
	ui_node_canvas_t *c = app_studio_graph();
	return c != NULL && studio_graph_node(c, studio_sel, STUDIO_ROLE_PATTERN) == NULL;
}

bool app_studio_ramp_hard(void) {
	ui_node_t *ramp = app_studio_ramp_node();
	return ramp != NULL && ramp->buttons->buffer[0]->data->buffer[0] == 1;
}

void app_studio_ramp_set_hard(bool hard) {
	if (app_studio_ramp_node() == NULL || app_studio_ramp_hard() == hard) {
		return;
	}
	studio_script_canvas();
	app_studio_ramp_node()->buttons->buffer[0]->data->buffer[0] = hard ? 1 : 0;
}

void app_studio_ramp_select(int32_t index) {
	f32_array_t *v = app_studio_ramp_values();
	if (v != NULL && index >= 0 && index < (i32)v->length / 5) {
		studio_stop = index;
	}
}

void app_studio_ramp_set_color(int32_t index, float r, float g, float b) {
	f32_array_t *v = app_studio_ramp_values();
	if (v == NULL || index < 0 || index >= (i32)v->length / 5) {
		return;
	}
	studio_script_canvas();
	v->buffer[index * 5 + 0] = r;
	v->buffer[index * 5 + 1] = g;
	v->buffer[index * 5 + 2] = b;
}

void app_studio_ramp_set_pos(int32_t index, float pos) {
	f32_array_t *v = app_studio_ramp_values();
	if (v == NULL || index < 0 || index >= (i32)v->length / 5) {
		return;
	}
	pos = pos < 0.0f ? 0.0f : pos > 1.0f ? 1.0f : pos;
	if (v->buffer[index * 5 + 4] != pos) {
		studio_script_canvas();
		v->buffer[index * 5 + 4] = pos;
	}
}

void app_studio_ramp_add(void) {
	f32_array_t *v = app_studio_ramp_values();
	i32          sorted[STUDIO_RAMP_MAX];
	i32          len = v != NULL ? studio_ramp_sorted(v, sorted) : 0;
	if (len == 0 || len >= STUDIO_RAMP_MAX) {
		return;
	}
	studio_script_canvas();
	i32 at = 0;
	for (i32 i = 0; i < len; ++i) {
		if (sorted[i] == studio_stop) {
			at = i;
		}
	}
	i32 a = sorted[at];
	i32 b = at + 1 < len ? sorted[at + 1] : sorted[at];
	f32 p = a == b ? fminf(v->buffer[a * 5 + 4] + 0.2, 1.0) : (v->buffer[a * 5 + 4] + v->buffer[b * 5 + 4]) * 0.5;
	f32 col[3];
	studio_ramp_color_at(v, sorted, len, p, col);
	f32_array_push(v, col[0]);
	f32_array_push(v, col[1]);
	f32_array_push(v, col[2]);
	f32_array_push(v, 1.0);
	f32_array_push(v, p);
	studio_stop = len;
}

void app_studio_ramp_remove(void) {
	f32_array_t *v = app_studio_ramp_values();
	if (v == NULL || v->length / 5 <= 1 || studio_stop < 0 || studio_stop >= (i32)v->length / 5) {
		return;
	}
	studio_script_canvas();
	for (i32 i = studio_stop * 5; i + 5 < (i32)v->length; ++i) {
		v->buffer[i] = v->buffer[i + 5];
	}
	v->length -= 5;
	studio_stop = 0;
}

// Controls. The list is made again by app_studio_control_count, set and press find a control by its key.

#define APP_STUDIO_CONTROLS_MAX 64
#define APP_STUDIO_KEY_VALUE    0x100  // + studio_value_t
#define APP_STUDIO_KEY_INPUT    0x200  // + input of the pattern node
#define APP_STUDIO_KEY_SHUFFLE  0x300
#define APP_STUDIO_KEY_MAPPING  0x301
#define APP_STUDIO_KEY_MASK     0x302
#define APP_STUDIO_KEY_BLEND    0x303
#define APP_STUDIO_KEY_CHANNEL  0x310  // + channel
#define APP_STUDIO_KEY_VARIABLE 0x1000 // + index in variable_list

typedef struct {
	app_studio_control_t c;
	char                 options[512];
} app_studio_control_slot_t;

static app_studio_control_slot_t app_studio_controls[APP_STUDIO_CONTROLS_MAX];
static i32                       app_studio_controls_n = 0;

static app_studio_control_t *app_studio_push(uint32_t key, i32 kind, i32 section, char *section_name, char *name) {
	if (app_studio_controls_n >= APP_STUDIO_CONTROLS_MAX) {
		return NULL;
	}
	app_studio_control_slot_t *s = &app_studio_controls[app_studio_controls_n++];
	s->options[0]                = '\0';
	s->c                         = (app_studio_control_t){0};
	s->c.key                     = key;
	s->c.kind                    = kind;
	s->c.section                 = section;
	s->c.section_name            = app_str(section_name);
	s->c.name                    = app_str(name);
	s->c.options                 = s->options;
	s->c.max                     = 1.0f;
	return &s->c;
}

static void app_studio_push_slider(uint32_t key, i32 section, char *section_name, char *name, f32 min, f32 max, f32 value) {
	app_studio_control_t *c = app_studio_push(key, APP_STUDIO_SLIDER, section, section_name, name);
	if (c != NULL) {
		c->min   = min;
		c->max   = max;
		c->value = value;
	}
}

static void app_studio_push_value(ui_node_canvas_t *g, i32 value, i32 section, char *section_name, char *name, f32 min, f32 max) {
	app_studio_push_slider(APP_STUDIO_KEY_VALUE + value, section, section_name, name, min, max, studio_graph_get(g, studio_sel, value));
}

static void app_studio_push_toggle(uint32_t key, i32 section, char *section_name, char *name, bool on) {
	app_studio_control_t *c = app_studio_push(key, APP_STUDIO_TOGGLE, section, section_name, name);
	if (c != NULL) {
		c->value = on ? 1.0f : 0.0f;
	}
}

static void app_studio_push_choice(uint32_t key, i32 section, char *section_name, char *name, char **options, i32 n, i32 picked) {
	app_studio_control_t *c = app_studio_push(key, APP_STUDIO_CHOICE, section, section_name, name);
	if (c == NULL) {
		return;
	}
	c->value  = picked;
	char *buf = app_studio_controls[app_studio_controls_n - 1].options;
	for (i32 i = 0; i < n; ++i) {
		strncat(buf, options[i], 510 - strlen(buf));
		if (i + 1 < n) {
			strncat(buf, "\n", 510 - strlen(buf));
		}
	}
}

static void app_studio_graph_controls(ui_node_canvas_t *g) {
	i32        k       = studio_sel;
	ui_node_t *pat     = studio_graph_node(g, k, STUDIO_ROLE_PATTERN);
	bool       pattern = pat != NULL;

	char *sp = k == 0 ? tr("Base Pattern") : tr("Pattern");
	if (pattern) { // The pattern node's inputs by name: the shared ones, then its own
		for (i32 i = 1; i < pat->inputs->length; ++i) {
			ui_node_socket_t *s = pat->inputs->buffer[i];
			if (i == 5 || (i == 7 && pat->inputs->buffer[6]->default_value->buffer[0] <= 0.0)) {
				continue; // Seed has the Shuffle button, Distort Scale shows with Distort
			}
			app_studio_push_slider(APP_STUDIO_KEY_INPUT + i, APP_STUDIO_SECTION_PATTERN, sp, tr(s->name), s->min, s->max, s->default_value->buffer[0]);
		}
		app_studio_push(APP_STUDIO_KEY_SHUFFLE, APP_STUDIO_BUTTON, APP_STUDIO_SECTION_PATTERN, sp, tr("Shuffle"));
		char *mappings[2] = {tr("UV"), tr("Triplanar")};
		app_studio_push_choice(APP_STUDIO_KEY_MAPPING, APP_STUDIO_SECTION_PATTERN, sp, tr("Mapping"), mappings, 2,
		                       (i32)pat->buttons->buffer[0]->default_value->buffer[0]);
	}

	char *sc = tr("Colors");
	if (pattern) {
		app_studio_push_value(g, STUDIO_VALUE_CELL_VAR, APP_STUDIO_SECTION_COLORS, sc, tr("Cell Variation"), 0.0, 1.0);
		app_studio_push_toggle(APP_STUDIO_KEY_VALUE + STUDIO_VALUE_CELL_COLORS, APP_STUDIO_SECTION_COLORS, sc, tr("A random color of the ramp for each cell"),
		                       studio_graph_get(g, k, STUDIO_VALUE_CELL_COLORS) > 0.5);
	}

	char *ss = tr("Surface");
	app_studio_push_value(g, STUDIO_VALUE_ROUGHNESS, APP_STUDIO_SECTION_SURFACE, ss, tr("Roughness"), 0.0, 1.0);
	if (pattern) {
		app_studio_push_value(g, STUDIO_VALUE_ROUGH_VAR, APP_STUDIO_SECTION_SURFACE, ss, tr("Roughness Variation"), 0.0, 1.0);
	}
	app_studio_push_value(g, STUDIO_VALUE_METALLIC, APP_STUDIO_SECTION_SURFACE, ss, tr("Metallic"), 0.0, 1.0);
	if (pattern) {
		app_studio_push_value(g, STUDIO_VALUE_METAL_VAR, APP_STUDIO_SECTION_SURFACE, ss, tr("Metallic Variation"), 0.0, 1.0);
		app_studio_push_value(g, STUDIO_VALUE_DEPTH, APP_STUDIO_SECTION_SURFACE, ss, tr("Depth"), 0.0, 1.0);
		app_studio_push_value(g, STUDIO_VALUE_CAVITY, APP_STUDIO_SECTION_SURFACE, ss, tr("Cavity Darkening"), 0.0, 1.0);
		app_studio_push_value(g, STUDIO_VALUE_CUTOUT, APP_STUDIO_SECTION_SURFACE, ss, tr("Cutout"), 0.0, 1.0);
	}
	app_studio_push_value(g, STUDIO_VALUE_EMISSION, APP_STUDIO_SECTION_SURFACE, ss, tr("Glow"), 0.0, 1.0);
	app_studio_push_value(g, STUDIO_VALUE_SUBSURFACE, APP_STUDIO_SECTION_SURFACE, ss, tr("Subsurface"), 0.0, 1.0);

	if (k == 0) {
		return;
	}
	char *sm                       = tr("Mask");
	char *masks[STUDIO_MASK_COUNT] = {tr("Full"), tr("Edges"), tr("Cavities"), tr("Facing Up"), tr("Pattern")};
	i32   mask                     = studio_graph_mask(g, k);
	app_studio_push_choice(APP_STUDIO_KEY_MASK, APP_STUDIO_SECTION_MASK, sm, tr("Where"), masks, pattern ? STUDIO_MASK_COUNT : STUDIO_MASK_COUNT - 1, mask);
	if (mask != STUDIO_MASK_FULL) {
		app_studio_push_value(g, STUDIO_VALUE_MASK_AMOUNT, APP_STUDIO_SECTION_MASK, sm, tr("Amount"), 0.0, 1.0);
		app_studio_push_value(g, STUDIO_VALUE_MASK_SOFT, APP_STUDIO_SECTION_MASK, sm, tr("Softness"), 0.0, 1.0);
		app_studio_push_value(g, STUDIO_VALUE_MASK_BREAKUP, APP_STUDIO_SECTION_MASK, sm, tr("Breakup"), 0.0, 1.0);
		if (studio_graph_get(g, k, STUDIO_VALUE_MASK_BREAKUP) > 0.0) {
			app_studio_push_value(g, STUDIO_VALUE_MASK_SCALE, APP_STUDIO_SECTION_MASK, sm, tr("Breakup Scale"), 1.0, 200.0);
		}
		if (mask == STUDIO_MASK_PATTERN) {
			app_studio_push_toggle(APP_STUDIO_KEY_VALUE + STUDIO_VALUE_MASK_INVERT, APP_STUDIO_SECTION_MASK, sm, tr("Where the pattern is low"),
			                       studio_graph_get(g, k, STUDIO_VALUE_MASK_INVERT) > 0.5);
		}
	}
	app_studio_push_value(g, STUDIO_VALUE_OPACITY, APP_STUDIO_SECTION_MASK, sm, tr("Opacity"), 0.0, 1.0);

	char *sh          = tr("Changes");
	char *channels[4] = {tr("Color"), tr("Roughness"), tr("Metallic"), tr("Height")};
	for (i32 ch = 0; ch < 4; ++ch) {
		app_studio_push_toggle(APP_STUDIO_KEY_CHANNEL + ch, APP_STUDIO_SECTION_CHANGES, sh, channels[ch], studio_graph_channel(g, k, ch));
	}
	if (studio_graph_channel(g, k, 3)) { // How the height goes over the layers below
		char *blends[3] = {tr("Mix"), tr("Add"), tr("Carve")};
		app_studio_push_choice(APP_STUDIO_KEY_BLEND, APP_STUDIO_SECTION_CHANGES, sh, tr("Height"), blends, 3, studio_graph_height_blend(g, k));
	}
}

static void app_studio_variable_controls(ui_node_canvas_t *c) {
	ui_node_t_array_t *vars = variable_list(c);
	for (i32 i = 0; i < vars->length; ++i) {
		ui_node_t   *v   = vars->buffer[i];
		f32_array_t *arr = variable_value_array(v);
		if (arr == NULL || arr->length == 0) {
			continue;
		}
		i32      kind    = variable_kind(v);
		uint32_t key     = APP_STUDIO_KEY_VARIABLE + i;
		i32      section = 100 + variable_group(v);
		char    *sname   = variable_group_name(variable_group(v));
		char    *label   = variable_name(v);
		if (string_equals(variable_id(v), "shuffle")) {
			continue; // Not offered, the variation the material came with stays
		}
		if (kind == 0 || kind == 1) { // Number, Whole Number
			f32 min, max, step;
			variable_range(v, &min, &max, &step);
			app_studio_control_t *ctl = app_studio_push(key, APP_STUDIO_SLIDER, section, sname, label);
			if (ctl != NULL) {
				ctl->min     = min;
				ctl->max     = max;
				ctl->step    = kind == 1 ? 1.0f : 0.0f;
				ctl->percent = variable_unit(v) == 1;
				ctl->value   = arr->buffer[0];
			}
		}
		else if (kind == 2) {
			app_studio_push_toggle(key, section, sname, label, arr->buffer[0] != 0);
		}
		else if (kind == 3) {
			string_array_t *options = variable_options(v);
			if (options->length > 0) {
				app_studio_push_choice(key, section, sname, label, (char **)options->buffer, options->length, (i32)arr->buffer[0]);
			}
		}
		else if (kind == 4 && arr->length >= 3) {
			app_studio_control_t *ctl = app_studio_push(key, APP_STUDIO_COLOR, section, sname, label);
			if (ctl != NULL) {
				ctl->color[0] = arr->buffer[0];
				ctl->color[1] = arr->buffer[1];
				ctl->color[2] = arr->buffer[2];
			}
		}
		else if (kind == 5 && arr->length >= 10) { // Gradient: its colors, a swatch per stop in order of position
			app_studio_control_t *ctl = app_studio_push(key, APP_STUDIO_GRADIENT, section, sname, label);
			i32                   sorted[STUDIO_RAMP_MAX];
			if (ctl != NULL) {
				ctl->stops = studio_ramp_sorted(arr, sorted);
			}
		}
	}
}

int32_t app_studio_control_count(void) {
	app_studio_controls_n = 0;
	ui_node_canvas_t *c   = app_studio_canvas();
	if (c == NULL) {
		return 0;
	}
	if (studio_vars) {
		app_studio_variable_controls(c);
	}
	else if (app_studio_graph() != NULL) {
		app_studio_graph_controls(c);
	}
	return app_studio_controls_n;
}

bool app_studio_control(int32_t i, app_studio_control_t *out) {
	if (i < 0 || i >= app_studio_controls_n) {
		return false;
	}
	*out = app_studio_controls[i].c;
	return true;
}

static ui_node_t *app_studio_variable(uint32_t key) {
	ui_node_canvas_t *c = app_studio_canvas();
	if (c == NULL || !studio_vars || key < APP_STUDIO_KEY_VARIABLE) {
		return NULL;
	}
	ui_node_t_array_t *vars = variable_list(c);
	i32                i    = (i32)(key - APP_STUDIO_KEY_VARIABLE);
	return i < vars->length ? vars->buffer[i] : NULL;
}

void app_studio_control_set(uint32_t key, float value) {
	ui_node_t *v = app_studio_variable(key);
	if (v != NULL) {
		f32_array_t *arr = variable_value_array(v);
		if (arr == NULL || arr->length == 0) {
			return;
		}
		if (variable_kind(v) == 1) {
			value = math_floor(value + 0.5);
		}
		if (arr->buffer[0] != value) {
			studio_script_canvas();
			arr->buffer[0] = value;
		}
		return;
	}
	ui_node_canvas_t *g = app_studio_graph();
	if (g == NULL) {
		return;
	}
	i32 k = studio_sel;
	if (key >= APP_STUDIO_KEY_VALUE && key < APP_STUDIO_KEY_VALUE + STUDIO_VALUE_COUNT) {
		i32 value_id = key - APP_STUDIO_KEY_VALUE;
		if (studio_graph_get(g, k, value_id) != value) {
			studio_script_canvas();
			studio_graph_set(g, k, value_id, value);
		}
	}
	else if (key > APP_STUDIO_KEY_INPUT && key < APP_STUDIO_KEY_SHUFFLE) {
		ui_node_t *pat = studio_graph_node(g, k, STUDIO_ROLE_PATTERN);
		i32        i   = key - APP_STUDIO_KEY_INPUT;
		if (pat != NULL && i < pat->inputs->length && pat->inputs->buffer[i]->default_value->buffer[0] != value) {
			studio_script_canvas();
			pat->inputs->buffer[i]->default_value->buffer[0] = value;
		}
	}
	else if (key == APP_STUDIO_KEY_MAPPING) {
		ui_node_t *pat = studio_graph_node(g, k, STUDIO_ROLE_PATTERN);
		if (pat != NULL && pat->buttons->buffer[0]->default_value->buffer[0] != value) {
			studio_script_canvas();
			pat->buttons->buffer[0]->default_value->buffer[0] = value != 0 ? 1 : 0;
		}
	}
	else if (key == APP_STUDIO_KEY_MASK) {
		i32 mask = (i32)value;
		if (k > 0 && mask >= 0 && mask < STUDIO_MASK_COUNT && mask != studio_graph_mask(g, k)) {
			studio_script_canvas();
			studio_graph_set_mask(g, k, mask);
		}
	}
	else if (key == APP_STUDIO_KEY_BLEND) {
		i32 blend = (i32)value;
		if (k > 0 && blend >= 0 && blend < 3 && blend != studio_graph_height_blend(g, k)) {
			studio_script_canvas();
			studio_graph_set_height_blend(g, k, blend);
		}
	}
	else if (key >= APP_STUDIO_KEY_CHANNEL && key < APP_STUDIO_KEY_CHANNEL + 4) {
		i32  ch = key - APP_STUDIO_KEY_CHANNEL;
		bool on = value != 0;
		if (k > 0 && on != studio_graph_channel(g, k, ch)) {
			studio_script_canvas();
			studio_graph_set_channel(g, k, ch, on);
		}
	}
}

void app_studio_control_set_color(uint32_t key, float r, float g, float b) {
	ui_node_t   *v   = app_studio_variable(key);
	f32_array_t *arr = v != NULL ? variable_value_array(v) : NULL;
	if (arr == NULL || arr->length < 3) {
		return;
	}
	studio_script_canvas();
	arr->buffer[0] = r;
	arr->buffer[1] = g;
	arr->buffer[2] = b;
}

// The pattern's Shuffle: a new seed
void app_studio_control_press(uint32_t key) {
	ui_node_canvas_t *g   = app_studio_graph();
	ui_node_t        *pat = g != NULL && key == APP_STUDIO_KEY_SHUFFLE ? studio_graph_node(g, studio_sel, STUDIO_ROLE_PATTERN) : NULL;
	if (pat != NULL) {
		studio_script_canvas();
		pat->inputs->buffer[5]->default_value->buffer[0] = (f32)iron_random_get_max(1000);
	}
}

bool app_studio_control_stop(uint32_t key, int32_t i, float *rgb_pos) {
	ui_node_t   *v   = app_studio_variable(key);
	f32_array_t *arr = v != NULL ? variable_value_array(v) : NULL;
	if (arr == NULL || arr->length < 10) {
		return false;
	}
	i32 sorted[STUDIO_RAMP_MAX];
	i32 len = studio_ramp_sorted(arr, sorted);
	if (i < 0 || i >= len) {
		return false;
	}
	i32 s      = sorted[i];
	rgb_pos[0] = arr->buffer[s * 5 + 0];
	rgb_pos[1] = arr->buffer[s * 5 + 1];
	rgb_pos[2] = arr->buffer[s * 5 + 2];
	rgb_pos[3] = arr->buffer[s * 5 + 4];
	return true;
}

void app_studio_control_set_stop(uint32_t key, int32_t i, float r, float g, float b) {
	ui_node_t   *v   = app_studio_variable(key);
	f32_array_t *arr = v != NULL ? variable_value_array(v) : NULL;
	if (arr == NULL || arr->length < 10) {
		return;
	}
	i32 sorted[STUDIO_RAMP_MAX];
	i32 len = studio_ramp_sorted(arr, sorted);
	if (i < 0 || i >= len) {
		return;
	}
	studio_script_canvas();
	i32 s                = sorted[i];
	arr->buffer[s * 5 + 0] = r;
	arr->buffer[s * 5 + 1] = g;
	arr->buffer[s * 5 + 2] = b;
}
