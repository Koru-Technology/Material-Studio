#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec2 size; // ID map size, it is read texel by texel so part edges are not blended
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D texpaint_colorid;
layout(set = 0, binding = 3) uniform texture2D texcolorid;

// Picked ID map parts into a mask: white where the ID map has one of the picked colors, see ui_id_mask.c

// 16x1 picked colors, alpha 0 for free slots

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
	vec2 texel = min(vin.tex * constants.size, constants.size - vec2(1.0, 1.0));
	vec3 c = texelFetch(sampler2D(texcolorid, sampler_linear), ivec2(ivec2(uint(texel.x), uint(texel.y))), 0).rgb;
	float hit = 0.0;
	for (int i = 0; i < 16; i += 1) {
		vec4 s = texelFetch(sampler2D(texpaint_colorid, sampler_linear), ivec2(ivec2(uint(i), uint(0))), 0);
		if (s.a > 0.5 && abs(s.r - c.r) < 0.02 && abs(s.g - c.g) < 0.02 && abs(s.b - c.b) < 0.02) {
			hit = 1.0;
		}
	}
	if (hit < 0.5) {
		discard;
	}
	frag_color = vec4(1.0, 1.0, 1.0, 1.0);
}
#endif
