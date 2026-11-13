#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) mat4 WVP;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D my_texture;

#ifdef VERT
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec2 in_nor;
layout(location = 2) in vec2 in_tex;
layout(location = 0) out Varyings {
	vec3 nor;
	vec2 tex;
} vout;

void main() {	vout.nor = vec3(in_nor.xy, in_pos.w);
	vout.tex = in_tex;
	gl_Position = constants.WVP * vec4(in_pos.xyz, 1.0);
}
#endif

#ifdef FRAG
layout(location = 0) in Varyings {
	vec3 nor;
	vec2 tex;
} vin;
layout(location = 0) out vec4 frag_color;

void main() {
    vec3 l = vec3(0.5, 0.0, 0.5);
    vec3 base_color = vec3(1.0, 1.0, 1.0); // texture(sampler2D(my_texture, sampler_linear), vin.tex).rgb;
    vec3 ambient = base_color * 0.5;
    vec3 n = normalize(vin.nor);
    float dotnl = max(dot(n, l), 0.0);
    vec3 diffuse = dotnl * base_color;
    frag_color = vec4(ambient + diffuse, 1.0);
}
#endif
