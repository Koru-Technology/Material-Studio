#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 VP;
	layout(offset = 64) vec3 color;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;

#ifdef VERT
layout(location = 0) in vec3 in_pos;

void main() {	gl_Position = constants.VP * vec4(in_pos, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) out vec4 frag_color;

void main() {
	frag_color = vec4(constants.color, 1.0);
}
#endif
