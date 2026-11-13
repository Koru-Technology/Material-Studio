#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 VP;
	layout(offset = 64) vec2 center;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;

#ifdef VERT
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec4 in_col;
layout(location = 0) out Varyings {
	vec4 wpos;
	vec4 col;
} vout;

void main() {	vout.wpos = in_pos;
	vout.col = in_col;
	gl_Position = constants.VP * vec4(in_pos.xyz, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec4 wpos;
	vec4 col;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	// pos.w is the distance from the center the line fades out at
	float fade = clamp(1.0 - distance(vin.wpos.xy, constants.center) / vin.wpos.w, 0.0, 1.0);
	frag_color = vec4(vin.col.rgb, vin.col.a * fade);
}
#endif
