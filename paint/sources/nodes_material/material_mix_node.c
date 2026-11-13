#include "../global.h"

// Material Mix: lays material B over material A, all channels at once, where Mask times Opacity says. The buttons pick the
// channels B takes part in, an edge wear layer changes the color, roughness and metallic but keeps the height below it.
// The height of B mixes in, or adds to / carves into the one below: pores, rivets, a fine bump.

#define MATERIAL_MIX_CHANNELS 8
#define MATERIAL_MIX_HEIGHT   3

static char *material_mix_names[MATERIAL_MIX_CHANNELS] = {"col", "rough", "metal", "height", "occ", "emis", "subs", "opac"};

// Channels the node has: nodes saved by an older version have fewer, the missing ones keep their defaults
static i32 material_mix_channels(ui_node_t *node) {
	i32 n = (node->inputs->length - 2) / 2;
	return n < 0 ? 0 : n > MATERIAL_MIX_CHANNELS ? MATERIAL_MIX_CHANNELS : n;
}

static char *material_mix_eval(ui_node_t *node) {
	char *store = parser_material_store_var_name(node);
	if (string_array_index_of(parser_material_parsed, store) >= 0) {
		return store;
	}
	any_array_push(parser_material_parsed, store);

	char *opacity = string_copy(parser_material_parse_value_input(node->inputs->buffer[0], false));
	char *mask    = string_copy(parser_material_parse_value_input(node->inputs->buffer[1], false));
	parser_material_write(parser_material_shader, string_tmp("float %s_f = clamp(%s * %s, 0.0, 1.0);", store, opacity, mask));
	free(opacity);
	free(mask);

	i32   n            = material_mix_channels(node);
	i32   height_blend = node->buttons->length > n ? (i32)node->buttons->buffer[n]->default_value->buffer[0] : 0;
	char *defaults[MATERIAL_MIX_CHANNELS] = {"vec3(0.8, 0.8, 0.8)", "0.5", "0.0", "0.0", "1.0", "0.0", "0.0", "1.0"};
	for (i32 i = 0; i < MATERIAL_MIX_CHANNELS; ++i) {
		char *type = i == 0 ? "vec3" : "float";
		char *name = material_mix_names[i];
		if (i >= n) {
			parser_material_write(parser_material_shader, string_tmp("%s %s_%s = %s;", type, store, name, defaults[i]));
			continue;
		}
		ui_node_socket_t *in_a = node->inputs->buffer[2 + i];
		ui_node_socket_t *in_b = node->inputs->buffer[2 + n + i];
		bool              on   = i < node->buttons->length && node->buttons->buffer[i]->default_value->buffer[0] > 0.5;
		char             *a    = string_copy(i == 0 ? parser_material_parse_vector_input(in_a) : parser_material_parse_value_input(in_a, false));
		if (on) {
			char *b = string_copy(i == 0 ? parser_material_parse_vector_input(in_b) : parser_material_parse_value_input(in_b, false));
			if (i == MATERIAL_MIX_HEIGHT && height_blend == 1) { // Add
				parser_material_write(parser_material_shader, string_tmp("float %s_%s = %s + %s * %s_f;", store, name, a, b, store));
			}
			else if (i == MATERIAL_MIX_HEIGHT && height_blend == 2) { // Subtract
				parser_material_write(parser_material_shader, string_tmp("float %s_%s = %s - %s * %s_f;", store, name, a, b, store));
			}
			else {
				parser_material_write(parser_material_shader, string_tmp("%s %s_%s = mix(%s, %s, %s_f);", type, store, name, a, b, store));
			}
			free(b);
		}
		else {
			parser_material_write(parser_material_shader, string_tmp("%s %s_%s = %s;", type, store, name, a));
		}
		free(a);
	}
	return store;
}

static char *material_mix_output(ui_node_t *node, ui_node_socket_t *socket) {
	char *store = material_mix_eval(node);
	for (i32 i = 0; i < MATERIAL_MIX_CHANNELS && i < node->outputs->length; ++i) {
		if (node->outputs->buffer[i] == socket) {
			return string_tmp("%s_%s", store, material_mix_names[i]);
		}
	}
	return string_tmp("%s_rough", store);
}

char *material_mix_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[0]) {
		return material_mix_output(node, socket);
	}
	char *v = material_mix_output(node, socket);
	return string_tmp("vec3(%s, %s, %s)", v, v, v);
}

char *material_mix_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	if (socket == node->outputs->buffer[0]) {
		return string_tmp("%s.x", material_mix_output(node, socket));
	}
	return material_mix_output(node, socket);
}

void material_mix_node_init() {
	char  *names[MATERIAL_MIX_CHANNELS]    = {_tr("Base Color"), _tr("Roughness"), _tr("Metallic"), _tr("Height"), _tr("Occlusion"), _tr("Emission"), _tr("Subsurface"), _tr("Opacity")};
	f32    defaults[MATERIAL_MIX_CHANNELS] = {0.8, 0.5, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0};
	ui_node_socket_array_t *inputs         = any_array_create_from_raw(
        (void *[]){
            nodes_material_socket_def(_tr("Opacity"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
            nodes_material_socket_def(_tr("Mask"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
        },
        2);
	ui_node_socket_array_t *outputs = any_array_create_from_raw((void *[]){}, 0);
	ui_node_button_array_t *buttons = any_array_create_from_raw((void *[]){}, 0);
	for (i32 side = 0; side < 2; ++side) {
		for (i32 i = 0; i < MATERIAL_MIX_CHANNELS; ++i) {
			char *name = string("%s %s", names[i], side == 0 ? "A" : "B");
			any_array_push(inputs, i == 0 ? nodes_material_socket_def(name, "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0)
			                              : nodes_material_socket_def(name, "VALUE", f32_array_create_x(defaults[i]), 0.0, 1.0));
		}
	}
	for (i32 i = 0; i < MATERIAL_MIX_CHANNELS; ++i) {
		any_array_push(outputs, i == 0 ? nodes_material_socket_def(names[i], "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0)
		                               : nodes_material_socket_def(names[i], "VALUE", f32_array_create_x(defaults[i]), 0.0, 1.0));
		any_array_push(buttons, nodes_material_button_def(names[i], "BOOL", 1, NULL));
	}
	any_array_push(buttons, nodes_material_button_def(_tr("Height Blend"), "ENUM", 0, string_tmp("%s\n%s\n%s", _tr("Mix"), _tr("Add"), _tr("Subtract"))));
	ui_node_t *def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                        .name    = _tr("Material Mix"),
	                                        .type    = "MATERIAL_MIX",
	                                        .x       = 0,
	                                        .y       = 0,
	                                        .color   = 0xff4982a0,
	                                        .inputs  = inputs,
	                                        .outputs = outputs,
	                                        .buttons = buttons,
	                                        .width   = 0,
	                                        .flags   = 0});
	any_array_push(nodes_material_utilities, def);
	any_map_set(parser_material_node_vectors, "MATERIAL_MIX", material_mix_node_vector);
	any_map_set(parser_material_node_values, "MATERIAL_MIX", material_mix_node_value);
}
