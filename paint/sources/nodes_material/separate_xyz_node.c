
#include "../global.h"

char *separate_xyz_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *vec = parser_material_parse_vector_input(node->inputs->buffer[0]);
	if (socket == node->outputs->buffer[0]) {
		return string_tmp("%s.x", vec);
	}
	else if (socket == node->outputs->buffer[1]) {
		return string_tmp("%s.y", vec);
	}
	else {
		return string_tmp("%s.z", vec);
	}
}

void separate_xyz_node_init() {

	ui_node_t *separate_xyz_node_def = ALLOC_INIT(ui_node_t, {.id     = 0,
	                                                          .name   = _tr("Separate XYZ"),
	                                                          .type   = "SEPXYZ",
	                                                          .x      = 0,
	                                                          .y      = 0,
	                                                          .color  = 0xff62676d,
	                                                          .inputs = any_array_create_from_raw(
	                                                              (void *[]){
	                                                                  nodes_material_socket_def(_tr("Vector"), "VECTOR", f32_array_create_xyz(0.0, 0.0, 0.0), 0.0, 1.0),
	                                                              },
	                                                              1),
	                                                          .outputs = any_array_create_from_raw(
	                                                              (void *[]){
	                                                                  nodes_material_socket_def(_tr("X"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                                                  nodes_material_socket_def(_tr("Y"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                                                  nodes_material_socket_def(_tr("Z"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                                              },
	                                                              3),
	                                                          .buttons = any_array_create_from_raw((void *[]){}, 0),
	                                                          .width   = 0,
	                                                          .flags   = 0});

	any_array_push(nodes_material_utilities, separate_xyz_node_def);
	any_map_set(parser_material_node_values, "SEPXYZ", separate_xyz_node_value);
}
