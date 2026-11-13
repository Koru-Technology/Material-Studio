#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) float empty;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;

#ifdef VERT
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec2 in_nor;
layout(location = 2) in vec2 in_tex;

void main() {	vec2 tex_coord = vec2(in_tex.x * 2.0 - 1.0, (1.0 - in_tex.y) * 2.0 - 1.0);
	gl_Position = vec4(tex_coord, 0.0, 1.0);
	float keep = in_pos.x + in_nor.x;
}
#endif

#ifdef FRAG
layout(location = 0) out float frag_color;

void main() {
	frag_color = 1.0;
}
#endif
