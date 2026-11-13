#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) float dilate_radius;
	layout(offset = 8) vec2 tex_size;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;
layout(set = 0, binding = 3) uniform texture2D texdilate;

// const vec2 offsets[8] = {
// 	vec2(-1.0,  0.0), vec2( 1.0,  0.0), vec2( 0.0,  1.0), vec2( 0.0, -1.0),
// 	vec2(-1.0,  1.0), vec2( 1.0,  1.0), vec2( 1.0, -1.0), vec2(-1.0, -1.0)
// };

vec2 get_offset(int i) {
	if (i == 0) {
		return vec2(-1.0,  0.0);
	}
	if (i == 1) {
		return vec2( 1.0,  0.0);
	}
	if (i == 2) {
		return vec2( 0.0,  1.0);
	}
	if (i == 3) {
		return vec2( 0.0, -1.0);
	}
	if (i == 4) {
		return vec2(-1.0,  1.0);
	}
	if (i == 5) {
		return vec2( 1.0,  1.0);
	}
	if (i == 6) {
		return vec2( 1.0, -1.0);
	}
	return vec2(-1.0, -1.0);
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
	// Based on https://shaderbits.com/blog/uv-dilation by Ryan Brucks
	vec2 texel_size = vec2(1.0, 1.0) / constants.tex_size;
	float min_dist = 10000000.0;
	//uvec2 coord = uvec2(vin.tex * constants.tex_size);
	vec2 coordf = vec2(vin.tex.x * constants.tex_size.x, vin.tex.y * constants.tex_size.y);
	uvec2 coord = uvec2(uint(coordf.x), uint(coordf.y));
	//float mask = texelFetch(sampler2D(texdilate, sampler_linear), ivec2(coord), 0).r;
	vec4 mask4 = texelFetch(sampler2D(texdilate, sampler_linear), ivec2(coord), 0);
	float mask = mask4.r;
	if (mask > 0.0) {
		discard;
	}

	vec4 color = texelFetch(sampler2D(tex, sampler_linear), ivec2(coord), 0);

	int i = 0;
	while (i < int(constants.dilate_radius)) {
		i += 1;
		int j = 0;
		while (j < 8) {
			vec2 cur_uv = vin.tex + get_offset(j) * texel_size * vec2(float(i), float(i));
			coordf = cur_uv * constants.tex_size;
			coord = uvec2(uint(coordf.x), uint(coordf.y));
			vec4 offset_mask4 = texelFetch(sampler2D(texdilate, sampler_linear), ivec2(coord), 0);
			float offset_mask = offset_mask4.r;
			vec4 offset_col = texelFetch(sampler2D(tex, sampler_linear), ivec2(coord), 0);

			if (offset_mask != 0.0) {
				float cur_dist = length(vin.tex - cur_uv);
				if (cur_dist < min_dist) {
					vec2 project_uv = cur_uv + get_offset(j) * texel_size * vec2(float(i), float(i)) * vec2(0.25, 0.25);
					vec4 direction = textureLod(sampler2D(tex, sampler_linear), project_uv, 0.0);
					min_dist = cur_dist;
					if (direction.x != 0.0 || direction.y != 0.0 || direction.z != 0.0) {
						vec4 delta = offset_col - direction;
						color = offset_col + delta * vec4(4.0, 4.0, 4.0, 4.0);
					}
					else {
						color = offset_col;
					}
				}
			}
			j += 1;
		}
	}

	frag_color = color;
}
#endif
