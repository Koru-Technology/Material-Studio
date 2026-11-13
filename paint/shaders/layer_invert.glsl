#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec4 empty;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;

#ifdef VERT
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_tex;
layout(location = 2) in vec4 in_col;
layout(location = 0) out Varyings {
	vec2 tex;
	vec4 col;
} vout;

void main() {	gl_Position = vec4(in_pos, 0.0, 1.0);
	vout.tex = in_tex;
	vout.col = in_col;
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 tex;
	vec4 col;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	frag_color = vec4(1.0, 1.0, 1.0, 1.0) - textureLod(sampler2D(tex, sampler_linear), vin.tex, 0.0) * vin.col;
}
#endif
