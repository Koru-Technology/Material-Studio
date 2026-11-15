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
// Files: mm_nodes.c types and their definitions (sockets, buttons, model), mm_nodes_params.c values and parameter sockets,
// mm_nodes_sync.c dynamic nodes, mm_nodes_buttons.c node editor buttons, mm_nodes_register.c startup.
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
#define MM_PARAM_SOCKET       0x100 // display of a parameter socket (a parameter wired to a Variable)

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
	return nodes_material_socket_ex(mm_nodes_unique(sockets, name), type, mm_nodes_socket_color(type), dv, 0.0, 1.0, 100, 0);
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
		mm_json_set(o, k, mm_json_string(mm_replace(s, "#", mm_printf("%d", index))));
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
			mm_json_set(p, "name", mm_json_string(name));
			if (n == 2 && strcmp(ks[0], "False") == 0 && strcmp(ks[1], "True") == 0) {
				mm_json_set(p, "type", mm_json_string("boolean"));
			}
			else {
				mm_json_set(p, "type", mm_json_string("enum"));
				mm_json_t *values = mm_json_new(MM_JSON_ARRAY);
				for (i32 k = 0; k < n; ++k) {
					mm_json_t *e = mm_json_new(MM_JSON_OBJECT);
					mm_json_set(e, "name", mm_json_string(ks[k]));
					mm_json_set(e, "value", mm_json_string(ks[k]));
					mm_json_push(values, e);
				}
				mm_json_set(p, "values", values);
			}
			mm_json_set(p, "default", mm_json_number(0));
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
			mm_json_set(p, "name", mm_json_string(name));
			mm_json_set(p, "label", mm_json_string(mm_json_str(mm_json_get(w, "label"), "")));
		}
		else if (strcmp(type, "named_parameter") == 0) {
			p = mm_json_new(MM_JSON_OBJECT);
			mm_json_set(p, "name", mm_json_string(name));
			mm_json_set(p, "label", mm_json_string(mm_json_str(mm_json_get(w, "label"), "")));
			mm_json_set(p, "type", mm_json_string("float"));
			mm_json_set(p, "min", mm_json_number(mm_json_num(mm_json_get(w, "min"), 0)));
			mm_json_set(p, "max", mm_json_number(mm_json_num(mm_json_get(w, "max"), 1)));
			mm_json_set(p, "step", mm_json_number(mm_json_num(mm_json_get(w, "step"), 0.01)));
			mm_json_set(p, "default", mm_json_number(mm_json_num(mm_json_get(w, "default"), 0.5)));
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
		any_array_push(def->buttons, mm_nodes_button("mm_value", "CUSTOM", f32_array_create_x(0), mm_nodes_text_data(mm_json_text(v)), 0, 1, 100, 1));
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
			mm_json_set(o, "type", mm_json_string(types[i]));
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
		mm_json_push(e, mm_json_string(params[i].name));
		mm_json_push(e, mm_json_number(params[i].kind));
		mm_json_push(e, mm_json_number(params[i].button));
		mm_json_push(e, mm_json_number(params[i].first));
		mm_json_push(e, mm_json_string(params[i].label));
		mm_json_push(layout, e);
	}
	mm_json_set(s, "layout", layout);
	return s;
}

static ui_node_button_t *mm_nodes_state_button(mm_json_t *state) {
	return mm_nodes_button("mm_state", "CUSTOM", f32_array_create_x(0), mm_nodes_text_data(mm_json_text(state)), 0, 1, 100, 0);
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
		mm_json_set(e, "generic_size", mm_json_number(1));
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
