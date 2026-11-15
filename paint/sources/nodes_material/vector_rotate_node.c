
#include "../global.h"

char *vector_rotate_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *vec    = parser_material_parse_vector_input(node->inputs->buffer[0]);
	char *center = parser_material_parse_vector_input(node->inputs->buffer[1]);
	char *axis   = parser_material_parse_vector_input(node->inputs->buffer[2]);
	char *angle  = parser_material_parse_value_input(node->inputs->buffer[3], false);
	bool  invert = node->buttons->buffer[0]->default_value->buffer[0] > 0;
	char *name   = parser_material_store_var_name(node);
	char *v      = string_tmp("%s_v", name);
	char *ax     = string_tmp("%s_ax", name);
	char *cosA   = string_tmp("%s_cosA", name);
	char *sinA   = string_tmp("%s_sinA", name);
	parser_material_write(parser_material_shader, string_tmp("vec3 %s = %s - %s;", v, vec, center));
	parser_material_write(parser_material_shader, string_tmp("vec3 %s = normalize(%s);", ax, axis));
	parser_material_write(parser_material_shader, string_tmp("float %s = cos(%s%s * (3.14159265 / 180.0));", cosA, invert ? "-" : "", angle));
	parser_material_write(parser_material_shader, string_tmp("float %s = sin(%s%s * (3.14159265 / 180.0));", sinA, invert ? "-" : "", angle));
	return string_tmp("(%s * %s + cross(%s, %s) * %s + %s * dot(%s, %s) * (1.0 - %s) + %s)", v, cosA, ax, v, sinA, ax, ax, v, cosA, center);
}

void vector_rotate_node_init() {

	ui_node_t *vector_rotate_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Vector Rotate"),
	                           .type   = "VECT_ROTATE",
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff62676d,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_ex(_tr("Vector"), "VECTOR", 0xff6363c7, f32_array_create_xyz(0.0, 0.0, 0.0), 0.0, 1.0, 100, 1),
	                                   nodes_material_socket_ex(_tr("Center"), "VECTOR", 0xff6363c7, f32_array_create_xyz(0.0, 0.0, 0.0), 0.0, 1.0, 100, 1),
	                                   nodes_material_socket_ex(_tr("Axis"), "VECTOR", 0xff6363c7, f32_array_create_xyz(0.0, 0.0, 1.0), 0.0, 1.0, 100, 1),
	                                   nodes_material_socket_ex(_tr("Angle"), "VALUE", 0xffa1a1a1, f32_array_create_x(0.0), 0.0, 360.0, 10, 0),
	                               },
	                               4),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("Vector"), "VECTOR", f32_array_create_xyz(0.0, 0.0, 0.0), 0.0, 1.0),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t,
	                                              {.name = _tr("Invert"), .type = "BOOL", .output = 0, .default_value = f32_array_create_x(0), .height = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_utilities, vector_rotate_node_def);
	any_map_set(parser_material_node_vectors, "VECT_ROTATE", vector_rotate_node_vector);
}
