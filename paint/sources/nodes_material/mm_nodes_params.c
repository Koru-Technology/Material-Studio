#include "../global.h"
#include "../material_maker/mm_internal.h"
#include <math.h>

// Material Maker nodes (mm_nodes.c): a node's name, seed and parameters, read and written as Material Maker saves them,
// and the parameter sockets that wire a parameter to a Variable. Included after mm_nodes.c, whose types and statics it uses.

// Buttons of a node by name

static i32 mm_nodes_button_index(ui_node_t *node, char *name) {
	for (i32 i = node->buttons->length - 1; i >= 0; --i) {
		ui_node_button_t *b = node->buttons->buffer[i];
		if (string_equals(b->type, "CUSTOM") && string_equals(b->name, name)) {
			return i;
		}
	}
	return -1;
}

static mm_json_t *mm_nodes_state(ui_node_t *node) { // In the arena, NULL for static types
	i32 i = mm_nodes_button_index(node, "mm_state");
	if (i < 0) {
		return NULL;
	}
	u8_array_t *d = node->buttons->buffer[i]->data;
	return d != NULL && d->length > 0 ? mm_json_parse((char *)d->buffer, strnlen((char *)d->buffer, d->length)) : NULL;
}

// Where the parameters of the node are: its state's layout for dynamic types, the type's otherwise. In the arena.
static mm_nodes_param_t *mm_nodes_layout(ui_node_t *node, i32 *count) {
	*count             = 0;
	mm_nodes_type_t *t = node != NULL ? mm_nodes_built(node->type) : NULL;
	if (t == NULL) {
		return NULL;
	}
	mm_json_t *state  = t->dynamic ? mm_nodes_state(node) : NULL;
	mm_json_t *layout = mm_json_get(state, "layout");
	if (layout == NULL || layout->type != MM_JSON_ARRAY) {
		*count = t->param_count;
		return t->params;
	}
	mm_nodes_param_t *p = mm_alloc(sizeof(mm_nodes_param_t) * (layout->count + 1));
	for (i32 i = 0; i < layout->count; ++i) {
		mm_json_t *e = layout->items[i];
		p[i].name    = (char *)mm_json_str(mm_json_at(e, 0), "");
		p[i].kind    = (i32)mm_json_num(mm_json_at(e, 1), 0);
		p[i].button  = (i32)mm_json_num(mm_json_at(e, 2), 0);
		p[i].first   = (i32)mm_json_num(mm_json_at(e, 3), 0);
		p[i].label   = (char *)mm_json_str(mm_json_at(e, 4), "");
	}
	*count = layout->count;
	return p;
}

// Seed and name. The name is the node's name in Material Maker's graph (remote widgets refer to it), kept in the data of
// the seed button.

static i32 mm_nodes_seed_index(ui_node_t *node) {
	i32 i = mm_nodes_button_index(node, "mm_seed");
	return i >= 0 && node->buttons->buffer[i]->default_value != NULL && node->buttons->buffer[i]->default_value->length >= 4 ? i : -1;
}

// get_seed_from_position, as Material Maker seeds a node added without one
static double mm_nodes_position_seed(ui_node_t *node) {
	int64_t x = (int64_t)node->x;
	int64_t y = (int64_t)node->y;
	return (double)(((x * 0x1f1f1f1f) ^ y) % 65536);
}

char *mm_node_name(ui_node_t *node) {
	i32 i = node != NULL ? mm_nodes_seed_index(node) : -1;
	if (i < 0) {
		return NULL;
	}
	u8_array_t *d = node->buttons->buffer[i]->data;
	return d != NULL && d->length > 1 ? (char *)d->buffer : NULL;
}

static void mm_nodes_set_name(ui_node_t *node, const char *name) {
	i32 i = mm_nodes_seed_index(node);
	if (i >= 0 && name != NULL) {
		node->buttons->buffer[i]->data = mm_nodes_text_data(name);
	}
}

void mm_node_created(ui_node_t *node) {
	if (!mm_node_is(node)) {
		return;
	}
	i32 i = mm_nodes_seed_index(node);
	if (i >= 0) {
		f32 *s = node->buttons->buffer[i]->default_value->buffer;
		if (s[3] == 0) {
			double seed = mm_nodes_position_seed(node);
			s[0]        = (f32)seed;
			s[1]        = (f32)(seed - s[0]);
			s[3]        = 1;
		}
		if (mm_node_name(node) == NULL) { // Unique in its canvas as node ids are
			mm_nodes_set_name(node, string_tmp("%s_%d", mm_nodes_find(node->type)->mm_type, node->id));
		}
	}
}

double mm_node_seed(ui_node_t *node, bool *locked) {
	i32 i = node != NULL ? mm_nodes_seed_index(node) : -1;
	if (i < 0) {
		if (locked != NULL) {
			*locked = false;
		}
		return node != NULL ? mm_nodes_position_seed(node) : 0;
	}
	f32 *s = node->buttons->buffer[i]->default_value->buffer;
	if (locked != NULL) {
		*locked = s[2] != 0;
	}
	return s[3] != 0 ? (double)s[0] + (double)s[1] : mm_nodes_position_seed(node);
}

char *mm_node_input_expression(ui_node_t *node, i32 input) {
	mm_nodes_type_t *t = node != NULL ? mm_nodes_built(node->type) : NULL;
	if (t != NULL && mm_nodes_is_builtin(t->mm_type)) { // No default expressions, Material Maker's default shader
		return input >= 0 && input < mm_node_port_count(node) ? "" : NULL;
	}
	return t != NULL && input >= 0 && input < t->input_count ? t->input_exprs[input] : NULL;
}

// Button values back to Material Maker's parameter values, in the current mm arena

static mm_json_t *mm_nodes_param_value(mm_nodes_param_t *p, ui_node_button_t *b) {
	f32 *v = b->default_value != NULL ? b->default_value->buffer : NULL;
	i32  n = b->default_value != NULL ? b->default_value->length : 0;
	switch (p->kind) {
	case MM_NODES_PARAM_FLOAT:
		if (n > 0 && string_equals(b->type, "VALUE") && b->data != NULL && b->data->length > 1) { // An expression
			return mm_json_string(mm_strndup((const char *)b->data->buffer, strnlen((const char *)b->data->buffer, b->data->length)));
		}
		return n > 0 && string_equals(b->type, "VALUE") ? mm_json_number(v[0]) : NULL;
	case MM_NODES_PARAM_ENUM:
		return n > 0 && string_equals(b->type, "ENUM") ? mm_json_number((i32)v[0]) : NULL;
	case MM_NODES_PARAM_BOOL:
		return n > 0 && string_equals(b->type, "BOOL") ? mm_json_boolean(v[0] != 0) : NULL;
	case MM_NODES_PARAM_SIZE:
		return n > 0 && string_equals(b->type, "ENUM") ? mm_json_number(p->first + (i32)v[0]) : NULL;
	case MM_NODES_PARAM_COLOR: {
		if (n < 3 || !string_equals(b->type, "RGBA")) {
			return NULL;
		}
		mm_json_t *c = mm_json_new(MM_JSON_OBJECT);
		mm_json_set(c, "type", mm_json_string("Color"));
		mm_json_set(c, "r", mm_json_number(v[0]));
		mm_json_set(c, "g", mm_json_number(v[1]));
		mm_json_set(c, "b", mm_json_number(v[2]));
		mm_json_set(c, "a", mm_json_number(n > 3 ? v[3] : 1.0));
		return c;
	}
	case MM_NODES_PARAM_GRAD: {
		if (n < 5 || !string_equals(b->name, "mm_gradient")) {
			return NULL;
		}
		mm_json_t *g = mm_json_new(MM_JSON_OBJECT);
		mm_json_t *a = mm_json_new(MM_JSON_ARRAY);
		mm_json_set(g, "type", mm_json_string("Gradient"));
		mm_json_set(g, "interpolation", mm_json_number(b->data != NULL && b->data->length > 0 ? b->data->buffer[0] : 1));
		for (i32 i = 0; i + 4 < n; i += 5) {
			mm_json_t *pt = mm_json_new(MM_JSON_OBJECT);
			mm_json_set(pt, "pos", mm_json_number(v[i + 4]));
			mm_json_set(pt, "r", mm_json_number(v[i + 0]));
			mm_json_set(pt, "g", mm_json_number(v[i + 1]));
			mm_json_set(pt, "b", mm_json_number(v[i + 2]));
			mm_json_set(pt, "a", mm_json_number(v[i + 3]));
			mm_json_push(a, pt);
		}
		mm_json_set(g, "points", a);
		return g;
	}
	case MM_NODES_PARAM_CURVE: {
		if (n < 4 || !string_equals(b->name, "mm_curve")) {
			return NULL;
		}
		mm_json_t *c = mm_json_new(MM_JSON_OBJECT);
		mm_json_t *a = mm_json_new(MM_JSON_ARRAY);
		mm_json_set(c, "type", mm_json_string("Curve"));
		for (i32 i = 0; i + 3 < n; i += 4) {
			mm_json_t *pt = mm_json_new(MM_JSON_OBJECT);
			mm_json_set(pt, "x", mm_json_number(v[i + 0]));
			mm_json_set(pt, "y", mm_json_number(v[i + 1]));
			mm_json_set(pt, "ls", mm_json_number(v[i + 2]));
			mm_json_set(pt, "rs", mm_json_number(v[i + 3]));
			mm_json_push(a, pt);
		}
		mm_json_set(c, "points", a);
		return c;
	}
	case MM_NODES_PARAM_STRING:
		return b->data != NULL && b->data->length > 0 && string_equals(b->name, "mm_string")
		           ? mm_json_string(mm_strndup((const char *)b->data->buffer, strnlen((const char *)b->data->buffer, b->data->length)))
		           : NULL;
	default:
		if (b->data == NULL || b->data->length == 0 || !string_equals(b->name, "mm_value")) {
			return NULL;
		}
		return mm_json_parse((const char *)b->data->buffer, strnlen((const char *)b->data->buffer, b->data->length));
	}
}

// The node's parameters as Material Maker saves them ({"scale_x": 4, "gradient": {"type": "Gradient", ..}, ..}), for the
// generator. In the current mm arena. Parameters whose button does not match (a project saved with another nodes.pack)
// are left out, Material Maker uses their defaults.
mm_json_t *mm_node_parameters(ui_node_t *node) {
	mm_json_t        *rv    = mm_json_new(MM_JSON_OBJECT);
	i32               count = 0;
	mm_nodes_param_t *ps    = mm_nodes_layout(node, &count);
	for (i32 i = 0; i < count; ++i) {
		mm_nodes_param_t *p = &ps[i];
		if (p->button < 0 || p->button >= node->buttons->length) {
			continue;
		}
		mm_json_t *v = mm_nodes_param_value(p, node->buttons->buffer[p->button]);
		if (v != NULL) {
			mm_json_set(rv, p->name, v);
		}
	}
	return rv;
}

// Material Maker's name of a socket type
static const char *mm_nodes_mm_socket_type(char *type) {
	static const char *map[][2] = {
	    {"VALUE", "f"},          {"RGB", "rgb"},         {"RGBA", "rgba"},   {"MM_SDF2D", "sdf2d"}, {"MM_SDF3D", "sdf3d"}, {"MM_SDF3DC", "sdf3dc"},
	    {"MM_TEX3D_GS", "tex3d_gs"}, {"MM_TEX3D", "tex3d"}, {"MM_V4V4", "v4v4"}, {"MM_FILL", "fill"},
	};
	for (i32 i = 0; type != NULL && i < (i32)ARRAY_LENGTH(map); ++i) {
		if (strcmp(type, map[i][0]) == 0) {
			return map[i][1];
		}
	}
	return "any";
}

// The node as Material Maker saves a node of a graph: type, name, parameters, seed, what its generator saves besides
// (version, widgets, io, port_type ..), and nodes / connections of an inline graph
mm_json_t *mm_node_data(ui_node_t *node) {
	mm_nodes_type_t *t = node != NULL ? mm_nodes_built(node->type) : NULL;
	if (t == NULL) {
		return NULL;
	}
	mm_json_t *d = mm_json_new(MM_JSON_OBJECT);
	mm_json_set(d, "type", mm_json_string(t->mm_type));
	if (mm_node_name(node) != NULL) {
		mm_json_set(d, "name", mm_json_string(mm_node_name(node)));
	}
	if (strcmp(t->mm_type, "graph") == 0) {
		mm_json_t *e = mm_json_parse(t->entry, strlen(t->entry));
		mm_json_set(d, "nodes", mm_json_get(e, "nodes"));
		mm_json_set(d, "connections", mm_json_get(e, "connections"));
	}
	mm_json_set(d, "parameters", mm_node_parameters(node));
	mm_json_t *state = mm_nodes_state(node);
	for (i32 i = 0; state != NULL && i < state->count; ++i) {
		if (state->items[i] != NULL && strcmp(state->keys[i], "layout") != 0) {
			mm_json_set(d, state->keys[i], state->items[i]);
		}
	}
	if (strcmp(t->mm_type, "portal") == 0) { // The type it carries
		ui_node_socket_t *s = node->inputs->length > 0 ? node->inputs->buffer[0] : node->outputs->length > 0 ? node->outputs->buffer[0] : NULL;
		mm_json_set(d, "port_type", mm_json_string(mm_nodes_mm_socket_type(s != NULL ? s->type : NULL)));
	}
	bool   locked = false;
	double seed   = mm_node_seed(node, &locked);
	mm_json_set(d, "seed", mm_json_number(seed));
	mm_json_set(d, "seed_locked", mm_json_boolean(locked));
	if (t->generic && mm_json_get(d, "generic_size") == NULL) {
		mm_json_set(d, "generic_size", mm_json_number(1));
	}
	return d;
}

char *mm_node_parameters_json(ui_node_t *node) {
	mm_mark_t m = mm_arena_mark();
	char     *s = string_copy(mm_json_text(mm_node_parameters(node)));
	mm_arena_release(m);
	return s;
}

char *mm_node_data_json(ui_node_t *node) {
	mm_mark_t  m = mm_arena_mark();
	mm_json_t *d = mm_node_data(node);
	if (d != NULL) {
		mm_json_set(d, "nodes", NULL); // Long, the parameters and seed are what changes
		mm_json_set(d, "connections", NULL);
	}
	char *s = string_copy(d != NULL ? mm_json_text(d) : "");
	mm_arena_release(m);
	return s;
}

// Material Maker values into buttons, the opposite of mm_nodes_param_value
static void mm_nodes_store(mm_nodes_param_t *p, ui_node_button_t *b, mm_json_t *v) {
	if (v == NULL || b->default_value == NULL) {
		return;
	}
	f32 *d = b->default_value->buffer;
	switch (p->kind) {
	case MM_NODES_PARAM_FLOAT:
		d[0]    = mm_json_num(v, d[0]);
		b->data = mm_nodes_float_expr(v);
		break;
	case MM_NODES_PARAM_ENUM:
		d[0] = mm_nodes_clampi(mm_json_num(v, d[0]), 0, (i32)b->max);
		break;
	case MM_NODES_PARAM_BOOL:
		d[0] = mm_json_bool(v, d[0] != 0) ? 1 : 0;
		break;
	case MM_NODES_PARAM_SIZE:
		d[0] = mm_nodes_clampi(mm_json_num(v, p->first + d[0]) - p->first, 0, (i32)b->max);
		break;
	case MM_NODES_PARAM_COLOR:
		if (v->type == MM_JSON_OBJECT && b->default_value->length >= 4) {
			d[0] = mm_json_num(mm_json_get(v, "r"), d[0]);
			d[1] = mm_json_num(mm_json_get(v, "g"), d[1]);
			d[2] = mm_json_num(mm_json_get(v, "b"), d[2]);
			d[3] = mm_json_num(mm_json_get(v, "a"), d[3]);
		}
		break;
	case MM_NODES_PARAM_GRAD: {
		i32 interp       = 1;
		b->default_value = mm_nodes_gradient_values(v, &interp);
		b->data          = u8_array_create(1);
		b->data->buffer[0] = (u8)(interp < 0 || interp > 3 ? 1 : interp);
		break;
	}
	case MM_NODES_PARAM_CURVE:
		b->default_value = mm_nodes_curve_values(v);
		break;
	case MM_NODES_PARAM_STRING:
		b->data = mm_nodes_text_data(mm_json_str(v, ""));
		break;
	default:
		b->data = mm_nodes_text_data(mm_json_text(v));
		break;
	}
}

static mm_nodes_param_t *mm_nodes_find_param(ui_node_t *node, const char *name) {
	i32               count = 0;
	mm_nodes_param_t *ps    = mm_nodes_layout(node, &count);
	for (i32 i = 0; i < count; ++i) {
		if (strcmp(ps[i].name, name) == 0 && ps[i].button >= 0 && ps[i].button < node->buttons->length) {
			return &ps[i];
		}
	}
	return NULL;
}

// Node of the canvas by its name in Material Maker's graph
ui_node_t *mm_node_named(ui_node_canvas_t *canvas, char *name) {
	for (i32 i = 0; canvas != NULL && name != NULL && i < canvas->nodes->length; ++i) {
		ui_node_t *n  = canvas->nodes->buffer[i];
		char      *nn = mm_node_is(n) ? mm_node_name(n) : NULL;
		if (nn != NULL && strcmp(nn, name) == 0) {
			return n;
		}
	}
	return NULL;
}

// Parameter sockets: a parameter wired to a Variable (variable_node.c) gets an input socket after the node's ports, only
// while it is linked. Its name is the parameter's ("rows"), or "gradient#2" for the color of stop 2 of a gradient (index
// in the button's values); display MM_PARAM_SOCKET marks it. variable_propagate writes what the link carries into the
// parameter's button before the node is used, so shaders, previews and buffers read the button as for any parameter.

bool mm_node_is_param_socket(ui_node_socket_t *s) {
	return s != NULL && (s->display & MM_PARAM_SOCKET) != 0;
}

// Inputs that are Material Maker's ports, the parameter sockets come after them
i32 mm_node_port_count(ui_node_t *node) {
	i32 n = 0;
	for (i32 i = 0; i < node->inputs->length; ++i) {
		n += mm_node_is_param_socket(node->inputs->buffer[i]) ? 0 : 1;
	}
	return n;
}

static char *mm_nodes_param_key(char *param, i32 stop) {
	return stop >= 0 ? string_tmp("%s#%d", param, stop) : param;
}

// The parameter and stop (-1: none) of a parameter socket, false for other sockets
bool mm_node_param_socket_key(ui_node_t *node, i32 input, char **param, i32 *stop) {
	ui_node_socket_t *s = input >= 0 && input < node->inputs->length ? node->inputs->buffer[input] : NULL;
	if (!mm_node_is_param_socket(s)) {
		return false;
	}
	static char name[128];
	snprintf(name, sizeof(name), "%s", s->name);
	char *hash = strchr(name, '#');
	*stop      = hash != NULL ? atoi(hash + 1) : -1;
	if (hash != NULL) {
		*hash = 0;
	}
	*param = name;
	return true;
}

// The kind of a parameter of the node (MM_NODES_PARAM_*), -1 when it has none of that name
i32 mm_node_param_kind(ui_node_t *node, char *param) {
	mm_mark_t         mk = mm_arena_mark();
	mm_nodes_param_t *p  = mm_nodes_find_param(node, param);
	i32               k  = p != NULL ? p->kind : -1;
	mm_arena_release(mk);
	return k;
}

static ui_node_button_t *mm_nodes_param_button(ui_node_t *node, char *param, i32 *kind) {
	mm_mark_t         mk = mm_arena_mark();
	mm_nodes_param_t *p  = mm_nodes_find_param(node, param);
	ui_node_button_t *b  = p != NULL ? node->buttons->buffer[p->button] : NULL;
	*kind                = p != NULL ? p->kind : -1;
	mm_arena_release(mk);
	return b;
}

// The input index of the parameter socket, made when missing (-1 when the node has no such parameter)
i32 mm_node_param_socket(ui_node_t *node, char *param, i32 stop, ui_node_canvas_t *canvas) {
	i32               kind = -1;
	ui_node_button_t *b    = mm_nodes_param_button(node, param, &kind);
	if (b == NULL || kind == MM_NODES_PARAM_CURVE || kind == MM_NODES_PARAM_STRING || kind == MM_NODES_PARAM_OTHER ||
	    (stop >= 0 && (kind != MM_NODES_PARAM_GRAD || stop * 5 + 4 >= b->default_value->length))) {
		return -1;
	}
	char *key = mm_nodes_param_key(param, stop);
	for (i32 i = 0; i < node->inputs->length; ++i) {
		if (mm_node_is_param_socket(node->inputs->buffer[i]) && string_equals(node->inputs->buffer[i]->name, key)) {
			return i;
		}
	}
	char *type = kind == MM_NODES_PARAM_COLOR || stop >= 0 ? "RGBA" : kind == MM_NODES_PARAM_GRAD ? "GRADIENT" : "VALUE";
	f32   v[4] = {0, 0, 0, 1};
	ui_node_socket_t *s = ALLOC_INIT(ui_node_socket_t, {.id            = ui_get_socket_id(canvas->nodes),
	                                                    .node_id       = node->id,
	                                                    .name          = string_copy(key),
	                                                    .type          = type,
	                                                    .color         = string_equals(type, "GRADIENT") ? 0xffd88a3d : nodes_material_get_socket_color(type),
	                                                    .default_value = f32_array_create_from_raw(v, string_equals(type, "VALUE") ? 1 : 4),
	                                                    .min           = 0.0,
	                                                    .max           = 1.0,
	                                                    .precision     = 100,
	                                                    .display       = MM_PARAM_SOCKET});
	any_array_push(node->inputs, s);
	return node->inputs->length - 1;
}

// Unlinked parameter sockets are removed, the links of later sockets follow their index
void mm_nodes_prune_param_sockets(ui_node_canvas_t *canvas) {
	for (i32 ni = 0; canvas != NULL && ni < canvas->nodes->length; ++ni) {
		ui_node_t *node = canvas->nodes->buffer[ni];
		if (!mm_node_is(node)) {
			continue;
		}
		for (i32 i = node->inputs->length - 1; i >= 0; --i) {
			if (!mm_node_is_param_socket(node->inputs->buffer[i])) {
				continue;
			}
			bool linked = false;
			for (i32 l = 0; l < canvas->links->length && !linked; ++l) {
				ui_node_link_t *k = canvas->links->buffer[l];
				linked            = k->to_id == node->id && k->to_socket == i;
			}
			if (linked) {
				continue;
			}
			for (i32 j = i; j < node->inputs->length - 1; ++j) {
				node->inputs->buffer[j] = node->inputs->buffer[j + 1];
			}
			node->inputs->length--;
			for (i32 l = 0; l < canvas->links->length; ++l) {
				ui_node_link_t *k = canvas->links->buffer[l];
				if (k->to_id == node->id && k->to_socket > i) {
					k->to_socket--;
				}
			}
		}
	}
}

// The current value of a parameter as floats: [v] for float, enum (index), bool, size; [r,g,b,a] for a color or a stop;
// [r,g,b,a,pos]*n for a gradient (interp: its interpolation). Returns how many, 0 when there is no such parameter.
i32 mm_node_param_get(ui_node_t *node, char *param, i32 stop, f32 *out, i32 max, i32 *interp) {
	i32               kind = -1;
	ui_node_button_t *b    = mm_nodes_param_button(node, param, &kind);
	if (b == NULL || b->default_value == NULL) {
		return 0;
	}
	f32_array_t *v = b->default_value;
	if (kind == MM_NODES_PARAM_GRAD) {
		if (interp != NULL) {
			*interp = b->data != NULL && b->data->length > 0 ? b->data->buffer[0] : 1;
		}
		if (stop >= 0) {
			if (stop * 5 + 4 >= v->length || max < 4) {
				return 0;
			}
			for (i32 i = 0; i < 4; ++i) {
				out[i] = v->buffer[stop * 5 + i];
			}
			return 4;
		}
	}
	i32 n = kind == MM_NODES_PARAM_COLOR ? 4 : kind == MM_NODES_PARAM_GRAD ? v->length : 1;
	n     = n < max ? n : max;
	for (i32 i = 0; i < n; ++i) {
		out[i] = i < v->length ? v->buffer[i] : (i == 3 ? 1.0 : 0.0);
	}
	return n;
}

// The names of the options of an enum parameter, one per line ("" when not an enum)
char *mm_node_param_options(ui_node_t *node, char *param) {
	i32               kind = -1;
	ui_node_button_t *b    = mm_nodes_param_button(node, param, &kind);
	if (b == NULL || kind != MM_NODES_PARAM_ENUM || b->data == NULL || b->data->length < 2) {
		return "";
	}
	return string_tmp("%s", (char *)b->data->buffer);
}

// The range of a float parameter: min, max and step (1 / precision)
bool mm_node_param_range(ui_node_t *node, char *param, f32 *min, f32 *max, f32 *step) {
	i32               kind = -1;
	ui_node_button_t *b    = mm_nodes_param_button(node, param, &kind);
	if (b == NULL || kind != MM_NODES_PARAM_FLOAT) {
		return false;
	}
	*min  = b->min;
	*max  = b->max;
	*step = b->precision > 0 ? 1.0 / b->precision : 0.0;
	return true;
}

// Writes a value (as mm_node_param_get gives it) into the parameter's button. A float set this way replaces an
// expression. True when the button changed.
bool mm_node_param_set(ui_node_t *node, char *param, i32 stop, f32 *v, i32 n, i32 interp) {
	i32               kind = -1;
	ui_node_button_t *b    = mm_nodes_param_button(node, param, &kind);
	if (b == NULL || b->default_value == NULL || n < 1) {
		return false;
	}
	f32_array_t *d       = b->default_value;
	bool         changed = false;
	switch (kind) {
	case MM_NODES_PARAM_FLOAT:
		changed = d->buffer[0] != v[0] || (b->data != NULL && b->data->length > 1);
		d->buffer[0] = v[0];
		if (b->data != NULL && b->data->length > 1) {
			b->data = NULL;
		}
		break;
	case MM_NODES_PARAM_ENUM:
	case MM_NODES_PARAM_SIZE: {
		i32 count = 1;
		for (i32 i = 0; kind == MM_NODES_PARAM_ENUM && b->data != NULL && i < b->data->length && b->data->buffer[i] != 0; ++i) {
			count += b->data->buffer[i] == '\n' ? 1 : 0;
		}
		f32 x   = math_floor(v[0] + 0.5);
		x       = kind == MM_NODES_PARAM_ENUM ? (x < 0 ? 0 : x > count - 1 ? count - 1 : x) : x;
		changed = d->buffer[0] != x;
		d->buffer[0] = x;
		break;
	}
	case MM_NODES_PARAM_BOOL: {
		f32 x   = v[0] != 0 ? 1.0 : 0.0;
		changed = d->buffer[0] != x;
		d->buffer[0] = x;
		break;
	}
	case MM_NODES_PARAM_COLOR:
		for (i32 i = 0; i < 3 && i < n && i < d->length; ++i) { // Its alpha stays
			changed      = changed || d->buffer[i] != v[i];
			d->buffer[i] = v[i];
		}
		break;
	case MM_NODES_PARAM_GRAD:
		if (stop >= 0) {
			for (i32 i = 0; i < 3 && i < n && stop * 5 + i < d->length; ++i) {
				changed                 = changed || d->buffer[stop * 5 + i] != v[i];
				d->buffer[stop * 5 + i] = v[i];
			}
		}
		else if (n >= 5) {
			n = n - n % 5;
			changed = d->length != n || (b->data != NULL && b->data->length > 0 && b->data->buffer[0] != interp);
			for (i32 i = 0; i < n && !changed; ++i) {
				changed = d->buffer[i] != v[i];
			}
			if (changed) {
				d->length = 0;
				for (i32 i = 0; i < n; ++i) {
					f32_array_push(d, v[i]);
				}
				if (b->data == NULL || b->data->length < 1) {
					b->data = u8_array_create(1);
				}
				b->data->buffer[0] = interp;
			}
		}
		break;
	default:
		break;
	}
	return changed;
}

// Parameters a Variable can drive, for "Export Parameter": count, then name, label and kind of each
i32 mm_node_export_count(ui_node_t *node) {
	mm_mark_t mk    = mm_arena_mark();
	i32       count = 0;
	mm_nodes_layout(node, &count);
	mm_arena_release(mk);
	return count;
}

bool mm_node_export_param_at(ui_node_t *node, i32 i, char **name, char **label, i32 *kind) {
	mm_mark_t         mk    = mm_arena_mark();
	i32               count = 0;
	mm_nodes_param_t *ps    = mm_nodes_layout(node, &count);
	bool              ok    = i >= 0 && i < count;
	if (ok) {
		*name  = string_tmp("%s", ps[i].name);
		*label = string_tmp("%s", ps[i].label != NULL && ps[i].label[0] != 0 ? ps[i].label : ps[i].name);
		*kind  = ps[i].kind;
		ok     = ps[i].kind == MM_NODES_PARAM_FLOAT || ps[i].kind == MM_NODES_PARAM_ENUM || ps[i].kind == MM_NODES_PARAM_BOOL ||
		     ps[i].kind == MM_NODES_PARAM_COLOR || ps[i].kind == MM_NODES_PARAM_GRAD;
	}
	mm_arena_release(mk);
	return ok;
}

// Label of a parameter socket: "Rows", "Gradient: Stop 3"
static char *mm_nodes_socket_label(ui_node_t *node, ui_node_socket_t *s) {
	if (!mm_node_is_param_socket(s) || !mm_node_is(node)) {
		return NULL;
	}
	i32   input = -1;
	for (i32 i = 0; i < node->inputs->length; ++i) {
		input = node->inputs->buffer[i] == s ? i : input;
	}
	char *param = NULL;
	i32   stop  = -1;
	if (!mm_node_param_socket_key(node, input, &param, &stop)) {
		return NULL;
	}
	param = string_copy(param);
	mm_mark_t         mk    = mm_arena_mark();
	mm_nodes_param_t *p     = mm_nodes_find_param(node, param);
	char             *label = string_tmp("%s", p != NULL && p->label != NULL && p->label[0] != 0 ? tr(p->label) : param);
	mm_arena_release(mk);
	free(param);
	return stop >= 0 ? string_tmp("%s: %s %d", label, tr("Stop"), stop + 1) : label;
}

static any_map_t *mm_nodes_remote_seen = NULL; // Remote node to the parameters json it last applied

static void mm_nodes_remote_apply(ui_node_t *remote, const char *name, mm_json_t *v, ui_node_canvas_t *canvas, i32 depth);

// mm_gen_set_param on a node of the canvas: its button, and through a remote the parameters it controls
static void mm_nodes_set_param(ui_node_t *node, const char *name, mm_json_t *v, ui_node_canvas_t *canvas, i32 depth) {
	if (node == NULL || depth > MM_NODES_MAX_DEPTH) {
		return;
	}
	mm_nodes_type_t *t = mm_nodes_built(node->type);
	if (t != NULL && strcmp(t->mm_type, "remote") == 0) {
		mm_nodes_remote_apply(node, name, v, canvas, depth + 1);
	}
	mm_nodes_param_t *p = mm_nodes_find_param(node, name);
	if (p != NULL) {
		mm_nodes_store(p, node->buttons->buffer[p->button], v);
	}
	if (t != NULL && strcmp(t->mm_type, "remote") == 0 && mm_nodes_remote_seen != NULL) { // Applied, not to be applied again
		any_map_set(mm_nodes_remote_seen, string_copy(string_tmp("%p", node)), mm_node_parameters_json(node));
	}
}

static i32 mm_nodes_key_cmp(const void *a, const void *b) {
	return strcmp(*(char *const *)a, *(char *const *)b);
}

// gen_remote.gd set_parameter: a linked control sets the widget of every linked node, a configuration control sets the
// values of the chosen configuration (sorted by name)
static void mm_nodes_remote_apply(ui_node_t *remote, const char *name, mm_json_t *v, ui_node_canvas_t *canvas, i32 depth) {
	mm_json_t *widgets = mm_json_get(mm_nodes_state(remote), "widgets");
	for (i32 i = 0; widgets != NULL && i < widgets->count; ++i) {
		mm_json_t  *w    = widgets->items[i];
		const char *wn   = mm_nodes_jstr(w, "name");
		const char *type = mm_nodes_jstr(w, "type");
		if (wn == NULL || type == NULL || strcmp(wn, name) != 0) {
			continue;
		}
		if (strcmp(type, "linked_control") == 0) {
			mm_json_t *links = mm_json_get(w, "linked_widgets");
			for (i32 k = 0; links != NULL && k < links->count; ++k) {
				mm_nodes_set_param(mm_node_named(canvas, mm_nodes_jstr(links->items[k], "node")), mm_json_str(mm_json_get(links->items[k], "widget"), ""), v,
				                   canvas, depth);
			}
		}
		else if (strcmp(type, "config_control") == 0) {
			mm_json_t *cfg = mm_json_get(w, "configurations");
			i32        idx = v != NULL && v->type == MM_JSON_BOOL ? (v->boolean ? 1 : 0) : (i32)mm_json_num(v, 0);
			if (cfg == NULL || cfg->type != MM_JSON_OBJECT || idx < 0 || idx >= cfg->count) {
				return; // Incorrect configuration index, Material Maker returns
			}
			char **keys = mm_alloc(sizeof(char *) * cfg->count);
			memcpy(keys, cfg->keys, sizeof(char *) * cfg->count);
			qsort(keys, cfg->count, sizeof(char *), mm_nodes_key_cmp);
			mm_json_t *list = mm_json_get(cfg, keys[idx]);
			for (i32 k = 0; list != NULL && k < list->count; ++k) {
				mm_nodes_set_param(mm_node_named(canvas, mm_nodes_jstr(list->items[k], "node")), mm_json_str(mm_json_get(list->items[k], "widget"), ""),
				                   mm_json_get(list->items[k], "value"), canvas, depth);
			}
		}
		break;
	}
}
