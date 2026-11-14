#include "../global.h"
#include "../material_maker/mm_internal.h"

// Material Maker materials: the MM Material node and the .ptex importer.
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

// After mm_bridge_init, which takes the code generation of every other MM_ type
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

// Import

typedef struct mm_import_result {
	i32      ptex_nodes;   // Nodes of the graph's top level
	i32      nodes;        // Created
	i32      dropped;      // Comments, debug and export nodes
	i32      unknown;      // Types not known, skipped
	i32      ptex_links;   // Connections
	i32      links;        // Created (the material's links to the output not counted)
	i32      lost_links;   // Connections whose nodes or ports are missing
	mm_buf_t report;       // malloc'd text
} mm_import_result_t;

static mm_import_result_t mm_import_last = {0};

static void mm_import_note(const char *fmt, const char *a, const char *b) {
	char line[512];
	snprintf(line, sizeof(line), fmt, a, b);
	mm_buf_t *r = &mm_import_last.report;
	size_t    n = strlen(line);
	if (r->len + n + 2 > r->cap) {
		r->cap  = (r->len + n + 2) * 2;
		r->data = realloc(r->data, r->cap);
	}
	memcpy(r->data + r->len, line, n);
	r->len += n;
	r->data[r->len++] = '\n';
	r->data[r->len]   = 0;
}

i32 mm_import_count(i32 what) {
	mm_import_result_t *r = &mm_import_last;
	return what == 0 ? r->ptex_nodes : what == 1 ? r->nodes : what == 2 ? r->dropped : what == 3 ? r->unknown : what == 4 ? r->ptex_links : what == 5 ? r->links : r->lost_links;
}

char *mm_import_report() {
	return mm_import_last.report.data != NULL ? mm_import_last.report.data : "";
}

static bool mm_import_is_material(mm_json_t *n) {
	const char *type = mm_nodes_jstr(n, "type");
	return (type != NULL && starts_with((char *)type, "material")) || mm_json_get(mm_json_get(n, "shader_model"), "preview_shader") != NULL;
}

// Our type for a node of a Material Maker graph, NULL to drop it ("" when not known)
static char *mm_import_type(mm_json_t *n) {
	const char *type = mm_nodes_jstr(n, "type");
	if (type == NULL) {
		return "";
	}
	static const char *dropped[] = {"comment", "comment_line", "debug", "export", "autoexport", "ios"};
	for (i32 i = 0; i < (i32)(sizeof(dropped) / sizeof(dropped[0])); ++i) {
		if (strcmp(type, dropped[i]) == 0) {
			return NULL;
		}
	}
	if (mm_import_is_material(n)) {
		return strcmp(type, "material_unlit") == 0 ? "MM_MATERIAL_UNLIT" : "MM_MATERIAL";
	}
	if (mm_json_get(n, "nodes") != NULL || mm_json_get(n, "connections") != NULL) {
		return "MM_graph";
	}
	if (mm_json_get(n, "shader_model") != NULL || mm_json_get(n, "model_data") != NULL) {
		return "MM_shader";
	}
	if (strcmp(type, "sdf") == 0 || mm_json_get(n, "sdf_scene") != NULL) {
		return "MM_sdf";
	}
	char *t = string_tmp("MM_%s", type);
	return mm_nodes_find(t) != NULL ? t : "";
}

// What Material Maker assumes for a node saved without them: a buffer without version is version 0, a generic node
// without generic_size has its .mmg's, a node without seed is seeded from its position
static mm_json_t *mm_import_defaults(mm_json_t *n) {
	const char *type = mm_nodes_jstr(n, "type");
	mm_json_t  *d    = mm_json_copy(n);
	if (type != NULL && strcmp(type, "buffer") == 0 && mm_json_get(d, "version") == NULL) {
		mm_json_set(d, "version", mm_nodes_num(0));
	}
	if (mm_json_get(d, "seed") == NULL && mm_json_get(d, "seed_int") == NULL && mm_json_get(d, "seed_value") == NULL) {
		// get_seed_from_position, from its position in Material Maker's graph
		mm_json_t *p = mm_json_get(n, "node_position");
		int64_t    x = (int64_t)mm_json_num(mm_json_get(p, "x"), 0);
		int64_t    y = (int64_t)mm_json_num(mm_json_get(p, "y"), 0);
		mm_json_set(d, "seed", mm_nodes_num((double)(((x * 0x1f1f1f1f) ^ y) % 65536)));
		mm_json_set(d, "seed_locked", mm_nodes_bool(false));
	}
	if (mm_json_get(d, "generic_size") == NULL) {
		mm_json_t *lib = type != NULL && mm_json_get(n, "shader_model") == NULL ? mm_library_node(type) : NULL;
		mm_json_t *gs  = mm_json_get(lib, "generic_size");
		if (gs != NULL) {
			mm_json_set(d, "generic_size", gs);
		}
	}
	return d;
}

static ui_node_t *mm_import_output_node(ui_node_canvas_t *c) {
	for (i32 i = 0; i < c->nodes->length; ++i) {
		if (string_equals(c->nodes->buffer[i]->type, "OUTPUT_MATERIAL_PBR")) {
			return c->nodes->buffer[i];
		}
	}
	return NULL;
}

// Index of a material input of the imported material node: its input names (its .mmg, or its own shader model) to ours
static i32 mm_import_material_port(mm_json_t *n, i32 port, bool unlit) {
	mm_json_t *sm     = mm_json_get(n, "shader_model");
	mm_json_t *lib    = sm != NULL ? NULL : mm_library_node(mm_nodes_jstr(n, "type"));
	mm_json_t *inputs = mm_json_get(sm != NULL ? sm : mm_json_get(lib, "shader_model"), "inputs");
	const char *name  = mm_nodes_jstr(mm_json_at(inputs, port), "name");
	if (name == NULL) {
		return unlit ? -1 : (port < MM_MAT_INPUTS ? port : -1);
	}
	if (unlit) {
		return strcmp(name, "color_tex") == 0 ? 0 : -1;
	}
	for (i32 i = 0; i < MM_MAT_INPUTS; ++i) {
		if (strcmp(name, mm_material_input_names[i]) == 0) {
			return i;
		}
	}
	if (strcmp(name, "parallax_tex") == 0) { // Custom export materials: the depth they displace by, unless they have depth too
		for (i32 i = 0; inputs != NULL && i < inputs->count; ++i) {
			if (strcmp(mm_json_str(mm_json_get(inputs->items[i], "name"), ""), "depth_tex") == 0) {
				return -1;
			}
		}
		return 6;
	}
	return -1;
}

static void mm_import_material_params(ui_node_t *node, mm_json_t *n) {
	bool              unlit  = string_equals(node->type, "MM_MATERIAL_UNLIT");
	mm_nodes_param_t *params = unlit ? mm_material_unlit_params : mm_material_params;
	i32               count  = unlit ? 1 : (i32)(sizeof(mm_material_params) / sizeof(mm_material_params[0]));
	mm_json_t        *values = mm_json_get(n, "parameters");
	for (i32 i = 0; i < count; ++i) {
		mm_nodes_store(&params[i], node->buttons->buffer[params[i].button], mm_json_get(values, params[i].name));
	}
	i32 k = mm_nodes_button_index(node, "mm_material_kind");
	if (k >= 0 && mm_nodes_jstr(n, "type") != NULL) {
		node->buttons->buffer[k]->data = mm_nodes_text_data(mm_nodes_jstr(n, "type"));
	}
}

// The material's parameters as Material Maker saves them
char *mm_material_parameters_json(ui_node_t *node) {
	if (!mm_material_is(node)) {
		return "";
	}
	bool              unlit  = string_equals(node->type, "MM_MATERIAL_UNLIT");
	mm_nodes_param_t *params = unlit ? mm_material_unlit_params : mm_material_params;
	i32               count  = unlit ? 1 : (i32)(sizeof(mm_material_params) / sizeof(mm_material_params[0]));
	mm_mark_t         mk     = mm_arena_mark();
	mm_json_t        *o      = mm_json_new(MM_JSON_OBJECT);
	for (i32 i = 0; i < count; ++i) {
		mm_json_t *v = mm_nodes_param_value(&params[i], node->buttons->buffer[params[i].button]);
		if (v != NULL) {
			mm_json_set(o, params[i].name, v);
		}
	}
	mm_json_set(o, "kind", mm_nodes_str(mm_material_kind(node)));
	char *s = string_copy(mm_nodes_json_text(o));
	mm_arena_release(mk);
	return s;
}

static ui_node_t *mm_import_make(ui_node_canvas_t *canvas, ui_nodes_t *nodes, char *type) {
	ui_node_t *def = nodes_material_get_node_t(type);
	if (def == NULL) {
		mm_nodes_type_t *t = mm_nodes_find(type);
		def                = t != NULL ? t->def : NULL;
	}
	if (def == NULL) {
		return NULL;
	}
	ui_node_t *n = ui_nodes_make_node(def, nodes, canvas);
	any_array_push(canvas->nodes, n);
	return n;
}

// Builds the canvas of target from a Material Maker graph (a .ptex): every node as its MM_ type at its position, the
// material node as MM_MATERIAL linked to the output, connections as links (Material Maker's port index is our socket
// index). Comments, debug and export nodes are dropped, nodes of unknown types skipped and listed (mm_import_report).
bool mm_import_graph(mm_json_t *graph, slot_material_t *target) {
	mm_import_result_t *r = &mm_import_last;
	r->ptex_nodes = r->nodes = r->dropped = r->unknown = r->ptex_links = r->links = r->lost_links = 0;
	r->report.len                                                                           = 0;
	if (r->report.data != NULL) {
		r->report.data[0] = 0;
	}
	mm_json_t *nodes = mm_json_get(graph, "nodes");
	if (target == NULL || nodes == NULL || nodes->type != MM_JSON_ARRAY) {
		mm_import_note("%s%s", "not a Material Maker graph", "");
		return false;
	}
	ui_node_canvas_t *canvas = target->canvas;
	ui_node_t        *output = mm_import_output_node(canvas);
	canvas->nodes->length    = 0; // Only the output node stays
	canvas->links->length    = 0;
	if (output != NULL) {
		any_array_push(canvas->nodes, output);
	}

	// Layout: Material Maker's positions, taller as our nodes list their parameters
	f32 minx = 1e9, miny = 1e9;
	for (i32 i = 0; i < nodes->count; ++i) {
		mm_json_t *p = mm_json_get(nodes->items[i], "node_position");
		minx         = math_min(minx, mm_json_num(mm_json_get(p, "x"), 0));
		miny         = math_min(miny, mm_json_num(mm_json_get(p, "y"), 0));
	}
	r->ptex_nodes        = nodes->count;
	ui_node_t **made     = calloc(nodes->count + 1, sizeof(ui_node_t *));
	ui_node_t  *material = NULL;
	i32         mat_i    = -1;
	for (i32 i = 0; i < nodes->count; ++i) {
		mm_json_t  *n    = nodes->items[i];
		char       *type = mm_import_type(n);
		const char *name = mm_json_str(mm_json_get(n, "name"), "");
		if (type == NULL) {
			r->dropped++;
			continue;
		}
		if (type[0] == 0) {
			r->unknown++;
			mm_import_note("unknown node %s (%s), skipped", name, mm_json_str(mm_json_get(n, "type"), "?"));
			continue;
		}
		type         = string_copy(type);
		ui_node_t *u = mm_import_make(canvas, target->nodes, type);
		free(type);
		if (u == NULL) {
			r->unknown++;
			mm_import_note("node %s (%s) could not be made, skipped", name, mm_json_str(mm_json_get(n, "type"), "?"));
			continue;
		}
		mm_json_t *p = mm_json_get(n, "node_position");
		u->x         = (mm_json_num(mm_json_get(p, "x"), 0) - minx) * 1.1 + 40;
		u->y         = (mm_json_num(mm_json_get(p, "y"), 0) - miny) * 1.6 + 40;
		if (mm_material_is(u)) {
			mm_import_material_params(u, n);
			if (material == NULL) {
				material = u;
				mat_i    = i;
			}
		}
		made[i] = u;
		r->nodes++;
	}
	// Values after every node exists (remotes refer to the others by name)
	for (i32 i = 0; i < nodes->count; ++i) {
		if (made[i] != NULL && !mm_material_is(made[i])) {
			mm_node_set_data(made[i], mm_import_defaults(nodes->items[i]), canvas);
		}
	}

	// Connections, as an array or Material Maker's compact form {to: {port: {node, port}}}
	mm_json_t *cons = mm_json_get(graph, "connections");
	mm_json_t *list = mm_json_new(MM_JSON_ARRAY);
	if (cons != NULL && cons->type == MM_JSON_ARRAY) {
		list = cons;
	}
	else if (cons != NULL && cons->type == MM_JSON_OBJECT) {
		for (i32 i = 0; i < cons->count; ++i) {
			mm_json_t *ports = cons->items[i];
			for (i32 k = 0; ports != NULL && k < ports->count; ++k) {
				mm_json_t *c = mm_json_new(MM_JSON_OBJECT);
				mm_json_set(c, "from", mm_json_get(ports->items[k], "node"));
				mm_json_set(c, "from_port", mm_json_get(ports->items[k], "port"));
				mm_json_set(c, "to", mm_nodes_str(cons->keys[i]));
				mm_json_set(c, "to_port", mm_nodes_num(atof(ports->keys[k])));
				mm_json_push(list, c);
			}
		}
	}
	r->ptex_links = list->count;
	for (i32 i = 0; i < list->count; ++i) {
		mm_json_t  *c  = list->items[i];
		const char *fn = mm_json_str(mm_json_get(c, "from"), "");
		const char *tn = mm_json_str(mm_json_get(c, "to"), "");
		i32         fi = -1, ti = -1;
		for (i32 k = 0; k < nodes->count && (fi < 0 || ti < 0); ++k) {
			const char *nn = mm_json_str(mm_json_get(nodes->items[k], "name"), "");
			fi             = fi < 0 && strcmp(nn, fn) == 0 ? k : fi;
			ti             = ti < 0 && strcmp(nn, tn) == 0 ? k : ti;
		}
		ui_node_t *from  = fi >= 0 ? made[fi] : NULL;
		ui_node_t *to    = ti >= 0 ? made[ti] : NULL;
		i32        fport = (i32)mm_json_num(mm_json_get(c, "from_port"), 0);
		i32        tport = (i32)mm_json_num(mm_json_get(c, "to_port"), 0);
		if (to != NULL && mm_material_is(to)) {
			tport = mm_import_material_port(nodes->items[ti], tport, string_equals(to->type, "MM_MATERIAL_UNLIT"));
		}
		if (from == NULL || to == NULL || fport < 0 || fport >= from->outputs->length || tport < 0 || tport >= to->inputs->length) {
			bool dropped_end = (fi >= 0 && made[fi] == NULL && mm_import_type(nodes->items[fi]) == NULL) ||
			                   (ti >= 0 && made[ti] == NULL && mm_import_type(nodes->items[ti]) == NULL);
			if (!dropped_end) {
				r->lost_links++;
				mm_import_note("connection %s -> %s lost", fn, tn);
			}
			else {
				r->ptex_links--; // To or from a comment, debug or export node
			}
			continue;
		}
		any_array_push(canvas->links, project_create_node_link(canvas->links, from->id, fport, to->id, tport));
		r->links++;
	}

	mm_nodes_sync(canvas); // Remotes find their targets, switches and "any" sockets settle
	i32 links = 0;
	for (i32 i = 0; i < canvas->links->length; ++i) {
		links++;
	}
	if (links < r->links) { // A switch or buffer took some
		r->lost_links += r->links - links;
		r->links = links;
	}

	if (material != NULL && output != NULL) {
		output->x = material->x + 260;
		output->y = material->y;
		for (i32 i = 0; i < material->outputs->length && i < output->inputs->length; ++i) {
			any_array_push(canvas->links, project_create_node_link(canvas->links, material->id, i, output->id, i));
		}
	}
	else if (material == NULL) {
		mm_import_note("%s%s", "no material node", "");
	}
	free(made);
	(void)mat_i;
	return material != NULL;
}

bool mm_import_ptex(char *ptex_text, slot_material_t *target) {
	if (ptex_text == NULL) {
		return false;
	}
	mm_mark_t  mk = mm_arena_mark();
	mm_json_t *g  = mm_json_parse(ptex_text, strlen(ptex_text));
	bool       ok = mm_import_graph(g, target);
	mm_arena_release(mk);
	return ok;
}

// A new material of the project, named name, built from the graph, compiled on the next frame
static slot_material_t *mm_import_new_material(char *name, char *ptex, size_t len) {
	if (g_project == NULL || g_project->_->materials->length == 0) {
		return NULL;
	}
	slot_material_t *m = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	m->canvas->name    = string_copy(slot_material_unique_name(m->canvas, name));
	any_array_push(g_project->_->materials, m);
	mm_mark_t  mk = mm_arena_mark();
	mm_json_t *g  = mm_json_parse(ptex, len);
	mm_import_graph(g, m);
	mm_arena_release(mk);
	g_context->material = m;
	history_new_material();
	slot_material_t_array_t *imported = any_array_create_from_raw((void *[]){}, 0);
	any_array_push(imported, m);
	sys_notify_on_next_frame(&import_arm_run_material_from_project_on_next_frame, imported);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	return m;
}

// Opening or dropping a .ptex file
slot_material_t *mm_import_file(char *path) {
	buffer_t *b = iron_load_blob(path);
	if (b == NULL) {
		console_error(tr("Failed to import material"));
		return NULL;
	}
	char *file = path_base_name(path);
	i32   dot  = string_last_index_of(file, ".");
	char *name = string_copy(dot > 0 ? substring(file, 0, dot) : file);
	mm_nodes_init();
	slot_material_t *m = mm_import_new_material(name, (char *)b->buffer, b->length);
	if (m != NULL && mm_import_last.unknown > 0) {
		console_info(string_tmp("%s: %s", name, mm_import_report()));
	}
	return m;
}

// Material Maker's library (data/material_maker/library.pack, from materialmaker.org)

static mm_pack_t *mm_import_library_pack = NULL;

static mm_pack_t *mm_import_library_open() {
	if (mm_import_library_pack == NULL) {
		buffer_t *blob = data_get_blob("material_maker/library.pack");
		if (blob == NULL) {
			return NULL;
		}
		u8 *data = malloc(blob->length); // The pack keeps pointing into it
		memcpy(data, blob->buffer, blob->length);
		mm_import_library_pack = mm_pack_open(data, blob->length);
	}
	return mm_import_library_pack;
}

i32 mm_import_library_count() {
	mm_pack_t *p = mm_import_library_open();
	return p != NULL ? mm_pack_count(p) : 0;
}

i32 mm_import_library_id(i32 i) {
	mm_pack_t *p = mm_import_library_open();
	return p != NULL ? (i32)mm_json_num(mm_json_get(mm_pack_entry(p, i), "id"), -1) : -1;
}

char *mm_import_library_name(i32 id) {
	mm_pack_t *p = mm_import_library_open();
	i32        k = p != NULL ? mm_pack_find(p, "id", string_tmp("%d", id)) : -1;
	return k >= 0 ? (char *)mm_json_str(mm_json_get(mm_pack_entry(p, k), "name"), "") : NULL;
}

char *mm_import_library_kind(i32 id) {
	mm_pack_t *p = mm_import_library_open();
	i32        k = p != NULL ? mm_pack_find(p, "id", string_tmp("%d", id)) : -1;
	return k >= 0 ? (char *)mm_json_str(mm_json_get(mm_pack_entry(p, k), "kind"), "") : NULL;
}

// The library material with that "id" as a new material of the project
slot_material_t *mm_import_library(i32 id) {
	mm_pack_t *p = mm_import_library_open();
	i32        k = p != NULL ? mm_pack_find(p, "id", string_tmp("%d", id)) : -1;
	if (k < 0) {
		return NULL;
	}
	size_t len = 0;
	char  *txt = mm_pack_read(p, k, &len);
	if (txt == NULL) {
		return NULL;
	}
	mm_nodes_init();
	slot_material_t *m = mm_import_new_material((char *)mm_json_str(mm_json_get(mm_pack_entry(p, k), "name"), "Material"), txt, len);
	free(txt);
	return m;
}

// Imports into target without making a material (bulk checks)
bool mm_import_library_into(i32 id, slot_material_t *target) {
	mm_pack_t *p = mm_import_library_open();
	i32        k = p != NULL ? mm_pack_find(p, "id", string_tmp("%d", id)) : -1;
	if (k < 0 || target == NULL) {
		return false;
	}
	size_t len = 0;
	char  *txt = mm_pack_read(p, k, &len);
	if (txt == NULL) {
		return false;
	}
	mm_mark_t  mk = mm_arena_mark();
	bool       ok = mm_import_graph(mm_json_parse(txt, len), target);
	mm_arena_release(mk);
	free(txt);
	return ok;
}

ui_node_t *mm_import_material_node(slot_material_t *m) {
	for (i32 i = 0; m != NULL && i < m->canvas->nodes->length; ++i) {
		if (mm_material_is(m->canvas->nodes->buffer[i])) {
			return m->canvas->nodes->buffer[i];
		}
	}
	return NULL;
}

// Flat preview of one output of a node (script_material_node_preview shows the output picked in the editor)
gpu_texture_t *mm_import_preview(ui_node_t *node, i32 socket, i32 size) {
	if (node == NULL || g_context->material == NULL) {
		return NULL;
	}
	i32 before = i32_imap_get(g_context->node_preview_socket_map, node->id);
	i32_imap_set(g_context->node_preview_socket_map, node->id, socket);
	gpu_texture_t *img = script_material_node_preview(node, NULL, size);
	i32_imap_set(g_context->node_preview_socket_map, node->id, before);
	return img;
}

// Compiles the material and renders its preview on the next frame, as after an import
void mm_import_compile(slot_material_t *m) {
	slot_material_t_array_t *list = any_array_create_from_raw((void *[]){}, 0);
	any_array_push(list, m);
	sys_notify_on_next_frame(&import_arm_run_material_from_project_on_next_frame, list);
}

// Readback of a preview as 8 bit texels in the readback's channel order (RGBA64 material previews converted)
static float mm_import_half(uint16_t h) {
	uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
	float    v = e == 0 ? m / 1024.0f / 16384.0f : e == 31 ? 65504.0f : (1.0f + m / 1024.0f) * powf(2.0f, (float)e - 15.0f);
	return s ? -v : v;
}

static buffer_t *mm_import_texels(gpu_texture_t *t) {
	buffer_t *b = t != NULL ? script_get_texture_pixels(t) : NULL;
	if (b == NULL || t->format != GPU_TEXTURE_FORMAT_RGBA64) {
		return b;
	}
	i32       n = t->width * t->height;
	buffer_t *o = buffer_create(n * 4);
	for (i32 i = 0; i < n * 4; ++i) {
		uint16_t h   = b->buffer[i * 2] | (b->buffer[i * 2 + 1] << 8);
		f32      v   = mm_import_half(h);
		o->buffer[i] = (u8)(math_max(0.0, math_min(1.0, v)) * 255.0 + 0.5);
	}
#ifdef IRON_BGRA
	for (i32 i = 0; i < n; ++i) { // In the order 8 bit readbacks come in, as iron_write_png takes them
		u8 r              = o->buffer[i * 4];
		o->buffer[i * 4]  = o->buffer[i * 4 + 2];
		o->buffer[i * 4 + 2] = r;
	}
#endif
	return o;
}

void mm_import_save_png(gpu_texture_t *t, char *path) {
	buffer_t *b = mm_import_texels(t);
	if (b != NULL) {
		iron_write_png(path, b, t->width, t->height, 0);
	}
}

// Mean absolute difference of two previews of the same size, per channel 0..255, -1 when they do not compare
i32 mm_import_image_diff(gpu_texture_t *a, gpu_texture_t *b) {
	buffer_t *pa = mm_import_texels(a);
	buffer_t *pb = mm_import_texels(b);
	if (pa == NULL || pb == NULL || pa->length != pb->length || pa->length == 0) {
		return -1;
	}
	i64 d = 0;
	for (u32 i = 0; i < pa->length; i += 4) {
		for (i32 c = 0; c < 3; ++c) {
			d += abs((i32)pa->buffer[i + c] - (i32)pb->buffer[i + c]);
		}
	}
	return (i32)(d / (pa->length / 4 * 3));
}
