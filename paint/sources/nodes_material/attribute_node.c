
#include "../global.h"

char *attribute_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[0]) { // Color
		if (parser_material_shader->context->allow_vcols) {
			node_shader_context_add_elem(parser_material_shader->context, "col", "short4norm"); // Vcols only for now
			return "input.vcolor";
		}
		else {
			return ("vec3(0.0, 0.0, 0.0)");
		}
	}
	else {                                                                                // Vector
		node_shader_context_add_elem(parser_material_shader->context, "tex", "short2norm"); // UVMaps only for now
		return "vec3(tex_coord.x, tex_coord.y, 0.0)";
	}
}

char *attribute_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_constant(parser_material_shader, "float time", "_time");
	return "constants.time";
}

void attribute_node_init() {

	ui_node_t *attribute_node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                                       .name    = _tr("Attribute"),
	                                                       .type    = "ATTRIBUTE",
	                                                       .x       = 0,
	                                                       .y       = 0,
	                                                       .color   = 0xffb34f5a,
	                                                       .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                                                       .outputs = any_array_create_from_raw(
	                                                           (void *[]){
	                                                               nodes_material_socket_def(_tr("Color"), "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0),
	                                                               nodes_material_socket_def(_tr("Vector"), "VECTOR", f32_array_create_xyz(0.0, 0.0, 0.0), 0.0, 1.0),
	                                                               nodes_material_socket_def(_tr("Factor"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                                               nodes_material_socket_def(_tr("Alpha"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                                           },
	                                                           4),
	                                                       .buttons = any_array_create_from_raw(
	                                                           (void *[]){
	                                                               ALLOC_INIT(ui_node_button_t, {.name          = _tr("Name"),
	                                                                                             .type          = "STRING",
	                                                                                             .output        = -1,
	                                                                                             .default_value = f32_array_create_x(0),
	                                                                                             .data          = NULL,
	                                                                                             .min           = 0.0,
	                                                                                             .max           = 1.0,
	                                                                                             .precision     = 100,
	                                                                                             .height        = 0}),
	                                                           },
	                                                           1),
	                                                       .width = 0,
	                                                       .flags = 0});

	any_array_push(nodes_material_input, attribute_node_def);
	any_map_set(parser_material_node_vectors, "ATTRIBUTE", attribute_node_vector);
	any_map_set(parser_material_node_values, "ATTRIBUTE", attribute_node_value);
}
