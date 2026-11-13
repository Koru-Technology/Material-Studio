#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) float opac;
	layout(offset = 4) float influence;
	layout(offset = 8) float sharpness;
	layout(offset = 12) float offset;
	layout(offset = 16) float use_below;
	layout(offset = 20) float reach;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex0;
layout(set = 0, binding = 3) uniform texture2D texa;
layout(set = 0, binding = 4) uniform texture2D texpack;
layout(set = 0, binding = 5) uniform texture2D texbelow;
layout(set = 0, binding = 6) uniform texture2D texcover;

// Multiplies a height blended mask into the combined mask: the mask edge moves with the height of the layer (texpack.a)
// compared to the height of the layers below (texbelow.a) or a flat level

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
	// Averaged over a disk of the reach radius: the edge of a hard brush stroke becomes a ramp the strands reach into
	float mask = 0.0;
	if (constants.reach > 0.0) {
		// Samples off the uv islands are skipped, they would pull the empty texture space in along the seams
		float wsum = 0.0;
		for (int k = 0; k < 24; k += 1) {
			float fk = float(k);
			float rr = constants.reach * sqrt((fk + 0.5) / 24.0);
			float aa = fk * 2.39996;
			vec2 tco = vin.tex + vec2(cos(aa), sin(aa)) * rr;
			float w = step(0.5, textureLod(sampler2D(texcover, sampler_linear), tco, 0.0).r);
			mask += textureLod(sampler2D(tex0, sampler_linear), tco, 0.0).r * w;
			wsum += w;
		}
		if (wsum > 0.0) {
			mask = mask / wsum;
		}
		else {
			mask = textureLod(sampler2D(tex0, sampler_linear), vin.tex, 0.0).r;
		}
	}
	else {
		mask = textureLod(sampler2D(tex0, sampler_linear), vin.tex, 0.0).r;
	}
	float cola = textureLod(sampler2D(texa, sampler_linear), vin.tex, 0.0).r;
	float height_top = textureLod(sampler2D(texpack, sampler_linear), vin.tex, 0.0).a;
	float height_below = 0.5;
	if (constants.use_below > 0.5) {
		height_below = textureLod(sampler2D(texbelow, sampler_linear), vin.tex, 0.0).a;
	}
	mask = mix(1.0, mask, constants.opac);
	// Towards the outer end of the band a strand needs more height, towards the inner end less: strands stay crisp
	float band = (1.0 - smoothstep(0.0, 0.25, mask)) * -3.0 + smoothstep(0.75, 1.0, mask) * 3.0;
	mask = clamp((mask * 2.0 - 1.0 + (height_top - height_below + constants.offset) * constants.influence + band) * constants.sharpness * 0.5 + 0.5, 0.0, 1.0);
	float out_color = cola * mask;
	frag_color = vec4(out_color, out_color, out_color, 1.0);
}
#endif
