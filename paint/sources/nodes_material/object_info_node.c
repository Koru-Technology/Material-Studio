
#include "../global.h"

char *object_info_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[0]) { // Location
		parser_material_shader->frag_wposition = true;
		return "input.wposition";
	}
	return "";
}

char *object_info_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[1]) { // Object Index
		node_shader_add_constant(parser_material_shader, "float object_info_index", "_object_info_index");
		return "constants.object_info_index";
	}
	else if (socket == node->outputs->buffer[2]) { // Material Index
		node_shader_add_constant(parser_material_shader, "float object_info_material_index", "_object_info_material_index");
		return "constants.object_info_material_index";
	}
	else { // Random
		node_shader_add_constant(parser_material_shader, "float object_info_random", "_object_info_random");
		return "constants.object_info_random";
	}
}

void object_info_node_init() {

	ui_node_t *object_info_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                           .name    = _tr("Object Info"),
	                           .type    = "OBJECT_INFO",
	                           .x       = 0,
	                           .y       = 0,
	                           .color   = 0xffb34f5a,
	                           .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("Location"), "VECTOR", f32_array_create_xyz(0.0, 0.0, 0.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Object Index"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Material Index"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Random"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                               },
	                               4),
	                           .buttons = any_array_create_from_raw((void *[]){}, 0),
	                           .width   = 0,
	                           .flags   = 0});

	any_array_push(nodes_material_input, object_info_node_def);
	any_map_set(parser_material_node_vectors, "OBJECT_INFO", object_info_node_vector);
	any_map_set(parser_material_node_values, "OBJECT_INFO", object_info_node_value);
}
