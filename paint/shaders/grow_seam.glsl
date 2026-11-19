#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec2 tex_size;
	layout(offset = 8) vec2 mode; // x: 1 shrinks (min), 0 grows (max). y: 1 takes the diagonal neighbors too
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;
layout(set = 0, binding = 3) uniform texture2D texnbr0;
layout(set = 0, binding = 4) uniform texture2D texnbr1;

// One grow (max) or shrink (min) step of a texel, as blur_seam.glsl: texnbr0/1 hold the uvs of the left, right, up and down
// neighbors, across uv seams in the neighboring island, negative where there is none (a texel at an open border of the
// mesh does not shrink from it). The diagonals are the up and down neighbors of the left and right ones.

#ifdef VERT
layout(location = 0) in vec2 in_pos;
layout(location = 0) out Varyings {
	vec2 tex;
} vout;

void main() {
	vout.tex   = in_pos.xy * 0.5 + 0.5;
	vout.tex.y = 1.0 - vout.tex.y;
	gl_Position = vec4(in_pos.xy, 0.0, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 tex;
} vin;
layout(location = 0) out vec4 frag_color;

float pick(float v, float s) {
	return constants.mode.x > 0.5 ? min(v, s) : max(v, s);
}

float at(vec2 uv) {
	return textureLod(sampler2D(tex, sampler_linear), uv, 0.0).r;
}

// The up and down neighbors of the texel at uv
float diagonals(float v, vec2 uv) {
	vec4 d = texelFetch(sampler2D(texnbr1, sampler_linear), ivec2(uv * constants.tex_size), 0);
	if (d.x >= 0.0) {
		v = pick(v, at(d.xy));
	}
	if (d.z >= 0.0) {
		v = pick(v, at(d.zw));
	}
	return v;
}

void main() {
	ivec2 coord = ivec2(vin.tex * constants.tex_size);
	vec4  n0    = texelFetch(sampler2D(texnbr0, sampler_linear), coord, 0);
	vec4  n1    = texelFetch(sampler2D(texnbr1, sampler_linear), coord, 0);
	float v     = texelFetch(sampler2D(tex, sampler_linear), coord, 0).r;
	bool  diag  = constants.mode.y > 0.5;
	if (n0.x >= 0.0) {
		v = pick(v, at(n0.xy));
		if (diag) {
			v = diagonals(v, n0.xy);
		}
	}
	if (n0.z >= 0.0) {
		v = pick(v, at(n0.zw));
		if (diag) {
			v = diagonals(v, n0.zw);
		}
	}
	if (n1.x >= 0.0) {
		v = pick(v, at(n1.xy));
	}
	if (n1.z >= 0.0) {
		v = pick(v, at(n1.zw));
	}
	frag_color = vec4(v, v, v, 1.0);
}
#endif
