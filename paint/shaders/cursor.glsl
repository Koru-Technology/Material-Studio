#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 VP;
	layout(offset = 64) mat4 invVP;
	layout(offset = 128) vec2 mouse;
	layout(offset = 136) vec2 tex_step;
	layout(offset = 144) float radius;
	layout(offset = 160) vec3 camera_right;
	layout(offset = 176) vec3 tint;
	layout(offset = 192) vec3 camera_up;
	layout(offset = 208) float camera_align;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D gbufferD;

vec3 get_pos(vec2 uv) {
	float depth = textureLod(sampler2D(gbufferD, sampler_linear), vec2(uv.x, 1.0 - uv.y), 0.0).r;
	vec4 wpos = vec4(uv * 2.0 - 1.0, depth, 1.0);
	wpos = constants.invVP * wpos;
	return wpos.xyz / wpos.w;
}

vec3 get_normal(vec3 p0, vec2 uv) {
	vec3 p1 = get_pos(uv + vec2(constants.tex_step.x * 4.0, 0.0));
	vec3 p2 = get_pos(uv + vec2(0.0, constants.tex_step.y * 4.0));
	return normalize(cross(p2 - p0, p1 - p0));
}

#ifdef VERT
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec2 in_nor;
layout(location = 2) in vec2 in_tex;
layout(location = 0) out Varyings {
	vec2 tex;
} vout;

void main() {
	float keep = in_pos.x + in_nor.x; // hlsl
	vout.tex = in_tex;
	vec3 wpos = get_pos(constants.mouse);
	vec2 uv1 = constants.mouse + constants.tex_step * vec2(4.0, 4.0);
	vec2 uv2 = constants.mouse - constants.tex_step * vec2(4.0, 4.0);
	vec3 wpos1 = get_pos(uv1);
	vec3 wpos2 = get_pos(uv2);
	vec3 n = normalize(get_normal(wpos, constants.mouse) + get_normal(wpos1, uv1) + get_normal(wpos2, uv2));
	vec3 n_tan;
	vec3 n_bin;

	if (constants.camera_align > 0.0) {
		n_tan = constants.camera_right;
		n_bin = constants.camera_up;
	}
	else {
		n_tan = normalize(constants.camera_right - n * dot(constants.camera_right, n));
		n_bin = cross(n_tan, n);
	}

	if (gl_VertexIndex == 0) {
		wpos += normalize(-n_tan - n_bin) * 0.7 * constants.radius;
	}
	/*else */if (gl_VertexIndex == 1) {
		wpos += normalize( n_tan - n_bin) * 0.7 * constants.radius;
	}
	/*else */if (gl_VertexIndex == 2) {
		wpos += normalize( n_tan + n_bin) * 0.7 * constants.radius;
	}
	/*else */if (gl_VertexIndex == 3) {
		wpos += normalize(-n_tan + n_bin) * 0.7 * constants.radius;
	}

	gl_Position = constants.VP * vec4(wpos, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 tex;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	float radius = 0.45;
	float thickness = 0.03;
	float dist = distance(vin.tex, vec2(0.5, 0.5));
	float ring = smoothstep(radius - thickness, radius, dist) - smoothstep(radius, radius + thickness, dist);
	frag_color = vec4(constants.tint, min(ring, 0.6));
}
#endif
