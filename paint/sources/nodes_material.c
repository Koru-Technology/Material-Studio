
#include "global.h"

void nodes_material_init() {
	if (nodes_material_list != NULL) {
		return;
	}

	nodes_material_input = any_array_create_from_raw((void *[]){}, 0);
	attribute_node_init();
	rgb_node_init(); // color_node_init
	geometry_node_init();
	layer_node_init();
	layer_mask_node_init();
	material_node_init();
	object_info_node_init();
	picker_node_init();
	texture_coordinate_node_init();
	tilesheet_node_init();
	uv_map_node_init();
	value_node_init();
	variable_node_init();
	wireframe_node_init();
	mask_nodes_init();

	nodes_material_texture = any_array_create_from_raw((void *[]){}, 0);
	bake_texture_node_init();
	brick_texture_node_init();
	camera_texture_node_init();
	checker_texture_node_init();
	curvature_bake_node_init();
	gabor_texture_node_init();
	gradient_texture_node_init();
	image_texture_node_init();
	magic_texture_node_init();
	noise_texture_node_init();
	text_texture_node_init();
	voronoi_texture_node_init();
	pattern_nodes_init();
	wave_texture_node_init();

	nodes_material_color = any_array_create_from_raw((void *[]){}, 0);
	blur_node_init();
	brightness_contrast_node_init();
	color_mask_node_init();
	color_ramp_node_init();
	combine_color_node_init();
	gamma_node_init();
	hue_saturation_value_node_init();
	invert_color_node_init();
	mix_color_node_init();
	quantize_node_init();
	rgb_curves_node_init();
	rgb_to_bw_node_init();
	replace_color_node_init();
	separate_color_node_init();
	warp_node_init();

	nodes_material_utilities = any_array_create_from_raw((void *[]){}, 0);
	bump_node_init();
	clamp_node_init();
	combine_xyz_node_init();
	float_curve_node_init();
	map_range_node_init();
	select_value_node_init();
	material_mix_node_init();
	mapping_node_init();
	math2_node_init();
	mix_normal_map_node_init();
	normal_node_init();
	normal_map_node_init();
	separate_xyz_node_init();
	vector_curves_node_init();
	vector_math2_node_init();
	vector_rotate_node_init();
	vector_transform_node_init();

#ifdef WITH_NEURAL
	nodes_material_neural = any_array_create_from_raw((void *[]){}, 0);
	edit_image_node_init();
	image_to_pbr_node_init();
	repeat_node_init();
	save_image_node_init();
	text_to_image_node_init();
	upscale_image_node_init();
#endif

	nodes_material_mm = any_array_create_from_raw((void *[]){}, 0);
	mm_nodes_init();
	mm_bridge_init();
	mm_material_init(); // MM Material nodes, their own code

	nodes_material_group = any_array_create_from_raw((void *[]){}, 0);
	group_node_init();

	// The categories of nodes_material_category_names
	nodes_material_list = any_array_create(0);
	any_array_push(nodes_material_list, nodes_material_input);
	any_array_push(nodes_material_list, nodes_material_texture);
	any_array_push(nodes_material_list, nodes_material_color);
	any_array_push(nodes_material_list, nodes_material_utilities);
#ifdef WITH_NEURAL
	any_array_push(nodes_material_list, nodes_material_neural);
#endif
	any_array_push(nodes_material_list, nodes_material_mm);
	any_array_push(nodes_material_list, nodes_material_group);
}

// Names of the node menu categories, in the order of nodes_material_list
string_array_t *nodes_material_category_names() {
	string_array_t *names = any_array_create(0);
	any_array_push(names, _tr("Input"));
	any_array_push(names, _tr("Texture"));
	any_array_push(names, _tr("Color"));
	any_array_push(names, _tr("Utilities"));
#ifdef WITH_NEURAL
	any_array_push(names, _tr("Neural"));
#endif
	any_array_push(names, _tr("Material Maker"));
	any_array_push(names, _tr("Group"));
	return names;
}

ui_node_t *nodes_material_get_node_t(char *node_type) {
	for (i32 i = 0; i < nodes_material_list->length; ++i) {
		ui_node_t_array_t *c = nodes_material_list->buffer[i];
		for (i32 i = 0; i < c->length; ++i) {
			ui_node_t *n = c->buffer[i];
			if (string_equals(n->type, node_type)) {
				return n;
			}
		}
	}
	return NULL;
}

ui_node_t *nodes_material_create_node(char *node_type, node_group_t *group) {
	ui_node_t *n = nodes_material_get_node_t(node_type);
	if (n == NULL) {
		return NULL;
	}
	ui_node_canvas_t *canvas = group != NULL ? group->canvas : ui_nodes_get_canvas(false);
	ui_nodes_t       *nodes  = group != NULL ? group->nodes : g_context->material->nodes;
	ui_node_t        *node   = ui_nodes_make_node(n, nodes, canvas);
	any_array_push(canvas->nodes, node);
	return node;
}

// Socket of a node definition, the color of its type, precision 100
ui_node_socket_t *nodes_material_socket_def(char *name, char *type, f32_array_t *value, f32 min, f32 max) {
	u32 color = string_equals(type, "VALUE") ? 0xffa1a1a1 : string_equals(type, "VECTOR") ? 0xff6363c7 : 0xffc7c729;
	return nodes_material_socket_ex(name, type, color, value, min, max, 100, 0);
}

// Socket of a node definition with its own color, precision or display
ui_node_socket_t *nodes_material_socket_ex(char *name, char *type, u32 color, f32_array_t *value, f32 min, f32 max, f32 precision, i32 display) {
	return ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                     .node_id       = 0,
	                                     .name          = name,
	                                     .type          = type,
	                                     .color         = color,
	                                     .default_value = value,
	                                     .min           = min,
	                                     .max           = max,
	                                     .precision     = precision,
	                                     .display       = display});
}

// Sockets added to nodes (group sockets): color, default value and name of a type
i32 nodes_material_get_socket_color(char *type) {
	return string_equals(type, "RGBA")          ? 0xffc7c729
	       : string_equals(type, "VECTOR")      ? 0xff6363c7
	       : string_equals(type, "MM_SDF2D")    ? 0xff4fc35b // Material Maker types, they only link to their own kind
	       : string_equals(type, "MM_SDF3D")    ? 0xffb05ad6
	       : string_equals(type, "MM_SDF3DC")   ? 0xffd66ac8
	       : string_equals(type, "MM_TEX3D_GS") ? 0xff7ab8a4
	       : string_equals(type, "MM_TEX3D")    ? 0xff3fb6e0
	       : string_equals(type, "MM_V4V4")     ? 0xffe08a3f
	       : string_equals(type, "MM_FILL")     ? 0xffe05a5a
	                                            : 0xffa1a1a1;
}

f32_array_t *nodes_material_get_socket_default_value(char *type) {
	return string_equals(type, "RGBA")     ? f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0)
	       : string_equals(type, "VECTOR") ? f32_array_create_xyz(0.0, 0.0, 0.0)
	                                       : f32_array_create_x(0.0);
}

char *nodes_material_get_socket_name(char *type) {
	return string_equals(type, "RGBA") ? _tr("Color") : string_equals(type, "VECTOR") ? _tr("Vector") : _tr("Value");
}

ui_node_socket_t *nodes_material_create_socket(ui_nodes_t *nodes, ui_node_t *node, char *name, char *type, ui_node_canvas_t *canvas, f32 min, f32 max,
                                               void *default_value) {
	ui_node_socket_t *soc =
	    ALLOC_INIT(ui_node_socket_t, {.id            = ui_get_socket_id(canvas->nodes),
	                                  .node_id       = node->id,
	                                  .name          = name == NULL ? nodes_material_get_socket_name(type) : name,
	                                  .type          = type,
	                                  .color         = nodes_material_get_socket_color(type),
	                                  .default_value = default_value == NULL ? nodes_material_get_socket_default_value(type) : default_value,
	                                  .min           = min,
	                                  .max           = max,
	                                  .precision     = 100});
	return soc;
}
