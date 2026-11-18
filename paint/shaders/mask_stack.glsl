#version 450

// One effect of a mask stack (util_mask_stack.c): reads the result below (texa), writes it with the effect applied
layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) int type;      // mask_effect_type_t, -1 downsamples texa for a blur
	layout(offset = 4) int blending;  // mask_blend_t, sources only
	layout(offset = 8) float opac;
	layout(offset = 16) vec4 params0; // params[0..3]
	layout(offset = 32) vec4 params1; // params[4..7]
	layout(offset = 48) vec4 params2; // params[8..11]
	layout(offset = 64) vec4 params3; // params[12..15]
	layout(offset = 80) vec2 src_size; // ID map size for ID Selection, texa size for the downsample
	layout(offset = 96) vec4 uv_rect;  // ID Selection: the part of the mask drawn (u0, v0, du, dv), 0 0 1 1 the whole (export tiles)
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D texa;   // Accumulated
layout(set = 0, binding = 3) uniform texture2D texsrc; // Paint effect texpaint, a generator's or fill's cached output, the ID map,
                                                       // the blurred result, the anchored layer; Height Blend: the result
                                                       // below widened by its reach
layout(set = 0, binding = 4) uniform texture2D textop; // Height Blend: the height (a) of the mask's layer
layout(set = 0, binding = 5) uniform texture2D texcmp; // and of what it is compared with

layout(location = 0) VARYING Varyings {
	vec2 tex;
} VARYING_NAME;

#ifdef VERT
layout(location = 0) in vec2 in_pos;

void main() {
	vout.tex   = in_pos.xy * 0.5 + 0.5;
	vout.tex.y = 1.0 - vout.tex.y;
	gl_Position = vec4(in_pos.xy, 0.0, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) out vec4 frag_color;

float blend(float a, float v, int mode) {
	if (mode == 1) { // Multiply
		return a * v;
	}
	if (mode == 2) { // Add
		return a + v;
	}
	if (mode == 3) { // Subtract
		return a - v;
	}
	if (mode == 4) { // Max
		return max(a, v);
	}
	if (mode == 5) { // Min
		return min(a, v);
	}
	if (mode == 6) { // Overlay
		return a < 0.5 ? 2.0 * a * v : 1.0 - 2.0 * (1.0 - a) * (1.0 - v);
	}
	return v; // Normal
}

// Picked part i (0..14) of an ID Selection, 0xrrggbb as a float
float id_part(int i) {
	vec4 p = i < 3 ? constants.params0 : i < 7 ? constants.params1 : i < 11 ? constants.params2 : constants.params3;
	int  k = (i + 1) % 4;
	return k == 0 ? p.x : k == 1 ? p.y : k == 2 ? p.z : p.w;
}

// The ID map texel matches a picked part within ui_id_mask.c's tolerance
float id_selected(vec3 c) {
	int n = int(constants.params0.x + 0.5);
	for (int i = 0; i < 15; i += 1) {
		if (i >= n) {
			break;
		}
		float f = floor(id_part(i) + 0.5);
		float r = floor(f / 65536.0);
		float g = floor((f - r * 65536.0) / 256.0);
		float b = f - r * 65536.0 - g * 256.0;
		vec3  d = abs(vec3(r, g, b) / 255.0 - c);
		if (d.x < 0.02 && d.y < 0.02 && d.z < 0.02) {
			return 1.0;
		}
	}
	return 0.0;
}

void main() {
	if (constants.type == -1) { // Downsample by params0.x, 2x2 texels a linear tap
		int   s   = int(constants.params0.x + 0.5);
		vec2  inv = vec2(1.0, 1.0) / constants.src_size;
		vec2  o   = floor(vin.tex * constants.src_size / float(s)) * float(s);
		float sum = 0.0;
		int   h   = s / 2;
		if (h == 0) {
			sum = textureLod(sampler2D(texa, sampler_linear), (o + 0.5) * inv, 0.0).r;
		}
		else {
			for (int j = 0; j < 16; j += 1) {
				if (j >= h) {
					break;
				}
				for (int i = 0; i < 16; i += 1) {
					if (i >= h) {
						break;
					}
					sum += textureLod(sampler2D(texa, sampler_linear), (o + vec2(float(i), float(j)) * 2.0 + 1.0) * inv, 0.0).r;
				}
			}
			sum /= float(h * h);
		}
		frag_color = vec4(sum, sum, sum, 1.0);
		return;
	}

	float a = textureLod(sampler2D(texa, sampler_linear), vin.tex, 0.0).r;
	float r = a;

	if (constants.type == 1 && constants.params0.y < 0.5) { // Fill of a value, an image or grunge reads its output below
		r = blend(a, constants.params0.x, constants.blending);
	}
	else if (constants.type == 2) { // Levels
		float x = clamp((a - constants.params0.x) / max(constants.params0.y - constants.params0.x, 0.00001), 0.0, 1.0);
		x = pow(x, 1.0 / max(constants.params0.z, 0.00001));
		r = mix(constants.params0.w, constants.params1.x, x);
	}
	else if (constants.type == 3) { // Invert
		r = 1.0 - a;
	}
	else if (constants.type == 8) { // ID Selection, texsrc the ID map (black without one)
		vec2  uv    = constants.uv_rect.xy + vin.tex * constants.uv_rect.zw;
		vec2  texel = min(uv * constants.src_size, constants.src_size - vec2(1.0, 1.0));
		vec3  c     = texelFetch(sampler2D(texsrc, sampler_linear), ivec2(int(texel.x), int(texel.y)), 0).rgb;
		float v     = constants.src_size.x > 1.0 ? id_selected(c) : 0.0;
		r           = blend(a, v, constants.blending);
	}
	else if (constants.type == 12) { // Histogram Scan: a ramp of width 1 - contrast centred on 1 - position
		float w = max(1.0 - constants.params0.y, 0.0001);
		float c = 1.0 - constants.params0.x;
		r       = clamp((a - (c - w * 0.5)) / w, 0.0, 1.0);
	}
	else if (constants.type == 17) { // Height Blend: influence, sharpness, offset, reach, compare with
		float m     = textureLod(sampler2D(texsrc, sampler_linear), vin.tex, 0.0).r;
		float top   = textureLod(sampler2D(textop, sampler_linear), vin.tex, 0.0).a;
		float other = constants.params1.x < 0.5 ? 0.5 : textureLod(sampler2D(texcmp, sampler_linear), vin.tex, 0.0).a;
		// Only inside the edge band: deep in the painted area stays clean, deep in the unpainted area keeps everything.
		// Towards the outer end of the band a strand needs more height, towards the inner end less: strands stay crisp
		float band = (1.0 - smoothstep(0.0, 0.25, m)) * -3.0 + smoothstep(0.75, 1.0, m) * 3.0;
		r          = (m * 2.0 - 1.0 + (top - other + constants.params0.z) * constants.params0.x + band) * constants.params0.y * 0.5 + 0.5;
	}
	else if (constants.type == 18) { // Edge Breakup: width, amount, softness. texsrc the result below blurred across its edge
		// (1 deep inside, falling across the edge), textop noise. Kept where the blur is above the noise times the amount:
		// amount 0 the result below as it is, more frays it in from the edge; deep inside stays, nothing grows.
		float c = textureLod(sampler2D(texsrc, sampler_linear), vin.tex, 0.0).r;
		float n = textureLod(sampler2D(textop, sampler_linear), vin.tex, 0.0).r;
		float s = max(constants.params0.z * 0.1, 0.005);
		float t = n * constants.params0.y * 0.85; // At most 0.95 with the softness: the full inside never frays
		r       = a * smoothstep(t - s, t + s, c);
	}
	else if (constants.type == 13) { // Blur, texsrc the blurred result
		r = textureLod(sampler2D(texsrc, sampler_linear), vin.tex, 0.0).r;
	}
	else if (constants.type == 14) { // Anchor: mask (r), height or opacity (a), or occluded (1 - r) of another layer
		vec4  s    = textureLod(sampler2D(texsrc, sampler_linear), vin.tex, 0.0);
		int   read = int(constants.params0.y + 0.5);
		float v    = read == 0 ? s.r : read == 1 ? s.a : 1.0 - s.r;
		r       = blend(a, v, constants.blending);
	}
	else { // Paint, generators, fills of an image or grunge
		float v = textureLod(sampler2D(texsrc, sampler_linear), vin.tex, 0.0).r;
		r = blend(a, v, constants.blending);
	}

	float o = clamp(mix(a, clamp(r, 0.0, 1.0), constants.opac), 0.0, 1.0);
	frag_color = vec4(o, o, o, 1.0);
}
#endif
