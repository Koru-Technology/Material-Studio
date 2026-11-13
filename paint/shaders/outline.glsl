#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec4 color;
	layout(offset = 16) vec2 tex_step;
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
	vec2 s = constants.tex_step;
	vec2 d = s * 0.7071;
	vec2 lo = s * 0.5;
	vec2 hi = vec2(1.0, 1.0) - lo;
	float inside = textureLod(sampler2D(tex, sampler_linear), clamp(vin.tex, lo, hi), 0.0).r;
	float around = textureLod(sampler2D(tex, sampler_linear), clamp(vin.tex + vec2(s.x, 0.0), lo, hi), 0.0).r;
	around += textureLod(sampler2D(tex, sampler_linear), clamp(vin.tex + vec2(-s.x, 0.0), lo, hi), 0.0).r;
	around += textureLod(sampler2D(tex, sampler_linear), clamp(vin.tex + vec2(0.0, s.y), lo, hi), 0.0).r;
	around += textureLod(sampler2D(tex, sampler_linear), clamp(vin.tex + vec2(0.0, -s.y), lo, hi), 0.0).r;
	around += textureLod(sampler2D(tex, sampler_linear), clamp(vin.tex + vec2(d.x, d.y), lo, hi), 0.0).r;
	around += textureLod(sampler2D(tex, sampler_linear), clamp(vin.tex + vec2(-d.x, d.y), lo, hi), 0.0).r;
	around += textureLod(sampler2D(tex, sampler_linear), clamp(vin.tex + vec2(d.x, -d.y), lo, hi), 0.0).r;
	around += textureLod(sampler2D(tex, sampler_linear), clamp(vin.tex + vec2(-d.x, -d.y), lo, hi), 0.0).r;
	float alpha = (1.0 - inside) * clamp(around * 0.5, 0.0, 1.0);
	frag_color = vec4(constants.color.rgb, constants.color.a * alpha);
}
#endif
