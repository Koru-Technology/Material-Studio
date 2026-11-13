#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec2 dir_inv;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D tex;
layout(set = 0, binding = 3) uniform texture2D gbuffer0;

const float num_taps      = 4.0;
const float blur_sigma    = 0.125;
const float nor_threshold = 0.8;
const float nor_scale     = 5.0;

vec2 octahedron_wrap(vec2 v) {
	vec2 a;
	if (v.x >= 0.0) {
		a.x = 1.0;
	}
	else {
		a.x = -1.0;
	}

	if (v.y >= 0.0) {
		a.y = 1.0;
	}
	else {
		a.y = -1.0;
	}

	vec2 r;
	r.x = abs(v.y);
	r.y = abs(v.x);
	r.x = 1.0 - r.x;
	r.y = 1.0 - r.y;
	return r * a;
}

vec3 get_nor(vec2 enc) {
	vec3 n;
	n.z = 1.0 - abs(enc.x) - abs(enc.y);
	if (n.z >= 0.0) {
		n.xy = enc.xy;
	}
	else {
		n.xy = octahedron_wrap(enc.xy);
	}
	n = normalize(n);
	return n;
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
layout(location = 0) out float frag_color;

void main() {

	vec3 nor = get_nor(textureLod(sampler2D(gbuffer0, sampler_linear), vin.tex, 0.0).rg);
	float weight = 1.0;
	float color = textureLod(sampler2D(tex, sampler_linear), vin.tex, 0.0).r;

	for (int i = 1; i <= int(num_taps); i += 1) {
		float fi = float(i);
		float gw = exp(0.0 - fi * fi * blur_sigma);
		vec2 off = fi * constants.dir_inv;

		vec3 nor2 = get_nor(textureLod(sampler2D(gbuffer0, sampler_linear), vin.tex + off, 0.0).rg);
		float w = gw * clamp((dot(nor2, nor) - nor_threshold) * nor_scale, 0.0, 1.0);
		color += textureLod(sampler2D(tex, sampler_linear), vin.tex + off, 0.0).r * w;
		weight += w;

		nor2 = get_nor(textureLod(sampler2D(gbuffer0, sampler_linear), vin.tex - off, 0.0).rg);
		w = gw * clamp((dot(nor2, nor) - nor_threshold) * nor_scale, 0.0, 1.0);
		color += textureLod(sampler2D(tex, sampler_linear), vin.tex - off, 0.0).r * w;
		weight += w;
	}

	frag_color = color / weight;
}
#endif
