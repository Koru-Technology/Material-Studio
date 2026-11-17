#version 450

// One mask generator or textured fill (util_mask_generators.c): the paint mesh drawn flat in uv space, R the value and A
// the coverage, dilated afterwards by mask_generator_dilate. Axes are Y up as the user sees them, the engine is Z up.
// Patterns come from the 3d position, seamless across uv islands. The registered position, world normal and normal maps
// (baked by the app: object space, Z up) stand in for the mesh's own where they are registered.
layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat3 N;            // Normal matrix of the drawn object (Directional, Light), d * N takes a world
	                                      // direction into the object (Streaks)
	layout(offset = 48) vec4 params0;     // params[0..3]
	layout(offset = 64) vec4 params1;     // params[4..7]
	layout(offset = 80) vec4 params2;     // params[8..11]
	layout(offset = 96) vec4 params3;     // Noise: scale, amount (3D Noise: distortion), seed, detail
	layout(offset = 112) vec3 pos_min;    // Mesh bounds, packed position units
	layout(offset = 128) vec3 pos_inv;    // 1 / extent per axis, Position Gradient
	layout(offset = 144) int type;        // mask_effect_type_t
	layout(offset = 148) int uv_map;      // 1: the second uv set, in_tex.zw
	layout(offset = 152) float noise_inv; // 1 / largest extent, isotropic noise
	layout(offset = 160) vec4 uv_rect;    // The part of the mask drawn (u0, v0, du, dv), 0 0 1 1 the whole (export tiles)
	layout(offset = 176) int maps;        // Registered mesh maps: 1 position, 2 world normal, 4 normal; 8 micro details
	                                      // from a layer (texlnor, texlpack) instead of the normal map
	layout(offset = 180) float map_scale; // Position map units to packed position units (1 / scale_pos)
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D texcurv; // Signed: 0.5 flat, brighter ridges, darker grooves
layout(set = 0, binding = 3) uniform texture2D texao;
layout(set = 0, binding = 4) uniform texture2D texthick; // White thick
layout(set = 0, binding = 5) uniform texture2D texpos;   // Position map: object position * 0.5 + 0.5
layout(set = 0, binding = 6) uniform texture2D texwnor;  // World normal map: object normal * 0.5 + 0.5
layout(set = 0, binding = 7) uniform texture2D texnor;   // Tangent space normal map
layout(set = 0, binding = 8) uniform texture2D teximg;   // Image fill
layout(set = 0, binding = 9) uniform texture2D texlnor;  // Mask Builder micro details: a layer's tangent normal (rgb)
layout(set = 0, binding = 10) uniform texture2D texlpack; // and its height (a)

layout(location = 0) VARYING Varyings {
	vec3 pos;
	vec3 nor;
	vec2 uv;  // First uv set, the mesh maps are baked in it
	vec2 uvl; // The uv set of the layer, the fill's uv projection
} VARYING_NAME;

#ifdef VERT
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec2 in_nor;
layout(location = 2) in vec4 in_tex; // tex, and tex1 in zw when the mesh has a second uv set (one 4 short attribute)

void main() {
	vec2 t      = constants.uv_map == 1 ? in_tex.zw : in_tex.xy;
	vec2 r      = (t - constants.uv_rect.xy) / constants.uv_rect.zw;
	gl_Position = vec4(r.x * 2.0 - 1.0, (1.0 - r.y) * 2.0 - 1.0, 0.0, 1.0);
	vout.pos    = in_pos.xyz;
	vout.nor    = vec3(in_nor.xy, in_pos.w);
	vout.uv     = in_tex.xy;
	vout.uvl    = t;
}
#endif

#ifdef FRAG
layout(location = 0) out vec4 frag_color;

// tex_noise_f / tex_noise_fbm of the Noise Texture node (nodes_material/noise_texture_node.c)
float hash(float n) {
	return fract(sin(n) * 10000.0);
}

float tex_noise_f(vec3 x) {
	vec3  step = vec3(110.0, 241.0, 171.0);
	vec3  i    = floor(x);
	vec3  f    = fract(x);
	float n    = dot(i, step);
	vec3  u    = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash(n + dot(step, vec3(0.0, 0.0, 0.0))), hash(n + dot(step, vec3(1.0, 0.0, 0.0))), u.x),
	               mix(hash(n + dot(step, vec3(0.0, 1.0, 0.0))), hash(n + dot(step, vec3(1.0, 1.0, 0.0))), u.x), u.y),
	           mix(mix(hash(n + dot(step, vec3(0.0, 0.0, 1.0))), hash(n + dot(step, vec3(1.0, 0.0, 1.0))), u.x),
	               mix(hash(n + dot(step, vec3(0.0, 1.0, 1.0))), hash(n + dot(step, vec3(1.0, 1.0, 1.0))), u.x), u.y),
	           u.z);
}

float tex_noise_fbm(vec3 p, float detail, float roughness, float lacunarity) {
	float fscale = 1.0;
	float amp    = 1.0;
	float maxamp = 0.0;
	float sum    = 0.0;
	int   n      = int(clamp(detail, 0.0, 8.0));
	for (int ii = 0; ii <= n; ii += 1) {
		sum    = sum + amp * tex_noise_f(p * fscale);
		maxamp = maxamp + amp;
		amp    = amp * roughness;
		fscale = fscale * lacunarity;
	}
	float rmd = detail - floor(detail);
	if (rmd > 0.0) {
		float t       = tex_noise_f(p * fscale);
		float sum2    = sum + t * amp;
		float maxamp2 = maxamp + amp;
		return mix(sum / maxamp, sum2 / maxamp2, rmd);
	}
	return sum / maxamp;
}

vec3 hash3(vec3 p) {
	return fract(sin(vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)), dot(p, vec3(113.5, 271.9, 124.6)))) *
	             43758.5453);
}

// Jittered grid (voronoi_texture_node.c): distance to the nearest point, a random value of its cell
vec2 cells(vec3 x) {
	vec3  i  = floor(x);
	vec3  f  = fract(x);
	float d1 = 8.0;
	float id = 0.0;
	for (int zz = -1; zz <= 1; zz += 1) {
		for (int yy = -1; yy <= 1; yy += 1) {
			for (int xx = -1; xx <= 1; xx += 1) {
				vec3  o = vec3(float(xx), float(yy), float(zz));
				vec3  h = hash3(i + o);
				float d = length(o + h - f);
				if (d < d1) {
					d1 = d;
					id = fract(h.x * 7.13 + h.y * 3.71 + h.z * 1.37);
				}
			}
		}
	}
	return vec2(d1, id);
}

// A thin disc of random tilt and size in each cell: where the surface cuts one, a straight line fading at its ends.
// Discs reach at most half a cell past their own, so only the 8 cells nearest x can touch it.
float scratches(vec3 x) {
	x       *= 0.7; // Cells a bit larger, the scratches keep their length
	vec3  i = floor(x);
	vec3  f = fract(x);
	vec3  b = step(0.5, f) - 1.0; // The lower of the two nearest cells on each axis
	float v = 0.0;
	for (int zz = 0; zz <= 1; zz += 1) {
		for (int yy = 0; yy <= 1; yy += 1) {
			for (int xx = 0; xx <= 1; xx += 1) {
				vec3  o   = b + vec3(float(xx), float(yy), float(zz));
				vec3  h   = hash3(i + o);
				vec3  g   = hash3(i + o + vec3(17.0, 31.0, 47.0));
				vec3  nrm = normalize(g * 2.0 - 1.0 + vec3(0.0001, 0.0, 0.0));
				vec3  r   = f - o - h;
				float t   = dot(r, nrm);
				float rad = 0.25 + 0.25 * g.x;
				float s   = clamp(1.0 - abs(t) / 0.04, 0.0, 1.0) * (1.0 - smoothstep(rad * 0.4, rad, length(r - nrm * t)));
				v         = max(v, s * (0.6 + 0.4 * h.z));
			}
		}
	}
	return v;
}

// 3D Noise value in 0 - 1 of pattern at q
float pattern(int pat, vec3 q, float detail) {
	if (pat == 1) { // Cells: round spots of varied size
		vec2  c = cells(q);
		float n = 1.0 - c.x * mix(1.0, 1.6, c.y);
		if (detail > 1.0) {
			n += (tex_noise_fbm(q * 3.0, detail - 2.0, 0.5, 2.0) - 0.5) * 0.25;
		}
		return n;
	}
	if (pat == 2) { // Scratches, finer layers with more detail
		float n = 0.0;
		float s = 1.0;
		float k = 1.0;
		int   l = detail > 2.5 ? 2 : 1; // Each layer costs 8 cells
		for (int i = 0; i < 2; i += 1) {
			if (i >= l) {
				break;
			}
			n = max(n, scratches(q * s + vec3(float(i) * 13.7)) * k);
			s *= 1.9;
			k *= 0.8;
		}
		return n;
	}
	if (pat == 3) { // Streaks: noise stretched along the world's down
		vec3  d = normalize(vec3(0.0, 0.0, -1.0) * constants.N);
		float t = dot(q, d);
		q       = q - d * t * 0.92;
	}
	return (tex_noise_fbm(q, detail - 1.0, 0.5, 2.0) - 0.5) * 2.2 + 0.5; // Clouds and streaks, spread over 0 - 1
}

// Step at 0, softer as sharpness goes to 0
float sharp(float x, float sharpness) {
	float w = mix(0.3, 0.005, clamp(sharpness, 0.0, 1.0));
	return smoothstep(-w, w, x);
}

// Engine axis of a user axis (0 x, 1 y, 2 z, Y up), sign in the result
vec3 user_axis(int axis) {
	if (axis == 0) {
		return vec3(1.0, 0.0, 0.0);
	}
	if (axis == 1) {
		return vec3(0.0, 0.0, 1.0);
	}
	return vec3(0.0, -1.0, 0.0);
}

// Mesh maps

// Tangent space normal ts on the surface of normal n in uv set uv, the tangents solved from the uv derivatives as the
// normal bake does
vec3 tangent_to_object(vec3 ts, vec3 n, vec2 uv) {
	vec3  dp1 = dFdx(vin.pos);
	vec3  dp2 = dFdy(vin.pos);
	vec2  du1 = dFdx(uv);
	vec2  du2 = dFdy(uv);
	float s   = du1.x * du2.y - du2.x * du1.y < 0.0 ? -1.0 : 1.0;
	vec3  tu  = (dp1 * du2.y - dp2 * du1.y) * s;
	vec3  tv  = (dp2 * du1.x - dp1 * du2.x) * s;
	vec3  t   = tu - n * dot(n, tu);
	if (dot(t, t) <= dot(tu, tu) * 0.000001) { // Zero area triangles
		return n;
	}
	t      = normalize(t);
	vec3 b = tv - n * dot(n, tv) - t * dot(t, tv);
	if (dot(b, b) <= dot(tv, tv) * 0.000001) {
		return n;
	}
	b = normalize(b);
	return normalize(t * ts.x - b * ts.y + n * ts.z); // The bake writes (t, -b, n)
}

// Micro details from a layer: its normal and height, in the layer's uvs
bool micro_from_layer() {
	return (constants.maps & 8) != 0;
}

// World direction of the surface: the world normal map or the mesh's normal, with the micro details by micro
vec3 surface_normal(float micro) {
	vec3 n = normalize(vin.nor);
	if ((constants.maps & 2) != 0) {
		vec3 m = textureLod(sampler2D(texwnor, sampler_linear), vin.uv, 0.0).rgb * 2.0 - 1.0;
		if (dot(m, m) > 0.01) {
			n = normalize(m);
		}
	}
	if (micro > 0.0 && micro_from_layer()) {
		vec3 ts = textureLod(sampler2D(texlnor, sampler_linear), vin.uvl, 0.0).rgb * 2.0 - 1.0;
		n       = normalize(mix(n, tangent_to_object(ts, n, vin.uvl), micro));
	}
	else if (micro > 0.0 && (constants.maps & 4) != 0) {
		vec3 ts = textureLod(sampler2D(texnor, sampler_linear), vin.uv, 0.0).rgb * 2.0 - 1.0;
		n       = normalize(mix(n, tangent_to_object(ts, n, vin.uv), micro));
	}
	return normalize(constants.N * n);
}

// Object position in packed units: the position map or the mesh's
vec3 surface_position() {
	if ((constants.maps & 1) != 0) {
		return (textureLod(sampler2D(texpos, sampler_linear), vin.uv, 0.0).rgb * 2.0 - 1.0) * constants.map_scale;
	}
	return vin.pos;
}

// Signed curvature of a layer's normal and height, positive on bumps. Its offsets cover the same surface at any
// resolution: a texel of a 1024 texture, twice that for the height.
float layer_curvature() {
	vec2  size = vec2(textureSize(sampler2D(texlnor, sampler_linear), 0));
	vec2  px   = max(size / 1024.0, vec2(1.0, 1.0)) / size;
	vec2  uv   = vin.uvl;
	float xr   = textureLod(sampler2D(texlnor, sampler_linear), uv + vec2(px.x, 0.0), 0.0).r;
	float xl   = textureLod(sampler2D(texlnor, sampler_linear), uv - vec2(px.x, 0.0), 0.0).r;
	float yd   = textureLod(sampler2D(texlnor, sampler_linear), uv + vec2(0.0, px.y), 0.0).g;
	float yu   = textureLod(sampler2D(texlnor, sampler_linear), uv - vec2(0.0, px.y), 0.0).g;
	float div  = (xr - xl) - (yd - yu);
	// Height: the center against its neighbours, above them on a ridge, below them in a crack
	vec2  ph = px * 2.0;
	float h  = textureLod(sampler2D(texlpack, sampler_linear), uv, 0.0).a;
	float hn = textureLod(sampler2D(texlpack, sampler_linear), uv + vec2(ph.x, 0.0), 0.0).a +
	           textureLod(sampler2D(texlpack, sampler_linear), uv - vec2(ph.x, 0.0), 0.0).a +
	           textureLod(sampler2D(texlpack, sampler_linear), uv + vec2(0.0, ph.y), 0.0).a +
	           textureLod(sampler2D(texlpack, sampler_linear), uv - vec2(0.0, ph.y), 0.0).a;
	return clamp(div * 10.0 + (h - hn * 0.25) * 6.0, -1.0, 1.0);
}

// Signed curvature of the micro details, positive on bumps: a layer's, or the divergence of the normal map's slopes over
// a texel each way
float micro_curvature() {
	if (micro_from_layer()) {
		return layer_curvature();
	}
	if ((constants.maps & 4) == 0) {
		return 0.0;
	}
	vec2  px = 1.0 / vec2(textureSize(sampler2D(texnor, sampler_linear), 0));
	float xr = textureLod(sampler2D(texnor, sampler_linear), vin.uv + vec2(px.x, 0.0), 0.0).r;
	float xl = textureLod(sampler2D(texnor, sampler_linear), vin.uv - vec2(px.x, 0.0), 0.0).r;
	float yd = textureLod(sampler2D(texnor, sampler_linear), vin.uv + vec2(0.0, px.y), 0.0).g;
	float yu = textureLod(sampler2D(texnor, sampler_linear), vin.uv - vec2(0.0, px.y), 0.0).g;
	return clamp(((xr - xl) - (yd - yu)) * 10.0, -1.0, 1.0); // Encoded 0 - 1, y written as -b
}

// 0 - 1 along a user direction (+Y, -Y, +X, -X, +Z, -Z) over the mesh bounds
float position_gradient(vec3 p, int d) {
	vec3  pn = clamp((p - constants.pos_min) * constants.pos_inv, 0.0, 1.0);
	int   a  = d / 2;
	float t  = a == 0 ? pn.z : a == 1 ? pn.x : 1.0 - pn.y;
	return d % 2 == 0 ? t : 1.0 - t;
}

// World direction of a user direction (+Y, -Y, +X, -X, +Z, -Z)
vec3 user_direction(int d) {
	int pair = d / 2;
	return user_axis(pair == 0 ? 1 : pair == 1 ? 0 : 2) * (d % 2 == 0 ? 1.0 : -1.0);
}

// Grunge: tileable patterns over 0 - 1 (one repeat), white where the grunge is

float ghash(vec2 c, float seed) {
	uint h = uint(c.x) * 1597334673u ^ uint(c.y) * 3812015801u ^ uint(seed) * 2798796415u;
	h      = (h ^ (h >> 16u)) * 2246822519u;
	h      = (h ^ (h >> 13u)) * 3266489917u;
	h      = h ^ (h >> 16u);
	return float(h) / 4294967295.0;
}

vec2 ghash2(vec2 c, float seed) {
	return vec2(ghash(c, seed), ghash(c, seed + 7919.0));
}

// Value noise repeating every per cells
float gnoise(vec2 p, vec2 per, float seed) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = ghash(mod(i, per), seed);
	float b = ghash(mod(i + vec2(1.0, 0.0), per), seed);
	float c = ghash(mod(i + vec2(0.0, 1.0), per), seed);
	float d = ghash(mod(i + vec2(1.0, 1.0), per), seed);
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float gfbm(vec2 p, vec2 per, float seed, int octaves) {
	float sum = 0.0;
	float amp = 0.5;
	float tot = 0.0;
	for (int i = 0; i < 6; i += 1) {
		if (i >= octaves) {
			break;
		}
		sum += amp * gnoise(p, per, seed + float(i));
		tot += amp;
		p   *= 2.0;
		per *= 2.0;
		amp *= 0.5;
	}
	return sum / tot;
}

// Distances to the nearest and second nearest point of a jittered grid repeating every per cells
vec2 gcells(vec2 p, vec2 per, float seed) {
	vec2  i  = floor(p);
	vec2  f  = fract(p);
	float d1 = 8.0;
	float d2 = 8.0;
	for (int yy = -1; yy <= 1; yy += 1) {
		for (int xx = -1; xx <= 1; xx += 1) {
			vec2  o = vec2(float(xx), float(yy));
			float d = length(o + ghash2(mod(i + o, per), seed) - f);
			if (d < d1) {
				d2 = d1;
				d1 = d;
			}
			else if (d < d2) {
				d2 = d;
			}
		}
	}
	return vec2(d1, d2);
}

// A straight scratch of random angle and length in most cells, fading at its ends
float gscratches(vec2 p, vec2 per, float seed) {
	vec2  i = floor(p);
	vec2  f = fract(p);
	float v = 0.0;
	for (int yy = -1; yy <= 1; yy += 1) {
		for (int xx = -1; xx <= 1; xx += 1) {
			vec2 o = vec2(float(xx), float(yy));
			vec2 c = mod(i + o, per);
			vec2 h = ghash2(c, seed);
			vec2 g = ghash2(c, seed + 31.0);
			if (g.y < 0.3) {
				continue;
			}
			float a   = g.x * 3.14159;
			vec2  dir = vec2(cos(a), sin(a));
			vec2  r   = f - o - h;
			float len = 0.3 + 0.5 * h.y; // Half its length, cells
			float t   = dot(r, dir);
			float d   = length(r - dir * clamp(t, -len, len));
			float end = 1.0 - smoothstep(len * 0.4, len, abs(t));
			v         = max(v, (1.0 - smoothstep(0.008, 0.03, d)) * end * (0.5 + 0.5 * g.y));
		}
	}
	return v;
}

float grunge(int pat, vec2 uv, float seed) {
	float s = seed * 32.0;
	if (pat == 1) { // Scratches
		return max(gscratches(uv * 5.0, vec2(5.0), s), gscratches(uv * 12.0, vec2(12.0), s + 1.0) * 0.7);
	}
	if (pat == 2) { // Leaks: noise stretched down the v axis
		float n = gfbm(vec2(uv.x * 40.0, uv.y * 2.0), vec2(40.0, 2.0), s, 5);
		return smoothstep(0.5, 0.75, n) * (0.6 + 0.4 * gfbm(uv * 8.0, vec2(8.0), s + 8.0, 3));
	}
	if (pat == 3) { // Rust: warped blotches and pits
		vec2  w    = vec2(gfbm(uv * 4.0, vec2(4.0), s + 8.0, 3), gfbm(uv * 4.0, vec2(4.0), s + 16.0, 3)) - 0.5;
		float n    = gfbm(uv * 5.0 + w * 1.5, vec2(5.0), s, 6);
		float pits = smoothstep(0.68, 0.76, gfbm(uv * 40.0, vec2(40.0), s + 24.0, 2));
		return max(smoothstep(0.5, 0.6, n) * (0.75 + 0.25 * gfbm(uv * 20.0, vec2(20.0), s + 4.0, 2)), pits * 0.7);
	}
	if (pat == 4) { // Speckles
		float a = smoothstep(0.62, 0.68, gfbm(uv * 48.0, vec2(48.0), s, 3));
		float b = smoothstep(0.66, 0.7, gfbm(uv * 96.0, vec2(96.0), s + 8.0, 2)) * 0.7;
		return max(a, b) * (0.5 + 0.5 * gfbm(uv * 6.0, vec2(6.0), s + 16.0, 3));
	}
	if (pat == 5) { // Cracks: edges of warped cells where a broader noise lets them show
		vec2  w     = vec2(gfbm(uv * 6.0, vec2(6.0), s + 8.0, 3), gfbm(uv * 6.0, vec2(6.0), s + 16.0, 3)) - 0.5;
		vec2  c     = gcells(uv * 7.0 + w * 1.2, vec2(7.0), s);
		vec2  cf    = gcells(uv * 16.0 + w * 2.4, vec2(16.0), s + 4.0);
		float big   = 1.0 - smoothstep(0.0, 0.05, c.y - c.x);
		float fine  = (1.0 - smoothstep(0.0, 0.04, cf.y - cf.x)) * 0.6;
		float where = smoothstep(0.35, 0.55, gfbm(uv * 3.0, vec2(3.0), s + 24.0, 3));
		return max(big, fine) * where;
	}
	// Spots: blotches and smaller dots
	float blot = smoothstep(0.56, 0.66, gfbm(uv * 6.0, vec2(6.0), s, 5));
	float dots = smoothstep(0.72, 0.78, gfbm(uv * 32.0, vec2(32.0), s + 8.0, 2)) * 0.8;
	return max(blot, dots);
}

// Fill value at uv (repeats): the image's gray (alpha keeps stencils), or the grunge
float fill_source(vec2 uv) {
	if (int(constants.params0.y + 0.5) == 1) {
		vec4 c = textureLod(sampler2D(teximg, sampler_linear), uv, 0.0);
		return dot(c.rgb, vec3(0.299, 0.587, 0.114)) * c.a;
	}
	return grunge(int(constants.params0.w + 0.5), fract(uv), constants.params2.z);
}

vec2 fill_rotate(vec2 uv) {
	float a = radians(constants.params1.z);
	float c = cos(a);
	float s = sin(a);
	return vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
}

// Image or grunge projected on the uvs, or on three planes blended by the normal (triplanar)
float fill(vec3 p) {
	float scale = constants.params1.y;
	float x     = 0.0;
	if (constants.params1.x < 0.5) {
		x = fill_source((fill_rotate(vin.uvl - 0.5) + 0.5) * scale);
	}
	else {
		vec3 q = (p - constants.pos_min) * constants.noise_inv * scale;
		vec3 w = pow(abs(normalize(vin.nor)), vec3(4.0, 4.0, 4.0));
		w      = w / (w.x + w.y + w.z);
		// Planes upright: the image's v goes down the engine's z
		x = w.x * fill_source(fill_rotate(vec2(q.y, -q.z))) + w.y * fill_source(fill_rotate(vec2(q.x, -q.z))) +
		    w.z * fill_source(fill_rotate(q.xy));
	}
	float c = constants.params2.x; // Contrast narrows, balance shifts: identity at 0.5, 0
	x       = clamp((x - (1.0 - constants.params1.w)) / (1.0 - 0.98 * c) + 0.5, 0.0, 1.0);
	return constants.params2.y > 0.5 ? 1.0 - x : x;
}

void main() {
	vec3 p = vin.pos;

	// Noise from the 3d position: seamless across uv islands, centred on 0
	float noise  = 0.0;
	float amount = constants.params3.y;
	if (amount > 0.0 && constants.type != 9 && constants.type != 1) { // Not 3D Noise's distortion, fills have none
		vec3 q = (p - constants.pos_min) * constants.noise_inv * constants.params3.x;
		q      = q + fract(vec3(0.1031, 0.1030, 0.0973) * constants.params3.z) * 97.0;
		noise  = (tex_noise_fbm(q, constants.params3.w - 1.0, 0.5, 2.0) - 0.5) * 2.0 * amount;
	}

	float v = 0.0;
	if (constants.type == 4) { // Edge Wear
		float c      = textureLod(sampler2D(texcurv, sampler_linear), vin.uv, 0.0).r;
		float convex = max(c - 0.5, 0.0) * 2.0;
		v            = sharp(convex + noise - (1.0 - constants.params0.x), constants.params0.y);
	}
	else if (constants.type == 5) { // Dirt
		float c      = textureLod(sampler2D(texcurv, sampler_linear), vin.uv, 0.0).r;
		float ao     = textureLod(sampler2D(texao, sampler_linear), vin.uv, 0.0).r;
		float cavity = max(0.5 - c, 0.0) * 2.0;
		v            = sharp(mix(cavity, 1.0 - ao, constants.params0.z) + noise - (1.0 - constants.params0.x), constants.params0.y);
	}
	else if (constants.type == 1) { // Fill of an image or grunge
		v = fill(p);
	}
	else if (constants.type == 6) { // Position Gradient
		int   axis = int(constants.params0.x + 0.5);
		vec3  pn   = (surface_position() - constants.pos_min) * constants.pos_inv; // 0 - 1 over the bounds
		float t    = axis == 0 ? pn.x : axis == 1 ? pn.z : 1.0 - pn.y;
		t          = t + noise;
		float d    = constants.params0.z - constants.params0.y;
		if (abs(d) < 0.0001) {
			v = t >= constants.params0.y ? 1.0 : 0.0;
		}
		else {
			float x = clamp((t - constants.params0.y) / d, 0.0, 1.0);
			v       = x * x * (3.0 - 2.0 * x);
		}
	}
	else if (constants.type == 7) { // Directional
		vec3 dir = user_direction(int(constants.params0.x + 0.5));
		vec3 n   = surface_normal(0.0);
		v        = sharp(dot(n, dir) * 0.5 + 0.5 + noise - (1.0 - constants.params0.y), constants.params0.z);
	}
	else if (constants.type == 9) { // 3D Noise: pattern, coverage, sharpness
		vec3 q = (p - constants.pos_min) * constants.noise_inv * constants.params3.x;
		q      = q + fract(vec3(0.1031, 0.1030, 0.0973) * constants.params3.z) * 97.0;
		if (constants.params3.y > 0.0) { // Distortion warps the domain
			vec3 w = vec3(tex_noise_fbm(q + vec3(5.2, 1.3, 7.1), 1.0, 0.5, 2.0), tex_noise_fbm(q + vec3(1.7, 9.2, 3.4), 1.0, 0.5, 2.0),
			              tex_noise_fbm(q + vec3(8.3, 2.8, 4.6), 1.0, 0.5, 2.0));
			q      = q + (w - 0.5) * 3.0 * constants.params3.y;
		}
		float n = clamp(pattern(int(constants.params0.x + 0.5), q, constants.params3.w), 0.0, 1.0);
		float w = mix(0.3, 0.005, clamp(constants.params0.z, 0.0, 1.0)); // As sharp(): coverage 0 black, 1 white
		v       = sharp(n - mix(1.0 + w, -w, clamp(constants.params0.y, 0.0, 1.0)), constants.params0.z);
	}
	else if (constants.type == 10) { // Light: azimuth, elevation (90 from straight above), softness wraps, ao weight
		float az = radians(constants.params0.x);
		float el = radians(constants.params0.y);
		vec3  l  = vec3(cos(el) * sin(az), -cos(el) * cos(az), sin(el)); // User (sin az, sin el, cos az), Y up
		vec3  n  = surface_normal(0.0);
		float s  = constants.params0.z;
		float ao = textureLod(sampler2D(texao, sampler_linear), vin.uv, 0.0).r;
		v        = clamp((dot(n, l) + s) / (1.0 + s), 0.0, 1.0) * mix(1.0, ao, constants.params0.w) + noise;
	}
	else if (constants.type == 11) { // Thickness: side (0 thin, 1 thick), amount, sharpness
		float t = textureLod(sampler2D(texthick, sampler_linear), vin.uv, 0.0).r;
		float x = constants.params0.x > 0.5 ? t : 1.0 - t;
		v       = sharp(x + noise - (1.0 - constants.params0.y), constants.params0.z);
	}

	else if (constants.type == 15) { // Mask Builder: the parts weighted and joined (screen), then balance and contrast
		float c     = textureLod(sampler2D(texcurv, sampler_linear), vin.uv, 0.0).r - 0.5;
		float micro = constants.params2.y;
		c           = clamp(c + micro_curvature() * micro * 0.5, -0.5, 0.5);
		float ao    = textureLod(sampler2D(texao, sampler_linear), vin.uv, 0.0).r;
		float nd    = max(dot(surface_normal(micro), user_direction(int(constants.params1.z + 0.5))), 0.0);
		float pg    = position_gradient(surface_position(), int(constants.params2.x + 0.5));
		float keep  = (1.0 - constants.params0.z * max(c, 0.0) * 2.0) * (1.0 - constants.params0.w * max(-c, 0.0) * 2.0) *
		             (1.0 - constants.params1.x * (1.0 - ao)) * (1.0 - constants.params1.y * nd) * (1.0 - constants.params1.w * pg);
		v = sharp(1.0 - keep + noise - (1.0 - constants.params0.x), constants.params0.y);
		if (constants.params2.z > 0.5) {
			v = 1.0 - v;
		}
	}
	else if (constants.type == 16) { // Ambient Occlusion: side (0 occluded, 1 open), amount, sharpness
		float ao = textureLod(sampler2D(texao, sampler_linear), vin.uv, 0.0).r;
		float x  = constants.params0.x > 0.5 ? ao : 1.0 - ao;
		v        = sharp(x + noise - (1.0 - constants.params0.y), constants.params0.z);
	}

	v          = clamp(v, 0.0, 1.0);
	frag_color = vec4(v, v, v, 1.0);
}
#endif
