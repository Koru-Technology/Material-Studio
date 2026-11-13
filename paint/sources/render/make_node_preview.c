
#include "../global.h"

node_shader_context_t *make_node_preview_run(material_t *data, ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	char                  *context_id = "mesh";
	shader_context_t      *props      = ALLOC_INIT(shader_context_t, {.name            = context_id,
	                                                                  .depth_write     = false,
	                                                                  .compare_mode    = "always",
	                                                                  .cull_mode       = "clockwise",
	                                                                  .vertex_elements = any_array_create_from_raw(
                                                                (void *[]){
                                                                    ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "short4norm"}),
                                                                    ALLOC_INIT(vertex_element_t, {.name = "nor", .data = "short2norm"}),
                                                                    ALLOC_INIT(vertex_element_t, {.name = "tex", .data = "short2norm"}),
                                                                    ALLOC_INIT(vertex_element_t, {.name = "col", .data = "short4norm"}),
                                                                },
                                                                4),
	                                                                  .color_attachments = any_array_create_from_raw(
                                                                (void *[]){
                                                                    "RGBA32",
                                                                },
                                                                1)});
	node_shader_context_t *con_mesh   = node_shader_context_create(data, props);

	con_mesh->allow_vcols = true;

	node_shader_t *shader = node_shader_context_make_shader(con_mesh);

	node_shader_write_attrib_vert(shader, "output.pos = vec4(input.pos.xy * 3.0, 0.0, 1.0);");
	node_shader_write_attrib_vert(shader, "vec2 madd = vec2(0.5, 0.5);");
	node_shader_add_out(shader, "vec2 tex_coord");
	node_shader_write_attrib_vert(shader, "output.tex_coord = output.pos.xy * madd + madd;");
	node_shader_write_attrib_vert(shader, "output.tex_coord.y = 1.0 - output.tex_coord.y;");
	node_shader_write_attrib_frag(shader, "vec2 tex_coord = input.tex_coord;");

	parser_material_init();
	parser_material_canvases = any_array_create_from_raw(
	    (void *[]){
	        g_context->material->canvas,
	    },
	    1);
	parser_material_nodes = g_context->material->canvas->nodes;
	parser_material_links = g_context->material->canvas->links;
	if (group != NULL) {
		parser_material_push_group(group);
		parser_material_parents = parents;
	}
	ui_node_link_t_array_t *links          = parser_material_links;
	i32                     socket_preview = i32_imap_get(g_context->node_preview_socket_map, node->id);
	if (parser_material_blur_passthrough && string_equals(node->type, "BLUR")) {
		socket_preview = 0; // Blur texture holds the color, opacity goes to alpha
	}
	ui_node_link_t *link =
	    ALLOC_INIT(ui_node_link_t,
	               {.id = ui_next_link_id(links), .from_id = node->id, .from_socket = socket_preview == -1 ? 0 : socket_preview, .to_id = -1, .to_socket = -1});
	any_array_push(links, link);

	parser_material_con  = con_mesh;
	parser_material_shader = shader;

	parser_material_transform_color_space = false;
	char *res                             = parser_material_write_result(link);
	parser_material_transform_color_space = true;
	char *st                              = node->outputs->buffer[link->from_socket]->type;
	if (!string_equals(st, "RGB") && !string_equals(st, "RGBA") && !string_equals(st, "VECTOR")) {
		res = string_copy(parser_material_to_vec3(res));
	}
	array_remove(links, link);

	shader->frag_out = "vec4";
	node_shader_write_frag(shader, string_tmp("vec3 basecol = %s;", res));
	if (parser_material_blur_passthrough && string_equals(node->type, "BLUR")) {
		// Texture sampled by the blur node, premultiplied by the input alpha
		node_shader_write_frag(shader, string_tmp("float basecol_a = %s;", blur_node_input_alpha(node)));
		node_shader_write_frag(shader, "output = vec4(basecol.rgb * basecol_a, basecol_a);");
	}
	else {
		node_shader_write_frag(shader, "output = vec4(basecol.rgb, 1.0);");
	}

	parser_material_finalize(con_mesh);

	con_mesh->data->shader_from_source = true;
	gpu_create_shaders_from_glsl(node_shader_get(shader), &con_mesh->data->vertex_shader, &con_mesh->data->fragment_shader,
	                             &con_mesh->data->_->vertex_shader_size, &con_mesh->data->_->fragment_shader_size);
	return con_mesh;
}
