#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec4 empty;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;

const float auto_exposure_speed = 1.0;

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
	vec4 color;
	color.a = 0.01 * auto_exposure_speed;
	color.rgb = textureLod(sampler2D(tex, sampler_linear), vec2(0.5, 0.5), 0.0).rgb +
				textureLod(sampler2D(tex, sampler_linear), vec2(0.2, 0.2), 0.0).rgb +
				textureLod(sampler2D(tex, sampler_linear), vec2(0.8, 0.2), 0.0).rgb +
				textureLod(sampler2D(tex, sampler_linear), vec2(0.2, 0.8), 0.0).rgb +
				textureLod(sampler2D(tex, sampler_linear), vec2(0.8, 0.8), 0.0).rgb;
	color.rgb = color.rgb / 5.0;
	frag_color = color;
}
#endif
