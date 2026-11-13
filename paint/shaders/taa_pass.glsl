#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 inv_vp; // Current view projection without jitter, inverted
	layout(offset = 64) mat4 prev_vp; // Previous frame view projection without jitter
	layout(offset = 128) vec2 texel;
	layout(offset = 136) float taa_blend;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;
layout(set = 0, binding = 3) uniform texture2D tex2;
layout(set = 0, binding = 4) uniform texture2D gbufferD;

// The previous frame is read where the surface was then (from the depth and both camera matrices), not at the same pixel,
// so a turning view does not leave a second image behind. History outside the colors around the pixel is clamped to them,
// surfaces that just came into view take the current frame.

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
	vec4 current = textureLod(sampler2D(tex, sampler_linear), vin.tex, 0.0);
	vec3 lo = current.rgb;
	vec3 hi = current.rgb;
	for (int y = -1; y <= 1; y += 1) {
		for (int x = -1; x <= 1; x += 1) {
			vec3 c = textureLod(sampler2D(tex, sampler_linear), vin.tex + vec2(float(x), float(y)) * constants.texel, 0.0).rgb;
			lo = min(lo, c);
			hi = max(hi, c);
		}
	}

	float depth = textureLod(sampler2D(gbufferD, sampler_linear), vin.tex, 0.0).r;
	vec4 wpos = constants.inv_vp * vec4(vin.tex.x * 2.0 - 1.0, (1.0 - vin.tex.y) * 2.0 - 1.0, depth, 1.0);
	wpos = wpos / wpos.w;
	vec4 ppos = constants.prev_vp * vec4(wpos.xyz, 1.0);
	float blend = constants.taa_blend;
	if (ppos.w <= 0.0) {
		blend = 0.0;
	}
	vec2 puv = vec2(ppos.x / ppos.w * 0.5 + 0.5, 1.0 - (ppos.y / ppos.w * 0.5 + 0.5));
	if (puv.x < 0.0 || puv.x > 1.0 || puv.y < 0.0 || puv.y > 1.0) {
		blend = 0.0;
	}
	vec3 previous = clamp(textureLod(sampler2D(tex2, sampler_linear), puv, 0.0).rgb, lo, hi);
	frag_color = vec4(mix(current.rgb, previous, blend), 1.0);
}
#endif
