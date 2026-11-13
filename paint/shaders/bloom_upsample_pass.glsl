#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec2 screen_size_inv;
	layout(offset = 8) int current_mip_level;
	layout(offset = 12) float sample_scale;
	layout(offset = 16) float bloom_strength;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;

vec2 clamp_coord(vec2 coord, vec2 texel_size) {
	vec2 lo = texel_size * 1.5;
	vec2 hi = vec2(1.0, 1.0) - lo;
	return vec2(clamp(coord.x, lo.x, hi.x), clamp(coord.y, lo.y, hi.y));
}

vec3 upsample_dual_filter(vec2 tex_coord, vec2 texel_size) {
	vec2 delta = texel_size * constants.sample_scale;

	vec3 result;
	result  = textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord + vec2(-delta.x * 2.0, 0.0), texel_size), 0.0).rgb;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord + vec2(-delta.x, delta.y),   texel_size), 0.0).rgb * 2.0;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord + vec2(0.0, delta.y * 2.0),  texel_size), 0.0).rgb;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord + delta,                       texel_size), 0.0).rgb * 2.0;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord + vec2(delta.x * 2.0, 0.0),  texel_size), 0.0).rgb;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord + vec2(delta.x, -delta.y),   texel_size), 0.0).rgb * 2.0;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord + vec2(0.0, -delta.y * 2.0), texel_size), 0.0).rgb;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord - delta,                       texel_size), 0.0).rgb * 2.0;

	return result * (1.0 / 12.0);
}

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
	color.rgb = upsample_dual_filter(vin.tex, constants.screen_size_inv);

	if (constants.current_mip_level == 0) {
		color.rgb = color.rgb * vec3(constants.bloom_strength, constants.bloom_strength, constants.bloom_strength);
	}

	color.a = 1.0;
	frag_color = color;
}
#endif
