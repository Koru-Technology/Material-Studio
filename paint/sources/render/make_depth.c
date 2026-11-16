
#include "../global.h"

node_shader_context_t *make_depth_run(material_t *data) {
	shader_context_t      *props     = ALLOC_INIT(shader_context_t, {.name            = "depth",
	                                                                 .depth_write     = true,
	                                                                 .compare_mode    = "less",
	                                                                 .cull_mode       = g_context->cull_backfaces ? "clockwise" : "none",
	                                                                 .vertex_elements = node_shader_mesh_elements(),
	                                                                 .color_attachments = any_array_create_from_raw(
                                                                (void *[]){
                                                                    "RGBA64",
                                                                },
                                                                1),
	                                                                 .depth_attachment = "D32"});
	node_shader_context_t *con_depth = node_shader_context_create(data, props);

	node_shader_t *shader  = node_shader_context_make_shader(con_depth);
	shader->frag_wposition = true;

	node_shader_add_constant(shader, "mat4 VP", "_view_proj_matrix");

	node_shader_write_vert(shader, "output.pos = constants.VP * vec4(output.wposition.xyz, 1.0);");

	// The uv coordinates under each pixel, as gbuffer2 has them (half floats: a coarse part and a residual): clone and
	// smudge read the layer where the brush's screen points are, from this camera (render_path_paint_uv_target)
	node_shader_add_out(shader, "vec2 paint_uv");
	node_shader_write_vert(shader, "output.paint_uv = input.tex;");
	node_shader_write_frag(shader, "vec2 paint_uv_hi = floor(input.paint_uv * 256.0) / 256.0;");
	node_shader_write_frag(shader, "output = vec4(input.paint_uv - paint_uv_hi, paint_uv_hi);");

	con_depth->data->color_writes_red = u8_array_create_from_raw(
	    (u8[]){
	        true,
	    },
	    1);

	con_depth->data->color_writes_green = u8_array_create_from_raw(
	    (u8[]){
	        true,
	    },
	    1);

	con_depth->data->color_writes_blue = u8_array_create_from_raw(
	    (u8[]){
	        true,
	    },
	    1);

	con_depth->data->color_writes_alpha = u8_array_create_from_raw(
	    (u8[]){
	        true,
	    },
	    1);

	parser_material_finalize(con_depth);

	con_depth->data->shader_from_source = true;
	gpu_create_shaders_from_glsl(node_shader_get(shader), &con_depth->data->vertex_shader, &con_depth->data->fragment_shader,
	                             &con_depth->data->_->vertex_shader_size, &con_depth->data->_->fragment_shader_size);
	return con_depth;
}
