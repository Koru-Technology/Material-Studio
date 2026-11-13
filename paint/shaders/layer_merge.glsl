#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) float opac;
	layout(offset = 4) int blending;
	layout(offset = 8) float tex1w;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex0;
layout(set = 0, binding = 3) uniform texture2D tex1;
layout(set = 0, binding = 4) uniform texture2D texmask;
layout(set = 0, binding = 5) uniform texture2D texa;

vec3 hsv_to_rgb(vec3 c) {
	vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * mix(K.xxx, clamp(p - K.xxx, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)), c.y);
}

vec3 rgb_to_hsv(vec3 c) {
	vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
	vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
	float d = q.x - min(q.w, q.y);
	float e = 0.0000000001;
	return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec4 blend_modes(int blending, vec4 col0, vec4 cola, float maskr, float str) {
	if (blending == 0) { // Mix
		return vec4(
			mix(cola.rgb, col0.rgb, str), max(col0.a * maskr, cola.a));
	}
	if (blending == 1) { // Darken
		return vec4(
			mix(cola.rgb, min(cola.rgb, col0.rgb), str), max(col0.a * maskr, cola.a));
	}
	if (blending == 2) { // Multiply
		return vec4(
			mix(cola.rgb, cola.rgb * col0.rgb, str), max(col0.a * maskr, cola.a));
	}
	if (blending == 3) { // Burn
		return vec4(
			mix(
				cola.rgb,
				vec3(1.0, 1.0, 1.0) - (vec3(1.0, 1.0, 1.0) - cola.rgb) / col0.rgb,
				str),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 4) { // Lighten
		return vec4(
			max(cola.rgb, col0.rgb * str),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 5) { // Screen
		return vec4(
			(vec3(1.0, 1.0, 1.0) - (vec3(1.0 - str, 1.0 - str, 1.0 - str) +
				str * (vec3(1.0, 1.0, 1.0) - col0.rgb)) * (vec3(1.0, 1.0, 1.0) - cola.rgb)),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 6) { // Dodge
		return vec4(
			mix(cola.rgb, cola.rgb / (vec3(1.0, 1.0, 1.0) - col0.rgb), str),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 7) { // Add
		return vec4(
			mix(cola.rgb, cola.rgb + col0.rgb, str),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 8) { // Overlay
		float rr = mix(2.0 * cola.r * col0.r, 1.0 - 2.0 * (1.0 - cola.r) * (1.0 - col0.r), step(0.5, cola.r));
		float gg = mix(2.0 * cola.g * col0.g, 1.0 - 2.0 * (1.0 - cola.g) * (1.0 - col0.g), step(0.5, cola.g));
		float bb = mix(2.0 * cola.b * col0.b, 1.0 - 2.0 * (1.0 - cola.b) * (1.0 - col0.b), step(0.5, cola.b));
		return vec4(
			mix(cola.rgb, vec3(rr, gg, bb), str),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 9) { // Soft Light
		return vec4(
			((1.0 - str) * cola.rgb +
				str * ((vec3(1.0, 1.0, 1.0) - cola.rgb) * col0.rgb * cola.rgb +
				cola.rgb * (vec3(1.0, 1.0, 1.0) - (vec3(1.0, 1.0, 1.0) - col0.rgb) * (vec3(1.0, 1.0, 1.0) - cola.rgb)))),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 10) { // Linear Light
		return vec4(
			(cola.rgb + str * (vec3(2.0, 2.0, 2.0) * (col0.rgb - vec3(0.5, 0.5, 0.5)))),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 11) { // Difference
		return vec4(
			mix(cola.rgb, abs(cola.rgb - col0.rgb), str),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 12) { // Subtract
		return vec4(
			mix(cola.rgb, cola.rgb - col0.rgb, str),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 13) { // Divide
		return vec4(
			vec3(1.0 - str, 1.0 - str, 1.0 - str) * cola.rgb + vec3(str, str, str) * cola.rgb / col0.rgb,
			max(col0.a * maskr, cola.a));
	}
	if (blending == 14) { // Hue
		return vec4(
			mix(
				cola.rgb,
				hsv_to_rgb(
					vec3(rgb_to_hsv(col0.rgb).r,
					rgb_to_hsv(cola.rgb).g,
					rgb_to_hsv(cola.rgb).b)),
				str),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 15) { // Saturation
		return vec4(
			mix(
				cola.rgb,
				hsv_to_rgb(
					vec3(rgb_to_hsv(cola.rgb).r,
					rgb_to_hsv(col0.rgb).g,
					rgb_to_hsv(cola.rgb).b)),
				str),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 16) { // Color
		return vec4(
			mix(
				cola.rgb,
				hsv_to_rgb(
					vec3(rgb_to_hsv(col0.rgb).r,
					rgb_to_hsv(col0.rgb).g,
					rgb_to_hsv(cola.rgb).b)),
				str),
			max(col0.a * maskr, cola.a));
	}
	if (blending == 17) { // Value
		return vec4(
			mix(
				cola.rgb,
				hsv_to_rgb(
					vec3(rgb_to_hsv(cola.rgb).r,
					rgb_to_hsv(cola.rgb).g,
					rgb_to_hsv(col0.rgb).b)),
				str),
			max(col0.a * maskr, cola.a));
	}

	return vec4(1.0, 1.0, 1.0, 1.0);
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
	vec4 col0 = textureLod(sampler2D(tex0, sampler_linear), vin.tex, 0.0);
	vec4 cola = textureLod(sampler2D(texa, sampler_linear), vin.tex, 0.0);
	float maskr = textureLod(sampler2D(texmask, sampler_linear), vin.tex, 0.0).r;
	float str = col0.a * constants.opac;
	str *= maskr;

	if (constants.blending == 105) { // Weight of the upper layer in merged _nor and _pack
		float a = cola.a + str * (1.0 - cola.a);
		if (a <= 0.0) {
			{ frag_color = vec4(0.0, 0.0, 0.0, 0.0); return; }
		}
		{ frag_color = vec4(str, cola.a, a, str / a); return; }
	}
	if (constants.blending == 106) { // Merging layers, _nor with normal blending, tex0 from 105
		if (col0.b <= 0.0) {
			{ frag_color = cola; return; }
		}
		vec4 col1 = textureLod(sampler2D(tex1, sampler_linear), vin.tex, 0.0);
		vec3 flat_nor = vec3(0.5, 0.5, 1.0);
		vec3 n1 = mix(flat_nor, cola.rgb, col0.g) * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);
		vec3 n2 = mix(flat_nor, col1.rgb, col0.r) * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);
		vec3 blended = normalize(vec3(n1.xy + n2.xy, n1.z * n2.z)) * vec3(0.5, 0.5, 0.5) + vec3(0.5, 0.5, 0.5);
		{ frag_color = vec4(flat_nor + (blended - flat_nor) / col0.b, max(col1.a, cola.a)); return; }
	}
	if (constants.blending == 107) { // Merging layers, _pack with height blending, tex0 from 105
		if (col0.b <= 0.0) {
			{ frag_color = cola; return; }
		}
		vec4 col1 = textureLod(sampler2D(tex1, sampler_linear), vin.tex, 0.0);
		{ frag_color = vec4(mix(cola.rgb, col1.rgb, col0.a), (cola.a * col0.g + col1.a * col0.r) / col0.b); return; }
	}
	if (constants.blending >= 300 && constants.blending < 400) { // Merging _pack, the layer blend mode on occlusion, roughness, metallic
		vec4 col1 = textureLod(sampler2D(tex1, sampler_linear), vin.tex, 0.0);
		vec4 b = blend_modes(constants.blending - 300, vec4(col1.rgb, 1.0), cola, 1.0, str);
		{ frag_color = vec4(clamp(b.rgb, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)), mix(cola.a, col1.a, str)); return; }
	}
	if (constants.blending >= 400 && constants.blending < 500) { // Same with height blending
		vec4 col1 = textureLod(sampler2D(tex1, sampler_linear), vin.tex, 0.0);
		vec4 b = blend_modes(constants.blending - 400, vec4(col1.rgb, 1.0), cola, 1.0, str);
		{ frag_color = vec4(clamp(b.rgb, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)), cola.a + col1.a * str); return; }
	}
	if (constants.blending == 101) { // Merging _nor and _pack
		vec4 col1 = textureLod(sampler2D(tex1, sampler_linear), vin.tex, 0.0);
		{ frag_color = mix(cola, col1, str); return; }
	}
	if (constants.blending == 102) { // Merging _nor with normal blending
		vec4 col1 = textureLod(sampler2D(tex1, sampler_linear), vin.tex, 0.0);
		// Whiteout blend
		vec3 n1 = cola.rgb * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);
		vec3 n2 = mix(vec3(0.5, 0.5, 1.0), col1.rgb, str) * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);
		{ frag_color = vec4(
			normalize(vec3(n1.xy + n2.xy, n1.z * n2.z)) * vec3(0.5, 0.5, 0.5) + vec3(0.5, 0.5, 0.5),
			max(col1.a, cola.a)); return; }
	}
	if (constants.blending == 103) { // Merging _pack with height blending
		vec4 col1 = textureLod(sampler2D(tex1, sampler_linear), vin.tex, 0.0);
		{ frag_color = vec4(mix(cola.rgb, col1.rgb, str), cola.a + col1.a * str); return; }
	}
	if (constants.blending == 104) { // Merge _pack.height into _nor
		float tex_step = 1.0 / constants.tex1w;
		float height0 = textureLod(sampler2D(tex1, sampler_linear), vec2(vin.tex.x - tex_step, vin.tex.y), 0.0).a;
		float height1 = textureLod(sampler2D(tex1, sampler_linear), vec2(vin.tex.x + tex_step, vin.tex.y), 0.0).a;
		float height2 = textureLod(sampler2D(tex1, sampler_linear), vec2(vin.tex.x, vin.tex.y - tex_step), 0.0).a;
		float height3 = textureLod(sampler2D(tex1, sampler_linear), vec2(vin.tex.x, vin.tex.y + tex_step), 0.0).a;
		float height_dx = height0 - height1;
		float height_dy = height2 - height3;
		// Whiteout blend
		vec3 n1 = col0.rgb * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);
		vec3 n2 = normalize(vec3(height_dx * 16.0, height_dy * 16.0, 1.0));
		{ frag_color = vec4(
			normalize(vec3(n1.xy + n2.xy, n1.z * n2.z)) * vec3(0.5, 0.5, 0.5) + vec3(0.5, 0.5, 0.5),
			col0.a); return; }
	}
	float a = cola.a + str * (1.0 - cola.a);
	int code = constants.blending;
	bool lin = false;
	if (code >= 1000) { // Linear blending: colors blend in linear space and stay gamma encoded
		lin = true;
		code = code - 1000;
		col0.rgb = pow(max(col0.rgb, vec3(0.0, 0.0, 0.0)), vec3(2.2, 2.2, 2.2));
		cola.rgb = pow(max(cola.rgb, vec3(0.0, 0.0, 0.0)), vec3(2.2, 2.2, 2.2));
	}
	int mode = code;
	if (mode >= 200) { // Merging layers, the result is composited later
		if (a <= 0.0) {
			if (lin) {
				cola.rgb = pow(max(cola.rgb, vec3(0.0, 0.0, 0.0)), vec3(0.4545, 0.4545, 0.4545));
			}
			{ frag_color = cola; return; }
		}
		mode = mode - 200;
	}
	vec4 blended = blend_modes(mode, col0, cola, maskr, str);
	if (code >= 200) {
		blended = vec4((blended.rgb * cola.a + col0.rgb * str * (1.0 - cola.a)) / a, a);
	}
	if (lin) {
		blended.rgb = pow(max(blended.rgb, vec3(0.0, 0.0, 0.0)), vec3(0.4545, 0.4545, 0.4545));
	}
	frag_color = blended;
}
#endif
