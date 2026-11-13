#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 WVP;
	layout(offset = 64) mat3 N;
	layout(offset = 112) float tex_unpack;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;

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

	// return (1.0 - abs(v.yx)) * (vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0));
}

float pack_f32_i16(float f, uint i) {
	// GBuffer helper - Sebastien Lagarde
	// https://seblagarde.wordpress.com/2018/09/02/gbuffer-helper-packing-integer-and-float-together/
	// const int num_bit_target = 16;
	// const int num_bit_i = 4;
	// const float prec = float(1 << num_bit_target);
	// const float maxi = float(1 << num_bit_i);
	// const float prec_minus_one = prec - 1.0;
	// const float t1 = ((prec / maxi) - 1.0) / prec_minus_one;
	// const float t2 = (prec / maxi) / prec_minus_one;
	// return t1 * f + t2 * float(i);
	return 0.062485207147583624 * min(f, 0.9990234375) + 0.062500476102698687 * float(i);
}

#ifdef VERT
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec2 in_nor;
layout(location = 2) in vec2 in_tex;
layout(location = 0) out Varyings {
	vec2 tex;
	vec3 wnormal;
} vout;

void main() {	gl_Position = constants.WVP * vec4(in_pos.xyz, 1.0);
	vout.tex = in_tex * constants.tex_unpack;
	vout.wnormal = normalize(constants.N * vec3(in_nor.xy, in_pos.w));
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 tex;
	vec3 wnormal;
} vin;
layout(location = 0) out vec4 frag_out_0;
layout(location = 1) out vec4 frag_out_1;
layout(location = 2) out vec4 frag_out_2;

void main() {
	vec3 n = normalize(vin.wnormal);
	vec3 basecol = vec3(0.2, 0.2, 0.2) + vin.tex.x * 0.0000001; // keep tex_coord
	float roughness = 0.4;
	float metallic = 0.0;
	float occlusion = 1.0;

	// n /= abs(n.x) + abs(n.y) + abs(n.z);
	n = n / (abs(n.x) + abs(n.y) + abs(n.z));
	if (n.z >= 0.0) {
		n.xy = n.xy;
	}
	else {
		n.xy = octahedron_wrap(n.xy);
	}

	// uint matid = 0;
	uint matid = uint(0.0);

	frag_out_0 = vec4(n.xy, roughness, pack_f32_i16(metallic, matid));
	frag_out_1 = vec4(basecol, occlusion);
	frag_out_2 = vec4(0.0, 0.0, 0.0, 0.0);
	return;
}
#endif
