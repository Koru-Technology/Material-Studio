#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec2 tile_size;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex0;
layout(set = 0, binding = 3) uniform texture2D tex1;
layout(set = 0, binding = 4) uniform texture2D tex2;
layout(set = 0, binding = 5) uniform texture2D tex3;
layout(set = 0, binding = 6) uniform texture2D tex4;
layout(set = 0, binding = 7) uniform texture2D tex5;

// One texel per block of tile_size.x texels (tile_size.y blocks a side), 1 when the layer (tex3-5) differs from its snapshot
// (tex0-2) anywhere in the block. Small blocks keep each thread short, one long loop per tile hangs the gpu on ios.

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
	int t = int(constants.tile_size.x);
	int bx = int(floor(vin.tex.x * constants.tile_size.y)) * t;
	int by = int(floor(vin.tex.y * constants.tile_size.y)) * t;
	float d = 0.0;
	for (int y = 0; y < t; y += 1) {
		for (int x = 0; x < t; x += 1) {
			uvec2 p = uvec2(uint(bx + x), uint(by + y));
			vec4 e = abs(texelFetch(sampler2D(tex0, sampler_linear), ivec2(p), 0) - texelFetch(sampler2D(tex3, sampler_linear), ivec2(p), 0)) + abs(texelFetch(sampler2D(tex1, sampler_linear), ivec2(p), 0) - texelFetch(sampler2D(tex4, sampler_linear), ivec2(p), 0)) + abs(texelFetch(sampler2D(tex2, sampler_linear), ivec2(p), 0) - texelFetch(sampler2D(tex5, sampler_linear), ivec2(p), 0));
			d = d + e.x + e.y + e.z + e.w;
		}
	}
	if (d > 0.0) {
		{ frag_color = vec4(1.0, 1.0, 1.0, 1.0); return; }
	}
	frag_color = vec4(0.0, 0.0, 0.0, 1.0);
}
#endif
