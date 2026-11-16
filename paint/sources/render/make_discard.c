
#include "../global.h"

void make_discard_material_id(node_shader_t *shader, char *tex_coord) {
	char *tc;
	if (tex_coord == NULL) {
		shader->frag_wvpposition = true;
		node_shader_write_frag(
		    shader, "vec2 picker_sample_tc = vec2(input.wvpposition.x / input.wvpposition.w, input.wvpposition.y / input.wvpposition.w) * 0.5 + 0.5;");
		node_shader_write_frag(shader, "picker_sample_tc.y = 1.0 - picker_sample_tc.y;");
		tc = "picker_sample_tc";
	}
	else {
		tc = string_tmp("input.%s", tex_coord);
	}
	node_shader_add_texture(shader, "texpaint_nor_undo", "_texpaint_nor_undo");
	node_shader_write_frag(shader, string_tmp("float picker_sample_a = textureLod(sampler2D(texpaint_nor_undo, sampler_linear), %s, 0.0).a;", tc));
	// material_id * 3 + (0 - normal, 1 - emission, 2 - subsurface)
	node_shader_write_frag(shader, "float picker_sample_id = floor(floor(picker_sample_a * 255.0 + 0.5) / 3.0);");
	node_shader_write_frag(shader, string_tmp("if (abs(picker_sample_id - %s) > 0.5) { discard; }", f32_to_string_with_zeros((f32)g_context->materialid_picked)));
}

void make_discard_picked_color(node_shader_t *shader, char *tex_src, char *tex_src_link) {
	shader->frag_wvpposition = true;
	node_shader_add_constant(shader, "vec3 picker_base", "_picker_base");
	node_shader_add_constant(shader, "float picker_color_mask_tolerance", "_picker_color_mask_tolerance");
	node_shader_add_texture(shader, tex_src, tex_src_link);
	node_shader_write_frag(
	    shader, "vec2 picker_color_tc = vec2(input.wvpposition.x / input.wvpposition.w, input.wvpposition.y / input.wvpposition.w) * 0.5 + 0.5;");
	node_shader_write_frag(shader, "picker_color_tc.y = 1.0 - picker_color_tc.y;");
	node_shader_write_frag(shader, string_tmp("vec3 picker_sample_col = textureLod(sampler2D(%s, sampler_linear), picker_color_tc, 0.0).rgb;", tex_src));
	node_shader_write_frag(shader, "if (abs(picker_sample_col.r - constants.picker_base.r) > constants.picker_color_mask_tolerance || "
	                             "abs(picker_sample_col.g - constants.picker_base.g) > constants.picker_color_mask_tolerance || "
	                             "abs(picker_sample_col.b - constants.picker_base.b) > constants.picker_color_mask_tolerance) { discard; }");
}
