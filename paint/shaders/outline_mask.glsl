#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 WVP;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;

#ifdef VERT
layout(location = 0) in vec4 in_pos;

void main() {	gl_Position = constants.WVP * vec4(in_pos.xyz, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) out vec4 frag_color;

void main() {
	frag_color = vec4(1.0, 1.0, 1.0, 1.0);
}
#endif
