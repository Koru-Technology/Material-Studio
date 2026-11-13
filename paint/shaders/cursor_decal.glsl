#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 VP;
	layout(offset = 64) mat4 invVP;
	layout(offset = 128) vec2 mouse;
	layout(offset = 136) vec2 tex_step;
	layout(offset = 144) float radius;
	layout(offset = 160) vec3 camera_right;
	layout(offset = 176) float opacity;
	layout(offset = 184) vec2 angle;
	layout(offset = 192) float scale_x;
	layout(offset = 208) vec3 camera_up;
	layout(offset = 224) float camera_align;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D gbufferD;
layout(set = 0, binding = 3) uniform texture2D texdecal;
layout(set = 0, binding = 4) uniform texture2D gbuffer0;

vec3 get_pos(vec2 uv) {
	float depth = textureLod(sampler2D(gbufferD, sampler_linear), vec2(uv.x, 1.0 - uv.y), 0.0).r;
	// Off the mesh, anchor at the depth of the world origin
	if (depth >= 1.0) {
		vec4 o = constants.VP * vec4(0.0, 0.0, 0.0, 1.0);
		if (o.w > 0.0) {
			depth = o.z / o.w;
		}
	}
	vec4 wpos = vec4(uv * 2.0 - 1.0, depth, 1.0);
	wpos = constants.invVP * wpos;
	return wpos.xyz / wpos.w;
}

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
	r.x = 1.0 - abs(v.y);
	r.y = 1.0 - abs(v.x);
	return r * a;
}

vec3 get_normal(vec2 uv) {
	vec2 g0 = textureLod(sampler2D(gbuffer0, sampler_linear), vec2(uv.x, 1.0 - uv.y), 0.0).rg;
	vec3 n;
	n.z = 1.0 - abs(g0.x) - abs(g0.y);
	if (n.z >= 0.0) {
		n.x = g0.x;
		n.y = g0.y;
	}
	else {
		vec2 fw = octahedron_wrap(g0.xy);
		n.x = fw.x;
		n.y = fw.y;
	}
	return n;
}

#ifdef VERT
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec2 in_nor;
layout(location = 2) in vec2 in_tex;
layout(location = 0) out Varyings {
	vec2 tex;
} vout;

void main() {
	float keep = in_pos.x + in_nor.x; // hlsl
	vout.tex = in_tex;
	vec3 wpos = get_pos(constants.mouse);

	vec2 uv1 = constants.mouse + constants.tex_step * vec2(4.0, 4.0);
	vec2 uv2 = constants.mouse - constants.tex_step * vec2(4.0, 4.0);
	vec3 n = normalize(get_normal(constants.mouse) + get_normal(uv1) + get_normal(uv2));
	float depth = textureLod(sampler2D(gbufferD, sampler_linear), vec2(constants.mouse.x, 1.0 - constants.mouse.y), 0.0).r;

	vec3 n_tan;
	vec3 n_bin;
	if (constants.camera_align > 0.0 || depth >= 1.0) {
		n_tan = constants.camera_right;
		n_bin = -constants.camera_up;
	}
	else {
		vec3 cam_right = constants.camera_right;
		if (abs(dot(n, cam_right)) > 0.999) { cam_right = vec3(0.0, 0.0, 1.0); }
		n_tan = normalize(cam_right - n * dot(cam_right, n));
		n_bin = cross(n_tan, n);
	}

	// Rotate tangent basis by brush angle
	vec3 r_tan = constants.angle.x * n_tan + constants.angle.y * n_bin;
	vec3 r_bin = -constants.angle.y * n_tan + constants.angle.x * n_bin;

	if (gl_VertexIndex == 3) {
		wpos += (-constants.scale_x * r_tan - r_bin) * constants.radius;
	}
	/*else */if (gl_VertexIndex == 2) {
		wpos += ( constants.scale_x * r_tan - r_bin) * constants.radius;
	}
	/*else */if (gl_VertexIndex == 1) {
		wpos += ( constants.scale_x * r_tan + r_bin) * constants.radius;
	}
	/*else */if (gl_VertexIndex == 0) {
		wpos += (-constants.scale_x * r_tan + r_bin) * constants.radius;
	}

	gl_Position = constants.VP * vec4(wpos, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec2 tex;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
	vec4 col = textureLod(sampler2D(texdecal, sampler_linear), vin.tex, 0.0);
	frag_color = vec4(col.rgb, col.a * constants.opacity);
}
#endif
