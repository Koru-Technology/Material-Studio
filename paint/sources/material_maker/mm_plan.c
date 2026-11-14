// Material Maker render passes: the buffers a material samples (gen_buffer.gd, gen_iterate_buffer.gd) and its maps
// (the material node's outputs, as Material Maker exports them), each a fragment shader in the iron_glsl.h conventions
// equivalent to Material Maker's buffer_compute.tres.

// Each pass's code is generated in the arena and released once its GLSL is copied out (Material Maker's generation
// makes many intermediate strings): the planning state, the GLSL and the texture lists are malloc'd until the end.

#include "mm_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	mm_gen_t  *gen;
	int        pass;    // -1 while being planned
	mm_gen_t  *loop_of; // The iterate buffer whose loop it belongs to
} entry_t;

typedef struct {
	mm_pass_t  p;            // glsl, glsl_loop and name malloc'd
	mm_gen_t **tex_gen;      // For each texture, the generator rendering it, NULL: own previous iteration (malloc'd)
	mm_gen_t **loop_tex_gen;
	mm_gen_t  *loop_of;
} tmp_pass_t;

typedef struct {
	entry_t    *entries;
	int         nentries;
	tmp_pass_t *passes;
	int         npasses;
	char       *error;
	mm_gen_t   *looping; // The iterate buffer whose loop source is being composed
	mm_gen_t  **hoists;  // MM_G_HOIST generators made (malloc'd, freed by finish)
	int         nhoists;
} plan_t;

// Loop-invariant inputs of an iterate buffer's loop source. Material Maker renders the whole loop source every
// iteration; whatever it reads that does not depend on the loop texture (nor on the iteration) is the same each time,
// so it renders once into its own pass and the loop reads it back. Only requests at the pixel's own uv (the root
// context, uv as is) are answered that way: elsewhere a texture read would need filtering, which is not exact.

typedef struct {
	mm_gen_t *gen;
	int       output;
	mm_gen_t *hoist; // Made when first substituted
	bool      ok;    // Decided invariant and worth a pass
	int       type;  // Its output type, when ok
} hoist_cand_t;

typedef struct {
	plan_t       *s;
	mm_gen_t     *self; // The iterate buffer
	int           size; // Its size
	hoist_cand_t *cands;
	int           ncands;
	bool          substitute; // false: recording requests, true: answering them
} hoist_state_t;

// "uv", "(uv)", "((uv))": the pixel's own coordinates
static bool plain_uv(const char *uv) {
	while (*uv == '(') {
		uv++;
	}
	size_t n = strlen(uv);
	while (n > 0 && uv[n - 1] == ')') {
		n--;
	}
	return n == 2 && strncmp(uv, "uv", 2) == 0;
}

static mm_gen_t *hoist_hook(mm_gen_t *g, const char *uv, int output, mm_context_t *ctx, void *data) {
	hoist_state_t *h = data;
	if (mm_context_has_parent(ctx) || !plain_uv(uv)) {
		return NULL;
	}
	for (int i = 0; i < h->ncands; ++i) {
		hoist_cand_t *c = &h->cands[i];
		if (c->gen == g && c->output == output) {
			if (!h->substitute || !c->ok) {
				return NULL;
			}
			if (c->hoist == NULL) {
				mm_gen_t *x      = calloc(1, sizeof(mm_gen_t));
				x->kind          = MM_G_HOIST;
				x->id            = 1000000 + h->s->nhoists; // Names o<id>_tex, apart from Material Maker's ids
				x->name          = strdup(mm_printf("hoist_%d", x->id));
				x->type          = x->name;
				x->pass          = -1;
				x->hoist_src     = g;
				x->hoist_output  = output;
				x->hoist_size    = h->size;
				x->hoist_type    = c->type;
				h->s->hoists     = realloc(h->s->hoists, sizeof(mm_gen_t *) * (h->s->nhoists + 1));
				h->s->hoists[h->s->nhoists++] = x;
				c->hoist         = x;
			}
			return c->hoist;
		}
	}
	if (!h->substitute) {
		h->cands                = realloc(h->cands, sizeof(hoist_cand_t) * (h->ncands + 1));
		h->cands[h->ncands++] = (hoist_cand_t){.gen = g, .output = output};
	}
	return NULL;
}

static entry_t *find_entry(plan_t *s, mm_gen_t *g) {
	for (int i = 0; i < s->nentries; ++i) {
		if (s->entries[i].gen == g) {
			return &s->entries[i];
		}
	}
	return NULL;
}

static int add_pass(plan_t *s, tmp_pass_t *p) {
	s->passes             = realloc(s->passes, sizeof(tmp_pass_t) * (s->npasses + 1));
	s->passes[s->npasses] = *p;
	return s->npasses++;
}

static void fail(plan_t *s, const char *msg) {
	if (s->error == NULL) {
		s->error = strdup(msg);
	}
}

static double iterations_of(mm_gen_t *g) {
	mm_json_t *v = mm_gen_param(g, "iterations");
	return v != NULL && v->type == MM_JSON_NUMBER ? floor(v->number) : 1.0; // Expressions: TODO named parameters
}

static const char *rgba_value(mm_code_t *c) {
	if (c->output_values[MM_T_RGBA] != NULL) {
		return c->output_values[MM_T_RGBA];
	}
	if (c->output_values[MM_T_RGB] != NULL) {
		return mm_printf("vec4(%s, 1.0)", c->output_values[MM_T_RGB]);
	}
	if (c->output_values[MM_T_F] != NULL) {
		return mm_printf("vec4(vec3(%s), 1.0)", c->output_values[MM_T_F]);
	}
	return "vec4(0.0, 0.0, 0.0, 1.0)";
}

static int plan_gen(plan_t *s, mm_gen_t *g);

static entry_t *find_entry(plan_t *s, mm_gen_t *g);

// Which recorded requests can be hoisted: the code of that output, made on its own, reads neither the loop texture nor
// a pass of the loop nor the iteration, is of a type a texture holds exactly, and is more than a few operations (a pass
// costs a texture and a draw). Called after the loop source was composed once: the textures it reads are planned.
static bool hoist_decide(hoist_state_t *h) {
	bool        any  = false;
	const char *iter = mm_printf("o%d_iteration", h->self->id);
	for (int i = 0; i < h->ncands; ++i) {
		hoist_cand_t *c = &h->cands[i];
		mm_gen_t     *g = c->gen;
		if (g->kind == MM_G_BUFFER || g->kind == MM_G_ITERATE || g->kind == MM_G_IMAGE || g->kind == MM_G_TEXT || g->kind == MM_G_HOIST ||
		    g->kind == MM_G_OTHER || g->kind == MM_G_NATIVE) {
			continue;
		}
		mm_code_t *code = mm_get_shader_code(g, "uv", c->output, mm_context_new(NULL));
		int        t    = code->output_type;
		if ((t != MM_T_F && t != MM_T_RGB && t != MM_T_RGBA) || code->output_values[t] == NULL) {
			continue;
		}
		if (strlen(mm_buf_get(&code->defs)) + strlen(mm_buf_get(&code->code)) < 256) {
			continue;
		}
		bool dependent = false;
		for (int k = 0; k < code->nuniforms && !dependent; ++k) {
			mm_uniform_t *u = &code->uniforms[k];
			if (u->type == MM_U_SAMPLER) {
				entry_t *e = u->texture != NULL ? find_entry(h->s, u->texture) : NULL;
				dependent  = u->texture == NULL || u->texture == h->self || e == NULL || e->pass == -1 || e->loop_of == h->self;
			}
			else {
				dependent = strcmp(u->name, iter) == 0;
			}
		}
		if (!dependent) {
			c->ok   = true;
			c->type = t;
			any     = true;
		}
	}
	if (getenv("MM_HOIST_DEBUG") != NULL) { // Env MM_HOIST_DEBUG: what each iterate buffer's loop could hoist
		for (int i = 0; i < h->ncands; ++i) {
			fprintf(stderr, "[hoist] o%d loop: o%d %s output %d: %s\n", h->self->id, h->cands[i].gen->id, h->cands[i].gen->name, h->cands[i].output,
			        h->cands[i].ok ? "hoisted" : "kept");
		}
	}
	return any;
}

// The GLSL of a pass rendering c. self: the iterate buffer this is a source of, loop: its loop source.
static char *compose(plan_t *s, mm_code_t *c, mm_gen_t *self, bool loop, mm_gen_t ***tex_gen, int *ntex, mm_gen_t **loop_of) {
	mm_buf_t decl = {0};
	mm_buf_t uni  = {0};
	*ntex         = 0;
	*tex_gen      = calloc(c->nuniforms + 1, sizeof(mm_gen_t *));
	for (int i = 0; i < c->nuniforms; ++i) {
		mm_uniform_t *u = &c->uniforms[i];
		if (u->type == MM_U_SAMPLER) {
			mm_gen_t *t = u->texture;
			mm_gen_t *src;
			if (t == NULL || (t->kind != MM_G_BUFFER && t->kind != MM_G_ITERATE && t->kind != MM_G_IMAGE && t->kind != MM_G_TEXT && t->kind != MM_G_HOIST)) {
				fail(s, mm_printf("Unsupported texture node %s", t != NULL ? t->type : "?"));
				src = NULL;
			}
			else if (t->kind == MM_G_ITERATE && u->texture_output == 1) {
				entry_t *e = find_entry(s, t);
				if (t == self && loop) {
					src = NULL; // Own previous iteration
				}
				else {
					// Whatever reads an iterate buffer's loop texture is re-rendered each time it changes (Material
					// Maker's dependency updates): it is part of that loop, whether planned from inside it or not
					if (e == NULL || e->pass != -1) {
						plan_gen(s, t);
					}
					src      = t;
					*loop_of = t;
				}
			}
			else {
				plan_gen(s, t);
				entry_t *e = find_entry(s, t);
				if (e != NULL && e->pass == -1) {
					if (s->looping != NULL) {
						// Reached again from an iterate buffer's loop while being planned (it reads the loop texture
						// and the loop reads it): it is part of that loop, re-rendered each iteration as in Material Maker
						e->loop_of = s->looping;
						*loop_of   = s->looping;
					}
					else {
						fail(s, mm_printf("Buffer cycle through %s", t->type));
					}
				}
				if (e != NULL && e->loop_of != NULL && e->loop_of != self) {
					*loop_of = e->loop_of;
				}
				src = t;
			}
			mm_buf_printf(&decl, "layout(set = 0, binding = %d) uniform texture2D %s_t;\n#define %s sampler2D(%s_t, sampler_linear)\n", 2 + *ntex, u->name,
			              u->name, u->name);
			(*tex_gen)[(*ntex)++] = src;
			continue;
		}
		// An iterate buffer's iteration: dynamic in its loop pass
		const char *it = strstr(u->name, "_iteration");
		if (it != NULL && it[10] == 0 && u->name[0] == 'o') {
			if (self != NULL && strncmp(u->name + 1, mm_printf("%d_", self->id), strlen(mm_printf("%d_", self->id))) == 0) {
				if (loop) {
					mm_buf_printf(&uni, "#define %s int(constants.mm.y)\n", u->name);
				}
				else {
					mm_buf_printf(&uni, "const int %s = 0;\n", u->name);
				}
				continue;
			}
			mm_gen_t *t = NULL;
			for (int k = 0; k < s->nentries; ++k) {
				if (strcmp(mm_printf("o%d_iteration", s->entries[k].gen->id), u->name) == 0) {
					t = s->entries[k].gen;
				}
			}
			if (t != NULL) {
				mm_buf_printf(&uni, "const int %s = %d;\n", u->name, (int)iterations_of(t) + 1);
				continue;
			}
		}
		mm_buf_add(&uni, mm_uniform_runtime_declaration(u, "constants.mm.w"));
	}

	const char *value = rgba_value(c);
	char       *defs  = mm_buf_get(&c->defs);
	char       *code  = mm_buf_get(&c->code);
	char       *globs = mm_code_globals_string(c, mm_printf("%s%s%s", defs, code, value));

	mm_buf_t b = {0};
	mm_buf_add(&b, "#version 450\n\n"
	               "layout(set = 0, binding = 0, std140) uniform Constants {\n\tvec4 mm;\n} constants;\n\n"
	               "layout(set = 0, binding = 1) uniform sampler sampler_linear;\n");
	mm_buf_add(&b, mm_buf_get(&decl));
	mm_buf_add(&b, "\n#ifdef VERT\n"
	               "layout(location = 0) in vec2 in_pos;\n"
	               "void main() {\n\tgl_Position = vec4(in_pos.xy, 0.0, 1.0);\n}\n"
	               "#endif\n\n"
	               "#ifdef FRAG\n");
	mm_buf_add(&b, mm_buf_get(&uni));
	mm_buf_add(&b, "\nconst float PI = 3.14159265359;\nconst float TAU = 2.0*PI;\n\n");
	mm_buf_add(&b, mm_shader_functions());
	mm_buf_add(&b, "\nconst float seed_variation = 0.0;\nconst float elapsed_time = 0.0;\n\n");
	mm_buf_add(&b, globs);
	mm_buf_add(&b, "\n");
	mm_buf_add(&b, defs);
	mm_buf_add(&b, "\nlayout(location = 0) out vec4 frag_color;\n\n"
	               "void main() {\n"
	               "\tfloat _seed_variation_ = seed_variation;\n"
	               "\tvec4 _controlled_variation_ = vec4(0.0);\n"
	               "\tvec2 pixel = gl_FragCoord.xy;\n"
	               "\tvec2 image_size = vec2(constants.mm.x);\n"
	               "\tvec2 uv = pixel/image_size;\n");
	mm_buf_add(&b, code);
	mm_buf_printf(&b, "\n\tfrag_color = %s;\n}\n#endif\n", value);
	return strdup(mm_buf_get(&b));
}

static mm_code_t *source_code(mm_gen_t *g, int input) {
	mm_port_t  src = mm_get_source(g, input);
	mm_code_t *c   = src.gen != NULL ? mm_get_shader_code(src.gen, "uv", src.output, mm_context_new(NULL)) : mm_code_default();
	if (c->output_type == MM_T_COUNT) {
		c = mm_code_default();
	}
	return c;
}

static int plan_gen(plan_t *s, mm_gen_t *g) {
	entry_t *e = find_entry(s, g);
	if (e != NULL) {
		return e->pass;
	}
	s->entries                      = realloc(s->entries, sizeof(entry_t) * (s->nentries + 1));
	s->entries[s->nentries].gen     = g;
	s->entries[s->nentries].pass    = -1;
	s->entries[s->nentries].loop_of = NULL;
	int ei                          = s->nentries++;

	if (g->kind == MM_G_IMAGE || g->kind == MM_G_TEXT) { // A CPU texture: no shader, the renderer uploads its pixels
		tmp_pass_t p    = {0};
		int        h    = 0;
		p.p.texture_gen = g;
		p.p.name        = strdup(mm_printf("o%d %s", g->id, g->name));
		mm_texture_size(g, &p.p.size, &h);
		int pi              = add_pass(s, &p);
		s->entries[ei].pass = pi;
		return pi;
	}

	if (g->kind == MM_G_HOIST) { // Its source at the pixel's uv, stored exactly
		mm_mark_t  mark = mm_arena_mark();
		tmp_pass_t p    = {0};
		p.p.size        = g->hoist_size;
		p.p.greyscale   = g->hoist_type == MM_T_F;
		p.p.f32         = true;
		p.p.exact       = true;
		p.p.name        = strdup(mm_printf("o%d hoisted o%d %s", g->id, g->hoist_src->id, g->hoist_src->name));
		mm_code_t *c    = mm_get_shader_code(g->hoist_src, "uv", g->hoist_output, mm_context_new(NULL));
		p.p.glsl        = compose(s, c, NULL, false, &p.tex_gen, &p.p.textures, &p.loop_of);
		mm_arena_release(mark);
		int pi                 = add_pass(s, &p);
		s->entries[ei].pass    = pi;
		s->entries[ei].loop_of = p.loop_of;
		return pi;
	}

	mm_mark_t mark = mm_arena_mark();
	mm_buffer_analyze(g);
	tmp_pass_t p  = {0};
	p.p.size      = (int)pow(2.0, mm_json_num(mm_gen_param(g, "size"), 9));
	p.p.greyscale = g->greyscale;
	p.p.name      = strdup(mm_printf("o%d %s", g->id, g->name));
	if (g->kind == MM_G_BUFFER) {
		p.p.f32  = g->version == 2 && mm_json_bool(mm_gen_param(g, "f32"), false);
		p.p.glsl = compose(s, source_code(g, 0), NULL, false, &p.tex_gen, &p.p.textures, &p.loop_of);
	}
	else {
		p.p.f32        = mm_json_bool(mm_gen_param(g, "f32"), false);
		p.p.iterate    = true;
		p.p.iterations = (int)iterations_of(g);
		p.p.shrink     = mm_json_bool(mm_gen_param(g, "shrink"), false);
		p.p.glsl       = compose(s, source_code(g, 0), g, false, &p.tex_gen, &p.p.textures, &p.loop_of);
		mm_gen_t *lo     = NULL;
		mm_gen_t *outer  = s->looping;
		s->looping       = g;
		// The loop source, recording what it requests at the pixel's uv; composed, it plans the textures it reads. Then
		// the requests found loop-invariant are answered by hoisted passes and the loop source is made again.
		mm_code_hook_t saved_hook = mm_code_hook;
		void          *saved_data = mm_code_hook_data;
		hoist_state_t  h          = {.s = s, .self = g, .size = p.p.size};
		bool           hoisting   = !p.p.shrink && getenv("MM_NO_HOIST") == NULL;
		if (hoisting) {
			mm_code_hook      = hoist_hook;
			mm_code_hook_data = &h;
		}
		mm_code_t *lsrc   = source_code(g, 1);
		mm_code_hook      = saved_hook;
		mm_code_hook_data = saved_data;
		p.p.glsl_loop     = compose(s, lsrc, g, true, &p.loop_tex_gen, &p.p.loop_textures, &lo);
		if (hoisting && hoist_decide(&h)) {
			free(p.p.glsl_loop);
			free(p.loop_tex_gen);
			lo                = NULL;
			h.substitute      = true;
			mm_code_hook      = hoist_hook;
			mm_code_hook_data = &h;
			lsrc              = source_code(g, 1);
			mm_code_hook      = saved_hook;
			mm_code_hook_data = saved_data;
			p.p.glsl_loop     = compose(s, lsrc, g, true, &p.loop_tex_gen, &p.p.loop_textures, &lo);
		}
		free(h.cands);
		s->looping       = outer;
		if (p.loop_of == NULL && lo != g) {
			p.loop_of = lo;
		}
	}
	mm_arena_release(mark);
	if (s->entries[ei].loop_of != NULL && p.loop_of == NULL) {
		p.loop_of = s->entries[ei].loop_of; // Joined an iterate buffer's loop while being planned
	}
	int pi                 = add_pass(s, &p);
	s->entries[ei].pass    = pi;
	s->entries[ei].loop_of = p.loop_of;
	return pi;
}

static int resolve(plan_t *s, mm_gen_t *g) {
	if (g == NULL) {
		return -1;
	}
	entry_t *e = find_entry(s, g);
	return e != NULL ? e->pass : -1;
}

static mm_plan_t *plan(mm_material_t *m, int size);
static void       finish(plan_t *s, mm_plan_t *rv);

mm_plan_t *mm_plan(mm_material_t *m, int size) {
	jmp_buf  guard;
	jmp_buf *outer = mm_guard;
	if (setjmp(guard) == 0) {
		mm_guard      = &guard;
		mm_plan_t *rv = plan(m, size);
		mm_guard      = outer;
		return rv;
	}
	mm_guard = outer; // A runaway graph: the arena grew past its limit (the planning state leaks, it is rare)
	mm_graph_reset();
	mm_code_hook      = NULL;
	mm_code_hook_data = NULL;
	mm_plan_t *rv = mm_alloc(sizeof(mm_plan_t));
	for (int i = 0; i < MM_MAP_COUNT; ++i) {
		rv->map_pass[i] = -1;
	}
	rv->error = "Too large";
	return rv;
}

// Material kinds Material Maker exports as shaders (dynamic, unlit) have no texture outputs: give them the standard
// material's, so they bake like the others. Dynamic has the standard inputs; unlit's colour becomes the albedo.
static void synthesize_outputs(mm_gen_t *mat) {
	mm_json_t *outs = NULL;
	if (mm_gen_input_defs(mat) != NULL && mm_gen_input_defs(mat)->count >= 8) {
		mm_json_t *std = mm_json_get(mm_library_node("material"), "shader_model");
		outs           = mm_json_get(std, "outputs");
	}
	else {
		static const char *unlit[][2] = {
		    {"rgba", "$color_tex($uv)"},
		    {"rgb", "vec3(1.0, 1.0, 0.0)"}, // ORM: no occlusion, rough, not metallic
		    {"rgb", "vec3(0.0)"},
		    {"rgb", "vec3(0.5)*vec3(-1.0, 1.0, -1.0)+vec3(1.0, 0.0, 1.0)"}, // Flat, Material Maker's convention
		    {"f", "0.0"},
		    {"f", "0.0"},
		    {"rgba", "vec4(0.0, 0.0, 0.0, 0.0)"},
		    {"rgb", "vec3(0.5)*vec3(-1.0, 1.0, -1.0)+vec3(1.0, 0.0, 1.0)"},
		    {"f", "1.0"}, // Height: 1 - depth
		};
		outs = mm_json_new(MM_JSON_ARRAY);
		for (size_t i = 0; i < sizeof(unlit) / sizeof(unlit[0]); ++i) {
			mm_json_t *o = mm_json_new(MM_JSON_OBJECT);
			mm_json_t *t = mm_json_new(MM_JSON_STRING);
			t->string    = (char *)unlit[i][0];
			mm_json_t *e = mm_json_new(MM_JSON_STRING);
			e->string    = (char *)unlit[i][1];
			mm_json_set(o, "type", t);
			mm_json_set(o, unlit[i][0], e);
			mm_json_push(outs, o);
		}
	}
	if (outs == NULL || mat->model == NULL) {
		return;
	}
	mm_json_t *model = mm_json_copy(mat->model);
	mm_json_set(model, "outputs", outs);
	mm_shader_set_model(mat, model);
}

static mm_plan_t *plan(mm_material_t *m, int size) {
	mm_plan_t *rv = mm_alloc(sizeof(mm_plan_t));
	for (int i = 0; i < MM_MAP_COUNT; ++i) {
		rv->map_pass[i] = -1;
	}
	if (m->material == NULL) {
		rv->error = m->error != NULL ? m->error : "No material";
		return rv;
	}
	mm_gen_t  *mat  = m->material;
	mm_json_t *outs = mm_gen_output_defs(mat);
	if (outs == NULL || outs->count < 9) {
		synthesize_outputs(mat);
		outs = mm_gen_output_defs(mat);
	}
	if (outs == NULL || outs->count < 9) {
		rv->error = mm_printf("Unsupported material kind %s", m->kind);
		return rv;
	}
	if (size <= 0) {
		size = m->size;
	}

	// Material Maker's Godot 4 ORM export: output and the inputs whose connection makes it exported
	static const struct {
		mm_map_t    map;
		int         output;
		const char *inputs[3];
	} maps[] = {
	    {MM_MAP_ALBEDO, 0, {"albedo_tex"}},
	    {MM_MAP_ORM, 1, {"ao_tex", "roughness_tex", "metallic_tex"}},
	    {MM_MAP_EMISSION, 2, {"emission_tex"}},
	    {MM_MAP_NORMAL, 3, {"normal_tex"}},
	    {MM_MAP_HEIGHT, 8, {"depth_tex"}},
	    {MM_MAP_SSS, 5, {"sss_tex"}},
	};
	mm_json_t *ins = mm_gen_input_defs(mat);
	plan_t     s   = {0};
	for (size_t k = 0; k < sizeof(maps) / sizeof(maps[0]); ++k) {
		bool connected = false;
		for (int j = 0; j < 3 && maps[k].inputs[j] != NULL; ++j) {
			for (int i = 0; ins != NULL && i < ins->count; ++i) {
				if (strcmp(mm_json_str(mm_json_get(ins->items[i], "name"), ""), maps[k].inputs[j]) == 0 && mm_get_source(mat, i).gen != NULL) {
					connected = true;
				}
			}
		}
		rv->map_connected[maps[k].map] = connected;

		mm_mark_t  mark = mm_arena_mark();
		mm_code_t *c    = mm_get_shader_code(mat, "uv", maps[k].output, mm_context_new(NULL));
		tmp_pass_t p    = {0};
		mm_gen_t  *lo   = NULL;
		p.p.size        = 1 << size;
		p.p.greyscale   = c->output_type == MM_T_F;
		p.p.name        = strdup(mm_printf("map %d", maps[k].output));
		p.p.glsl        = compose(&s, c, NULL, false, &p.tex_gen, &p.p.textures, &lo);
		mm_arena_release(mark);
		rv->map_pass[maps[k].map] = add_pass(&s, &p);
	}
	finish(&s, rv);
	return rv;
}

// The planning state into the arena, freed
static void finish(plan_t *sp, mm_plan_t *rv) {
	plan_t s   = *sp;
	rv->count  = s.npasses;
	rv->passes = mm_alloc(sizeof(mm_pass_t) * (s.npasses + 1));
	for (int i = 0; i < s.npasses; ++i) {
		tmp_pass_t *t   = &s.passes[i];
		mm_pass_t  *p   = &rv->passes[i];
		*p              = t->p;
		p->glsl         = mm_strdup(t->p.glsl);
		p->glsl_loop    = t->p.glsl_loop != NULL ? mm_strdup(t->p.glsl_loop) : NULL;
		p->name         = mm_strdup(t->p.name);
		p->loop_of      = resolve(&s, t->loop_of);
		p->texture_pass = mm_alloc(sizeof(int) * (p->textures + 1));
		for (int k = 0; k < p->textures; ++k) {
			p->texture_pass[k] = resolve(&s, t->tex_gen[k]);
		}
		p->loop_texture_pass = mm_alloc(sizeof(int) * (p->loop_textures + 1));
		for (int k = 0; k < p->loop_textures; ++k) {
			p->loop_texture_pass[k] = resolve(&s, t->loop_tex_gen[k]);
		}
	}
	rv->error = s.error != NULL ? mm_strdup(s.error) : NULL;
	for (int i = 0; i < s.npasses; ++i) {
		free(s.passes[i].p.glsl);
		free(s.passes[i].p.glsl_loop);
		free(s.passes[i].p.name);
		free(s.passes[i].tex_gen);
		free(s.passes[i].loop_tex_gen);
	}
	free(s.passes);
	free(s.entries);
	free(s.error);
	for (int i = 0; i < s.nhoists; ++i) {
		free(s.hoists[i]->name);
		free(s.hoists[i]);
	}
	free(s.hoists);
}

// Embedding: the passes of the buffers code emitted into the app's shaders samples (mm_emit_t buffers). pass[i] is the
// pass whose texture buffer_gens[i] / outputs[i] is. Our nodes (native generators) can not run in a pass on their own:
// a buffer they feed fails the plan.
bool mm_planning_standalone = false;
bool mm_planning_native_hit = false;

static mm_plan_t *plan_buffers(mm_gen_t **gens, int n, int *pass) {
	mm_plan_t *rv = mm_alloc(sizeof(mm_plan_t));
	for (int i = 0; i < MM_MAP_COUNT; ++i) {
		rv->map_pass[i] = -1;
	}
	plan_t s = {0};
	for (int i = 0; i < n; ++i) {
		if (gens[i] != NULL && (gens[i]->kind == MM_G_BUFFER || gens[i]->kind == MM_G_ITERATE || gens[i]->kind == MM_G_IMAGE || gens[i]->kind == MM_G_TEXT)) {
			plan_gen(&s, gens[i]);
		}
	}
	for (int i = 0; i < n; ++i) {
		pass[i] = resolve(&s, gens[i]);
	}
	finish(&s, rv);
	return rv;
}

mm_plan_t *mm_plan_buffers(mm_gen_t **gens, int n, int *pass) {
	jmp_buf  guard;
	jmp_buf *outer         = mm_guard;
	mm_planning_standalone = true;
	mm_planning_native_hit = false;
	mm_plan_t *rv          = NULL;
	if (setjmp(guard) == 0) {
		mm_guard = &guard;
		rv       = plan_buffers(gens, n, pass);
	}
	else {
		mm_graph_reset();
		mm_code_hook      = NULL;
		mm_code_hook_data = NULL;
		rv = mm_alloc(sizeof(mm_plan_t));
		for (int i = 0; i < MM_MAP_COUNT; ++i) {
			rv->map_pass[i] = -1;
		}
		rv->error = "Too large";
	}
	mm_guard               = outer;
	mm_planning_standalone = false;
	if (mm_planning_native_hit && rv->error == NULL) {
		rv->error = "A buffer is fed by one of our nodes"; // TODO: render those in the material's context
	}
	return rv;
}
