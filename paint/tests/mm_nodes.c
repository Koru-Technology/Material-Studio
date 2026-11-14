#include "global.h"

// Material Maker nodes: the library registers as native node types, every type builds from its .mmg, sockets and buttons
// match the definitions (shader, graph, generic, enum, bool, size, color, gradient, curve, polygon), the nodes link to each
// other and to native nodes, draw in the node editor, and survive save and load with their parameters, links and seed.

int fails  = 0;
int checks = 0;
int step   = 0;
int sub    = 0;
int shown  = 0;

slot_material_t *mat       = NULL;
char            *mat_name  = "mm_nodes_test";
char            *file      = NULL;
int              links     = 0;
int              nodes     = 0;
any_array_t     *types     = NULL;
any_array_t     *params    = NULL; // Parameters json of each type before saving
any_array_t     *datas     = NULL; // Node json with the seed
any_array_t     *in_counts = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mm_nodes] %s %s", ok ? "ok  " : "FAIL", name);
}

ui_node_t *node(char *type) {
	return script_material_get_node(type);
}

ui_node_button_t *button(ui_node_t *n, int i) {
	return n->buttons->buffer[i];
}

int has(char *s, char *part) {
	return s != NULL && string_index_of(s, part) >= 0;
}

int buttons_are(ui_node_t *n, char *list) { // "ENUM VALUE BOOL CUSTOM", button types in order
	char *s = "";
	for (int i = 0; i < n->buttons->length; ++i) {
		s = string("%s%s%s", s, i > 0 ? " " : "", button(n, i)->type);
	}
	if (!string_equals(s, list)) {
		printf("[mm_nodes] %s buttons: %s", n->type, s);
		return 0;
	}
	return 1;
}

ui_node_socket_t *input(ui_node_t *n, int i) {
	return n->inputs->buffer[i];
}

int socket_is(ui_node_socket_t *s, char *name, char *type) {
	if (!string_equals(s->name, name) || !string_equals(s->type, type)) {
		printf("[mm_nodes] socket %s %s", s->name, s->type);
		return 0;
	}
	return 1;
}

void step_library() {
	printf("[mm_nodes] registration %.2f ms", mm_nodes_init_ms());
	check(mm_nodes_count() >= 290, string("library nodes registered (%d)", mm_nodes_count()));
	int failures = mm_nodes_build_all();
	printf("[mm_nodes] building all %d types %.2f ms", mm_nodes_count(), mm_nodes_build_ms());
	printf("[mm_nodes] skipped:\n%s", mm_nodes_skipped());
	check(failures == 0, string("every type builds from its definition (%d failures)", failures));
	check(string_equals(ui_nodes_get_menu_path("MM_fbm4"), "Material Maker/Noise/FBM"), "fbm4 in Material Maker/Noise");
	check(string_equals(ui_nodes_get_menu_path("MM_graph_pattern_fibers"), "Material Maker/Pattern/Fibers"), "inline graph Fibers registered");
	check(mm_nodes_count() == 320, string("library, GDScript generators and presets registered (%d of 320)", mm_nodes_count()));
	check(ui_nodes_get_menu_path("MM_shape@star") != NULL && ui_nodes_get_menu_path("MM_bricks3@herringbone") != NULL &&
	          ui_nodes_get_menu_path("MM_buffer@advanced") != NULL && ui_nodes_get_menu_path("MM_portal@out") != NULL,
	      "presets registered as MM_<mmg>@<name>");
	check(!has(mm_nodes_skipped(), "preset") && !has(mm_nodes_skipped(), "Miscellaneous/Buffer:") && !has(mm_nodes_skipped(), "Switch") &&
	          has(mm_nodes_skipped(), "Miscellaneous/Comment: comment is written in GDScript"),
	      "only comments, exports, debug, Easy SDF, mesh map and custom shader are skipped");
}

// The generators written in GDScript and the presets

int count_links_to(ui_node_t *n) {
	int c = 0;
	for (int i = 0; i < mat->canvas->links->length; ++i) {
		ui_node_link_t *l = mat->canvas->links->buffer[i];
		if (l->to_id == n->id) {
			c = c + 1;
		}
	}
	return c;
}

int link_to(ui_node_t *n, int socket, ui_node_t *from) {
	for (int i = 0; i < mat->canvas->links->length; ++i) {
		ui_node_link_t *l = mat->canvas->links->buffer[i];
		if (l->to_id == n->id && l->to_socket == socket && l->from_id == from->id) {
			return 1;
		}
	}
	return 0;
}

int socket_names(any_array_t *sockets, char *list) {
	char *s = "";
	for (int i = 0; i < sockets->length; ++i) {
		ui_node_socket_t *so = sockets->buffer[i];
		s = string("%s%s%s", s, i > 0 ? " " : "", so->name);
	}
	if (!string_equals(s, list)) {
		printf("[mm_nodes] sockets: %s", s);
		return 0;
	}
	return 1;
}

ui_node_t *remote = NULL;
ui_node_t *rfbm   = NULL;

void step_builtins(ui_node_t *noise, ui_node_t *fbm, ui_node_t *col, ui_node_t *uni, ui_node_t *out) {
	ui_node_canvas_t *c      = mat->canvas;
	ui_node_t        *buf    = script_material_create_node_at("MM_buffer", -100, 0);
	ui_node_t        *adv    = script_material_create_node_at("MM_buffer@advanced", -100, 200);
	ui_node_t        *iter   = script_material_create_node_at("MM_iterate_buffer", -100, 400);
	ui_node_t        *sw     = script_material_create_node_at("MM_switch", 100, 0);
	ui_node_t        *rr     = script_material_create_node_at("MM_reroute", 100, 200);
	ui_node_t        *pin    = script_material_create_node_at("MM_portal", 100, 400);
	ui_node_t        *pout   = script_material_create_node_at("MM_portal@out", 300, 400);
	ui_node_t        *img    = script_material_create_node_at("MM_image", 300, 0);
	ui_node_t        *txt    = script_material_create_node_at("MM_text", 300, 200);
	ui_node_t        *herr   = script_material_create_node_at("MM_bricks3@herringbone", 500, 0);
	ui_node_t        *star   = script_material_create_node_at("MM_shape@star", 500, 200);
	remote                   = script_material_create_node_at("MM_remote", 500, 400);
	rfbm                     = fbm;
	check(buf != NULL && adv != NULL && iter != NULL && sw != NULL && rr != NULL && pin != NULL && pout != NULL && img != NULL && txt != NULL &&
	          herr != NULL && star != NULL && remote != NULL,
	      "GDScript generators and presets spawn");
	if (buf == NULL || adv == NULL || iter == NULL || sw == NULL || rr == NULL || pin == NULL || pout == NULL || img == NULL || txt == NULL || herr == NULL ||
	    star == NULL || remote == NULL) {
		return;
	}

	// Buffer: the library's is version 1, version 0 has a second output and lod, version 2 filter / mipmap / f32
	check(socket_is(input(buf, 0), "In", "RGBA") && buf->outputs->length == 1 && buttons_are(buf, "ENUM CUSTOM CUSTOM") && has(mm_node_data_json(buf), "\"version\":1") &&
	          has(mm_node_parameters_json(buf), "\"size\":9"),
	      "buffer version 1 from the library");
	check(adv->outputs->length == 1 && buttons_are(adv, "ENUM BOOL BOOL BOOL CUSTOM CUSTOM") && has(mm_node_data_json(adv), "\"version\":2"), "advanced buffer is version 2");
	mm_node_set_data_json(buf, "{\"version\":0,\"parameters\":{\"size\":10,\"lod\":2.5}}", c);
	check(buf->outputs->length == 2 && buttons_are(buf, "ENUM VALUE CUSTOM CUSTOM") && has(mm_node_parameters_json(buf), "\"lod\":2.5") &&
	          has(mm_node_parameters_json(buf), "\"size\":10") && has(mm_node_data_json(buf), "\"version\":0"),
	      "buffer set to version 0: lod and two outputs");
	script_material_connect(buf, 1, rr, 0);
	int before = c->links->length;
	mm_node_set_data_json(buf, "{\"version\":2}", c);
	check(buf->outputs->length == 1 && buttons_are(buf, "ENUM BOOL BOOL BOOL CUSTOM CUSTOM") && c->links->length == before - 1 &&
	          has(mm_node_parameters_json(buf), "\"size\":10"),
	      "buffer set to version 2 drops the link of its lod output, keeps its size");
	script_material_connect(col, 0, buf, 0);

	// Iterate buffer
	check(socket_names(iter->inputs, "In Loop In") && iter->outputs->length == 2 && buttons_are(iter, "ENUM BOOL BOOL VALUE BOOL BOOL BOOL CUSTOM") &&
	          has(mm_node_parameters_json(iter), "\"iterations\":10") && has(mm_node_parameters_json(iter), "\"size\":8"),
	      "iterate buffer ports and parameters");
	script_material_connect(buf, 0, iter, 0);

	// Switch: inputs A1 B1 A2 B2, links move with their socket as outputs and choices change
	check(socket_names(sw->inputs, "A1 B1 A2 B2") && socket_names(sw->outputs, "A B") && buttons_are(sw, "VALUE VALUE VALUE CUSTOM CUSTOM"), "switch ports by letter and choice");
	script_material_connect(fbm, 0, sw, 2);   // A2
	script_material_connect(uni, 0, sw, 1);   // B1
	script_material_connect(sw, 0, out, 4);   // A
	ui_node_t *math = node("MM_math");
	script_material_connect(sw, 1, math, 1);  // B
	mm_node_update(sw, c);
	ui_node_socket_t *a2 = sw->inputs->buffer[2];
	check(string_equals(a2->type, "VALUE") && string_equals(input(sw, 0)->type, "VALUE") && string_equals(input(sw, 1)->type, "RGBA"),
	      string("switch sockets take the linked types (%s)", a2->type));
	button(sw, 0)->default_value->buffer[0] = 3;
	mm_node_update(sw, c);
	check(socket_names(sw->inputs, "A1 B1 C1 A2 B2 C2") && socket_names(sw->outputs, "A B C") && link_to(sw, 3, fbm) && link_to(sw, 1, uni),
	      "switch with 3 outputs: A2 and B1 keep their links");
	button(sw, 1)->default_value->buffer[0] = 3;
	button(sw, 2)->default_value->buffer[0] = 2;
	mm_node_update(sw, c);
	check(sw->inputs->length == 9 && link_to(sw, 3, fbm) && button(sw, 2)->max == 2 && button(sw, 2)->default_value->buffer[0] == 2, "switch with 3 choices, source up to 2");
	button(sw, 0)->default_value->buffer[0] = 1;
	button(sw, 1)->default_value->buffer[0] = 2;
	mm_node_update(sw, c);
	check(socket_names(sw->inputs, "A1 A2") && sw->outputs->length == 1 && link_to(sw, 1, fbm) && count_links_to(sw) == 1 && !link_to(math, 1, sw) &&
	          button(sw, 2)->default_value->buffer[0] == 1,
	      "switch back to 1 output: B links removed, A2 moved, source clamped");
	button(sw, 0)->default_value->buffer[0] = 2;
	mm_node_update(sw, c);

	// Reroute takes the type of what it carries
	check(socket_is(input(rr, 0), "In", "MM_ANY") && rr->outputs->length == 1, "reroute is MM_ANY");
	script_material_connect(col, 0, rr, 0);
	mm_node_update(rr, c);
	ui_node_socket_t *rro = rr->outputs->buffer[0];
	check(string_equals(input(rr, 0)->type, "RGBA") && string_equals(rro->type, "RGBA"), "reroute adopts RGBA from its input");
	check(ui_nodes_socket_types_compatible("MM_ANY", "MM_SDF2D") && ui_nodes_socket_types_compatible("RGBA", "MM_ANY") && !ui_nodes_socket_types_compatible("RGBA", "MM_SDF2D"),
	      "MM_ANY links to anything");

	// Portal: In has the input, Out the output of the In with the same link
	check(pin->inputs->length == 1 && pin->outputs->length == 0 && pout->inputs->length == 0 && pout->outputs->length == 1 && buttons_are(pin, "CUSTOM CUSTOM CUSTOM") &&
	          has(mm_node_parameters_json(pin), "\"link\":\"aperture_1\""),
	      "portal in and out, link as a text field");
	script_material_connect(uni, 0, pin, 0);
	mm_node_update(pin, c);
	mm_node_update(pout, c);
	ui_node_socket_t *po = pout->outputs->buffer[0];
	check(string_equals(po->type, "RGBA") && has(mm_node_data_json(pout), "\"io\":1") && has(mm_node_data_json(pout), "\"port_type\":\"rgba\"") &&
	          has(mm_node_data_json(pout), "\"type\":\"portal\""),
	      "portal out carries the RGBA of its in");

	// Image and text
	check(buttons_are(img, "CUSTOM BOOL BOOL BOOL CUSTOM") && has(mm_node_parameters_json(img), "mm_icon.png") && has(mm_node_parameters_json(img), "\"filter\":true"),
	      "image parameters");
	mm_node_set_data_json(img, "{\"parameters\":{\"image\":\"%PROJECT_PATH%/stone.png\",\"clamp\":true}}", c);
	check(has(mm_node_parameters_json(img), "stone.png") && has(mm_node_parameters_json(img), "\"clamp\":true"), "image path set");
	check(buttons_are(txt, "CUSTOM CUSTOM CUSTOM RGBA CUSTOM RGBA VALUE VALUE ENUM BOOL VALUE VALUE CUSTOM") && has(mm_node_parameters_json(txt), "\"font_size\":64") &&
	          has(mm_node_parameters_json(txt), "\"text\":\"Text\""),
	      "text parameters");

	// Presets report the node they are
	check(has(mm_node_data_json(herr), "\"type\":\"bricks3\"") && has(mm_node_parameters_json(herr), "\"pattern\":3") && string_equals(herr->name, "Herringbone"),
	      "herringbone preset is bricks3 with its pattern");
	check(has(mm_node_data_json(star), "\"type\":\"shape\"") && has(mm_node_parameters_json(star), "\"sides\":5") && has(mm_node_parameters_json(star), "\"shape\":2"),
	      "star preset is shape with its values");

	// Remote: a linked control, a configuration control and a named parameter, controlling fbm by its name
	char *fn = mm_node_name(fbm);
	char *w  = string("{\"widgets\":[{\"name\":\"p1\",\"label\":\"Scale\",\"type\":\"linked_control\",\"linked_widgets\":[{\"node\":\"%s\",\"widget\":\"scale_x\"},"
	                 "{\"node\":\"%s\",\"widget\":\"scale_y\"}]},"
	                 "{\"name\":\"p2\",\"label\":\"Folded\",\"type\":\"config_control\",\"configurations\":{\"Plain\":[{\"node\":\"%s\",\"widget\":\"folds\",\"value\":0}],"
	                 "\"Folded\":[{\"node\":\"%s\",\"widget\":\"folds\",\"value\":3}]}},"
	                 "{\"name\":\"p3\",\"label\":\"Amount\",\"type\":\"named_parameter\",\"min\":0,\"max\":2,\"step\":0.1,\"default\":0.5}],"
	                 "\"parameters\":{\"p1\":7,\"p2\":1,\"p3\":1.5}}",
	                 fn, fn, fn, fn);
	mm_node_set_data_json(remote, w, c);
	check(fn != NULL && remote->inputs->length == 0 && remote->outputs->length == 0 && buttons_are(remote, "VALUE ENUM VALUE CUSTOM CUSTOM"),
	      string("remote controls, no sockets (controls %s)", fn));
	check(string_equals(button(remote, 0)->name, "Scale") && button(remote, 0)->min == 1 && button(remote, 0)->max == 32,
	      string("linked control from fbm's scale_x definition (%s %.0f..%.0f)", button(remote, 0)->name, button(remote, 0)->min, button(remote, 0)->max));
	char *cfg = (char *)button(remote, 1)->data->buffer;
	check(string_index_of(cfg, "Folded") == 0 && string_index_of(cfg, "Plain") == 7, string("configurations sorted by name (%s)", cfg));
	check(has(mm_node_parameters_json(remote), "\"p1\":7") && has(mm_node_parameters_json(remote), "\"p3\":1.5") && has(mm_node_data_json(remote), "\"widgets\""),
	      "remote values and widgets");
	button(remote, 0)->default_value->buffer[0] = 9;
	button(remote, 1)->default_value->buffer[0] = 0; // Folded
	mm_node_update(remote, c);
	check(button(fbm, 1)->default_value->buffer[0] == 9 && button(fbm, 2)->default_value->buffer[0] == 9 && button(fbm, 3)->default_value->buffer[0] == 3,
	      "remote writes the scale into both linked widgets and the folded configuration");
	button(remote, 1)->default_value->buffer[0] = 1; // Plain
	mm_node_update(remote, c);
	check(button(fbm, 3)->default_value->buffer[0] == 0, "remote plain configuration");

	any_array_push(types, "MM_buffer");
	any_array_push(types, "MM_buffer@advanced");
	any_array_push(types, "MM_iterate_buffer");
	any_array_push(types, "MM_switch");
	any_array_push(types, "MM_reroute");
	any_array_push(types, "MM_portal");
	any_array_push(types, "MM_portal@out");
	any_array_push(types, "MM_image");
	any_array_push(types, "MM_text");
	any_array_push(types, "MM_bricks3@herringbone");
	any_array_push(types, "MM_shape@star");
	any_array_push(types, "MM_remote");
}

void step_create() {
	mat       = script_material_create(mat_name);
	types     = any_array_create(0);
	ui_node_t *noise   = script_material_create_node_at("TEX_NOISE", -900, 0);
	ui_node_t *fbm     = script_material_create_node_at("MM_fbm4", -700, 0);
	ui_node_t *col     = script_material_create_node_at("MM_colorize", -500, 0);
	ui_node_t *uni     = script_material_create_node_at("MM_uniform", -500, 300);
	ui_node_t *blend   = script_material_create_node_at("MM_blend2", -300, 0);
	ui_node_t *nmap    = script_material_create_node_at("MM_normal_map2", -300, 300);
	ui_node_t *white   = script_material_create_node_at("MM_noise_white", -700, 600);
	ui_node_t *math    = script_material_create_node_at("MM_math", -500, 600);
	ui_node_t *tone    = script_material_create_node_at("MM_tonality", -300, 600);
	ui_node_t *poly    = script_material_create_node_at("MM_sdpolygon", -700, 900);
	ui_node_t *fibers  = script_material_create_node_at("MM_graph_pattern_fibers", -500, 900);
	ui_node_t *out     = script_material_get_node("OUTPUT_MATERIAL_PBR");
	check(fbm != NULL && col != NULL && uni != NULL && blend != NULL && nmap != NULL && white != NULL && math != NULL && tone != NULL &&
	          poly != NULL && fibers != NULL && out != NULL,
	      "Material Maker nodes spawn next to native nodes");
	if (fbm == NULL || col == NULL || uni == NULL || blend == NULL || nmap == NULL || white == NULL || math == NULL || tone == NULL || poly == NULL ||
	    fibers == NULL || out == NULL) {
		return;
	}

	// Shader node: fbm4, an enum and floats, an input with an expression default
	check(fbm->inputs->length == 1 && socket_is(fbm->inputs->buffer[0], "Offset Input", "VALUE") && fbm->outputs->length == 1 &&
	          socket_is(fbm->outputs->buffer[0], "Output", "VALUE"),
	      "fbm4 sockets");
	check(string_equals(fbm->name, "FBM") && buttons_are(fbm, "ENUM VALUE VALUE VALUE VALUE VALUE VALUE VALUE CUSTOM"), "fbm4 buttons");
	check(string_equals(button(fbm, 0)->name, "Noise") && button(fbm, 0)->default_value->buffer[0] == 0 && has((char *)button(fbm, 0)->data->buffer, "Voronoise"),
	      "fbm4 noise enum with the library value");
	check(string_equals(button(fbm, 1)->name, "Scale X") && button(fbm, 1)->default_value->buffer[0] == 2 && button(fbm, 1)->min == 1 &&
	          button(fbm, 1)->max == 32 && button(fbm, 1)->precision == 1,
	      "fbm4 Scale X slider: value, range and step");
	check(string_equals(mm_node_input_expression(fbm, 0), "$offset"), "fbm4 input default stays an expression");

	// Graph node: normal_map2, ports from gen_inputs / gen_outputs, parameters from gen_parameters
	check(nmap->inputs->length == 1 && socket_is(nmap->inputs->buffer[0], "Bump", "VALUE") && nmap->outputs->length == 1 &&
	          socket_is(nmap->outputs->buffer[0], "Normal", "RGB"),
	      "normal_map2 ports from its ios nodes");
	check(buttons_are(nmap, "ENUM ENUM VALUE BOOL CUSTOM"), "normal_map2 parameters from its remote");
	char *np = mm_node_parameters_json(nmap);
	printf("[mm_nodes] normal_map2 %s", np);
	check(has(np, "\"size\":10") && has(np, "\"buffer\":true") && has(np, "\"strength\":1") && has(np, "\"param2\":0"), "normal_map2 parameter values");

	// Generic node: blend2 with one layer
	check(blend->inputs->length == 3 && socket_is(blend->inputs->buffer[0], "Background", "RGBA") && socket_is(blend->inputs->buffer[1], "Layer1", "RGBA") &&
	          socket_is(blend->inputs->buffer[2], "Opacity1", "VALUE"),
	      "blend2 generic inputs with generic_size 1");
	ui_node_socket_t *opacity = blend->inputs->buffer[2];
	check(opacity->default_value->buffer[0] == 1.0 && mm_node_input_expression(blend, 2) == NULL && mm_node_input_expression(blend, 1) != NULL,
	      "constant default to the socket, expressions stay Material Maker's");
	check(buttons_are(blend, "ENUM VALUE CUSTOM CUSTOM") && has(mm_node_parameters_json(blend), "\"blend_type1\":0"), "blend2 generic parameters");
	mm_node_set_data_json(blend, "{\"generic_size\":2,\"parameters\":{\"amount2\":0.25}}", mat->canvas);
	check(blend->inputs->length == 5 && socket_is(blend->inputs->buffer[3], "Layer2", "RGBA") && has(mm_node_parameters_json(blend), "\"amount2\":0.25") &&
	          has(mm_node_data_json(blend), "\"generic_size\":2"),
	      "blend2 with generic_size 2 from Material Maker data");

	// Gradient, color, bool, size, curve, polygon
	check(socket_is(col->outputs->buffer[0], "Output", "RGBA") && buttons_are(col, "CUSTOM CUSTOM") && string_equals(button(col, 0)->name, "mm_gradient") &&
	          button(col, 0)->default_value->length == 10 && button(col, 0)->data->buffer[0] == 1,
	      "colorize gradient as color ramp stops");
	check(uni->inputs->length == 0 && buttons_are(uni, "CUSTOM RGBA CUSTOM") && button(uni, 1)->default_value->buffer[1] > 0.41 &&
	          button(uni, 1)->default_value->buffer[1] < 0.42 && button(uni, 1)->default_value->buffer[2] == 1,
	      "uniform color button with the library color");
	check(buttons_are(math, "ENUM VALUE VALUE BOOL CUSTOM") && button(math, 3)->default_value->buffer[0] == 0, "math enum, floats and bool");
	check(buttons_are(white, "ENUM CUSTOM") && button(white, 0)->default_value->buffer[0] == 7 && has((char *)button(white, 0)->data->buffer, "2048"),
	      "noise_white size as power of two sizes");
	check(socket_is(tone->inputs->buffer[0], "Input1", "VALUE") && buttons_are(tone, "CUSTOM CUSTOM CUSTOM") && string_equals(button(tone, 0)->name, "mm_curve") &&
	          button(tone, 0)->default_value->length == 8,
	      "tonality curve with slopes");
	check(socket_is(poly->outputs->buffer[0], "Output", "MM_SDF2D") && buttons_are(poly, "CUSTOM CUSTOM") && has((char *)button(poly, 0)->data->buffer, "Polygon"),
	      "sdpolygon keeps its polygon, Material Maker socket type");
	check(fibers->outputs->length >= 1, string("inline graph Fibers has outputs (%d)", fibers->outputs->length));

	// Seeds: the library's, or from the position
	char *wd = mm_node_data_json(white);
	check(has(wd, "\"seed\":0") && has(wd, "\"type\":\"noise_white\""), "seed from the library entry");

	// Links between Material Maker and native nodes
	script_material_connect(noise, 0, fbm, 0);
	script_material_connect(fbm, 0, col, 0);
	script_material_connect(col, 0, blend, 0);
	script_material_connect(uni, 0, blend, 1);
	script_material_connect(fbm, 0, nmap, 0);
	script_material_connect(white, 0, math, 0);
	script_material_connect(math, 0, tone, 0);
	script_material_connect(blend, 0, out, 0);
	script_material_connect(nmap, 0, out, 5);
	script_material_connect(tone, 0, out, 3);
	links = mat->canvas->links->length;
	check(links >= 10, string("links between Material Maker and native nodes (%d)", links));

	// Edits
	button(fbm, 0)->default_value->buffer[0] = 3;
	button(fbm, 1)->default_value->buffer[0] = 7;
	f32_array_t *g                         = button(col, 0)->default_value;
	f32_array_push(g, 1.0);
	f32_array_push(g, 0.0);
	f32_array_push(g, 0.0);
	f32_array_push(g, 1.0);
	f32_array_push(g, 0.5);
	button(col, 0)->data->buffer[0]           = 3;
	button(uni, 1)->default_value->buffer[0]  = 0.25;
	button(math, 3)->default_value->buffer[0] = 1;
	button(white, 0)->default_value->buffer[0] = 2;
	button(tone, 0)->default_value->buffer[1] = 0.2;
	button(nmap, 1)->default_value->buffer[0] = 3;

	char *cp = mm_node_parameters_json(col);
	check(has(cp, "\"type\":\"Gradient\"") && has(cp, "\"interpolation\":3") && has(cp, "\"pos\":0.5"), string("gradient back to Material Maker's format %s", cp));
	check(has(mm_node_parameters_json(white), "\"size\":6"), "size back to log2");
	check(has(mm_node_parameters_json(math), "\"clamp\":true"), "bool back to true / false");
	check(has(mm_node_parameters_json(uni), "\"type\":\"Color\"") && has(mm_node_parameters_json(uni), "\"r\":0.25"), "color back to Material Maker's format");
	check(has(mm_node_parameters_json(tone), "\"type\":\"Curve\"") && has(mm_node_parameters_json(tone), "\"ls\""), "curve back with its slopes");
	check(has(mm_node_parameters_json(poly), "\"polygon\":{"), "polygon kept as it was");

	any_array_push(types, "MM_fbm4");
	any_array_push(types, "MM_colorize");
	any_array_push(types, "MM_uniform");
	any_array_push(types, "MM_blend2");
	any_array_push(types, "MM_normal_map2");
	any_array_push(types, "MM_noise_white");
	any_array_push(types, "MM_math");
	any_array_push(types, "MM_tonality");
	any_array_push(types, "MM_sdpolygon");
	any_array_push(types, "MM_graph_pattern_fibers");
	step_builtins(noise, fbm, col, uni, out);
	params    = any_array_create(0);
	datas     = any_array_create(0);
	in_counts = any_array_create(0);
	for (int i = 0; i < types->length; ++i) {
		ui_node_t *n = node(types->buffer[i]);
		any_array_push(params, mm_node_parameters_json(n));
		any_array_push(datas, mm_node_data_json(n));
		any_array_push(in_counts, string("%d %d %d", n->inputs->length, n->outputs->length, n->buttons->length));
	}
	nodes = mat->canvas->nodes->length;
	links = mat->canvas->links->length;
	gpu_texture_t *img = script_material_node_preview(blend, NULL, 16);
	check(img != NULL, "a Material Maker node previews");
}

void step_reload_check() {
	slot_material_t *m = script_get_material(mat_name);
	check(m != NULL, "material survives save and load");
	if (m == NULL) {
		return;
	}
	script_material_set(m);
	check(m->canvas->nodes->length == nodes && m->canvas->links->length == links,
	      string("nodes and links survive (%d of %d nodes, %d of %d links)", m->canvas->nodes->length, nodes, m->canvas->links->length, links));
	for (int i = 0; i < types->length; ++i) {
		ui_node_t *n = node(types->buffer[i]);
		if (n == NULL) {
			check(0, string("%s survives", types->buffer[i]));
			continue;
		}
		char *p = mm_node_parameters_json(n);
		char *d = mm_node_data_json(n);
		char *c = string("%d %d %d", n->inputs->length, n->outputs->length, n->buttons->length);
		check(string_equals(p, params->buffer[i]), string("%s parameters survive", types->buffer[i]));
		check(string_equals(d, datas->buffer[i]), string("%s seed survives", types->buffer[i]));
		check(string_equals(c, in_counts->buffer[i]), string("%s sockets and buttons survive", types->buffer[i]));
		if (!string_equals(p, params->buffer[i])) {
			printf("[mm_nodes] before %s", params->buffer[i]);
			printf("[mm_nodes] after  %s", p);
		}
	}
	// A reloaded remote still controls fbm by its name
	ui_node_t *r = node("MM_remote");
	ui_node_t *f = node("MM_fbm4");
	if (r != NULL && f != NULL) {
		mm_node_update(r, m->canvas);
		button(r, 0)->default_value->buffer[0] = 5;
		mm_node_update(r, m->canvas);
		check(button(f, 1)->default_value->buffer[0] == 5, "reloaded remote controls the reloaded fbm");
	}
	ui_node_t *sw = node("MM_switch");
	check(sw != NULL && sw->inputs->length == 4 && sw->outputs->length == 2, "switch sockets survive");
}


void next() {
	if (step == 0) {
		step_library();
	}
	else if (step == 1) {
		step_create();
		if (!ui_nodes_show) { // Draw the nodes, custom buttons included
			ui_base_show_material_nodes();
			shown = 1;
		}
	}
	else if (step < 12) {
		base_redraw_ui();
	}
	else if (step == 12) {
		if (shown) {
			ui_base_show_material_nodes();
		}
		check(1, "node editor drew the Material Maker nodes");
		file = string("%s/mm_nodes_test.arm", script_save_path());
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == 13) {
		script_project_new();
	}
	else if (step == 14) {
		script_project_open(file);
	}
	else if (step == 15) {
		step_reload_check();
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[mm_nodes] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MM_NODES PASS" : "MM_NODES FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	script_notify_on_next_frame(next);
}
