#include "../global.h"
#include "../material_maker/mm_internal.h"

// The MM Material node: Material Maker's material in imported graphs (.ptex import in mm_import.c).
//
// MM_MATERIAL replaces Material Maker's material node (material, material_dynamic, material_tesselated, ..) in imported
// graphs: its inputs are material.mmg's, its outputs OUTPUT_MATERIAL_PBR's inputs, its code ours (not the generator's).
// Conversions follow what Material Maker shows, its preview shader, which is what its Godot 4 ORM export becomes as an
// ORMMaterial3D:
//   Base Color  albedo_tex * albedo_color (kept in the textures' encoding, as Material Maker exports albedo). Where the
//               surface emits, the emission color: our emission is a mask that lights the base color.
//   Opacity     opacity_tex * albedo_color.a when flags_transparent, else 1 (Material Maker exports transparency only then)
//   Occlusion   ao_tex (Godot's AO; the ao parameter is AO_LIGHT_AFFECT, how much AO darkens direct light, not modelled)
//   Roughness   roughness * roughness_tex, Metallic metallic * metallic_tex (ORM: the parameters scale the texture).
//               Unlinked metallic_tex is $metallic as in material.mmg (so metallic squared, as in Material Maker), 1.0
//               for material_dynamic
//   Normal Map  normal_tex in Material Maker's convention to ours (OpenGL, +Y up, tangent space in [0, 1]): Material
//               Maker's export for Godot, normal_tex * (-1, 1, -1) + (1, 0, 1), z rebuilt from xy, then the normal
//               parameter as Godot's NORMAL_MAP_DEPTH: normalize(mix((0, 0, 1), n, normal))
//   Emission    max(emission_tex * emission_energy) as our emission mask, its color goes to Base Color
//   Height      (1 - depth_tex) * 2 * depth_scale, clamped (Material Maker's depth is 1 - height), 0 when unlinked
//   Subsurface  sss_tex * sss
// MM_MATERIAL_UNLIT replaces material_unlit: color_tex.rgb as Base Color, fully emissive (our emissive surfaces show their
// base color unlit), opacity its alpha with the Mix blend mode.

#define MM_MAT_INPUTS 9
static const char *mm_material_input_names[MM_MAT_INPUTS] = {"albedo_tex", "metallic_tex", "roughness_tex", "emission_tex", "normal_tex",
                                                             "ao_tex",     "depth_tex",    "opacity_tex",   "sss_tex"};

// Buttons
#define MM_MAT_B_COLOR    1
#define MM_MAT_B_METALLIC 2
#define MM_MAT_B_ROUGH    3
#define MM_MAT_B_EMISSION 4
#define MM_MAT_B_NORMAL   5
#define MM_MAT_B_AO       6
#define MM_MAT_B_DEPTH    7
#define MM_MAT_B_TRANSP   8
#define MM_MAT_B_SSS      9
#define MM_MAT_B_SIZE     10

static mm_nodes_param_t mm_material_params[] = {
    {"albedo_color", "Albedo", MM_NODES_PARAM_COLOR, MM_MAT_B_COLOR, 0},
    {"metallic", "Metallic", MM_NODES_PARAM_FLOAT, MM_MAT_B_METALLIC, 0},
    {"roughness", "Roughness", MM_NODES_PARAM_FLOAT, MM_MAT_B_ROUGH, 0},
    {"emission_energy", "Emission", MM_NODES_PARAM_FLOAT, MM_MAT_B_EMISSION, 0},
    {"normal", "Normal", MM_NODES_PARAM_FLOAT, MM_MAT_B_NORMAL, 0},
    {"ao", "Ambient occlusion", MM_NODES_PARAM_FLOAT, MM_MAT_B_AO, 0},
    {"depth_scale", "Depth", MM_NODES_PARAM_FLOAT, MM_MAT_B_DEPTH, 0},
    {"flags_transparent", "Transparency", MM_NODES_PARAM_BOOL, MM_MAT_B_TRANSP, 0},
    {"sss", "Subsurf. scatter.", MM_NODES_PARAM_FLOAT, MM_MAT_B_SSS, 0},
    {"size", "Size", MM_NODES_PARAM_SIZE, MM_MAT_B_SIZE, 6},
};
static mm_nodes_param_t mm_material_unlit_params[] = {
    {"blend", "Blend", MM_NODES_PARAM_ENUM, 0, 0},
};

static char *mm_material_normal_fn = "\
vec3 mm_material_normal(vec3 t, float depth) { \
	vec3 g = t * vec3(-1.0, 1.0, -1.0) + vec3(1.0, 0.0, 1.0); \
	vec2 xy = g.xy * 2.0 - 1.0; \
	vec3 m = vec3(xy, sqrt(max(0.0, 1.0 - dot(xy, xy)))); \
	return normalize(mix(vec3(0.0, 0.0, 1.0), m, depth)) * 0.5 + vec3(0.5, 0.5, 0.5); \
} \
";

bool mm_material_is(ui_node_t *n) {
	return n != NULL && (string_equals(n->type, "MM_MATERIAL") || string_equals(n->type, "MM_MATERIAL_UNLIT"));
}

static f32 mm_material_b(ui_node_t *node, i32 b) {
	return b < node->buttons->length && node->buttons->buffer[b]->default_value != NULL ? node->buttons->buffer[b]->default_value->buffer[0] : 0.0;
}

static char *mm_material_f(f32 v) {
	return string_copy(f32_to_string_with_zeros(v));
}

static char *mm_material_kind(ui_node_t *node) {
	i32 i = mm_nodes_button_index(node, "mm_material_kind");
	u8_array_t *d = i >= 0 ? node->buttons->buffer[i]->data : NULL;
	return d != NULL && d->length > 0 ? (char *)d->buffer : "material";
}

// Input as a type, copied: the parser's temporary strings do not outlive further parsing
static char *mm_material_in(ui_node_t *node, i32 i, char *type) {
	return string_copy(parser_material_parse_input_as(node->inputs->buffer[i], type, NULL));
}

static bool mm_material_linked(ui_node_t *node, i32 i) {
	return parser_material_get_input_link(node->inputs->buffer[i]) != NULL;
}

static char *mm_material_output(ui_node_t *node, ui_node_socket_t *socket) {
	i32 out = 0;
	while (out < node->outputs->length && node->outputs->buffer[out] != socket) {
		out++;
	}
	if (string_equals(node->type, "MM_MATERIAL_UNLIT")) {
		char *c  = mm_material_in(node, 0, "RGBA");
		char *rv = out == 0   ? string_tmp("vec4(%s.rgb, 1.0)", c)
		           : out == 1 ? (mm_material_b(node, 0) == 1 ? string_tmp("%s.a", c) : "1.0") // Mix blends by alpha
		           : out == 2 ? "1.0"
		           : out == 3 ? "1.0"
		           : out == 4 ? "0.0"
		           : out == 5 ? "vec3(0.5, 0.5, 1.0)"
		           : out == 6 ? "1.0" // Emissive: shown unlit
		                      : "0.0";
		free(c);
		return rv;
	}
	f32 *col = node->buttons->buffer[MM_MAT_B_COLOR]->default_value->buffer;
	char *rv = "0.0";
	if (out == 0 || out == 6) { // Base Color, Emission
		char *alb  = mm_material_in(node, 0, "RGB");
		char *em   = mm_material_in(node, 3, "RGB");
		char *ee   = mm_material_f(mm_material_b(node, MM_MAT_B_EMISSION));
		char *base = string_copy(string_tmp("(%s * vec3(%s, %s, %s))", alb, f32_to_string_with_zeros(col[0]), f32_to_string_with_zeros(col[1]),
		                                    f32_to_string_with_zeros(col[2])));
		char *e    = string_copy(string_tmp("(%s * %s)", em, ee));
		char *m    = string_copy(string_tmp("max(%s.r, max(%s.g, %s.b))", e, e, e));
		f32  *ed   = node->inputs->buffer[3]->default_value->buffer;
		bool  none = !mm_material_linked(node, 3) && ed[0] == 0 && ed[1] == 0 && ed[2] == 0;
		if (out == 0) {
			rv = none ? string_tmp("vec4(%s, 1.0)", base) : string_tmp("vec4(mix(%s, clamp(%s, 0.0, 1.0), step(0.004, %s)), 1.0)", base, e, m);
		}
		else {
			rv = none ? "0.0" : string_tmp("clamp(%s, 0.0, 1.0)", m);
		}
		free(alb);
		free(em);
		free(ee);
		free(base);
		free(e);
		free(m);
	}
	else if (out == 1) { // Opacity
		if (mm_material_b(node, MM_MAT_B_TRANSP) != 0) {
			char *op = mm_material_in(node, 7, "VALUE");
			rv       = string_tmp("clamp(%s * %s, 0.0, 1.0)", op, f32_to_string_with_zeros(col[3]));
			free(op);
		}
		else {
			rv = "1.0";
		}
	}
	else if (out == 2) { // Occlusion
		char *ao = mm_material_in(node, 5, "VALUE");
		rv       = string_tmp("%s", ao);
		free(ao);
	}
	else if (out == 3 || out == 4) { // Roughness, Metallic
		i32   in = out == 3 ? 2 : 1;
		f32   p  = mm_material_b(node, out == 3 ? MM_MAT_B_ROUGH : MM_MAT_B_METALLIC);
		char *v  = NULL;
		if (in == 1 && !mm_material_linked(node, 1)) { // $metallic, 1.0 in material_dynamic
			v = mm_material_f(string_equals(mm_material_kind(node), "material_dynamic") ? 1.0 : p);
		}
		else {
			v = mm_material_in(node, in, "VALUE");
		}
		rv = string_tmp("(%s * %s)", v, f32_to_string_with_zeros(p));
		free(v);
	}
	else if (out == 5) { // Normal Map
		if (mm_material_linked(node, 4)) {
			node_shader_add_function(parser_material_shader, mm_material_normal_fn);
			char *t = mm_material_in(node, 4, "RGB");
			rv      = string_tmp("mm_material_normal(%s, %s)", t, f32_to_string_with_zeros(mm_material_b(node, MM_MAT_B_NORMAL)));
			free(t);
		}
		else {
			rv = "vec3(0.5, 0.5, 1.0)";
		}
	}
	else if (out == 7) { // Height
		if (mm_material_linked(node, 6)) {
			char *d = mm_material_in(node, 6, "VALUE");
			rv      = string_tmp("clamp((1.0 - %s) * %s, 0.0, 1.0)", d, f32_to_string_with_zeros(2.0 * mm_material_b(node, MM_MAT_B_DEPTH)));
			free(d);
		}
		else {
			rv = "0.0";
		}
	}
	else if (out == 8) { // Subsurface
		char *s = mm_material_in(node, 8, "VALUE");
		rv      = string_tmp("(%s * %s)", s, f32_to_string_with_zeros(mm_material_b(node, MM_MAT_B_SSS)));
		free(s);
	}
	return rv;
}

static void mm_material_kind_button(i32 node_id) {
	// Hidden, the material kind
}

static ui_node_socket_array_t *mm_material_outputs() {
	return any_array_create_from_raw(
	    (void *[]){
	        nodes_material_socket_def(_tr("Base Color"), "RGBA", f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Opacity"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Occlusion"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Roughness"), "VALUE", f32_array_create_x(0.1), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Metallic"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Normal Map"), "VECTOR", f32_array_create_xyz(0.5, 0.5, 1.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Emission"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Height"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	        nodes_material_socket_def(_tr("Subsurface"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
	    },
	    9);
}

static ui_node_button_t *mm_material_value(char *name, f32 v, f32 max) {
	return mm_nodes_button(name, "VALUE", f32_array_create_x(v), NULL, 0, max, 100, 0);
}

// MM_MATERIAL and MM_MATERIAL_UNLIT, their code ours (mm_material_output)
void mm_material_init() {
	any_map_set(ui_nodes_custom_buttons, "mm_material_kind", mm_material_kind_button);
	any_map_set(parser_material_node_outputs, "MM_MATERIAL", mm_material_output);
	any_map_set(parser_material_node_outputs, "MM_MATERIAL_UNLIT", mm_material_output);
	static bool done = false; // nodes_material_list is made after the categories are filled
	if (done) {
		return;
	}
	done = true;
	mm_buf_t sizes = {0};
	for (i32 k = 6; k <= 13; ++k) {
		mm_buf_printf(&sizes, k > 6 ? "\n%d" : "%d", 1 << k);
	}
	ui_node_t *def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                        .name    = _tr("MM Material"),
	                                        .type    = "MM_MATERIAL",
	                                        .x       = 0,
	                                        .y       = 0,
	                                        .color   = 0xff527a52,
	                                        .inputs  = any_array_create_from_raw(
                                                (void *[]){
                                                    nodes_material_socket_def(_tr("Albedo"), "RGB", f32_array_create_xyz(1.0, 1.0, 1.0), 0.0, 1.0),
                                                    nodes_material_socket_def(_tr("Metallic"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
                                                    nodes_material_socket_def(_tr("Roughness"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
                                                    nodes_material_socket_def(_tr("Emission"), "RGB", f32_array_create_xyz(0.0, 0.0, 0.0), 0.0, 1.0),
                                                    nodes_material_socket_def(_tr("Normal"), "RGB", f32_array_create_xyz(0.5, 0.5, 0.5), 0.0, 1.0),
                                                    nodes_material_socket_def(_tr("Ambient Occlusion"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
                                                    nodes_material_socket_def(_tr("Depth"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
                                                    nodes_material_socket_def(_tr("Opacity"), "VALUE", f32_array_create_x(1.0), 0.0, 1.0),
                                                    nodes_material_socket_def(_tr("Subsurface"), "VALUE", f32_array_create_x(0.0), 0.0, 1.0),
                                                },
                                                MM_MAT_INPUTS),
	                                        .outputs = mm_material_outputs(),
	                                        .buttons = any_array_create_from_raw(
	                                            (void *[]){
	                                                mm_nodes_button("mm_label", "CUSTOM", f32_array_create_x(0), mm_nodes_text_data("Albedo"), 0, 1, 100, 1),
	                                                mm_nodes_button("Albedo", "RGBA", f32_array_create_xyzw(1, 1, 1, 1), NULL, 0, 1, 100, 0),
	                                                mm_material_value("Metallic", 1, 1),
	                                                mm_material_value("Roughness", 1, 1),
	                                                mm_material_value("Emission", 1, 1),
	                                                mm_material_value("Normal", 1, 10),
	                                                mm_material_value("Ambient occlusion", 1, 1),
	                                                mm_material_value("Depth", 0.5, 1),
	                                                mm_nodes_button("Transparency", "BOOL", f32_array_create_x(0), NULL, 0, 1, 100, 0),
	                                                mm_material_value("Subsurf. scatter.", 0, 1),
	                                                mm_nodes_button("Size", "ENUM", f32_array_create_x(5), mm_nodes_text_data(mm_buf_get(&sizes)), 0, 7, 100, 0),
	                                                mm_nodes_button("mm_material_kind", "CUSTOM", f32_array_create_x(0), mm_nodes_text_data("material"), 0, 1, 100, 0),
	                                            },
	                                            12),
	                                        .width   = 0,
	                                        .flags   = 0});
	ui_node_t *unlit = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                          .name    = _tr("MM Unlit Material"),
	                                          .type    = "MM_MATERIAL_UNLIT",
	                                          .x       = 0,
	                                          .y       = 0,
	                                          .color   = 0xff527a52,
	                                          .inputs  = any_array_create_from_raw(
                                                  (void *[]){
                                                      nodes_material_socket_def(_tr("Color"), "RGBA", f32_array_create_xyzw(1.0, 1.0, 1.0, 1.0), 0.0, 1.0),
                                                  },
                                                  1),
	                                          .outputs = mm_material_outputs(),
	                                          .buttons = any_array_create_from_raw(
	                                              (void *[]){
	                                                  mm_nodes_button("Blend", "ENUM", f32_array_create_x(0), mm_nodes_text_data("Add\nMix\nMul\nSub"), 0, 3, 100, 0),
	                                                  mm_nodes_button("mm_material_kind", "CUSTOM", f32_array_create_x(0), mm_nodes_text_data("material_unlit"), 0, 1, 100, 0),
	                                              },
	                                              2),
	                                          .width   = 0,
	                                          .flags   = 0});
	any_array_push(nodes_material_mm, def);
	any_array_push(nodes_material_mm, unlit);
	ui_nodes_set_menu_path("MM_MATERIAL", "Material Maker/Material/MM Material");
	ui_nodes_set_menu_path("MM_MATERIAL_UNLIT", "Material Maker/Material/MM Unlit Material");
}
