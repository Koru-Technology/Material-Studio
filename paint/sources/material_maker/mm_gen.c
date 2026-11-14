// Material Maker's shader generation (gen_base.gd, gen_shader.gd, gen_context.gd, the io types and the gradient / curve
// types), ported to C. Names, formats and the order of substitutions follow Material Maker so the GLSL is the same.

#include "mm_internal.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Io types, in Material Maker's order (the order outputs are looked up in)

const mm_type_info_t mm_types[MM_T_COUNT] = {
    {"f", "float", "vec2 uv", "uv", 0, 2, {{MM_T_RGB, "vec3($(value))"}, {MM_T_RGBA, "vec4(vec3($(value)), 1.0)"}}},
    {"rgb", "vec3", "vec2 uv", "uv", 0, 2, {{MM_T_F, "(dot($(value), vec3(1.0))/3.0)"}, {MM_T_RGBA, "vec4($(value), 1.0)"}}},
    {"rgba", "vec4", "vec2 uv", "uv", 0, 2, {{MM_T_F, "(dot(($(value)).rgb, vec3(1.0))/3.0)"}, {MM_T_RGB, "(($(value)).rgb)"}}},
    {"sdf2d", "float", "vec2 uv", "uv", 1, 0, {{0}}},
    {"sdf3d", "float", "vec3 p", "p", 2, 1, {{MM_T_SDF3DC, "vec2($(value), 0.0)"}}},
    {"sdf3dc", "vec2", "vec3 p", "p", 2, 1, {{MM_T_SDF3D, "($(value)).x"}}},
    {"tex3d_gs", "float", "vec4 p", "p", 3, 1, {{MM_T_TEX3D, "vec3($(value))"}}},
    {"tex3d", "vec3", "vec4 p", "p", 3, 1, {{MM_T_TEX3D_GS, "(dot($(value), vec3(1.0))/3.0)"}}},
    {"v4v4", "vec4", "vec4 p", "p", 4, 0, {{0}}},
    {"fill", "vec4", "vec2 uv", "uv", 5, 0, {{0}}},
    {"any", "", "", "", 42, 0, {{0}}},
};

mm_type_t mm_type_from_name(const char *name) {
	for (int i = 0; i < MM_T_COUNT; ++i) {
		if (name != NULL && strcmp(mm_types[i].name, name) == 0) {
			return (mm_type_t)i;
		}
	}
	return MM_T_COUNT;
}

// Shader code

mm_code_t *mm_code_new(void) {
	mm_code_t *c   = mm_alloc(sizeof(mm_code_t));
	c->output_type = MM_T_COUNT;
	return c;
}

mm_code_t *mm_code_default(void) {
	mm_code_t *c                 = mm_code_new();
	c->output_type               = MM_T_RGBA;
	c->output_values[MM_T_F]     = "0.0";
	c->output_values[MM_T_RGB]   = "vec3(0.0)";
	c->output_values[MM_T_RGBA]  = "vec4(0.0, 0.0, 0.0, 0.0)";
	return c;
}

static void add_global_shared(mm_code_t *c, char *code, char *source) {
	if (c->nglobals == c->cap_globals) {
		c->cap_globals = c->cap_globals == 0 ? 8 : c->cap_globals * 2;
		mm_global_t *g = mm_alloc(sizeof(mm_global_t) * c->cap_globals);
		if (c->nglobals > 0) {
			memcpy(g, c->globals, sizeof(mm_global_t) * c->nglobals);
		}
		c->globals = g;
	}
	c->globals[c->nglobals].code   = code;
	c->globals[c->nglobals].source = source;
	c->nglobals++;
}

void mm_code_add_global(mm_code_t *c, const char *code, const char *source) {
	for (int i = 0; i < c->nglobals; ++i) {
		if (strcmp(c->globals[i].code, code) == 0) {
			return;
		}
	}
	add_global_shared(c, mm_strdup(code), mm_strdup(source));
}

void mm_code_add_globals(mm_code_t *c, mm_code_t *from) {
	for (int i = 0; i < from->nglobals; ++i) {
		const char *code = from->globals[i].code;
		bool        dup  = false;
		for (int k = 0; k < c->nglobals && !dup; ++k) {
			dup = c->globals[k].code == code || strcmp(c->globals[k].code, code) == 0;
		}
		if (!dup) {
			add_global_shared(c, from->globals[i].code, from->globals[i].source); // Arena strings never change, share them
		}
	}
}

static mm_uniform_t *find_uniform(mm_code_t *c, const char *name) {
	for (int i = 0; i < c->nuniforms; ++i) {
		if (c->uniforms[i].name == name || strcmp(c->uniforms[i].name, name) == 0) {
			return &c->uniforms[i];
		}
	}
	return NULL;
}

static mm_uniform_t *new_uniform(mm_code_t *c) {
	if (c->nuniforms == c->cap_uniforms) {
		c->cap_uniforms = c->cap_uniforms == 0 ? 8 : c->cap_uniforms * 2;
		mm_uniform_t *u = mm_alloc(sizeof(mm_uniform_t) * c->cap_uniforms);
		if (c->nuniforms > 0) {
			memcpy(u, c->uniforms, sizeof(mm_uniform_t) * c->nuniforms);
		}
		c->uniforms = u;
	}
	return &c->uniforms[c->nuniforms++];
}

mm_uniform_t *mm_code_add_uniform(mm_code_t *c, const char *name, mm_utype_t type, int size, const double *values, int nvalues) {
	mm_uniform_t *e = find_uniform(c, name);
	if (e != NULL) {
		return e;
	}
	mm_uniform_t *n = new_uniform(c);
	n->name         = mm_strdup(name);
	n->type         = type;
	n->size         = size;
	if (nvalues > 0) {
		n->values = mm_alloc(sizeof(double) * nvalues);
		memcpy(n->values, values, sizeof(double) * nvalues);
	}
	return n;
}

void mm_code_add_uniforms(mm_code_t *c, mm_code_t *from) {
	for (int i = 0; i < from->nuniforms; ++i) {
		mm_uniform_t *f = &from->uniforms[i];
		mm_uniform_t *u = find_uniform(c, f->name);
		if (u == NULL) {
			u  = new_uniform(c);
			*u = *f; // Name and values are arena data that never change, shared
		}
		u->texture        = f->texture;
		u->texture_output = f->texture_output;
	}
}

char *mm_uniform_declaration(mm_uniform_t *u) {
	const char *t = u->type == MM_U_FLOAT ? "float" : u->type == MM_U_INT ? "int" : u->type == MM_U_VEC2 ? "vec2" : "vec4";
	if (u->type == MM_U_SAMPLER) {
		return mm_strdup(""); // Bound textures, declared by the pass
	}
	int      n = u->type == MM_U_VEC4 ? 4 : u->type == MM_U_VEC2 ? 2 : 1;
	mm_buf_t b = {0};
	if (u->size > 0) {
		mm_buf_printf(&b, "const %s %s[%d] = %s[](", t, u->name, u->size, t);
		for (int i = 0; i < u->size; ++i) {
			if (i > 0) {
				mm_buf_add(&b, ", ");
			}
			if (u->type == MM_U_INT) {
				mm_buf_printf(&b, "%d", (int)u->values[i]);
			}
			else if (n == 1) {
				mm_buf_printf(&b, "%.9f", u->values[i]);
			}
			else {
				mm_buf_printf(&b, "%s(", t);
				for (int k = 0; k < n; ++k) {
					mm_buf_printf(&b, k > 0 ? ", %.9f" : "%.9f", u->values[i * n + k]);
				}
				mm_buf_add(&b, ")");
			}
		}
		mm_buf_add(&b, ");\n");
		return mm_buf_get(&b);
	}
	double *v = u->values;
	double  z[4] = {0, 0, 0, 0};
	if (v == NULL) {
		v = z;
	}
	if (u->type == MM_U_INT) {
		return mm_printf("const int %s = %d;\n", u->name, (int)v[0]);
	}
	if (u->type == MM_U_FLOAT) {
		return mm_printf("const float %s = %.9f;\n", u->name, v[0]);
	}
	if (u->type == MM_U_VEC2) {
		return mm_printf("const vec2 %s = vec2(%.9f, %.9f);\n", u->name, v[0], v[1]);
	}
	return mm_printf("const vec4 %s = vec4(%.9f, %.9f, %.9f, %.9f);\n", u->name, v[0], v[1], v[2], v[3]);
}

// Material Maker passes parameters as uniforms: values the shader compiler knows nothing about. As constants, it folds
// expressions of them at compile time with other results than the GPU's at run time (hashes of the seed:
// fract(cos(...) * 43758.5) give another noise). Arithmetic on a run time value does not hide them either: with fast
// math 0.0*one folds to 0.0, and 1.0-(1.0+zero) becomes -zero, -0.0 where Material Maker has +0.0 (x/(1.0-contrast) at
// contrast 1 then gives -inf, clamped to 0 instead of 1). Their bits plus a run time 0 make them as opaque as uniforms,
// with the exact value of Material Maker's float uniforms. Arrays are only indexed and compared.
static char *opaque_float(double v, const char *zero) {
	float    f = (float)v;
	uint32_t bits;
	memcpy(&bits, &f, sizeof(bits));
	return mm_printf("uintBitsToFloat(%uu+floatBitsToUint(%s))", bits, zero);
}

char *mm_uniform_runtime_declaration(mm_uniform_t *u, const char *zero) {
	if (u->type == MM_U_SAMPLER || u->size > 0) {
		return mm_uniform_declaration(u);
	}
	double *v    = u->values;
	double  z[4] = {0, 0, 0, 0};
	if (v == NULL) {
		v = z;
	}
	switch (u->type) {
	case MM_U_INT:
		return mm_printf("#define %s (%d+int(floatBitsToUint(%s)))\n", u->name, (int)v[0], zero);
	case MM_U_FLOAT:
		return mm_printf("#define %s %s\n", u->name, opaque_float(v[0], zero));
	case MM_U_VEC2:
		return mm_printf("#define %s vec2(%s, %s)\n", u->name, opaque_float(v[0], zero), opaque_float(v[1], zero));
	default:
		return mm_printf("#define %s vec4(%s, %s, %s, %s)\n", u->name, opaque_float(v[0], zero), opaque_float(v[1], zero), opaque_float(v[2], zero),
		                 opaque_float(v[3], zero));
	}
}

// GLSL utilities (gen_base.gd)

static int find_matching_parenthesis(const char *s, int i, char op, char cp) {
	int level = 0;
	int len   = (int)strlen(s);
	while (i < len) {
		char c = s[i];
		if (c == op) {
			level++;
		}
		else if (c == cp) {
			level--;
			if (level == 0) {
				return i;
			}
		}
		i++;
		const char *no     = strchr(s + i, op);
		const char *nc     = strchr(s + i, cp);
		int         next_o = no != NULL ? (int)(no - s) : -1;
		int         next_c = nc != NULL ? (int)(nc - s) : -1;
		int         max_p  = next_o > next_c ? next_o : next_c;
		if (max_p < 0) {
			return -1;
		}
		int min_p = next_o < next_c ? next_o : next_c;
		i         = min_p < 0 ? max_p : min_p;
	}
	return i;
}

static char *remove_comments(const char *s) {
	char *r = mm_strdup(s);
	while (true) {
		char *b = strstr(r, "/*");
		if (b == NULL) {
			break;
		}
		char *e = strstr(b, "*/");
		if (e == NULL) {
			break;
		}
		memmove(b, e + 2, strlen(e + 2) + 1);
	}
	mm_buf_t out = {0};
	char    *p   = r;
	while (*p) {
		char *c = strstr(p, "//");
		if (c == NULL) {
			mm_buf_add(&out, p);
			break;
		}
		mm_buf_addn(&out, p, c - p);
		char *nl = strchr(c, '\n');
		if (nl == NULL) {
			break;
		}
		p = nl; // Keep the newline
	}
	return mm_buf_get(&out);
}

static void strs_push(mm_strs_t *a, char *s) {
	char **items = mm_alloc(sizeof(char *) * (a->count + 1));
	if (a->count > 0) {
		memcpy(items, a->items, sizeof(char *) * a->count);
	}
	items[a->count++] = s;
	a->items          = items;
}

#define split_glsl mm_split_glsl
#define glsl_declaration_name mm_glsl_declaration_name

mm_strs_t split_glsl(const char *src) {
	mm_strs_t a = {0};
	char     *s = mm_strip(remove_comments(src));
	while (*s) {
		char *semi = strchr(s, ';');
		char *br   = strchr(s, '{');
		if (semi != NULL && (br == NULL || semi < br)) {
			strs_push(&a, mm_strndup(s, semi - s + 1));
			s = semi + 1;
		}
		else if (br != NULL) {
			int   close = find_matching_parenthesis(s, (int)(br - s), '{', '}');
			if (close < 0) {
				break;
			}
			char *d = mm_strndup(s, close + 1);
			s       = mm_strip(s + close + 1);
			if (s[0] == ';') {
				d = mm_printf("%s;", d);
				s++;
			}
			strs_push(&a, d);
		}
		else {
			break; // Material Maker prints an error and stops
		}
		s = mm_strip(s);
	}
	return a;
}

char *glsl_declaration_name(const char *s) {
	// words = s.split(" ", false)
	mm_strs_t words = {0};
	{
		const char *p = s;
		while (*p) {
			while (*p == ' ') {
				p++;
			}
			if (!*p) {
				break;
			}
			const char *e = p;
			while (*e && *e != ' ') {
				e++;
			}
			strs_push(&words, mm_strndup(p, e - p));
			p = e;
		}
	}
	if (words.count > 2 && (strcmp(words.items[0], "const") == 0 || strcmp(words.items[0], "uniform") == 0 || strcmp(words.items[0], "varying") == 0)) {
		char *d  = words.items[2];
		char *br = strchr(d, '[');
		return br != NULL ? mm_strndup(d, br - d) : d;
	}
	// split = s.split(" ") (empty parts kept), then split[1].split("(")
	const char *sp = strchr(s, ' ');
	if (sp != NULL) {
		const char *second = sp + 1;
		const char *end    = strchr(second, ' ');
		char       *w      = end != NULL ? mm_strndup(second, end - second) : mm_strdup(second);
		char       *paren  = strchr(w, '(');
		if (paren != NULL) {
			return mm_strndup(w, paren - w);
		}
	}
	return mm_strdup("");
}

static char *fix_int_seed_parameters(char *s);

// A function declaration's text before its body, without comments and whitespace: "" for other declarations
static char *decl_signature(const char *d) {
	const char *brace = strchr(d, '{');
	if (brace == NULL || strchr(d, '(') == NULL || strchr(d, '(') > brace) {
		return "";
	}
	mm_buf_t b = {0};
	for (const char *p = d; p < brace; ++p) {
		if (p[0] == '/' && p[1] == '/') {
			while (p < brace && *p != '\n') {
				p++;
			}
			continue;
		}
		if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
			mm_buf_add(&b, mm_printf("%c", *p));
		}
	}
	return mm_buf_get(&b);
}

char *mm_code_globals_string(mm_code_t *c, const char *code) {
	mm_buf_t rv = {0};
	char    *r  = mm_strdup("");
	// Signatures of every declaration, for the int seed fix (fix_int_seed_parameters)
	mm_strs_t sigs = {0};
	for (int gi = 0; gi < c->nglobals; ++gi) {
		mm_strs_t decls = split_glsl(c->globals[gi].code);
		for (int di = 0; di < decls.count; ++di) {
			strs_push(&sigs, decl_signature(decls.items[di]));
		}
	}
	for (int gi = c->nglobals - 1; gi >= 0; --gi) {
		bool      added = false;
		mm_strs_t decls = split_glsl(c->globals[gi].code);
		for (int di = decls.count - 1; di >= 0; --di) {
			char *d    = decls.items[di];
			char *name = glsl_declaration_name(d);
			char *fixed = fix_int_seed_parameters(d);
			if (fixed != d) {
				char *sig   = decl_signature(fixed);
				bool  taken = false;
				for (int k = 0; k < sigs.count && !taken; ++k) {
					taken = sig[0] != 0 && strcmp(sigs.items[k], sig) == 0;
				}
				if (!taken) {
					d = fixed;
					strs_push(&sigs, sig); // Another old copy of it keeps its int seed (the calls resolve to this one)
				}
			}
			if (code[0] == 0 || name[0] == 0 || strstr(code, name) != NULL || strstr(r, name) != NULL) {
				r     = mm_printf("\n// '%s'\n%s\n%s", name, d, r);
				added = true;
			}
		}
		if (added) {
			r = mm_printf("// #globals: %s\n%s", c->globals[gi].source, r);
		}
	}
	(void)rv;
	return r;
}

// Context

typedef struct {
	mm_gen_t *gen;
	mm_strs_t variants;
} mm_variants_t;

struct mm_context {
	mm_variants_t *items;
	int            count;
	mm_context_t  *parent;
};

mm_context_t *mm_context_new(mm_context_t *parent) {
	mm_context_t *c = mm_alloc(sizeof(mm_context_t));
	c->parent       = parent;
	return c;
}

static mm_variants_t *context_find(mm_context_t *c, mm_gen_t *g) {
	for (int i = 0; i < c->count; ++i) {
		if (c->items[i].gen == g) {
			return &c->items[i];
		}
	}
	return NULL;
}

static mm_variants_t *context_add(mm_context_t *c, mm_gen_t *g) {
	mm_variants_t *items = mm_alloc(sizeof(mm_variants_t) * (c->count + 1));
	if (c->count > 0) {
		memcpy(items, c->items, sizeof(mm_variants_t) * c->count);
	}
	items[c->count].gen = g;
	c->items            = items;
	return &c->items[c->count++];
}

bool mm_context_has_parent(mm_context_t *c) {
	return c->parent != NULL;
}

mm_context_t *mm_context_parent(mm_context_t *c) {
	return c->parent;
}

// A snapshot of the variants (strings shared, they never change), same parent
mm_context_t *mm_context_copy(mm_context_t *c) {
	mm_context_t *r = mm_alloc(sizeof(mm_context_t));
	r->parent       = c->parent;
	r->count        = c->count;
	r->items        = mm_alloc(sizeof(mm_variants_t) * (c->count + 1));
	for (int i = 0; i < c->count; ++i) {
		r->items[i].gen            = c->items[i].gen;
		r->items[i].variants.count = c->items[i].variants.count;
		r->items[i].variants.items = mm_alloc(sizeof(char *) * (c->items[i].variants.count + 1));
		if (c->items[i].variants.count > 0) {
			memcpy(r->items[i].variants.items, c->items[i].variants.items, sizeof(char *) * c->items[i].variants.count);
		}
	}
	return r;
}

bool mm_context_has_variant(mm_context_t *c, mm_gen_t *g) {
	return context_find(c, g) != NULL || (c->parent != NULL && mm_context_has_variant(c->parent, g));
}

static void context_touch(mm_context_t *c, mm_gen_t *g) {
	if (context_find(c, g) == NULL) {
		context_add(c, g);
	}
	if (c->parent != NULL) {
		context_touch(c->parent, g);
	}
}

int mm_context_get_variant(mm_context_t *c, mm_gen_t *g, const char *variant) {
	int            rv = -1;
	mm_variants_t *v  = context_find(c, g);
	if (v != NULL) {
		for (int i = 0; i < v->variants.count; ++i) {
			if (strcmp(v->variants.items[i], variant) == 0) {
				rv = i;
				break;
			}
		}
		if (rv == -1) {
			strs_push(&v->variants, mm_strdup(variant));
		}
	}
	else {
		v = context_add(c, g);
		strs_push(&v->variants, mm_strdup(variant));
	}
	context_touch(c, g);
	return rv;
}

// Gradient and curve parameter types (types/gradient.gd, types/curve.gd)

typedef struct {
	double v;
	double c[4];
} grad_point_t;

static int grad_cmp(const void *a, const void *b) {
	double d = ((const grad_point_t *)a)->v - ((const grad_point_t *)b)->v;
	return d < 0 ? -1 : d > 0 ? 1 : 0;
}

char *mm_type_gradient(mm_code_t *c, mm_json_t *value, const char *name, bool declare) {
	int           interpolation = 1;
	mm_json_t    *pts           = NULL;
	if (value != NULL && value->type == MM_JSON_ARRAY) {
		pts = value;
	}
	else if (value != NULL && value->type == MM_JSON_OBJECT) {
		pts           = mm_json_get(value, "points");
		mm_json_t *it = mm_json_get(value, "interpolation");
		interpolation = it != NULL ? (int)mm_json_num(it, 1) : 1;
	}
	int           n = pts != NULL ? pts->count : 0;
	grad_point_t *p = mm_alloc(sizeof(grad_point_t) * (n > 0 ? n : 1));
	for (int i = 0; i < n; ++i) {
		mm_json_t *q = pts->items[i];
		p[i].v       = mm_json_num(mm_json_get(q, "pos"), 0);
		p[i].c[0]    = mm_json_num(mm_json_get(q, "r"), 0);
		p[i].c[1]    = mm_json_num(mm_json_get(q, "g"), 0);
		p[i].c[2]    = mm_json_num(mm_json_get(q, "b"), 0);
		p[i].c[3]    = mm_json_num(mm_json_get(q, "a"), 1);
	}
	if (interpolation == 0 && value != NULL && value->type == MM_JSON_OBJECT) { // Constant: drop the doubled points
		for (int i = n - 1; i > 0; --i) {
			if (memcmp(p[i].c, p[i - 1].c, sizeof(double) * 4) == 0) {
				memmove(&p[i], &p[i + 1], sizeof(grad_point_t) * (n - i - 1));
				n--;
			}
			else {
				p[i].v = 0.5 * (p[i - 1].v + p[i].v);
			}
		}
		if (n > 0) {
			p[0].v = 0;
		}
	}
	qsort(p, n, sizeof(grad_point_t), grad_cmp); // sort(): stable enough for distinct positions
	for (int i = 0; i + 1 < n; ++i) {
		if (p[i].v + 0.0000005 >= p[i + 1].v) {
			p[i + 1].v = p[i].v + 0.000001;
		}
	}
	if (declare) {
		double *pos = mm_alloc(sizeof(double) * (n > 0 ? n : 1));
		double *col = mm_alloc(sizeof(double) * 4 * (n > 0 ? n : 1));
		for (int i = 0; i < n; ++i) {
			pos[i] = p[i].v;
			memcpy(col + i * 4, p[i].c, sizeof(double) * 4);
		}
		mm_code_add_uniform(c, mm_printf("p_%s_pos", name), MM_U_FLOAT, n, pos, n);
		mm_code_add_uniform(c, mm_printf("p_%s_col", name), MM_U_VEC4, n, col, n * 4);

		mm_buf_t s = {0};
#define PV(i) mm_printf("p_%s_pos[%d]", name, (i))
#define PC(i) mm_printf("p_%s_col[%d]", name, (i))
		mm_buf_printf(&s, "vec4 %s_gradient_fct(float x) {\n", name);
		if (n == 0) {
			mm_buf_add(&s, "  return vec4(0.0, 0.0, 0.0, 1.0);\n");
		}
		else if (interpolation == 0) {
			mm_buf_printf(&s, "  if (x < %s) {\n", PV(1));
			mm_buf_printf(&s, "    return %s;\n", PC(0));
			int last = n - 1;
			for (int i = 1; i < last; ++i) {
				mm_buf_printf(&s, "  } else if (x < %s) {\n", PV(i + 1));
				mm_buf_printf(&s, "    return %s;\n", PC(i));
			}
			mm_buf_add(&s, "  }\n");
			mm_buf_printf(&s, "  return %s;\n", PC(last));
		}
		else if (interpolation == 1 || interpolation == 2) {
			mm_buf_printf(&s, "  if (x < %s) {\n", PV(0));
			mm_buf_printf(&s, "    return %s;\n", PC(0));
			int last = n - 1;
			for (int i = 0; i < last; ++i) {
				mm_buf_printf(&s, "  } else if (x < %s) {\n", PV(i + 1));
				const char *f = interpolation == 1 ? "(" : "0.5-0.5*cos(3.14159265359*";
				mm_buf_printf(&s, "    return mix(%s, %s, %s(x-%s)/(%s-%s)));\n", PC(i), PC(i + 1), f, PV(i), PV(i + 1), PV(i));
			}
			mm_buf_add(&s, "  }\n");
			mm_buf_printf(&s, "  return %s;\n", PC(last));
		}
		else { // Cubic
			mm_buf_printf(&s, "  if (x < %s) {\n", PV(0));
			mm_buf_printf(&s, "    return %s;\n", PC(0));
			int last = n - 1;
			for (int i = 0; i < last; ++i) {
				mm_buf_printf(&s, "  } else if (x < %s) {\n", PV(i + 1));
				char *dx = mm_printf("(x-%s)/(%s-%s)", PV(i), PV(i + 1), PV(i));
				char *b  = mm_printf("mix(%s, %s, %s)", PC(i), PC(i + 1), dx);
				if (i > 0) {
					char *a = mm_printf("mix(%s, %s, (x-%s)/(%s-%s))", PC(i - 1), PC(i), PV(i - 1), PV(i), PV(i - 1));
					if (i < last - 1) {
						char *cc = mm_printf("mix(%s, %s, (x-%s)/(%s-%s))", PC(i + 1), PC(i + 2), PV(i + 1), PV(i + 2), PV(i + 1));
						char *ac = mm_printf("mix(%s, %s, 0.5-0.5*cos(3.14159265359*%s))", a, cc, dx);
						mm_buf_printf(&s, "    return 0.5*(%s + %s);\n", b, ac);
					}
					else {
						mm_buf_printf(&s, "    return mix(%s, %s, 0.5+0.5*%s);\n", a, b, dx);
					}
				}
				else if (i < last - 1) {
					char *cc = mm_printf("mix(%s, %s, (x-%s)/(%s-%s))", PC(i + 1), PC(i + 2), PV(i + 1), PV(i + 2), PV(i + 1));
					mm_buf_printf(&s, "    return mix(%s, %s, 1.0-0.5*%s);\n", cc, b, dx);
				}
				else {
					mm_buf_printf(&s, "    return %s;\n", b);
				}
			}
			mm_buf_add(&s, "  }\n");
			mm_buf_printf(&s, "  return %s;\n", PC(last));
		}
#undef PV
#undef PC
		mm_buf_add(&s, "}\n");
		mm_buf_add(&c->defs, mm_buf_get(&s));
	}
	return mm_printf("%s_gradient_fct", name);
}

char *mm_type_curve(mm_code_t *c, mm_json_t *value, const char *name, bool declare) {
	mm_json_t *pts = mm_json_get(value, "points");
	int        n   = pts != NULL ? pts->count : 0;
	if (declare) {
		for (int i = 0; i < n; ++i) {
			mm_json_t *q  = pts->items[i];
			double     x  = mm_json_num(mm_json_get(q, "x"), 0);
			double     y  = mm_json_num(mm_json_get(q, "y"), 0);
			double     ls = mm_json_num(mm_json_get(q, "ls"), 0);
			double     rs = mm_json_num(mm_json_get(q, "rs"), 0);
			mm_code_add_uniform(c, mm_printf("p_%s_%d_x", name, i), MM_U_FLOAT, 0, &x, 1);
			mm_code_add_uniform(c, mm_printf("p_%s_%d_y", name, i), MM_U_FLOAT, 0, &y, 1);
			mm_code_add_uniform(c, mm_printf("p_%s_%d_ls", name, i), MM_U_FLOAT, 0, &ls, 1);
			mm_code_add_uniform(c, mm_printf("p_%s_%d_rs", name, i), MM_U_FLOAT, 0, &rs, 1);
		}
		mm_buf_t s = {0};
		mm_buf_printf(&s, "float %s_curve_fct(float x) {\n", name);
		for (int i = 0; i < n - 1; ++i) {
			if (i < n - 2) {
				mm_buf_printf(&s, "if (x <= p_%s_%d_x) ", name, i + 1);
			}
			mm_buf_add(&s, "{\n");
			mm_buf_printf(&s, "float dx = x - p_%s_%d_x;\n", name, i);
			mm_buf_printf(&s, "float d = p_%s_%d_x - p_%s_%d_x;\n", name, i + 1, name, i);
			mm_buf_add(&s, "float t = dx/d;\nfloat omt = (1.0 - t);\nfloat omt2 = omt * omt;\nfloat omt3 = omt2 * omt;\nfloat t2 = t * t;\n"
			               "float t3 = t2 * t;\nd /= 3.0;\n");
			mm_buf_printf(&s, "float y1 = p_%s_%d_y;\n", name, i);
			mm_buf_printf(&s, "float yac = p_%s_%d_y + d*p_%s_%d_rs;\n", name, i, name, i);
			mm_buf_printf(&s, "float ybc = p_%s_%d_y - d*p_%s_%d_ls;\n", name, i + 1, name, i + 1);
			mm_buf_printf(&s, "float y2 = p_%s_%d_y;\n", name, i + 1);
			mm_buf_add(&s, "return y1*omt3 + yac*omt2*t*3.0 + ybc*omt*t2*3.0 + y2*t3;\n}\n");
		}
		mm_buf_add(&s, "}\n");
		mm_buf_add(&c->defs, mm_buf_get(&s));
	}
	return mm_printf("%s_curve_fct", name);
}

// Shader model preprocessing (gen_shader.gd)

static bool is_word(char c) {
	return isalnum((unsigned char)c) || c == '_';
}

static bool is_word_letter(char c) { // WORD_LETTERS, with '?'
	return isalnum((unsigned char)c) || c == '_' || c == '?';
}

// Regex (\w+)\s+(\w*\$(?:\(name\)\w*|name))\s*\((.*)\)\s*{ : instance functions, with the variation parameters added
static char *find_instance_functions(const char *code, mm_strs_t *functions) {
	mm_strs_t   matches = {0}, replacements = {0};
	const char *s       = code;
	const char *p       = code;
	while ((p = strchr(p, '$')) != NULL) {
		const char *name_end = NULL;
		if (strncmp(p, "$(name)", 7) == 0) {
			name_end = p + 7;
			while (is_word(*name_end)) {
				name_end++;
			}
		}
		else if (strncmp(p, "$name", 5) == 0 && !is_word(p[5])) {
			name_end = p + 5;
		}
		if (name_end == NULL) {
			p++;
			continue;
		}
		// Function name: \w* before the $
		const char *fn = p;
		while (fn > s && is_word(fn[-1])) {
			fn--;
		}
		// \s+ then the type (\w+) before it
		const char *ws = fn;
		while (ws > s && isspace((unsigned char)ws[-1])) {
			ws--;
		}
		if (ws == fn || ws == s || !is_word(ws[-1])) {
			p = name_end;
			continue;
		}
		const char *ty = ws;
		while (ty > s && is_word(ty[-1])) {
			ty--;
		}
		// \s*\( then (.*) to the last ) of the line followed by \s*{
		const char *q = name_end;
		while (isspace((unsigned char)*q)) {
			q++;
		}
		if (*q != '(') {
			p = name_end;
			continue;
		}
		const char *line_end = strchr(q, '\n');
		if (line_end == NULL) {
			line_end = q + strlen(q);
		}
		const char *close = NULL;
		for (const char *r = line_end - 1; r > q; --r) {
			if (*r == ')') {
				const char *after = r + 1;
				while (isspace((unsigned char)*after)) {
					after++;
				}
				if (*after == '{') {
					close = r;
					break;
				}
			}
		}
		if (close == NULL) {
			p = name_end;
			continue;
		}
		const char *brace = close + 1;
		while (*brace != '{') {
			brace++;
		}
		char *type  = mm_strndup(ty, ws - ty);
		char *fname = mm_strndup(fn, name_end - fn);
		char *args  = mm_strndup(q + 1, close - q - 1);
		if (strcmp(type, "return") != 0) {
			strs_push(functions, fname);
			strs_push(&matches, mm_strndup(ty, brace - ty + 1));
			strs_push(&replacements, mm_printf("%s %s(%s, float _seed_variation_, vec4 _controlled_variation_) {", type, fname, args));
		}
		p = brace + 1;
	}
	char *r = mm_strdup(code);
	for (int i = 0; i < matches.count; ++i) {
		r = mm_replace(r, matches.items[i], replacements.items[i]);
	}
	return r;
}

static int find_ci(const char *s, const char *what, int from) {
	int n = (int)strlen(what), len = (int)strlen(s);
	for (int i = from; i + n <= len; ++i) {
		if (strncasecmp(s + i, what, n) == 0) {
			return i;
		}
	}
	return -1;
}

static char *fix_instance_functions(const char *code, mm_strs_t *functions) {
	const char *vp = ", _seed_variation_, _controlled_variation_";
	char       *r  = mm_strdup(code);
	for (int fi = 0; fi < functions->count; ++fi) {
		const char *f        = functions->items[fi];
		int         location = 0;
		while (true) {
			location = find_ci(r, f, location);
			if (location == -1) {
				break;
			}
			if (location > 0 && is_word(r[location - 1])) {
				location += (int)strlen(f);
				continue;
			}
			int p = location + (int)strlen(f);
			while (true) {
				char ch = r[p];
				if (ch == '(') {
					p        = find_matching_parenthesis(r, p, '(', ')');
					location = p;
					if (p < 0) {
						break;
					}
					bool replace = true;
					int  len     = (int)strlen(r) - 1;
					int  p2      = p;
					while (p2 < len) {
						p2++;
						char c2 = r[p2];
						if (c2 == ' ' || c2 == '\t' || c2 == '\n' || c2 == '\r') {
							continue;
						}
						if (c2 == '{') {
							replace = false;
						}
						break;
					}
					if (replace) {
						r = mm_printf("%.*s%s%s", p, r, vp, r + p);
					}
					break;
				}
				else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
					p++;
				}
				else {
					break;
				}
			}
			if (p < 0) {
				break;
			}
			location = p;
		}
	}
	return r;
}

static char *expand_generic_code(const char *code, int generic_size, int first) {
	mm_buf_t rv = {0};
	char    *c  = mm_replace(code, "#count", mm_printf("%d", generic_size));
	while (*c) {
		char *f = strstr(c, "#for");
		if (f == NULL) {
			mm_buf_add(&rv, c);
			break;
		}
		mm_buf_addn(&rv, c, f - c);
		c          = f + 4;
		char *e    = strstr(c, "#end");
		char *body = e != NULL ? mm_strndup(c, e - c) : mm_strdup(c);
		c          = e != NULL ? e + 4 : c + strlen(c);
		for (int gi = 0; gi < generic_size; ++gi) {
			mm_buf_add(&rv, mm_replace(body, "#", mm_printf("%d", gi + first)));
		}
	}
	return mm_buf_get(&rv);
}

static const char *jstr(mm_json_t *o, const char *k) {
	return mm_json_str(mm_json_get(o, k), NULL);
}

static void output_set_type(mm_json_t *o) {
	if (jstr(o, "type") != NULL && mm_json_get(o, jstr(o, "type")) != NULL) {
		return;
	}
	for (int t = 0; t < MM_T_COUNT; ++t) {
		if (mm_json_get(o, mm_types[t].name) != NULL) {
			mm_json_t *v = mm_json_new(MM_JSON_STRING);
			v->string    = (char *)mm_types[t].name;
			mm_json_set(o, "type", v);
			return;
		}
	}
}

static bool name_generic(mm_json_t *item, const char *field, bool indirect) {
	const char *s = indirect ? jstr(item, jstr(item, field) != NULL ? jstr(item, field) : "") : jstr(item, field);
	return s != NULL && strchr(s, '#') != NULL;
}

static void generic_range(mm_json_t *arr, const char *field, bool indirect, int *first, int *last) {
	*first = -1;
	*last  = -1;
	for (int i = 0; arr != NULL && i < arr->count; ++i) {
		if (name_generic(arr->items[i], field, indirect)) {
			if (*first == -1) {
				*first = i;
			}
			else if (*last != -1) {
				*first = *last = -1;
				return;
			}
		}
		else if (*first != -1 && *last == -1) {
			*last = i;
		}
	}
	if (*first != -1 && *last == -1) {
		*last = arr->count;
	}
}

static mm_json_t *jstring(const char *s) {
	mm_json_t *v = mm_json_new(MM_JSON_STRING);
	v->string    = mm_strdup(s);
	return v;
}

static void replace_field(mm_json_t *o, const char *k, int gv) {
	const char *s = jstr(o, k);
	if (s != NULL && o->type == MM_JSON_OBJECT && mm_json_get(o, k)->type == MM_JSON_STRING) {
		mm_json_set(o, k, jstring(mm_replace(s, "#", mm_printf("%d", gv))));
	}
}

// Models saved by Material Maker before 1.0 declare functions taking "int seed", seeds have been floats since ($seed is
// "(seed_oN+fract(_seed_variation_))"): such calls match no function. Their bodies only use float(seed), so the
// parameter becomes a float, which is what the current library nodes declare (voronoi: "float seed"). Instance
// functions are named after their node: fixed when the model is set. Globals are shared: fixed when assembled, only
// when no declaration of the shader already takes the float (a current library node), as Material Maker then resolves
// the calls to that overload.
static char *fix_int_seed_parameters(char *s) {
	if (s == NULL || strstr(s, "int seed") == NULL) {
		return s;
	}
	mm_buf_t    b = {0};
	const char *p = s;
	const char *m;
	while ((m = strstr(p, "int seed")) != NULL) {
		const char *before = m;
		while (before > s && (before[-1] == ' ' || before[-1] == '\t' || before[-1] == '\n')) {
			before--;
		}
		const char *after = m + 8;
		while (*after == ' ' || *after == '\t' || *after == '\n') {
			after++;
		}
		bool param = before > s && (before[-1] == '(' || before[-1] == ',') && (*after == ')' || *after == ',');
		mm_buf_add(&b, mm_printf("%.*s", (int)(m - p), p));
		mm_buf_add(&b, param ? "float seed" : "int seed");
		p = m + 8;
	}
	mm_buf_add(&b, p);
	return mm_buf_get(&b);
}

void mm_shader_set_model(mm_gen_t *g, mm_json_t *model) {
	g->model         = model;
	mm_json_t *outs  = mm_json_get(model, "outputs");
	char      *inst  = fix_int_seed_parameters((char *)jstr(model, "instance"));
	mm_strs_t  funcs = {0};
	g->inputs        = mm_json_get(model, "inputs");
	g->param_defs    = mm_json_get(model, "parameters");
	g->includes      = mm_json_get(model, "includes");
	g->global        = (char *)jstr(model, "global"); // Its int seed parameters are fixed when globals are assembled
	g->code          = (char *)jstr(model, "code");
	g->instance      = NULL;
	if (inst != NULL && inst[0] != 0) {
		g->instance = find_instance_functions(inst, &funcs);
		g->instance = fix_instance_functions(g->instance, &funcs);
		if (g->code != NULL) {
			g->code = fix_instance_functions(g->code, &funcs);
		}
		if (g->param_defs != NULL) {
			mm_json_t *pd = mm_json_new(MM_JSON_ARRAY);
			for (int i = 0; i < g->param_defs->count; ++i) {
				mm_json_t *p = g->param_defs->items[i];
				if (strcmp(mm_json_str(mm_json_get(p, "type"), ""), "enum") == 0) {
					mm_json_t *vals    = mm_json_get(p, "values");
					bool       replace = false;
					mm_json_t *nv      = mm_json_new(MM_JSON_ARRAY);
					for (int k = 0; vals != NULL && k < vals->count; ++k) {
						const char *v  = mm_json_str(mm_json_get(vals->items[k], "value"), "");
						char       *fv = fix_instance_functions(v, &funcs);
						if (strcmp(fv, v) != 0) {
							replace = true;
						}
						mm_json_t *e = mm_json_new(MM_JSON_OBJECT);
						mm_json_set(e, "name", mm_json_copy(mm_json_get(vals->items[k], "name")));
						mm_json_set(e, "value", jstring(fv));
						mm_json_push(nv, e);
					}
					if (replace) {
						p = mm_json_copy(p);
						mm_json_set(p, "values", nv);
					}
				}
				mm_json_push(pd, p);
			}
			g->param_defs = pd;
		}
		if (outs != NULL) {
			mm_json_t *no = mm_json_new(MM_JSON_ARRAY);
			for (int i = 0; i < outs->count; ++i) {
				mm_json_t *o = mm_json_copy(outs->items[i]);
				output_set_type(o);
				const char *t = jstr(o, "type");
				if (t != NULL && jstr(o, t) != NULL) {
					mm_json_set(o, t, jstring(fix_instance_functions(jstr(o, t), &funcs)));
				}
				mm_json_push(no, o);
			}
			outs = no;
		}
	}
	else if (outs != NULL) {
		mm_json_t *no = mm_json_new(MM_JSON_ARRAY);
		for (int i = 0; i < outs->count; ++i) {
			mm_json_t *o = mm_json_copy(outs->items[i]);
			output_set_type(o);
			mm_json_push(no, o);
		}
		outs = no;
	}
	g->outputs = outs;

	// Generic (#) inputs, parameters and outputs
	bool generic = false;
	for (int i = 0; g->param_defs != NULL && i < g->param_defs->count; ++i) {
		generic = generic || name_generic(g->param_defs->items[i], "name", false);
	}
	for (int i = 0; g->inputs != NULL && i < g->inputs->count; ++i) {
		generic = generic || name_generic(g->inputs->items[i], "name", false);
	}
	if (generic) {
		int fi, li, fp, lp, fo, lo;
		generic_range(mm_json_get(model, "inputs"), "name", false, &fi, &li);
		generic_range(mm_json_get(model, "parameters"), "name", false, &fp, &lp);
		generic_range(g->outputs, "type", true, &fo, &lo);
		int gs = g->generic_size > 0 ? g->generic_size : 1;
		if (fi != -1) {
			mm_json_t *src = mm_json_get(model, "inputs"), *ni = mm_json_new(MM_JSON_ARRAY);
			for (int i = 0; i < fi; ++i) {
				mm_json_push(ni, src->items[i]);
			}
			for (int k = 0; k < gs; ++k) {
				for (int i = fi; i < li; ++i) {
					mm_json_t *in = mm_json_copy(src->items[i]);
					replace_field(in, "name", k + 1);
					replace_field(in, "label", k + 1);
					mm_json_push(ni, in);
				}
			}
			for (int i = li; i < src->count; ++i) {
				mm_json_push(ni, src->items[i]);
			}
			g->inputs = ni;
		}
		mm_json_t *src = g->param_defs, *np = mm_json_new(MM_JSON_ARRAY);
		for (int i = 0; src != NULL && i < src->count; ++i) {
			if (fp != -1 && i == fp) {
				for (int k = 0; k < gs; ++k) {
					for (int j = fp; j < lp; ++j) {
						mm_json_t *pp = mm_json_copy(src->items[j]);
						replace_field(pp, "name", k + 1);
						mm_json_push(np, pp);
					}
				}
				i = lp - 1;
				continue;
			}
			mm_json_push(np, src->items[i]);
		}
		g->param_defs = np;
		if (fo != -1) {
			mm_json_t *so = g->outputs, *no = mm_json_new(MM_JSON_ARRAY);
			for (int i = 0; i < fo; ++i) {
				mm_json_push(no, so->items[i]);
			}
			for (int k = 0; k < gs; ++k) {
				for (int i = fo; i < lo; ++i) {
					mm_json_t *o = mm_json_copy(so->items[i]);
					replace_field(o, jstr(o, "type"), k + 1);
					mm_json_push(no, o);
				}
			}
			for (int i = lo; i < so->count; ++i) {
				mm_json_push(no, so->items[i]);
			}
			g->outputs = no;
		}
		if (g->code != NULL) {
			g->code = expand_generic_code(g->code, gs, 1);
		}
		if (g->instance != NULL) {
			g->instance = expand_generic_code(g->instance, gs, 1);
		}
	}

	g->model_uses_seed = false;
	for (int i = 0; outs != NULL && i < outs->count; ++i) {
		const char *t = jstr(outs->items[i], "type");
		const char *c = t != NULL ? jstr(outs->items[i], t) : NULL;
		if (c != NULL && (strstr(c, "$seed") != NULL || strstr(c, "$(seed)") != NULL)) {
			g->model_uses_seed = true;
		}
	}
	const char *mc = jstr(model, "code"), *mi = jstr(model, "instance");
	if ((mc != NULL && (strstr(mc, "$seed") || strstr(mc, "$(seed)"))) || (mi != NULL && (strstr(mi, "$seed") || strstr(mi, "$(seed)")))) {
		g->model_uses_seed = true;
	}
}

// Code generation (gen_shader.gd)

typedef struct {
	char *name;
	char *value; // A string replacement
	int   input; // >= 0: an input, replaced through replace_input
} mm_var_t;

typedef struct {
	mm_var_t *items;
	int       count;
} mm_vars_t;

static void vars_set(mm_vars_t *v, const char *name, const char *value, int input) {
	for (int i = 0; i < v->count; ++i) {
		if (strcmp(v->items[i].name, name) == 0) {
			v->items[i].value = value != NULL ? mm_strdup(value) : NULL;
			v->items[i].input = input;
			return;
		}
	}
	mm_var_t *items = mm_alloc(sizeof(mm_var_t) * (v->count + 1));
	if (v->count > 0) {
		memcpy(items, v->items, sizeof(mm_var_t) * v->count);
	}
	items[v->count].name  = mm_strdup(name);
	items[v->count].value = value != NULL ? mm_strdup(value) : NULL;
	items[v->count].input = input;
	v->items              = items;
	v->count++;
}

static mm_var_t *vars_get(mm_vars_t *v, const char *name) {
	for (int i = 0; i < v->count; ++i) {
		if (strcmp(v->items[i].name, name) == 0) {
			return &v->items[i];
		}
	}
	return NULL;
}

static char *replace_variables(mm_gen_t *g, const char *string, mm_vars_t *vars, mm_code_t *rv, mm_context_t *ctx);

static char *find_keyword_call(const char *s, const char *keyword) {
	char       *search = mm_printf("$%s(", keyword);
	const char *pos    = strstr(s, search);
	if (pos == NULL) {
		return "";
	}
	int begin = (int)(pos - s) + (int)strlen(search);
	int end   = find_matching_parenthesis(s, begin - 1, '(', ')');
	if (end > 0) {
		return mm_strndup(s + begin, end - begin);
	}
	return "#error";
}

static char *replace_rnd(const char *s, int offset) {
	char *r = mm_strdup(s);
	while (true) {
		char *params = find_keyword_call(r, "rnd");
		if (params[0] == 0 || strcmp(params, "#error") == 0) {
			break;
		}
		char       *rep = mm_printf("$rnd(%s)", params);
		const char *pos = strstr(r, rep);
		if (pos == NULL) {
			break;
		}
		char *with = mm_printf("param_rnd(%s, $seed+%f)", params, sin((double)((pos - r) + offset)) * 0.5 + 0.5);
		r          = mm_replace(r, rep, with);
	}
	return r;
}

static char *replace_rndi(const char *s, int offset) {
	char *r = mm_strdup(s);
	while (true) {
		char *params = find_keyword_call(r, "rndi");
		if (params[0] == 0 || strcmp(params, "#error") == 0) {
			break;
		}
		char       *rep = mm_printf("$rndi(%s)", params);
		const char *pos = strstr(r, rep);
		if (pos == NULL) {
			break;
		}
		char *with = mm_printf("param_rndi(%s, $seed+%f)", params, sin((double)(pos - r)) + offset);
		r          = mm_replace(r, rep, with);
	}
	return r;
}

static char *genname(mm_gen_t *g) {
	return mm_printf("o%d", g->id);
}

static void get_common_variables(mm_gen_t *g, const char *uv, mm_code_t *rv, mm_vars_t *vars) {
	char *gn = genname(g);
	vars_set(vars, "uv", uv, -1);
	vars_set(vars, "time", "elapsed_time", -1);
	// Named parameters of remotes in the parent graph
	mm_gen_t *parent = g->parent;
	for (int i = 0; parent != NULL && i < parent->nchildren; ++i) {
		mm_gen_t *r = parent->children[i];
		if (r->kind != MM_G_REMOTE || r->widgets == NULL) {
			continue;
		}
		for (int w = 0; w < r->widgets->count; ++w) {
			mm_json_t *wd = r->widgets->items[w];
			if (strcmp(mm_json_str(mm_json_get(wd, "type"), ""), "named_parameter") == 0) {
				const char *n  = mm_json_str(mm_json_get(wd, "name"), "");
				char       *id = mm_printf("o%d_%s", r->id, n);
				double      v  = mm_json_num(mm_gen_param(r, n), 0);
				vars_set(vars, n, id, -1);
				mm_code_add_uniform(rv, id, MM_U_FLOAT, 0, &v, 1);
			}
		}
	}
	vars_set(vars, "name", gn, -1);
	vars_set(vars, "seed", g->seed_locked ? mm_printf("seed_%s", gn) : mm_printf("(seed_%s+fract(_seed_variation_))", gn), -1);
	vars_set(vars, "?1", "_controlled_variation_.x", -1);
	vars_set(vars, "?2", "_controlled_variation_.y", -1);
	vars_set(vars, "?3", "_controlled_variation_.z", -1);
	vars_set(vars, "?4", "_controlled_variation_.w", -1);
	vars_set(vars, "node_id", mm_printf("%d", g->id), -1);
}

static void process_parameters(mm_gen_t *g, mm_code_t *rv, mm_vars_t *vars, bool declare) {
	char *gn         = genname(g);
	int   rnd_offset = 0;
	if (mm_gen_has_randomness(g)) {
		if (declare) {
			double s = mm_gen_seed(g);
			mm_code_add_uniform(rv, mm_printf("seed_%s", gn), MM_U_FLOAT, 0, &s, 1);
		}
		vars_set(vars, "seed", mm_printf("(seed_%s+fract(_seed_variation_))", gn), -1);
	}
	for (int i = 0; g->param_defs != NULL && i < g->param_defs->count; ++i) {
		mm_json_t  *p     = g->param_defs->items[i];
		const char *name  = mm_json_str(mm_json_get(p, "name"), "");
		const char *type  = mm_json_str(mm_json_get(p, "type"), "");
		mm_json_t  *value = mm_gen_param(g, name);
		char       *pn    = mm_printf("%s_%s", gn, name);
		if (strcmp(type, "float") == 0) {
			if (value == NULL || value->type != MM_JSON_STRING) {
				double v = mm_json_num(value, 0);
				if (declare) {
					mm_code_add_uniform(rv, mm_printf("p_%s", pn), MM_U_FLOAT, 0, &v, 1);
				}
				vars_set(vars, name, mm_printf("p_%s", pn), -1);
			}
			else {
				char *s = replace_rnd(value->string, rnd_offset);
				s       = replace_rndi(s, rnd_offset);
				vars_set(vars, name, mm_printf("(%s)", s), -1);
			}
			rnd_offset += 17;
		}
		else if (strcmp(type, "color") == 0) {
			double c[4] = {mm_json_num(mm_json_get(value, "r"), 0), mm_json_num(mm_json_get(value, "g"), 0), mm_json_num(mm_json_get(value, "b"), 0),
			               mm_json_num(mm_json_get(value, "a"), 1)};
			if (declare) {
				mm_code_add_uniform(rv, mm_printf("p_%s", pn), MM_U_VEC4, 0, c, 4);
			}
			vars_set(vars, name, mm_printf("p_%s", pn), -1);
		}
		else if (strcmp(type, "enum") == 0) {
			mm_json_t *vals = mm_json_get(p, "values");
			char      *s    = "";
			if (vals != NULL && vals->count > 0) {
				int k = 0;
				if (value != NULL && value->type == MM_JSON_NUMBER) {
					double d = value->number;
					k        = (d < 0 || d >= vals->count) ? 0 : (int)d;
				}
				s = (char *)mm_json_str(mm_json_get(vals->items[k], "value"), "");
			}
			vars_set(vars, name, s, -1);
		}
		else if (strcmp(type, "boolean") == 0) {
			vars_set(vars, name, mm_json_bool(value, false) ? "true" : "false", -1);
		}
		else if (strcmp(type, "size") == 0) {
			vars_set(vars, name, mm_printf("%.1f", pow(2.0, mm_json_num(value, 0))), -1);
		}
		else if (strcmp(type, "gradient") == 0) {
			vars_set(vars, name, mm_type_gradient(rv, value, pn, declare), -1);
		}
		else if (strcmp(type, "curve") == 0) {
			vars_set(vars, name, mm_type_curve(rv, value, pn, declare), -1);
		}
		else if (strcmp(type, "polygon") == 0 || strcmp(type, "polyline") == 0) {
			vars_set(vars, name, mm_type_polygon(rv, value, pn, declare, strcmp(type, "polygon") == 0), -1);
		}
		else if (strcmp(type, "splines") == 0) {
			vars_set(vars, name, mm_type_splines(rv, value, pn, declare), -1);
		}
		else if (strcmp(type, "pixels") == 0) {
			char *size = NULL;
			vars_set(vars, name, mm_type_pixels(rv, value, pn, declare, &size), -1);
			vars_set(vars, mm_printf("%s_size", name), size != NULL ? size : "ivec2(1, 1)", -1);
		}
		else if (strcmp(type, "lattice") == 0) {
			char *size = NULL;
			vars_set(vars, name, mm_type_lattice(rv, value, pn, declare, &size), -1);
			vars_set(vars, mm_printf("%s_size", name), size != NULL ? size : "ivec2(1, 1)", -1);
		}
	}
}

static char *split_parameters_joined(const char *s, int *count) {
	// s is "(a, b, ...)", returns "a, b" with the parameters stripped, *count parameters
	int len = (int)strlen(s);
	*count  = 0;
	if (len < 2 || s[0] != '(' || find_matching_parenthesis(s, 0, '(', ')') != len - 1) {
		return mm_strdup("");
	}
	char     *inner = mm_strndup(s + 1, len - 2);
	mm_strs_t parts = {0};
	char     *p     = mm_strdup("");
	char     *t     = inner;
	while (true) {
		char *comma = strchr(t, ',');
		char *piece = comma != NULL ? mm_strndup(t, comma - t) : mm_strdup(t);
		p           = p[0] != 0 ? mm_printf("%s,%s", p, piece) : piece;
		int  po = 0, pc = 0, bo = 0, bc = 0;
		for (char *q = p; *q; ++q) {
			po += *q == '(';
			pc += *q == ')';
			bo += *q == '[';
			bc += *q == ']';
		}
		if (po == pc && bo == bc) {
			strs_push(&parts, mm_strip(p));
			p = mm_strdup("");
		}
		if (comma == NULL) {
			break;
		}
		t = comma + 1;
	}
	mm_buf_t b = {0};
	for (int i = 0; i < parts.count; ++i) {
		if (i > 0) {
			mm_buf_add(&b, ", ");
		}
		mm_buf_add(&b, parts.items[i]);
	}
	*count = parts.count;
	return mm_buf_get(&b);
}

static mm_json_t *output_attributes(mm_gen_t *src, int output); // Buffers, iterate buffers, images, text

static char *replace_input(mm_gen_t *g, int input, const char *suffix, const char *parameters, mm_vars_t *vars, mm_code_t *rv, mm_context_t *ctx) {
	mm_json_t *def = mm_json_at(g->inputs, input);
	mm_port_t  src = mm_get_source(g, input);
	if (src.gen == NULL) {
		mm_var_t *uvv    = vars_get(vars, "uv");
		char     *old_uv = uvv->value;
		uvv->value       = mm_strdup(parameters);
		char *r          = replace_variables(g, mm_json_str(mm_json_get(def, "default"), ""), vars, rv, ctx);
		uvv            = vars_get(vars, "uv");
		uvv->value     = old_uv;
		return r;
	}
	if (suffix[0] != 0) {
		mm_json_t *attrs = output_attributes(src.gen, src.output);
		const char *an   = strcmp(suffix, "size") == 0 ? "texture_size" : suffix;
		if (attrs != NULL && mm_json_get(attrs, an) != NULL) {
			return (char *)mm_json_str(mm_json_get(attrs, an), "");
		}
		if (strcmp(suffix, "texture") == 0 || strcmp(suffix, "size") == 0) {
			return mm_printf("(error: Cannot find attribute %s for input %s)", suffix, jstr(def, "name"));
		}
	}
	if (mm_json_truthy(mm_json_get(def, "function"))) { // Some definitions say "true" (a string)
		char *fn = mm_printf("o%d_input_%s", g->id, jstr(def, "name"));
		if (strcmp(suffix, "variation") == 0) {
			int   n;
			char *joined = split_parameters_joined(parameters, &n);
			const char *ps = n == 1 ? ", _seed_variation_, _controlled_variation_)" : n == 2 ? ", _controlled_variation_)" : ")";
			return mm_printf("%s(%s%s", fn, joined, ps);
		}
		return mm_printf("%s(%s, _seed_variation_, _controlled_variation_)", fn, parameters);
	}
	mm_code_t *srv = mm_get_shader_code(src.gen, parameters, src.output, ctx);
	mm_code_add_uniforms(rv, srv);
	mm_buf_add(&rv->defs, mm_buf_get(&srv->defs));
	mm_code_add_globals(rv, srv);
	mm_buf_add(&rv->code, mm_buf_get(&srv->code));
	rv->alias   = srv;
	mm_type_t t = mm_type_from_name(jstr(def, "type"));
	if (t == MM_T_COUNT || srv->output_values[t] == NULL) {
		return mm_printf("(error: no %s output)", jstr(def, "type"));
	}
	return srv->output_values[t];
}

static char *replace_variables(mm_gen_t *g, const char *string, mm_vars_t *vars, mm_code_t *rv, mm_context_t *ctx) {
	char *s          = mm_strdup(string);
	char *string_end = mm_strdup("");
	while (true) {
		char *dollar = strrchr(s, '$');
		if (dollar == NULL) {
			break;
		}
		int   pos = (int)(dollar - s);
		int   variable_end;
		char *variable;
		if (s[pos + 1] == '(') {
			variable_end = find_matching_parenthesis(s, pos + 1, '(', ')') + 1;
			if (variable_end <= 0) {
				break;
			}
			variable = mm_strndup(s + pos + 2, variable_end - 3 - pos);
		}
		else {
			int e = pos + 1;
			while (s[e] && is_word_letter(s[e])) {
				e++;
			}
			variable_end = e;
			variable     = mm_strndup(s + pos + 1, e - pos - 1);
		}
		string_end = mm_printf("%s%s", s + variable_end, string_end);
		s          = mm_strndup(s, pos);
		mm_var_t *v = vars_get(vars, variable);
		if (v == NULL) {
			s = mm_printf("%s(error: %s not found)", s, variable);
		}
		else if (v->input < 0) {
			s = mm_printf("%s%s", s, v->value);
		}
		else {
			bool  replace_all = pos == 0;
			char *suffix      = "";
			char *params      = "";
			string_end        = mm_strip(string_end);
			if (string_end[0] == '.') {
				string_end = mm_strip(string_end + 1);
				int e      = 0;
				while (string_end[e] && is_word_letter(string_end[e])) {
					e++;
				}
				suffix     = mm_strndup(string_end, e);
				string_end = mm_strip(string_end + e);
			}
			if (string_end[0] == '(') {
				int pe = find_matching_parenthesis(string_end, 0, '(', ')');
				if (pe < 0) {
					pe = (int)strlen(string_end) - 1;
				}
				if (pe < (int)strlen(string_end) - 1) {
					replace_all = false;
				}
				params = mm_strndup(string_end, pe + 1);
				if (params[1] == '(' && find_matching_parenthesis(params, 1, '(', ')') == (int)strlen(params) - 2) {
					params = mm_strndup(params + 1, strlen(params) - 2);
				}
				string_end = mm_strdup(string_end + pe + 1);
			}
			else {
				replace_all = false;
			}
			s = mm_printf("%s%s", s, replace_input(g, v->input, suffix, params, vars, rv, ctx));
			if (!replace_all) {
				rv->alias = NULL;
			}
		}
	}
	return mm_printf("%s%s", s, string_end);
}

static void generate_input_function(mm_gen_t *g, int index, mm_json_t *input, mm_code_t *rv, mm_context_t *ctx) {
	mm_port_t src = mm_get_source(g, index);
	if (src.gen == NULL) {
		return;
	}
	mm_type_t  t     = mm_type_from_name(jstr(input, "type"));
	if (t == MM_T_COUNT) {
		return;
	}
	mm_context_t *local = mm_context_new(ctx);
	mm_code_t    *srv   = mm_get_shader_code(src.gen, mm_types[t].params, src.output, local);
	mm_code_add_uniforms(rv, srv);
	mm_buf_add(&rv->defs, mm_buf_get(&srv->defs));
	mm_code_add_globals(rv, srv);
	mm_buf_printf(&rv->defs, "%s o%d_input_%s(%s, float _seed_variation_, vec4 _controlled_variation_) {\n", mm_types[t].glsl, g->id, jstr(input, "name"),
	              mm_types[t].paramdefs);
	mm_buf_printf(&rv->defs, "%s\n", mm_buf_get(&srv->code));
	mm_buf_printf(&rv->defs, "return %s;\n}\n", srv->output_values[t] != NULL ? srv->output_values[t] : "0.0");
}

static mm_code_t *shader_get_code(mm_gen_t *g, const char *uv, int output_index, mm_context_t *ctx) {
	char      *gn  = genname(g);
	mm_code_t *rv  = mm_code_new();
	bool       decl = !mm_context_has_variant(ctx, g);
	mm_json_t *output = mm_json_at(g->outputs, output_index);
	if (output == NULL) {
		return rv;
	}
	mm_vars_t vars = {0};
	get_common_variables(g, uv, rv, &vars);
	process_parameters(g, rv, &vars, decl);
	for (int i = 0; g->inputs != NULL && i < g->inputs->count; ++i) {
		mm_json_t *in = g->inputs->items[i];
		if (decl && mm_json_truthy(mm_json_get(in, "function"))) {
			generate_input_function(g, i, in, rv, ctx);
		}
		vars_set(&vars, jstr(in, "name"), NULL, i);
	}
	if (decl) {
		for (int i = 0; g->includes != NULL && i < g->includes->count; ++i) {
			const char *inc = mm_json_str(g->includes->items[i], "");
			mm_json_t  *lib = mm_library_node(inc);
			const char *gl  = lib != NULL ? jstr(mm_json_get(lib, "shader_model"), "global") : NULL;
			if (gl != NULL && gl[0] != 0) {
				mm_code_add_global(rv, gl, inc);
			}
		}
		if (g->global != NULL) {
			mm_code_add_global(rv, g->global, mm_printf("%s (%s)", mm_gen_hier_name(g), gn));
		}
		if (g->instance != NULL) {
			char *ic = mm_strip(replace_variables(g, g->instance, &vars, rv, ctx));
			if (ic[0] != 0) {
				mm_buf_printf(&rv->defs, "\n// #instance: %s (%s)\n", mm_gen_hier_name(g), gn);
				mm_buf_add(&rv->defs, replace_variables(g, g->instance, &vars, rv, ctx));
				mm_buf_add(&rv->defs, "\n\n");
			}
		}
	}
	const char *otype = jstr(output, "type");
	const char *oexpr = otype != NULL ? jstr(output, otype) : NULL;
	if (g->code != NULL && (oexpr == NULL || strstr(oexpr, "@NOCODE") == NULL)) {
		int variant = mm_context_get_variant(ctx, g, uv);
		vars_set(&vars, "name_uv", mm_printf("%s_%d", gn, mm_context_get_variant(ctx, g, uv)), -1);
		if (variant == -1) {
			char *code = replace_variables(g, g->code, &vars, rv, ctx);
			if (code[0] != 0) {
				mm_buf_printf(&rv->code, "\n// #code: %s (%s)\n", mm_gen_hier_name(g), gn);
				mm_buf_add(&rv->code, code);
			}
		}
	}
	char *variant_string = mm_printf("%s,%d", uv, output_index);
	int   variant        = mm_context_get_variant(ctx, g, variant_string);
	bool  assign         = false;
	if (variant == -1) {
		variant = mm_context_get_variant(ctx, g, variant_string);
		assign  = true;
	}
	bool use_alias = false;
	for (int t = 0; t < MM_T_COUNT; ++t) {
		const char *raw = jstr(output, mm_types[t].name);
		if (raw == NULL) {
			continue;
		}
		char *os   = mm_strip(mm_replace(mm_replace(raw, "@NOCODE", ""), "@KEEPTYPE", ""));
		char *expr = replace_variables(g, os, &vars, rv, ctx);
		if (rv->alias != NULL && strstr(raw, "@KEEPTYPE") != NULL) {
			use_alias = true;
		}
		if (!use_alias) {
			char *vn = mm_printf("%s_%d_%d_%s", gn, output_index, variant, mm_types[t].name);
			if (assign) {
				mm_buf_printf(&rv->code, "\n// #output%d: %s (%s)\n", output_index, mm_gen_hier_name(g), gn);
				mm_buf_printf(&rv->code, "%s %s = %s;\n", mm_types[t].glsl, vn, expr);
			}
			rv->output_values[t] = vn;
		}
	}
	if (use_alias) {
		memcpy(rv->output_values, rv->alias->output_values, sizeof(rv->output_values));
		rv->output_type = rv->alias->output_type;
	}
	else {
		rv->output_type = mm_type_from_name(otype);
	}
	return rv;
}

// Texture generators: buffers, iterate buffers (gen_texture.gd _get_shader_code_lod)

void mm_buffer_analyze(mm_gen_t *g); // mm_graph.c: finds whether a buffer renders greyscale

static mm_code_t *texture_get_code(mm_gen_t *g, const char *uv, int output_index, mm_context_t *ctx, bool grey, double lod, const char *suffix,
                                   const char *adjusted_uv, int texture_output) {
	char      *gn      = genname(g);
	mm_code_t *rv      = mm_code_new();
	rv->output_type    = grey ? MM_T_F : MM_T_RGBA;
	char      *tex     = mm_printf("%s%s", gn, suffix);
	int        variant = mm_context_get_variant(ctx, g, uv);
	const char *type   = grey ? "float" : "vec4";
	const char *sel    = grey ? ".r" : "";
	if (variant == -1) {
		variant         = mm_context_get_variant(ctx, g, uv);
		mm_uniform_t *u = mm_code_add_uniform(rv, tex, MM_U_SAMPLER, 0, NULL, 0);
		u->texture        = g;
		u->texture_output = texture_output;
		if (lod < 0) {
			mm_buf_printf(&rv->code, "%s %s_%d = textureLod(%s, %s, 0.0)%s;\n", type, gn, variant, tex, adjusted_uv, sel);
		}
		else {
			mm_code_add_uniform(rv, mm_printf("p_%s_lod", gn), MM_U_FLOAT, 0, &lod, 1);
			mm_buf_printf(&rv->code, "%s %s_%d = textureLod(%s, %s, p_o%d_lod)%s;\n", type, gn, variant, tex, adjusted_uv, g->id, sel);
		}
	}
	rv->output_values[rv->output_type] = mm_printf("%s_%d", gn, variant);
	return rv;
}

static mm_json_t *output_attributes(mm_gen_t *src, int output) {
	mm_json_t *a  = mm_json_new(MM_JSON_OBJECT);
	char      *gn = genname(src);
	if (src->kind == MM_G_BUFFER) {
		mm_json_set(a, "texture", jstring(mm_printf("%s_tex", gn)));
		mm_json_set(a, "texture_size", jstring(mm_printf("%s_tex_size", gn)));
		return a;
	}
	if (src->kind == MM_G_ITERATE) {
		mm_json_set(a, "texture", jstring(mm_printf(output == 0 ? "%s_tex" : "%s_loop_tex", gn)));
		mm_json_set(a, "texture_size", jstring(mm_printf("%s_it_tex_size", gn)));
		if (output == 1) {
			mm_json_set(a, "iteration", jstring(mm_printf("%s_iteration", gn)));
		}
		return a;
	}
	if (src->kind == MM_G_IMAGE || src->kind == MM_G_TEXT) {
		mm_json_set(a, "texture", jstring(mm_printf("%s_tex", gn)));
		mm_json_set(a, "texture_size", jstring(mm_printf("%s_tex_size", gn)));
		return a;
	}
	return NULL;
}

// A hoisted loop input (mm_plan): the texel its pass rendered at this pixel, read without filtering. The loop pass and
// the hoisted pass have the same size and uv falls on texel centres, so this is the value the loop would compute.
static mm_code_t *hoist_get_code(mm_gen_t *g, const char *uv, mm_context_t *ctx) {
	char       *gn      = genname(g);
	mm_code_t  *rv      = mm_code_new();
	char       *tex     = mm_printf("%s_tex", gn);
	int         t       = g->hoist_type;
	const char *sel     = t == MM_T_F ? ".r" : t == MM_T_RGB ? ".rgb" : "";
	int         variant = mm_context_get_variant(ctx, g, uv);
	if (variant == -1) {
		variant         = mm_context_get_variant(ctx, g, uv);
		mm_uniform_t *u = mm_code_add_uniform(rv, tex, MM_U_SAMPLER, 0, NULL, 0);
		u->texture        = g;
		u->texture_output = 0;
		mm_buf_printf(&rv->code, "%s %s_%d = texelFetch(%s, ivec2(floor(fract(%s) * vec2(textureSize(%s, 0)))), 0)%s;\n", mm_types[t].glsl, gn, variant, tex,
		              uv, tex, sel);
	}
	rv->output_type    = t;
	rv->output_values[t] = mm_printf("%s_%d", gn, variant);
	return rv;
}

static mm_code_t *gen_get_code_raw(mm_gen_t *g, const char *uv, int output, mm_context_t *ctx) {
	switch (g->kind) {
	case MM_G_SHADER:
		return shader_get_code(g, uv, output, ctx);
	case MM_G_GRAPH: {
		mm_gen_t *outs = mm_gen_child(g, "gen_outputs");
		return outs != NULL ? gen_get_code_raw(outs, uv, output, ctx) : mm_code_new();
	}
	case MM_G_IOS: {
		mm_port_t src = mm_get_source(g, output);
		return src.gen != NULL ? gen_get_code_raw(src.gen, uv, src.output, ctx) : mm_code_default();
	}
	case MM_G_SWITCH: {
		int       outputs = (int)mm_json_num(mm_gen_param(g, "outputs"), 2);
		int       source  = (int)mm_json_num(mm_gen_param(g, "source"), 0);
		mm_port_t src     = mm_get_source(g, output + source * outputs);
		return src.gen != NULL ? gen_get_code_raw(src.gen, uv, src.output, ctx) : mm_code_default();
	}
	case MM_G_REROUTE: {
		mm_port_t src = mm_get_source(g, 0);
		return src.gen != NULL ? gen_get_code_raw(src.gen, uv, src.output, ctx) : mm_code_default();
	}
	case MM_G_PORTAL: { // An OUT portal renders the source of the IN portal with the same link
		const char *link = mm_json_str(mm_gen_param(g, "link"), "aperture_1");
		for (int i = 0; g->parent != NULL && i < g->parent->nchildren; ++i) {
			mm_gen_t *w = g->parent->children[i];
			if (w != g && w->kind == MM_G_PORTAL && (int)mm_json_num(mm_json_get(w->data, "io"), 0) == 0 &&
			    strcmp(mm_json_str(mm_gen_param(w, "link"), "aperture_1"), link) == 0) {
				mm_port_t src = mm_get_source(w, 0);
				return src.gen != NULL ? gen_get_code_raw(src.gen, uv, src.output, ctx) : mm_code_default();
			}
		}
		return mm_code_default();
	}
	case MM_G_BUFFER: {
		mm_buffer_analyze(g);
		bool  filter = g->version != 2 || mm_json_bool(mm_gen_param(g, "filter"), true);
		char *gn     = genname(g);
		char *auv    = filter ? mm_printf("fract(%s)", uv) : mm_printf("((floor(fract(%s) * %s_tex_size)+vec2(0.5))/%s_tex_size)", uv, gn, gn);
		double lod   = output == 0 ? -1.0 : mm_json_num(mm_gen_param(g, "lod"), 0);
		mm_code_t *rv = texture_get_code(g, uv, output, ctx, g->greyscale, lod, "_tex", auv, 0);
		double     sz = pow(2.0, mm_json_num(mm_gen_param(g, "size"), 9));
		mm_code_add_uniform(rv, mm_printf("%s_tex_size", gn), MM_U_FLOAT, 0, &sz, 1);
		return rv;
	}
	case MM_G_ITERATE: {
		mm_buffer_analyze(g);
		bool       filter = mm_json_bool(mm_gen_param(g, "filter"), true);
		char      *gn     = genname(g);
		char      *auv    = filter ? mm_printf("fract(%s)", uv) : mm_printf("((floor(fract(%s) * %s_it_tex_size)+vec2(0.5))/%s_it_tex_size)", uv, gn, gn);
		mm_code_t *rv     = texture_get_code(g, uv, output, ctx, g->greyscale, -1.0, output == 0 ? "_it_tex" : "_loop_tex", auv, output);
		double     sz     = pow(2.0, mm_json_num(mm_gen_param(g, "size"), 9));
		mm_code_add_uniform(rv, mm_printf("%s_it_tex_size", gn), MM_U_FLOAT, 0, &sz, 1);
		double it = 0; // Dynamic in the loop pass (see mm_plan)
		mm_code_add_uniform(rv, mm_printf("%s_iteration", gn), MM_U_INT, 0, &it, 1);
		return rv;
	}
	case MM_G_IMAGE:
	case MM_G_TEXT: {
		char      *auv = g->kind == MM_G_IMAGE ? mm_image_adjusted_uv(g, uv) : mm_strdup(uv); // MMGenText: uv as is
		mm_code_t *rv  = texture_get_code(g, uv, output, ctx, false, -1.0, "_tex", auv, 0);
		return rv;
	}
	case MM_G_NATIVE:
		return mm_native_code(g, uv, ctx);
	case MM_G_HOIST:
		return hoist_get_code(g, uv, ctx);
	default:
		return mm_code_new();
	}
}

mm_code_hook_t mm_code_hook      = NULL;
void          *mm_code_hook_data = NULL;

mm_code_t *mm_get_shader_code(mm_gen_t *g, const char *uv, int output, mm_context_t *ctx) {
	if (mm_code_hook != NULL) {
		mm_gen_t *sub = mm_code_hook(g, uv, output, ctx, mm_code_hook_data);
		if (sub != NULL) {
			g      = sub;
			output = 0;
		}
	}
	mm_code_t *rv = gen_get_code_raw(g, uv, output, ctx);
	if (rv->output_type < MM_T_COUNT) {
		const mm_type_info_t *ti = &mm_types[rv->output_type];
		for (int i = 0; i < ti->nconvert; ++i) {
			mm_type_t to = ti->convert[i].to;
			if (rv->output_values[to] == NULL && rv->output_values[rv->output_type] != NULL) {
				rv->output_values[to] = mm_replace(ti->convert[i].expr, "$(value)", rv->output_values[rv->output_type]);
			}
		}
	}
	return rv;
}
