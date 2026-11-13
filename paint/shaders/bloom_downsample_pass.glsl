#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec2 screen_size_inv;
	layout(offset = 8) int current_mip_level;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;

const float bloom_knee = 0.5;
const float bloom_threshold = 0.8;
const float epsilon = 0.000062;

vec2 clamp_coord(vec2 coord, vec2 texel_size) {
	vec2 lo = texel_size * 0.5;
	vec2 hi = vec2(1.0, 1.0) - lo;
	return vec2(clamp(coord.x, lo.x, hi.x), clamp(coord.y, lo.y, hi.y));
}

vec3 downsample_dual_filter(vec2 tex_coord, vec2 texel_size) {
	vec3 delta = vec3(texel_size.xy, texel_size.x) * vec3(0.5, 0.5, -0.5);

	vec3 result;
	result  = textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord,            texel_size), 0.0).rgb * 4.0;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord - delta.xy, texel_size), 0.0).rgb;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord - delta.zy, texel_size), 0.0).rgb;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord + delta.zy, texel_size), 0.0).rgb;
	result += textureLod(sampler2D(tex, sampler_linear), clamp_coord(tex_coord + delta.xy, texel_size), 0.0).rgb;

	return result * (1.0 / 8.0);
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
	color.rgb = downsample_dual_filter(vin.tex, constants.screen_size_inv);

	if (constants.current_mip_level == 0) {
		float brightness = max(color.r, max(color.g, color.b));

		float softening_curve = brightness - bloom_threshold + bloom_knee;
		softening_curve = clamp(softening_curve, 0.0, 2.0 * bloom_knee);
		softening_curve = softening_curve * softening_curve / (4.0 * bloom_knee + epsilon);

		float contribution_factor = max(softening_curve, brightness - bloom_threshold);

		contribution_factor /= max(epsilon, brightness);

		color.rgb = color.rgb * contribution_factor;
	}

	color.a = 1.0;
	frag_color = color;
}
#endif
