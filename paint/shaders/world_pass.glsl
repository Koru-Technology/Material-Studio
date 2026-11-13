#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 SMVP;
	layout(offset = 64) vec4 envmap_data_world; // angle, tonemap_strength, empty, strength
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D envmap;

const float PI = 3.1415926535;
const float PI2 = 6.283185307;

vec2 envmap_equirect(vec3 normal, float angle) {
	float phi = acos(normal.z);
	float theta = atan(-normal.y, normal.x) + PI + angle;
	return vec2(theta / PI2, phi / PI);
}

vec3 tonemap_filmic(vec3 color) {
	// Based on Filmic Tonemapping Operators http://filmicgames.com/archives/75
	// vec3 x = max(vec3(0.0, 0.0, 0.0), color - 0.004);
	vec3 x;
	x.x = max(0.0, color.x - 0.004);
	x.y = max(0.0, color.y - 0.004);
	x.z = max(0.0, color.z - 0.004);
	return (x * (x * 6.2 + 0.5)) / (x * (x * 6.2 + 1.7) + 0.06);
}

// View transforms, scene linear in, display out
vec3 tonemap_aces(vec3 x) {
	// Fitted ACES filmic curve (Narkowicz)
	vec3 c = clamp((x * (x * 2.51 + vec3(0.03, 0.03, 0.03))) / (x * (x * 2.43 + vec3(0.59, 0.59, 0.59)) + vec3(0.14, 0.14, 0.14)), vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));
	return pow(c, vec3(0.4545, 0.4545, 0.4545));
}

vec3 tonemap_agx(vec3 col) {
	// AgX base look (minimal fit by bwrensch)
	vec3 v;
	v.x = dot(col, vec3(0.842479062253094, 0.0784335999999992, 0.0792237451477643));
	v.y = dot(col, vec3(0.0423282422610123, 0.878468636469772, 0.0791661274605434));
	v.z = dot(col, vec3(0.0423756549057051, 0.0784336, 0.879142973793104));
	float max_ev = 4.026069;
	float min_ev = max_ev - 16.5; // -12.47393, written without a unary minus
	v = clamp(log(max(v, vec3(0.00001, 0.00001, 0.00001))) * 1.442695, vec3(min_ev, min_ev, min_ev), vec3(max_ev, max_ev, max_ev)); // log2
	v = (v - vec3(min_ev, min_ev, min_ev)) / (max_ev - min_ev);
	vec3 v2 = v * v;
	vec3 v4 = v2 * v2;
	v = v4 * v2 * 15.5 + v4 * 31.96 + v2 * 0.4298 + v * 0.1191 - v4 * v * 40.14 - v2 * v * 6.868 - vec3(0.00232, 0.00232, 0.00232);
	vec3 o;
	o.x = v.x * 1.19687900512017 - v.y * 0.0980208811401368 - v.z * 0.0990297440797205;
	o.y = v.y * 1.15190312990417 - v.x * 0.0528968517574562 - v.z * 0.0989611768448433;
	o.z = v.z * 1.15107367264116 - v.x * 0.0529716355144438 - v.y * 0.0980434501171241;
	return clamp(o, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));
}

vec3 tonemap_standard(vec3 x) {
	return pow(clamp(x, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)), vec3(0.4545, 0.4545, 0.4545));
}

// 0 none, 1 filmic, 2 aces, 3 agx, 4 standard
vec3 view_transform(vec3 color, float mode) {
	if (mode < 0.5) {
		return color;
	}
	if (mode < 1.5) {
		return tonemap_filmic(color);
	}
	if (mode < 2.5) {
		return tonemap_aces(color);
	}
	if (mode < 3.5) {
		return tonemap_agx(color);
	}
	return tonemap_standard(color);
}

#ifdef VERT
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_nor;
layout(location = 0) out Varyings {
	vec3 nor;
} vout;

void main() {	gl_Position = constants.SMVP * vec4(in_pos, 1.0);
	vout.nor = in_nor;
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec3 nor;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	vec3 n = normalize(vin.nor);
	vec4 color;
	color.rgb = texture(sampler2D(envmap, sampler_linear), envmap_equirect(-n, constants.envmap_data_world.x)).rgb * constants.envmap_data_world.w;

	// Tonemap with gamma - non-lit modes
	color.rgb = view_transform(color.rgb, constants.envmap_data_world.y);

	color.a = 0.0; // Mark as non-opaque
	frag_color = color;
}
#endif
