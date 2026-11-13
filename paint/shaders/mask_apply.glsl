#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) float mask_opacity;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex0;
layout(set = 0, binding = 3) uniform texture2D texa;

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
	vec4 col0 = textureLod(sampler2D(tex0, sampler_linear), vin.tex, 0.0);
	float mask = textureLod(sampler2D(texa, sampler_linear), vin.tex, 0.0).r;
	mask = mix(1.0, mask, constants.mask_opacity);
	frag_color = vec4(col0.rgb, col0.a * mask);
}
#endif
