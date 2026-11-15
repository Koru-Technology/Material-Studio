
#include "../global.h"

char *layer_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	i32   l  = node->buttons->buffer[0]->default_value->buffer[0];
	char *ls = i32_to_string(l);
	if (socket == node->outputs->buffer[0]) { // Base
		node_shader_add_texture(parser_material_shader, string_tmp("texpaint%s", ls), string_tmp("_texpaint%s", ls));
		return string_tmp("texture(sampler2D(texpaint%s, sampler_linear), tex_coord).rgb", ls);
	}
	else { // Normal
		node_shader_add_texture(parser_material_shader, string_tmp("texpaint_nor%s", ls), string_tmp("_texpaint_nor%s", ls));
		return string_tmp("texture(sampler2D(texpaint_nor%s, sampler_linear), tex_coord).rgb", ls);
	}
}

char *layer_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	i32   l  = node->buttons->buffer[0]->default_value->buffer[0];
	char *ls = i32_to_string(l);
	if (socket == node->outputs->buffer[1]) { // Opac
		node_shader_add_texture(parser_material_shader, string_tmp("texpaint%s", ls), string_tmp("_texpaint%s", ls));
		return string_tmp("texture(sampler2D(texpaint%s, sampler_linear), tex_coord).a", ls);
	}
	else if (socket == node->outputs->buffer[2]) { // Occ
		node_shader_add_texture(parser_material_shader, string_tmp("texpaint_pack%s", ls), string_tmp("_texpaint_pack%s", ls));
		return string_tmp("texture(sampler2D(texpaint_pack%s, sampler_linear), tex_coord).r", ls);
	}
	else if (socket == node->outputs->buffer[3]) { // Rough
		node_shader_add_texture(parser_material_shader, string_tmp("texpaint_pack%s", ls), string_tmp("_texpaint_pack%s", ls));
		return string_tmp("texture(sampler2D(texpaint_pack%s, sampler_linear), tex_coord).g", ls);
	}
	else if (socket == node->outputs->buffer[4]) { // Metal
		node_shader_add_texture(parser_material_shader, string_tmp("texpaint_pack%s", ls), string_tmp("_texpaint_pack%s", ls));
		return string_tmp("texture(sampler2D(texpaint_pack%s, sampler_linear), tex_coord).b", ls);
	}
	else if (socket == node->outputs->buffer[6]) {
		return "0.0";
	} // Emission
	else if (socket == node->outputs->buffer[7]) { // Height
		node_shader_add_texture(parser_material_shader, string_tmp("texpaint_pack%s", ls), string_tmp("_texpaint_pack%s", ls));
		return string_tmp("texture(sampler2D(texpaint_pack%s, sampler_linear), tex_coord).a", ls);
	}
	else {
		return "0.0";
	} // Subsurface
}

void layer_node_init() {

	ui_node_t *layer_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                           .name    = _tr("Layer"),
	                           .type    = "LAYER", // extension
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
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Layer"),
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

	any_array_push(nodes_material_input, layer_node_def);
	any_map_set(parser_material_node_vectors, "LAYER", layer_node_vector);
	any_map_set(parser_material_node_values, "LAYER", layer_node_value);
}
