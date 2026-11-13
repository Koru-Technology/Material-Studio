#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec2 screen_size_inv;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;

#ifdef VERT
layout(location = 0) in vec2 in_pos;
layout(location = 0) out Varyings {
	vec2 tex;
} vout;

void main() {	vout.tex = in_pos.xy * 0.5 + 0.5;
	vout.tex.y = 1.0 - vout.tex.y;
	gl_Position = vec4(in_pos.xy, 0.0, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 tex;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	// 4X resolve
	vec2 tex_step = constants.screen_size_inv / 4.0;
	vec4 col = texture(sampler2D(tex, sampler_linear), vin.tex);
	col += texture(sampler2D(tex, sampler_linear), vin.tex + vec2(1.5, 0.0) * tex_step);
	col += texture(sampler2D(tex, sampler_linear), vin.tex + vec2(-1.5, 0.0) * tex_step);
	col += texture(sampler2D(tex, sampler_linear), vin.tex + vec2(0.0, 1.5) * tex_step);
	col += texture(sampler2D(tex, sampler_linear), vin.tex + vec2(0.0, -1.5) * tex_step);
	col += texture(sampler2D(tex, sampler_linear), vin.tex + vec2(1.5, 1.5) * tex_step);
	col += texture(sampler2D(tex, sampler_linear), vin.tex + vec2(-1.5, -1.5) * tex_step);
	col += texture(sampler2D(tex, sampler_linear), vin.tex + vec2(1.5, -1.5) * tex_step);
	col += texture(sampler2D(tex, sampler_linear), vin.tex + vec2(-1.5, 1.5) * tex_step);
	frag_color = col / 9.0;
}
#endif
