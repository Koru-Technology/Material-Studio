
#include "../global.h"

// The input of the material's output node read by the output of the node, NULL when there is none (material deleted, no
// output node). The outputs are the output node's inputs, in the same order.
static ui_node_socket_t *material_node_source(ui_node_t *node, ui_node_socket_t *socket) {
	i32 mi    = node->buttons->length > 0 && node->buttons->buffer[0]->default_value != NULL ? node->buttons->buffer[0]->default_value->buffer[0] : -1;
	i32 index = parser_material_socket_index(node, socket);
	if (mi < 0 || mi >= g_project->_->materials->length || index < 0) {
		return NULL;
	}
	slot_material_t *m = g_project->_->materials->buffer[mi];
	if (parser_material_is_open(m->canvas)) { // The material itself, or a material it is in
		return NULL;
	}
	ui_node_t *output_node = parser_material_node_by_type(m->canvas->nodes, "OUTPUT_MATERIAL_PBR");
	return output_node != NULL && index < output_node->inputs->length ? output_node->inputs->buffer[index] : NULL;
}

// Entered like a group: a group inside the material pops back to this canvas, not to the one holding the node
static void material_node_enter(ui_node_t *node) {
	slot_material_t *m = g_project->_->materials->buffer[(i32)node->buttons->buffer[0]->default_value->buffer[0]];
	parser_material_push_group(m->canvas);
	any_array_push(parser_material_parents, node);
}

static void material_node_leave() {
	parser_material_pop_group();
	array_pop(parser_material_parents);
}

char *material_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	ui_node_socket_t *inp = material_node_source(node, socket);
	i32               i   = parser_material_socket_index(node, socket);
	if (inp == NULL || (i != 0 && i != 5)) { // Base, Normal
		return "vec3(0.0, 0.0, 0.0)";
	}
	material_node_enter(node);
	char *result = string_copy(parser_material_parse_vector_input(inp));
	material_node_leave();
	return result;
}

char *material_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	ui_node_socket_t *inp = material_node_source(node, socket);
	i32               i   = parser_material_socket_index(node, socket);
	if (inp == NULL || i < 1 || i == 5 || i == 6 || i > 7) { // Opacity, Occlusion, Roughness, Metallic, Height
		return "0.0";
	}
	material_node_enter(node);
	char *result = string_copy(parser_material_parse_value_input(inp, false));
	material_node_leave();
	return result;
}

void material_node_init() {

	ui_node_t *material_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                           .name    = _tr("Material"),
	                           .type    = "MATERIAL", // extension
	                           .x       = 0,
	                           .y       = 0,
	                           .color   = 0xff4982a0,
	                           .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   nodes_material_socket_def(_tr("Base Color"), "RGBA", f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Opacity"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Occlusion"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Roughness"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Metallic"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                                   nodes_material_socket_ex(_tr("Normal Map"), "VECTOR", -10238109, f32_array_create_xyz(0.5, 0.5, 1.0), 0.0, 1.0, 100, 0),
	                                   nodes_material_socket_def(_tr("Emission"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Height"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                                   nodes_material_socket_def(_tr("Subsurface"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                               },
	                               9),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Material"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string(""),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_input, material_node_def);
	any_map_set(parser_material_node_vectors, "MATERIAL", material_node_vector);
	any_map_set(parser_material_node_values, "MATERIAL", material_node_value);
}
