// Material Maker graphs: generators built from .ptex / .mmg data (loader.gd create_gen, gen_graph.gd, gen_remote.gd,
// gen_base.gd parameters and seeds), port lookup through subgraphs, and the library of node definitions.

#include "mm_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MM_MAX_SEED 4294967296.0

// Library

static mm_pack_t *mm_nodes = NULL;

typedef struct {
	char      *name;
	mm_json_t *json;
} mm_libcache_t;

static mm_libcache_t *mm_lib     = NULL;
static int            mm_lib_n   = 0;
static char          *mm_sf      = NULL; // Shader functions

void mm_set_nodes(mm_pack_t *nodes) {
	mm_nodes = nodes;
}

mm_json_t *mm_library_node(const char *type) {
	if (type == NULL || mm_nodes == NULL) {
		return NULL;
	}
	for (int i = 0; i < mm_lib_n; ++i) {
		if (strcmp(mm_lib[i].name, type) == 0) {
			return mm_lib[i].json;
		}
	}
	int        k    = mm_pack_find(mm_nodes, "name", type);
	mm_json_t *json = NULL;
	if (k >= 0) {
		size_t len;
		char  *text = mm_pack_read(mm_nodes, k, &len);
		if (text != NULL) {
			json = mm_json_parse_persistent(text, len);
			free(text);
		}
	}
	mm_lib                  = realloc(mm_lib, sizeof(mm_libcache_t) * (mm_lib_n + 1));
	mm_lib[mm_lib_n].name   = strdup(type);
	mm_lib[mm_lib_n].json   = json;
	mm_lib_n++;
	return json;
}

const char *mm_shader_functions(void) {
	if (mm_sf == NULL && mm_nodes != NULL) {
		int k = mm_pack_find(mm_nodes, "name", "shader_functions");
		if (k >= 0) {
			mm_sf = mm_pack_read(mm_nodes, k, NULL);
		}
	}
	return mm_sf != NULL ? mm_sf : "";
}

// Generators

int mm_next_id = 1; // Also mm_embed.c

static const char *jstr(mm_json_t *o, const char *k) {
	return mm_json_str(mm_json_get(o, k), NULL);
}

static mm_json_t *jstring(const char *s) {
	mm_json_t *v = mm_json_new(MM_JSON_STRING);
	v->string    = mm_strdup(s);
	return v;
}

static mm_json_t *jnumber(double d) {
	mm_json_t *v = mm_json_new(MM_JSON_NUMBER);
	v->number    = d;
	v->is_int    = d == floor(d);
	return v;
}

mm_gen_t *mm_gen_child(mm_gen_t *graph, const char *name) {
	for (int i = 0; graph != NULL && i < graph->nchildren; ++i) {
		if (strcmp(graph->children[i]->name, name) == 0) {
			return graph->children[i];
		}
	}
	return NULL;
}

const char *mm_gen_hier_name(mm_gen_t *g) {
	return g->name;
}

static mm_port_t graph_port_source(mm_gen_t *graph, const char *gen_name, int input_index) {
	mm_port_t rv = {NULL, 0};
	if (strcmp(gen_name, "gen_inputs") == 0) {
		if (graph->parent != NULL && graph->parent->kind == MM_G_GRAPH) {
			rv = graph_port_source(graph->parent, graph->name, input_index);
		}
	}
	else {
		for (int i = 0; graph->connections != NULL && i < graph->connections->count; ++i) {
			mm_json_t *c = graph->connections->items[i];
			if (strcmp(mm_json_str(mm_json_get(c, "to"), ""), gen_name) == 0 && (int)mm_json_num(mm_json_get(c, "to_port"), -1) == input_index) {
				mm_gen_t *src = mm_gen_child(graph, mm_json_str(mm_json_get(c, "from"), ""));
				if (src != NULL) {
					if (src->kind == MM_G_GRAPH) {
						rv = graph_port_source(src, "gen_outputs", (int)mm_json_num(mm_json_get(c, "from_port"), 0));
					}
					else {
						rv.gen    = src;
						rv.output = (int)mm_json_num(mm_json_get(c, "from_port"), 0);
					}
				}
			}
		}
	}
	if (rv.gen != NULL && strcmp(rv.gen->name, "gen_inputs") == 0 && rv.gen->parent != NULL) {
		rv = graph_port_source(rv.gen->parent, "gen_inputs", rv.output);
	}
	return rv;
}

mm_port_t mm_get_source(mm_gen_t *g, int input_index) {
	mm_port_t none = {NULL, 0};
	if (g->parent == NULL) {
		return none;
	}
	return graph_port_source(g->parent, g->name, input_index);
}

double mm_gen_seed(mm_gen_t *g) {
	double s = g->seed_value;
	if (!g->seed_locked && g->parent != NULL && g->parent->kind == MM_G_GRAPH && g->parent->transmits_seed) {
		s += mm_gen_seed(g->parent);
	}
	return s;
}

bool mm_gen_has_randomness(mm_gen_t *g) {
	if (g->kind == MM_G_SHADER) {
		return g->model_uses_seed || g->params_use_seed;
	}
	if (g->kind == MM_G_GRAPH) {
		if (!g->transmits_seed) {
			return false;
		}
		for (int i = 0; i < g->nchildren; ++i) {
			if (mm_gen_has_randomness(g->children[i]) && !g->children[i]->seed_locked) {
				return true;
			}
		}
	}
	return false;
}

static mm_json_t *def(const char *name, const char *type, double dflt) {
	mm_json_t *d = mm_json_new(MM_JSON_OBJECT);
	mm_json_set(d, "name", jstring(name));
	mm_json_set(d, "type", jstring(type));
	mm_json_set(d, "default", jnumber(dflt));
	return d;
}

static mm_json_t *remote_param_defs(mm_gen_t *g) {
	mm_json_t *rv = mm_json_new(MM_JSON_ARRAY);
	for (int i = 0; g->widgets != NULL && i < g->widgets->count; ++i) {
		mm_json_t  *w    = g->widgets->items[i];
		const char *type = mm_json_str(mm_json_get(w, "type"), "");
		const char *name = mm_json_str(mm_json_get(w, "name"), "");
		if (strcmp(type, "config_control") == 0) {
			mm_json_t *cfg = mm_json_get(w, "configurations");
			bool       tf  = cfg != NULL && cfg->count == 2 &&
			         ((strcmp(cfg->keys[0], "False") == 0 && strcmp(cfg->keys[1], "True") == 0) || (strcmp(cfg->keys[0], "True") == 0 && strcmp(cfg->keys[1], "False") == 0));
			mm_json_push(rv, def(name, tf ? "boolean" : "enum", 0));
		}
		else if (strcmp(type, "linked_control") == 0) {
			mm_json_t *links = mm_json_get(w, "linked_widgets");
			mm_json_t *pd    = NULL;
			if (links != NULL && links->count > 0 && g->parent != NULL) {
				mm_gen_t *target = mm_gen_child(g->parent, mm_json_str(mm_json_get(links->items[0], "node"), ""));
				if (target != NULL) {
					pd = mm_gen_param_def(target, mm_json_str(mm_json_get(links->items[0], "widget"), ""));
				}
			}
			if (pd != NULL) {
				pd = mm_json_copy(pd);
				mm_json_set(pd, "name", jstring(name));
				mm_json_push(rv, pd);
			}
		}
		else if (strcmp(type, "named_parameter") == 0) {
			mm_json_push(rv, def(name, "float", mm_json_num(mm_json_get(w, "default"), 0.5)));
		}
	}
	return rv;
}

static mm_json_t *param_defs(mm_gen_t *g) {
	mm_json_t *rv = mm_json_new(MM_JSON_ARRAY);
	switch (g->kind) {
	case MM_G_SHADER:
		return g->param_defs != NULL ? g->param_defs : rv;
	case MM_G_GRAPH: {
		mm_gen_t *gp = mm_gen_child(g, "gen_parameters");
		return gp != NULL ? param_defs(gp) : rv;
	}
	case MM_G_REMOTE:
		return remote_param_defs(g);
	case MM_G_BUFFER: {
		mm_json_t *s = def("size", "size", 4);
		mm_json_set(s, "first", jnumber(4));
		mm_json_push(rv, s);
		if (g->version == 0) {
			mm_json_push(rv, def("lod", "float", 0));
		}
		else if (g->version == 2) {
			mm_json_push(rv, def("filter", "boolean", 1));
			mm_json_push(rv, def("mipmap", "boolean", 1));
			mm_json_push(rv, def("f32", "boolean", 0));
		}
		return rv;
	}
	case MM_G_ITERATE: {
		mm_json_t *s = def("size", "size", 4);
		mm_json_set(s, "first", jnumber(4));
		mm_json_push(rv, s);
		mm_json_push(rv, def("shrink", "boolean", 0));
		mm_json_push(rv, def("autostop", "boolean", 0));
		mm_json_push(rv, def("iterations", "float", 5));
		mm_json_push(rv, def("filter", "boolean", 1));
		mm_json_push(rv, def("mipmap", "boolean", 1));
		mm_json_push(rv, def("f32", "boolean", 0));
		return rv;
	}
	case MM_G_SWITCH:
		mm_json_push(rv, def("outputs", "float", 2));
		mm_json_push(rv, def("choices", "float", 2));
		mm_json_push(rv, def("source", "float", 0));
		return rv;
	default:
		return rv;
	}
}

mm_json_t *mm_gen_param_def(mm_gen_t *g, const char *name) {
	mm_json_t *defs = param_defs(g);
	for (int i = 0; defs != NULL && i < defs->count; ++i) {
		if (strcmp(mm_json_str(mm_json_get(defs->items[i], "name"), ""), name) == 0) {
			return defs->items[i];
		}
	}
	return NULL;
}

mm_json_t *mm_gen_param(mm_gen_t *g, const char *name) {
	mm_json_t *v = mm_json_get(g->params, name);
	if (v != NULL) {
		return v;
	}
	if ((g->kind == MM_G_BUFFER || g->kind == MM_G_ITERATE) && strcmp(name, "size") == 0) {
		return jnumber(9); // _ready / _init set it when missing
	}
	mm_json_t *d = mm_gen_param_def(g, name);
	if (d == NULL) {
		return NULL;
	}
	mm_json_t *dv = mm_json_get(d, "default");
	if (strcmp(mm_json_str(mm_json_get(d, "type"), ""), "size") == 0) { // init_parameters
		return jnumber(mm_json_num(dv, 0) - mm_json_num(mm_json_get(d, "first"), 0));
	}
	return dv;
}

static bool has_rnd(mm_json_t *v) {
	return v != NULL && v->type == MM_JSON_STRING && (strstr(v->string, "$rnd(") != NULL || strstr(v->string, "$rndi(") != NULL);
}

static int str_cmp(const void *a, const void *b) {
	return strcmp(*(char *const *)a, *(char *const *)b);
}

void mm_gen_set_param(mm_gen_t *g, const char *name, mm_json_t *v) {
	if (g->params == NULL) {
		g->params = mm_json_new(MM_JSON_OBJECT);
	}
	if (g->kind == MM_G_SHADER) {
		if (has_rnd(v)) {
			g->params_use_seed = true;
		}
	}
	if (g->kind == MM_G_REMOTE && g->parent != NULL) {
		for (int i = 0; g->widgets != NULL && i < g->widgets->count; ++i) {
			mm_json_t *w = g->widgets->items[i];
			if (strcmp(mm_json_str(mm_json_get(w, "name"), ""), name) != 0) {
				continue;
			}
			const char *type = mm_json_str(mm_json_get(w, "type"), "");
			if (strcmp(type, "linked_control") == 0) {
				mm_json_t *links = mm_json_get(w, "linked_widgets");
				for (int k = 0; links != NULL && k < links->count; ++k) {
					mm_gen_t *t = mm_gen_child(g->parent, mm_json_str(mm_json_get(links->items[k], "node"), ""));
					if (t != NULL) {
						mm_gen_set_param(t, mm_json_str(mm_json_get(links->items[k], "widget"), ""), v);
					}
				}
			}
			else if (strcmp(type, "config_control") == 0) {
				mm_json_t *cfg = mm_json_get(w, "configurations");
				int        idx = (int)mm_json_num(v, 0);
				if (cfg != NULL && idx >= 0 && idx < cfg->count) {
					char **keys = mm_alloc(sizeof(char *) * cfg->count);
					memcpy(keys, cfg->keys, sizeof(char *) * cfg->count);
					qsort(keys, cfg->count, sizeof(char *), str_cmp);
					mm_json_t *list = mm_json_get(cfg, keys[idx]);
					for (int k = 0; list != NULL && k < list->count; ++k) {
						mm_gen_t *t = mm_gen_child(g->parent, mm_json_str(mm_json_get(list->items[k], "node"), ""));
						if (t != NULL) {
							mm_gen_set_param(t, mm_json_str(mm_json_get(list->items[k], "widget"), ""), mm_json_get(list->items[k], "value"));
						}
					}
				}
				else {
					return; // Incorrect configuration index, Material Maker returns
				}
			}
			break;
		}
	}
	mm_json_set(g->params, name, v);
	if (g->kind == MM_G_REMOTE && g->parent != NULL && strcmp(g->name, "gen_parameters") == 0) {
		mm_json_set(g->parent->params, name, v);
	}
	if (g->kind == MM_G_GRAPH) {
		mm_gen_t *gp = mm_gen_child(g, "gen_parameters");
		if (gp != NULL) {
			mm_gen_set_param(gp, name, v);
		}
	}
}

mm_json_t *mm_gen_output_defs(mm_gen_t *g) {
	switch (g->kind) {
	case MM_G_SHADER:
		return g->outputs;
	case MM_G_GRAPH: {
		mm_gen_t *o = mm_gen_child(g, "gen_outputs");
		return o != NULL ? o->ports : NULL;
	}
	default:
		return NULL;
	}
}

mm_json_t *mm_gen_input_defs(mm_gen_t *g) {
	switch (g->kind) {
	case MM_G_SHADER:
		return g->inputs;
	case MM_G_GRAPH: {
		mm_gen_t *i = mm_gen_child(g, "gen_inputs");
		return i != NULL ? i->ports : NULL;
	}
	default:
		return NULL;
	}
}

// Loading (loader.gd)

static mm_gen_t *create_gen(mm_json_t *data, bool fix, mm_gen_t *parent);

mm_gen_t *mm_create_gen(mm_json_t *data, mm_gen_t *parent) {
	return create_gen(data, true, parent);
}

static void graph_add_children(mm_gen_t *graph, mm_json_t *nodes, mm_json_t *connections) {
	for (int i = 0; nodes != NULL && i < nodes->count; ++i) {
		mm_gen_t *c = create_gen(nodes->items[i], true, graph);
		if (c == NULL) {
			continue;
		}
		mm_gen_t **ch = mm_alloc(sizeof(mm_gen_t *) * (graph->nchildren + 1));
		if (graph->nchildren > 0) {
			memcpy(ch, graph->children, sizeof(mm_gen_t *) * graph->nchildren);
		}
		ch[graph->nchildren++] = c;
		graph->children        = ch;
	}
	if (connections != NULL && connections->type == MM_JSON_ARRAY) {
		if (graph->connections == NULL) {
			graph->connections = mm_json_new(MM_JSON_ARRAY);
		}
		for (int i = 0; i < connections->count; ++i) {
			mm_json_push(graph->connections, connections->items[i]);
		}
	}
	else if (connections != NULL && connections->type == MM_JSON_OBJECT) { // Compact form: {to: {port: {node, port}}}
		if (graph->connections == NULL) {
			graph->connections = mm_json_new(MM_JSON_ARRAY);
		}
		for (int i = 0; i < connections->count; ++i) {
			mm_json_t *ports = connections->items[i];
			for (int k = 0; k < ports->count; ++k) {
				mm_json_t *c = mm_json_new(MM_JSON_OBJECT);
				mm_json_set(c, "from", jstring(mm_json_str(mm_json_get(ports->items[k], "node"), "")));
				mm_json_set(c, "from_port", jnumber(mm_json_num(mm_json_get(ports->items[k], "port"), 0)));
				mm_json_set(c, "to", jstring(connections->keys[i]));
				mm_json_set(c, "to_port", jnumber(atof(ports->keys[k])));
				mm_json_push(graph->connections, c);
			}
		}
	}
}

static void deserialize(mm_gen_t *g, mm_json_t *data) {
	// _deserialize of each class
	switch (g->kind) {
	case MM_G_SHADER: {
		mm_json_t *sm = mm_json_get(data, "shader_model");
		if (sm == NULL) {
			sm = mm_json_get(data, "model_data");
		}
		if (sm == NULL && mm_json_get(data, "sdf_scene") != NULL) {
			sm = mm_sdf_shader_model(mm_json_get(data, "sdf_scene"));
		}
		mm_json_t *gs = mm_json_get(data, "generic_size");
		if (gs != NULL) {
			g->generic_size = (int)mm_json_num(gs, 1);
		}
		if (sm != NULL) {
			mm_shader_set_model(g, sm);
		}
		else if (gs != NULL && g->model != NULL) {
			mm_shader_set_model(g, g->model);
		}
		break;
	}
	case MM_G_GRAPH: {
		mm_json_t *nodes = mm_json_get(data, "nodes");
		graph_add_children(g, nodes, mm_json_get(data, "connections"));
		break;
	}
	case MM_G_IOS:
		g->ports = mm_json_get(data, "ports");
		break;
	case MM_G_REMOTE:
		g->widgets = mm_json_get(data, "widgets");
		break;
	case MM_G_BUFFER:
		g->version = (int)mm_json_num(mm_json_get(data, "version"), 0);
		break;
	default:
		break;
	}
	// gen_base deserialize
	const char *name = jstr(data, "name");
	if (name != NULL) {
		g->name = mm_strdup(name);
	}
	mm_json_t *pos = mm_json_get(data, "node_position");
	if (pos != NULL) {
		g->pos_x = mm_json_num(mm_json_get(pos, "x"), 0);
		g->pos_y = mm_json_num(mm_json_get(pos, "y"), 0);
	}
	mm_json_t *params = mm_json_get(data, "parameters");
	if (params != NULL && params->type == MM_JSON_OBJECT) {
		for (int i = 0; i < params->count; ++i) {
			mm_gen_set_param(g, params->keys[i], params->items[i]);
		}
	}
	else {
		mm_json_t *defs = param_defs(g);
		for (int i = 0; defs != NULL && i < defs->count; ++i) {
			const char *pn = mm_json_str(mm_json_get(defs->items[i], "name"), "");
			if (strcmp(pn, "type") != 0 && mm_json_get(data, pn) != NULL) {
				mm_gen_set_param(g, pn, mm_json_get(data, pn));
			}
		}
	}
	mm_json_t *sl    = mm_json_get(data, "seed_locked");
	g->seed_locked   = sl != NULL && sl->type == MM_JSON_BOOL && sl->boolean;
	if (mm_json_get(data, "seed_int") != NULL) {
		g->seed_value = mm_json_num(mm_json_get(data, "seed_int"), 0) / MM_MAX_SEED;
	}
	else if (mm_json_get(data, "seed") != NULL) {
		g->seed_value = mm_json_num(mm_json_get(data, "seed"), 0);
	}
	else if (mm_json_get(data, "seed_value") != NULL) {
		g->seed_locked = true;
		g->seed_value  = mm_json_num(mm_json_get(data, "seed_value"), 0);
	}
	else {
		g->seed_locked = false;
		int64_t x      = (int64_t)g->pos_x;
		int64_t y      = (int64_t)g->pos_y;
		g->seed_value  = (double)(((x * 0x1f1f1f1f) ^ y) % 65536);
	}
}

static bool has_path(mm_json_t *d, const char *a, const char *b) {
	mm_json_t *v = mm_json_get(d, a);
	return v != NULL && (b == NULL || mm_json_get(v, b) != NULL);
}

static mm_gen_t *new_gen(mm_kind_t kind, mm_gen_t *parent) {
	mm_gen_t *g       = mm_alloc(sizeof(mm_gen_t));
	g->kind           = kind;
	g->id             = mm_next_id++;
	g->parent         = parent;
	g->params         = mm_json_new(MM_JSON_OBJECT);
	g->transmits_seed = true;
	g->generic_size   = 1;
	g->pass           = -1;
	g->name           = "";
	return g;
}

static mm_gen_t *create_gen(mm_json_t *data, bool fix, mm_gen_t *parent) {
	if (data == NULL || data->type != MM_JSON_OBJECT) {
		return NULL;
	}
	if (fix) { // fix_data
		if (mm_json_get(data, "nodes") != NULL && mm_json_get(data, "connections") != NULL && strcmp(mm_json_str(mm_json_get(data, "label"), ""), "HBAO") == 0) {
			data = mm_json_copy(data);
			mm_json_set(data, "type", jstring("hbao"));
			mm_json_set(data, "nodes", NULL);
			mm_json_set(data, "connections", NULL);
		}
		else if (mm_json_get(mm_json_get(data, "shader_model"), "name") != NULL &&
		         strcmp(mm_json_str(mm_json_get(mm_json_get(data, "shader_model"), "name"), ""), "Levels") == 0) {
			data = mm_json_copy(data);
			mm_json_set(data, "shader_model", NULL);
			mm_json_set(data, "type", jstring("height_blend_levels"));
		}
	}
	mm_gen_t   *g    = NULL;
	const char *type = jstr(data, "type");
	if (has_path(data, "shader_model", "preview_shader")) {
		g              = new_gen(MM_G_SHADER, parent);
		g->is_material = true;
	}
	else if (mm_json_get(data, "connections") != NULL || mm_json_get(data, "nodes") != NULL) {
		g = new_gen(MM_G_GRAPH, parent);
	}
	else if (mm_json_get(data, "is_brush") != NULL) {
		g = new_gen(MM_G_OTHER, parent);
	}
	else if (mm_json_get(data, "shader_model") != NULL || mm_json_get(data, "sdf_scene") != NULL || mm_json_get(data, "model_data") != NULL) {
		g = new_gen(MM_G_SHADER, parent);
	}
	else if (mm_json_get(data, "widgets") != NULL) {
		g = new_gen(MM_G_REMOTE, parent);
	}
	if (g == NULL && type != NULL) {
		struct {
			const char *name;
			mm_kind_t   kind;
		} types[] = {{"buffer", MM_G_BUFFER}, {"image", MM_G_IMAGE},     {"text", MM_G_TEXT},       {"iterate_buffer", MM_G_ITERATE},
		             {"ios", MM_G_IOS},       {"switch", MM_G_SWITCH},   {"reroute", MM_G_REROUTE}, {"portal", MM_G_PORTAL},
		             {"export", MM_G_OTHER},  {"comment", MM_G_OTHER},   {"debug", MM_G_OTHER},     {"comment_line", MM_G_OTHER},
		             {"webcam", MM_G_OTHER},  {"meshmap", MM_G_OTHER},   {"autoexport", MM_G_OTHER}};
		for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
			if (strcmp(type, types[i].name) == 0) {
				g = new_gen(types[i].kind, parent);
				break;
			}
		}
		if (g == NULL && strcmp(type, "sdf") == 0) {
			g = new_gen(MM_G_SHADER, parent);
		}
		if (g == NULL && strcmp(type, "material_export") == 0) {
			g              = new_gen(MM_G_SHADER, parent);
			g->is_material = true;
		}
		if (g == NULL) {
			mm_json_t *lib = mm_library_node(type);
			if (lib != NULL) {
				g = create_gen(lib, false, parent);
			}
		}
		if (g != NULL) {
			g->name = mm_strdup(type);
		}
	}
	if (g == NULL) {
		return NULL;
	}
	g->type = type != NULL ? mm_strdup(type) : "";
	g->data = data;
	deserialize(g, data);
	return g;
}

static int analyze_depth = 0;

void mm_graph_reset(void) {
	analyze_depth = 0;
}

void mm_buffer_analyze(mm_gen_t *g) {
	// is_greyscale: the buffer renders greyscale when its source is greyscale (iterate buffers: both sources). Material
	// Maker settles it after a first generation, here it is generated once more when it changes. Done once per buffer:
	// generating the source meets the upstream buffers, which would otherwise be analyzed again (exponential in chains).
	if (g->analyzed != 0 || analyze_depth > 64) { // Done, or being analyzed (an iterate buffer's loop reads itself)
		return;
	}
	g->analyzed = 1;
	analyze_depth++;
	for (int round = 0; round < 2; ++round) {
		bool grey = true;
		int  n    = g->kind == MM_G_ITERATE ? 2 : 1;
		for (int i = 0; i < n; ++i) {
			mm_port_t  src = mm_get_source(g, i);
			mm_code_t *c   = src.gen != NULL ? mm_get_shader_code(src.gen, "uv", src.output, mm_context_new(NULL)) : mm_code_default();
			if (c->output_type == MM_T_COUNT) {
				c = mm_code_default();
			}
			if (c->output_type != MM_T_F) {
				grey = false;
			}
		}
		if (grey == g->greyscale) {
			break;
		}
		g->greyscale = grey;
	}
	analyze_depth--;
	g->analyzed = 2;
}

static mm_material_t *load(const char *ptex, size_t len);

mm_material_t *mm_load(const char *ptex, size_t len) {
	jmp_buf  guard;
	jmp_buf *outer = mm_guard;
	if (setjmp(guard) == 0) {
		mm_guard         = &guard;
		mm_material_t *m = load(ptex, len);
		mm_guard         = outer;
		return m;
	}
	mm_guard         = outer; // The arena grew past its limit
	mm_graph_reset();
	mm_material_t *m = mm_alloc(sizeof(mm_material_t));
	m->error         = "Too large";
	return m;
}

static mm_material_t *load(const char *ptex, size_t len) {
	mm_material_t *m    = mm_alloc(sizeof(mm_material_t));
	mm_json_t     *json = mm_json_parse(ptex, len);
	if (json == NULL) {
		m->error = "Not a Material Maker file";
		return m;
	}
	mm_next_id = 1;
	m->root    = create_gen(json, true, NULL);
	if (m->root == NULL || m->root->kind != MM_G_GRAPH) {
		m->error = "Not a Material Maker graph";
		return m;
	}
	for (int i = 0; i < m->root->nchildren; ++i) {
		mm_gen_t *c = m->root->children[i];
		if (c->is_material) {
			m->material = c;
			m->kind     = c->type != NULL && c->type[0] ? c->type : "material_export";
			m->size     = (int)mm_json_num(mm_gen_param(c, "size"), 10);
			break;
		}
	}
	if (m->material == NULL) {
		m->error = "No material node";
	}
	return m;
}
