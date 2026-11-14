#include "global.h"

// Material parser: greyscale of colors, node groups (outputs sharing a name, one group used twice, previews inside nested
// groups, groups inside a material node), RGBA results as vec4 and the Material Maker socket types.
// Every check renders the flat node preview of a node and reads its center texel.

int fails  = 0;
int checks = 0;
int step   = 0;
int size   = 16;

slot_material_t *mat_root = NULL;
slot_material_t *mat_sub  = NULL;
any_array_t     *groups   = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[material_parser] %s %s", ok ? "ok  " : "FAIL", name);
}

int near(int a, int b) {
	int d = a - b;
	return d >= -3 && d <= 3;
}

// Center texel of the node's preview, parents is the group path into the node's canvas
int px_r = 0;
int bgra = 0; // Texel order of the target readback, found from a red preview
int px_g = 0;
int px_b = 0;
void preview(ui_node_t *node, any_array_t *parents) {
	px_r                 = -1;
	px_g                 = -1;
	px_b                 = -1;
	gpu_texture_t *image = script_material_node_preview(node, parents, size);
	buffer_t      *b     = script_get_texture_pixels(image);
	if (b == NULL) {
		return;
	}
	int i = ((size / 2) * size + size / 2) * 4;
	px_r  = buffer_get_u8(b, i + (bgra ? 2 : 0));
	px_g  = buffer_get_u8(b, i + 1);
	px_b  = buffer_get_u8(b, i + (bgra ? 0 : 2));
}

int preview_is(ui_node_t *node, any_array_t *parents, int r, int g, int b) {
	preview(node, parents);
	return near(px_r, r) && near(px_g, g) && near(px_b, b);
}

char *got() {
	return string("got %d %d %d", px_r, px_g, px_b);
}

ui_node_canvas_t *group(char *name) {
	ui_node_canvas_t *g = script_material_group_create(name);
	any_array_push(groups, g);
	return g;
}

ui_node_t *rgb(ui_node_canvas_t *canvas, float r, float g, float b, float a) {
	ui_node_t *n = script_canvas_create_node(canvas, "RGB", NULL);
	script_material_set_color(n, 0, 0, r, g, b, a);
	return n;
}

// Mix node at factor 1, shows color 2 when color 1 is parsed first
ui_node_t *mix_second(ui_node_canvas_t *canvas) {
	ui_node_t *n = script_canvas_create_node(canvas, "MIX_RGB", NULL);
	script_material_set_float(n, 1, 0, 1.0);
	return n;
}

ui_node_t *io(ui_node_canvas_t *g, char *type) {
	for (int i = 0; i < g->nodes->length; ++i) {
		ui_node_t *n = g->nodes->buffer[i];
		if (string_equals(n->type, type)) {
			return n;
		}
	}
	return NULL;
}

// Script nodes

ui_node_socket_t *socket(char *name, char *type) {
	ui_node_socket_t *s = calloc(1, sizeof(ui_node_socket_t));
	s->name             = name;
	s->type             = type;
	s->color            = 0xffc7c729;
	s->default_value    = f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0);
	s->max              = 1.0;
	s->precision        = 100.0;
	return s;
}

ui_node_t *node_def(char *type, char *in_type, char *out_type) {
	ui_node_t *n = calloc(1, sizeof(ui_node_t));
	n->name      = type;
	n->type      = type;
	n->color     = 0xffb34f5a;
	n->inputs    = any_array_create(0);
	n->outputs   = any_array_create(0);
	n->buttons   = any_array_create(0);
	if (in_type != NULL) {
		any_array_push(n->inputs, socket("In", in_type));
	}
	any_array_push(n->outputs, socket("Out", out_type));
	return n;
}

char *grey_node(ui_node_t *node, char *socket_name) { // Greyscale of the input color
	ui_node_socket_t *inp = node->inputs->buffer[0];
	return string("vec3(%s)", parser_material_parse_value_input(inp, 1));
}

char *alpha_node(ui_node_t *node, char *socket_name) { // Alpha of the input as RGBA
	ui_node_socket_t *inp = node->inputs->buffer[0];
	return string("vec3(%s.w)", parser_material_parse_input_as(inp, "RGBA", NULL));
}

char *sdf_src_node(ui_node_t *node, char *socket_name) { // MM_SDF3D output
	return "0.25";
}

char *sdf_dst_node(ui_node_t *node, char *socket_name) { // MM_SDF3DC input as color
	ui_node_socket_t *inp = node->inputs->buffer[0];
	char             *v   = parser_material_parse_input_as(inp, "MM_SDF3DC", "vec2(0.75, 0.5)");
	return string("vec3(%s.x, %s.y, 0.0)", v, v);
}

void add_script_nodes() {
	any_array_t *list = any_array_create(0);
	any_array_push(list, node_def("PT_GREY", "RGBA", "RGBA"));
	any_array_push(list, node_def("PT_ALPHA", "RGBA", "RGBA"));
	any_array_push(list, node_def("PT_SDF_SRC", NULL, "MM_SDF3D"));
	any_array_push(list, node_def("PT_SDF_DST", "MM_SDF3DC", "RGBA"));
	script_material_category_add("Parser Test", list);
	script_material_custom_nodes_set("PT_GREY", grey_node);
	script_material_custom_nodes_set("PT_ALPHA", alpha_node);
	script_material_custom_nodes_set("PT_SDF_SRC", sdf_src_node);
	script_material_custom_nodes_set("PT_SDF_DST", sdf_dst_node);
}

void remove_script_nodes() {
	script_material_custom_nodes_remove("PT_GREY");
	script_material_custom_nodes_remove("PT_ALPHA");
	script_material_custom_nodes_remove("PT_SDF_SRC");
	script_material_custom_nodes_remove("PT_SDF_DST");
	script_material_category_remove("Parser Test");
}

// Greyscale of a color reads r, g, b in order (was .rbg)
void test_greyscale() {
	ui_node_t *red = rgb(NULL, 1.0, 0.0, 0.0, 1.0);
	preview(red, NULL);
	bgra = px_b > 200 && px_r < 50;
	check(preview_is(red, NULL, 255, 0, 0), string("red preview (%s)", got()));
	ui_node_t *c = rgb(NULL, 0.0, 1.0, 0.0, 1.0);
	ui_node_t *n = script_canvas_create_node(NULL, "PT_GREY", NULL);
	script_canvas_connect(NULL, c, 0, n, 0);
	check(preview_is(n, NULL, 150, 150, 150), string("greyscale of green is 0.587 (%s)", got()));
}

// Two group outputs with the same name ("Color", "Color") keep their own results
void test_group_outputs() {
	ui_node_canvas_t *g = group("PT Outputs");
	script_material_group_add_socket(g, 1, "RGBA", NULL);
	script_material_group_add_socket(g, 1, "RGBA", NULL);
	ui_node_t *out = io(g, "GROUP_OUTPUT");
	script_canvas_connect(g, rgb(g, 1.0, 0.0, 0.0, 1.0), 0, out, 0);
	script_canvas_connect(g, rgb(g, 0.0, 1.0, 0.0, 1.0), 0, out, 1);

	ui_node_t *gn  = script_canvas_create_node(NULL, "GROUP", "PT Outputs");
	ui_node_t *mix = mix_second(NULL);
	script_canvas_connect(NULL, gn, 0, mix, 1);
	script_canvas_connect(NULL, gn, 1, mix, 2);
	check(gn->outputs->length == 2, "group node has both outputs");
	check(preview_is(mix, NULL, 0, 255, 0), string("second group output named like the first is its own (%s)", got()));
}

// One group used twice gets the input of each instance, RGBA alpha passes through the group
void test_group_twice() {
	ui_node_canvas_t *g = group("PT Pass");
	script_material_group_add_socket(g, 0, "RGBA", NULL);
	script_material_group_add_socket(g, 1, "RGBA", NULL);
	script_canvas_connect(g, io(g, "GROUP_INPUT"), 0, io(g, "GROUP_OUTPUT"), 0);

	ui_node_t *g1 = script_canvas_create_node(NULL, "GROUP", "PT Pass");
	ui_node_t *g2 = script_canvas_create_node(NULL, "GROUP", "PT Pass");
	script_canvas_connect(NULL, rgb(NULL, 1.0, 0.0, 0.0, 1.0), 0, g1, 0);
	script_canvas_connect(NULL, rgb(NULL, 0.0, 0.0, 1.0, 0.25), 0, g2, 0);
	ui_node_t *mix = mix_second(NULL);
	script_canvas_connect(NULL, g1, 0, mix, 1);
	script_canvas_connect(NULL, g2, 0, mix, 2);
	check(preview_is(mix, NULL, 0, 0, 255), string("same group twice keeps each instance's input (%s)", got()));

	ui_node_t *a = script_canvas_create_node(NULL, "PT_ALPHA", NULL);
	script_canvas_connect(NULL, g2, 0, a, 0);
	check(preview_is(a, NULL, 64, 64, 64), string("RGBA alpha survives the group (%s)", got()));
}

// Preview of a node inside a group nested in a group reads the group input through every level
void test_nested_preview() {
	ui_node_canvas_t *inner = group("PT Inner");
	script_material_group_add_socket(inner, 0, "RGBA", NULL);
	script_material_group_add_socket(inner, 1, "RGBA", NULL);
	script_canvas_connect(inner, io(inner, "GROUP_INPUT"), 0, io(inner, "GROUP_OUTPUT"), 0);

	ui_node_canvas_t *outer = group("PT Outer");
	script_material_group_add_socket(outer, 0, "RGBA", NULL);
	script_material_group_add_socket(outer, 1, "RGBA", NULL);
	ui_node_t *in_node = script_canvas_create_node(outer, "GROUP", "PT Inner");
	script_canvas_connect(outer, io(outer, "GROUP_INPUT"), 0, in_node, 0);
	script_canvas_connect(outer, in_node, 0, io(outer, "GROUP_OUTPUT"), 0);

	ui_node_t *out_node = script_canvas_create_node(NULL, "GROUP", "PT Outer");
	script_canvas_connect(NULL, rgb(NULL, 0.0, 0.0, 1.0, 1.0), 0, out_node, 0);

	any_array_t *parents = any_array_create(0);
	any_array_push(parents, out_node);
	any_array_push(parents, in_node);
	check(preview_is(io(inner, "GROUP_INPUT"), parents, 0, 0, 255), string("preview inside nested groups reads the outer input (%s)", got()));
	check(parents->length == 2, "the caller's group path is left alone");
	check(preview_is(out_node, NULL, 0, 0, 255), string("nested groups pass the color through (%s)", got()));
}

// A group inside a material used by a material node returns to that material's canvas
void test_material_node() {
	ui_node_canvas_t *g = group("PT Red");
	script_material_group_add_socket(g, 1, "RGBA", NULL);
	script_canvas_connect(g, rgb(g, 1.0, 0.0, 0.0, 1.0), 0, io(g, "GROUP_OUTPUT"), 0);

	mat_sub        = script_material_create("parser_sub");
	int        idx = script_material_count() - 1;
	ui_node_t *gn  = script_canvas_create_node(NULL, "GROUP", "PT Red");
	ui_node_t *mix = mix_second(NULL);
	script_canvas_connect(NULL, gn, 0, mix, 1);
	script_canvas_connect(NULL, rgb(NULL, 0.0, 1.0, 0.0, 1.0), 0, mix, 2);
	script_canvas_connect(NULL, mix, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);

	script_material_set(mat_root);
	ui_node_t *mn = script_canvas_create_node(NULL, "MATERIAL", NULL);
	script_material_set_button(mn, 0, idx);
	check(preview_is(mn, NULL, 0, 255, 0), string("material node with a group inside (%s)", got()));
}

// Material Maker socket types: what links, the conversions, reading an input as a type
void test_mm_types() {
	int ok = ui_nodes_socket_types_compatible("MM_SDF3D", "MM_SDF3DC") && ui_nodes_socket_types_compatible("MM_SDF3DC", "MM_SDF3D") &&
	         ui_nodes_socket_types_compatible("MM_TEX3D", "MM_TEX3D_GS") && ui_nodes_socket_types_compatible("MM_FILL", "MM_FILL") &&
	         ui_nodes_socket_types_compatible("RGBA", "VALUE");
	int no = !ui_nodes_socket_types_compatible("MM_SDF3D", "MM_SDF2D") && !ui_nodes_socket_types_compatible("VALUE", "MM_SDF2D") &&
	         !ui_nodes_socket_types_compatible("MM_V4V4", "RGBA") && !ui_nodes_socket_types_compatible("MM_FILL", "MM_V4V4");
	check(ok && no, "Material Maker types link to their own kind only");

	ok = string_equals(parser_material_convert("v", "MM_SDF3D", "MM_SDF3DC"), "vec2(v, 0.0)") &&
	     string_equals(parser_material_convert("v", "MM_SDF3DC", "MM_SDF3D"), "v.x") &&
	     string_equals(parser_material_convert("v", "MM_TEX3D_GS", "MM_TEX3D"), "vec3(v)") &&
	     string_equals(parser_material_convert("v", "MM_TEX3D", "MM_TEX3D_GS"), "(dot(v, vec3(1.0)) / 3.0)") &&
	     string_equals(parser_material_convert("v", "RGB", "RGBA"), "vec4(v, 1.0)") &&
	     string_equals(parser_material_convert("v", "RGBA", "VECTOR"), "v.xyz") &&
	     string_equals(parser_material_convert("v", "VALUE", "RGBA"), "vec4(vec3(v), 1.0)") &&
	     string_equals(parser_material_convert("v", "RGBA", "VALUE"), "dot(v.rgb, vec3(0.299, 0.587, 0.114))") &&
	     parser_material_convert("v", "MM_SDF2D", "VALUE") == NULL;
	check(ok, "socket type conversions");
	ok = string_equals(parser_material_socket_glsl_type("MM_SDF3DC"), "vec2") && string_equals(parser_material_socket_glsl_type("MM_TEX3D"), "vec3") &&
	     string_equals(parser_material_socket_glsl_type("MM_FILL"), "vec4") && string_equals(parser_material_socket_glsl_type("RGBA"), "vec4") &&
	     string_equals(parser_material_socket_glsl_type("MM_SDF2D"), "float");
	check(ok, "socket GLSL types");

	ui_node_t *src = script_canvas_create_node(NULL, "PT_SDF_SRC", NULL);
	ui_node_t *dst = script_canvas_create_node(NULL, "PT_SDF_DST", NULL);
	check(src != NULL && dst != NULL, "Material Maker typed script nodes created");
	if (src == NULL || dst == NULL) {
		return;
	}
	check(preview_is(dst, NULL, 191, 128, 0), string("unlinked input returns the node's default (%s)", got()));
	script_canvas_connect(NULL, src, 0, dst, 0);
	check(preview_is(dst, NULL, 64, 0, 0), string("sdf3d converts to sdf3dc (%s)", got()));
	script_canvas_connect(NULL, rgb(NULL, 1.0, 1.0, 1.0, 1.0), 0, dst, 0); // Forced past the editor
	check(preview_is(dst, NULL, 191, 128, 0), string("a color linked to an sdf input is ignored (%s)", got()));
}

void next() {
	if (step == 0) {
		groups   = any_array_create(0);
		mat_root = script_material_create("parser_root");
		add_script_nodes();
		test_greyscale();
	}
	else if (step == 1) {
		test_group_outputs();
	}
	else if (step == 2) {
		test_group_twice();
	}
	else if (step == 3) {
		test_nested_preview();
	}
	else if (step == 4) {
		test_material_node();
	}
	else if (step == 5) {
		test_mm_types();
	}
	else {
		script_material_set(mat_root);
		script_material_delete(mat_sub);
		script_material_delete(mat_root);
		for (int i = 0; i < groups->length; ++i) {
			script_material_group_remove(groups->buffer[i]);
		}
		remove_script_nodes();
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[material_parser] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "PARSER PASS" : "PARSER FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	script_notify_on_next_frame(next);
}
