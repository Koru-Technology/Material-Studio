#version 450

layout(set = 0, binding = 1) uniform sampler sampler_linear;

#ifdef VERT
layout(location = 0) in vec3 in_pos;

void main() {	gl_Position = vec4(in_pos, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) out vec4 frag_color;

void main() {
	frag_color = vec4(1.0, 0.0, 0.0, 1.0);
}
#endif
