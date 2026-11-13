#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec2 tex_size;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;
layout(set = 0, binding = 3) uniform texture2D texnbr0;
layout(set = 0, binding = 4) uniform texture2D texnbr1;

// One diffusion step, texnbr0/1 hold the uvs of the left, right, up and down neighbors,
// across uv seams they point into the neighboring island, negative when there is no neighbor

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
	vec2 coordf = vin.tex * constants.tex_size;
	uvec2 coord = uvec2(uint(coordf.x), uint(coordf.y));
	vec4 n0 = texelFetch(sampler2D(texnbr0, sampler_linear), ivec2(coord), 0);
	vec4 n1 = texelFetch(sampler2D(texnbr1, sampler_linear), ivec2(coord), 0);
	vec4 sum = texelFetch(sampler2D(tex, sampler_linear), ivec2(coord), 0);
	float count = 1.0;
	if (n0.x >= 0.0) {
		sum += textureLod(sampler2D(tex, sampler_linear), n0.xy, 0.0);
		count += 1.0;
	}
	if (n0.z >= 0.0) {
		sum += textureLod(sampler2D(tex, sampler_linear), n0.zw, 0.0);
		count += 1.0;
	}
	if (n1.x >= 0.0) {
		sum += textureLod(sampler2D(tex, sampler_linear), n1.xy, 0.0);
		count += 1.0;
	}
	if (n1.z >= 0.0) {
		sum += textureLod(sampler2D(tex, sampler_linear), n1.zw, 0.0);
		count += 1.0;
	}
	frag_color = sum / vec4(count, count, count, count);
}
#endif
