#include "../global.h"

// Material Mix: lays material B over material A, all channels at once, where Mask times Opacity says. The buttons pick the
// channels B takes part in, an edge wear layer changes the color, roughness and metallic but keeps the height below it.

#define MATERIAL_MIX_CHANNELS 5

static char *material_mix_eval(ui_node_t *node) {
	char *store = parser_material_store_var_name(node);
	if (string_array_index_of(parser_material_parsed, store) >= 0) {
		return store;
	}
	any_array_push(parser_material_parsed, store);

	char *opacity = string_copy(parser_material_parse_value_input(node->inputs->buffer[0], false));
	char *mask    = string_copy(parser_material_parse_value_input(node->inputs->buffer[1], false));
	parser_material_write(parser_material_kong, string_tmp("float %s_f = clamp(%s * %s, 0.0, 1.0);", store, opacity, mask));
	free(opacity);
	free(mask);

	char *names[MATERIAL_MIX_CHANNELS] = {"col", "rough", "metal", "height", "occ"};
	for (i32 i = 0; i < MATERIAL_MIX_CHANNELS; ++i) {
		ui_node_socket_t *in_a = node->inputs->buffer[2 + i];
		ui_node_socket_t *in_b = node->inputs->buffer[2 + MATERIAL_MIX_CHANNELS + i];
		bool              on   = i < node->buttons->length && node->buttons->buffer[i]->default_value->buffer[0] > 0.5;
		char             *a    = string_copy(i == 0 ? parser_material_parse_vector_input(in_a) : parser_material_parse_value_input(in_a, false));
		char             *type = i == 0 ? "float3" : "float";
		if (on) {
			char *b = string_copy(i == 0 ? parser_material_parse_vector_input(in_b) : parser_material_parse_value_input(in_b, false));
			parser_material_write(parser_material_kong, string_tmp("%s %s_%s = lerp(%s, %s, %s_f);", type, store, names[i], a, b, store));
			free(b);
		}
		else {
			parser_material_write(parser_material_kong, string_tmp("%s %s_%s = %s;", type, store, names[i], a));
		}
		free(a);
	}
	return store;
}

static char *material_mix_output(ui_node_t *node, ui_node_socket_t *socket) {
	char *names[MATERIAL_MIX_CHANNELS] = {"col", "rough", "metal", "height", "occ"};
	char *store                        = material_mix_eval(node);
	for (i32 i = 0; i < MATERIAL_MIX_CHANNELS; ++i) {
		if (node->outputs->buffer[i] == socket) {
			return string_tmp("%s_%s", store, names[i]);
		}
	}
	return string_tmp("%s_rough", store);
}

char *material_mix_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[0]) {
		return material_mix_output(node, socket);
	}
	char *v = material_mix_output(node, socket);
	return string_tmp("float3(%s, %s, %s)", v, v, v);
}

char *material_mix_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[0]) {
		return string_tmp("%s.x", material_mix_output(node, socket));
	}
	return material_mix_output(node, socket);
}

void material_mix_node_init() {
	ui_node_socket_array_t *inputs = any_array_create_from_raw(
	    (void *[]){
	        nodes_material_socket_def(_tr("Opacity"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Mask"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Base Color A"), "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Roughness A"), "VALUE", f32_array_create_x(0.5), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Metallic A"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Height A"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Occlusion A"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Base Color B"), "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Roughness B"), "VALUE", f32_array_create_x(0.5), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Metallic B"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Height B"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Occlusion B"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	    },
	    2 + MATERIAL_MIX_CHANNELS * 2);
	ui_node_t *def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                        .name    = _tr("Material Mix"),
	                                        .type    = "MATERIAL_MIX",
	                                        .x       = 0,
	                                        .y       = 0,
	                                        .color   = 0xff4982a0,
	                                        .inputs  = inputs,
	                                        .outputs = any_array_create_from_raw(
	                                            (void *[]){
	                                                nodes_material_socket_def(_tr("Base Color"), "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0),
	                                                nodes_material_socket_def(_tr("Roughness"), "VALUE", f32_array_create_x(0.5), 0.0, 1.0),
	                                                nodes_material_socket_def(_tr("Metallic"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                                nodes_material_socket_def(_tr("Height"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                                nodes_material_socket_def(_tr("Occlusion"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	                                            },
	                                            MATERIAL_MIX_CHANNELS),
	                                        .buttons = any_array_create_from_raw(
	                                            (void *[]){
	                                                nodes_material_button_def(_tr("Color"), "BOOL", 1, NULL),
	                                                nodes_material_button_def(_tr("Roughness"), "BOOL", 1, NULL),
	                                                nodes_material_button_def(_tr("Metallic"), "BOOL", 1, NULL),
	                                                nodes_material_button_def(_tr("Height"), "BOOL", 1, NULL),
	                                                nodes_material_button_def(_tr("Occlusion"), "BOOL", 1, NULL),
	                                            },
	                                            MATERIAL_MIX_CHANNELS),
	                                        .width = 0,
	                                        .flags = 0});
	any_array_push(nodes_material_utilities, def);
	any_map_set(parser_material_node_vectors, "MATERIAL_MIX", material_mix_node_vector);
	any_map_set(parser_material_node_values, "MATERIAL_MIX", material_mix_node_value);
}
