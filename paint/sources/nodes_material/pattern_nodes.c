#include "../global.h"

// Pattern nodes: tileable procedural patterns in the style of Material Maker. Every pattern node has the same first inputs
// (Vector, Scale, Stretch, Angle, Contrast, Seed, Distort, Distort Scale), up to four of its own after them, a Mapping
// button (UV or Triplanar) and two outputs: Height (1 on the surface, 0 in the recesses) and Cell (a random value for each
// brick, tile, cell..). With UV mapping the cell counts snap to whole numbers so the pattern wraps at the uv borders.
//
// The noise, hash and scratch functions are ported from Material Maker (https://github.com/RodZill4/material-maker),
// MIT License, Copyright (c) 2018-present Rodolphe Suescun and contributors. See paint/THIRD_PARTY_NOTICES.md.

#define PATTERN_COMMON_INPUTS 8
#define PATTERN_EXTRA_INPUTS  6

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
float2 pat_rotate(float2 v, float an) { \
	float c = cos(an); \
	float s = sin(an); \
	return float2(v.x * c - v.y * s, v.x * s + v.y * c); \
} \
float pat_box(float2 q, float2 hs) { \
	float2 d = abs(q) - hs; \
	return max(d.x, d.y); \
} \
float pat_bevel(float d, float gap, float bevel) { \
	float x = clamp((d - gap) / max(bevel, 0.001), 0.0, 1.0); \
	return 1.0 - (1.0 - x) * (1.0 - x); \
} \
";

// a: persistence, ridged (folds the noise into thin ridges, veins)
static char *str_pattern_noise = "\
float2 pat_noise_fbm(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float h = pat_fbm(p, per, seed, a.x); \
	float hn = clamp((h - 0.5) * 2.5 + 0.5, 0.0, 1.0); \
	h = lerp(h, 1.0 - abs(hn * 2.0 - 1.0), a.y); \
	return float2(h, pat_noise(p, per, seed + 11.0)); \
} \
";

// a: edge width, randomness, variation, rounding (domes the cells towards their center)
static char *str_pattern_cells = "\
float2 pat_voronoi(float2 p, float2 per, float4 a, float4 a2, float seed) { \
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
	float dome = clamp(1.0 - sqrt(f1) / 0.75, 0.0, 1.0); \
	edge = edge * (1.0 - a.w * (1.0 - dome)); \
	return float2(edge * (1.0 - a.z + a.z * cr), cr); \
} \
";

// a: rings, waviness
static char *str_pattern_grain = "\
float2 pat_grain(float2 p, float2 per, float4 a, float4 a2, float seed) { \
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
float2 pat_bricks(float2 p, float2 per, float4 a, float4 a2, float seed) { \
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
float2 pat_tiles(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 id = floor(p); \
	float2 f = p - id; \
	float2 e = min(f, float2(1.0, 1.0) - f); \
	float cr = pat_cell_rand(id, per, seed); \
	float checker = frac((id.x + id.y) * 0.5) * 2.0; \
	cr = lerp(cr, checker * 0.8 + cr * 0.2, a.w); \
	float h = pat_bevel(min(e.x, e.y), a.x * 0.5, a.y * 0.5); \
	return float2(h * (1.0 - a.z + a.z * cr), cr); \
} \
";

// a: thread width, fiber, twill (threads go over two and under two, the crossings step along a diagonal)
static char *str_pattern_weave = "\
float2 pat_weave(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 q = p * 2.0; \
	float2 c = floor(q); \
	float2 f = q - c; \
	float alt = frac((c.x + c.y) * 0.5) * 2.0; \
	if (a.z > 0.5) { \
		alt = floor(frac((c.x + c.y) * 0.25) * 2.0); \
	} \
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
float2 pat_scratches(float2 p, float2 per, float4 a, float4 a2, float seed) { \
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
float2 pat_hex(float2 p, float2 per, float4 a, float4 a2, float seed) { \
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
float2 pat_pebbles(float2 p, float2 per, float4 a, float4 a2, float seed) { \
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


// Dots on a grid: rivets with no randomness, spots, pores and lights with some, density leaves cells empty
// a: size, randomness, density, softness; a2: offset x, offset y
static char *str_pattern_dots = "\
float2 pat_dots(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 q = p + float2(a2.x, a2.y); \
	float2 cell = floor(q); \
	float2 f = q - cell; \
	float h = 0.0; \
	float cid = 0.0; \
	for (int jj = 0; jj < 3; jj += 1) { \
		for (int kk = 0; kk < 3; kk += 1) { \
			float2 o = float2(float(kk) - 1.0, float(jj) - 1.0); \
			float2 id = pat_wrap(cell + o, per) + pat_seed(seed); \
			float2 rr = pat_rand2(id); \
			float rs = pat_rand(id + float2(7.3, 1.9)); \
			float rv = pat_rand(id + float2(3.7, 5.1)); \
			if (rv < a.z) { \
				float rad = max(a.x * 0.5 * (1.0 - a.y * 0.5 * rs), 0.01); \
				float2 d = o + float2(0.5, 0.5) + (rr - float2(0.5, 0.5)) * a.y * (1.0 - a.x) - f; \
				float dist = sqrt(dot(d, d)) / rad; \
				float dome = sqrt(max(1.0 - dist * dist, 0.0)); \
				float edge = clamp((1.0 - dist) / max(a.w, 0.01), 0.0, 1.0); \
				float v = min(dome, edge); \
				if (v > h) { \
					h = v; \
					cid = rs; \
				} \
			} \
		} \
	} \
	return float2(h, cid); \
} \
";

// Bands across the pattern: a sine, saw or square profile, wavy if asked
// a: profile (0 sine, 1 saw, 2 square), width (of the square band), waviness, wave count
static char *str_pattern_stripes = "\
float2 pat_stripes(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float k = pat_shrink(per.x, max(a.w, 0.1)); \
	float wv = sin(p.x * k * 6.28318530718 + pat_rand(pat_seed(seed)) * 6.28318530718) * a.z * 0.5; \
	float v = p.y + wv + abs(frac(p.x) - 0.5) * 2.0 * a2.x; \
	float t = v - floor(v); \
	float h = 0.5 + 0.5 * sin(t * 6.28318530718); \
	if (a.x > 0.5) { \
		h = t; \
	} \
	if (a.x > 1.5) { \
		float e = 0.03; \
		h = smoothstep(0.0, e, t) * (1.0 - smoothstep(a.y, a.y + e, t)); \
	} \
	return float2(h, pat_cell_rand(float2(0.0, floor(v)), float2(4096.0, per.y), seed)); \
} \
";

// Hair: thin strands along the pattern, gathered in clumps that are taller at their centers, bent a little
// a: length, clumping, clump size, bend
static char *str_pattern_fibers = "\
float2 pat_fibers(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float bend = (pat_gnoise(p * float2(0.25, 0.25 / max(a.x, 1.0)), per * float2(0.25, 0.25), seed + 5.0) - 0.5) * a.w * 2.0; \
	float2 q = float2(p.x + bend, p.y); \
	float ky = pat_shrink(per.y, max(a.x, 1.0)); \
	float s1 = pat_gnoise(float2(q.x, q.y * ky), float2(per.x, per.y * ky), seed); \
	float s2 = pat_gnoise(float2(q.x, q.y * ky) * 2.0, float2(per.x, per.y * ky) * 2.0, seed + 2.0); \
	float strands = max(smoothstep(0.42, 0.68, s1), smoothstep(0.45, 0.7, s2) * 0.6); \
	float cs = max(a.z, 1.0); \
	float kx = pat_shrink(per.x, cs); \
	float kc = pat_shrink(per.y, cs * max(a.x, 1.0) * 0.5); \
	float2 cq = float2(q.x * kx, q.y * kc); \
	float2 cper = float2(per.x * kx, per.y * kc); \
	float2 cell = floor(cq); \
	float2 f = cq - cell; \
	float f1 = 8.0; \
	float cid = 0.0; \
	for (int jj = 0; jj < 3; jj += 1) { \
		for (int kk = 0; kk < 3; kk += 1) { \
			float2 o = float2(float(kk) - 1.0, float(jj) - 1.0); \
			float2 rr = pat_rand2(pat_wrap(cell + o, cper) + pat_seed(seed)); \
			float2 r = o + rr - f; \
			float d = dot(r, r); \
			if (d < f1) { \
				f1 = d; \
				cid = rr.x; \
			} \
		} \
	} \
	float clump = clamp(1.0 - sqrt(f1) / 0.8, 0.0, 1.0); \
	return float2(strands * (1.0 - a.y + a.y * clump), cid); \
} \
";

// Floor boards: rows of long boards with staggered ends, grain running along each board
// a: gap, bevel, board length (cells), grain
static char *str_pattern_planks = "\
float2 pat_planks(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float row = floor(p.y); \
	float len = max(floor(a.z + 0.5), 1.0); \
	float off = floor(pat_cell_rand(float2(0.0, row), float2(1.0, per.y), seed + 2.0) * len); \
	float x = p.x + off; \
	float bx = floor(x / len); \
	float2 id = float2(bx, row); \
	float fx = (x - bx * len) / len; \
	float fy = p.y - row; \
	float d = min(min(fx, 1.0 - fx) * len, min(fy, 1.0 - fy)); \
	float cr = pat_cell_rand(id, float2(max(floor(per.x / len + 0.5), 1.0), per.y), seed); \
	float h = pat_bevel(d, a.x * 0.5, a.y * 0.5); \
	float g = pat_gnoise(float2(x * 0.35, fy * 3.0 + cr * 37.0), float2(4096.0, 4096.0), seed + 4.0); \
	float grain = 0.5 + 0.5 * sin((fy * 5.0 + g * 4.0 + cr * 3.0) * 6.28318530718); \
	return float2(h * (1.0 - a.w * 0.4 + a.w * 0.4 * grain), cr); \
} \
";

// Roof tiles: overlapping rows, each tile rises to its rounded or square lower lip, the tile above shades its top
// a: roundness, gap, slope, variation
static char *str_pattern_shingles = "\
float2 pat_shingles(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float row = floor(p.y); \
	float x = p.x + 0.5 * (row - 2.0 * floor(row * 0.5)); \
	float col = floor(x); \
	float fx = x - col; \
	float fy = p.y - row; \
	float cx = fx * 2.0 - 1.0; \
	float lip = a.x * 0.45 * (1.0 - sqrt(max(1.0 - cx * cx, 0.0))); \
	float2 id = float2(col, row); \
	float h = 0.0; \
	if (fy < lip) { \
		float r2 = row - 1.0; \
		float x2 = p.x + 0.5 * (r2 - 2.0 * floor(r2 * 0.5)); \
		id = float2(floor(x2), r2); \
		h = 0.15 * (1.0 - fy / max(lip, 0.001)); \
	} \
	else { \
		float v = (fy - lip) / max(1.0 - lip, 0.001); \
		h = 1.0 - v * a.z; \
		float e = min(fx, 1.0 - fx); \
		h = h * pat_bevel(e, a.y * 0.5, 0.06); \
		h = h * pat_bevel(fy - lip, 0.0, 0.05); \
	} \
	float cr = pat_cell_rand(id, per, seed); \
	return float2(h * (1.0 - a.w + a.w * cr), cr); \
} \
";

// Herringbone: boards one wide and length long, horizontal and vertical ones in stair steps. The cells of the lattice
// (1, 1), (length, -length) hold one board of each.
// a: gap, bevel, length, variation
static char *str_pattern_herringbone = "\
float2 pat_herringbone(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float bl = max(floor(a.z + 0.5), 2.0); \
	float2 c = floor(p); \
	float2 f = p - c; \
	float sd = c.x - c.y; \
	float band = floor(sd / (2.0 * bl)); \
	float sm = sd - band * 2.0 * bl; \
	float along = 0.0; \
	float across = 0.0; \
	float2 id = float2(0.0, 0.0); \
	if (sm < bl) { \
		along = sm + f.x; \
		across = f.y; \
		id = float2(band, c.y); \
	} \
	else { \
		along = 2.0 * bl - 1.0 - sm + f.y; \
		across = f.x; \
		id = float2(band + 517.0, c.x); \
	} \
	float d = min(min(along, bl - along), min(across, 1.0 - across)); \
	float cr = pat_rand(id + pat_seed(seed)); \
	float h = pat_bevel(d, a.x * 0.5, a.y * 0.5); \
	return float2(h * (1.0 - a.w + a.w * cr), cr); \
} \
";

// Parquet: square blocks of strips, the strips of neighbor blocks cross each other
// a: strips, gap, bevel, variation
static char *str_pattern_parquet = "\
float2 pat_parquet(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float n = max(floor(a.x + 0.5), 1.0); \
	float2 b = floor(p); \
	float2 f = p - b; \
	float flip = frac((b.x + b.y) * 0.5) * 2.0; \
	float u = lerp(f.x, f.y, flip); \
	float v = lerp(f.y, f.x, flip); \
	float strip = floor(v * n); \
	float acr = v * n - strip; \
	float d = min(min(acr, 1.0 - acr), min(u, 1.0 - u) * n); \
	float cr = pat_rand(float2(b.x * 7.0 + strip, b.y) + pat_seed(seed)); \
	float h = pat_bevel(d, a.y * 0.5, a.z * 0.5); \
	return float2(h * (1.0 - a.w + a.w * cr), cr); \
} \
";

// Uneven bricks: each cell split in two again and again, across its longer side mostly, at a random place
// a: gap, bevel, splits, variation
static char *str_pattern_uneven = "\
float2 pat_uneven(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 c = pat_wrap(floor(p), per); \
	float2 f = p - floor(p); \
	float2 lo = float2(0.0, 0.0); \
	float2 hi = float2(1.0, 1.0); \
	for (int ii = 0; ii < 6; ii += 1) { \
		if (float(ii) < a.z) { \
			float2 sz = hi - lo; \
			float r1 = pat_rand(c * 1.31 + lo * 7.1 + hi * 3.3 + pat_seed(seed)); \
			float r2 = pat_rand(c * 2.17 + lo * 1.9 + hi * 5.7 + pat_seed(seed + 1.0)); \
			float split = 0.3 + 0.4 * r2; \
			if (sz.x * (0.6 + 0.8 * r1) > sz.y) { \
				float m = lo.x + sz.x * split; \
				if (f.x < m) { \
					hi.x = m; \
				} \
				else { \
					lo.x = m; \
				} \
			} \
			else { \
				float m = lo.y + sz.y * split; \
				if (f.y < m) { \
					hi.y = m; \
				} \
				else { \
					lo.y = m; \
				} \
			} \
		} \
	} \
	float2 e = min(f - lo, hi - f); \
	float cr = pat_rand(c + (lo + hi) * 3.7 + pat_seed(seed + 2.0)); \
	float h = pat_bevel(min(e.x, e.y), a.x * 0.5, a.y * 0.5); \
	return float2(h * (1.0 - a.w + a.w * cr), cr); \
} \
";

// Fans of overlapping circles in staggered rows, the lower rows over the upper ones: rings (seigaiha), or stones along the
// rings (arc pavement) when segments is above 0
// a: rings, radius, gap, variation
static char *str_pattern_fan = "\
float2 pat_fan_rows(float2 p, float2 per, float4 a, float seed, float segments) { \
	float rad = max(a.y, 0.3); \
	float rs = rad * 0.5; \
	float jb = floor(p.y / rs); \
	float found = 0.0; \
	float h = 0.0; \
	float cr = 0.0; \
	for (int ii = 0; ii < 5; ii += 1) { \
		float j = jb - 2.0 + float(ii); \
		float off = 0.5 * (j - 2.0 * floor(j * 0.5)); \
		float cx = floor(p.x - off + 0.5) + off; \
		float2 d = p - float2(cx, j * rs); \
		float r = sqrt(dot(d, d)) / rad; \
		if (found < 0.5 && r < 1.0 && d.y > -rad * 0.05) { \
			found = 1.0; \
			float rings = max(floor(a.x + 0.5), 1.0); \
			float rr = r * rings; \
			float ring = floor(rr); \
			float fr = rr - ring; \
			float dr = min(fr, 1.0 - fr) / rings * rad; \
			cr = pat_rand(float2(cx, j + ring * 13.0) + pat_seed(seed)); \
			if (segments > 0.5) { \
				float an = atan2(d.x, max(d.y, 0.0001)) / 3.14159265 + 0.5; \
				float cnt = max(floor(segments * (ring + 1.0) + 0.5), 1.0); \
				float sa = an * cnt; \
				float sf = sa - floor(sa); \
				float da = min(sf, 1.0 - sf) / cnt * 3.14159265 * r * rad; \
				dr = min(dr, da); \
				cr = pat_rand(float2(cx + floor(sa) * 7.3, j + ring * 13.0) + pat_seed(seed)); \
			} \
			h = pat_bevel(dr, a.z * 0.5, a.z * 0.5 + 0.03); \
		} \
	} \
	return float2(h * (1.0 - a.w + a.w * cr), cr); \
} \
float2 pat_seigaiha(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	return pat_fan_rows(p, per, a, seed, 0.0); \
} \
float2 pat_arcs(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	return pat_fan_rows(p, per, a, seed, 2.0); \
} \
";

// Eight pointed stars, two squares turned 45 degrees, and the crosses between them
// a: star size, gap, bevel, variation
static char *str_pattern_stars = "\
float2 pat_stars(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 c = floor(p); \
	float2 q = p - c - float2(0.5, 0.5); \
	float hs = a.x * 0.5; \
	float star = min(pat_box(q, float2(hs, hs)), pat_box(pat_rotate(q, 0.785398), float2(hs, hs))); \
	float2 e = min(p - c, float2(1.0, 1.0) - (p - c)); \
	float d = min(abs(star), min(e.x, e.y) + max(-star, 0.0) * 4.0); \
	float inside = 1.0 - step(0.0, star); \
	float cr = pat_cell_rand(c, per, seed); \
	float h = pat_bevel(d, a.y * 0.5, a.z * 0.5); \
	return float2(h * (1.0 - a.w + a.w * cr), inside * 0.6 + 0.4 * cr); \
} \
";

// Truchet: quarter circles in the corners of each cell, turned at random, they join into winding bands
// a: width, softness, variation
static char *str_pattern_truchet = "\
float2 pat_truchet(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 c = floor(p); \
	float2 f = p - c; \
	float cr = pat_cell_rand(c, per, seed); \
	if (cr > 0.5) { \
		f.x = 1.0 - f.x; \
	} \
	float d1 = abs(sqrt(dot(f, f)) - 0.5); \
	float2 g = f - float2(1.0, 1.0); \
	float d2 = abs(sqrt(dot(g, g)) - 0.5); \
	float d = min(d1, d2); \
	float w = a.x * 0.5; \
	float h = 1.0 - smoothstep(w - max(a.y, 0.005) * w, w, d); \
	h = h * sqrt(max(1.0 - (d / max(w, 0.001)) * (d / max(w, 0.001)), 0.0)); \
	return float2(h * (1.0 - a.z + a.z * cr), cr); \
} \
";

// Ornaments: a flower in each cell of staggered rows, petals around a round heart (damask, floral wallpaper)
// a: petals, size, heart, softness
static char *str_pattern_motif = "\
float2 pat_motif(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float row = floor(p.y); \
	float2 q = p; \
	q.x = q.x + 0.5 * (row - 2.0 * floor(row * 0.5)); \
	float2 c = floor(q); \
	float2 d = q - c - float2(0.5, 0.5); \
	float r = sqrt(dot(d, d)) * 2.0; \
	float an = atan2(d.y, d.x); \
	float n = max(floor(a.x + 0.5), 2.0); \
	float petal = a.y * (0.55 + 0.45 * abs(cos(an * n * 0.5))); \
	float soft = max(a.w, 0.01); \
	float flower = clamp((petal - r) / soft, 0.0, 1.0); \
	float heart = clamp((a.z * a.y - r) / soft, 0.0, 1.0); \
	float vein = 1.0 - 0.35 * pow(abs(sin(an * n * 0.5)), 30.0) * step(a.z * a.y, r); \
	float h = max(flower * 0.7 * vein, heart); \
	return float2(h, pat_cell_rand(c, per, seed)); \
} \
";

// Thatch: rows of straw bundles laid over each other, ragged at their lower edge
// a: strand density, slope, ragged, variation
static char *str_pattern_thatch = "\
float2 pat_thatch(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float row = floor(p.y); \
	float fy = p.y - row; \
	float rag = (pat_gnoise(float2(p.x * 6.0, row * 3.1), float2(4096.0, 4096.0), seed) - 0.5) * a.z; \
	float v = clamp(fy + rag, 0.0, 1.0); \
	float strands = pat_gnoise(float2(p.x * a.x, fy * 2.0 + row * 7.0), float2(4096.0, 4096.0), seed + 1.0); \
	strands = strands * 0.6 + 0.4 * pat_gnoise(float2(p.x * a.x * 2.3, fy * 3.0 + row * 3.0), float2(4096.0, 4096.0), seed + 2.0); \
	float h = (1.0 - v * a.y) * (0.45 + 0.55 * smoothstep(0.3, 0.7, strands)); \
	float cr = pat_rand(float2(floor(p.x * 2.0), row) + pat_seed(seed)); \
	return float2(h * (1.0 - a.w + a.w * cr), cr); \
} \
";

// Rings: round log ends packed together, growth rings inside a bark rim; with a large size, banded stone like malachite
// a: size, rings, wobble, bark
static char *str_pattern_rings = "\
float2 pat_rings(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 cell = floor(p); \
	float2 f = p - cell; \
	float f1 = 8.0; \
	float2 nearest = cell; \
	float2 nd = float2(0.0, 0.0); \
	for (int jj = 0; jj < 3; jj += 1) { \
		for (int kk = 0; kk < 3; kk += 1) { \
			float2 o = float2(float(kk) - 1.0, float(jj) - 1.0); \
			float2 rr = pat_rand2(pat_wrap(cell + o, per) + pat_seed(seed)); \
			float2 r = o + float2(0.25, 0.25) + rr * 0.5 - f; \
			float d = dot(r, r); \
			if (d < f1) { \
				f1 = d; \
				nearest = cell + o; \
				nd = r; \
			} \
		} \
	} \
	float cr = pat_cell_rand(nearest, per, seed + 1.0); \
	float rad = max(a.x * (0.8 + 0.4 * cr), 0.05); \
	float r = sqrt(f1) / rad; \
	float wob = (pat_gnoise(nd * 6.0 + nearest * 3.0, float2(4096.0, 4096.0), seed + 2.0) - 0.5) * a.z; \
	float rings = frac(r * a.y + wob); \
	float ringh = 0.7 + 0.3 * smoothstep(0.0, 0.25, rings) * (1.0 - smoothstep(0.75, 1.0, rings)); \
	float bark = smoothstep(1.0 - a.w, 1.0 - a.w * 0.5, r); \
	float inside = 1.0 - smoothstep(0.97, 1.0, r); \
	float h = inside * lerp(ringh, 0.45, bark); \
	return float2(h, lerp(rings * 0.6 + cr * 0.4, 0.05, bark)); \
} \
";

// Scatter: shapes thrown in three overlapping passes, turned and sized at random. Shape: 0 flake, 1 leaf, 2 coin, 3 hex nut,
// 4 ring, 5 flower
// a: shape, size, size jitter, rotation; a2: density
static char *str_pattern_scatter = "\
float pat_shape(float2 q, float shape) { \
	float d = pat_box(q, float2(0.5, 0.22)); \
	if (shape > 0.5) { \
		float2 o = float2(0.0, 0.32); \
		d = max(sqrt(dot(q - o, q - o)) - 0.55, sqrt(dot(q + o, q + o)) - 0.55); \
	} \
	if (shape > 1.5) { \
		d = sqrt(dot(q, q)) - 0.45; \
	} \
	if (shape > 2.5) { \
		float2 aq = abs(q); \
		d = max(max(aq.x * 0.866 + aq.y * 0.5, aq.y) - 0.42, 0.17 - sqrt(dot(q, q))); \
	} \
	if (shape > 3.5) { \
		d = abs(sqrt(dot(q, q)) - 0.32) - 0.1; \
	} \
	if (shape > 4.5) { \
		float an = atan2(q.y, q.x); \
		d = sqrt(dot(q, q)) - 0.45 * (0.55 + 0.45 * abs(cos(an * 2.5))); \
	} \
	return d; \
} \
float2 pat_scatter(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float h = 0.0; \
	float cid = 0.0; \
	for (int kk = 0; kk < 3; kk += 1) { \
		float2 q0 = p + float2(float(kk) * 0.37, float(kk) * 0.61); \
		float2 cell = floor(q0); \
		for (int jj = 0; jj < 9; jj += 1) { \
			float2 o = float2(floor(float(jj) / 3.0) - 1.0, float(jj) - 3.0 * floor(float(jj) / 3.0) - 1.0); \
			float2 id = pat_wrap(cell + o, per) + pat_seed(seed + float(kk) * 11.0); \
			float2 rr = pat_rand2(id); \
			float rs = pat_rand(id + float2(7.3, 1.9)); \
			float rv = pat_rand(id + float2(3.7, 5.1)); \
			if (rv < a2.x) { \
				float sz = max(a.y * (1.0 - a.z * rs), 0.05); \
				float2 lq = (q0 - cell - o - float2(0.5, 0.5) - (rr - float2(0.5, 0.5)) * 0.6) / sz; \
				lq = pat_rotate(lq, (rr.x - 0.5) * 6.28318530718 * a.w); \
				float sd = pat_shape(lq, a.x); \
				if (sd < 0.0) { \
					float prof = clamp(-sd * 6.0, 0.0, 1.0); \
					if (a.x > 0.5 && a.x < 1.5) { \
						prof = prof * (1.0 - 0.35 * exp(-abs(lq.x) * 40.0)); \
					} \
					if (a.x > 1.5 && a.x < 2.5) { \
						prof = 0.75 + 0.25 * smoothstep(-0.08, -0.02, sd); \
					} \
					h = (0.5 + 0.5 * prof) * (0.85 + 0.15 * float(kk) / 2.0); \
					cid = rs; \
				} \
			} \
		} \
	} \
	return float2(h, cid); \
} \
";

// Tufted: cushions bulging between buttons on a diamond grid, creases running between the buttons
// a: puffiness, button size, crease, variation
static char *str_pattern_tufted = "\
float2 pat_tufted(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 q = float2(p.x + p.y, p.y - p.x) * 0.5; \
	float2 c = floor(q); \
	float2 f = q - c; \
	float pillow = sqrt(max(sin(f.x * 3.14159) * sin(f.y * 3.14159), 0.0)); \
	float crease = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)); \
	float h = pow(pillow, max(1.0 - a.x, 0.2)) * (1.0 - a.z * (1.0 - smoothstep(0.0, 0.06, crease))); \
	float2 g = f - floor(f + float2(0.5, 0.5)); \
	float bd = sqrt(dot(g, g)); \
	float button = 1.0 - smoothstep(a.y * 0.5, a.y * 0.5 + 0.02, bd); \
	h = lerp(h, 0.18 + 0.15 * sqrt(max(1.0 - bd / max(a.y * 0.5, 0.01), 0.0)), button); \
	float cr = pat_cell_rand(c, float2(4096.0, 4096.0), seed); \
	return float2(h * (1.0 - a.w + a.w * cr), cr); \
} \
";

// Tread plate: raised lozenges turned one way and the other like a checker board
// a: length, width, softness
static char *str_pattern_tread = "\
float2 pat_tread(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 c = floor(p); \
	float2 q = p - c - float2(0.5, 0.5); \
	float flip = frac((c.x + c.y) * 0.5) * 2.0; \
	q = pat_rotate(q, lerp(0.785398, -0.785398, flip)); \
	float hl = a.x * 0.5; \
	float2 k = float2(clamp(q.x, -hl, hl), 0.0); \
	float2 dq = q - k; \
	float d = sqrt(dot(dq, dq)) / max(a.y * 0.5, 0.01); \
	float h = sqrt(max(1.0 - d * d, 0.0)); \
	h = h * smoothstep(1.0, 1.0 - max(a.z, 0.02), d); \
	return float2(h, pat_cell_rand(c, per, seed)); \
} \
";

// Chain mail: rings of two staggered grids linking through each other, each one leans so the other passes over it
// a: ring radius, thickness, variation
static char *str_pattern_chainmail = "\
float pat_ring_h(float2 q, float rad, float th) { \
	float d = abs(sqrt(dot(q, q)) - rad) / max(th, 0.01); \
	return sqrt(max(1.0 - d * d, 0.0)); \
} \
float2 pat_chainmail(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 ca = floor(p + float2(0.5, 0.5)); \
	float2 qa = p - ca; \
	float2 pb = p + float2(0.5, 0.5); \
	float2 cb = floor(pb + float2(0.5, 0.5)); \
	float2 qb = pb - cb; \
	float ha = pat_ring_h(qa, a.x, a.y) * (0.75 + 0.25 * clamp(qa.x / max(a.x, 0.01), -1.0, 1.0)); \
	float hb = pat_ring_h(qb, a.x, a.y) * (0.75 - 0.25 * clamp(qb.x / max(a.x, 0.01), -1.0, 1.0)); \
	float h = max(ha, hb); \
	float cr = pat_rand(lerp(ca, cb + float2(31.0, 17.0), step(ha, hb)) + pat_seed(seed)); \
	return float2(h * (1.0 - a.z + a.z * cr), cr); \
} \
";

// Radial: a disc in each cell brushed in circles, fine scratches running around its center
// a: density, disc size, variation
static char *str_pattern_radial = "\
float2 pat_radial(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 c = floor(p); \
	float2 q = p - c - float2(0.5, 0.5); \
	float r = sqrt(dot(q, q)); \
	float an = atan2(q.y, q.x) / 6.28318530718; \
	float lines = pat_gnoise(float2(r * a.x, an * 3.0), float2(4096.0, 4096.0), seed); \
	lines = lines * 0.7 + 0.3 * pat_gnoise(float2(r * a.x * 3.1, an * 9.0), float2(4096.0, 4096.0), seed + 1.0); \
	float disc = 1.0 - smoothstep(a.y * 0.5 - 0.01, a.y * 0.5, r); \
	float h = lerp(0.5, lines, disc); \
	float cr = pat_cell_rand(c, per, seed + 2.0); \
	return float2(h * (1.0 - a.z + a.z * cr), r); \
} \
";

// Streaks: drips running down, they start at random heights and fade as they go (use triplanar mapping for world down)
// a: density, length, width, wobble
static char *str_pattern_streaks = "\
float2 pat_streaks(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float s = 0.0; \
	for (int kk = 0; kk < 3; kk += 1) { \
		float k = a.x * (1.0 + float(kk) * 0.7); \
		float wx = p.x * k + (pat_gnoise(float2(p.x * 2.0, p.y * 3.0), float2(4096.0, 4096.0), seed + float(kk)) - 0.5) * a.w * 2.0; \
		float col = floor(wx); \
		float fx = wx - col; \
		float r1 = pat_rand(float2(col, float(kk) * 13.0) + pat_seed(seed)); \
		float r2 = pat_rand(float2(col, float(kk) * 7.0 + 3.0) + pat_seed(seed)); \
		float start = r1 * 4.0; \
		float len = max(a.y * (0.4 + 0.8 * r2), 0.05); \
		float yy = p.y / 4.0; \
		float t = (start - (yy - floor(yy)) * 4.0) / len; \
		float fade = smoothstep(0.0, 0.08, t) * (1.0 - smoothstep(0.3, 1.0, t)); \
		float w = clamp(1.0 - abs(fx - 0.5) * 2.0 / max(a.z, 0.02), 0.0, 1.0); \
		s = max(s, fade * w * (0.5 + 0.5 * r2)); \
	} \
	return float2(s, s); \
} \
";

// Craters: bowls with raised rims in three passes; with cracks, holes with lines running out of them (impacts)
// a: size, size jitter, density, rim; a2: cracks, depth
static char *str_pattern_craters = "\
float2 pat_craters(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float h = 0.5; \
	float cid = 0.0; \
	for (int kk = 0; kk < 3; kk += 1) { \
		float2 q0 = p + float2(float(kk) * 0.41, float(kk) * 0.73); \
		float2 cell = floor(q0); \
		for (int jj = 0; jj < 9; jj += 1) { \
			float2 o = float2(floor(float(jj) / 3.0) - 1.0, float(jj) - 3.0 * floor(float(jj) / 3.0) - 1.0); \
			float2 id = pat_wrap(cell + o, per) + pat_seed(seed + float(kk) * 11.0); \
			float2 rr = pat_rand2(id); \
			float rs = pat_rand(id + float2(7.3, 1.9)); \
			float rv = pat_rand(id + float2(3.7, 5.1)); \
			if (rv < a.z) { \
				float rad = max(a.x * 0.5 * (1.0 - a.y * rs), 0.02); \
				float2 d = q0 - cell - o - float2(0.5, 0.5) - (rr - float2(0.5, 0.5)) * 0.6; \
				float r = sqrt(dot(d, d)) / rad; \
				if (r < 2.5) { \
					float bowl = (1.0 - min(r * r, 1.0)) * a2.y; \
					float rim = exp(-(r - 1.0) * (r - 1.0) * 18.0) * a.w; \
					float an = atan2(d.y, d.x); \
					float crack = pow(abs(sin(an * 4.0 + rs * 12.0)), 40.0) * (1.0 - smoothstep(1.0, 2.5, r)) * step(1.0, r) * a2.x; \
					h = h - bowl * 0.5 + rim * 0.25 - crack * 0.3; \
					if (r < 1.0) { \
						cid = rs; \
					} \
				} \
			} \
		} \
	} \
	return float2(clamp(h, 0.0, 1.0), cid); \
} \
";

// Mesh: two families of wires crossing in diamonds, going over and under each other (chain link fence, nets)
// a: thickness, stretch, weave
static char *str_pattern_mesh = "\
float2 pat_mesh(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float u = p.x + p.y * max(a.y, 0.1); \
	float v = p.x - p.y * max(a.y, 0.1); \
	float fu = u - floor(u); \
	float fv = v - floor(v); \
	float th = max(a.x, 0.01); \
	float du = abs(fu - 0.5) * 2.0 / th; \
	float dv = abs(fv - 0.5) * 2.0 / th; \
	float wu = sqrt(max(1.0 - du * du, 0.0)) * (0.8 + 0.2 * a.z * sin(v * 3.14159)); \
	float wv = sqrt(max(1.0 - dv * dv, 0.0)) * (0.8 - 0.2 * a.z * sin(u * 3.14159)); \
	float h = max(wu, wv); \
	return float2(h, step(wu, wv)); \
} \
";

// Rope: rows of rope, the strands winding around each one
// a: strands, twist, gap, fiber
static char *str_pattern_rope = "\
float2 pat_rope(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float row = floor(p.y); \
	float fy = p.y - row; \
	float g = a.z * 0.5; \
	float v = clamp((fy - g) / max(1.0 - 2.0 * g, 0.01), 0.0, 1.0); \
	float body = sqrt(max(1.0 - (2.0 * v - 1.0) * (2.0 * v - 1.0), 0.0)) * step(g, fy) * step(fy, 1.0 - g); \
	float t = p.x * a.y + v * max(floor(a.x + 0.5), 1.0); \
	float strand = frac(t); \
	float sh = sqrt(max(1.0 - (2.0 * strand - 1.0) * (2.0 * strand - 1.0), 0.0)); \
	float fib = 0.5 + 0.5 * sin((p.x * a.y * 9.0 - v * 14.0) * 6.28318530718); \
	float h = body * (0.55 + 0.45 * sh) * (1.0 - a.w * 0.2 + a.w * 0.2 * fib); \
	return float2(h, pat_rand(float2(row, floor(t)) + pat_seed(seed))); \
} \
";

// Stitches: rows of slanted stitches, one or two lines (leather seams, embroidery)
// a: length, width, slant, double
static char *str_pattern_stitches = "\
float2 pat_stitches(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float row = floor(p.y); \
	float fy = p.y - row - 0.5; \
	float h = 0.0; \
	for (int kk = 0; kk < 2; kk += 1) { \
		float off = (float(kk) - 0.5) * 0.3 * step(0.5, a.w); \
		if (float(kk) < 0.5 + step(0.5, a.w)) { \
			float yy = fy - off; \
			float x = p.x * 4.0 + yy * a.z * 4.0; \
			float fx = x - floor(x); \
			float dash = step(fx, a.x); \
			float across = 1.0 - smoothstep(a.y * 0.5, a.y * 0.5 + 0.02, abs(yy)); \
			float bulge = sqrt(max(1.0 - (2.0 * fx / max(a.x, 0.01) - 1.0) * (2.0 * fx / max(a.x, 0.01) - 1.0), 0.0)); \
			h = max(h, dash * across * (0.6 + 0.4 * bulge)); \
		} \
	} \
	return float2(h, h); \
} \
";

// Feathers: rows of pointed feathers over each other, a shaft down the middle, barbs slanting off it
// a: point, barbs, shaft, variation
static char *str_pattern_feathers = "\
float2 pat_feathers(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float row = floor(p.y); \
	float x = p.x + 0.5 * (row - 2.0 * floor(row * 0.5)); \
	float col = floor(x); \
	float fx = x - col; \
	float fy = p.y - row; \
	float cx = fx * 2.0 - 1.0; \
	float lip = a.x * 0.5 * abs(cx); \
	float2 id = float2(col, row); \
	float h = 0.0; \
	if (fy < lip) { \
		float r2 = row - 1.0; \
		float x2 = p.x + 0.5 * (r2 - 2.0 * floor(r2 * 0.5)); \
		id = float2(floor(x2), r2); \
		h = 0.15; \
	} \
	else { \
		float v = (fy - lip) / max(1.0 - lip, 0.001); \
		float barbs = 0.5 + 0.5 * sin((fy * a.y + abs(cx) * a.y * 0.6) * 6.28318530718); \
		float shaft = 1.0 - smoothstep(a.z, a.z + 0.03, abs(cx)); \
		h = (1.0 - v * 0.6) * (0.75 + 0.25 * barbs) * (1.0 - smoothstep(0.85, 1.0, abs(cx))); \
		h = max(h, shaft * (1.0 - v * 0.5)); \
	} \
	float cr = pat_cell_rand(id, per, seed); \
	return float2(h * (1.0 - a.w + a.w * cr), cr); \
} \
";

// Facets: flat faces leaning every way, a crease between them (crumpled paper soft, crystals sharp)
// a: slope, crease, randomness, variation
static char *str_pattern_facets = "\
float2 pat_facets(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 cell = floor(p); \
	float2 f = p - cell; \
	float f1 = 8.0; \
	float f2 = 8.0; \
	float2 nearest = cell; \
	float2 nr = float2(0.0, 0.0); \
	for (int jj = 0; jj < 3; jj += 1) { \
		for (int kk = 0; kk < 3; kk += 1) { \
			float2 o = float2(float(kk) - 1.0, float(jj) - 1.0); \
			float2 rr = pat_rand2(pat_wrap(cell + o, per) + pat_seed(seed)); \
			float2 r = o + float2(0.5, 0.5) + (rr - float2(0.5, 0.5)) * a.z - f; \
			float d = dot(r, r); \
			if (d < f1) { \
				f2 = f1; \
				f1 = d; \
				nearest = cell + o; \
				nr = r; \
			} \
			else if (d < f2) { \
				f2 = d; \
			} \
		} \
	} \
	float2 g = pat_rand2(pat_wrap(nearest, per) + pat_seed(seed + 3.0)) * 2.0 - float2(1.0, 1.0); \
	float cr = pat_cell_rand(nearest, per, seed + 1.0); \
	float h = 0.5 + dot(nr, g) * a.x + (cr - 0.5) * 0.3 * a.w; \
	float crease = smoothstep(0.0, max(a.y, 0.002), sqrt(f2) - sqrt(f1)); \
	h = h * (0.6 + 0.4 * crease); \
	return float2(clamp(h, 0.0, 1.0), cr); \
} \
";

// Spider webs: spokes from the center of each cell and a spiral thread around them
// a: spokes, rings, thickness, size
static char *str_pattern_web = "\
float2 pat_web(float2 p, float2 per, float4 a, float4 a2, float seed) { \
	float2 c = floor(p); \
	float2 q = p - c - float2(0.5, 0.5); \
	float r = sqrt(dot(q, q)) / max(a.w * 0.5, 0.05); \
	float an = atan2(q.y, q.x) / 6.28318530718 + 0.5; \
	float n = max(floor(a.x + 0.5), 3.0); \
	float sp = an * n; \
	float spoke = abs(sp - floor(sp + 0.5)) / n * 6.28318530718 * r; \
	float ring = r * a.y + an; \
	float rd = abs(ring - floor(ring + 0.5)) / a.y; \
	float th = a.z * 0.05; \
	float lines = 1.0 - smoothstep(th, th * 2.0, min(spoke, rd)); \
	float inside = 1.0 - smoothstep(0.95, 1.0, r); \
	float h = lines * inside; \
	return float2(h, h); \
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
	{"PATTERN_NOISE",     "Noise",     "pat_noise_fbm", &str_pattern_noise,     8.0,  2, {"Persistence", "Ridged"},                                                 {0.5, 0.0},                       {1.0, 1.0}},
	{"PATTERN_CELLS",     "Cells",     "pat_voronoi",   &str_pattern_cells,     5.0,  4, {"Edge Width", "Randomness", "Variation", "Rounding"},                     {0.12, 1.0, 0.3, 0.0},            {1.0, 1.0, 1.0, 1.0}},
	{"PATTERN_GRAIN",     "Wood Grain","pat_grain",     &str_pattern_grain,     3.0,  2, {"Rings", "Waviness"},                                                     {2.0, 3.0},                       {16.0, 8.0}},
	{"PATTERN_BRICKS",    "Bricks",    "pat_bricks",    &str_pattern_bricks,    6.0,  4, {"Mortar", "Bevel", "Offset", "Variation"},                                {0.06, 0.06, 0.5, 0.15},          {0.5, 0.5, 1.0, 1.0}},
	{"PATTERN_TILES",     "Tiles",     "pat_tiles",     &str_pattern_tiles,     8.0,  4, {"Gap", "Bevel", "Variation", "Checker"},                                  {0.05, 0.05, 0.08, 0.0},          {0.5, 0.5, 1.0, 1.0}},
	{"PATTERN_HEX",       "Hexagons",  "pat_hex",       &str_pattern_hex,       8.0,  3, {"Gap", "Bevel", "Variation"},                                             {0.06, 0.08, 0.1},                {0.5, 0.5, 1.0}},
	{"PATTERN_WEAVE",     "Weave",     "pat_weave",     &str_pattern_weave,     20.0, 3, {"Thread Width", "Fiber", "Twill"},                                        {0.9, 0.2, 0.0},                  {1.0, 1.0, 1.0}},
	{"PATTERN_PEBBLES",   "Pebbles",   "pat_pebbles",   &str_pattern_pebbles,   8.0,  4, {"Size", "Size Jitter", "Softness", "Variation"},                          {0.45, 0.4, 0.3, 0.4},            {1.0, 1.0, 1.0, 1.0}},
	{"PATTERN_DOTS",      "Dots",      "pat_dots",      &str_pattern_dots,      10.0, 6, {"Size", "Randomness", "Density", "Softness", "Offset X", "Offset Y"},     {0.5, 0.0, 1.0, 0.3, 0.0, 0.0},   {1.0, 1.0, 1.0, 1.0, 1.0, 1.0}},
	{"PATTERN_STRIPES",   "Stripes",   "pat_stripes",   &str_pattern_stripes,   8.0,  5, {"Profile", "Width", "Waviness", "Wave Length", "Zigzag"},                 {0.0, 0.5, 0.0, 4.0, 0.0},        {2.0, 1.0, 2.0, 16.0, 2.0}},
	{"PATTERN_FIBERS",    "Fibers",    "pat_fibers",    &str_pattern_fibers,    60.0, 4, {"Length", "Clumping", "Clump Size", "Bend"},                              {8.0, 0.6, 10.0, 0.3},            {30.0, 1.0, 60.0, 1.0}},
	{"PATTERN_PLANKS",    "Planks",    "pat_planks",    &str_pattern_planks,    8.0,  4, {"Gap", "Bevel", "Board Length", "Grain"},                               {0.04, 0.05, 4.0, 0.6},           {0.5, 0.5, 16.0, 1.0}},
	{"PATTERN_SHINGLES",  "Roof Tiles","pat_shingles",  &str_pattern_shingles,  8.0,  4, {"Roundness", "Gap", "Slope", "Variation"},                             {1.0, 0.04, 0.6, 0.15},           {1.0, 0.5, 1.0, 1.0}},
	{"PATTERN_HERRINGBONE","Herringbone","pat_herringbone",&str_pattern_herringbone, 12.0, 4, {"Gap", "Bevel", "Length", "Variation"},                         {0.04, 0.05, 4.0, 0.15},          {0.5, 0.5, 12.0, 1.0}},
	{"PATTERN_PARQUET",   "Parquet",   "pat_parquet",   &str_pattern_parquet,   6.0,  4, {"Strips", "Gap", "Bevel", "Variation"},                                   {3.0, 0.03, 0.04, 0.15},          {8.0, 0.5, 0.5, 1.0}},
	{"PATTERN_UNEVEN",    "Uneven Bricks","pat_uneven", &str_pattern_uneven,    3.0,  4, {"Gap", "Bevel", "Splits", "Variation"},                                   {0.04, 0.06, 4.0, 0.15},          {0.5, 0.5, 6.0, 1.0}},
	{"PATTERN_SEIGAIHA",  "Seigaiha",  "pat_seigaiha",  &str_pattern_fan,       6.0,  4, {"Rings", "Radius", "Gap", "Variation"},                                   {4.0, 0.7, 0.04, 0.1},            {10.0, 1.5, 0.3, 1.0}},
	{"PATTERN_ARCS",      "Arc Pavement","pat_arcs",    &str_pattern_fan,       3.0,  4, {"Rings", "Radius", "Gap", "Variation"},                                   {5.0, 0.7, 0.03, 0.2},            {10.0, 1.5, 0.3, 1.0}},
	{"PATTERN_STARS",     "Star Tiles","pat_stars",     &str_pattern_stars,     6.0,  4, {"Star Size", "Gap", "Bevel", "Variation"},                                {0.7, 0.04, 0.05, 0.1},           {1.0, 0.3, 0.3, 1.0}},
	{"PATTERN_TRUCHET",   "Truchet",   "pat_truchet",   &str_pattern_truchet,   8.0,  3, {"Width", "Softness", "Variation"},                                        {0.3, 0.3, 0.0},                  {1.0, 1.0, 1.0}},
	{"PATTERN_MOTIF",     "Ornament",  "pat_motif",     &str_pattern_motif,     6.0,  4, {"Petals", "Size", "Heart", "Softness"},                                   {6.0, 0.8, 0.3, 0.06},            {12.0, 1.0, 1.0, 0.5}},
	{"PATTERN_THATCH",    "Thatch",    "pat_thatch",    &str_pattern_thatch,    8.0,  4, {"Strand Density", "Slope", "Ragged", "Variation"},                        {30.0, 0.6, 0.3, 0.2},            {80.0, 1.0, 1.0, 1.0}},
	{"PATTERN_RINGS",     "Rings",     "pat_rings",     &str_pattern_rings,     5.0,  4, {"Size", "Rings", "Wobble", "Bark"},                                       {0.45, 8.0, 0.3, 0.12},           {2.0, 30.0, 2.0, 0.5}},
	{"PATTERN_SCATTER",   "Scatter",   "pat_scatter",   &str_pattern_scatter,   6.0,  5, {"Shape", "Size", "Size Jitter", "Rotation", "Density"},                   {0.0, 0.7, 0.4, 1.0, 0.8},        {5.0, 2.0, 1.0, 1.0, 1.0}},
	{"PATTERN_TUFTED",    "Tufted",    "pat_tufted",    &str_pattern_tufted,    4.0,  4, {"Puffiness", "Button Size", "Crease", "Variation"},                       {0.5, 0.12, 0.5, 0.1},            {1.0, 0.5, 1.0, 1.0}},
	{"PATTERN_TREAD",     "Tread Plate","pat_tread",    &str_pattern_tread,     10.0, 3, {"Length", "Width", "Softness"},                                           {0.55, 0.16, 0.4},                {1.0, 0.5, 1.0}},
	{"PATTERN_CHAINMAIL", "Chain Mail","pat_chainmail", &str_pattern_chainmail, 12.0, 3, {"Ring Radius", "Thickness", "Variation"},                                 {0.42, 0.1, 0.1},                 {0.8, 0.3, 1.0}},
	{"PATTERN_RADIAL",    "Radial",    "pat_radial",    &str_pattern_radial,    1.0,  3, {"Density", "Disc Size", "Variation"},                                     {200.0, 0.95, 0.0},               {600.0, 1.0, 1.0}},
	{"PATTERN_STREAKS",   "Streaks",   "pat_streaks",   &str_pattern_streaks,   1.0,  4, {"Density", "Length", "Width", "Wobble"},                                  {12.0, 1.5, 0.5, 0.3},            {60.0, 4.0, 1.0, 1.0}},
	{"PATTERN_CRATERS",   "Craters",   "pat_craters",   &str_pattern_craters,   4.0,  6, {"Size", "Size Jitter", "Density", "Rim", "Cracks", "Depth"},               {0.6, 0.6, 0.6, 0.6, 0.0, 0.6},   {2.0, 1.0, 1.0, 1.0, 1.0, 1.0}},
	{"PATTERN_MESH",      "Mesh",      "pat_mesh",      &str_pattern_mesh,      10.0, 3, {"Thickness", "Stretch", "Weave"},                                         {0.2, 1.0, 0.5},                  {1.0, 3.0, 1.0}},
	{"PATTERN_ROPE",      "Rope",      "pat_rope",      &str_pattern_rope,      4.0,  4, {"Strands", "Twist", "Gap", "Fiber"},                                      {3.0, 2.0, 0.05, 0.5},            {6.0, 8.0, 0.4, 1.0}},
	{"PATTERN_STITCHES",  "Stitches",  "pat_stitches",  &str_pattern_stitches,  8.0,  4, {"Length", "Width", "Slant", "Double"},                                    {0.6, 0.12, 0.3, 0.0},            {1.0, 0.5, 1.0, 1.0}},
	{"PATTERN_FEATHERS",  "Feathers",  "pat_feathers",  &str_pattern_feathers,  6.0,  4, {"Point", "Barbs", "Shaft", "Variation"},                                  {1.0, 30.0, 0.04, 0.2},           {1.0, 80.0, 0.2, 1.0}},
	{"PATTERN_FACETS",    "Facets",    "pat_facets",    &str_pattern_facets,    6.0,  4, {"Slope", "Crease", "Randomness", "Variation"},                            {0.6, 0.05, 1.0, 0.3},            {2.0, 0.5, 1.0, 1.0}},
	{"PATTERN_WEB",       "Spider Web","pat_web",       &str_pattern_web,       2.0,  4, {"Spokes", "Rings", "Thickness", "Size"},                                  {12.0, 8.0, 0.5, 0.95},           {24.0, 24.0, 2.0, 1.0}},
	{"PATTERN_SCRATCHES", "Scratches", "pat_scratches", &str_pattern_scratches, 4.0,  4, {"Length", "Width", "Waviness", "Randomness"},                             {0.5, 1.0, 0.5, 0.5},             {1.0, 4.0, 1.0, 1.0}},
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
	pattern_lib_use(pattern_node_index(node->type));

	ui_node_link_t *vec_link = parser_material_get_input_link(node->inputs->buffer[0]);
	char           *vec      = vec_link != NULL ? string_copy(parser_material_parse_vector_input(node->inputs->buffer[0])) : NULL;
	char           *in[PATTERN_COMMON_INPUTS + PATTERN_EXTRA_INPUTS];
	for (i32 i = 1; i < PATTERN_COMMON_INPUTS + PATTERN_EXTRA_INPUTS; ++i) {
		in[i] = i < node->inputs->length ? string_copy(parser_material_parse_value_input(node->inputs->buffer[i], false)) : string_copy("0.0");
	}
	parser_material_write(parser_material_kong, string_tmp("float4 %s_t = float4(%s, %s, %s * 0.0174533, %s);", store, in[1], in[2], in[3], in[5]));
	parser_material_write(parser_material_kong, string_tmp("float4 %s_d = float4(%s, %s, 0.0, 0.0);", store, in[6], in[7]));
	parser_material_write(parser_material_kong, string_tmp("float4 %s_a = float4(%s, %s, %s, %s);", store, in[8], in[9], in[10], in[11]));
	parser_material_write(parser_material_kong, string_tmp("float4 %s_b = float4(%s, %s, 0.0, 0.0);", store, in[12], in[13]));

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
			                      string_tmp("%s_r = %s_r + %s(pat_coord(%s, %s_t, %s_d, %s_sc, 1000.0), float2(4096.0, 4096.0), %s_a, %s_b, %s_t.w) * %s_w.%s;",
			                                 store, store, def->fn, planes[i], store, store, store, store, store, store, store, w[i]));
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
		parser_material_write(parser_material_kong, string_tmp("float2 %s_r = %s(pat_coord(%s, %s_t, %s_d, %s_sc, 1.0), %s_sc, %s_a, %s_b, %s_t.w);", store, def->fn,
		                                                       uv, store, store, store, store, store, store, store));
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

// The shader functions are kept in a map and written in no set order, so functions calling each other go in one string: the
// shared functions and the patterns the shader uses. One string per set of patterns, made once.

#define PATTERN_LIB_MAX 64
static u64   pattern_lib_masks[PATTERN_LIB_MAX];
static char *pattern_lib_strs[PATTERN_LIB_MAX];
static i32   pattern_lib_count = 0;

static char *pattern_lib(u64 mask) {
	for (i32 i = 0; i < pattern_lib_count; ++i) {
		if (pattern_lib_masks[i] == mask) {
			return pattern_lib_strs[i];
		}
	}
	size_t size = strlen(str_pattern_common) + 1;
	for (i32 i = 0; i < PATTERN_DEF_COUNT; ++i) {
		if (mask & ((u64)1 << i)) {
			size += strlen(*pattern_defs[i].src);
		}
	}
	char *lib = malloc(size);
	strcpy(lib, str_pattern_common);
	for (i32 i = 0; i < PATTERN_DEF_COUNT; ++i) {
		bool added = false; // Patterns sharing a string add it once
		for (i32 j = 0; j < i; ++j) {
			added = added || ((mask & ((u64)1 << j)) && pattern_defs[j].src == pattern_defs[i].src);
		}
		if ((mask & ((u64)1 << i)) && !added) {
			strcat(lib, *pattern_defs[i].src);
		}
	}
	if (pattern_lib_count < PATTERN_LIB_MAX) {
		pattern_lib_masks[pattern_lib_count] = mask;
		pattern_lib_strs[pattern_lib_count]  = lib;
		pattern_lib_count++;
	}
	return lib;
}

static u64 pattern_lib_mask_of(char *lib) {
	for (i32 i = 0; i < pattern_lib_count; ++i) {
		if (pattern_lib_strs[i] == lib) {
			return pattern_lib_masks[i];
		}
	}
	return 0;
}

// The shared functions in the shader being parsed, with pattern i (-1 for none)
void pattern_lib_use(i32 i) {
	any_map_t *fns  = parser_material_kong->functions;
	char      *key  = "float pat_rand";
	i32        slot = -1;
	for (i32 k = 0; k < fns->keys->capacity; ++k) {
		if (fns->keys->buffer[k] != NULL && string_equals(fns->keys->buffer[k], key)) {
			slot = k;
			break;
		}
	}
	u64 mask = slot >= 0 ? pattern_lib_mask_of(fns->values->buffer[slot]) : 0;
	u64 want = mask | (i >= 0 ? ((u64)1 << i) : 0);
	if (slot >= 0 && want == mask) {
		return;
	}
	if (slot < 0) {
		node_shader_add_function(parser_material_kong, pattern_lib(want));
	}
	else {
		fns->values->buffer[slot] = pattern_lib(want); // The key stays, the map frees it
	}
}

void pattern_nodes_init() {
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
