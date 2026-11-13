#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec4 empty;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D height_map;
layout(set = 0, binding = 3) uniform texture2D normal_map;

const float num_dirs   = 64.0;  // cosine-weighted hemisphere directions
const float num_steps  = 24.0;  // height-field march steps per direction
const float max_radius = 0.04;  // search radius
const float height_scale = 1.0;
const float ao_bias    = 0.0015; // ignore occluders within this height of the surface
const float ao_max_diff = 0.3;   // ignore occluders taller than this
const float ao_power   = 1.6;    // contrast of the final occlusion
const float PI2        = 6.28318530718;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 tangent(vec3 n) {
	vec3 t1 = cross(n, vec3(0.0, 0.0, 1.0));
	vec3 t2 = cross(n, vec3(0.0, 1.0, 0.0));
	if (length(t1) > length(t2)) {
		return normalize(t1);
	}
	return normalize(t2);
}

float ao_ray(vec3 dir, vec2 uv, float h0) {
	float step_size = max_radius / num_steps;
	float t = step_size;
	int i = 0;
	while (i < int(num_steps)) {
		vec2 coord = uv + dir.xy * t;
		if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0) {
			return 0.0; // walked off the map
		}
		float ray_z = h0 + dir.z * t;
		float surf = textureLod(sampler2D(height_map, sampler_linear), coord, 0.0).r * height_scale;
		float delta = surf - ray_z;
		if (delta > ao_bias && delta < ao_max_diff) {
			return 1.0 - t / max_radius; // linear distance falloff
		}
		t += step_size;
		i += 1;
	}
	return 0.0;
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
layout(location = 0) out vec4 frag_color;

void main() {
	float height = textureLod(sampler2D(height_map, sampler_linear), vin.tex, 0.0).r * height_scale;
	vec3 normal = textureLod(sampler2D(normal_map, sampler_linear), vin.tex, 0.0).rgb * 2.0 - 1.0;
	vec3 n = normalize(normal);

	vec3 t1 = tangent(n);
	vec3 t2 = cross(n, t1);

	float jitter = hash(vin.tex);
	float h0 = height + ao_bias;

	float occ = 0.0;
	int i = 0;
	while (i < int(num_dirs)) {
		// Cosine-weighted hemisphere sample
		float u1 = (float(i) + 0.5) / num_dirs;
		float u2 = fract(float(i) * 0.61803398875 + jitter);
		float r = sqrt(u1);
		float phi = PI2 * u2;
		float lx = r * cos(phi);
		float ly = r * sin(phi);
		float lz = sqrt(max(0.0, 1.0 - u1));
		vec3 dir = lx * t1 + ly * t2 + lz * n;

		occ += ao_ray(dir, vin.tex, h0);
		i += 1;
	}

	float ao = 1.0 - occ / num_dirs;
	ao = pow(max(0.0, ao), ao_power);
	frag_color = vec4(ao, ao, ao, 1.0);
}
#endif
