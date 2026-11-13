#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 WVP;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D my_texture;

#ifdef VERT
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec2 in_tex;
layout(location = 0) out Varyings {
	vec2 tex;
} vout;

void main() {	vout.tex = in_tex;
	gl_Position = constants.WVP * vec4(in_pos.xyz, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 tex;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	frag_color = texture(sampler2D(my_texture, sampler_linear), vin.tex);
}
#endif
