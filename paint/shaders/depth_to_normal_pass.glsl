#version 450

layout(set = 0, binding = 0, std140) uniform Constants {
	layout(offset = 0) vec2 texel_size;
} constants;

layout(set = 0, binding = 1) uniform sampler sampler_linear;
layout(set = 0, binding = 2) uniform texture2D height_map;

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
	vec2 ts = constants.texel_size;
	float h00 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2(-2.0, -2.0) * ts, 0.0).r;
	float h01 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2(-1.0, -2.0) * ts, 0.0).r;
	float h02 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 0.0, -2.0) * ts, 0.0).r;
	float h03 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 1.0, -2.0) * ts, 0.0).r;
	float h04 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 2.0, -2.0) * ts, 0.0).r;
	float h10 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2(-2.0, -1.0) * ts, 0.0).r;
	float h11 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2(-1.0, -1.0) * ts, 0.0).r;
	float h12 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 0.0, -1.0) * ts, 0.0).r;
	float h13 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 1.0, -1.0) * ts, 0.0).r;
	float h14 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 2.0, -1.0) * ts, 0.0).r;
	float h20 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2(-2.0,  0.0) * ts, 0.0).r;
	float h21 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2(-1.0,  0.0) * ts, 0.0).r;
	float h23 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 1.0,  0.0) * ts, 0.0).r;
	float h24 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 2.0,  0.0) * ts, 0.0).r;
	float h30 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2(-2.0,  1.0) * ts, 0.0).r;
	float h31 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2(-1.0,  1.0) * ts, 0.0).r;
	float h32 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 0.0,  1.0) * ts, 0.0).r;
	float h33 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 1.0,  1.0) * ts, 0.0).r;
	float h34 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 2.0,  1.0) * ts, 0.0).r;
	float h40 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2(-2.0,  2.0) * ts, 0.0).r;
	float h41 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2(-1.0,  2.0) * ts, 0.0).r;
	float h42 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 0.0,  2.0) * ts, 0.0).r;
	float h43 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 1.0,  2.0) * ts, 0.0).r;
	float h44 = textureLod(sampler2D(height_map, sampler_linear), vin.tex + vec2( 2.0,  2.0) * ts, 0.0).r;
	float dx = 2.0 * (h00 - h04) + 1.0 * (h01 - h03)
	              + 3.0 * (h10 - h14) + 2.0 * (h11 - h13)
	              + 4.0 * (h20 - h24) + 3.0 * (h21 - h23)
	              + 3.0 * (h30 - h34) + 2.0 * (h31 - h33)
	              + 2.0 * (h40 - h44) + 1.0 * (h41 - h43);
	float dy = 2.0 * (h40 - h00) + 3.0 * (h41 - h01) + 4.0 * (h42 - h02) + 3.0 * (h43 - h03) + 2.0 * (h44 - h04)
	              + 1.0 * (h30 - h10) + 2.0 * (h31 - h11) + 3.0 * (h32 - h12) + 2.0 * (h33 - h13) + 1.0 * (h34 - h14);
	vec3 normal = normalize(vec3(dx, dy, 1.4375));
	normal = normal * 0.5 + 0.5;
	frag_color = vec4(normal.x, normal.y, normal.z, 1.0);
}
#endif
