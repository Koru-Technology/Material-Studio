#include "../global.h"

// Pattern nodes: tileable procedural patterns in the style of Material Maker. Every pattern node has the same first inputs
// (Vector, Scale, Stretch, Angle, Contrast, Seed, Distort, Distort Scale), up to four of its own after them, a Mapping
// button (UV or Triplanar) and two outputs: Height (1 on the surface, 0 in the recesses) and Cell (a random value for each
// brick, tile, cell..). With UV mapping the cell counts snap to whole numbers so the pattern wraps at the uv borders.
//
// The noise, hash and scratch functions are ported from Material Maker (https://github.com/RodZill4/material-maker),
// MIT License, Copyright (c) 2018-present Rodolphe Suescun and contributors. See paint/THIRD_PARTY_NOTICES.md.

#define PATTERN_COMMON_INPUTS 8
#define PATTERN_EXTRA_INPUTS  4

// From Material Maker rand / rand2 and old_value_noise_2d / old_fbm_2d_value (nodes/fbm2.mmg), MIT, (c) Rodolphe Suescun and
// contributors. The noise repeats every per cells, the fbm doubles per with the frequency so it repeats too. The fbm sums
// gradient noise, value noise shows its grid.
char *str_pattern_common = "\
float pat_rand(float2 x) { \
	float d = dot(x, float2(13.9898, 8.141)); \
	return frac(cos(d - 3.14 * floor(d / 3.14)) * 43758.5453); \
} \
float2 pat_rand2(float2 x) { \
	float2 d = float2(dot(x, float2(13.9898, 8.141)), dot(x, float2(3.4562, 17.398))); \
	return frac(cos(d - float2(3.14, 3.14) * floor(d / 3.14)) * 43758.5453); \
} \
float2 pat_wrap(float2 c, float2 per) { \
	return c - per * floor(c / per); \
} \
float2 pat_seed(float seed) { \
	return float2(seed * 0.1373, seed * 0.2917); \
} \
float pat_cell_rand(float2 c, float2 per, float seed) { \
	return pat_rand(pat_wrap(c, per) + pat_seed(seed)); \
} \
float pat_noise(float2 p, float2 per, float seed) { \
	float2 i = floor(p); \
	float2 f = p - i; \
	float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0); \
	float a = pat_cell_rand(i, per, seed); \
	float b = pat_cell_rand(i + float2(1.0, 0.0), per, seed); \
	float c = pat_cell_rand(i + float2(0.0, 1.0), per, seed); \
	float d = pat_cell_rand(i + float2(1.0, 1.0), per, seed); \
	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y); \
} \
float pat_gnoise(float2 p, float2 per, float seed) { \
	float2 i = floor(p); \
	float2 f = p - i; \
	float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0); \
	float a00 = pat_cell_rand(i, per, seed) * 6.28318530718; \
	float a10 = pat_cell_rand(i + float2(1.0, 0.0), per, seed) * 6.28318530718; \
	float a01 = pat_cell_rand(i + float2(0.0, 1.0), per, seed) * 6.28318530718; \
	float a11 = pat_cell_rand(i + float2(1.0, 1.0), per, seed) * 6.28318530718; \
	float n00 = dot(float2(cos(a00), sin(a00)), f); \
	float n10 = dot(float2(cos(a10), sin(a10)), f - float2(1.0, 0.0)); \
	float n01 = dot(float2(cos(a01), sin(a01)), f - float2(0.0, 1.0)); \
	float n11 = dot(float2(cos(a11), sin(a11)), f - float2(1.0, 1.0)); \
	return 0.5 + 0.7 * lerp(lerp(n00, n10, u.x), lerp(n01, n11, u.x), u.y); \
} \
float pat_fbm(float2 p, float2 per, float seed, float persistence) { \
	float sum = 0.0; \
	float amp = 1.0; \
	float total = 0.0; \
	float2 q = p; \
	float2 qp = per; \
	for (int ii = 0; ii < 5; ii += 1) { \
		sum = sum + amp * pat_gnoise(q, qp, seed); \
		total = total + amp; \
		q = q * 2.0; \
		qp = qp * 2.0; \
		amp = amp * persistence; \
	} \
	return sum / total; \
} \
float2 pat_cells(float4 t, float tile) { \
	float sx = max(t.x, 0.01); \
	float sy = max(t.x * t.y, 0.01); \
	if (tile > 0.5) { \
		sx = max(floor(sx + 0.5), 1.0); \
		sy = max(floor(sy + 0.5), 1.0); \
	} \
	return float2(sx, sy); \
} \
float pat_shrink(float per, float f) { \
	return max(floor(per / f + 0.5), 1.0) / per; \
} \
float2 pat_coord(float2 uv, float4 t, float4 d, float2 sc, float dper) { \
	float c = cos(t.z); \
	float s = sin(t.z); \
	float2 q = uv - float2(0.5, 0.5); \
	q = float2(q.x * c - q.y * s, q.x * s + q.y * c) + float2(0.5, 0.5); \
	float2 p = q * sc; \
	if (d.x > 0.0) { \
		float ds = max(floor(d.y + 0.5), 1.0); \
		float2 dp = float2(ds, ds) * dper; \
		float2 o = float2(pat_fbm(uv * ds, dp, t.w + 3.1, 0.5), pat_fbm(uv * ds, dp, t.w + 7.7, 0.5)); \
		p = p + (o - float2(0.5, 0.5)) * d.x * 4.0; \
	} \
	return p; \
} \
float pat_bevel(float d, float gap, float bevel) { \
	float x = clamp((d - gap) / max(bevel, 0.001), 0.0, 1.0); \
	return 1.0 - (1.0 - x) * (1.0 - x); \
} \
";

// a: persistence
static char *str_pattern_noise = "\
float2 pat_noise_fbm(float2 p, float2 per, float4 a, float seed) { \
	float h = pat_fbm(p, per, seed, a.x); \
	return float2(h, pat_noise(p, per, seed + 11.0)); \
} \
";

// a: edge width, randomness, variation
static char *str_pattern_cells = "\
float2 pat_voronoi(float2 p, float2 per, float4 a, float seed) { \
	float2 cell = floor(p); \
	float2 f = p - cell; \
	float f1 = 8.0; \
	float f2 = 8.0; \
	float2 nearest = cell; \
	for (int jj = 0; jj < 3; jj += 1) { \
		for (int kk = 0; kk < 3; kk += 1) { \
			float2 o = float2(float(kk) - 1.0, float(jj) - 1.0); \
			float2 rr = pat_rand2(pat_wrap(cell + o, per) + pat_seed(seed)); \
			float2 r = o + float2(0.5, 0.5) + (rr - float2(0.5, 0.5)) * a.y - f; \
			float d = dot(r, r); \
			if (d < f1) { \
				f2 = f1; \
				f1 = d; \
				nearest = cell + o; \
			} \
			else if (d < f2) { \
				f2 = d; \
			} \
		} \
	} \
	float cr = pat_cell_rand(nearest, per, seed + 1.0); \
	float edge = smoothstep(0.0, max(a.x, 0.001), sqrt(f2) - sqrt(f1)); \
	return float2(edge * (1.0 - a.z + a.z * cr), cr); \
} \
";

// a: rings, waviness
static char *str_pattern_grain = "\
float2 pat_grain(float2 p, float2 per, float4 a, float seed) { \
	float k = pat_shrink(per.x, 3.3); \
	float w = pat_fbm(float2(p.x * k, p.y * 2.0), float2(per.x * k, per.y * 2.0), seed, 0.5); \
	float rings = max(floor(a.x + 0.5), 1.0); \
	float ring = frac(p.y * rings + w * a.y); \
	float profile = smoothstep(0.0, 0.6, ring) * (1.0 - smoothstep(0.6, 1.0, ring)); \
	float fine = pat_noise(float2(p.x, p.y * 40.0), float2(per.x, per.y * 40.0), seed + 3.0); \
	return float2(0.35 + 0.65 * profile * (0.8 + 0.2 * fine), w); \
} \
";

// After Material Maker bricks2.mmg (rows of bricks, bevel), MIT, (c) Rodolphe Suescun and contributors.
// a: mortar, bevel, offset, variation
static char *str_pattern_bricks = "\
float2 pat_bricks(float2 p, float2 per, float4 a, float seed) { \
	float2 b = float2(p.x, p.y * 2.0); \
	float row = floor(b.y); \
	b.x = b.x + a.z * (row - 2.0 * floor(row * 0.5)); \
	float2 id = floor(b); \
	float2 f = b - id; \
	float2 e = min(f, float2(1.0, 1.0) - f) * float2(1.0, 0.5); \
	float cr = pat_cell_rand(id, float2(per.x, per.y * 2.0), seed); \
	float h = pat_bevel(min(e.x, e.y), a.x * 0.5, a.y * 0.5); \
	return float2(h * (1.0 - a.w + a.w * cr), cr); \
} \
";

// a: gap, bevel, variation
static char *str_pattern_tiles = "\
float2 pat_tiles(float2 p, float2 per, float4 a, float seed) { \
	float2 id = floor(p); \
	float2 f = p - id; \
	float2 e = min(f, float2(1.0, 1.0) - f); \
	float cr = pat_cell_rand(id, per, seed); \
	float h = pat_bevel(min(e.x, e.y), a.x * 0.5, a.y * 0.5); \
	return float2(h * (1.0 - a.z + a.z * cr), cr); \
} \
";

// a: thread width, fiber
static char *str_pattern_weave = "\
float2 pat_weave(float2 p, float2 per, float4 a, float seed) { \
	float2 q = p * 2.0; \
	float2 c = floor(q); \
	float2 f = q - c; \
	float alt = frac((c.x + c.y) * 0.5) * 2.0; \
	float h = lerp(sin(f.y * 3.14159), sin(f.x * 3.14159), alt); \
	float wd = max(a.x, 0.01); \
	h = clamp((h - (1.0 - wd)) / wd, 0.0, 1.0); \
	float fiber = lerp(sin(f.x * 18.85), sin(f.y * 18.85), alt) * 0.5 + 0.5; \
	float n = pat_noise(q * 4.0, per * 8.0, seed); \
	h = 0.2 + 0.8 * h * (1.0 - a.y + a.y * fiber) * (0.92 + 0.08 * n); \
	return float2(h, pat_cell_rand(c, per * 2.0, seed)); \
} \
";

// From Material Maker old_scratch / old_scratches (nodes/scratches.mmg), MIT, (c) Rodolphe Suescun and contributors. One
// scratch per cell and layer, the layers shift by random fractions of a cell.
// a: length, width, waviness, randomness
static char *str_pattern_scratches = "\
float pat_scratch(float2 p, float2 per, float4 a, float2 sd) { \
	float2 c = floor(p); \
	float2 r1 = pat_rand2(pat_wrap(c, per) + sd); \
	float2 r2 = pat_rand2(r1); \
	float2 uv = p - c; \
	float2 border = min(uv, float2(1.0, 1.0) - uv) * 10.0; \
	uv = uv * 2.0 - float2(1.0, 1.0); \
	float an = 6.28318530718 * (r1.x - 0.5) * a.w; \
	float co = cos(an); \
	float si = sin(an); \
	uv = float2(co * uv.x + si * uv.y, si * uv.x - co * uv.y); \
	uv.y = uv.y + 2.0 * r1.y - 1.0; \
	uv.y = uv.y + 0.5 * a.z * cos(2.0 * uv.x + 6.28318530718 * r2.y); \
	uv.x = uv.x / max(a.x, 0.05); \
	uv.y = uv.y / max(a.y, 0.05); \
	return clamp(min(border.x, border.y), 0.0, 1.0) * max(1.0 - uv.x * uv.x, 0.0) * max(0.0, 1.0 - 1000.0 * uv.y * uv.y); \
} \
float2 pat_scratches(float2 p, float2 per, float4 a, float seed) { \
	float v = 0.0; \
	float2 sd = pat_seed(seed) + float2(0.31, 0.77); \
	for (int ii = 0; ii < 4; ii += 1) { \
		sd = pat_rand2(sd); \
		v = max(v, pat_scratch(p + sd, per, a, sd)); \
	} \
	v = clamp(v, 0.0, 1.0); \
	return float2(1.0 - v, v); \
} \
";

// After Material Maker beehive.mmg (beehive_dist, beehive_center), MIT, (c) Rodolphe Suescun and contributors.
// a: gap, bevel, variation
static char *str_pattern_hex = "\
float2 pat_hex(float2 p, float2 per, float4 a, float seed) { \
	float2 s = float2(1.0, 1.73205080757); \
	float2 q = float2(p.x, p.y * 1.73205080757); \
	float4 hc = floor(float4(q.x, q.y, q.x - 0.5, q.y - 1.0) / float4(s.x, s.y, s.x, s.y)) + float4(0.5, 0.5, 0.5, 0.5); \
	float2 h1 = q - hc.xy * s; \
	float2 h2 = q - (hc.zw + float2(0.5, 0.5)) * s; \
	float2 hp = h1; \
	float2 id = hc.xy; \
	if (dot(h2, h2) < dot(h1, h1)) { \
		hp = h2; \
		id = hc.zw + float2(9.73, 9.73); \
	} \
	float2 ap = abs(hp); \
	float dist = max(dot(ap, s * 0.5), ap.x); \
	float cr = pat_rand(id + pat_seed(seed)); \
	float h = pat_bevel(0.5 - dist, a.x * 0.5, a.y * 0.5); \
	return float2(h * (1.0 - a.z + a.z * cr), cr); \
} \
";

// Round pebbles scattered one per cell, the highest wins where they overlap
// a: size, size jitter, softness, variation
static char *str_pattern_pebbles = "\
float2 pat_pebbles(float2 p, float2 per, float4 a, float seed) { \
	float2 cell = floor(p); \
	float2 f = p - cell; \
	float h = 0.0; \
	float cid = 0.0; \
	for (int jj = 0; jj < 3; jj += 1) { \
		for (int kk = 0; kk < 3; kk += 1) { \
			float2 o = float2(float(kk) - 1.0, float(jj) - 1.0); \
			float2 id = pat_wrap(cell + o, per) + pat_seed(seed); \
			float2 rr = pat_rand2(id); \
			float rs = pat_rand(id + float2(7.3, 1.9)); \
			float rv = pat_rand(id + float2(3.7, 5.1)); \
			float rad = max(a.x * (1.0 - a.y * rs), 0.02); \
			float2 d = o + float2(0.2, 0.2) + rr * 0.6 - f; \
			float dist = sqrt(dot(d, d)) / rad; \
			float dome = sqrt(max(1.0 - dist * dist, 0.0)); \
			float edge = clamp((1.0 - dist) / max(a.z, 0.01), 0.0, 1.0); \
			float v = min(dome, edge) * (1.0 - a.w + a.w * rv); \
			if (v > h) { \
				h = v; \
				cid = rv; \
			} \
		} \
	} \
	return float2(h, cid); \
} \
";

typedef struct pattern_def {
	char *type;
	char *name;
	char *fn;
	char **src;
	f32   scale;
	i32   extras;
	char *extra[PATTERN_EXTRA_INPUTS];
	f32   value[PATTERN_EXTRA_INPUTS];
	f32   max[PATTERN_EXTRA_INPUTS];
} pattern_def_t;

// clang-format off
static pattern_def_t pattern_defs[] = {
	{"PATTERN_NOISE",     "Noise",     "pat_noise_fbm", &str_pattern_noise,     8.0,  1, {"Persistence"},                                   {0.5},                   {1.0}},
	{"PATTERN_CELLS",     "Cells",     "pat_voronoi",   &str_pattern_cells,     5.0,  3, {"Edge Width", "Randomness", "Variation"},         {0.12, 1.0, 0.3},        {1.0, 1.0, 1.0}},
	{"PATTERN_GRAIN",     "Wood Grain","pat_grain",     &str_pattern_grain,     3.0,  2, {"Rings", "Waviness"},                             {2.0, 3.0},              {16.0, 8.0}},
	{"PATTERN_BRICKS",    "Bricks",    "pat_bricks",    &str_pattern_bricks,    6.0,  4, {"Mortar", "Bevel", "Offset", "Variation"},        {0.06, 0.06, 0.5, 0.15}, {0.5, 0.5, 1.0, 1.0}},
	{"PATTERN_TILES",     "Tiles",     "pat_tiles",     &str_pattern_tiles,     8.0,  3, {"Gap", "Bevel", "Variation"},                     {0.05, 0.05, 0.08},      {0.5, 0.5, 1.0}},
	{"PATTERN_HEX",       "Hexagons",  "pat_hex",       &str_pattern_hex,       8.0,  3, {"Gap", "Bevel", "Variation"},                     {0.06, 0.08, 0.1},       {0.5, 0.5, 1.0}},
	{"PATTERN_WEAVE",     "Weave",     "pat_weave",     &str_pattern_weave,     20.0, 2, {"Thread Width", "Fiber"},                         {0.9, 0.2},              {1.0, 1.0}},
	{"PATTERN_PEBBLES",   "Pebbles",   "pat_pebbles",   &str_pattern_pebbles,   8.0,  4, {"Size", "Size Jitter", "Softness", "Variation"},  {0.45, 0.4, 0.3, 0.4},   {1.0, 1.0, 1.0, 1.0}},
	{"PATTERN_SCRATCHES", "Scratches", "pat_scratches", &str_pattern_scratches, 4.0,  4, {"Length", "Width", "Waviness", "Randomness"},     {0.5, 1.0, 0.5, 0.5},    {1.0, 4.0, 1.0, 1.0}},
};
// clang-format on

#define PATTERN_DEF_COUNT ((i32)(sizeof(pattern_defs) / sizeof(pattern_defs[0])))

i32 pattern_node_count() {
	return PATTERN_DEF_COUNT;
}

char *pattern_node_type(i32 i) {
	return pattern_defs[i].type;
}

char *pattern_node_name(i32 i) {
	return tr(pattern_defs[i].name);
}

i32 pattern_node_index(char *type) {
	for (i32 i = 0; i < PATTERN_DEF_COUNT; ++i) {
		if (string_equals(pattern_defs[i].type, type)) {
			return i;
		}
	}
	return -1;
}

bool pattern_node_is(ui_node_t *node) {
	return node != NULL && pattern_node_index(node->type) >= 0;
}

static char *pattern_eval(ui_node_t *node) {
	char *store = parser_material_store_var_name(node);
	if (string_array_index_of(parser_material_parsed, store) >= 0) {
		return store;
	}
	any_array_push(parser_material_parsed, store);
	pattern_def_t *def = &pattern_defs[pattern_node_index(node->type)];
	node_shader_add_function(parser_material_kong, str_pattern_lib);

	ui_node_link_t *vec_link = parser_material_get_input_link(node->inputs->buffer[0]);
	char           *vec      = vec_link != NULL ? string_copy(parser_material_parse_vector_input(node->inputs->buffer[0])) : NULL;
	char           *in[PATTERN_COMMON_INPUTS + PATTERN_EXTRA_INPUTS];
	for (i32 i = 1; i < PATTERN_COMMON_INPUTS + PATTERN_EXTRA_INPUTS; ++i) {
		in[i] = i < node->inputs->length ? string_copy(parser_material_parse_value_input(node->inputs->buffer[i], false)) : string_copy("0.0");
	}
	parser_material_write(parser_material_kong, string_tmp("float4 %s_t = float4(%s, %s, %s * 0.0174533, %s);", store, in[1], in[2], in[3], in[5]));
	parser_material_write(parser_material_kong, string_tmp("float4 %s_d = float4(%s, %s, 0.0, 0.0);", store, in[6], in[7]));
	parser_material_write(parser_material_kong, string_tmp("float4 %s_a = float4(%s, %s, %s, %s);", store, in[8], in[9], in[10], in[11]));

	bool triplanar = vec == NULL && node->buttons->length > 0 && node->buttons->buffer[0]->default_value->buffer[0] > 0.5;
	if (triplanar) {
		parser_material_kong->frag_bposition = true;
		parser_material_kong->frag_n         = true;
		parser_material_write(parser_material_kong, string_tmp("float3 %s_w = abs(n);", store));
		parser_material_write(parser_material_kong, string_tmp("%s_w = %s_w / max(%s_w.x + %s_w.y + %s_w.z, 0.0001);", store, store, store, store, store));
		parser_material_write(parser_material_kong, string_tmp("float2 %s_sc = pat_cells(%s_t, 0.0);", store, store));
		char *planes[3] = {"bposition.yz", "bposition.xz", "bposition.xy"};
		char *w[3]      = {"x", "y", "z"};
		parser_material_write(parser_material_kong, string_tmp("float2 %s_r = float2(0.0, 0.0);", store));
		for (i32 i = 0; i < 3; ++i) {
			parser_material_write(parser_material_kong,
			                      string_tmp("%s_r = %s_r + %s(pat_coord(%s, %s_t, %s_d, %s_sc, 1000.0), float2(4096.0, 4096.0), %s_a, %s_t.w) * %s_w.%s;",
			                                 store, store, def->fn, planes[i], store, store, store, store, store, store, w[i]));
		}
	}
	else {
		char *uv = vec;
		if (uv == NULL) {
			node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");
			uv = "tex_coord";
		}
		else {
			uv = string_tmp("%s.xy", vec);
		}
		parser_material_write(parser_material_kong, string_tmp("float2 %s_sc = pat_cells(%s_t, 1.0);", store, store));
		parser_material_write(parser_material_kong, string_tmp("float2 %s_r = %s(pat_coord(%s, %s_t, %s_d, %s_sc, 1.0), %s_sc, %s_a, %s_t.w);", store, def->fn,
		                                                       uv, store, store, store, store, store, store));
	}
	parser_material_write(parser_material_kong, string_tmp("float2 %s = float2(clamp((%s_r.x - 0.5) * %s + 0.5, 0.0, 1.0), %s_r.y);", store, store, in[4], store));

	for (i32 i = 1; i < PATTERN_COMMON_INPUTS + PATTERN_EXTRA_INPUTS; ++i) {
		free(in[i]);
	}
	if (vec != NULL) {
		free(vec);
	}
	return store;
}

char *pattern_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *store = pattern_eval(node);
	return string_tmp(socket == node->outputs->buffer[1] ? "%s.y" : "%s.x", store);
}

char *pattern_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *store = pattern_eval(node);
	return string_tmp(socket == node->outputs->buffer[1] ? "float3(%s.y, %s.y, %s.y)" : "float3(%s.x, %s.x, %s.x)", store, store, store);
}

ui_node_socket_t *nodes_material_socket_def(char *name, char *type, f32_array_t *value, f32 min, f32 max) {
	u32 color = string_equals(type, "VALUE") ? 0xffa1a1a1 : string_equals(type, "VECTOR") ? 0xff6363c7 : 0xffc7c729;
	return ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                     .node_id       = 0,
	                                     .name          = name,
	                                     .type          = type,
	                                     .color         = color,
	                                     .default_value = value,
	                                     .min           = min,
	                                     .max           = max,
	                                     .precision     = 100,
	                                     .display       = 0});
}

ui_node_button_t *nodes_material_button_def(char *name, char *type, f32 value, char *data) {
	return ALLOC_INIT(ui_node_button_t, {.name          = name,
	                                     .type          = type,
	                                     .output        = 0,
	                                     .default_value = f32_array_create_x(value),
	                                     .data          = data != NULL ? u8_array_create_from_string(data) : NULL,
	                                     .min           = 0.0,
	                                     .max           = 1.0,
	                                     .precision     = 100,
	                                     .height        = 0});
}

// The shared functions and every pattern in one string: the shader functions are kept in a map and written in no set order,
// so functions calling each other go together
char *str_pattern_lib = NULL;

void pattern_nodes_init() {
	if (str_pattern_lib == NULL) {
		size_t size = strlen(str_pattern_common) + 1;
		for (i32 i = 0; i < PATTERN_DEF_COUNT; ++i) {
			size += strlen(*pattern_defs[i].src);
		}
		str_pattern_lib = malloc(size);
		strcpy(str_pattern_lib, str_pattern_common);
		for (i32 i = 0; i < PATTERN_DEF_COUNT; ++i) {
			strcat(str_pattern_lib, *pattern_defs[i].src);
		}
	}
	char *mapping = string_tmp("%s\n%s", _tr("UV"), _tr("Triplanar"));
	for (i32 i = 0; i < PATTERN_DEF_COUNT; ++i) {
		pattern_def_t *d      = &pattern_defs[i];
		any_array_t   *inputs = any_array_create_from_raw(
            (void *[]){
                nodes_material_socket_def(_tr("Vector"), "VECTOR", f32_array_create_xyz(0.0, 0.0, 0.0), 0.0, 1.0),
                nodes_material_socket_def(_tr("Scale"), "VALUE", f32_array_create_x(d->scale), 1.0, 100.0),
                nodes_material_socket_def(_tr("Stretch"), "VALUE", f32_array_create_x(1.0), 1.0, 50.0),
                nodes_material_socket_def(_tr("Angle"), "VALUE", f32_array_create_x(0.0), 0.0, 180.0),
                nodes_material_socket_def(_tr("Contrast"), "VALUE", f32_array_create_x(1.0), 0.2, 4.0),
                nodes_material_socket_def(_tr("Seed"), "VALUE", f32_array_create_x(0.0), 0.0, 1000.0),
                nodes_material_socket_def(_tr("Distort"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
                nodes_material_socket_def(_tr("Distort Scale"), "VALUE", f32_array_create_x(4.0), 1.0, 32.0),
            },
            PATTERN_COMMON_INPUTS);
		for (i32 j = 0; j < d->extras; ++j) {
			any_array_push(inputs, nodes_material_socket_def(_tr(d->extra[j]), "VALUE", f32_array_create_x(d->value[j]), 0.0, d->max[j]));
		}
		ui_node_t *def = ALLOC_INIT(ui_node_t, {.id      = 0,
		                                        .name    = _tr(d->name),
		                                        .type    = d->type,
		                                        .x       = 0,
		                                        .y       = 0,
		                                        .color   = 0xff4982a0,
		                                        .inputs  = inputs,
		                                        .outputs = any_array_create_from_raw(
		                                            (void *[]){
		                                                nodes_material_socket_def(_tr("Height"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
		                                                nodes_material_socket_def(_tr("Cell"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
		                                            },
		                                            2),
		                                        .buttons = any_array_create_from_raw(
		                                            (void *[]){
		                                                nodes_material_button_def(_tr("Mapping"), "ENUM", 0, mapping),
		                                            },
		                                            1),
		                                        .width = 0,
		                                        .flags = 0});
		any_array_push(nodes_material_texture, def);
		any_map_set(parser_material_node_values, d->type, pattern_node_value);
		any_map_set(parser_material_node_vectors, d->type, pattern_node_vector);
	}
}
