
#include "../global.h"

char *color_mask_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *input_color = parser_material_parse_vector_input(node->inputs->buffer[0]);
	char *mask_color  = parser_material_parse_vector_input(node->inputs->buffer[1]);
	char *radius      = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char *fuzziness   = parser_material_parse_value_input(node->inputs->buffer[3], false);
	return string_tmp("clamp(1.0 - (distance(%s, %s) - %s) / max(%s, %s), 0.0, 1.0)", input_color, mask_color, radius, fuzziness,
	                  f32_to_string(parser_material_eps));
}

void color_mask_node_init() {

	ui_node_t *color_mask_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Color Mask"),
	                           .type   = "COLMASK", // extension
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff62676d,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("Color"), "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Mask Color"), "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Radius"), "VALUE", f32_array_create_x(0.1), 0.0, 1.74),
	                                   nodes_material_socket_def(_tr("Fuzziness"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                               },
	                               4),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("Mask"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw((void *[]){}, 0),
	                           .width   = 0,
	                           .flags   = 0});

	any_array_push(nodes_material_color, color_mask_node_def);
	any_map_set(parser_material_node_values, "COLMASK", color_mask_node_value);
}
