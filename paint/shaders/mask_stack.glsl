#version 450

// One effect of a mask stack (util_mask_stack.c): reads the result below (texa), writes it with the effect applied
layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) int type;      // mask_effect_type_t
	layout(offset = 4) int blending;  // mask_blend_t, sources only
	layout(offset = 8) float opac;
	layout(offset = 16) vec4 params0; // params[0..3]
	layout(offset = 32) vec4 params1; // params[4..7]
	layout(offset = 48) vec4 params2; // params[8..11]
	layout(offset = 64) vec4 params3; // params[12..15]
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D texa;   // Accumulated
layout(set = 0, binding = 3) uniform texture2D texsrc; // Paint effect texpaint, a generator's cached output

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

void main() {
	float a = textureLod(sampler2D(texa, sampler_linear), vin.tex, 0.0).r;
	float r = a;

	if (constants.type == 0 || constants.type >= 4) { // Paint, generators
		float v = textureLod(sampler2D(texsrc, sampler_linear), vin.tex, 0.0).r;
		r = blend(a, v, constants.blending);
	}
	else if (constants.type == 1) { // Fill
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

	float o = clamp(mix(a, clamp(r, 0.0, 1.0), constants.opac), 0.0, 1.0);
	frag_color = vec4(o, o, o, 1.0);
}
#endif
