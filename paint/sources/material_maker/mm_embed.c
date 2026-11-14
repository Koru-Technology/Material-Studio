// Embedding: graphs that mix Material Maker nodes with the app's own nodes, generated into the app's shaders (mm.h,
// "Embedding"). The app mirrors its node canvas as a graph: Material Maker nodes by type and parameters, its own nodes
// as native generators whose code at a uv comes from the native code callback. An emitter per shader hands out code
// pieces: declarations for the shader's global scope, statements for the current scope and the value.

#include "mm_internal.h"
#include <string.h>

static mm_native_fn mm_native = NULL;
extern int          mm_next_id; // mm_graph.c

void mm_set_native_code(mm_native_fn fn) {
	mm_native = fn;
}

void mm_reset_ids(void) {
	mm_next_id = 1;
}

static mm_json_t *jstring(const char *s) {
	mm_json_t *v = mm_json_new(MM_JSON_STRING);
	v->string    = mm_strdup(s);
	return v;
}

static mm_json_t *jnumber(double d) {
	mm_json_t *v = mm_json_new(MM_JSON_NUMBER);
	v->number    = d;
	return v;
}

static void add_child(mm_gen_t *graph, mm_gen_t *g) {
	mm_gen_t **ch = mm_alloc(sizeof(mm_gen_t *) * (graph->nchildren + 1));
	if (graph->nchildren > 0) {
		memcpy(ch, graph->children, sizeof(mm_gen_t *) * graph->nchildren);
	}
	ch[graph->nchildren++] = g;
	graph->children        = ch;
}

mm_gen_t *mm_graph_new(void) {
	mm_json_t *data = mm_json_new(MM_JSON_OBJECT);
	mm_json_set(data, "type", jstring("graph"));
	mm_json_set(data, "nodes", mm_json_new(MM_JSON_ARRAY));
	mm_json_set(data, "connections", mm_json_new(MM_JSON_ARRAY));
	return mm_create_gen(data, NULL);
}

mm_gen_t *mm_graph_add_node(mm_gen_t *graph, const char *name, const char *type, mm_json_t *parameters, double seed, double x, double y) {
	mm_json_t *data = mm_json_new(MM_JSON_OBJECT);
	mm_json_set(data, "name", jstring(name));
	mm_json_set(data, "type", jstring(type));
	if (parameters != NULL) {
		mm_json_set(data, "parameters", parameters);
	}
	mm_json_t *pos = mm_json_new(MM_JSON_OBJECT);
	mm_json_set(pos, "x", jnumber(x));
	mm_json_set(pos, "y", jnumber(y));
	mm_json_set(data, "node_position", pos);
	if (seed >= 0.0) {
		mm_json_set(data, "seed", jnumber(seed));
	}
	mm_gen_t *g = mm_create_gen(data, graph);
	if (g != NULL) {
		add_child(graph, g);
	}
	return g;
}

mm_gen_t *mm_graph_add_data(mm_gen_t *graph, const char *name, mm_json_t *data) {
	data = mm_json_copy(data);
	mm_json_set(data, "name", jstring(name));
	mm_gen_t *g = mm_create_gen(data, graph);
	if (g != NULL) {
		add_child(graph, g);
	}
	return g;
}

mm_gen_t *mm_graph_add_native(mm_gen_t *graph, const char *name, void *node, int output, const char *type) {
	mm_gen_t *g      = mm_alloc(sizeof(mm_gen_t));
	g->kind          = MM_G_NATIVE;
	g->id            = mm_next_id++;
	g->name          = mm_strdup(name);
	g->type          = "native";
	g->parent        = graph;
	g->params        = mm_json_new(MM_JSON_OBJECT);
	g->pass          = -1;
	g->native        = node;
	g->native_output = output;
	g->native_type   = mm_type_from_name(type);
	if (g->native_type == MM_T_COUNT) {
		g->native_type = MM_T_RGBA;
	}
	add_child(graph, g);
	return g;
}

mm_gen_t *mm_graph_add_constant(mm_gen_t *graph, const char *name, const char *type, const char *value) {
	mm_gen_t *g     = mm_graph_add_native(graph, name, NULL, 0, type);
	g->native_value = mm_strdup(value);
	return g;
}

void mm_graph_connect(mm_gen_t *graph, const char *from, int from_port, const char *to, int to_port) {
	if (graph->connections == NULL) {
		graph->connections = mm_json_new(MM_JSON_ARRAY);
	}
	mm_json_t *c = mm_json_new(MM_JSON_OBJECT);
	mm_json_set(c, "from", jstring(from));
	mm_json_set(c, "from_port", jnumber(from_port));
	mm_json_set(c, "to", jstring(to));
	mm_json_set(c, "to_port", jnumber(to_port));
	mm_json_push(graph->connections, c);
}

// Native generators (gen_get_code_raw, MM_G_NATIVE)

extern bool mm_planning_standalone; // mm_plan.c: planning buffer passes, our nodes can not run there
extern bool mm_planning_native_hit;

mm_code_t *mm_native_code(mm_gen_t *g, const char *uv, mm_context_t *ctx) {
	if (g->native_value != NULL) { // A constant (mm_graph_add_constant): the same anywhere, buffer passes included
		mm_code_t *rv                      = mm_code_new();
		rv->output_type                    = (mm_type_t)g->native_type;
		rv->output_values[rv->output_type] = g->native_value;
		return rv;
	}
	if (mm_planning_standalone) {
		mm_planning_native_hit = true;
		return mm_code_default();
	}
	mm_code_t *rv   = mm_code_new();
	rv->output_type = (mm_type_t)g->native_type;
	char *var       = NULL;
	int   variant   = mm_context_get_variant(ctx, g, uv);
	if (variant == -1) {
		variant           = mm_context_get_variant(ctx, g, uv);
		bool       main   = !mm_context_has_parent(ctx) && strcmp(uv, MM_MAIN_UV) == 0;
		mm_native_t n     = mm_native != NULL ? mm_native(g->native, g->native_output, uv, main) : (mm_native_t){NULL, NULL};
		const char *glsl  = mm_types[g->native_type].glsl;
		var               = mm_printf("o%d_%d_native", g->id, variant);
		const char *value = n.value != NULL ? n.value : "0.0";
		if (n.code != NULL && n.code[0] != 0) { // Its own scope: the native code redeclares the built-ins it uses
			mm_buf_printf(&rv->code, "%s %s;\n{\n%s\n%s = %s;\n}\n", glsl, var, n.code, var, value);
		}
		else {
			mm_buf_printf(&rv->code, "%s %s = %s;\n", glsl, var, value);
		}
	}
	else {
		var = mm_printf("o%d_%d_native", g->id, variant);
	}
	rv->output_values[rv->output_type] = var;
	return rv;
}

// Emitters

struct mm_emitter {
	mm_context_t *ctx;
	char        **names; // Declarations emitted: uniforms and globals
	int           count;
	char         *one;   // A run time 1.0 (see mm_uniform_runtime_declaration)
};

mm_emitter_t *mm_emitter_new(const char *one) {
	mm_emitter_t *e = mm_alloc(sizeof(mm_emitter_t));
	e->ctx          = mm_context_new(NULL);
	e->one          = mm_strdup(one != NULL ? one : "1.0");
	return e;
}

// A nested scope (code evaluated at another uv): variants emitted outside are not visible there, they are emitted
// again. Declarations stay shared.
void mm_emitter_push(mm_emitter_t *e) {
	e->ctx = mm_context_new(e->ctx);
}

void mm_emitter_pop(mm_emitter_t *e) {
	mm_context_t *parent = mm_context_parent(e->ctx);
	if (parent != NULL) {
		e->ctx = parent;
	}
}

// Undoing an emission the caller did not use: what it emitted is emitted again next time
mm_emit_mark_t mm_emitter_mark(mm_emitter_t *e) {
	mm_emit_mark_t m = {mm_context_copy(e->ctx), e->count};
	return m;
}

void mm_emitter_reset(mm_emitter_t *e, mm_emit_mark_t m) {
	e->ctx   = m.ctx;
	e->count = m.count;
}

static bool emitted(mm_emitter_t *e, const char *name) {
	for (int i = 0; i < e->count; ++i) {
		if (strcmp(e->names[i], name) == 0) {
			return true;
		}
	}
	return false;
}

static void set_emitted(mm_emitter_t *e, const char *name) {
	char **n = mm_alloc(sizeof(char *) * (e->count + 1));
	if (e->count > 0) {
		memcpy(n, e->names, sizeof(char *) * e->count);
	}
	n[e->count++] = mm_strdup(name);
	e->names      = n;
}

mm_emit_t mm_emit(mm_emitter_t *e, mm_gen_t *g, int output, const char *uv, const char *type) {
	mm_emit_t  rv = {0};
	mm_code_t *c  = mm_get_shader_code(g, uv, output, e->ctx);
	if (c->output_type == MM_T_COUNT) {
		c = mm_code_default();
	}
	mm_type_t want = type != NULL ? mm_type_from_name(type) : c->output_type;
	if (want == MM_T_COUNT) {
		want = c->output_type;
	}
	rv.value = c->output_values[want];
	rv.type  = mm_types[want].name;
	if (rv.value == NULL) { // No conversion to that type
		rv.value = c->output_values[c->output_type];
		rv.type  = mm_types[c->output_type].name;
	}

	mm_buf_t decl = {0};
	for (int i = 0; i < c->nuniforms; ++i) {
		mm_uniform_t *u = &c->uniforms[i];
		if (u->type == MM_U_SAMPLER) { // Rendered by the app (mm_plan_buffers), bound under this name
			rv.needs_buffers = true;
			char     **names = mm_alloc(sizeof(char *) * (rv.nbuffers + 1));
			mm_gen_t **gens  = mm_alloc(sizeof(mm_gen_t *) * (rv.nbuffers + 1));
			int       *outs  = mm_alloc(sizeof(int) * (rv.nbuffers + 1));
			if (rv.nbuffers > 0) {
				memcpy(names, rv.buffer_names, sizeof(char *) * rv.nbuffers);
				memcpy(gens, rv.buffer_gens, sizeof(mm_gen_t *) * rv.nbuffers);
				memcpy(outs, rv.buffer_outputs, sizeof(int) * rv.nbuffers);
			}
			names[rv.nbuffers] = u->name;
			gens[rv.nbuffers]  = u->texture;
			outs[rv.nbuffers]  = u->texture_output;
			rv.buffer_names    = names;
			rv.buffer_gens     = gens;
			rv.buffer_outputs  = outs;
			rv.nbuffers++;
			continue;
		}
		if (!emitted(e, u->name)) {
			set_emitted(e, u->name);
			mm_buf_add(&decl, mm_uniform_runtime_declaration(u, e->one));
		}
	}
	// Globals the code uses, without the declarations emitted for earlier outputs
	char *defs = mm_buf_get(&c->defs);
	char *code = mm_buf_get(&c->code);
	char *used = mm_printf("%s%s%s", defs, code, rv.value);
	char *globs = mm_strdup("");
	for (int gi = c->nglobals - 1; gi >= 0; --gi) {
		mm_strs_t decls = mm_split_glsl(c->globals[gi].code);
		for (int di = decls.count - 1; di >= 0; --di) {
			char *d    = decls.items[di];
			char *name = mm_glsl_declaration_name(d);
			if (name[0] != 0 && emitted(e, name)) {
				continue;
			}
			if (name[0] == 0 || strstr(used, name) != NULL || strstr(globs, name) != NULL) {
				globs = mm_printf("%s\n%s", d, globs);
				if (name[0] != 0) {
					set_emitted(e, name);
				}
			}
		}
	}
	mm_buf_add(&decl, globs);
	mm_buf_add(&decl, "\n");
	mm_buf_add(&decl, defs);
	rv.declarations = mm_buf_get(&decl);
	rv.code         = code;
	return rv;
}
