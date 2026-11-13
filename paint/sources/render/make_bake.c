
#include "../global.h"

char *make_bake_axis_string(i32 i) {
	return i == BAKE_AXIS_X    ? "vec3(1,0,0)"
	       : i == BAKE_AXIS_Y  ? "vec3(0,1,0)"
	       : i == BAKE_AXIS_Z  ? "vec3(0,0,1)"
	       : i == BAKE_AXIS_MX ? "vec3(-1,0,0)"
	       : i == BAKE_AXIS_MY ? "vec3(0,-1,0)"
	                           : "vec3(0,0,-1)";
}

void make_bake_run(node_shader_context_t *con, node_shader_t *shader) {
	if (g_context->bake_type == BAKE_TYPE_CURVATURE) {
		char *strength = string_tmp("float(%s)", f32_to_string(g_context->bake_curv_strength));
		char *radius   = string_tmp("float(%s)", f32_to_string(g_context->bake_curv_radius));
		char *offset   = string_tmp("float(%s)", f32_to_string(g_context->bake_curv_offset));
		shader->frag_n   = true;
		node_shader_write_frag(shader, "vec3 dx = dFdx(n);");
		node_shader_write_frag(shader, "vec3 dy = dFdy(n);");
		node_shader_write_frag(shader, "float curvature = max(dot(dx, dx), dot(dy, dy));");
		node_shader_write_frag(shader,
		                       string_tmp("curvature = clamp(pow(curvature, (1.0 / %s) * 0.25) * %s * 2.0 + %s / 10.0, 0.0, 1.0);", radius, strength, offset));
		if (g_context->bake_axis != BAKE_AXIS_XYZ) {
			char *axis = make_bake_axis_string(g_context->bake_axis);
			node_shader_write_frag(shader, string_tmp("curvature *= dot(n, %s);", axis));
		}
		node_shader_write_frag(shader, "output[0] = vec4(curvature, curvature, curvature, 1.0);");
	}
	else if (g_context->bake_type == BAKE_TYPE_NORMAL) { // Tangent
		shader->frag_n = true;
		node_shader_add_texture(shader, "texpaint_undo", "_texpaint_undo"); // Baked high-poly normals
		node_shader_write_frag(shader,
		                       "vec3 n0 = textureLod(sampler2D(texpaint_undo, sampler_linear), tex_coord, 0.0).rgb * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);");
		// Tangent basis solved from the uv derivatives
		shader->frag_wposition = true;
		node_shader_write_frag(shader, "vec3 bake_dp1 = dFdx(input.wposition);");
		node_shader_write_frag(shader, "vec3 bake_dp2 = dFdy(input.wposition);");
		node_shader_write_frag(shader, "vec2 bake_duv1 = dFdx(tex_coord);");
		node_shader_write_frag(shader, "vec2 bake_duv2 = dFdy(tex_coord);");
		node_shader_write_frag(shader, "float bake_u1 = bake_duv1.x;");
		node_shader_write_frag(shader, "float bake_v1 = bake_duv1.y;");
		node_shader_write_frag(shader, "float bake_u2 = bake_duv2.x;");
		node_shader_write_frag(shader, "float bake_v2 = bake_duv2.y;");
		node_shader_write_frag(shader, "float bake_s = 1.0;");
		node_shader_write_frag(shader, "if (bake_u1 * bake_v2 - bake_u2 * bake_v1 < 0.0) { bake_s = -1.0; }");
		node_shader_write_frag(shader, "vec3 bake_tu = (bake_dp1 * bake_v2 - bake_dp2 * bake_v1) * bake_s;");
		node_shader_write_frag(shader, "vec3 bake_tv = (bake_dp2 * bake_u1 - bake_dp1 * bake_u2) * bake_s;");
		node_shader_write_frag(shader, "vec3 bake_t = bake_tu - n * dot(n, bake_tu);");
		node_shader_write_frag(shader, "vec3 res = vec3(0.5, 0.5, 1.0);");
		// Zero area triangles, like the poles of a uv sphere
		node_shader_write_frag(shader, "if (dot(bake_t, bake_t) > dot(bake_tu, bake_tu) * 0.000001) {");
		node_shader_write_frag(shader, "	bake_t = normalize(bake_t);");
		node_shader_write_frag(shader, "	vec3 bake_b = bake_tv - n * dot(n, bake_tv) - bake_t * dot(bake_t, bake_tv);");
		node_shader_write_frag(shader, "	if (dot(bake_b, bake_b) > dot(bake_tv, bake_tv) * 0.000001) {");
		node_shader_write_frag(shader, "		bake_b = normalize(bake_b);");
		node_shader_write_frag(
		    shader, "		res = normalize(vec3(dot(n0, bake_t), 0.0 - dot(n0, bake_b), dot(n0, n))) * vec3(0.5, 0.5, 0.5) + vec3(0.5, 0.5, 0.5);");
		node_shader_write_frag(shader, "	}");
		node_shader_write_frag(shader, "}");
		node_shader_write_frag(shader, "output[0] = vec4(res, 1.0);");
	}
	else if (g_context->bake_type == BAKE_TYPE_NORMAL_OBJECT) {
		shader->frag_n = true;
		node_shader_write_frag(shader, "output[0] = vec4(n * vec3(0.5, 0.5, 0.5) + vec3(0.5, 0.5, 0.5), 1.0);");
		if (g_context->bake_up_axis == BAKE_UP_AXIS_Y) {
			node_shader_write_frag(shader, "output[0].rgb = vec3(output[0].r, output[0].b, 1.0 - output[0].g);");
		}
	}
	else if (g_context->bake_type == BAKE_TYPE_HEIGHT) {
		shader->frag_wposition = true;
		node_shader_add_texture(shader, "texpaint_undo", "_texpaint_undo"); // Baked high-poly positions
		node_shader_write_frag(shader,
		                       "vec3 wpos0 = textureLod(sampler2D(texpaint_undo, sampler_linear), tex_coord, 0.0).rgb * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);");
		node_shader_write_frag(shader, "float res = distance(wpos0, input.wposition) * 10.0;");
		node_shader_write_frag(shader, "output[0] = vec4(res, res, res, 1.0);");
	}
	else if (g_context->bake_type == BAKE_TYPE_DERIVATIVE) {
		node_shader_add_texture(shader, "texpaint_undo", "_texpaint_undo"); // Baked height
		node_shader_write_frag(shader, "vec2 tex_dx = dFdx(tex_coord);");
		node_shader_write_frag(shader, "vec2 tex_dy = dFdy(tex_coord);");
		node_shader_write_frag(shader, "float h0 = textureLod(sampler2D(texpaint_undo, sampler_linear), tex_coord, 0.0).r * 100.0;");
		node_shader_write_frag(shader, "float h1 = textureLod(sampler2D(texpaint_undo, sampler_linear), tex_coord + tex_dx, 0.0).r * 100.0;");
		node_shader_write_frag(shader, "float h2 = textureLod(sampler2D(texpaint_undo, sampler_linear), tex_coord + tex_dy, 0.0).r * 100.0;");
		node_shader_write_frag(shader, "output[0] = vec4((h1 - h0) * 0.5 + 0.5, (h2 - h0) * 0.5 + 0.5, 0.0, 1.0);");
	}
	else if (g_context->bake_type == BAKE_TYPE_POSITION) {
		shader->frag_wposition = true;
		node_shader_write_frag(shader, "output[0] = vec4(input.wposition * vec3(0.5, 0.5, 0.5) + vec3(0.5, 0.5, 0.5), 1.0);");
		if (g_context->bake_up_axis == BAKE_UP_AXIS_Y) {
			node_shader_write_frag(shader, "output[0].rgb = vec3(output[0].r, output[0].b, 1.0 - output[0].g);");
		}
	}
	else if (g_context->bake_type == BAKE_TYPE_TEXCOORD) {
		node_shader_write_frag(shader, "output[0] = vec4(tex_coord.xy, 0.0, 1.0);");
	}
	else if (g_context->bake_type == BAKE_TYPE_MATERIALID) {
		node_shader_add_texture(shader, "texpaint_nor_undo", "_texpaint_nor_undo");
		node_shader_write_frag(shader, "float sample_matid = textureLod(sampler2D(texpaint_nor_undo, sampler_linear), tex_coord, 0.0).a + 1.0 / 255.0;");
		node_shader_write_frag(shader, "float matid_r = fract(sin(dot(vec2(sample_matid, sample_matid * 20.0), vec2(12.9898, 78.233))) * 43758.5453);");
		node_shader_write_frag(shader, "float matid_g = fract(sin(dot(vec2(sample_matid * 20.0, sample_matid), vec2(12.9898, 78.233))) * 43758.5453);");
		node_shader_write_frag(shader, "float matid_b = fract(sin(dot(vec2(sample_matid, sample_matid * 40.0), vec2(12.9898, 78.233))) * 43758.5453);");
		node_shader_write_frag(shader, "output[0] = vec4(matid_r, matid_g, matid_b, 1.0);");
	}
	else if (g_context->bake_type == BAKE_TYPE_OBJECTID) {
		node_shader_add_constant(shader, "float object_id", "_object_id");
		node_shader_write_frag(shader, "float obid = constants.object_id + 1.0 / 255.0;");
		node_shader_write_frag(shader, "float id_r = fract(sin(dot(vec2(obid, obid * 20.0), vec2(12.9898, 78.233))) * 43758.5453);");
		node_shader_write_frag(shader, "float id_g = fract(sin(dot(vec2(obid * 20.0, obid), vec2(12.9898, 78.233))) * 43758.5453);");
		node_shader_write_frag(shader, "float id_b = fract(sin(dot(vec2(obid, obid * 40.0), vec2(12.9898, 78.233))) * 43758.5453);");
		node_shader_write_frag(shader, "output[0] = vec4(id_r, id_g, id_b, 1.0);");
	}
	else if (g_context->bake_type == BAKE_TYPE_VERTEX_COLOR) {
		if (con->allow_vcols) {
			node_shader_context_add_elem(con, "col", "short4norm");
			node_shader_write_frag(shader, "output[0] = vec4(input.vcolor.r, input.vcolor.g, input.vcolor.b, 1.0);");
		}
		else {
			node_shader_write_frag(shader, "output[0] = vec4(1.0, 1.0, 1.0, 1.0);");
		}
	}
}

void make_bake_position_normal(node_shader_t *shader) {
	node_shader_add_out(shader, "vec3 position");
	node_shader_add_out(shader, "vec3 normal");
	node_shader_add_constant(shader, "mat4 W", "_world_matrix");
	node_shader_write_vert(shader, "output.position = (constants.W * vec4(input.pos.xyz, 1.0)).xyz;");
	node_shader_write_vert(shader, "output.normal = vec3(input.nor.xy, input.pos.w);");
	node_shader_write_vert(shader, "vec2 tpos = vec2(input.tex.x * 2.0 - 1.0, (1.0 - input.tex.y) * 2.0 - 1.0);");
	node_shader_write_vert(shader, "output.pos = vec4(tpos, 0.0, 1.0);");
	shader->frag_out = "vec4[2]";
	node_shader_write_frag(shader, "output[0] = vec4(input.position, 1.0);");
	node_shader_write_frag(shader, "output[1] = vec4(input.normal, 1.0);");
}

void make_bake_set_color_writes(node_shader_context_t *con_paint) {
	// Bake into base color, disable other slots
	con_paint->data->color_writes_red->buffer[1]   = false;
	con_paint->data->color_writes_green->buffer[1] = false;
	con_paint->data->color_writes_blue->buffer[1]  = false;
	con_paint->data->color_writes_alpha->buffer[1] = false;
	con_paint->data->color_writes_red->buffer[2]   = false;
	con_paint->data->color_writes_green->buffer[2] = false;
	con_paint->data->color_writes_blue->buffer[2]  = false;
	con_paint->data->color_writes_alpha->buffer[2] = false;
}
