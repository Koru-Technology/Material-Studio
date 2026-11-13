#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 invVP;
	layout(offset = 64) vec3 eye;
	layout(offset = 80) vec4 envmap_data; // angle, sin(angle), cos(angle), strength
	layout(offset = 96) int envmap_num_mipmaps;
	layout(offset = 112) vec4 shirr0;
	layout(offset = 128) vec4 shirr1;
	layout(offset = 144) vec4 shirr2;
	layout(offset = 160) vec4 shirr3;
	layout(offset = 176) vec4 shirr4;
	layout(offset = 192) vec4 shirr5;
	layout(offset = 208) vec4 shirr6;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D gbuffer0;
layout(set = 0, binding = 3) uniform texture2D gbuffer1;
layout(set = 0, binding = 4) uniform texture2D senvmap_radiance;
layout(set = 0, binding = 5) uniform texture2D senvmap_radiance0;
layout(set = 0, binding = 6) uniform texture2D senvmap_radiance1;
layout(set = 0, binding = 7) uniform texture2D senvmap_radiance2;
layout(set = 0, binding = 8) uniform texture2D senvmap_radiance3;
layout(set = 0, binding = 9) uniform texture2D senvmap_radiance4;
layout(set = 0, binding = 10) uniform texture2D ssaotex;

const float PI = 3.14159265358979;
const float PI2 = 6.28318530718;

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

// void unpack_f32_i16(float val, out float f, out uint i) {
// 	// Constant optimize by compiler
// 	const int num_bit_target = 16;
// 	const int num_bit_i = 4;
// 	const float prec = float(1 << num_bit_target);
// 	const float maxi = float(1 << num_bit_i);
// 	const float prec_minus_one = prec - 1.0;
// 	const float t1 = ((prec / maxi) - 1.0) / prec_minus_one;
// 	const float t2 = (prec / maxi) / prec_minus_one;
// 	// Code
// 	// extract integer part
// 	// + rcp(prec_minus_one) to deal with precision issue
// 	i = uint((val / t2) + (1.0 / prec_minus_one));
// 	// Now that we have i, solve formula in pack_f32_i16 for f
// 	//f = (val - t2 * float(i)) / t1 => convert in mads form
// 	f = clamp((-t2 * float(i) + val) / t1, 0.0, 1.0); // Saturate in case of precision issue
// }

vec3 surface_albedo(vec3 base_color, float metalness) {
	return mix(base_color, vec3(0.0, 0.0, 0.0), metalness);
}

vec3 surface_f0(vec3 base_color, float metalness) {
	return mix(vec3(0.04, 0.04, 0.04), base_color, metalness);
}

const float c1 = 0.429043;
const float c2 = 0.511664;
const float c3 = 0.743125;
const float c4 = 0.886227;
const float c5 = 0.247708;

vec3 sh_irradiance(vec3 nor) {
	// TODO: Use padding for 4th component and pass shirr[].xyz directly
	vec3 cl00 = vec3(constants.shirr0.x, constants.shirr0.y, constants.shirr0.z);
	vec3 cl1m1 = vec3(constants.shirr0.w, constants.shirr1.x, constants.shirr1.y);
	vec3 cl10 = vec3(constants.shirr1.z, constants.shirr1.w, constants.shirr2.x);
	vec3 cl11 = vec3(constants.shirr2.y, constants.shirr2.z, constants.shirr2.w);
	vec3 cl2m2 = vec3(constants.shirr3.x, constants.shirr3.y, constants.shirr3.z);
	vec3 cl2m1 = vec3(constants.shirr3.w, constants.shirr4.x, constants.shirr4.y);
	vec3 cl20 = vec3(constants.shirr4.z, constants.shirr4.w, constants.shirr5.x);
	vec3 cl21 = vec3(constants.shirr5.y, constants.shirr5.z, constants.shirr5.w);
	vec3 cl22 = vec3(constants.shirr6.x, constants.shirr6.y, constants.shirr6.z);
	return (
		cl22 * c1 * (nor.x * nor.x - nor.y * nor.y) +
		cl20 * c3 * nor.z * nor.z +
		cl00 * c4 -
		cl20 * c5 +
		cl2m2 * 2.0 * c1 * nor.x * nor.y +
		cl21  * 2.0 * c1 * nor.x * nor.z +
		cl2m1 * 2.0 * c1 * nor.y * nor.z +
		cl11  * 2.0 * c2 * nor.x +
		cl1m1 * 2.0 * c2 * nor.y +
		cl10  * 2.0 * c2 * nor.z
	);
}

float mip_from_roughness(float roughness, float num_mipmaps) {
	// First mipmap level = roughness 0, last = roughness = 1
	return roughness * num_mipmaps;
}

vec2 envmap_equirect(vec3 normal, float angle) {
	float phi = acos(normal.z);
	float theta = atan(-normal.y, normal.x) + PI + angle;
	return vec2(theta / PI2, phi / PI);
}

vec3 envmap_sample(float lod, vec2 coord) {
	if (lod == 0.0) {
		return textureLod(sampler2D(senvmap_radiance, sampler_linear), coord, 0.0).rgb;
	}
	if (lod == 1.0) {
		return textureLod(sampler2D(senvmap_radiance0, sampler_linear), coord, 0.0).rgb;
	}
	if (lod == 2.0) {
		return textureLod(sampler2D(senvmap_radiance1, sampler_linear), coord, 0.0).rgb;
	}
	if (lod == 3.0) {
		return textureLod(sampler2D(senvmap_radiance2, sampler_linear), coord, 0.0).rgb;
	}
	if (lod == 4.0) {
		return textureLod(sampler2D(senvmap_radiance3, sampler_linear), coord, 0.0).rgb;
	}
	return textureLod(sampler2D(senvmap_radiance4, sampler_linear), coord, 0.0).rgb;
}

float specular_occlusion(float dotnv, float ao, float roughness) {
	float e = exp((-16.0 * roughness - 1.0) * log(2.0)); // exp2()
	return clamp(pow(dotnv + ao, e) - 1.0 + ao, 0.0, 1.0);
}

// https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
vec3 env_brdf_approx(vec3 specular, float roughness, float dotnv) {
	vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
	vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
	vec4 r = c0 * roughness + c1;
	float a004 = min(r.x * r.x, exp((-9.28 * dotnv) * log(2.0))) * r.x + r.y;
	vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
	return specular * ab.x + ab.y;
}

#ifdef VERT
layout(location = 0) in vec2 in_pos;
layout(location = 0) out Varyings {
	vec2 tex;
	vec3 view_ray;
} vout;

void main() {	vout.tex = in_pos.xy * 0.5 + 0.5;
	vout.tex.y = 1.0 - vout.tex.y;
	gl_Position = vec4(in_pos.xy, 1.0, 1.0); // At the far plane, the greater depth test rejects the background

	// NDC (at the back of cube)
	vec4 v = vec4(in_pos.xy, 1.0, 1.0);
	v = constants.invVP * v;
	v.xyz = v.xyz / v.w;
	vout.view_ray = v.xyz - constants.eye;
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 tex;
	vec3 view_ray;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	// normal.xy, roughness, metallic/matid
	vec4 g0 = textureLod(sampler2D(gbuffer0, sampler_linear), vin.tex, 0.0);

	vec3 n;
	n.z = 1.0 - abs(g0.x) - abs(g0.y);
	if (n.z >= 0.0) {
		//n.xy = g0.xy;
		n.x = g0.x;
		n.y = g0.y;
	}
	else {
		//n.xy = octahedron_wrap(g0.xy);
		vec2 f2 = octahedron_wrap(g0.xy);
		n.x = f2.x;
		n.y = f2.y;
	}
	n = normalize(n);

	float roughness = g0.b;
	float metallic;
	uint matid;
	// unpack_f32_i16(g0.a, metallic, matid);
	matid = uint((g0.a / 0.06250047610269868710814625956118106842041015625) + (1.0 / 65535.0));
	metallic = clamp((-0.06250047610269868710814625956118106842041015625 * float(matid) + g0.a) / 0.062485207147583624058737396240234375, 0.0, 1.0);

	vec4 g1 = textureLod(sampler2D(gbuffer1, sampler_linear), vin.tex, 0.0); // basecolor.rgb, occ
	vec3 albedo = surface_albedo(g1.rgb, metallic);
	vec3 f0 = surface_f0(g1.rgb, metallic);

	vec3 v = normalize(-vin.view_ray);

	float dotnv = dot(n, v);
	if (dotnv < 0.05) {
		n = normalize(n + v * (0.05 - dotnv));
		dotnv = dot(n, v);
	}
	dotnv = max(0.0001, dotnv);

	float ao = g1.a * textureLod(sampler2D(ssaotex, sampler_linear), vin.tex, 0.0).r;

	// Envmap
	vec3 envl = sh_irradiance(
		vec3(
			n.x * constants.envmap_data.z + n.y * constants.envmap_data.y,
			n.y * constants.envmap_data.z - n.x * constants.envmap_data.y,
			n.z
		)
	);
	envl = envl / PI;

	vec3 reflection_world = reflect(-v, n);
	// float lod = mip_from_roughness(roughness, float(constants.envmap_num_mipmaps));
	// vec3 prefiltered_color = textureLod(sampler2D(senvmap_radiance, sampler_linear), envmap_equirect(reflection_world, constants.envmap_data.x), lod).rgb;
	float lod = mip_from_roughness(roughness, 5.0);
	float lod0 = floor(lod);
	float lod1 = ceil(lod);
	float lodf = lod - lod0;
	vec2 envmap_coord = envmap_equirect(reflection_world, constants.envmap_data.x);
	vec3 lodc0 = envmap_sample(lod0, envmap_coord);
	vec3 lodc1 = envmap_sample(lod1, envmap_coord);
	vec3 prefiltered_color = mix(lodc0, lodc1, lodf);

	// Emission - basecolor holds the emitted color, it is not an albedo
	vec3 emission = vec3(0.0, 0.0, 0.0);
	if (matid == uint(1.0)) { // materialid
		emission = g1.rgb;
		albedo = vec3(0.0, 0.0, 0.0);
	}

	envl.rgb = envl.rgb * albedo * ao;
	envl.rgb = envl.rgb + prefiltered_color * env_brdf_approx(f0, roughness, dotnv) * specular_occlusion(dotnv, ao, roughness);
	envl.rgb = envl.rgb * constants.envmap_data.w;

	vec4 color;
	color.rgb = envl.rgb + emission;

	color.rgb = max(color.rgb, vec3(0.0, 0.0, 0.0));
	color.a = 1.0; // Mark as opaque
	frag_color = color;
}
#endif
