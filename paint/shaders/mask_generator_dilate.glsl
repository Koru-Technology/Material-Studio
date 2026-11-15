#version 450

// Mask generator output (mask_generator.glsl) into its cache: covered texels as they are, the others take the nearest
// covered texel within the radius, searching 8 directions (dilate_pass.glsl without the extrapolation)
layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) float radius;
	layout(offset = 8) vec2 tex_size;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex; // R value, A coverage

layout(location = 0) VARYING Varyings {
	vec2 tex;
} VARYING_NAME;

#ifdef VERT
layout(location = 0) in vec2 in_pos;

void main() {
	vout.tex    = in_pos.xy * 0.5 + 0.5;
	vout.tex.y  = 1.0 - vout.tex.y;
	gl_Position = vec4(in_pos.xy, 0.0, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) out vec4 frag_color;

ivec2 get_offset(int i) {
	if (i == 0) {
		return ivec2(-1, 0);
	}
	if (i == 1) {
		return ivec2(1, 0);
	}
	if (i == 2) {
		return ivec2(0, 1);
	}
	if (i == 3) {
		return ivec2(0, -1);
	}
	if (i == 4) {
		return ivec2(-1, 1);
	}
	if (i == 5) {
		return ivec2(1, 1);
	}
	if (i == 6) {
		return ivec2(1, -1);
	}
	return ivec2(-1, -1);
}

void main() {
	ivec2 size  = ivec2(constants.tex_size);
	ivec2 coord = min(ivec2(vin.tex * constants.tex_size), size - ivec2(1, 1));
	vec4  c     = texelFetch(sampler2D(tex, sampler_linear), coord, 0);
	float v     = 0.0;
	if (c.a > 0.5) {
		v = c.r;
	}
	else {
		float min_dist = 1000000.0;
		int   r        = int(constants.radius);
		for (int i = 1; i <= r; ++i) {
			if (float(i) > min_dist) {
				break; // Nothing nearer further out
			}
			for (int j = 0; j < 8; ++j) {
				ivec2 cc = coord + get_offset(j) * i;
				if (cc.x < 0 || cc.y < 0 || cc.x >= size.x || cc.y >= size.y) {
					continue;
				}
				vec4  s    = texelFetch(sampler2D(tex, sampler_linear), cc, 0);
				float dist = j < 4 ? float(i) : float(i) * 1.4142;
				if (s.a > 0.5 && dist < min_dist) {
					min_dist = dist;
					v        = s.r;
				}
			}
		}
	}
	frag_color = vec4(v, v, v, 1.0);
}
#endif
