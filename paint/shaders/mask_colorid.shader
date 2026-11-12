
// Picked ID map parts into a mask: white where the ID map has one of the picked colors, see ui_id_mask.c

cbuffer constants {
	float2 size; // ID map size, it is read texel by texel so part edges are not blended
};

sampler sampler_linear;

tex2d texpaint_colorid; // 16x1 picked colors, alpha 0 for free slots

tex2d texcolorid;

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
	float2 texel = min(input.tex * constants.size, constants.size - float2(1.0, 1.0));
	float3 c = texcolorid[uint2(uint(texel.x), uint(texel.y))].rgb;
	float hit = 0.0;
	for (int i = 0; i < 16; i += 1) {
		float4 s = texpaint_colorid[uint2(uint(i), uint(0))];
		if (s.a > 0.5 && abs(s.r - c.r) < 0.02 && abs(s.g - c.g) < 0.02 && abs(s.b - c.b) < 0.02) {
			hit = 1.0;
		}
	}
	if (hit < 0.5) {
		discard;
	}
	return float4(1.0, 1.0, 1.0, 1.0);
}
