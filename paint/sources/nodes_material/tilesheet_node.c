
#include "../global.h"

char *tilesheet_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_context_add_elem(parser_material_shader->context, "tex", "short2norm");
	char *tile_x  = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char *tile_y  = parser_material_parse_value_input(node->inputs->buffer[1], false);
	i32   tiles_x = (i32)node->buttons->buffer[0]->default_value->buffer[0];
	i32   tiles_y = (i32)node->buttons->buffer[1]->default_value->buffer[0];
	return string_tmp("vec3((tex_coord.x + %s) / %d.0, (tex_coord.y + %s) / %d.0, 0.0)", tile_x, tiles_x, tile_y, tiles_y);
}

void tilesheet_node_init() {

	ui_node_t *tilesheet_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Tilesheet"),
	                           .type   = "TILESHEET",
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xffb34f5a,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_ex(_tr("Tile X"), "VALUE", 0xffa1a1a1, f32_array_create_x(0.0), 0.0, 63.0, 1, 0),
	                                   nodes_material_socket_ex(_tr("Tile Y"), "VALUE", 0xffa1a1a1, f32_array_create_x(0.0), 0.0, 63.0, 1, 0),
	                               },
	                               2),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("UV"), "VECTOR", f32_array_create_xyz(0.0, 0.0, 0.0), 0.0, 1.0),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Tiles X"),
	                                                                 .type          = "VALUE",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(4),
	                                                                 .data          = NULL,
	                                                                 .min           = 1.0,
	                                                                 .max           = 64.0,
	                                                                 .precision     = 1,
	                                                                 .height        = 0}),
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Tiles Y"),
	                                                                 .type          = "VALUE",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(4),
	                                                                 .data          = NULL,
	                                                                 .min           = 1.0,
	                                                                 .max           = 64.0,
	                                                                 .precision     = 1,
	                                                                 .height        = 0}),
	                               },
	                               2),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_input, tilesheet_node_def);
	any_map_set(parser_material_node_vectors, "TILESHEET", tilesheet_node_vector);
}
