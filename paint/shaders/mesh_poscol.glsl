#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 WVP;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;

#ifdef VERT
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec4 in_col;
layout(location = 0) out Varyings {
	vec3 vcolor;
} vout;

void main() {	vec4 spos = vec4(in_pos.xyz, 1.0);
	vout.vcolor = in_col.rgb;
	gl_Position = constants.WVP * spos;
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec3 vcolor;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	vec4 color;
	color = vec4(vin.vcolor, 1.0);
	// color.rgb = pow(color.rgb, vec3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
	color.r = pow(color.r, 1.0 / 2.2);
	color.g = pow(color.g, 1.0 / 2.2);
	color.b = pow(color.b, 1.0 / 2.2);
	frag_color = color;
}
#endif
