#include "../global.h"

// Masks for layering materials:
// Curvature Mask: the edges (ridges) or the cavities (grooves) of the model, from a curvature texture baked with the material.
// Direction Mask: the faces turned up or down.
// Both break their border up with noise.

#define CURVATURE_MASK_STRENGTH 1.0
#define CURVATURE_MASK_RADIUS   1.0
#define CURVATURE_MASK_SPREAD   4.0 // Wider than the Curvature node, wear reaches past the very edge

// Material whose canvas holds the node, NULL inside groups
slot_material_t *nodes_material_node_material(ui_node_t *node) {
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t  *m = g_project->_->materials->buffer[i];
		ui_node_canvas_t *c = m->canvas;
		for (i32 j = 0; c != NULL && j < c->nodes->length; ++j) {
			if (c->nodes->buffer[j] == node) {
				return m;
			}
		}
	}
	return NULL;
}

static bool curvature_mask_cavities(ui_node_t *node) {
	return node->buttons->buffer[0]->default_value->buffer[0] > 0.5;
}

char *curvature_mask_bake_id(ui_node_t *node, slot_material_t *m) {
	return string_tmp("m%d_cmask%d_%d", m->id, node->id, curvature_mask_cavities(node) ? 1 : 0);
}

void curvature_mask_bake_meshes(ui_node_t *node, mesh_object_t_array_t *objs, i32 res, char *id) {
	make_curvature_bake_mesh(id, objs, res, res, CURVATURE_MASK_STRENGTH, CURVATURE_MASK_RADIUS, 0.0, curvature_mask_cavities(node) ? 2 : 1, false, true,
	                         CURVATURE_MASK_SPREAD);
}

// Curvature of the paint meshes, with the material's other node previews
void curvature_mask_bake(ui_node_t *node) {
	slot_material_t *m = nodes_material_node_material(node);
	if (m == NULL) {
		return;
	}
	curvature_mask_bake_meshes(node, g_project->_->paint_objects, math_floor(config_get_texture_res_x()), string_copy(curvature_mask_bake_id(node, m)));
}

// The sphere of a material icon has other uvs than the model, its curvature would show anywhere: none while it renders
#define CURVATURE_SWAP_MAX 16
static char          *curvature_swap_keys[CURVATURE_SWAP_MAX];
static gpu_texture_t *curvature_swap_saved[CURVATURE_SWAP_MAX];
static i32            curvature_swap_count = 0;

void curvature_mask_preview_begin(slot_material_t *m) {
	curvature_swap_count = 0;
	if (m == NULL || m->canvas == NULL || g_context->node_previews == NULL) {
		return;
	}
	render_target_t *empty = any_map_get(render_path_render_targets, "empty_black");
	for (i32 i = 0; i < m->canvas->nodes->length && curvature_swap_count < CURVATURE_SWAP_MAX; ++i) {
		ui_node_t *node = m->canvas->nodes->buffer[i];
		if (!string_equals(node->type, "CURVATURE_MASK")) {
			continue;
		}
		char          *key = string_copy(curvature_mask_bake_id(node, m));
		gpu_texture_t *tex = any_map_get(g_context->node_previews, key);
		if (tex == NULL) {
			free(key);
			continue;
		}
		curvature_swap_keys[curvature_swap_count]  = key;
		curvature_swap_saved[curvature_swap_count] = tex;
		curvature_swap_count++;
		any_map_set(g_context->node_previews, key, empty->_image);
	}
}

void curvature_mask_preview_end() {
	for (i32 i = 0; i < curvature_swap_count; ++i) {
		any_map_set(g_context->node_previews, curvature_swap_keys[i], curvature_swap_saved[i]);
	}
	curvature_swap_count = 0;
}

// 1 - b * 0.9 + b * 1.8 * noise: no breakup at 0, the border wanders more the higher it is
static char *mask_breakup(char *breakup, char *scale) {
	pattern_lib_use(-1);
	node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");
	return string_tmp("(1.0 - %s * 0.9 + %s * 1.8 * clamp((pat_fbm(tex_coord * %s, float2(4096.0, 4096.0), 0.0, 0.5) - 0.5) * 2.5 + 0.5, 0.0, 1.0))",
	                  breakup, breakup, scale);
}

static char *curvature_mask_eval(ui_node_t *node) {
	char *store = parser_material_store_var_name(node);
	if (string_array_index_of(parser_material_parsed, store) >= 0) {
		return store;
	}
	any_array_push(parser_material_parsed, store);
	slot_material_t *m = nodes_material_node_material(node);
	if (m == NULL) { // Inside groups there is no curvature
		parser_material_write(parser_material_kong, string_tmp("float %s = 0.0;", store));
		return store;
	}
	char *amount  = string_copy(parser_material_parse_value_input(node->inputs->buffer[0], false));
	char *soft    = string_copy(parser_material_parse_value_input(node->inputs->buffer[1], false));
	char *breakup = string_copy(parser_material_parse_value_input(node->inputs->buffer[2], false));
	char *bscale  = string_copy(parser_material_parse_value_input(node->inputs->buffer[3], false));
	char *height  = string_copy(parser_material_parse_value_input(node->inputs->buffer[4], false));
	char *noise   = string_copy(mask_breakup(breakup, bscale));

	node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");
	char *tex = string("texbake_%s", curvature_mask_bake_id(node, m));
	node_shader_add_texture(parser_material_kong, tex, string("_%s", tex));
	// The amount lowers the curvature it starts at, the noise breaks the border up, a narrow ramp keeps half mixed metal out
	parser_material_write(parser_material_kong,
	                      string_tmp("float %s = clamp((sample(%s, sampler_linear, tex_coord).r - (1.0 - %s)) * 4.0, 0.0, 1.0) * clamp(%s, 0.0, 1.0);", store,
	                                 tex, amount, noise));
	if (curvature_mask_cavities(node)) { // Dirt settles in the recesses of the height too
		parser_material_write(parser_material_kong, string_tmp("%s = max(%s, (1.0 - %s) * %s * 0.55);", store, store, height, amount));
	}
	parser_material_write(parser_material_kong,
	                      string_tmp("%s = smoothstep(0.425 - %s * 0.5, 0.426 + %s * 0.5, %s);", store, soft, soft, store));
	free(amount);
	free(soft);
	free(breakup);
	free(bscale);
	free(height);
	free(noise);
	return store;
}

char *curvature_mask_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	return curvature_mask_eval(node);
}

char *curvature_mask_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *v = curvature_mask_eval(node);
	return string_tmp("float3(%s, %s, %s)", v, v, v);
}

static char *direction_mask_eval(ui_node_t *node) {
	char *store = parser_material_store_var_name(node);
	if (string_array_index_of(parser_material_parsed, store) >= 0) {
		return store;
	}
	any_array_push(parser_material_parsed, store);
	char *amount  = string_copy(parser_material_parse_value_input(node->inputs->buffer[0], false));
	char *soft    = string_copy(parser_material_parse_value_input(node->inputs->buffer[1], false));
	char *breakup = string_copy(parser_material_parse_value_input(node->inputs->buffer[2], false));
	char *bscale  = string_copy(parser_material_parse_value_input(node->inputs->buffer[3], false));
	char *noise   = string_copy(mask_breakup(breakup, bscale));
	bool  down    = node->buttons->buffer[0]->default_value->buffer[0] > 0.5;
	parser_material_kong->frag_n = true;
	parser_material_write(parser_material_kong, string_tmp("float %s_t = 1.0 - %s * 2.0;", store, amount));
	parser_material_write(parser_material_kong, string_tmp("float %s = %sn.z + (%s - 1.0) * 0.5;", store, down ? "-" : "", noise));
	parser_material_write(parser_material_kong,
	                      string_tmp("%s = smoothstep(%s_t - %s * 0.5, %s_t + 0.001 + %s * 0.5, %s);", store, store, soft, store, soft, store));
	free(amount);
	free(soft);
	free(breakup);
	free(bscale);
	free(noise);
	return store;
}

char *direction_mask_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	return direction_mask_eval(node);
}

char *direction_mask_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *v = direction_mask_eval(node);
	return string_tmp("float3(%s, %s, %s)", v, v, v);
}

// Height Mask: where a height (a pattern) is high, or low with Invert, with a soft border broken up by noise
static char *height_mask_eval(ui_node_t *node) {
	char *store = parser_material_store_var_name(node);
	if (string_array_index_of(parser_material_parsed, store) >= 0) {
		return store;
	}
	any_array_push(parser_material_parsed, store);
	char *height  = string_copy(parser_material_parse_value_input(node->inputs->buffer[0], false));
	char *amount  = string_copy(parser_material_parse_value_input(node->inputs->buffer[1], false));
	char *soft    = string_copy(parser_material_parse_value_input(node->inputs->buffer[2], false));
	char *breakup = string_copy(parser_material_parse_value_input(node->inputs->buffer[3], false));
	char *bscale  = string_copy(parser_material_parse_value_input(node->inputs->buffer[4], false));
	bool  invert  = node->buttons->buffer[0]->default_value->buffer[0] > 0.5;
	// Breakup removes more of the mask the higher it is: none at 0, about a tenth left at 1
	pattern_lib_use(-1);
	node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");
	char *noise = string_copy(string_tmp("smoothstep(%s * 0.8 - 0.1, %s * 0.8 + 0.1, clamp((pat_fbm(tex_coord * %s, float2(4096.0, 4096.0), 5.0, 0.5) - 0.5) * 2.5 + 0.5, 0.0, 1.0))",
	                                     breakup, breakup, bscale));
	parser_material_write(parser_material_kong, string_tmp("float %s_h = %s%s;", store, invert ? "1.0 - " : "", height));
	parser_material_write(parser_material_kong, string_tmp("float %s = smoothstep(1.0 - %s - %s * 0.5, 1.001 - %s + %s * 0.5, %s_h) * %s;",
	                                                       store, amount, soft, amount, soft, store, noise));
	free(height);
	free(amount);
	free(soft);
	free(breakup);
	free(bscale);
	free(noise);
	return store;
}

char *height_mask_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	return height_mask_eval(node);
}

char *height_mask_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *v = height_mask_eval(node);
	return string_tmp("float3(%s, %s, %s)", v, v, v);
}

static ui_node_t *mask_node_def(char *name, char *type, char *button, char *options, bool height) {
	ui_node_socket_array_t *inputs = any_array_create_from_raw(
	    (void *[]){
	        nodes_material_socket_def(_tr("Amount"), "VALUE", f32_array_create_x(0.5), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Softness"), "VALUE", f32_array_create_x(0.35), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Breakup"), "VALUE", f32_array_create_x(0.5), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Breakup Scale"), "VALUE", f32_array_create_x(25.0), 1.0, 200.0),
	    },
	    4);
	if (height) {
		any_array_push(inputs, nodes_material_socket_def(_tr("Height"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0));
	}
	return ALLOC_INIT(ui_node_t, {.id      = 0,
	                              .name    = name,
	                              .type    = type,
	                              .x       = 0,
	                              .y       = 0,
	                              .color   = 0xff4982a0,
	                              .inputs  = inputs,
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      nodes_material_socket_def(_tr("Mask"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	                                  },
	                                  1),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      nodes_material_button_def(button, "ENUM", 0, options),
	                                  },
	                                  1),
	                              .width = 0,
	                              .flags = 0});
}

void mask_nodes_init() {
	ui_node_t *curvature =
	    mask_node_def(_tr("Curvature Mask"), "CURVATURE_MASK", _tr("Type"), string_tmp("%s\n%s", _tr("Edges"), _tr("Cavities")), true);
	any_array_push(nodes_material_input, curvature);
	any_map_set(parser_material_node_values, "CURVATURE_MASK", curvature_mask_node_value);
	any_map_set(parser_material_node_vectors, "CURVATURE_MASK", curvature_mask_node_vector);

	ui_node_t *height = mask_node_def(_tr("Height Mask"), "HEIGHT_MASK", _tr("Invert"), NULL, false);
	height->buttons->buffer[0]->type = "BOOL";
	array_insert(height->inputs, 0, nodes_material_socket_def(_tr("Height"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0));
	height->inputs->buffer[2]->default_value->buffer[0] = 0.2; // Softness
	height->inputs->buffer[3]->default_value->buffer[0] = 0.0; // Breakup
	any_array_push(nodes_material_input, height);
	any_map_set(parser_material_node_values, "HEIGHT_MASK", height_mask_node_value);
	any_map_set(parser_material_node_vectors, "HEIGHT_MASK", height_mask_node_vector);

	ui_node_t *direction = mask_node_def(_tr("Direction Mask"), "DIRECTION_MASK", _tr("Facing"), string_tmp("%s\n%s", _tr("Up"), _tr("Down")), false);
	direction->inputs->buffer[0]->default_value->buffer[0] = 0.3;
	direction->inputs->buffer[1]->default_value->buffer[0] = 0.3;
	any_array_push(nodes_material_input, direction);
	any_map_set(parser_material_node_values, "DIRECTION_MASK", direction_mask_node_value);
	any_map_set(parser_material_node_vectors, "DIRECTION_MASK", direction_mask_node_vector);
}
