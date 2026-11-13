#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec4 params; // level, samples, alpha, pass
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D radiance;
layout(set = 0, binding = 3) uniform texture2D noise;

const float PI = 3.14159265358979;
const float PI2 = 6.28318530718;

float rand(vec2 co) {
	// return fract(sin(dot(co.xy, vec2(12.9898, 78.233)) % 3.14) * 43758.5453);
	vec2 uv = vec2(fract(co.x * 12.34), fract(co.y * 12.34));
	return texture(sampler2D(noise, sampler_linear), uv).r;
}

vec2 equirect(vec3 normal) {
	float phi = acos(normal.z);
	float theta = atan(-normal.y, normal.x) + PI;
	return vec2(theta / PI2, phi / PI);
}

vec3 reverse_equirect(vec2 co) {
	float theta = co.x * PI2 - PI;
	float phi = co.y * PI;
	return vec3(sin(phi) * cos(theta), -(sin(phi) * sin(theta)), cos(phi));
}

// vec3 cos_weighted_hemisphere_direction(vec3 n, vec2 co, uint seed) {
vec3 cos_weighted_hemisphere_direction(vec3 n, vec2 co, float seed) {
	vec2 r = vec2(rand(co * seed), rand(co * seed * 2.0));
	vec3 uu = normalize(cross(n, vec3(0.0, 1.0, 1.0)));
	vec3 vv = cross(uu, n);
	float ra = sqrt(r.y);
	float rx = ra * cos(PI2 * r.x);
	float ry = ra * sin(PI2 * r.x);
	float rz = sqrt(1.0 - r.y);
	vec3 rr = rx * uu + ry * vv + rz * n;
	return normalize(rr);
}

#ifdef VERT
layout(location = 0) in vec2 in_pos;
layout(location = 0) out Varyings {
	vec2 tex;
} vout;

void main() {	vout.tex = in_pos.xy * 0.5 + 0.5;
	vout.tex.y = 1.0 - vout.tex.y;
	gl_Position = vec4(in_pos.xy, 0.0, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 tex;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	vec4 color = vec4(0.0, 0.0, 0.0, constants.params.z);
	vec3 n = reverse_equirect(vin.tex);

	//for (int i = 0; i < int(constants.params.y); i += 1) {
	int i = 0;
	while (i < int(constants.params.y)) {
		vec3 dir = normalize(mix(n, cos_weighted_hemisphere_direction(n, vin.tex, float(i) + constants.params.w * constants.params.y), constants.params.x));
		vec3 sampled = texture(sampler2D(radiance, sampler_linear), equirect(dir)).rgb;
		sampled = min(sampled, vec3(10.0, 10.0, 10.0));
		color.rgb = color.rgb + sampled;

		//
		i += 1;
		//
	}
	color.rgb = color.rgb / constants.params.y;
	frag_color = color;
}
#endif
