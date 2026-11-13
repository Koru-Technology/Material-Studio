
#include "../global.h"

char *str_get_pos_nor_from_depth = "\
vec3 get_pos_from_depth(vec2 uv, mat4 invVP) { \
	float depth = textureLod(sampler2D(gbufferD, sampler_linear), vec2(uv.x, 1.0 - uv.y), 0.0).r; \
	vec4 wpos = vec4(uv * 2.0 - 1.0, depth, 1.0); \
	wpos = invVP * wpos; \
	return wpos.xyz / wpos.w; \
} \
vec3 get_nor_from_depth(vec3 p0, vec2 uv, mat4 invVP, vec2 tex_step) { \
	vec3 p1 = get_pos_from_depth(uv + vec2(tex_step.x * 4.0, 0.0), invVP); \
	vec3 p2 = get_pos_from_depth(uv + vec2(0.0, tex_step.y * 4.0), invVP); \
	return normalize(cross(p2 - p0, p1 - p0)); \
} \
";

void make_picking_run(node_shader_t *shader) {
	// Mangle vertices to form full screen triangle
	node_shader_write_vert(shader, "output.pos = vec4(-1.0 + float((gl_VertexIndex & 1) << uint(2)), -1.0 + float((gl_VertexIndex & 2) << uint(1)), 0.0, 1.0);");

	node_shader_add_texture(shader, "gbuffer2", NULL);
	node_shader_add_constant(shader, "vec2 gbuffer_size", "_gbuffer_size");
	node_shader_add_constant(shader, "vec4 inp", "_input_brush");

	// node_shader_write_frag(shader, "float2 tex_coord_inp = gbuffer2[uint2(constants.inp.x * constants.gbuffer_size.x, constants.inp.y *
	// constants.gbuffer_size.y)].ba;");
	node_shader_write_frag(
	    shader, "vec4 tex_coord_inp4 = texelFetch(sampler2D(gbuffer2, sampler_linear), ivec2(uint(constants.inp.x * constants.gbuffer_size.x), uint(constants.inp.y * constants.gbuffer_size.y)), 0);");
	node_shader_write_frag(shader, "vec2 tex_coord_inp = tex_coord_inp4.ba + tex_coord_inp4.rg;");

	if (g_context->tool == TOOL_TYPE_COLORID) {
		shader->frag_out = "vec4";
		node_shader_add_texture(shader, "texcolorid", "_texcolorid");
		node_shader_write_frag(shader, "vec3 idcol = textureLod(sampler2D(texcolorid, sampler_linear), tex_coord_inp, 0.0).rgb;");
		node_shader_write_frag(shader, "output = vec4(idcol, 1.0);");
	}
	else if (g_context->tool == TOOL_TYPE_PICKER) {
		if (g_context->pick_pos_nor_tex) {
			shader->frag_out = "vec4[2]";
			node_shader_add_texture(shader, "gbufferD", NULL);
			node_shader_add_constant(shader, "mat4 invVP", "_inv_view_proj_matrix");
			node_shader_add_function(shader, str_get_pos_nor_from_depth);
			node_shader_write_frag(shader, "vec3 out_pos_from_depth = get_pos_from_depth(vec2(constants.inp.x, 1.0 - constants.inp.y), constants.invVP);");
			node_shader_write_frag(shader, "vec3 out_nor_from_depth = get_nor_from_depth(out_pos_from_depth, vec2(constants.inp.x, 1.0 - "
			                             "constants.inp.y), constants.invVP, vec2(1.0, 1.0) / constants.gbuffer_size);");
			node_shader_write_frag(shader, "output[0] = vec4(out_pos_from_depth, tex_coord_inp.x);");
			node_shader_write_frag(shader, "output[1] = vec4(out_nor_from_depth, tex_coord_inp.y);");
		}
		else if (slot_layer_is_mask(g_context->layer)) {
			shader->frag_out = "vec4[2]";
			node_shader_add_texture(shader, "texpaint", NULL);
			node_shader_write_frag(shader, "float texpaint_val = textureLod(sampler2D(texpaint, sampler_linear), tex_coord_inp, 0.0).r;");
			node_shader_write_frag(shader, "output[0] = vec4(texpaint_val, texpaint_val, texpaint_val, 1.0);");
			node_shader_write_frag(shader, "output[1].rg = tex_coord_inp.xy;");
		}
		else {
			shader->frag_out = "vec4[4]";
			node_shader_add_texture(shader, "texpaint", NULL);
			node_shader_add_texture(shader, "texpaint_nor", NULL);
			node_shader_add_texture(shader, "texpaint_pack", NULL);
			node_shader_write_frag(shader, "output[0] = textureLod(sampler2D(texpaint, sampler_linear), tex_coord_inp, 0.0);");
			node_shader_write_frag(shader, "output[1] = textureLod(sampler2D(texpaint_nor, sampler_linear), tex_coord_inp, 0.0);");
			node_shader_write_frag(shader, "output[2] = textureLod(sampler2D(texpaint_pack, sampler_linear), tex_coord_inp, 0.0);");
			node_shader_write_frag(shader, "output[3].rg = tex_coord_inp.xy;");
		}
	}
	else if (g_context->tool == TOOL_TYPE_CURSOR) {
		shader->frag_out = "vec4";
		node_shader_add_texture(shader, "gbuffer1", NULL);
		node_shader_add_constant(shader, "vec2 gbuffer_size", "_gbuffer_size");
		node_shader_add_constant(shader, "vec4 inp", "_input_brush");
		node_shader_write_frag(shader,
		                       "uvec2 inp_co = uvec2(uint(constants.inp.x * constants.gbuffer_size.x), uint(constants.inp.y * constants.gbuffer_size.y));");
		node_shader_write_frag(shader, "output = gbuffer1[inp_co];");
	}
}
