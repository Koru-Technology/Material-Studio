cbuffer constants {
	float4x4 inv_vp;  // Current view projection without jitter, inverted
	float4x4 prev_vp; // Previous frame view projection without jitter
	float2 texel;
	float taa_blend;
};

sampler sampler_linear;

tex2d tex;

tex2d tex2;

tex2d gbufferD;

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

// The previous frame is read where the surface was then (from the depth and both camera matrices), not at the same pixel,
// so a turning view does not leave a second image behind. History outside the colors around the pixel is clamped to them,
// surfaces that just came into view take the current frame.
float4 frag(vert_out input) {
	float4 current = sample_lod(tex, sampler_linear, input.tex, 0.0);
	float3 lo = current.rgb;
	float3 hi = current.rgb;
	for (int y = -1; y <= 1; y += 1) {
		for (int x = -1; x <= 1; x += 1) {
			float3 c = sample_lod(tex, sampler_linear, input.tex + float2(float(x), float(y)) * constants.texel, 0.0).rgb;
			lo = min(lo, c);
			hi = max(hi, c);
		}
	}

	float depth = sample_lod(gbufferD, sampler_linear, input.tex, 0.0).r;
	float4 wpos = constants.inv_vp * float4(input.tex.x * 2.0 - 1.0, (1.0 - input.tex.y) * 2.0 - 1.0, depth, 1.0);
	wpos = wpos / wpos.w;
	float4 ppos = constants.prev_vp * float4(wpos.xyz, 1.0);
	float blend = constants.taa_blend;
	if (ppos.w <= 0.0) {
		blend = 0.0;
	}
	float2 puv = float2(ppos.x / ppos.w * 0.5 + 0.5, 1.0 - (ppos.y / ppos.w * 0.5 + 0.5));
	if (puv.x < 0.0 || puv.x > 1.0 || puv.y < 0.0 || puv.y > 1.0) {
		blend = 0.0;
	}
	float3 previous = clamp(sample_lod(tex2, sampler_linear, puv, 0.0).rgb, lo, hi);
	return float4(lerp(current.rgb, previous, blend), 1.0);
}
