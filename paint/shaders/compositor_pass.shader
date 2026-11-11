
cbuffer constants {
	float vignette_strength;
	float grain_strength;
	float tonemap_strength; // View transform: 0 none, 1 filmic, 2 aces, 3 agx, 4 standard
	float lut_size;
	float contrast_strength;
	float gamma_strength;
};

sampler sampler_linear;

tex2d tex;

tex2d lut_tex;

// tex2d histogram;

struct vert_in {
	float2 pos;
};

struct vert_out {
	float4 pos;
	float2 tex;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.tex = input.pos.xy * 0.5 + 0.5;
	output.tex.y = 1.0 - output.tex.y;
	output.pos = float4(input.pos.xy, 0.0, 1.0);
	return output;
}

float3 tonemap_filmic(float3 color) {
	// Based on Filmic Tonemapping Operators http://filmicgames.com/archives/75
	// float3 x = max(float3(0.0, 0.0, 0.0), color - 0.004);
	float3 x;
	x.x = max(0.0, color.x - 0.004);
	x.y = max(0.0, color.y - 0.004);
	x.z = max(0.0, color.z - 0.004);
	return (x * (x * 6.2 + 0.5)) / (x * (x * 6.2 + 1.7) + 0.06);
}

// View transforms, scene linear in, display out
float3 tonemap_aces(float3 x) {
	// Fitted ACES filmic curve (Narkowicz)
	float3 c = clamp((x * (x * 2.51 + float3(0.03, 0.03, 0.03))) / (x * (x * 2.43 + float3(0.59, 0.59, 0.59)) + float3(0.14, 0.14, 0.14)), float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
	return pow(c, float3(0.4545, 0.4545, 0.4545));
}

float3 tonemap_agx(float3 col) {
	// AgX base look (minimal fit by bwrensch)
	float3 v;
	v.x = dot(col, float3(0.842479062253094, 0.0784335999999992, 0.0792237451477643));
	v.y = dot(col, float3(0.0423282422610123, 0.878468636469772, 0.0791661274605434));
	v.z = dot(col, float3(0.0423756549057051, 0.0784336, 0.879142973793104));
	float max_ev = 4.026069;
	float min_ev = max_ev - 16.5; // -12.47393, written without a unary minus
	v = clamp(log(max(v, float3(0.00001, 0.00001, 0.00001))) * 1.442695, float3(min_ev, min_ev, min_ev), float3(max_ev, max_ev, max_ev)); // log2
	v = (v - float3(min_ev, min_ev, min_ev)) / (max_ev - min_ev);
	float3 v2 = v * v;
	float3 v4 = v2 * v2;
	v = v4 * v2 * 15.5 + v4 * 31.96 + v2 * 0.4298 + v * 0.1191 - v4 * v * 40.14 - v2 * v * 6.868 - float3(0.00232, 0.00232, 0.00232);
	float3 o;
	o.x = v.x * 1.19687900512017 - v.y * 0.0980208811401368 - v.z * 0.0990297440797205;
	o.y = v.y * 1.15190312990417 - v.x * 0.0528968517574562 - v.z * 0.0989611768448433;
	o.z = v.z * 1.15107367264116 - v.x * 0.0529716355144438 - v.y * 0.0980434501171241;
	return clamp(o, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
}

float3 tonemap_standard(float3 x) {
	return pow(clamp(x, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0)), float3(0.4545, 0.4545, 0.4545));
}

// 0 none, 1 filmic, 2 aces, 3 agx, 4 standard
float3 view_transform(float3 color, float mode) {
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

float4 frag(vert_out input) {
	float4 color = sample_lod(tex, sampler_linear, input.tex, 0.0);

	// Static grain
	float x = (input.tex.x + 4.0) * (input.tex.y + 4.0) * 10.0;
	float g = (((x % 13.0) + 1.0) * ((x % 123.0) + 1.0) % 0.01) - 0.005;
	color.rgb = color.rgb + (float3(g, g, g) * constants.grain_strength);

	// Vignette
	color.rgb = color.rgb * ((1.0 - constants.vignette_strength) + constants.vignette_strength * pow(16.0 * input.tex.x * input.tex.y * (1.0 - input.tex.x) * (1.0 - input.tex.y), 0.2));

	// Auto exposure
	// const float auto_exposure_strength = 1.0;
	// float expo = 2.0 - clamp(length(sample_lod(histogram, sampler_linear, float2(0.5, 0.5), 0.0).rgb), 0.0, 1.0);
	// color.rgb *= pow(expo, auto_exposure_strength * 2.0);

	if (constants.lut_size > 0.0) {
		// .cube lut color grading, a look on top of the view transform so bright values are not clipped first
		if (constants.tonemap_strength > 0.5) {
			color.rgb = view_transform(color.rgb, constants.tonemap_strength);
		}
		else {
			color.rgb = pow(max(color.rgb, float3(0.0, 0.0, 0.0)), float3(0.4545, 0.4545, 0.4545));
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
		float4 s0 = sample_lod(lut_tex, sampler_linear, float2(u0, v), 0.0);
		float4 s1 = sample_lod(lut_tex, sampler_linear, float2(u1, v), 0.0);
		color.rgb = lerp(s0.rgb, s1.rgb, float3(b_frac, b_frac, b_frac));
	}
	else {
		// View transform with gamma, tonemap_strength holds the transform
		color.rgb = view_transform(color.rgb, constants.tonemap_strength);
	}

	// Contrast
	color.rgb = (color.rgb - float3(0.5, 0.5, 0.5)) * constants.contrast_strength + float3(0.5, 0.5, 0.5);

	// Gamma
	float inv_gamma = 1.0 / max(constants.gamma_strength, 0.01);
	color.rgb = pow(max(color.rgb, float3(0.0, 0.0, 0.0)), float3(inv_gamma, inv_gamma, inv_gamma));

	return color;
}
