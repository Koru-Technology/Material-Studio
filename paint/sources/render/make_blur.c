
#include "../global.h"

char *str_get_smudge_tool_weight = "\
float get_smudge_tool_weight(int i) { \
	if (i == 0) { return 1.0 / 28.0; } \
	if (i == 1) { return 2.0 / 28.0; } \
	if (i == 2) { return 3.0 / 28.0; } \
	if (i == 3) { return 4.0 / 28.0; } \
	if (i == 4) { return 5.0 / 28.0; } \
	if (i == 5) { return 6.0 / 28.0; } \
	return 7.0 / 28.0; \
} \
";

char *str_get_blur_tool_weight = "\
float get_blur_tool_weight(int i) { \
	if (i == 0) { return 0.034619 / 2.0; } \
	if (i == 1) { return 0.044859 / 2.0; } \
	if (i == 2) { return 0.055857 / 2.0; } \
	if (i == 3) { return 0.066833 / 2.0; } \
	if (i == 4) { return 0.076841 / 2.0; } \
	if (i == 5) { return 0.084894 / 2.0; } \
	if (i == 6) { return 0.090126 / 2.0; } \
	if (i == 7) { return 0.09194 / 2.0; } \
	if (i == 8) { return 0.090126 / 2.0; } \
	if (i == 9) { return 0.084894 / 2.0; } \
	if (i == 10) { return 0.076841 / 2.0; } \
	if (i == 11) { return 0.066833 / 2.0; } \
	if (i == 12) { return 0.055857 / 2.0; } \
	if (i == 13) { return 0.044859 / 2.0; } \
	return 0.034619 / 2.0; \
} \
";

void make_blur_run(node_shader_t *shader) {
	node_shader_write_frag(shader, "vec3 basecol = vec3(0.0, 0.0, 0.0);");
	node_shader_write_frag(shader, "float roughness = 0.0;");
	node_shader_write_frag(shader, "float metallic = 0.0;");
	node_shader_write_frag(shader, "float occlusion = 0.0;");
	node_shader_write_frag(shader, "vec3 nortan = vec3(0.0, 0.0, 0.0);");
	node_shader_write_frag(shader, "float height = 0.0;");
	node_shader_write_frag(shader, "float mat_opacity = 1.0;");
	bool is_mask = slot_layer_is_mask(g_context->layer);
	if (is_mask) {
		node_shader_write_frag(shader, "float opacity = 1.0;");
	}
	else {
		node_shader_write_frag(shader, "float opacity = 0.0;");
	}
	if (g_context->material->paint_emis) {
		node_shader_write_frag(shader, "float emis = 0.0;");
	}
	if (g_context->material->paint_subs) {
		node_shader_write_frag(shader, "float subs = 0.0;");
	}

	if (g_context->blur_type == BLUR_TYPE_SMUDGE) {
		node_shader_add_function(shader, str_get_smudge_tool_weight);
		node_shader_write_frag(shader, "vec2 smudge_delta = constants.inp.xy - constants.inplast.xy;");
		node_shader_write_frag(shader, "for (int i = 0; i < 7; i += 1) {");
		node_shader_write_frag(shader, "vec2 smudge_sp = sp.xy - smudge_delta * ((float(i) + 1.0) / 7.0);");
		node_shader_write_frag(shader, "vec2 tex_coord_inp2 = input.blur_uv;");
		node_shader_write_frag(shader, "if (smudge_sp.x >= 0.0 && smudge_sp.y >= 0.0 && smudge_sp.x < 1.0 && smudge_sp.y < 1.0) {");
		node_shader_write_frag(shader, "vec4 tex_coord_inp24 = texelFetch(sampler2D(gbuffer2, sampler_linear), ivec2(uint(smudge_sp.x * constants.gbuffer_size.x), "
		                             "uint(smudge_sp.y * constants.gbuffer_size.y)), 0);");
		node_shader_write_frag(shader, "tex_coord_inp2 = tex_coord_inp24.ba + tex_coord_inp24.rg;");
		node_shader_write_frag(shader, "}");
		node_shader_write_frag(shader, "vec4 texpaint_sample = texture(sampler2D(texpaint_ref, sampler_linear), tex_coord_inp2);");
		node_shader_write_frag(shader, "float blur_weight_i = get_smudge_tool_weight(i);");
		if (is_mask) {
			node_shader_write_frag(shader, "basecol += texpaint_sample.rgb * blur_weight_i;");
		}
		else {
			node_shader_write_frag(shader, "opacity += texpaint_sample.a * blur_weight_i;");
			node_shader_write_frag(shader, "basecol += texpaint_sample.rgb * texpaint_sample.a * blur_weight_i;");
		}
		node_shader_write_frag(shader, "vec4 texpaint_pack_sample = texture(sampler2D(texpaint_pack_ref, sampler_linear), tex_coord_inp2) * blur_weight_i;");
		node_shader_write_frag(shader, "roughness += texpaint_pack_sample.g;");
		node_shader_write_frag(shader, "metallic += texpaint_pack_sample.b;");
		node_shader_write_frag(shader, "occlusion += texpaint_pack_sample.r;");
		node_shader_write_frag(shader, "height += texpaint_pack_sample.a;");
		node_shader_write_frag(shader, "nortan += texture(sampler2D(texpaint_nor_ref, sampler_linear), tex_coord_inp2).rgb * blur_weight_i;");
		node_shader_write_frag(shader, "}");
	}
	else {
		node_shader_write_frag(shader, "vec2 tex_coord_inp = input.blur_uv;");
		node_shader_add_constant(shader, "vec2 texpaint_size", "_texpaint_size");
		node_shader_write_frag(shader, "float blur_step = 1.0 / constants.texpaint_size.x;");
		node_shader_add_function(shader, str_get_blur_tool_weight);
		// X
		node_shader_write_frag(shader, "for (int i = 0; i <= 14; i += 1) {");
		node_shader_write_frag(shader, "vec4 texpaint_sample = texture(sampler2D(texpaint_ref, sampler_linear), tex_coord_inp + vec2(blur_step * float(i - 7), 0.0));");
		node_shader_write_frag(shader, "float blur_weight_i = get_blur_tool_weight(i);");
		if (is_mask) {
			node_shader_write_frag(shader, "basecol += texpaint_sample.rgb * blur_weight_i;");
		}
		else {
			node_shader_write_frag(shader, "opacity += texpaint_sample.a * blur_weight_i;");
			node_shader_write_frag(shader, "basecol += texpaint_sample.rgb * texpaint_sample.a * blur_weight_i;");
		}
		node_shader_write_frag(shader, "vec4 texpaint_pack_sample = texture(sampler2D(texpaint_pack_ref, sampler_linear), tex_coord_inp + vec2(blur_step * "
		                             "float(i - 7), 0.0)) * blur_weight_i;");
		node_shader_write_frag(shader, "roughness += texpaint_pack_sample.g;");
		node_shader_write_frag(shader, "metallic += texpaint_pack_sample.b;");
		node_shader_write_frag(shader, "occlusion += texpaint_pack_sample.r;");
		node_shader_write_frag(shader, "height += texpaint_pack_sample.a;");
		node_shader_write_frag(
		    shader, "nortan += texture(sampler2D(texpaint_nor_ref, sampler_linear), tex_coord_inp + vec2(blur_step * float(i - 7), 0.0)).rgb * blur_weight_i;");
		node_shader_write_frag(shader, "}");
		// Y
		node_shader_write_frag(shader, "for (int j = 0; j <= 14; j += 1) {");
		node_shader_write_frag(shader, "vec4 texpaint_sample = texture(sampler2D(texpaint_ref, sampler_linear), tex_coord_inp + vec2(0.0, blur_step * float(j - 7)));");
		node_shader_write_frag(shader, "float blur_weight_j = get_blur_tool_weight(j);");
		if (is_mask) {
			node_shader_write_frag(shader, "basecol += texpaint_sample.rgb * blur_weight_j;");
		}
		else {
			node_shader_write_frag(shader, "opacity += texpaint_sample.a * blur_weight_j;");
			node_shader_write_frag(shader, "basecol += texpaint_sample.rgb * texpaint_sample.a * blur_weight_j;");
		}
		node_shader_write_frag(shader, "vec4 texpaint_pack_sample = texture(sampler2D(texpaint_pack_ref, sampler_linear), tex_coord_inp + vec2(0.0, blur_step * "
		                             "float(j - 7))) * blur_weight_j;");
		node_shader_write_frag(shader, "roughness += texpaint_pack_sample.g;");
		node_shader_write_frag(shader, "metallic += texpaint_pack_sample.b;");
		node_shader_write_frag(shader, "occlusion += texpaint_pack_sample.r;");
		node_shader_write_frag(shader, "height += texpaint_pack_sample.a;");
		node_shader_write_frag(
		    shader, "nortan += texture(sampler2D(texpaint_nor_ref, sampler_linear), tex_coord_inp + vec2(0.0, blur_step * float(j - 7))).rgb * blur_weight_j;");
		node_shader_write_frag(shader, "}");
	}
	node_shader_write_frag(shader, "float blur_src_alpha = opacity;");
	node_shader_write_frag(shader, "opacity *= constants.brush_opacity;");
}
