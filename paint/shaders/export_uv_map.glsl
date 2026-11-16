#version 450

// Uv coverage of a rectangle of the texture (io/export_tiled.c): dilate_map.glsl, the uv rectangle rect (u0, v0, du, dv)
// mapped to the target
layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec4 rect;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;

#ifdef VERT
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec2 in_nor;
layout(location = 2) in vec2 in_tex;

void main() {
	vec2 uv = (in_tex - constants.rect.xy) / constants.rect.zw;
	vec2 tex_coord = vec2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
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
