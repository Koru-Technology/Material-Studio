#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec2 center;
	layout(offset = 8) vec2 size;
	layout(offset = 16) float radius;
	layout(offset = 20) float width;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;

#ifdef VERT
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_tex;
layout(location = 2) in vec4 in_col;
layout(location = 0) out Varyings {
	vec2 ndc;
	vec4 col;
} vout;

void main() {
	float keep = in_tex.x; // hlsl
	gl_Position = vec4(in_pos, 0.0, 1.0);
	vout.ndc = in_pos;
	vout.col = in_col;
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 ndc;
	vec4 col;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	// Pixel distance from the ring line, fixed width at any radius with a 1 px soft edge
	vec2 p = vec2(vin.ndc.x * 0.5 + 0.5, 0.5 - vin.ndc.y * 0.5) * constants.size;
	float d = abs(distance(p, constants.center) - constants.radius);
	float a = 1.0 - smoothstep(constants.width * 0.5 - 0.75, constants.width * 0.5 + 0.75, d);
	frag_color = vec4(vin.col.rgb, vin.col.a * a);
}
#endif
