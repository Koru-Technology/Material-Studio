cbuffer constants {
	float2 tile_size;
};

sampler sampler_linear;

tex2d tex0;

tex2d tex1;

tex2d tex2;

tex2d tex3;

tex2d tex4;

tex2d tex5;

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

// One texel per block of tile_size.x texels (tile_size.y blocks a side), 1 when the layer (tex3-5) differs from its snapshot
// (tex0-2) anywhere in the block. Small blocks keep each thread short, one long loop per tile hangs the gpu on ios.
float4 frag(vert_out input) {
	int t = int(constants.tile_size.x);
	int bx = int(floor(input.tex.x * constants.tile_size.y)) * t;
	int by = int(floor(input.tex.y * constants.tile_size.y)) * t;
	float d = 0.0;
	for (int y = 0; y < t; y += 1) {
		for (int x = 0; x < t; x += 1) {
			uint2 p = uint2(uint(bx + x), uint(by + y));
			float4 e = abs(tex0[p] - tex3[p]) + abs(tex1[p] - tex4[p]) + abs(tex2[p] - tex5[p]);
			d = d + e.x + e.y + e.z + e.w;
		}
	}
	if (d > 0.0) {
		return float4(1.0, 1.0, 1.0, 1.0);
	}
	return float4(0.0, 0.0, 0.0, 1.0);
}
