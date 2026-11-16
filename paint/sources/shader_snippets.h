#pragma once

// GLSL functions the material shader generators (render/make_*.c) and some nodes paste into the shaders they write

char *str_hue_sat = "\
vec3 ms_hsv_to_rgb(vec3 c) { \
	vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0); \
	vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www); \
	return mix(K.xxx, clamp(p - K.xxx, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)), c.y) * c.z; \
} \
vec3 ms_rgb_to_hsv(vec3 c) { \
	vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0); \
	vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g)); \
	vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r)); \
	float d = q.x - min(q.w, q.y); \
	float e = 0.0000000001; \
	return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x); \
} \
vec3 hue_sat(vec3 col, vec4 shift) { \
	vec3 hsv = ms_rgb_to_hsv(col); \
	hsv.x += shift.x; \
	hsv.y *= shift.y; \
	hsv.z *= shift.z; \
	return mix(ms_hsv_to_rgb(hsv), col, shift.w); \
} \
";

char *str_brightcontrast = "\
vec3 brightcontrast(vec3 col, float bright, float contr) { \
	float a = 1.0 + contr; \
	float b = bright - contr * 0.5; \
	return max(a * col + b, vec3(0.0, 0.0, 0.0)); \
} \
";

char *str_cotangent_frame = "\
mat3 cotangent_frame(vec3 n, vec3 p, vec2 tex_coord) { \
	vec2 duv1 = dFdx(tex_coord); \
	vec2 duv2 = dFdy(tex_coord); \
	vec3 dp1 = dFdx(p); \
	vec3 dp2 = dFdy(p); \
	vec3 dp2perp = cross(dp2, n); \
	vec3 dp1perp = cross(n, dp1); \
	vec3 t = dp2perp * duv1.x + dp1perp * duv2.x; \
	vec3 b = dp2perp * duv1.y + dp1perp * duv2.y; \
	float invmax = inversesqrt(max(dot(t, t), dot(b, b))); \
	return mat3(t * invmax, b * invmax, n); \
} \
";

char *str_octahedron_wrap = "\
vec2 octahedron_wrap(vec2 v) { \
	vec2 a; \
	if (v.x >= 0.0) { a.x = 1.0; } else { a.x = -1.0; } \
	if (v.y >= 0.0) { a.y = 1.0; } else { a.y = -1.0; } \
	vec2 r; \
	r.x = abs(v.y); \
	r.y = abs(v.x); \
	r.x = 1.0 - r.x; \
	r.y = 1.0 - r.y; \
	return r * a; \
} \
";

char *str_pack_float_int16 = "\
float pack_f32_i16(float f, uint i) { \
	return 0.062485207147583624 * min(f, 0.9990234375) + 0.062500476102698687 * float(i); \
} \
";

char *str_dither_bayer = "\
float dither_bayer(vec2 uv) { \
	int x = int(mod(uv.x, 4.0)); \
	int y = int(mod(uv.y, 4.0)); \
	if (y == 0) { \
		if (x == 0) { \
			return 0.0 / 16.0; \
		} \
		if (x == 1) { \
			return 8.0 / 16.0; \
		} \
		if (x == 2) { \
			return 2.0 / 16.0; \
		} \
		return 10.0 / 16.0; \
	} \
	if (y == 1) { \
		if (x == 0) { \
			return 12.0 / 16.0; \
		} \
		if (x == 1) { \
			return 4.0 / 16.0; \
		} \
		if (x == 2) { \
			return 14.0 / 16.0; \
		} \
		return 6.0 / 16.0; \
	} \
	if (y == 2) { \
		if (x == 0) { \
			return 3.0 / 16.0; \
		} \
		if (x == 1) { \
			return 11.0 / 16.0; \
		} \
		if (x == 2) { \
			return 1.0 / 16.0; \
		} \
		return 9.0 / 16.0; \
	} \
	if (x == 0) { \
		return 15.0 / 16.0; \
	} \
	if (x == 1) { \
		return 7.0 / 16.0; \
	} \
	if (x == 2) { \
		return 13.0 / 16.0; \
	} \
	return 5.0 / 16.0; \
} \
";
