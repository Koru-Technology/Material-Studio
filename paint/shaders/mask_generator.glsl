#version 450

// One mask generator (util_mask_generators.c): the paint mesh drawn flat in uv space, R the generator's value and A the
// coverage, dilated afterwards by mask_generator_dilate. Axes are Y up as the user sees them, the engine is Z up.
// Patterns come from the 3d position, seamless across uv islands.
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
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D texcurv; // Signed: 0.5 flat, brighter ridges, darker grooves
layout(set = 0, binding = 3) uniform texture2D texao;
layout(set = 0, binding = 4) uniform texture2D texthick; // White thick

layout(location = 0) VARYING Varyings {
	vec3 pos;
	vec3 nor;
	vec2 uv; // First uv set, the mesh maps are baked in it
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

void main() {
	vec3 p = vin.pos;

	// Noise from the 3d position: seamless across uv islands, centred on 0
	float noise  = 0.0;
	float amount = constants.params3.y;
	if (amount > 0.0 && constants.type != 9) {
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
	else if (constants.type == 6) { // Position Gradient
		int   axis = int(constants.params0.x + 0.5);
		vec3  pn   = (p - constants.pos_min) * constants.pos_inv; // 0 - 1 over the bounds
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
		int  d    = int(constants.params0.x + 0.5); // +Y, -Y, +X, -X, +Z, -Z
		int  pair = d / 2;
		vec3 dir  = user_axis(pair == 0 ? 1 : pair == 1 ? 0 : 2) * (d % 2 == 0 ? 1.0 : -1.0);
		vec3 n   = normalize(constants.N * vin.nor);
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
		vec3  n  = normalize(constants.N * vin.nor);
		float s  = constants.params0.z;
		float ao = textureLod(sampler2D(texao, sampler_linear), vin.uv, 0.0).r;
		v        = clamp((dot(n, l) + s) / (1.0 + s), 0.0, 1.0) * mix(1.0, ao, constants.params0.w) + noise;
	}
	else if (constants.type == 11) { // Thickness: side (0 thin, 1 thick), amount, sharpness
		float t = textureLod(sampler2D(texthick, sampler_linear), vin.uv, 0.0).r;
		float x = constants.params0.x > 0.5 ? t : 1.0 - t;
		v       = sharp(x + noise - (1.0 - constants.params0.y), constants.params0.z);
	}

	v          = clamp(v, 0.0, 1.0);
	frag_color = vec4(v, v, v, 1.0);
}
#endif
