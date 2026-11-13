#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec4 empty;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex0;
layout(set = 0, binding = 3) uniform texture2D tex1;
layout(set = 0, binding = 4) uniform texture2D tex2;

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
layout(location = 0) out vec4 frag_out_0;
layout(location = 1) out vec4 frag_out_1;
layout(location = 2) out vec4 frag_out_2;

void main() {
	frag_out_0 = textureLod(sampler2D(tex0, sampler_linear), vin.tex, 0.0);
	frag_out_1 = textureLod(sampler2D(tex1, sampler_linear), vin.tex, 0.0);
	frag_out_2 = textureLod(sampler2D(tex2, sampler_linear), vin.tex, 0.0);
	return;
}
#endif
