
#include "../global.h"

char *hue_saturation_value_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_add_function(parser_material_shader, str_hue_sat);
	char *hue = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char *sat = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *val = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char *fac = parser_material_parse_value_input(node->inputs->buffer[3], false);
	char *col = parser_material_parse_vector_input(node->inputs->buffer[4]);
	return string_tmp("hue_sat(%s, vec4(%s - 0.5, %s, %s, 1.0 - %s))", col, hue, sat, val, fac);
}

void hue_saturation_value_node_init() {

	ui_node_t *hue_saturation_value_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Hue/Saturation/Value"),
	                           .type   = "HUE_SAT",
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff448c6d,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("Hue"), "VALUE", f32_array_create_x(0.5), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Saturation"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Value"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Factor"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Color"), "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0),
	                               },
	                               5),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("Color"), "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw((void *[]){}, 0),
	                           .width   = 0,
	                           .flags   = 0});

	any_array_push(nodes_material_color, hue_saturation_value_node_def);
	any_map_set(parser_material_node_vectors, "HUE_SAT", hue_saturation_value_node_vector);
}
