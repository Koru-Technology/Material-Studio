
#include "../global.h"

char *quantize_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *strength = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char *col      = parser_material_parse_vector_input(node->inputs->buffer[1]);
	return string_tmp("(floor(100.0 * %s * %s) / (100.0 * %s))", strength, col, strength);
}

void quantize_node_init() {

	ui_node_t *quantize_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Quantize"),
	                           .type   = "QUANTIZE", // extension
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff448c6d,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("Strength"), "VALUE", f32_array_create_x(0.1), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Color"), "RGBA", f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0), 0.0, 1.0),
	                               },
	                               2),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("Color"), "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw((void *[]){}, 0),
	                           .width   = 0,
	                           .flags   = 0});

	any_array_push(nodes_material_color, quantize_node_def);
	any_map_set(parser_material_node_vectors, "QUANTIZE", quantize_node_vector);
}
