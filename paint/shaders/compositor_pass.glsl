#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) float vignette_strength;
	layout(offset = 4) float grain_strength;
	layout(offset = 8) float tonemap_strength; // View transform: 0 none, 1 filmic, 2 aces, 3 agx, 4 standard
	layout(offset = 12) float lut_size;
	layout(offset = 16) float contrast_strength;
	layout(offset = 20) float gamma_strength;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;
layout(set = 0, binding = 3) uniform texture2D lut_tex;

// tex2d histogram;

vec3 tonemap_filmic(vec3 color) {
	// Based on Filmic Tonemapping Operators http://filmicgames.com/archives/75
	// vec3 x = max(vec3(0.0, 0.0, 0.0), color - 0.004);
	vec3 x;
	x.x = max(0.0, color.x - 0.004);
	x.y = max(0.0, color.y - 0.004);
	x.z = max(0.0, color.z - 0.004);
	return (x * (x * 6.2 + 0.5)) / (x * (x * 6.2 + 1.7) + 0.06);
}

// View transforms, scene linear in, display out
vec3 tonemap_aces(vec3 x) {
	// Fitted ACES filmic curve (Narkowicz)
	vec3 c = clamp((x * (x * 2.51 + vec3(0.03, 0.03, 0.03))) / (x * (x * 2.43 + vec3(0.59, 0.59, 0.59)) + vec3(0.14, 0.14, 0.14)), vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));
	return pow(c, vec3(0.4545, 0.4545, 0.4545));
}

vec3 tonemap_agx(vec3 col) {
	// AgX base look (minimal fit by bwrensch)
	vec3 v;
	v.x = dot(col, vec3(0.842479062253094, 0.0784335999999992, 0.0792237451477643));
	v.y = dot(col, vec3(0.0423282422610123, 0.878468636469772, 0.0791661274605434));
	v.z = dot(col, vec3(0.0423756549057051, 0.0784336, 0.879142973793104));
	float max_ev = 4.026069;
	float min_ev = max_ev - 16.5; // -12.47393, written without a unary minus
	v = clamp(log(max(v, vec3(0.00001, 0.00001, 0.00001))) * 1.442695, vec3(min_ev, min_ev, min_ev), vec3(max_ev, max_ev, max_ev)); // log2
	v = (v - vec3(min_ev, min_ev, min_ev)) / (max_ev - min_ev);
	vec3 v2 = v * v;
	vec3 v4 = v2 * v2;
	v = v4 * v2 * 15.5 + v4 * 31.96 + v2 * 0.4298 + v * 0.1191 - v4 * v * 40.14 - v2 * v * 6.868 - vec3(0.00232, 0.00232, 0.00232);
	vec3 o;
	o.x = v.x * 1.19687900512017 - v.y * 0.0980208811401368 - v.z * 0.0990297440797205;
	o.y = v.y * 1.15190312990417 - v.x * 0.0528968517574562 - v.z * 0.0989611768448433;
	o.z = v.z * 1.15107367264116 - v.x * 0.0529716355144438 - v.y * 0.0980434501171241;
	return clamp(o, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));
}

vec3 tonemap_standard(vec3 x) {
	return pow(clamp(x, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)), vec3(0.4545, 0.4545, 0.4545));
}

// 0 none, 1 filmic, 2 aces, 3 agx, 4 standard
vec3 view_transform(vec3 color, float mode) {
	if (mode < 0.5) {
		return color;
	}
	if (mode < 1.5) {
		return tonemap_filmic(color);
	}
	if (mode < 2.5) {
		return tonemap_aces(color);
	}
	if (mode < 3.5) {
		return tonemap_agx(color);
	}
	return tonemap_standard(color);
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
	vec4 color = textureLod(sampler2D(tex, sampler_linear), vin.tex, 0.0);

	// Static grain
	float x = (vin.tex.x + 4.0) * (vin.tex.y + 4.0) * 10.0;
	float g = mod((mod(x, 13.0) + 1.0) * (mod(x, 123.0) + 1.0), 0.01) - 0.005;
	color.rgb = color.rgb + (vec3(g, g, g) * constants.grain_strength);

	// Vignette
	color.rgb = color.rgb * ((1.0 - constants.vignette_strength) + constants.vignette_strength * pow(16.0 * vin.tex.x * vin.tex.y * (1.0 - vin.tex.x) * (1.0 - vin.tex.y), 0.2));

	// Auto exposure
	// const float auto_exposure_strength = 1.0;
	// float expo = 2.0 - clamp(length(textureLod(sampler2D(histogram, sampler_linear), vec2(0.5, 0.5), 0.0).rgb), 0.0, 1.0);
	// color.rgb *= pow(expo, auto_exposure_strength * 2.0);

	if (constants.lut_size > 0.0) {
		// .cube lut color grading, a look on top of the view transform so bright values are not clipped first
		if (constants.tonemap_strength > 0.5) {
			color.rgb = view_transform(color.rgb, constants.tonemap_strength);
		}
		else {
			color.rgb = pow(max(color.rgb, vec3(0.0, 0.0, 0.0)), vec3(0.4545, 0.4545, 0.4545));
		}
		float r_s = min(max(color.x, 0.0), 1.0) * (constants.lut_size - 1.0);
		float g_s = min(max(color.y, 0.0), 1.0) * (constants.lut_size - 1.0);
		float b_s = min(max(color.z, 0.0), 1.0) * (constants.lut_size - 1.0);
		float b0 = floor(b_s);
		float b1 = min(b0 + 1.0, constants.lut_size - 1.0);
		float b_frac = b_s - b0;
		float strip_w = constants.lut_size * constants.lut_size;
		float u0 = (b0 * constants.lut_size + r_s + 0.5) / strip_w;
		float u1 = (b1 * constants.lut_size + r_s + 0.5) / strip_w;
		float v = (g_s + 0.5) / constants.lut_size;
		vec4 s0 = textureLod(sampler2D(lut_tex, sampler_linear), vec2(u0, v), 0.0);
		vec4 s1 = textureLod(sampler2D(lut_tex, sampler_linear), vec2(u1, v), 0.0);
		color.rgb = mix(s0.rgb, s1.rgb, vec3(b_frac, b_frac, b_frac));
	}
	else {
		// View transform with gamma, tonemap_strength holds the transform
		color.rgb = view_transform(color.rgb, constants.tonemap_strength);
	}

	// Contrast
	color.rgb = (color.rgb - vec3(0.5, 0.5, 0.5)) * constants.contrast_strength + vec3(0.5, 0.5, 0.5);

	// Gamma
	float inv_gamma = 1.0 / max(constants.gamma_strength, 0.01);
	color.rgb = pow(max(color.rgb, vec3(0.0, 0.0, 0.0)), vec3(inv_gamma, inv_gamma, inv_gamma));

	frag_color = color;
}
#endif
