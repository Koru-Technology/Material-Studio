
// Multiplies a height blended mask into the combined mask: the mask edge moves with the height of the layer (texpack.a)
// compared to the height of the layers below (texbelow.a) or a flat level

cbuffer constants {
	float opac;
	float influence;
	float sharpness;
	float offset;
	float use_below;
	float reach;
};

sampler sampler_linear;

tex2d tex0;

tex2d texa;

tex2d texpack;

tex2d texbelow;

tex2d texcover;

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

float4 frag(vert_out input) {
	// Averaged over a disk of the reach radius: the edge of a hard brush stroke becomes a ramp the strands reach into
	float mask = 0.0;
	if (constants.reach > 0.0) {
		// Samples off the uv islands are skipped, they would pull the empty texture space in along the seams
		float wsum = 0.0;
		for (int k = 0; k < 24; k += 1) {
			float fk = float(k);
			float rr = constants.reach * sqrt((fk + 0.5) / 24.0);
			float aa = fk * 2.39996;
			float2 tco = input.tex + float2(cos(aa), sin(aa)) * rr;
			float w = step(0.5, sample_lod(texcover, sampler_linear, tco, 0.0).r);
			mask += sample_lod(tex0, sampler_linear, tco, 0.0).r * w;
			wsum += w;
		}
		if (wsum > 0.0) {
			mask = mask / wsum;
		}
		else {
			mask = sample_lod(tex0, sampler_linear, input.tex, 0.0).r;
		}
	}
	else {
		mask = sample_lod(tex0, sampler_linear, input.tex, 0.0).r;
	}
	float cola = sample_lod(texa, sampler_linear, input.tex, 0.0).r;
	float height_top = sample_lod(texpack, sampler_linear, input.tex, 0.0).a;
	float height_below = 0.5;
	if (constants.use_below > 0.5) {
		height_below = sample_lod(texbelow, sampler_linear, input.tex, 0.0).a;
	}
	mask = lerp(1.0, mask, constants.opac);
	// Towards the outer end of the band a strand needs more height, towards the inner end less: strands stay crisp
	float band = (1.0 - smoothstep(0.0, 0.25, mask)) * -3.0 + smoothstep(0.75, 1.0, mask) * 3.0;
	mask = clamp((mask * 2.0 - 1.0 + (height_top - height_below + constants.offset) * constants.influence + band) * constants.sharpness * 0.5 + 0.5, 0.0, 1.0);
	float out_color = cola * mask;
	return float4(out_color, out_color, out_color, 1.0);
}
