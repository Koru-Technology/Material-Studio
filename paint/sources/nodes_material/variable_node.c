#include "../global.h"

// Variable: a value of the material exported as a control (paint/tools/material_maker/controls.md), as Unity's
// properties: a type, a default value, wired into the graph. The Material Studio shows a slider, switch, menu, swatch or
// gradient for each Variable of a material (ui_material_studio.c); the node editor is where they are added and wired.
//
// Its buttons, found by name (never by position):
//   Type              ENUM, VARIABLE_* below; the output socket follows it (VALUE, RGBA, or GRADIENT)
//   variable_name     the label, in data; the node's name follows it
//   variable_value    the value: [v] (Number, Whole Number, Toggle 0 / 1, Choice index), [r, g, b, a] (Color),
//                     [r, g, b, a, pos] * n with data[0] the interpolation (Gradient, as Material Maker's gradients)
//   variable_range    [min, max, step] of a Number or Whole Number
//   variable_options  the labels of a Choice, separated by commas, in data
//   variable_show     [group, order, unit]: VARIABLE_GROUP_*, its place in the group, VARIABLE_UNIT_*
//   variable_id       the control's id ("brick_rows"), in data, hidden
//
// Native inputs take a Variable by a link like any value. A Material Maker parameter takes one through a parameter
// socket (mm_nodes.c), possibly through Map Range and Select Value nodes: variable_propagate evaluates the chain on the
// CPU (variable_eval: a value the same everywhere, no per pixel input) and writes it into the parameter's button before
// the shader is made, as Material Maker's remote node does.
//
// Select Value: the value of its list at the index given by its input, for a Choice whose targets need other values than
// the option's index ("3, 4, 0").

#define VARIABLE_NUMBER   0
#define VARIABLE_WHOLE    1
#define VARIABLE_TOGGLE   2
#define VARIABLE_CHOICE   3
#define VARIABLE_COLOR    4
#define VARIABLE_GRADIENT 5

#define VARIABLE_MAX_DEPTH 16

static char *variable_types_data  = "Number\nWhole Number\nToggle\nChoice\nColor\nGradient";
static char *variable_groups[]    = {"Color", "Pattern", "Surface", "Detail"};
static char *variable_units[]     = {"None", "Percent", "Degrees", "Count", "Multiplier"};
static char *variable_group_keys[] = {"color", "pattern", "surface", "detail"};
static char *variable_unit_keys[]  = {"", "percent", "degrees", "count", "multiplier"};

static i32 variable_button(ui_node_t *node, char *name) {
	for (i32 i = 0; node != NULL && i < node->buttons->length; ++i) {
		if (string_equals(node->buttons->buffer[i]->name, name)) {
			return i;
		}
	}
	return -1;
}

static ui_node_button_t *variable_but(ui_node_t *node, char *name) {
	i32 i = variable_button(node, name);
	return i >= 0 ? node->buttons->buffer[i] : NULL;
}

static f32 variable_num(ui_node_t *node, char *name, i32 i, f32 dflt) {
	ui_node_button_t *b = variable_but(node, name);
	return b != NULL && b->default_value != NULL && i < b->default_value->length ? b->default_value->buffer[i] : dflt;
}

static void variable_set_num(ui_node_t *node, char *name, i32 i, f32 v) {
	ui_node_button_t *b = variable_but(node, name);
	if (b == NULL) {
		return;
	}
	if (b->default_value == NULL) {
		b->default_value = f32_array_create(0);
	}
	while (b->default_value->length <= i) {
		f32_array_push(b->default_value, 0);
	}
	b->default_value->buffer[i] = v;
}

static char *variable_text(ui_node_t *node, char *name) {
	ui_node_button_t *b = variable_but(node, name);
	return b != NULL && b->data != NULL && b->data->length > 0 ? (char *)b->data->buffer : "";
}

static void variable_set_text(ui_node_t *node, char *name, char *text) {
	ui_node_button_t *b = variable_but(node, name);
	if (b != NULL) {
		b->data = u8_array_create_from_string(text);
	}
}

bool variable_is(ui_node_t *node) {
	return node != NULL && string_equals(node->type, "VARIABLE");
}

i32 variable_kind(ui_node_t *node) {
	i32 k = (i32)variable_num(node, "Type", 0, 0);
	return k < VARIABLE_NUMBER || k > VARIABLE_GRADIENT ? VARIABLE_NUMBER : k;
}

char *variable_name(ui_node_t *node) {
	return variable_text(node, "variable_name");
}

char *variable_id(ui_node_t *node) {
	return variable_text(node, "variable_id");
}

// The labels of a Choice
string_array_t *variable_options(ui_node_t *node) {
	string_array_t *rv   = any_array_create_from_raw((void *[]){}, 0);
	char           *text = variable_text(node, "variable_options");
	char           *p    = text;
	while (*p != 0) {
		char *comma = strchr(p, ',');
		i32   len   = comma != NULL ? (i32)(comma - p) : (i32)strlen(p);
		while (len > 0 && p[0] == ' ') {
			p++;
			len--;
		}
		while (len > 0 && p[len - 1] == ' ') {
			len--;
		}
		char *o = calloc(len + 1, 1);
		memcpy(o, p, len);
		any_array_push(rv, o);
		if (comma == NULL) {
			break;
		}
		p = comma + 1;
	}
	return rv;
}

void variable_range(ui_node_t *node, f32 *min, f32 *max, f32 *step) {
	*min  = variable_num(node, "variable_range", 0, 0.0);
	*max  = variable_num(node, "variable_range", 1, 1.0);
	*step = variable_num(node, "variable_range", 2, 0.0);
	if (*max <= *min) {
		*max = *min + 1.0;
	}
}

i32 variable_group(ui_node_t *node) {
	i32 g = (i32)variable_num(node, "variable_show", 0, 0);
	return g < 0 || g > 3 ? 0 : g;
}

i32 variable_order(ui_node_t *node) {
	return (i32)variable_num(node, "variable_show", 1, 0);
}

i32 variable_unit(ui_node_t *node) {
	i32 u = (i32)variable_num(node, "variable_show", 2, 0);
	return u < 0 || u > 4 ? 0 : u;
}

char *variable_group_name(i32 g) {
	return tr(variable_groups[g < 0 || g > 3 ? 0 : g]);
}

// The value: how many floats (see the header), interp the gradient's interpolation
i32 variable_get(ui_node_t *node, f32 *out, i32 max, i32 *interp) {
	ui_node_button_t *b = variable_but(node, "variable_value");
	if (b == NULL || b->default_value == NULL) {
		return 0;
	}
	i32 k = variable_kind(node);
	i32 n = k == VARIABLE_COLOR ? 4 : k == VARIABLE_GRADIENT ? b->default_value->length - b->default_value->length % 5 : 1;
	n     = n < max ? n : max;
	for (i32 i = 0; i < n; ++i) {
		out[i] = i < b->default_value->length ? b->default_value->buffer[i] : (i == 3 ? 1.0 : 0.0);
	}
	if (k == VARIABLE_WHOLE || k == VARIABLE_CHOICE) {
		out[0] = math_floor(out[0] + 0.5);
	}
	if (k == VARIABLE_TOGGLE) {
		out[0] = out[0] != 0 ? 1.0 : 0.0;
	}
	if (interp != NULL) {
		*interp = b->data != NULL && b->data->length > 0 ? b->data->buffer[0] : 1;
	}
	return n;
}

// The value's floats themselves, for controls editing them in place (NULL when missing)
f32_array_t *variable_value_array(ui_node_t *node) {
	variable_sync(node);
	ui_node_button_t *b = variable_but(node, "variable_value");
	return b != NULL ? b->default_value : NULL;
}

void variable_set(ui_node_t *node, f32 *v, i32 n, i32 interp) {
	ui_node_button_t *b = variable_but(node, "variable_value");
	if (b == NULL || n < 1) {
		return;
	}
	b->default_value = f32_array_create_from_raw(v, n);
	if (variable_kind(node) == VARIABLE_GRADIENT) {
		if (b->data == NULL || b->data->length < 1) {
			b->data = u8_array_create(1);
		}
		b->data->buffer[0] = interp;
	}
}

// Keeps the value, the shown buttons and the output socket fitting the Type
void variable_sync(ui_node_t *node) {
	if (!variable_is(node)) {
		return;
	}
	i32               k = variable_kind(node);
	ui_node_button_t *v = variable_but(node, "variable_value");
	if (v != NULL) {
		i32 n = v->default_value != NULL ? v->default_value->length : 0;
		if (k == VARIABLE_COLOR && n != 4) {
			f32 x            = n > 0 ? v->default_value->buffer[0] : 0.5;
			v->default_value = f32_array_create_xyzw(x, x, x, 1.0);
		}
		else if (k == VARIABLE_GRADIENT && (n < 10 || n % 5 != 0)) {
			f32 g[10]        = {0, 0, 0, 1, 0, 1, 1, 1, 1, 1};
			v->default_value = f32_array_create_from_raw(g, 10);
			v->data          = u8_array_create(1);
			v->data->buffer[0] = 1;
		}
		else if (k <= VARIABLE_CHOICE && n != 1) {
			f32 x            = n > 0 ? v->default_value->buffer[0] : 0.0;
			v->default_value = f32_array_create_x(x);
		}
		v->height = k == VARIABLE_GRADIENT ? 5.2 : 1;
	}
	ui_node_button_t *r = variable_but(node, "variable_range");
	if (r != NULL) {
		r->height = k == VARIABLE_NUMBER || k == VARIABLE_WHOLE ? 1 : 0;
	}
	ui_node_button_t *o = variable_but(node, "variable_options");
	if (o != NULL) {
		o->height = k == VARIABLE_CHOICE ? 1 : 0;
	}
	char *type  = k == VARIABLE_COLOR ? "RGBA" : k == VARIABLE_GRADIENT ? "GRADIENT" : "VALUE";
	u32   color = k == VARIABLE_COLOR ? 0xffc7c729 : k == VARIABLE_GRADIENT ? 0xffd88a3d : 0xffa1a1a1;
	if (node->outputs->length > 0 && !string_equals(node->outputs->buffer[0]->type, type)) {
		ui_node_socket_t *s = node->outputs->buffer[0];
		s->type             = type;
		s->color            = color;
		s->default_value    = k == VARIABLE_COLOR ? f32_array_create_xyzw(0, 0, 0, 1) : f32_array_create_x(0);
	}
	char *name = variable_name(node);
	if (name[0] != 0 && !string_equals(node->name, name)) {
		node->name = string_copy(name);
	}
}

// Shader value of the output, a constant (the Studio previews Material Maker parameters through variable_propagate)
static char *variable_output(ui_node_t *node, ui_node_socket_t *socket) {
	f32 v[4] = {0, 0, 0, 1};
	i32 k    = variable_kind(node);
	if (k == VARIABLE_GRADIENT) {
		return "0.0";
	}
	variable_get(node, v, 4, NULL);
	if (k == VARIABLE_COLOR) {
		return string_tmp("vec4(%s, %s, %s, %s)", f32_to_string_with_zeros(v[0]), f32_to_string_with_zeros(v[1]), f32_to_string_with_zeros(v[2]),
		                  f32_to_string_with_zeros(v[3]));
	}
	return parser_material_vec1(v[0]);
}

// Select Value

static i32 select_value_list(ui_node_t *node, f32 *out, i32 max) {
	ui_node_button_t *b    = node->buttons->length > 0 ? node->buttons->buffer[0] : NULL;
	char             *text = b != NULL && b->data != NULL && b->data->length > 0 ? (char *)b->data->buffer : "";
	i32               n    = 0;
	char             *p    = text;
	while (*p != 0 && n < max) {
		char *end = NULL;
		f32   v   = strtof(p, &end);
		if (end == p) {
			p++;
			continue;
		}
		out[n++] = v;
		p        = end;
	}
	return n;
}

static char *select_value_output(ui_node_t *node, ui_node_socket_t *socket) {
	f32   vals[64];
	i32   n   = select_value_list(node, vals, 64);
	char *idx = parser_material_parse_value_input(node->inputs->buffer[0], false);
	if (n == 0) {
		return "0.0";
	}
	char *r = parser_material_vec1(vals[n - 1]);
	for (i32 i = n - 2; i >= 0; --i) {
		r = string_tmp("(%s < %s ? %s : %s)", idx, parser_material_vec1(i + 0.5), parser_material_vec1(vals[i]), r);
	}
	return r;
}

// Evaluation on the CPU

static ui_node_t *variable_node_by_id(ui_node_t_array_t *nodes, i32 id) {
	for (i32 i = 0; i < nodes->length; ++i) {
		if (nodes->buffer[i]->id == id) {
			return nodes->buffer[i];
		}
	}
	return NULL;
}

static i32 variable_eval_at(ui_node_t_array_t *nodes, ui_node_link_t_array_t *links, ui_node_t *node, i32 output, f32 *out, i32 max, i32 *interp,
                            i32 depth);

// An input's first component: what its link gives, else its own value. False when the link is not a constant chain.
static bool variable_eval_input(ui_node_t_array_t *nodes, ui_node_link_t_array_t *links, ui_node_t *node, i32 input, f32 *v, i32 depth) {
	for (i32 i = 0; i < links->length; ++i) {
		ui_node_link_t *l = links->buffer[i];
		if (l->to_id == node->id && l->to_socket == input) {
			ui_node_t *from = variable_node_by_id(nodes, l->from_id);
			f32        r[4];
			if (from == NULL || variable_eval_at(nodes, links, from, l->from_socket, r, 4, NULL, depth + 1) < 1) {
				return false;
			}
			*v = r[0];
			return true;
		}
	}
	ui_node_socket_t *s = input < node->inputs->length ? node->inputs->buffer[input] : NULL;
	*v                  = s != NULL && s->default_value != NULL && s->default_value->length > 0 ? s->default_value->buffer[0] : 0.0;
	return true;
}

static i32 variable_eval_at(ui_node_t_array_t *nodes, ui_node_link_t_array_t *links, ui_node_t *node, i32 output, f32 *out, i32 max, i32 *interp,
                            i32 depth) {
	if (depth > VARIABLE_MAX_DEPTH || max < 1) {
		return 0;
	}
	if (variable_is(node)) {
		return variable_get(node, out, max, interp);
	}
	if (string_equals(node->type, "VALUE") || string_equals(node->type, "RGB")) {
		f32_array_t *d = node->outputs->buffer[0]->default_value;
		i32          n = d->length < max ? d->length : max;
		for (i32 i = 0; i < n; ++i) {
			out[i] = d->buffer[i];
		}
		return n;
	}
	if (string_equals(node->type, "MAPRANGE")) {
		f32 x[5];
		for (i32 i = 0; i < 5; ++i) {
			if (!variable_eval_input(nodes, links, node, i, &x[i], depth)) {
				return 0;
			}
		}
		f32 r = x[2] != x[1] ? x[3] + (x[0] - x[1]) * (x[4] - x[3]) / (x[2] - x[1]) : x[3];
		if (node->buttons->length > 0 && node->buttons->buffer[0]->default_value->buffer[0] > 0) {
			f32 lo = x[3] < x[4] ? x[3] : x[4];
			f32 hi = x[3] < x[4] ? x[4] : x[3];
			r      = r < lo ? lo : r > hi ? hi : r;
		}
		out[0] = r;
		return 1;
	}
	if (string_equals(node->type, "SELECT_VALUE")) {
		f32 vals[64];
		i32 n = select_value_list(node, vals, 64);
		f32 i = 0;
		if (n == 0 || !variable_eval_input(nodes, links, node, 0, &i, depth)) {
			return 0;
		}
		i32 k  = (i32)math_floor(i + 0.5);
		k      = k < 0 ? 0 : k > n - 1 ? n - 1 : k;
		out[0] = vals[k];
		return 1;
	}
	return 0;
}

// The value of the output, when it is the same everywhere (Variable, Value, Color, Map Range, Select Value): how many
// floats, 0 when it is not
i32 variable_eval(ui_node_t_array_t *nodes, ui_node_link_t_array_t *links, ui_node_t *node, i32 output, f32 *out, i32 max, i32 *interp) {
	return variable_eval_at(nodes, links, node, output, out, max, interp, 0);
}

// Material Maker parameters wired to Variables take the values their links carry. True when a parameter changed.
bool variable_propagate(ui_node_t_array_t *nodes, ui_node_link_t_array_t *links) {
	bool changed = false;
	for (i32 i = 0; nodes != NULL && links != NULL && i < links->length; ++i) {
		ui_node_link_t *l     = links->buffer[i];
		ui_node_t      *to    = variable_node_by_id(nodes, l->to_id);
		char           *param = NULL;
		i32             stop  = -1;
		if (to == NULL || !mm_node_is(to) || !mm_node_param_socket_key(to, l->to_socket, &param, &stop)) {
			continue;
		}
		param           = string_copy(param);
		ui_node_t *from = variable_node_by_id(nodes, l->from_id);
		f32        v[320];
		i32        interp = 1;
		i32        n      = from != NULL ? variable_eval(nodes, links, from, l->from_socket, v, 320, &interp) : 0;
		if (n > 0) {
			changed = mm_node_param_set(to, param, stop, v, n, interp) || changed;
		}
		free(param);
	}
	return changed;
}

// For scripts: one float of the value, set one float of it, propagate a whole canvas
f32 variable_value(ui_node_t *node, i32 i) {
	f32 v[320];
	i32 n = variable_get(node, v, 320, NULL);
	return i >= 0 && i < n ? v[i] : 0.0;
}

void variable_set_value(ui_node_t *node, i32 i, f32 x) {
	f32_array_t *a = variable_value_array(node);
	if (a != NULL && i >= 0 && i < a->length) {
		a->buffer[i] = x;
	}
}

bool variable_propagate_canvas(ui_node_canvas_t *canvas) {
	return canvas != NULL && variable_propagate(canvas->nodes, canvas->links);
}

// The Variables of a canvas, by group then order
ui_node_t_array_t *variable_list(ui_node_canvas_t *canvas) {
	ui_node_t_array_t *rv = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; canvas != NULL && i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (!variable_is(n)) {
			continue;
		}
		variable_sync(n);
		i32 k = rv->length;
		any_array_push(rv, n);
		while (k > 0) {
			ui_node_t *a = rv->buffer[k - 1];
			i32        c = variable_group(a) != variable_group(n) ? variable_group(a) - variable_group(n) : variable_order(a) - variable_order(n);
			if (c <= 0) {
				break;
			}
			rv->buffer[k] = a;
			k--;
		}
		rv->buffer[k] = n;
	}
	return rv;
}

i32 variable_count(ui_node_canvas_t *canvas) {
	i32 n = 0;
	for (i32 i = 0; canvas != NULL && i < canvas->nodes->length; ++i) {
		n += variable_is(canvas->nodes->buffer[i]) ? 1 : 0;
	}
	return n;
}

ui_node_t *variable_find(ui_node_canvas_t *canvas, char *id) {
	for (i32 i = 0; canvas != NULL && i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (variable_is(n) && string_equals(variable_id(n), id)) {
			return n;
		}
	}
	return NULL;
}

// For scripts: the i-th Variable in panel order (group, then order), NULL past the end
ui_node_t *variable_at(ui_node_canvas_t *canvas, i32 i) {
	ui_node_t_array_t *vars = variable_list(canvas);
	return i >= 0 && i < vars->length ? vars->buffer[i] : NULL;
}

// For scripts: 0 min, 1 max, 2 step, 3 number of choice options, 4 length of the value array
f32 variable_param(ui_node_t *node, i32 what) {
	f32 min = 0.0, max = 1.0, step = 0.0;
	variable_range(node, &min, &max, &step);
	if (what == 3) {
		return variable_options(node)->length;
	}
	if (what == 4) {
		f32_array_t *arr = variable_value_array(node);
		return arr != NULL ? arr->length : 0;
	}
	return what == 0 ? min : what == 1 ? max : step;
}

// Set up: a Variable of the kind, its label, id, group (VARIABLE_GROUP_* key), unit key and order
void variable_setup(ui_node_t *node, i32 kind, char *label, char *id, char *group, char *unit, i32 order) {
	variable_set_num(node, "Type", 0, kind);
	variable_set_text(node, "variable_name", label);
	variable_set_text(node, "variable_id", id);
	i32 g = 0;
	for (i32 i = 0; i < 4; ++i) {
		g = group != NULL && string_equals(group, variable_group_keys[i]) ? i : g;
	}
	i32 u = 0;
	for (i32 i = 1; i < 5; ++i) {
		u = unit != NULL && string_equals(unit, variable_unit_keys[i]) ? i : u;
	}
	variable_set_num(node, "variable_show", 0, g);
	variable_set_num(node, "variable_show", 1, order);
	variable_set_num(node, "variable_show", 2, u);
	variable_sync(node);
}

void variable_set_range(ui_node_t *node, f32 min, f32 max, f32 step) {
	variable_set_num(node, "variable_range", 0, min);
	variable_set_num(node, "variable_range", 1, max);
	variable_set_num(node, "variable_range", 2, step);
}

void variable_set_options(ui_node_t *node, char *options) {
	variable_set_text(node, "variable_options", options);
}

// Drawing in the node editor

static ui_node_t *variable_drawn(i32 node_id, ui_node_button_t **but) {
	ui_node_t *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	i32        i    = ui_nodes_custom_button_index;
	if (node == NULL || i < 0 || i >= node->buttons->length) {
		return NULL;
	}
	*but = node->buttons->buffer[i];
	return node;
}

static void variable_next_id(ui_node_t *node, i32 k) {
	ui_set_next_id(((ui_id_t)node->id << 16) + ((ui_nodes_custom_button_index * 4 + k) & 0xffff) + 1); // As the editor keys its controls
}

static void variable_name_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = variable_drawn(node_id, &but);
	if (node == NULL) {
		return;
	}
	variable_sync(node);
	char *text = variable_name(node);
	variable_next_id(node, 0);
	char *edited = ui_text_input(&text, tr("Name"), UI_ALIGN_LEFT, true, false);
	if (edited != NULL && !string_equals(edited, variable_name(node))) {
		variable_set_text(node, "variable_name", edited);
		variable_sync(node);
	}
}

static void variable_value_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = variable_drawn(node_id, &but);
	if (node == NULL || but->default_value == NULL) {
		return;
	}
	variable_sync(node);
	i32  k = variable_kind(node);
	f32 *v = but->default_value->buffer;
	variable_next_id(node, 0);
	if (k == VARIABLE_NUMBER || k == VARIABLE_WHOLE) {
		f32 min, max, step;
		variable_range(node, &min, &max, &step);
		f32 prec = k == VARIABLE_WHOLE ? 1 : step > 0 ? 1.0 / step : 1000;
		ui_slider(&v[0], tr("Default"), min, max, true, prec, true, UI_ALIGN_LEFT, true);
		if (k == VARIABLE_WHOLE) {
			v[0] = math_floor(v[0] + 0.5);
		}
	}
	else if (k == VARIABLE_TOGGLE) {
		bool on = v[0] != 0;
		v[0]    = ui_check(&on, tr("Default"), "") ? 1.0 : 0.0;
	}
	else if (k == VARIABLE_CHOICE) {
		string_array_t *options = variable_options(node);
		if (options->length == 0) {
			any_array_push(options, tr("Option"));
		}
		i32 value = (i32)v[0];
		v[0]      = ui_combo(&value, options, tr("Default"), true, UI_ALIGN_LEFT, true);
	}
	else if (k == VARIABLE_COLOR) {
		ui_nodes_t              *nodes = ui_nodes_get_nodes();
		ui_nodes_editor_state_t *state = ui_nodes_editor_state(node);
		f32                      nx    = g_ui->_x;
		ui_row2();
		ui_text(tr("Default"), UI_ALIGN_LEFT, 0);
		if (ui_text("", UI_ALIGN_RIGHT, color_from_floats(v[0], v[1], v[2], 1.0)) == UI_STATE_STARTED) {
			f32 rx                = nx + g_ui->_w - ui_p(37);
			f32 ry                = g_ui->_y - ui_p(5);
			nodes->_input_started = g_ui->input_started = false;
			ui_nodes_rgba_popup(&state->color_state, v, math_floor(rx), math_floor(ry + UI_ELEMENT_H()));
		}
	}
	else if (k == VARIABLE_GRADIENT) {
		mm_nodes_gradient_edit(node, ui_nodes_custom_button_index, tr("Default"));
	}
}

static void variable_range_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = variable_drawn(node_id, &but);
	if (node == NULL || but->height == 0) {
		return;
	}
	f32 min, max, step;
	variable_range(node, &min, &max, &step);
	ui_row2();
	char *a = string_copy(f32_to_string(min));
	char *b = string_copy(f32_to_string(max));
	variable_next_id(node, 0);
	char *ea = ui_text_input(&a, tr("Min"), UI_ALIGN_LEFT, true, false);
	variable_next_id(node, 1);
	char *eb = ui_text_input(&b, tr("Max"), UI_ALIGN_LEFT, true, false);
	if (ea != NULL) {
		variable_set_num(node, "variable_range", 0, (f32)atof(ea));
	}
	if (eb != NULL) {
		variable_set_num(node, "variable_range", 1, (f32)atof(eb));
	}
}

static void variable_options_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = variable_drawn(node_id, &but);
	if (node == NULL || but->height == 0) {
		return;
	}
	char *text = variable_text(node, "variable_options");
	variable_next_id(node, 0);
	char *edited = ui_text_input(&text, tr("Options"), UI_ALIGN_LEFT, true, false);
	if (edited != NULL && !string_equals(edited, variable_text(node, "variable_options"))) {
		variable_set_text(node, "variable_options", edited);
	}
}

static void variable_show_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = variable_drawn(node_id, &but);
	if (node == NULL) {
		return;
	}
	string_array_t *groups = any_array_create_from_raw_tmp((void *[]){tr("Color"), tr("Pattern"), tr("Surface"), tr("Detail")}, 4);
	string_array_t *units =
	    any_array_create_from_raw_tmp((void *[]){tr("None"), tr("Percent"), tr("Degrees"), tr("Count"), tr("Multiplier")}, 5);
	ui_row2();
	i32 g = variable_group(node);
	variable_next_id(node, 0);
	variable_set_num(node, "variable_show", 0, ui_combo(&g, groups, tr("Group"), true, UI_ALIGN_LEFT, true));
	i32 u = variable_unit(node);
	variable_next_id(node, 1);
	variable_set_num(node, "variable_show", 2, ui_combo(&u, units, tr("Unit"), true, UI_ALIGN_LEFT, true));
}

static void variable_hidden_button(i32 node_id) {
}

static void select_value_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = variable_drawn(node_id, &but);
	if (node == NULL) {
		return;
	}
	char *text = but->data != NULL && but->data->length > 0 ? (char *)but->data->buffer : "";
	variable_next_id(node, 0);
	char *edited = ui_text_input(&text, tr("Values"), UI_ALIGN_LEFT, true, false);
	if (edited != NULL && (but->data == NULL || !string_equals(edited, (char *)but->data->buffer))) {
		but->data = u8_array_create_from_string(edited);
	}
}

static ui_node_button_t *variable_button_def(char *name, char *type, f32_array_t *value, char *data, f32 height) {
	return ALLOC_INIT(ui_node_button_t, {.name          = name,
	                                     .type          = type,
	                                     .output        = -1,
	                                     .default_value = value,
	                                     .data          = data != NULL ? u8_array_create_from_string(data) : NULL,
	                                     .min           = 0.0,
	                                     .max           = 1.0,
	                                     .precision     = 100,
	                                     .height        = height});
}

void variable_node_init() {
	ui_node_t *def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                        .name    = _tr("Variable"),
	                                        .type    = "VARIABLE",
	                                        .x       = 0,
	                                        .y       = 0,
	                                        .color   = 0xff4f8a5a,
	                                        .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                                        .outputs = any_array_create_from_raw(
	                                            (void *[]){
	                                                ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                              .node_id       = 0,
	                                                                              .name          = _tr("Value"),
	                                                                              .type          = "VALUE",
	                                                                              .color         = 0xffa1a1a1,
	                                                                              .default_value = f32_array_create_x(0.0),
	                                                                              .min           = 0.0,
	                                                                              .max           = 1.0,
	                                                                              .precision     = 100,
	                                                                              .display       = 0}),
	                                            },
	                                            1),
	                                        .buttons = any_array_create_from_raw(
	                                            (void *[]){
	                                                variable_button_def(_tr("Type"), "ENUM", f32_array_create_x(0), variable_types_data, 0),
	                                                variable_button_def("variable_name", "CUSTOM", f32_array_create_x(0), "Variable", 1),
	                                                variable_button_def("variable_value", "CUSTOM", f32_array_create_x(0.5), NULL, 1),
	                                                variable_button_def("variable_range", "CUSTOM", f32_array_create_xyz(0, 1, 0), NULL, 1),
	                                                variable_button_def("variable_options", "CUSTOM", f32_array_create_x(0), "", 0),
	                                                variable_button_def("variable_show", "CUSTOM", f32_array_create_xyz(0, 0, 0), NULL, 1),
	                                                variable_button_def("variable_id", "CUSTOM", f32_array_create_x(0), "", 0),
	                                            },
	                                            7),
	                                        .width = 0,
	                                        .flags = 0});
	any_array_push(nodes_material_input, def);
	any_map_set(parser_material_node_outputs, "VARIABLE", variable_output);

	any_map_set(ui_nodes_custom_buttons, "variable_name", variable_name_button);
	any_map_set(ui_nodes_custom_buttons, "variable_value", variable_value_button);
	any_map_set(ui_nodes_custom_buttons, "variable_range", variable_range_button);
	any_map_set(ui_nodes_custom_buttons, "variable_options", variable_options_button);
	any_map_set(ui_nodes_custom_buttons, "variable_show", variable_show_button);
	any_map_set(ui_nodes_custom_buttons, "variable_id", variable_hidden_button);
}

// Under Utilities: registered once nodes_material_utilities exists
void select_value_node_init() {
	ui_node_t *select = ALLOC_INIT(ui_node_t, {.id     = 0,
	                                           .name   = _tr("Select Value"),
	                                           .type   = "SELECT_VALUE",
	                                           .x      = 0,
	                                           .y      = 0,
	                                           .color  = 0xff62676d,
	                                           .inputs = any_array_create_from_raw(
	                                               (void *[]){
	                                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                 .node_id       = 0,
	                                                                                 .name          = _tr("Index"),
	                                                                                 .type          = "VALUE",
	                                                                                 .color         = 0xffa1a1a1,
	                                                                                 .default_value = f32_array_create_x(0.0),
	                                                                                 .min           = 0.0,
	                                                                                 .max           = 8.0,
	                                                                                 .precision     = 1,
	                                                                                 .display       = 0}),
	                                               },
	                                               1),
	                                           .outputs = any_array_create_from_raw(
	                                               (void *[]){
	                                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                 .node_id       = 0,
	                                                                                 .name          = _tr("Value"),
	                                                                                 .type          = "VALUE",
	                                                                                 .color         = 0xffa1a1a1,
	                                                                                 .default_value = f32_array_create_x(0.0),
	                                                                                 .min           = 0.0,
	                                                                                 .max           = 1.0,
	                                                                                 .precision     = 100,
	                                                                                 .display       = 0}),
	                                               },
	                                               1),
	                                           .buttons = any_array_create_from_raw(
	                                               (void *[]){
	                                                   variable_button_def("select_values", "CUSTOM", f32_array_create_x(0), "0, 1", 1),
	                                               },
	                                               1),
	                                           .width = 0,
	                                           .flags = 0});
	any_array_push(nodes_material_utilities, select);
	any_map_set(parser_material_node_outputs, "SELECT_VALUE", select_value_output);
	any_map_set(ui_nodes_custom_buttons, "select_values", select_value_button);
}
