#include "../global.h"
#include "../material_maker/mm_internal.h"
#include <math.h>

// Material Maker nodes: every node of Material Maker's library tree (library_base in nodes.pack, from
// material_maker/library/base.json) becomes a native node type "MM_<mmg name>" in the "Material Maker" category, in the
// tree's folders. Inline graphs of the tree (Fibers, Warp 1, ..) are "MM_graph_<tree path>", presets (tree entries that
// reuse a node with other parameters: Square, Herringbone, ..) "MM_<mmg name>@<name>", the same node with other defaults.
// The generators Material Maker writes in GDScript are types too, their ports and parameters from gen_*.gd
// (mm_nodes_builtin_model): MM_buffer, MM_iterate_buffer, MM_switch, MM_reroute, MM_remote, MM_portal, MM_image, MM_text.
// Those whose sockets or buttons depend on the node (buffer version, switch outputs x choices, portal io, remote widgets,
// "any" sockets) keep a hidden CUSTOM mm_state button: json of what Material Maker saves besides the parameters and the
// layout of the parameters' buttons. mm_node_update keeps them consistent as they are drawn: a switch rebuilds its
// sockets (links move with the socket of the same name), "any" sockets (MM_ANY) take the type they are linked to, a
// remote writes its changed controls into the nodes it controls (gen_remote.gd set_parameter). The Material Maker name
// of each node (remote widgets refer to it) is the data of its seed button.
//
// Startup only reads the tree and registers names, menu paths and the codegen callbacks. The sockets and buttons of a
// type are built from its .mmg when the type is first used (mm_node_def_ensure, from ui_nodes_make_node):
//   shader nodes   inputs, outputs and parameters of shader_model, generic "#" ports and parameters with generic_size 1
//   graph nodes    ports of the gen_inputs / gen_outputs ios nodes, parameters of the gen_parameters remote widgets
//
// Socket types: f VALUE, rgb RGB, rgba RGBA, sdf2d MM_SDF2D, sdf3d MM_SDF3D, sdf3dc MM_SDF3DC, tex3d_gs MM_TEX3D_GS,
// tex3d MM_TEX3D, v4v4 MM_V4V4, fill MM_FILL, any MM_ANY. Constant input defaults ("0.5", "vec4(1.0, 0.5, 0.0, 1.0)") become the
// socket's default_value, expressions ("$uv.x") stay Material Maker's (mm_node_input_expression) with a neutral socket value.
//
// Parameters to buttons, values kept in our formats (mm_node_parameters converts them back):
//   float      VALUE   [value], min / max, precision 1 / step
//   enum       ENUM    [index], data: the value names
//   boolean    BOOL    [0 or 1]
//   size       ENUM    [index], data: the sizes from 2^first to 2^last, Material Maker's value is first + index
//   color      a label row (CUSTOM mm_label) and an RGBA button [r, g, b, a]
//   gradient   CUSTOM mm_gradient  [r, g, b, a, pos, ..] as the Color Ramp, data[0]: Material Maker's interpolation
//              (0 constant, 1 linear, 2 smooth, 3 cubic)
//   curve      CUSTOM mm_curve     [x, y, ls, rs, ..] Material Maker's points with their slopes
//   string, file, image_path   CUSTOM mm_string   data: the text, a text field
//   others     CUSTOM mm_value     data: the value as json (polygon, polyline, splines, pixels, lattice), shown read only
// The last button of every node is a hidden CUSTOM mm_seed (height 0): [seed high, seed low, locked, set], the seed is
// high + low (a double kept in two floats). Canvases are decoded positionally, the node structs stay as they are.

#define MM_NODES_COLOR       0xff7a5c9e
#define MM_NODES_MAX_DEPTH   12
#define MM_NODES_PARAM_OTHER 0
#define MM_NODES_PARAM_FLOAT 1
#define MM_NODES_PARAM_ENUM  2
#define MM_NODES_PARAM_BOOL  3
#define MM_NODES_PARAM_SIZE  4
#define MM_NODES_PARAM_COLOR 5
#define MM_NODES_PARAM_GRAD  6
#define MM_NODES_PARAM_CURVE 7
#define MM_NODES_PARAM_STRING 8

typedef struct mm_nodes_param {
	char *name;   // Material Maker's parameter name
	char *label;  // Shown name
	i32   kind;   // MM_NODES_PARAM_*
	i32   button; // Index of its button
	i32   first;  // size: log2 of the first size
} mm_nodes_param_t;

typedef struct mm_nodes_type {
	char             *type;    // MM_<mmg name>
	char             *mm_type; // The .mmg name, "graph" for an inline graph
	char             *path;    // Tree path, "Noise/Fbm"
	char             *entry;   // The library entry as json, without its icon
	ui_node_t        *def;
	bool              built;
	bool              generic;
	bool              dynamic; // Sockets or buttons depend on the node (mm_nodes_is_dynamic)
	char             *error; // Why building failed
	mm_nodes_param_t *params;
	i32               param_count;
	char            **input_exprs; // Material Maker default of each input when not a constant, NULL for constants
	i32               input_count;
	i32               seed_button;
} mm_nodes_type_t;

ui_node_t_array_t       *nodes_material_mm  = NULL;
static mm_pack_t        *mm_nodes_pack      = NULL;
static any_map_t        *mm_nodes_types     = NULL; // Type string to mm_nodes_type_t
static mm_nodes_type_t **mm_nodes_list      = NULL;
static i32               mm_nodes_n         = 0;
static f32               mm_nodes_ms        = 0.0;
static mm_buf_t          mm_nodes_skips     = {0}; // Skipped library entries and why, malloc'd
static bool              mm_nodes_done_init = false;

// Json writing

static void mm_nodes_json_string(mm_buf_t *b, const char *s) {
	mm_buf_add(b, "\"");
	for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
		if (*p == '"' || *p == '\\') {
			char e[3] = {'\\', (char)*p, 0};
			mm_buf_add(b, e);
		}
		else if (*p == '\n') {
			mm_buf_add(b, "\\n");
		}
		else if (*p == '\t') {
			mm_buf_add(b, "\\t");
		}
		else if (*p == '\r') {
			mm_buf_add(b, "\\r");
		}
		else if (*p < 0x20) {
			mm_buf_printf(b, "\\u%04x", *p);
		}
		else {
			mm_buf_addn(b, (const char *)p, 1);
		}
	}
	mm_buf_add(b, "\"");
}

static void mm_nodes_json_write(mm_buf_t *b, mm_json_t *v) {
	if (v == NULL) {
		mm_buf_add(b, "null");
		return;
	}
	switch (v->type) {
	case MM_JSON_NULL:
		mm_buf_add(b, "null");
		break;
	case MM_JSON_BOOL:
		mm_buf_add(b, v->boolean ? "true" : "false");
		break;
	case MM_JSON_NUMBER:
		if (v->number != v->number || v->number > 1e300 || v->number < -1e300) {
			mm_buf_add(b, "0");
		}
		else if (v->is_int && v->number == floor(v->number) && fabs(v->number) < 9e15) {
			mm_buf_printf(b, "%lld", (long long)v->number);
		}
		else {
			mm_buf_printf(b, "%.17g", v->number);
		}
		break;
	case MM_JSON_STRING:
		mm_nodes_json_string(b, v->string != NULL ? v->string : "");
		break;
	case MM_JSON_ARRAY:
		mm_buf_add(b, "[");
		for (i32 i = 0; i < v->count; ++i) {
			if (i > 0) {
				mm_buf_add(b, ",");
			}
			mm_nodes_json_write(b, v->items[i]);
		}
		mm_buf_add(b, "]");
		break;
	case MM_JSON_OBJECT: {
		mm_buf_add(b, "{");
		bool first = true;
		for (i32 i = 0; i < v->count; ++i) {
			if (v->items[i] == NULL) { // Removed with mm_json_set(obj, key, NULL)
				continue;
			}
			if (!first) {
				mm_buf_add(b, ",");
			}
			first = false;
			mm_nodes_json_string(b, v->keys[i]);
			mm_buf_add(b, ":");
			mm_nodes_json_write(b, v->items[i]);
		}
		mm_buf_add(b, "}");
		break;
	}
	}
}

static char *mm_nodes_json_text(mm_json_t *v) { // In the arena
	mm_buf_t b = {0};
	mm_nodes_json_write(&b, v);
	return mm_buf_get(&b);
}

static mm_json_t *mm_nodes_num(double d) {
	mm_json_t *v = mm_json_new(MM_JSON_NUMBER);
	v->number    = d;
	v->is_int    = d == floor(d);
	return v;
}

static mm_json_t *mm_nodes_bool(bool b) {
	mm_json_t *v = mm_json_new(MM_JSON_BOOL);
	v->boolean   = b;
	return v;
}

static mm_json_t *mm_nodes_str(const char *s) {
	mm_json_t *v = mm_json_new(MM_JSON_STRING);
	v->string    = mm_strdup(s);
	return v;
}

static const char *mm_nodes_jstr(mm_json_t *o, const char *k) {
	mm_json_t *v = mm_json_get(o, k);
	return v != NULL && v->type == MM_JSON_STRING ? v->string : NULL;
}

// Registry

static mm_nodes_type_t *mm_nodes_find(char *type) {
	return type != NULL && mm_nodes_types != NULL ? any_map_get(mm_nodes_types, type) : NULL;
}

bool mm_node_is(ui_node_t *node) {
	return node != NULL && node->type != NULL && starts_with(node->type, "MM_") && mm_nodes_find(node->type) != NULL;
}

static i32 mm_nodes_visible = 0; // Types in the add menu

i32 mm_nodes_count() { // In the add menu
	return mm_nodes_visible;
}

i32 mm_nodes_count_all() {
	return mm_nodes_n;
}

f32 mm_nodes_init_ms() {
	return mm_nodes_ms;
}

char *mm_nodes_skipped() {
	return mm_nodes_skips.data != NULL ? mm_nodes_skips.data : "";
}

static void mm_nodes_skip(const char *fmt, const char *a, const char *b) {
	char line[512];
	snprintf(line, sizeof(line), fmt, a, b);
	size_t n = strlen(line);
	if (mm_nodes_skips.len + n + 2 > mm_nodes_skips.cap) {
		mm_nodes_skips.cap  = (mm_nodes_skips.len + n + 2) * 2;
		mm_nodes_skips.data = realloc(mm_nodes_skips.data, mm_nodes_skips.cap);
	}
	memcpy(mm_nodes_skips.data + mm_nodes_skips.len, line, n);
	mm_nodes_skips.len += n;
	mm_nodes_skips.data[mm_nodes_skips.len++] = '\n';
	mm_nodes_skips.data[mm_nodes_skips.len]   = 0;
}

// Sockets

static char *mm_nodes_socket_type(const char *mm) {
	static const char *map[][2] = {
	    {"f", "VALUE"},          {"rgb", "RGB"},         {"rgba", "RGBA"},   {"sdf2d", "MM_SDF2D"}, {"sdf3d", "MM_SDF3D"},
	    {"sdf3dc", "MM_SDF3DC"}, {"tex3d_gs", "MM_TEX3D_GS"}, {"tex3d", "MM_TEX3D"}, {"v4v4", "MM_V4V4"}, {"fill", "MM_FILL"}, {"any", "MM_ANY"},
	};
	for (i32 i = 0; mm != NULL && i < (i32)(sizeof(map) / sizeof(map[0])); ++i) {
		if (strcmp(mm, map[i][0]) == 0) {
			return (char *)map[i][1];
		}
	}
	return NULL;
}

static u32 mm_nodes_socket_color(char *type) {
	if (string_equals(type, "VALUE")) {
		return 0xffa1a1a1;
	}
	if (string_equals(type, "RGB") || string_equals(type, "RGBA")) {
		return 0xffc7c729;
	}
	// Material Maker's colors of its types (nodes/io_types.mmt)
	if (string_equals(type, "MM_SDF2D")) {
		return 0xffe8a31d;
	}
	if (string_equals(type, "MM_SDF3D")) {
		return 0xffeb6135;
	}
	if (string_equals(type, "MM_SDF3DC")) {
		return 0xffdf2626;
	}
	if (string_equals(type, "MM_TEX3D_GS")) {
		return 0xffa734dd;
	}
	if (string_equals(type, "MM_TEX3D")) {
		return 0xffe739d5;
	}
	if (string_equals(type, "MM_V4V4")) {
		return 0xffb79074;
	}
	if (string_equals(type, "MM_ANY")) {
		return 0xffdedede;
	}
	return 0xff2db76c; // MM_FILL
}

// Components of the socket type's value: float 1, vec2 2, vec3 3, vec4 4
static i32 mm_nodes_socket_size(char *type) {
	if (string_equals(type, "RGBA") || string_equals(type, "MM_V4V4") || string_equals(type, "MM_FILL")) {
		return 4;
	}
	if (string_equals(type, "RGB") || string_equals(type, "MM_TEX3D")) {
		return 3;
	}
	if (string_equals(type, "MM_SDF3DC")) {
		return 2;
	}
	return 1;
}

// A number literal, all of s
static bool mm_nodes_number(const char *s, f32 *out) {
	while (*s == ' ') {
		s++;
	}
	char  *end;
	double d = strtod(s, &end);
	if (end == s) {
		return false;
	}
	while (*end == ' ') {
		end++;
	}
	if (*end != 0) {
		return false;
	}
	*out = (f32)d;
	return true;
}

// "0.5", "vec3(0.5)", "vec4(1.0, 0.5, 0.0, 1.0)": the components, 0 when not a constant
static i32 mm_nodes_constant(const char *expr, f32 *v) {
	if (expr == NULL) {
		return 0;
	}
	if (mm_nodes_number(expr, &v[0])) {
		return 1;
	}
	char *s = mm_strip(expr);
	if (strlen(s) < 7 || strncmp(s, "vec", 3) != 0 || s[3] < '2' || s[3] > '4' || s[4] != '(' || s[strlen(s) - 1] != ')') {
		return 0;
	}
	char *args = mm_strndup(s + 5, strlen(s) - 6);
	i32   n    = 0;
	char *p    = args;
	while (n < 4) {
		char *comma = strchr(p, ',');
		if (comma != NULL) {
			*comma = 0;
		}
		if (!mm_nodes_number(p, &v[n])) {
			return 0;
		}
		n++;
		if (comma == NULL) {
			break;
		}
		p = comma + 1;
	}
	i32 dim = s[3] - '0';
	if (n == 1) { // vec3(0.5)
		for (i32 i = 1; i < dim; ++i) {
			v[i] = v[0];
		}
		return dim;
	}
	return n == dim ? n : 0;
}

// Without the "2:" layout prefix of Material Maker labels, NULL when nothing is left
static char *mm_nodes_label(const char *label) {
	if (label == NULL) {
		return NULL;
	}
	const char *p = label;
	while (*p >= '0' && *p <= '9') {
		p++;
	}
	if (p != label && *p == ':') {
		label = p + 1;
	}
	char *s = mm_strip(label);
	return s[0] != 0 ? s : NULL;
}

static char *mm_nodes_pick_label(mm_json_t *o, const char *a, const char *b, const char *c) {
	char *l = mm_nodes_label(mm_nodes_jstr(o, a));
	if (l == NULL && b != NULL) {
		l = mm_nodes_label(mm_nodes_jstr(o, b));
	}
	if (l == NULL && c != NULL) {
		l = mm_nodes_label(mm_nodes_jstr(o, c));
	}
	return l != NULL ? l : "";
}

// A name not yet used by the sockets
static char *mm_nodes_unique(ui_node_socket_array_t *sockets, char *name) {
	char *n = name[0] != 0 ? name : "Input";
	for (i32 k = 2;; ++k) {
		bool used = false;
		for (i32 i = 0; i < sockets->length && !used; ++i) {
			used = string_equals(sockets->buffer[i]->name, n);
		}
		if (!used) {
			return string_copy(n);
		}
		n = string_tmp("%s %d", name, k);
	}
}

static ui_node_socket_t *mm_nodes_socket(ui_node_socket_array_t *sockets, char *name, char *type, f32 *v, i32 n) {
	i32          size = mm_nodes_socket_size(type);
	f32_array_t *dv   = f32_array_create(size);
	for (i32 i = 0; i < size; ++i) {
		dv->buffer[i] = i < n ? v[i] : (n == 1 && i < 3 ? v[0] : (i == 3 && string_equals(type, "RGBA") ? 1.0 : 0.0));
	}
	return ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                     .node_id       = 0,
	                                     .name          = mm_nodes_unique(sockets, name),
	                                     .type          = type,
	                                     .color         = mm_nodes_socket_color(type),
	                                     .default_value = dv,
	                                     .min           = 0.0,
	                                     .max           = 1.0,
	                                     .precision     = 100,
	                                     .display       = 0});
}

// Generic expansion (gen_shader.gd set_shader_model): the run of items whose field holds "#" repeats gs times with # as
// 1, 2, .. Outputs are generic by their expression (indirect).

static bool mm_nodes_generic_item(mm_json_t *item, const char *field, bool indirect) {
	const char *s = mm_nodes_jstr(item, field);
	if (indirect && s != NULL) {
		s = mm_nodes_jstr(item, s);
	}
	return s != NULL && strchr(s, '#') != NULL;
}

static void mm_nodes_replace_field(mm_json_t *o, const char *k, i32 index) {
	const char *s = mm_nodes_jstr(o, k);
	if (s != NULL) {
		mm_json_set(o, k, mm_nodes_str(mm_replace(s, "#", mm_printf("%d", index))));
	}
}

static mm_json_t *mm_nodes_expand(mm_json_t *arr, const char *field, bool indirect, i32 gs, bool *generic) {
	if (arr == NULL || arr->type != MM_JSON_ARRAY) {
		return arr;
	}
	i32 first = -1;
	i32 last  = -1;
	for (i32 i = 0; i < arr->count; ++i) {
		if (mm_nodes_generic_item(arr->items[i], field, indirect)) {
			if (first == -1) {
				first = i;
			}
			else if (last != -1) {
				return arr; // Two runs, Material Maker does not expand
			}
		}
		else if (first != -1 && last == -1) {
			last = i;
		}
	}
	if (first == -1) {
		return arr;
	}
	if (last == -1) {
		last = arr->count;
	}
	*generic      = true;
	mm_json_t *rv = mm_json_new(MM_JSON_ARRAY);
	for (i32 i = 0; i < first; ++i) {
		mm_json_push(rv, arr->items[i]);
	}
	for (i32 k = 0; k < gs; ++k) {
		for (i32 i = first; i < last; ++i) {
			mm_json_t *c = mm_json_copy(arr->items[i]);
			if (indirect) {
				mm_nodes_replace_field(c, mm_nodes_jstr(c, field), k + 1);
				mm_nodes_replace_field(c, "shortdesc", k + 1);
			}
			else {
				mm_nodes_replace_field(c, field, k + 1);
				mm_nodes_replace_field(c, "label", k + 1);
				mm_nodes_replace_field(c, "shortdesc", k + 1);
			}
			mm_json_push(rv, c);
		}
	}
	for (i32 i = last; i < arr->count; ++i) {
		mm_json_push(rv, arr->items[i]);
	}
	return rv;
}

// Parameter definitions (gen_base.gd get_parameter_defs) of a node as saved in a graph, nodes being its graph's nodes

static mm_json_t *mm_nodes_param_defs(mm_json_t *data, mm_json_t *nodes, i32 depth);
static mm_json_t *mm_nodes_builtin_model(const char *type, mm_json_t *data, mm_json_t *canvas_nodes);

static mm_json_t *mm_nodes_def(const char *name, const char *type, double dflt, double first) {
	mm_json_t *d = mm_json_new(MM_JSON_OBJECT);
	mm_json_set(d, "name", mm_nodes_str(name));
	mm_json_set(d, "type", mm_nodes_str(type));
	mm_json_set(d, "default", mm_nodes_num(dflt));
	if (first >= 0) {
		mm_json_set(d, "first", mm_nodes_num(first));
		mm_json_set(d, "last", mm_nodes_num(13));
	}
	return d;
}

static mm_json_t *mm_nodes_child(mm_json_t *nodes, const char *name) {
	for (i32 i = 0; nodes != NULL && name != NULL && i < nodes->count; ++i) {
		const char *n = mm_nodes_jstr(nodes->items[i], "name");
		if (n != NULL && strcmp(n, name) == 0) {
			return nodes->items[i];
		}
	}
	return NULL;
}

static i32 mm_nodes_str_cmp(const void *a, const void *b) {
	return strcmp(*(char *const *)a, *(char *const *)b);
}

// gen_remote.gd get_parameter_defs
static mm_json_t *mm_nodes_remote_defs(mm_json_t *widgets, mm_json_t *nodes, i32 depth) {
	mm_json_t *rv = mm_json_new(MM_JSON_ARRAY);
	for (i32 i = 0; widgets != NULL && i < widgets->count; ++i) {
		mm_json_t  *w    = widgets->items[i];
		const char *type = mm_nodes_jstr(w, "type");
		const char *name = mm_nodes_jstr(w, "name");
		mm_json_t  *p    = NULL;
		if (type == NULL || name == NULL) {
			continue;
		}
		if (strcmp(type, "config_control") == 0) {
			mm_json_t *cfg = mm_json_get(w, "configurations");
			i32        n   = cfg != NULL && cfg->type == MM_JSON_OBJECT ? cfg->count : 0;
			char     **ks  = mm_alloc(sizeof(char *) * (n + 1));
			for (i32 k = 0; k < n; ++k) {
				ks[k] = cfg->keys[k];
			}
			qsort(ks, n, sizeof(char *), mm_nodes_str_cmp);
			p = mm_json_new(MM_JSON_OBJECT);
			mm_json_set(p, "name", mm_nodes_str(name));
			if (n == 2 && strcmp(ks[0], "False") == 0 && strcmp(ks[1], "True") == 0) {
				mm_json_set(p, "type", mm_nodes_str("boolean"));
			}
			else {
				mm_json_set(p, "type", mm_nodes_str("enum"));
				mm_json_t *values = mm_json_new(MM_JSON_ARRAY);
				for (i32 k = 0; k < n; ++k) {
					mm_json_t *e = mm_json_new(MM_JSON_OBJECT);
					mm_json_set(e, "name", mm_nodes_str(ks[k]));
					mm_json_set(e, "value", mm_nodes_str(ks[k]));
					mm_json_push(values, e);
				}
				mm_json_set(p, "values", values);
			}
			mm_json_set(p, "default", mm_nodes_num(0));
		}
		else if (strcmp(type, "linked_control") == 0) {
			// The first linked widget that still exists gives the definition (fix() drops the others)
			mm_json_t *links = mm_json_get(w, "linked_widgets");
			for (i32 k = 0; links != NULL && k < links->count && p == NULL; ++k) {
				mm_json_t  *target = mm_nodes_child(nodes, mm_nodes_jstr(links->items[k], "node"));
				const char *widget = mm_nodes_jstr(links->items[k], "widget");
				mm_json_t  *defs   = target != NULL && widget != NULL ? mm_nodes_param_defs(target, nodes, depth + 1) : NULL;
				for (i32 d = 0; defs != NULL && d < defs->count; ++d) {
					const char *dn = mm_nodes_jstr(defs->items[d], "name");
					if (dn != NULL && strcmp(dn, widget) == 0) {
						p = mm_json_copy(defs->items[d]);
						break;
					}
				}
			}
			if (p == NULL) {
				continue;
			}
			mm_json_set(p, "name", mm_nodes_str(name));
			mm_json_set(p, "label", mm_nodes_str(mm_json_str(mm_json_get(w, "label"), "")));
		}
		else if (strcmp(type, "named_parameter") == 0) {
			p = mm_json_new(MM_JSON_OBJECT);
			mm_json_set(p, "name", mm_nodes_str(name));
			mm_json_set(p, "label", mm_nodes_str(mm_json_str(mm_json_get(w, "label"), "")));
			mm_json_set(p, "type", mm_nodes_str("float"));
			mm_json_set(p, "min", mm_nodes_num(mm_json_num(mm_json_get(w, "min"), 0)));
			mm_json_set(p, "max", mm_nodes_num(mm_json_num(mm_json_get(w, "max"), 1)));
			mm_json_set(p, "step", mm_nodes_num(mm_json_num(mm_json_get(w, "step"), 0.01)));
			mm_json_set(p, "default", mm_nodes_num(mm_json_num(mm_json_get(w, "default"), 0.5)));
		}
		else {
			continue;
		}
		if (mm_json_get(w, "shortdesc") != NULL) {
			mm_json_set(p, "shortdesc", mm_json_get(w, "shortdesc"));
		}
		mm_json_push(rv, p);
	}
	return rv;
}

static mm_json_t *mm_nodes_param_defs(mm_json_t *data, mm_json_t *nodes, i32 depth) {
	mm_json_t *rv = mm_json_new(MM_JSON_ARRAY);
	if (data == NULL || depth > MM_NODES_MAX_DEPTH) {
		return rv;
	}
	mm_json_t *sm = mm_json_get(data, "shader_model");
	if (sm != NULL) {
		bool generic = false;
		i32  gs      = (i32)mm_json_num(mm_json_get(data, "generic_size"), 1);
		return mm_nodes_expand(mm_json_get(sm, "parameters"), "name", false, gs < 1 ? 1 : gs, &generic);
	}
	if (mm_json_get(data, "nodes") != NULL) { // A graph: its gen_parameters
		mm_json_t *inner = mm_json_get(data, "nodes");
		mm_json_t *gp    = mm_nodes_child(inner, "gen_parameters");
		return gp != NULL ? mm_nodes_remote_defs(mm_json_get(gp, "widgets"), inner, depth) : rv;
	}
	if (mm_json_get(data, "widgets") != NULL) {
		return mm_nodes_remote_defs(mm_json_get(data, "widgets"), nodes, depth);
	}
	const char *type = mm_nodes_jstr(data, "type");
	if (type == NULL) {
		return rv;
	}
	mm_json_t *bm = mm_nodes_builtin_model(type, data, nodes); // The generators written in GDScript
	if (bm != NULL) {
		return mm_json_get(bm, "parameters");
	}
	mm_json_t *lib = mm_library_node(type);
	if (lib == NULL) {
		return rv;
	}
	if (mm_json_get(data, "generic_size") != NULL && mm_json_get(lib, "shader_model") != NULL) {
		lib = mm_json_copy(lib);
		mm_json_set(lib, "generic_size", mm_json_get(data, "generic_size"));
	}
	return mm_nodes_param_defs(lib, nodes, depth + 1);
}

// Values set by a saved node: its parameters object, or parameters as members of the node (old files)
static void mm_nodes_apply_values(mm_json_t *values, mm_json_t *data, mm_json_t *defs) {
	mm_json_t *params = mm_json_get(data, "parameters");
	if (params != NULL && params->type == MM_JSON_OBJECT) {
		for (i32 i = 0; i < params->count; ++i) {
			mm_json_set(values, params->keys[i], params->items[i]);
		}
	}
	else if (params == NULL) {
		for (i32 i = 0; defs != NULL && i < defs->count; ++i) {
			const char *pn = mm_nodes_jstr(defs->items[i], "name");
			if (pn != NULL && strcmp(pn, "type") != 0 && mm_json_get(data, pn) != NULL) {
				mm_json_set(values, pn, mm_json_get(data, pn));
			}
		}
	}
}

// Buttons

static ui_node_button_t *mm_nodes_button(char *name, char *type, f32_array_t *value, u8_array_t *data, f32 min, f32 max, f32 precision,
                                         f32 height) {
	return ALLOC_INIT(ui_node_button_t, {.name          = name,
	                                     .type          = type,
	                                     .output        = -1,
	                                     .default_value = value,
	                                     .data          = data,
	                                     .min           = min,
	                                     .max           = max,
	                                     .precision     = precision,
	                                     .height        = height});
}

static u8_array_t *mm_nodes_text_data(const char *s) {
	return u8_array_create_from_string((char *)s);
}

// Gradient value (MMGradient.serialize, or the array of points of old files) to [r, g, b, a, pos, ..] and interpolation
static f32_array_t *mm_nodes_gradient_values(mm_json_t *v, i32 *interp) {
	mm_json_t *points = v != NULL && v->type == MM_JSON_ARRAY ? v : mm_json_get(v, "points");
	*interp           = (i32)mm_json_num(mm_json_get(v, "interpolation"), 1);
	f32_array_t *a    = f32_array_create(0);
	for (i32 i = 0; points != NULL && points->type == MM_JSON_ARRAY && i < points->count; ++i) {
		mm_json_t *p = points->items[i];
		f32_array_push(a, mm_json_num(mm_json_get(p, "r"), 0));
		f32_array_push(a, mm_json_num(mm_json_get(p, "g"), 0));
		f32_array_push(a, mm_json_num(mm_json_get(p, "b"), 0));
		f32_array_push(a, mm_json_num(mm_json_get(p, "a"), 1));
		f32_array_push(a, mm_json_num(mm_json_get(p, "pos"), 0));
	}
	if (a->length == 0) {
		f32 d[10] = {0, 0, 0, 1, 0, 1, 1, 1, 1, 1};
		for (i32 i = 0; i < 10; ++i) {
			f32_array_push(a, d[i]);
		}
	}
	return a;
}

// Curve value (MMCurve.serialize) to [x, y, ls, rs, ..]
static f32_array_t *mm_nodes_curve_values(mm_json_t *v) {
	mm_json_t   *points = v != NULL && v->type == MM_JSON_ARRAY ? v : mm_json_get(v, "points");
	f32_array_t *a      = f32_array_create(0);
	for (i32 i = 0; points != NULL && points->type == MM_JSON_ARRAY && i < points->count; ++i) {
		mm_json_t *p = points->items[i];
		f32_array_push(a, mm_json_num(mm_json_get(p, "x"), 0));
		f32_array_push(a, mm_json_num(mm_json_get(p, "y"), 0));
		f32_array_push(a, mm_json_num(mm_json_get(p, "ls"), 0));
		f32_array_push(a, mm_json_num(mm_json_get(p, "rs"), 0));
	}
	if (a->length < 8) { // MMCurve's default, a line
		a->length = 0;
		f32 d[8]  = {0, 0, 0, 1, 1, 1, 1, 0};
		for (i32 i = 0; i < 8; ++i) {
			f32_array_push(a, d[i]);
		}
	}
	return a;
}

// A float parameter given as an expression ("$uv.x", "$s * 2.0"): Material Maker evaluates it in the shader. Kept as the
// data of the VALUE button, it takes the place of the slider's value. NULL for numbers.
static u8_array_t *mm_nodes_float_expr(mm_json_t *v) {
	if (v == NULL || v->type != MM_JSON_STRING || v->string == NULL) {
		return NULL;
	}
	f32 n;
	return mm_nodes_number(v->string, &n) ? NULL : mm_nodes_text_data(v->string);
}

// Parameters to buttons: appends the buttons of parameter definition pd with value (NULL: its default) to n and records
// where they went in params
static void mm_nodes_add_param(mm_nodes_param_t **params, i32 *count, ui_node_t *def, mm_json_t *pd, mm_json_t *value) {
	const char *pname = mm_nodes_jstr(pd, "name");
	const char *ptype = mm_nodes_jstr(pd, "type");
	if (pname == NULL || ptype == NULL) {
		return;
	}
	char *label = string_copy(mm_nodes_pick_label(pd, "label", "shortdesc", "name"));
	if (label[0] == 0) {
		label = string_copy((char *)pname);
	}
	mm_nodes_param_t p = {.name = string_copy((char *)pname), .label = label, .kind = MM_NODES_PARAM_OTHER, .button = 0, .first = 0};
	mm_json_t       *dv = mm_json_get(pd, "default");
	if (strcmp(ptype, "float") == 0) {
		f32 min  = mm_json_num(mm_json_get(pd, "min"), 0);
		f32 max  = mm_json_num(mm_json_get(pd, "max"), 1);
		f32 step = mm_json_num(mm_json_get(pd, "step"), 0.005);
		f32 v    = mm_json_num(value, mm_json_num(dv, 0));
		if (max < min) {
			max = min + 1;
		}
		f32 prec = step > 0 ? roundf(1.0 / step) : 100;
		p.kind   = MM_NODES_PARAM_FLOAT;
		p.button = def->buttons->length;
		any_array_push(def->buttons, mm_nodes_button(label, "VALUE", f32_array_create_x(v), mm_nodes_float_expr(value), min, max, prec < 1 ? 1 : prec, 0));
	}
	else if (strcmp(ptype, "enum") == 0) {
		mm_json_t *values = mm_json_get(pd, "values");
		mm_buf_t   b      = {0};
		i32        n      = values != NULL ? values->count : 0;
		for (i32 i = 0; i < n; ++i) {
			if (i > 0) {
				mm_buf_add(&b, "\n");
			}
			mm_json_t *e = values->items[i]; // {name, value}, or a plain name (gen_reroute.gd)
			mm_buf_add(&b, e != NULL && e->type == MM_JSON_STRING ? e->string : mm_json_str(mm_json_get(e, "name"), ""));
		}
		i32 v = (i32)mm_json_num(value, mm_json_num(dv, 0));
		if (v < 0 || v >= n) {
			v = 0;
		}
		p.kind   = MM_NODES_PARAM_ENUM;
		p.button = def->buttons->length;
		any_array_push(def->buttons, mm_nodes_button(label, "ENUM", f32_array_create_x(v), mm_nodes_text_data(mm_buf_get(&b)), 0, n > 0 ? n - 1 : 0, 100, 0));
	}
	else if (strcmp(ptype, "boolean") == 0) {
		bool v   = mm_json_bool(value, mm_json_bool(dv, false));
		p.kind   = MM_NODES_PARAM_BOOL;
		p.button = def->buttons->length;
		any_array_push(def->buttons, mm_nodes_button(label, "BOOL", f32_array_create_x(v ? 1 : 0), NULL, 0, 1, 100, 0));
	}
	else if (strcmp(ptype, "size") == 0) {
		i32 first = (i32)mm_json_num(mm_json_get(pd, "first"), 4);
		i32 last  = (i32)mm_json_num(mm_json_get(pd, "last"), 13);
		if (last < first) {
			last = first;
		}
		// init_parameters sets a parameter without value to default - first
		i32      v = value != NULL ? (i32)mm_json_num(value, first) : (i32)mm_json_num(dv, first) - first;
		mm_buf_t b = {0};
		for (i32 k = first; k <= last; ++k) {
			mm_buf_printf(&b, k > first ? "\n%d" : "%d", 1 << k);
		}
		i32 index = v - first;
		index     = index < 0 ? 0 : index > last - first ? last - first : index;
		p.kind    = MM_NODES_PARAM_SIZE;
		p.first   = first;
		p.button  = def->buttons->length;
		any_array_push(def->buttons, mm_nodes_button(label, "ENUM", f32_array_create_x(index), mm_nodes_text_data(mm_buf_get(&b)), 0, last - first, 100, 0));
	}
	else if (strcmp(ptype, "color") == 0) {
		mm_json_t *c = value != NULL && value->type == MM_JSON_OBJECT ? value : dv;
		p.kind       = MM_NODES_PARAM_COLOR;
		any_array_push(def->buttons, mm_nodes_button("mm_label", "CUSTOM", f32_array_create_x(0), mm_nodes_text_data(label), 0, 1, 100, 1));
		p.button = def->buttons->length;
		any_array_push(def->buttons,
		               mm_nodes_button(label, "RGBA",
		                               f32_array_create_xyzw(mm_json_num(mm_json_get(c, "r"), 1), mm_json_num(mm_json_get(c, "g"), 1),
		                                                     mm_json_num(mm_json_get(c, "b"), 1), mm_json_num(mm_json_get(c, "a"), 1)),
		                               NULL, 0, 1, 100, 0));
	}
	else if (strcmp(ptype, "gradient") == 0) {
		i32          interp = 1;
		f32_array_t *vals   = mm_nodes_gradient_values(value != NULL ? value : dv, &interp);
		u8_array_t  *data   = u8_array_create(1);
		data->buffer[0]     = (u8)(interp < 0 || interp > 3 ? 1 : interp);
		p.kind              = MM_NODES_PARAM_GRAD;
		p.button            = def->buttons->length;
		any_array_push(def->buttons, mm_nodes_button("mm_gradient", "CUSTOM", vals, data, 0, 1, 100, 5.2));
	}
	else if (strcmp(ptype, "curve") == 0) {
		p.kind   = MM_NODES_PARAM_CURVE;
		p.button = def->buttons->length;
		any_array_push(def->buttons, mm_nodes_button("mm_curve", "CUSTOM", mm_nodes_curve_values(value != NULL ? value : dv), NULL, 0, 1, 100, 8.2));
	}
	else if (strcmp(ptype, "string") == 0 || strcmp(ptype, "file") == 0 || strcmp(ptype, "image_path") == 0) { // Text field
		p.kind   = MM_NODES_PARAM_STRING;
		p.button = def->buttons->length;
		any_array_push(def->buttons, mm_nodes_button("mm_string", "CUSTOM", f32_array_create_x(0), mm_nodes_text_data(mm_json_str(value != NULL ? value : dv, "")), 0, 1,
		                                             100, 1));
	}
	else { // polygon, polyline, splines, pixels, lattice: kept as they are
		mm_json_t *v = value != NULL ? value : dv;
		p.kind       = MM_NODES_PARAM_OTHER;
		p.button     = def->buttons->length;
		any_array_push(def->buttons, mm_nodes_button("mm_value", "CUSTOM", f32_array_create_x(0), mm_nodes_text_data(mm_nodes_json_text(v)), 0, 1, 100, 1));
		p.label = string_copy(string_tmp("%s: %s", label, ptype));
	}
	*params             = realloc(*params, sizeof(mm_nodes_param_t) * (*count + 1));
	(*params)[(*count)++] = p;
}

static void mm_nodes_add_port(mm_nodes_type_t *t, ui_node_t *def, mm_json_t *port, bool input, bool graph) {
	const char *mt   = mm_nodes_jstr(port, "type");
	char       *type = mm_nodes_socket_type(mt);
	if (type == NULL) {
		type = "VALUE";
	}
	f32 v[4] = {0, 0, 0, 1};
	if (input) {
		char       *name = graph ? mm_nodes_pick_label(port, "name", "shortdesc", NULL) : mm_nodes_pick_label(port, "label", "shortdesc", "name");
		const char *expr = mm_nodes_jstr(port, "default");
		i32         n    = graph ? 0 : mm_nodes_constant(expr, v);
		if (n == 0) { // Expression, or a graph port: Material Maker's business, neutral value
			v[0] = v[1] = v[2] = 0;
			v[3]               = 1;
		}
		any_array_push(def->inputs, mm_nodes_socket(def->inputs, name, type, v, n == 0 ? 4 : n));
		if (t != NULL) {
			t->input_exprs                   = realloc(t->input_exprs, sizeof(char *) * (t->input_count + 1));
			t->input_exprs[t->input_count++] = n == 0 ? strdup(expr != NULL ? expr : "") : NULL;
		}
	}
	else {
		char *name = graph ? mm_nodes_pick_label(port, "name", "shortdesc", NULL) : mm_nodes_pick_label(port, "shortdesc", "label", NULL);
		v[0] = v[1] = v[2] = 0;
		any_array_push(def->outputs, mm_nodes_socket(def->outputs, name[0] != 0 ? name : "Output", type, v, 4));
	}
}

// output_set_type: an output without type has it as the name of its expression member
static mm_json_t *mm_nodes_output_typed(mm_json_t *o) {
	if (mm_nodes_jstr(o, "type") != NULL) {
		return o;
	}
	static const char *types[] = {"f", "rgb", "rgba", "sdf2d", "sdf3d", "sdf3dc", "tex3d_gs", "tex3d", "v4v4", "fill"};
	for (i32 i = 0; i < (i32)(sizeof(types) / sizeof(types[0])); ++i) {
		if (mm_json_get(o, types[i]) != NULL) {
			o = mm_json_copy(o);
			mm_json_set(o, "type", mm_nodes_str(types[i]));
			return o;
		}
	}
	return o;
}

static double mm_nodes_seed_of(mm_json_t *data, bool *locked, bool *has) {
	mm_json_t *sl = mm_json_get(data, "seed_locked");
	*locked       = sl != NULL && sl->type == MM_JSON_BOOL && sl->boolean;
	*has          = true;
	if (mm_json_get(data, "seed_int") != NULL) {
		return mm_json_num(mm_json_get(data, "seed_int"), 0) / 4294967296.0;
	}
	if (mm_json_get(data, "seed") != NULL) {
		return mm_json_num(mm_json_get(data, "seed"), 0);
	}
	if (mm_json_get(data, "seed_value") != NULL) {
		*locked = true;
		return mm_json_num(mm_json_get(data, "seed_value"), 0);
	}
	*has = false;
	return 0;
}

// Generators written in GDScript (engine/nodes/gen_*.gd): their ports and parameters as a shader_model, from the node as
// saved (data: version of a buffer, outputs and choices of a switch, io of a portal). NULL for the other types.

static bool mm_nodes_is_builtin(const char *type) {
	static const char *types[] = {"buffer", "iterate_buffer", "switch", "reroute", "remote", "portal", "image", "text"};
	for (i32 i = 0; type != NULL && i < (i32)(sizeof(types) / sizeof(types[0])); ++i) {
		if (strcmp(type, types[i]) == 0) {
			return true;
		}
	}
	return false;
}

// Types whose sockets or buttons depend on the node: they keep a state button, updated as the node is drawn
// Material Maker nodes whose definition is in the node: "graph" (nodes, connections), "shader" (shader_model), "sdf"
// (sdf_scene, Easy SDF)
static bool mm_nodes_is_inline(const char *type) {
	return type != NULL && (strcmp(type, "graph") == 0 || strcmp(type, "shader") == 0 || strcmp(type, "sdf") == 0);
}

// Types whose sockets or buttons depend on the node: they keep a state button, updated as the node is drawn. Generic
// shader nodes (# ports) are too, as their generic_size (mm_nodes_build).
static bool mm_nodes_is_dynamic(const char *type) {
	return type != NULL && (strcmp(type, "buffer") == 0 || strcmp(type, "switch") == 0 || strcmp(type, "reroute") == 0 || strcmp(type, "remote") == 0 ||
	                        strcmp(type, "portal") == 0 || mm_nodes_is_inline(type));
}

static i32 mm_nodes_clampi(double v, i32 lo, i32 hi) {
	i32 i = (i32)floor(v + 0.5);
	return i < lo ? lo : i > hi ? hi : i;
}

static i32 mm_nodes_switch_size(mm_json_t *data, const char *name, i32 dflt, i32 lo) {
	return mm_nodes_clampi(mm_json_num(mm_json_get(mm_json_get(data, "parameters"), name), dflt), lo, 26); // The editor stops at 5, files do not
}

static mm_json_t *mm_nodes_builtin_model(const char *type, mm_json_t *data, mm_json_t *canvas_nodes) {
	if (!mm_nodes_is_builtin(type)) {
		return NULL;
	}
	const char *size = "{\"name\":\"size\",\"label\":\"Size\",\"type\":\"size\",\"first\":4,\"last\":13,\"default\":4}";
	const char *bits = "{\"name\":\"filter\",\"label\":\"Filter\",\"type\":\"boolean\",\"default\":true},"
	                   "{\"name\":\"mipmap\",\"label\":\"Mipmap\",\"type\":\"boolean\",\"default\":true},"
	                   "{\"name\":\"f32\",\"label\":\"32 bits\",\"type\":\"boolean\",\"default\":false}";
	mm_buf_t    b    = {0};
	if (strcmp(type, "buffer") == 0) {
		i32 version = (i32)mm_json_num(mm_json_get(data, "version"), 0);
		mm_buf_printf(&b, "{\"inputs\":[{\"name\":\"in\",\"label\":\"In\",\"type\":\"rgba\"}],\"outputs\":[{\"type\":\"rgba\"}%s],\"parameters\":[%s%s%s]}",
		              version == 0 ? ",{\"type\":\"rgba\"}" : "", size,
		              version == 0   ? ",{\"name\":\"lod\",\"label\":\"Lod\",\"type\":\"float\",\"min\":0,\"max\":10,\"step\":0.01,\"default\":0}"
		              : version == 2 ? ","
		                             : "",
		              version == 2 ? bits : "");
	}
	else if (strcmp(type, "iterate_buffer") == 0) {
		mm_buf_printf(&b,
		              "{\"inputs\":[{\"name\":\"in\",\"label\":\"In\",\"type\":\"rgba\"},{\"name\":\"loop_in\",\"label\":\"Loop In\",\"type\":\"rgba\"}],\"outputs\":[{\"type\":\"rgba\"},{\"type\":\"rgba\"}],"
		              "\"parameters\":[%s,{\"name\":\"shrink\",\"label\":\"Shrink\",\"type\":\"boolean\",\"default\":false},"
		              "{\"name\":\"autostop\",\"label\":\"Auto stop\",\"type\":\"boolean\",\"default\":false},"
		              "{\"name\":\"iterations\",\"label\":\"Iterations\",\"type\":\"float\",\"min\":1,\"max\":50,\"step\":1,\"default\":5},%s]}",
		              size, bits);
	}
	else if (strcmp(type, "switch") == 0) { // Inputs A1 B1 .. A2 B2 .., one output per letter
		i32 outputs = mm_nodes_switch_size(data, "outputs", 2, 1);
		i32 choices = mm_nodes_switch_size(data, "choices", 2, 2);
		mm_buf_add(&b, "{\"inputs\":[");
		for (i32 c = 0; c < choices; ++c) {
			for (i32 o = 0; o < outputs; ++o) {
				mm_buf_printf(&b, "%s{\"name\":\"%c%d\",\"label\":\"%c%d\",\"type\":\"any\"}", c + o > 0 ? "," : "", 'A' + o, c + 1, 'A' + o, c + 1);
			}
		}
		mm_buf_add(&b, "],\"outputs\":[");
		for (i32 o = 0; o < outputs; ++o) {
			mm_buf_printf(&b, "%s{\"name\":\"%c\",\"shortdesc\":\"%c\",\"type\":\"any\"}", o > 0 ? "," : "", 'A' + o, 'A' + o);
		}
		mm_buf_printf(&b,
		              "],\"parameters\":[{\"name\":\"outputs\",\"label\":\"Outputs\",\"type\":\"float\",\"min\":1,\"max\":5,\"step\":1,\"default\":2},"
		              "{\"name\":\"choices\",\"label\":\"Choices\",\"type\":\"float\",\"min\":2,\"max\":5,\"step\":1,\"default\":2},"
		              "{\"name\":\"source\",\"label\":\"Source\",\"type\":\"float\",\"min\":0,\"max\":%d,\"step\":1,\"default\":0}]}",
		              choices - 1);
	}
	else if (strcmp(type, "reroute") == 0) {
		mm_buf_add(&b, "{\"inputs\":[{\"name\":\"in\",\"label\":\"In\",\"type\":\"any\"}],\"outputs\":[{\"shortdesc\":\"Out\",\"type\":\"any\"}],"
		               "\"parameters\":[{\"name\":\"preview\",\"label\":\"Preview\",\"type\":\"enum\",\"values\":[\"none\",\"small\",\"large\",\"huge\"],\"default\":0}]}");
	}
	else if (strcmp(type, "remote") == 0) { // Controls of other nodes of the canvas, no ports
		mm_json_t *m = mm_json_new(MM_JSON_OBJECT);
		mm_json_set(m, "inputs", mm_json_new(MM_JSON_ARRAY));
		mm_json_set(m, "outputs", mm_json_new(MM_JSON_ARRAY));
		mm_json_set(m, "parameters", mm_nodes_remote_defs(mm_json_get(data, "widgets"), canvas_nodes, 0));
		return m;
	}
	else if (strcmp(type, "portal") == 0) { // io 0: In with an input, 1: Out with an output
		i32         io   = (i32)mm_json_num(mm_json_get(data, "io"), 0);
		const char *port = mm_nodes_jstr(data, "port_type") != NULL ? mm_nodes_jstr(data, "port_type") : "any";
		mm_buf_printf(&b, "{\"inputs\":[%s],\"outputs\":[%s],\"parameters\":[{\"name\":\"link\",\"label\":\"Link\",\"type\":\"string\",\"default\":\"aperture_1\"}]}",
		              io == 0 ? mm_printf("{\"name\":\"in\",\"label\":\"In\",\"type\":\"%s\"}", port) : "",
		              io == 1 ? mm_printf("{\"shortdesc\":\"Out\",\"type\":\"%s\"}", port) : "");
	}
	else if (strcmp(type, "image") == 0) {
		mm_buf_add(&b, "{\"inputs\":[],\"outputs\":[{\"type\":\"rgba\"}],\"parameters\":["
		               "{\"name\":\"image\",\"label\":\"Image\",\"type\":\"image_path\",\"default\":\"\"},"
		               "{\"name\":\"fix_ar\",\"label\":\"Fix Aspect Ratio\",\"type\":\"boolean\",\"default\":false},"
		               "{\"name\":\"clamp\",\"label\":\"Clamp\",\"type\":\"boolean\",\"default\":false},"
		               "{\"name\":\"filter\",\"label\":\"Filter\",\"type\":\"boolean\",\"default\":true}]}");
	}
	else { // text
		mm_buf_add(&b, "{\"inputs\":[],\"outputs\":[{\"type\":\"rgba\"}],\"parameters\":["
		               "{\"name\":\"text\",\"label\":\"Text\",\"type\":\"string\",\"default\":\"Text\"},"
		               "{\"name\":\"font\",\"label\":\"Font\",\"type\":\"file\",\"default\":\"\"},"
		               "{\"name\":\"fg\",\"label\":\"Foreground\",\"type\":\"color\",\"default\":{\"r\":1,\"g\":1,\"b\":1,\"a\":1}},"
		               "{\"name\":\"bg\",\"label\":\"Background\",\"type\":\"color\",\"default\":{\"r\":0,\"g\":0,\"b\":0,\"a\":1}},"
		               "{\"name\":\"font_size\",\"label\":\"Font size\",\"type\":\"float\",\"min\":0,\"max\":128,\"step\":1,\"default\":32},"
		               "{\"name\":\"line_spacing\",\"label\":\"Line Spacing\",\"type\":\"float\",\"min\":-512,\"max\":512,\"step\":1,\"default\":0},"
		               "{\"name\":\"alignment\",\"label\":\"Align\",\"type\":\"enum\",\"default\":0,\"values\":[{\"name\":\"Left\",\"value\":\"0\"},"
		               "{\"name\":\"Center\",\"value\":\"1\"},{\"name\":\"Right\",\"value\":\"2\"}]},"
		               "{\"name\":\"center\",\"label\":\"Center\",\"type\":\"boolean\",\"default\":false},"
		               "{\"name\":\"x\",\"label\":\"X\",\"type\":\"float\",\"min\":-0.5,\"max\":0.5,\"step\":0.001,\"default\":0.1},"
		               "{\"name\":\"y\",\"label\":\"Y\",\"type\":\"float\",\"min\":-0.5,\"max\":0.5,\"step\":0.001,\"default\":0.1}]}");
	}
	char *text = mm_buf_get(&b);
	return mm_json_parse(text, strlen(text));
}

// What a node is made of: ports, parameter definitions and values, from its definition and the node as saved (data)
typedef struct mm_nodes_model {
	mm_json_t  *inputs;
	mm_json_t  *outputs;
	mm_json_t  *defs;
	mm_json_t  *values;
	bool        graph;
	bool        generic;
	const char *error;
} mm_nodes_model_t;

static void mm_nodes_make_model(mm_nodes_type_t *t, mm_json_t *data, mm_json_t *canvas_nodes, mm_nodes_model_t *m) {
	memset(m, 0, sizeof(mm_nodes_model_t));
	m->values = mm_json_new(MM_JSON_OBJECT);
	if (data == NULL) {
		m->error = "no data";
		return;
	}
	mm_json_t *bm = mm_nodes_builtin_model(t->mm_type, data, canvas_nodes);
	if (bm != NULL) {
		m->inputs  = mm_json_get(bm, "inputs");
		m->outputs = mm_json_get(bm, "outputs");
		m->defs    = mm_json_get(bm, "parameters");
		mm_nodes_apply_values(m->values, data, m->defs);
		return;
	}
	// The definition: an inline graph or shader model in the data (Material Maker's "graph" and "shader" nodes, the
	// library's inline graphs), an Easy SDF scene, or the .mmg of the type
	mm_json_t *lib = data;
	if (mm_json_get(data, "nodes") == NULL && mm_json_get(data, "shader_model") == NULL) {
		if (mm_json_get(data, "sdf_scene") != NULL) {
			mm_json_t *sm = mm_sdf_shader_model(mm_json_get(data, "sdf_scene"));
			lib           = mm_json_copy(data);
			mm_json_set(lib, "shader_model", sm);
		}
		else {
			lib = mm_library_node(t->mm_type);
		}
	}
	if (lib == NULL || (mm_nodes_is_inline(t->mm_type) && mm_json_get(lib, "nodes") == NULL && mm_json_get(lib, "shader_model") == NULL)) {
		if (!mm_nodes_is_inline(t->mm_type)) {
			m->error = "no definition";
		}
		return; // An inline type without its data yet: no ports, no parameters
	}
	i32 gs = mm_nodes_clampi(mm_json_num(mm_json_get(data, "generic_size"), 1), 1, 32);
	m->graph       = mm_json_get(lib, "nodes") != NULL;
	mm_json_t *sm  = mm_json_get(lib, "shader_model");
	if (m->graph) {
		mm_json_t *nodes = mm_json_get(lib, "nodes");
		m->inputs        = mm_json_get(mm_nodes_child(nodes, "gen_inputs"), "ports");
		m->outputs       = mm_json_get(mm_nodes_child(nodes, "gen_outputs"), "ports");
		m->defs          = mm_nodes_param_defs(lib, NULL, 0);
	}
	else if (sm != NULL) {
		m->inputs    = mm_nodes_expand(mm_json_get(sm, "inputs"), "name", false, gs, &m->generic);
		mm_json_t *o = mm_json_get(sm, "outputs");
		m->outputs   = mm_json_new(MM_JSON_ARRAY);
		for (i32 i = 0; o != NULL && i < o->count; ++i) {
			mm_json_push(m->outputs, mm_nodes_output_typed(o->items[i]));
		}
		m->outputs = mm_nodes_expand(m->outputs, "type", true, gs, &m->generic);
		m->defs    = mm_nodes_expand(mm_json_get(sm, "parameters"), "name", false, gs, &m->generic);
	}
	else {
		m->error = "neither a shader nor a graph";
		return;
	}
	if ((m->outputs == NULL || m->outputs->count == 0) && !mm_nodes_is_inline(t->mm_type)) {
		m->error = "no outputs";
		return;
	}
	// Values: definitions' defaults, the graph's remote, the .mmg, then the node (loader.gd create_gen)
	if (m->graph) {
		mm_nodes_apply_values(m->values, mm_nodes_child(mm_json_get(lib, "nodes"), "gen_parameters"), m->defs);
	}
	if (lib != data) {
		mm_nodes_apply_values(m->values, lib, m->defs);
	}
	mm_nodes_apply_values(m->values, data, m->defs);
}

// Sockets and parameter buttons of the model into n (empty arrays)
static void mm_nodes_fill(mm_nodes_type_t *t, ui_node_t *n, mm_nodes_model_t *m, mm_nodes_param_t **params, i32 *count) {
	for (i32 i = 0; m->inputs != NULL && i < m->inputs->count; ++i) {
		mm_nodes_add_port(t, n, m->inputs->items[i], true, m->graph);
	}
	for (i32 i = 0; m->outputs != NULL && i < m->outputs->count; ++i) {
		mm_nodes_add_port(NULL, n, m->outputs->items[i], false, m->graph);
	}
	for (i32 i = 0; m->defs != NULL && i < m->defs->count; ++i) {
		mm_nodes_add_param(params, count, n, m->defs->items[i], mm_json_get(m->values, mm_json_str(mm_json_get(m->defs->items[i], "name"), "")));
	}
}

// State of a dynamic node, the data of its hidden mm_state button: what Material Maker saves besides the parameters
// (buffer version, remote widgets, portal io / port_type / color) and where the parameters' buttons are (layout:
// [[name, kind, button, first, label], ..]), as they change with the node
static const char *mm_nodes_state_keys[] = {"version", "widgets", "io", "port_type", "color", "horizontal_label", "nodes", "connections", "shader_model",
                                            "sdf_scene", "generic_size"};

static mm_json_t *mm_nodes_state_of(mm_json_t *data, mm_nodes_param_t *params, i32 count) {
	mm_json_t *s = mm_json_new(MM_JSON_OBJECT);
	for (i32 i = 0; i < (i32)(sizeof(mm_nodes_state_keys) / sizeof(mm_nodes_state_keys[0])); ++i) {
		if (mm_json_get(data, mm_nodes_state_keys[i]) != NULL) {
			mm_json_set(s, mm_nodes_state_keys[i], mm_json_get(data, mm_nodes_state_keys[i]));
		}
	}
	mm_json_t *layout = mm_json_new(MM_JSON_ARRAY);
	for (i32 i = 0; i < count; ++i) {
		mm_json_t *e = mm_json_new(MM_JSON_ARRAY);
		mm_json_push(e, mm_nodes_str(params[i].name));
		mm_json_push(e, mm_nodes_num(params[i].kind));
		mm_json_push(e, mm_nodes_num(params[i].button));
		mm_json_push(e, mm_nodes_num(params[i].first));
		mm_json_push(e, mm_nodes_str(params[i].label));
		mm_json_push(layout, e);
	}
	mm_json_set(s, "layout", layout);
	return s;
}

static ui_node_button_t *mm_nodes_state_button(mm_json_t *state) {
	return mm_nodes_button("mm_state", "CUSTOM", f32_array_create_x(0), mm_nodes_text_data(mm_nodes_json_text(state)), 0, 1, 100, 0);
}

static ui_node_button_t *mm_nodes_seed_button_of(mm_json_t *data) {
	bool   locked = false;
	bool   has    = false;
	double seed   = mm_nodes_seed_of(data, &locked, &has);
	f32    hi     = (f32)seed;
	return mm_nodes_button("mm_seed", "CUSTOM", f32_array_create_xyzw(hi, (f32)(seed - hi), locked ? 1 : 0, has ? 1 : 0), NULL, 0, 1, 100, 0);
}

static void mm_nodes_build(mm_nodes_type_t *t) {
	t->built       = true;
	ui_node_t *def = t->def;
	mm_mark_t  mk  = mm_arena_mark();
	mm_json_t *e   = mm_json_parse(t->entry, strlen(t->entry));
	if (t->dynamic && strcmp(t->mm_type, "switch") == 0 && e != NULL) { // Its defaults decide its ports
		mm_json_t *p = mm_json_get(e, "parameters");
		if (p == NULL || p->type != MM_JSON_OBJECT) {
			mm_json_set(e, "parameters", mm_json_new(MM_JSON_OBJECT));
		}
	}
	if (e != NULL && mm_json_get(e, "generic_size") != NULL) { // The library's generic nodes register with generic_size 1
		mm_json_set(e, "generic_size", mm_nodes_num(1));
	}
	mm_nodes_model_t m;
	mm_nodes_make_model(t, e, NULL, &m);
	if (m.error != NULL) {
		t->error = (char *)m.error;
		mm_arena_release(mk);
		return;
	}
	t->generic = m.generic;
	t->dynamic = t->dynamic || m.generic;
	mm_nodes_fill(t, def, &m, &t->params, &t->param_count);
	if (t->dynamic) {
		any_array_push(def->buttons, mm_nodes_state_button(mm_nodes_state_of(e, t->params, t->param_count)));
	}
	t->seed_button = def->buttons->length;
	any_array_push(def->buttons, mm_nodes_seed_button_of(e));
	mm_arena_release(mk);
}

void mm_node_def_ensure(ui_node_t *def) {
	if (def == NULL || def->type == NULL || !starts_with(def->type, "MM_")) {
		return;
	}
	mm_nodes_type_t *t = mm_nodes_find(def->type);
	if (t != NULL && t->def == def && !t->built) {
		mm_nodes_build(t);
	}
}

static mm_nodes_type_t *mm_nodes_built(char *type) {
	mm_nodes_type_t *t = mm_nodes_find(type);
	if (t != NULL && !t->built) {
		mm_nodes_build(t);
	}
	return t;
}

// Builds every type, the failures are listed with the skipped entries. Returns the number of failures.
static f32 mm_nodes_all_ms = 0.0;

f32 mm_nodes_build_ms() {
	return mm_nodes_all_ms;
}

i32 mm_nodes_build_all() {
	double t0       = iron_time();
	i32    failures = 0;
	for (i32 i = 0; i < mm_nodes_n; ++i) {
		mm_nodes_type_t *t = mm_nodes_built(mm_nodes_list[i]->type);
		if (t->error != NULL) {
			mm_nodes_skip("%s: %s", t->type, t->error);
			failures++;
		}
	}
	mm_nodes_all_ms = (f32)((iron_time() - t0) * 1000.0);
	return failures;
}

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

static void mm_nodes_set_state(ui_node_t *node, mm_json_t *state) {
	i32 i = mm_nodes_button_index(node, "mm_state");
	if (i >= 0) {
		node->buttons->buffer[i]->data = mm_nodes_text_data(mm_nodes_json_text(state));
	}
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
		return input >= 0 && input < node->inputs->length ? "" : NULL;
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
			return mm_nodes_str(mm_strndup((const char *)b->data->buffer, strnlen((const char *)b->data->buffer, b->data->length)));
		}
		return n > 0 && string_equals(b->type, "VALUE") ? mm_nodes_num(v[0]) : NULL;
	case MM_NODES_PARAM_ENUM:
		return n > 0 && string_equals(b->type, "ENUM") ? mm_nodes_num((i32)v[0]) : NULL;
	case MM_NODES_PARAM_BOOL:
		return n > 0 && string_equals(b->type, "BOOL") ? mm_nodes_bool(v[0] != 0) : NULL;
	case MM_NODES_PARAM_SIZE:
		return n > 0 && string_equals(b->type, "ENUM") ? mm_nodes_num(p->first + (i32)v[0]) : NULL;
	case MM_NODES_PARAM_COLOR: {
		if (n < 3 || !string_equals(b->type, "RGBA")) {
			return NULL;
		}
		mm_json_t *c = mm_json_new(MM_JSON_OBJECT);
		mm_json_set(c, "type", mm_nodes_str("Color"));
		mm_json_set(c, "r", mm_nodes_num(v[0]));
		mm_json_set(c, "g", mm_nodes_num(v[1]));
		mm_json_set(c, "b", mm_nodes_num(v[2]));
		mm_json_set(c, "a", mm_nodes_num(n > 3 ? v[3] : 1.0));
		return c;
	}
	case MM_NODES_PARAM_GRAD: {
		if (n < 5 || !string_equals(b->name, "mm_gradient")) {
			return NULL;
		}
		mm_json_t *g = mm_json_new(MM_JSON_OBJECT);
		mm_json_t *a = mm_json_new(MM_JSON_ARRAY);
		mm_json_set(g, "type", mm_nodes_str("Gradient"));
		mm_json_set(g, "interpolation", mm_nodes_num(b->data != NULL && b->data->length > 0 ? b->data->buffer[0] : 1));
		for (i32 i = 0; i + 4 < n; i += 5) {
			mm_json_t *pt = mm_json_new(MM_JSON_OBJECT);
			mm_json_set(pt, "pos", mm_nodes_num(v[i + 4]));
			mm_json_set(pt, "r", mm_nodes_num(v[i + 0]));
			mm_json_set(pt, "g", mm_nodes_num(v[i + 1]));
			mm_json_set(pt, "b", mm_nodes_num(v[i + 2]));
			mm_json_set(pt, "a", mm_nodes_num(v[i + 3]));
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
		mm_json_set(c, "type", mm_nodes_str("Curve"));
		for (i32 i = 0; i + 3 < n; i += 4) {
			mm_json_t *pt = mm_json_new(MM_JSON_OBJECT);
			mm_json_set(pt, "x", mm_nodes_num(v[i + 0]));
			mm_json_set(pt, "y", mm_nodes_num(v[i + 1]));
			mm_json_set(pt, "ls", mm_nodes_num(v[i + 2]));
			mm_json_set(pt, "rs", mm_nodes_num(v[i + 3]));
			mm_json_push(a, pt);
		}
		mm_json_set(c, "points", a);
		return c;
	}
	case MM_NODES_PARAM_STRING:
		return b->data != NULL && b->data->length > 0 && string_equals(b->name, "mm_string")
		           ? mm_nodes_str(mm_strndup((const char *)b->data->buffer, strnlen((const char *)b->data->buffer, b->data->length)))
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
	for (i32 i = 0; type != NULL && i < (i32)(sizeof(map) / sizeof(map[0])); ++i) {
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
	mm_json_set(d, "type", mm_nodes_str(t->mm_type));
	if (mm_node_name(node) != NULL) {
		mm_json_set(d, "name", mm_nodes_str(mm_node_name(node)));
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
		mm_json_set(d, "port_type", mm_nodes_str(mm_nodes_mm_socket_type(s != NULL ? s->type : NULL)));
	}
	bool   locked = false;
	double seed   = mm_node_seed(node, &locked);
	mm_json_set(d, "seed", mm_nodes_num(seed));
	mm_json_set(d, "seed_locked", mm_nodes_bool(locked));
	if (t->generic && mm_json_get(d, "generic_size") == NULL) {
		mm_json_set(d, "generic_size", mm_nodes_num(1));
	}
	return d;
}

char *mm_node_parameters_json(ui_node_t *node) {
	mm_mark_t m = mm_arena_mark();
	char     *s = string_copy(mm_nodes_json_text(mm_node_parameters(node)));
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
	char *s = string_copy(d != NULL ? mm_nodes_json_text(d) : "");
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
		b->data = mm_nodes_text_data(mm_nodes_json_text(v));
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
static ui_node_t *mm_nodes_named(ui_node_canvas_t *canvas, const char *name) {
	for (i32 i = 0; canvas != NULL && name != NULL && i < canvas->nodes->length; ++i) {
		ui_node_t *n  = canvas->nodes->buffer[i];
		char      *nn = mm_node_is(n) ? mm_node_name(n) : NULL;
		if (nn != NULL && strcmp(nn, name) == 0) {
			return n;
		}
	}
	return NULL;
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
				mm_nodes_set_param(mm_nodes_named(canvas, mm_nodes_jstr(links->items[k], "node")), mm_json_str(mm_json_get(links->items[k], "widget"), ""), v,
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
				mm_nodes_set_param(mm_nodes_named(canvas, mm_nodes_jstr(list->items[k], "node")), mm_json_str(mm_json_get(list->items[k], "widget"), ""),
				                   mm_json_get(list->items[k], "value"), canvas, depth);
			}
		}
		break;
	}
}

// The Material Maker nodes of the canvas as Material Maker would save them (remote widgets resolve their targets there)
static mm_json_t *mm_nodes_canvas_data(ui_node_canvas_t *canvas, ui_node_t *except) {
	mm_json_t *rv = mm_json_new(MM_JSON_ARRAY);
	for (i32 i = 0; canvas != NULL && i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (n != except && mm_node_is(n)) {
			mm_json_push(rv, mm_node_data(n));
		}
	}
	return rv;
}

// New sockets for the node, matched by name with the old ones: those keep their id, value and links (a link moves with
// its socket, as Material Maker reconnects a switch), links of the others are removed
static void mm_nodes_adopt_sockets(ui_node_t *node, ui_node_socket_array_t *old, ui_node_socket_array_t *now, bool inputs, ui_node_canvas_t *canvas) {
	i32  n     = now->length;
	i32 *remap = mm_alloc(sizeof(i32) * (old->length + 1));
	ui_node_socket_t **keep = mm_alloc(sizeof(ui_node_socket_t *) * (n + 1));
	for (i32 i = 0; i < old->length; ++i) {
		remap[i] = -1;
		for (i32 k = 0; k < n; ++k) {
			if (keep[k] == NULL && string_equals(old->buffer[i]->name, now->buffer[k]->name)) {
				remap[i] = k;
				keep[k]  = old->buffer[i];
				break;
			}
		}
	}
	if (canvas != NULL) {
		i32 i = 0;
		while (i < canvas->links->length) {
			ui_node_link_t *l    = canvas->links->buffer[i];
			bool            mine = inputs ? l->to_id == node->id : l->from_id == node->id;
			i32            *s    = inputs ? &l->to_socket : &l->from_socket;
			if (mine) {
				i32 to = *s >= 0 && *s < old->length ? remap[*s] : -1;
				if (to < 0) {
					for (i32 j = i; j < canvas->links->length - 1; ++j) {
						canvas->links->buffer[j] = canvas->links->buffer[j + 1];
					}
					canvas->links->length--;
					continue;
				}
				*s = to;
			}
			i++;
		}
	}
	i32 id = canvas != NULL ? ui_get_socket_id(canvas->nodes) : 0;
	old->length = 0;
	for (i32 k = 0; k < n; ++k) {
		ui_node_socket_t *s = keep[k];
		if (s == NULL) {
			s          = now->buffer[k];
			s->id      = id++;
			s->node_id = node->id;
		}
		any_array_push(old, s);
	}
}

// Rebuilds the sockets and buttons of a dynamic node from data (as mm_node_data gives it), the seed and name stay
static void mm_nodes_reshape(ui_node_t *node, mm_json_t *data, ui_node_canvas_t *canvas, bool buttons) {
	mm_nodes_type_t *t = mm_nodes_built(node->type);
	if (t == NULL || !t->dynamic) {
		return;
	}
	mm_nodes_model_t m;
	mm_nodes_make_model(t, data, strcmp(t->mm_type, "remote") == 0 ? mm_nodes_canvas_data(canvas, node) : NULL, &m);
	if (m.error != NULL && strcmp(t->mm_type, "remote") != 0) {
		return;
	}
	ui_node_t         scratch = {0};
	mm_nodes_param_t *params  = NULL;
	i32               count   = 0;
	scratch.inputs            = any_array_create_from_raw((void *[]){}, 0);
	scratch.outputs           = any_array_create_from_raw((void *[]){}, 0);
	scratch.buttons           = any_array_create_from_raw((void *[]){}, 0);
	mm_nodes_fill(NULL, &scratch, &m, &params, &count);
	mm_nodes_adopt_sockets(node, node->inputs, scratch.inputs, true, canvas);
	mm_nodes_adopt_sockets(node, node->outputs, scratch.outputs, false, canvas);
	if (buttons) {
		i32               si   = mm_nodes_seed_index(node);
		ui_node_button_t *seed = si >= 0 ? node->buttons->buffer[si] : mm_nodes_seed_button_of(data);
		node->buttons->length  = 0;
		for (i32 i = 0; i < scratch.buttons->length; ++i) {
			any_array_push(node->buttons, scratch.buttons->buffer[i]);
		}
		any_array_push(node->buttons, mm_nodes_state_button(mm_nodes_state_of(data, params, count)));
		any_array_push(node->buttons, seed);
	}
	else { // Same buttons: only the source range of a switch follows its choices
		mm_nodes_param_t *src = mm_nodes_find_param(node, "source");
		if (src != NULL) {
			ui_node_button_t *b = node->buttons->buffer[src->button];
			b->max              = mm_nodes_switch_size(data, "choices", 2, 2) - 1;
			b->default_value->buffer[0] = mm_nodes_clampi(b->default_value->buffer[0], 0, (i32)b->max);
		}
	}
	for (i32 i = 0; i < count; ++i) {
		free(params[i].name); // The labels are the buttons' names
	}
	free(params);
	array_free(scratch.inputs);
	array_free(scratch.outputs);
	array_free(scratch.buttons);
	free(scratch.inputs);
	free(scratch.outputs);
	free(scratch.buttons);
}

// Sockets of type "any" (switch, reroute, portal) take the type of what they are linked to, MM_ANY while unlinked
static ui_node_link_t *mm_nodes_link_to(ui_node_canvas_t *canvas, ui_node_t *node, i32 socket, bool input) {
	for (i32 i = 0; i < canvas->links->length; ++i) {
		ui_node_link_t *l = canvas->links->buffer[i];
		if (input ? (l->to_id == node->id && l->to_socket == socket) : (l->from_id == node->id && l->from_socket == socket)) {
			return l;
		}
	}
	return NULL;
}

static char *mm_nodes_linked_type(ui_node_canvas_t *canvas, ui_node_t *node, i32 socket, bool input) {
	ui_node_link_t *l     = mm_nodes_link_to(canvas, node, socket, input);
	ui_node_t      *other = l != NULL ? ui_get_node(canvas->nodes, input ? l->from_id : l->to_id) : NULL;
	if (other == NULL) {
		return NULL;
	}
	ui_node_socket_array_t *sockets = input ? other->outputs : other->inputs;
	i32                     i       = input ? l->from_socket : l->to_socket;
	return i >= 0 && i < sockets->length && !string_equals(sockets->buffer[i]->type, "MM_ANY") ? sockets->buffer[i]->type : NULL;
}

static void mm_nodes_set_socket_type(ui_node_socket_t *s, char *type) {
	if (!string_equals(s->type, type)) {
		s->type  = string_copy(type);
		s->color = mm_nodes_socket_color(type);
		if (s->default_value == NULL || (i32)s->default_value->length < mm_nodes_socket_size(type)) {
			f32 v[4] = {0, 0, 0, 1};
			s->default_value = f32_array_create(4);
			for (i32 i = 0; i < 4; ++i) {
				s->default_value->buffer[i] = v[i];
			}
		}
	}
}

static char *mm_nodes_string_param(ui_node_t *node, const char *name) {
	mm_nodes_param_t *p = mm_nodes_find_param(node, name);
	u8_array_t       *d = p != NULL ? node->buttons->buffer[p->button]->data : NULL;
	return d != NULL && d->length > 0 ? (char *)d->buffer : "";
}

static void mm_nodes_adopt_types(ui_node_t *node, mm_nodes_type_t *t, ui_node_canvas_t *canvas) {
	if (strcmp(t->mm_type, "reroute") == 0 && node->inputs->length > 0 && node->outputs->length > 0) {
		char *type = mm_nodes_linked_type(canvas, node, 0, true);
		type       = type != NULL ? type : mm_nodes_linked_type(canvas, node, 0, false);
		type       = type != NULL ? type : "MM_ANY";
		mm_nodes_set_socket_type(node->inputs->buffer[0], type);
		mm_nodes_set_socket_type(node->outputs->buffer[0], type);
	}
	else if (strcmp(t->mm_type, "switch") == 0) {
		i32 outputs = node->outputs->length;
		for (i32 o = 0; o < outputs; ++o) {
			char *type = NULL;
			for (i32 i = o; i < node->inputs->length && type == NULL; i += outputs) {
				type = mm_nodes_linked_type(canvas, node, i, true);
			}
			type = type != NULL ? type : mm_nodes_linked_type(canvas, node, o, false);
			type = type != NULL ? type : "MM_ANY";
			for (i32 i = o; i < node->inputs->length; i += outputs) {
				mm_nodes_set_socket_type(node->inputs->buffer[i], type);
			}
			mm_nodes_set_socket_type(node->outputs->buffer[o], type);
		}
	}
	else if (strcmp(t->mm_type, "portal") == 0) {
		if (node->inputs->length > 0) { // In: what is linked to it
			char *type = mm_nodes_linked_type(canvas, node, 0, true);
			mm_nodes_set_socket_type(node->inputs->buffer[0], type != NULL ? type : "MM_ANY");
		}
		if (node->outputs->length > 0) { // Out: what its In of the same link carries
			char *link = mm_nodes_string_param(node, "link");
			char *type = NULL;
			for (i32 i = 0; i < canvas->nodes->length && type == NULL; ++i) {
				ui_node_t *n = canvas->nodes->buffer[i];
				if (n != node && starts_with(n->type, "MM_portal") && n->inputs->length > 0 && string_equals(mm_nodes_string_param(n, "link"), link)) {
					char *it = n->inputs->buffer[0]->type;
					type     = string_equals(it, "MM_ANY") ? NULL : it;
				}
			}
			mm_nodes_set_socket_type(node->outputs->buffer[0], type != NULL ? type : "MM_ANY");
		}
	}
}

// Keeps a dynamic node consistent with its values, called as it is drawn and after edits: a switch rebuilds its sockets
// when outputs or choices change, "any" sockets follow their links, a remote applies its changed controls
void mm_node_update(ui_node_t *node, ui_node_canvas_t *canvas) {
	mm_nodes_type_t *t = node != NULL && canvas != NULL ? mm_nodes_built(node->type) : NULL;
	if (t == NULL || !t->dynamic) {
		return;
	}
	mm_mark_t mk = mm_arena_mark();
	if (strcmp(t->mm_type, "switch") == 0) {
		mm_json_t *d       = mm_node_data(node);
		i32        outputs = mm_nodes_switch_size(d, "outputs", 2, 1);
		i32        choices = mm_nodes_switch_size(d, "choices", 2, 2);
		if (node->outputs->length != outputs || node->inputs->length != outputs * choices) {
			mm_nodes_reshape(node, d, canvas, false);
		}
	}
	if (strcmp(t->mm_type, "remote") == 0) {
		if (mm_nodes_remote_seen == NULL) {
			mm_nodes_remote_seen = any_map_create();
		}
		char      *key  = string_tmp("%p", node);
		char      *last = any_map_get(mm_nodes_remote_seen, key);
		mm_json_t *now  = mm_node_parameters(node);
		char      *text = mm_nodes_json_text(now);
		if (last != NULL && strcmp(last, text) != 0) {
			mm_json_t *before = mm_json_parse(last, strlen(last));
			for (i32 i = 0; i < now->count; ++i) {
				mm_json_t *b = mm_json_get(before, now->keys[i]);
				if (b == NULL || strcmp(mm_nodes_json_text(b), mm_nodes_json_text(now->items[i])) != 0) {
					mm_nodes_remote_apply(node, now->keys[i], now->items[i], canvas, 0);
				}
			}
		}
		if (last == NULL || strcmp(last, text) != 0) {
			any_map_set(mm_nodes_remote_seen, string_copy(key), string_copy(text));
		}
	}
	mm_nodes_adopt_types(node, t, canvas);
	mm_arena_release(mk);
}

// Every Material Maker node of the canvas: remotes rebuild their controls from the nodes they control, then updates
void mm_nodes_sync(ui_node_canvas_t *canvas) {
	for (i32 i = 0; canvas != NULL && i < canvas->nodes->length; ++i) {
		ui_node_t       *n = canvas->nodes->buffer[i];
		mm_nodes_type_t *t = mm_node_is(n) ? mm_nodes_built(n->type) : NULL;
		if (t != NULL && strcmp(t->mm_type, "remote") == 0) {
			mm_mark_t mk = mm_arena_mark();
			mm_nodes_reshape(n, mm_node_data(n), canvas, true);
			mm_arena_release(mk);
		}
	}
	for (i32 i = 0; canvas != NULL && i < canvas->nodes->length; ++i) {
		mm_node_update(canvas->nodes->buffer[i], canvas);
	}
}

// Sets the node from a node of a Material Maker graph (as mm_node_data gives it, or read from a .ptex): name, seed,
// version, widgets, io, port_type and parameters. Dynamic nodes rebuild their sockets and buttons, links that no longer
// fit are removed.
void mm_node_set_data(ui_node_t *node, mm_json_t *data, ui_node_canvas_t *canvas) {
	mm_nodes_type_t *t = node != NULL && data != NULL ? mm_nodes_built(node->type) : NULL;
	if (t == NULL) {
		return;
	}
	mm_mark_t  mk     = mm_arena_mark();
	mm_json_t *merged = mm_node_data(node);
	mm_json_t *params = mm_json_get(merged, "parameters");
	for (i32 i = 0; i < data->count; ++i) {
		if (data->items[i] == NULL) {
			continue;
		}
		if (strcmp(data->keys[i], "parameters") == 0 && data->items[i]->type == MM_JSON_OBJECT) {
			for (i32 k = 0; k < data->items[i]->count; ++k) {
				mm_json_set(params, data->items[i]->keys[k], data->items[i]->items[k]);
			}
		}
		else if (strcmp(data->keys[i], "type") != 0) {
			mm_json_set(merged, data->keys[i], data->items[i]);
		}
	}
	if (t->dynamic) {
		mm_nodes_reshape(node, merged, canvas, true);
	}
	i32               count = 0;
	mm_nodes_param_t *ps    = mm_nodes_layout(node, &count);
	for (i32 i = 0; i < count; ++i) {
		if (ps[i].button >= 0 && ps[i].button < node->buttons->length) {
			mm_nodes_store(&ps[i], node->buttons->buffer[ps[i].button], mm_json_get(params, ps[i].name));
		}
	}
	bool   locked = false;
	bool   has    = false;
	double seed   = mm_nodes_seed_of(data, &locked, &has);
	i32    si     = mm_nodes_seed_index(node);
	if (has && si >= 0) {
		f32 *s = node->buttons->buffer[si]->default_value->buffer;
		s[0]   = (f32)seed;
		s[1]   = (f32)(seed - s[0]);
		s[2]   = locked ? 1 : 0;
		s[3]   = 1;
	}
	mm_nodes_set_name(node, mm_nodes_jstr(data, "name"));
	if (mm_nodes_remote_seen != NULL) { // Values set here are not edits to apply
		map_delete(mm_nodes_remote_seen, string_tmp("%p", node));
	}
	mm_node_update(node, canvas);
	mm_arena_release(mk);
}

void mm_node_set_data_json(ui_node_t *node, char *json, ui_node_canvas_t *canvas) {
	mm_mark_t mk = mm_arena_mark();
	mm_node_set_data(node, mm_json_parse(json, strlen(json)), canvas);
	mm_arena_release(mk);
}

// Codegen

// TODO(mm_ codegen): Material Maker shader code for these nodes comes from the mm_ generator (mm_gen.c port, inputs
// evaluated at other uvs). Until then every output is its socket's default value, in the socket's GLSL type
// (parser_material_socket_glsl_type).
static char *mm_nodes_codegen_stub(ui_node_t *node, ui_node_socket_t *socket) {
	f32_array_t *d = socket->default_value;
	char        *t = parser_material_socket_glsl_type(socket->type);
	i32          n = string_equals(t, "vec4") ? 4 : string_equals(t, "vec3") ? 3 : string_equals(t, "vec2") ? 2 : 1;
	if (d == NULL || d->length == 0) {
		return parser_material_socket_zero(socket->type);
	}
	char        *c[4];
	for (i32 i = 0; i < 4; ++i) {
		c[i] = f32_to_string_with_zeros(d != NULL && i < (i32)d->length ? d->buffer[i] : (i == 3 ? 1.0 : 0.0));
	}
	return n == 4   ? string_tmp("vec4(%s, %s, %s, %s)", c[0], c[1], c[2], c[3])
	       : n == 3 ? string_tmp("vec3(%s, %s, %s)", c[0], c[1], c[2])
	       : n == 2 ? string_tmp("vec2(%s, %s)", c[0], c[1])
	                : c[0];
}

// Custom buttons

// Label of the parameter whose button it is
static char *mm_nodes_button_label(ui_node_t *node, i32 button) {
	mm_mark_t         mk    = mm_arena_mark();
	i32               count = 0;
	mm_nodes_param_t *ps    = mm_nodes_layout(node, &count);
	char             *label = "";
	for (i32 i = 0; i < count; ++i) {
		if (ps[i].button == button) {
			label = string_tmp("%s", ps[i].label);
			break;
		}
	}
	mm_arena_release(mk);
	return label;
}

static ui_node_t *mm_nodes_drawn_node(i32 node_id, ui_node_button_t **but) {
	ui_node_t *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	i32        i    = ui_nodes_custom_button_index;
	if (node == NULL || i < 0 || i >= node->buttons->length) {
		return NULL;
	}
	*but = node->buttons->buffer[i];
	return node;
}

// Editor state of one button, a node may have several gradients or curves
static ui_nodes_editor_state_t *mm_nodes_ui_state(ui_node_t *node, i32 button) {
	ui_node_t key = *node;
	key.type      = string_tmp("%s#%d", node->type, button);
	return ui_nodes_editor_state(&key);
}

static void mm_nodes_label_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node != NULL) {
		ui_text(but->data != NULL ? (char *)but->data->buffer : "", UI_ALIGN_LEFT, 0);
	}
}

static void mm_nodes_value_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node != NULL) {
		ui_text(mm_nodes_button_label(node, ui_nodes_custom_button_index), UI_ALIGN_LEFT, 0);
	}
}

static void mm_nodes_seed_button(i32 node_id) {
	// Hidden, keeps the seed
}

// Hidden, keeps the state of a dynamic node and keeps the node consistent as it is drawn (mm_node_update)
static void mm_nodes_state_button_draw(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node != NULL) {
		mm_node_update(node, ui_nodes_get_canvas(true));
	}
}

// Text field of a string, file or image path parameter, kept in the button data
static void mm_nodes_string_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node == NULL) {
		return;
	}
	char *text = but->data != NULL && but->data->length > 0 ? (char *)but->data->buffer : "";
	ui_set_next_id(((ui_id_t)node->id << 16) + ((ui_nodes_custom_button_index * 4) & 0xffff) + 1); // As the editor keys its controls
	char *edited = ui_text_input(&text, mm_nodes_button_label(node, ui_nodes_custom_button_index), UI_ALIGN_LEFT, true, false);
	if (edited != NULL && (but->data == NULL || strcmp(edited, (char *)but->data->buffer) != 0)) {
		but->data = mm_nodes_text_data(edited);
	}
}

static void mm_nodes_gradient_eval(f32 *v, i32 n, i32 interp, f32 t, f32 *out) {
	// Stops sorted by position (insertion sort of indices)
	i32 order[64];
	n = n > 64 ? 64 : n;
	for (i32 i = 0; i < n; ++i) {
		i32 j = i;
		while (j > 0 && v[order[j - 1] * 5 + 4] > v[i * 5 + 4]) {
			order[j] = order[j - 1];
			j--;
		}
		order[j] = i;
	}
	i32 k = 0;
	while (k < n && v[order[k] * 5 + 4] < t) {
		k++;
	}
	f32 *a = &v[order[k > 0 ? k - 1 : 0] * 5];
	f32 *b = &v[order[k < n ? k : n - 1] * 5];
	f32  f = b[4] > a[4] ? (t - a[4]) / (b[4] - a[4]) : 0.0;
	f      = f < 0 ? 0 : f > 1 ? 1 : f;
	if (interp == 0) { // Constant: the stop before
		f = k < n && t >= b[4] ? 1.0 : 0.0;
	}
	else if (interp >= 2) { // Smooth, cubic shown smooth
		f = f * f * (3.0 - 2.0 * f);
	}
	for (i32 i = 0; i < 4; ++i) {
		out[i] = a[i] + (b[i] - a[i]) * f;
	}
}

static void mm_nodes_gradient_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node == NULL || but->default_value == NULL || but->default_value->length < 5) {
		return;
	}
	i32                      bi    = ui_nodes_custom_button_index;
	ui_nodes_t              *nodes = ui_nodes_get_nodes();
	ui_nodes_editor_state_t *state = mm_nodes_ui_state(node, bi);
	char                    *label = mm_nodes_button_label(node, bi);
	f32_array_t             *vals  = but->default_value;
	if (but->data == NULL || but->data->length < 1) {
		but->data            = u8_array_create(1);
		but->data->buffer[0] = 1;
	}
	f32 nx = g_ui->_x;
	f32 sw = g_ui->_w / (float)UI_NODES_SCALE();
	ui_text(label, UI_ALIGN_LEFT, 0);

	// Preview
	i32 len    = (i32)(vals->length / 5);
	i32 interp = but->data->buffer[0];
	for (i32 s = 0; s < 64; s++) {
		f32 t = (f32)s / 64.0;
		f32 c[4];
		mm_nodes_gradient_eval(vals->buffer, len, interp, t, c);
		ui_fill(t * sw, 0, sw / 64.0 + 1.0 / UI_NODES_SCALE(), UI_LINE_H() - 2 * UI_NODES_SCALE(), color_from_floats(c[0], c[1], c[2], 1.0));
	}
	g_ui->_y += UI_LINE_H();

	f32_array_t *row = f32_array_create_from_raw_tmp((f32[]){1 / 4.0, 1 / 4.0, 2 / 4.0}, 3);
	ui_row(row);
	if (ui_button("+", UI_ALIGN_CENTER, "") && len < 64) {
		i32 last = vals->length - 5;
		for (i32 i = 0; i < 4; ++i) {
			f32_array_push(vals, vals->buffer[last + i]);
		}
		f32_array_push(vals, 1.0);
		state->index[0] = len;
	}
	if (ui_button("-", UI_ALIGN_CENTER, "") && vals->length > 5) {
		vals->length -= 5;
		state->index[0] -= 1;
	}
	string_array_t *modes = any_array_create_from_raw_tmp((void *[]){tr("Constant"), tr("Linear"), tr("Smooth"), tr("Cubic")}, 4);
	state->interp         = but->data->buffer[0];
	but->data->buffer[0]  = ui_combo(&state->interp, modes, tr("Interpolate"), false, UI_ALIGN_LEFT, true);

	ui_row2();
	len   = (i32)(vals->length / 5);
	i32 i = math_floor(ui_slider(&state->index[0], "Index", 0, len - 1, false, 1, true, UI_ALIGN_LEFT, true));
	if (i >= len || i < 0) {
		state->index[0] = i = len - 1;
	}
	state->x                = vals->buffer[i * 5 + 4];
	vals->buffer[i * 5 + 4] = math_max(0.0, math_min(1.0, ui_slider(&state->x, "Pos", 0, 1, true, 100, true, UI_ALIGN_LEFT, true)));
	u32 color               = color_from_floats(vals->buffer[i * 5 + 0], vals->buffer[i * 5 + 1], vals->buffer[i * 5 + 2], 1.0);
	if (ui_text("", UI_ALIGN_RIGHT, color) == UI_STATE_STARTED) {
		f32 rx                = nx + g_ui->_w - ui_p(37);
		f32 ry                = g_ui->_y - ui_p(5);
		nodes->_input_started = g_ui->input_started = false;
		ui_nodes_rgba_popup(&state->color_state, vals->buffer + i * 5, math_floor(rx), math_floor(ry + UI_ELEMENT_H()));
	}
}

// MMCurve: cubic Hermite between points, ls / rs the slopes left and right of each point
static f32 mm_nodes_curve_eval(f32 *v, i32 n, f32 x) {
	if (n < 2) {
		return n == 1 ? v[1] : x;
	}
	if (x <= v[0]) {
		return v[1];
	}
	for (i32 i = 0; i < n - 1; ++i) {
		f32 *a = &v[i * 4];
		f32 *b = &v[(i + 1) * 4];
		if (x <= b[0]) {
			f32 dx = b[0] - a[0];
			if (dx <= 0.00001) {
				return b[1];
			}
			f32 t   = (x - a[0]) / dx;
			f32 t2  = t * t;
			f32 t3  = t2 * t;
			f32 h00 = 2 * t3 - 3 * t2 + 1;
			f32 h10 = t3 - 2 * t2 + t;
			f32 h01 = -2 * t3 + 3 * t2;
			f32 h11 = t3 - t2;
			return h00 * a[1] + h10 * dx * a[3] + h01 * b[1] + h11 * dx * b[2];
		}
	}
	return v[(n - 1) * 4 + 1];
}

static void mm_nodes_curve_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node == NULL || but->default_value == NULL || but->default_value->length < 8) {
		return;
	}
	i32                      bi    = ui_nodes_custom_button_index;
	ui_nodes_editor_state_t *state = mm_nodes_ui_state(node, bi);
	char                    *label = mm_nodes_button_label(node, bi);
	f32_array_t             *val   = but->default_value;
	i32                      num   = (i32)(val->length / 4);
	ui_text(label, UI_ALIGN_LEFT, 0);

	f32 sw  = g_ui->_w / (float)UI_NODES_SCALE();
	f32 ph  = UI_LINE_H() * 4 - 2 * UI_NODES_SCALE();
	f32 phs = ph * UI_SCALE();
	f32 pws = sw * UI_SCALE();
	f32 bx  = g_ui->_x;
	f32 by  = g_ui->_y;
	ui_fill(0, 0, sw, ph, 0xff1a1a1a);
	ui_fill(0, ph * 0.5f, sw, 1.0f / UI_NODES_SCALE(), 0xff333333);
	ui_fill(sw * 0.5f, 0, 1.0f / UI_NODES_SCALE(), ph, 0xff333333);
	draw_set_color(0xffffffff);
	f32 px = 0, py = 0;
	for (i32 s = 0; s <= 64; s++) {
		f32 t  = (f32)s / 64.0f;
		f32 y  = mm_nodes_curve_eval(val->buffer, num, t);
		f32 ax = bx + t * pws;
		f32 ay = math_max(by, math_min(by + phs, by + (1.0f - y) * phs));
		if (s > 0) {
			draw_line_aa(px, py, ax, ay, 1.5f);
		}
		px = ax;
		py = ay;
	}
	g_ui->_y += UI_LINE_H() * 4;

	f32_array_t *row = f32_array_create_from_raw_tmp((f32[]){1 / 5.0, 1 / 5.0, 3 / 5.0}, 3);
	ui_row(row);
	i32 i = math_floor(state->index[0]);
	i     = i < 0 ? 0 : i >= num ? num - 1 : i;
	if (ui_button("+", UI_ALIGN_CENTER, "") && num < 32) { // A point halfway to the next one, on the curve
		i32 a  = i < num - 1 ? i : i - 1;
		f32 x  = (val->buffer[a * 4] + val->buffer[(a + 1) * 4]) * 0.5;
		f32 y  = mm_nodes_curve_eval(val->buffer, num, x);
		f32 dx = val->buffer[(a + 1) * 4] - val->buffer[a * 4];
		f32 sl = dx > 0.00001 ? (val->buffer[(a + 1) * 4 + 1] - val->buffer[a * 4 + 1]) / dx : 0;
		for (i32 k = 0; k < 4; ++k) {
			f32_array_push(val, 0);
		}
		for (i32 k = val->length - 1; k >= (a + 2) * 4; --k) {
			val->buffer[k] = val->buffer[k - 4];
		}
		f32 *np         = &val->buffer[(a + 1) * 4];
		np[0]           = x;
		np[1]           = y;
		np[2]           = sl;
		np[3]           = sl;
		num++;
		state->index[0] = a + 1;
	}
	if (ui_button("-", UI_ALIGN_CENTER, "") && num > 2) {
		for (i32 k = i * 4; k < (i32)val->length - 4; ++k) {
			val->buffer[k] = val->buffer[k + 4];
		}
		val->length -= 4;
		num--;
	}
	i = math_floor(ui_slider(&state->index[0], "Index", 0, num - 1, false, 1, true, UI_ALIGN_LEFT, true));
	if (i >= num || i < 0) {
		state->index[0] = i = num - 1;
	}
	ui_row2();
	f32 lo   = i > 0 ? val->buffer[(i - 1) * 4] : 0;
	f32 hi   = i < num - 1 ? val->buffer[(i + 1) * 4] : 1;
	state->x = val->buffer[i * 4 + 0];
	state->y = val->buffer[i * 4 + 1];
	f32 x    = ui_slider(&state->x, "X", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
	val->buffer[i * 4 + 0] = math_max(lo, math_min(hi, x)); // Points stay sorted
	val->buffer[i * 4 + 1] = ui_slider(&state->y, "Y", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
}

// Registration

static char *mm_nodes_slug(const char *s) {
	char *r = string_copy((char *)s);
	for (char *p = r; *p; ++p) {
		*p = (*p >= 'A' && *p <= 'Z') ? *p + 32 : ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) ? *p : '_';
	}
	return r;
}

// hidden: not in the add menu, for imported graphs (every .mmg and website node of nodes.pack, the inline types)
static void mm_nodes_register(mm_json_t *entry, char *type, char *mm_type, const char *path, bool hidden) {
	mm_json_t *copy = mm_json_copy(entry);
	mm_json_set(copy, "icon_data", NULL);
	mm_json_set(copy, "icon", NULL);

	mm_nodes_type_t *t = calloc(1, sizeof(mm_nodes_type_t));
	t->type            = type;
	t->mm_type         = string_copy(mm_type);
	t->path            = string_copy((char *)path);
	t->entry           = string_copy(mm_nodes_json_text(copy));
	t->seed_button     = -1;
	t->dynamic         = mm_nodes_is_dynamic(mm_type);
	const char *name   = mm_nodes_jstr(entry, "display_name");
	const char *slash  = strrchr(path, '/');
	t->def             = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                            .name    = string_copy((char *)(name != NULL ? name : slash != NULL ? slash + 1 : path)),
	                                            .type    = type,
	                                            .x       = 0,
	                                            .y       = 0,
	                                            .color   = MM_NODES_COLOR,
	                                            .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                                            .outputs = any_array_create_from_raw((void *[]){}, 0),
	                                            .buttons = any_array_create_from_raw((void *[]){}, 0),
	                                            .width   = 0,
	                                            .flags   = 0});
	any_map_set(mm_nodes_types, type, t);
	mm_nodes_list                 = realloc(mm_nodes_list, sizeof(mm_nodes_type_t *) * (mm_nodes_n + 1));
	mm_nodes_list[mm_nodes_n++]   = t;
	if (!hidden) {
		any_array_push(nodes_material_mm, t->def);
		ui_nodes_set_menu_path(type, string_tmp("Material Maker/%s", path));
		mm_nodes_visible++;
	}
	any_map_set(parser_material_node_outputs, type, mm_nodes_codegen_stub);
}

void mm_nodes_init() {
	any_map_set(ui_nodes_custom_buttons, "mm_label", mm_nodes_label_button);
	any_map_set(ui_nodes_custom_buttons, "mm_value", mm_nodes_value_button);
	any_map_set(ui_nodes_custom_buttons, "mm_seed", mm_nodes_seed_button);
	any_map_set(ui_nodes_custom_buttons, "mm_gradient", mm_nodes_gradient_button);
	any_map_set(ui_nodes_custom_buttons, "mm_curve", mm_nodes_curve_button);
	any_map_set(ui_nodes_custom_buttons, "mm_state", mm_nodes_state_button_draw);
	any_map_set(ui_nodes_custom_buttons, "mm_string", mm_nodes_string_button);
	if (mm_nodes_done_init) {
		return;
	}
	mm_nodes_done_init = true;
	mm_nodes_types     = any_map_create();
	double t0          = iron_time();

	buffer_t *blob = data_get_blob("material_maker/nodes.pack");
	if (blob == NULL) {
		mm_nodes_skip("%s%s", "material_maker/nodes.pack not found", "");
		return;
	}
	u8 *data = malloc(blob->length); // The pack keeps pointing into it
	memcpy(data, blob->buffer, blob->length);
	mm_nodes_pack = mm_pack_open(data, blob->length);
	if (mm_nodes_pack == NULL) {
		mm_nodes_skip("%s%s", "material_maker/nodes.pack is not a pack", "");
		return;
	}
	mm_set_nodes(mm_nodes_pack);

	any_map_t *in_pack = any_map_create(); // .mmg names
	for (i32 i = 0; i < mm_pack_count(mm_nodes_pack); ++i) {
		mm_json_t  *e    = mm_pack_entry(mm_nodes_pack, i);
		const char *kind = mm_json_str(mm_json_get(e, "kind"), "");
		if (strcmp(kind, "mmg") == 0) {
			any_map_set(in_pack, (char *)mm_json_str(mm_json_get(e, "name"), ""), e);
		}
	}

	i32 k = mm_pack_find(mm_nodes_pack, "name", "library_base");
	if (k < 0) {
		mm_nodes_skip("%s%s", "nodes.pack has no library_base, run build_library.py", "");
		return;
	}
	mm_mark_t  m   = mm_arena_mark();
	size_t     len = 0;
	char      *txt = mm_pack_read(mm_nodes_pack, k, &len);
	mm_json_t *lib = txt != NULL ? mm_json_get(mm_json_parse(txt, len), "lib") : NULL;
	free(txt);
	for (i32 i = 0; lib != NULL && i < lib->count; ++i) {
		mm_json_t  *e    = lib->items[i];
		const char *type = mm_nodes_jstr(e, "type");
		const char *path = mm_nodes_jstr(e, "tree_item");
		if (type == NULL || path == NULL) {
			continue;
		}
		if (mm_json_get(e, "nodes") != NULL) { // An inline graph
			mm_nodes_register(e, string_copy(string_tmp("MM_graph_%s", mm_nodes_slug(path))), "graph", path, false);
		}
		else if (any_map_get(in_pack, (char *)type) != NULL || mm_nodes_is_builtin(type)) {
			char *t = string_tmp("MM_%s", type);
			if (mm_nodes_find(t) != NULL) { // A preset: the same node with other defaults, MM_<type>@<name>
				const char *dn = mm_nodes_jstr(e, "display_name");
				t              = string_tmp("MM_%s@%s", type, mm_nodes_slug(dn != NULL ? dn : path));
			}
			if (mm_nodes_find(t) != NULL) {
				mm_nodes_skip("%s: %s is registered twice", path, t);
			}
			else {
				mm_nodes_register(e, string_copy(t), (char *)type, path, false);
			}
		}
		else {
			mm_nodes_skip("%s: %s is written in GDScript, not a node definition", path, type);
		}
	}
	// Every other node definition of the pack, and Material Maker's inline nodes, for imported graphs
	for (i32 i = 0; i < mm_pack_count(mm_nodes_pack); ++i) {
		mm_json_t  *pe   = mm_pack_entry(mm_nodes_pack, i);
		const char *name = mm_json_str(mm_json_get(pe, "name"), "");
		char       *t    = string_tmp("MM_%s", name);
		if (strcmp(mm_json_str(mm_json_get(pe, "kind"), ""), "mmg") == 0 && mm_nodes_find(t) == NULL && !starts_with((char *)name, "material")) { // MM_MATERIAL takes materials
			mm_json_t *e = mm_json_new(MM_JSON_OBJECT);
			mm_json_set(e, "type", mm_nodes_str(name));
			mm_json_set(e, "display_name", mm_nodes_str(name));
			mm_nodes_register(e, string_copy(t), (char *)name, name, true);
		}
	}
	const char *inline_types[][2] = {{"graph", "Graph"}, {"shader", "Shader"}, {"sdf", "Easy SDF"}};
	for (i32 i = 0; i < 3; ++i) {
		mm_json_t *e = mm_json_new(MM_JSON_OBJECT);
		mm_json_set(e, "type", mm_nodes_str(inline_types[i][0]));
		mm_json_set(e, "display_name", mm_nodes_str(inline_types[i][1]));
		mm_nodes_register(e, string_copy(string_tmp("MM_%s", inline_types[i][0])), (char *)inline_types[i][0], inline_types[i][1], true);
	}
	mm_arena_release(m);
	map_free(in_pack);
	mm_nodes_ms = (f32)((iron_time() - t0) * 1000.0);
}
