
#include "../global.h"

char *combine_xyz_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *x = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char *y = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *z = parser_material_parse_value_input(node->inputs->buffer[2], false);
	return string_tmp("vec3(%s, %s, %s)", x, y, z);
}

void combine_xyz_node_init() {

	ui_node_t *combine_xyz_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Combine XYZ"),
	                           .type   = "COMBXYZ",
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff62676d,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("X"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Y"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Z"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                               },
	                               3),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("Vector"), "VECTOR", f32_array_create_xyz(0.0, 0.0, 0.0), 0.0, 1.0),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw((void *[]){}, 0),
	                           .width   = 0,
	                           .flags   = 0});

	any_array_push(nodes_material_utilities, combine_xyz_node_def);
	any_map_set(parser_material_node_vectors, "COMBXYZ", combine_xyz_node_vector);
}
