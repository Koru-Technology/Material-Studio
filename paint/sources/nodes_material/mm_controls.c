#include "../global.h"
#include "../material_maker/mm.h"

// Controls of library materials (data/material_maker/controls.json, built by paint/tools/material_maker/build_controls.py
// from the curated controls, controls.md): when a library material is imported, each control becomes a Variable
// (variable_node.c) wired to the parameters it drives through parameter sockets (mm_nodes.c). A control whose target
// range differs ("to") goes through a Map Range node, a choice whose target takes other values than the option's index
// through a Select Value node. Defaults are read from the graph, so the material looks as it did.
//
// Also "Export Parameter" in the node editor: a Variable of a parameter's current value, wired to it.

#define MM_CONTROLS_VAR_W 900 // Variables this far left of the graph, their Map Range / Select Value nodes in between
#define MM_CONTROLS_MID_W 500

static ui_node_t *mm_controls_node(ui_node_canvas_t *canvas, char *type, f32 x, f32 y) {
	ui_node_t *def = nodes_material_get_node_t(type);
	if (def == NULL) {
		return NULL;
	}
	ui_node_t *n = ui_nodes_make_node(def, NULL, canvas);
	n->x         = x;
	n->y         = y;
	n->flags     = UI_NODE_FLAG_NONE;
	any_array_push(canvas->nodes, n);
	return n;
}

static void mm_controls_link(ui_node_canvas_t *canvas, ui_node_t *from, i32 out, ui_node_t *to, i32 in) {
	if (from == NULL || to == NULL) {
		return;
	}
	any_array_push(canvas->links, project_create_node_link(canvas->links, from->id, out, to->id, in));
}

static i32 mm_controls_kind(char *type, char *unit, f32 step) {
	if (string_equals(type, "toggle")) {
		return 2;
	}
	if (string_equals(type, "choice")) {
		return 3;
	}
	if (string_equals(type, "color")) {
		return 4;
	}
	if (string_equals(type, "gradient")) {
		return 5;
	}
	return string_equals(unit, "count") || step == 1.0 ? 1 : 0;
}

// The json array's numbers as text, "3, 4, 0"
static char *mm_controls_list(f32 *v, i32 n) {
	char *r = "";
	for (i32 i = 0; i < n; ++i) {
		r = string_tmp("%s%s%s", r, i > 0 ? ", " : "", f32_to_string(v[i]));
	}
	return r;
}

// One control: its Variable and links, false when none of its targets is in the graph
static bool mm_controls_add(ui_node_canvas_t *canvas, mm_json_t *c, i32 order, f32 x, f32 *y) {
	char      *type    = (char *)mm_json_str(mm_json_get(c, "type"), "slider");
	char      *unit    = (char *)mm_json_str(mm_json_get(c, "unit"), "");
	f32        min     = (f32)mm_json_num(mm_json_get(c, "min"), 0.0);
	f32        max     = (f32)mm_json_num(mm_json_get(c, "max"), 1.0);
	f32        step    = (f32)mm_json_num(mm_json_get(c, "step"), 0.0);
	mm_json_t *targets = mm_json_get(c, "targets");
	mm_json_t *options = mm_json_get(c, "options");
	i32        kind    = mm_controls_kind(type, unit, step);
	if (targets == NULL || targets->type != MM_JSON_ARRAY) {
		return false;
	}

	ui_node_t *v = mm_controls_node(canvas, "VARIABLE", x - MM_CONTROLS_VAR_W, *y);
	if (v == NULL) {
		return false;
	}
	variable_setup(v, kind, (char *)mm_json_str(mm_json_get(c, "label"), "Control"), (char *)mm_json_str(mm_json_get(c, "id"), ""),
	               (char *)mm_json_str(mm_json_get(c, "group"), "pattern"), unit, order);
	if (kind <= 1) {
		variable_set_range(v, min, max, step);
	}
	if (kind == 3 && options != NULL) {
		char *labels = "";
		for (i32 i = 0; i < options->count; ++i) {
			labels = string_tmp("%s%s%s", labels, i > 0 ? ", " : "", mm_json_str(mm_json_get(mm_json_at(options, i), "label"), ""));
		}
		variable_set_options(v, labels);
	}

	i32        linked  = 0;
	bool       valued  = false;
	f32        mid_y   = *y;
	ui_node_t *mids[16];
	char      *mid_keys[16];
	i32        nmids   = 0;
	i32        current = -1; // The option in use (choices), -2 when the targets disagree
	for (i32 ti = 0; ti < targets->count; ++ti) {
		mm_json_t *t     = mm_json_at(targets, ti);
		char      *nname = (char *)mm_json_str(mm_json_get(t, "node"), "");
		char      *param = (char *)mm_json_str(mm_json_get(t, "param"), "");
		i32        stop  = (i32)mm_json_num(mm_json_get(t, "stop"), -1);
		ui_node_t *node  = strchr(nname, '/') == NULL ? mm_node_named(canvas, nname) : NULL; // Inside graph nodes: not yet
		if (node == NULL || mm_node_param_kind(node, param) < 0) {
			mm_import_note("control %s: no %s", mm_json_str(mm_json_get(c, "id"), ""), string_tmp("%s.%s", nname, param));
			continue;
		}
		f32 cur[320];
		i32 interp = 1;
		i32 n      = mm_node_param_get(node, param, stop, cur, 320, &interp);
		if (n < 1) {
			continue;
		}
		ui_node_t *from     = v;
		mm_json_t *to       = mm_json_get(t, "to");
		mm_json_t *offon    = mm_json_get(t, "off");
		char      *mid_key  = NULL;
		f32        sel[64];
		i32        nsel     = 0;
		bool       identity = true;
		if (kind <= 1 && to != NULL && to->type == MM_JSON_ARRAY && to->count == 2) { // A range of its own: Map Range
			f32 a   = (f32)mm_json_num(mm_json_at(to, 0), min);
			f32 b   = (f32)mm_json_num(mm_json_at(to, 1), max);
			mid_key = string_tmp("map %f %f", a, b);
			if (!valued) {
				f32 d = b != a ? min + (cur[0] - a) / (b - a) * (max - min) : cur[0];
				variable_set(v, &d, 1, 0);
				valued = true;
			}
			for (i32 i = 0; i < nmids && from == v; ++i) {
				from = string_equals(mid_keys[i], mid_key) ? mids[i] : from;
			}
			ui_node_t *m = from == v ? mm_controls_node(canvas, "MAPRANGE", x - MM_CONTROLS_MID_W, mid_y) : NULL;
			if (m != NULL) {
				f32 vals[5] = {0, min, max, a, b};
				for (i32 i = 1; i < 5; ++i) {
					m->inputs->buffer[i]->default_value->buffer[0] = vals[i];
				}
				m->buttons->buffer[0]->default_value->buffer[0] = 0; // No clamp
				mm_controls_link(canvas, v, 0, m, 0);
				mid_y += 260;
				from = m;
			}
		}
		else if (kind == 3 && options != NULL) { // Choice: the option's index, else a Select Value of this target's values
			for (i32 o = 0; o < options->count && nsel < 64; ++o) {
				f32 val      = (f32)mm_json_num(mm_json_at(mm_json_get(mm_json_at(options, o), "values"), ti), o);
				sel[nsel++]  = val;
				identity     = identity && val == o;
			}
			i32 match = -1;
			for (i32 o = 0; o < nsel && match < 0; ++o) {
				match = sel[o] == cur[0] ? o : match;
			}
			current = current == -1 ? match : current == match ? current : -2;
		}
		else if (kind == 2 && offon != NULL && offon->type != MM_JSON_NULL) { // Toggle with values of its own
			sel[0]   = (f32)mm_json_num(offon, 0);
			sel[1]   = (f32)mm_json_num(mm_json_get(t, "on"), 1);
			nsel     = 2;
			identity = sel[0] == 0 && sel[1] == 1;
			if (!valued) {
				f32 d = cur[0] == sel[1] ? 1.0 : 0.0;
				variable_set(v, &d, 1, 0);
				valued = true;
			}
		}
		else if (!valued) { // The first target's value
			if (kind == 2) {
				cur[0] = cur[0] != 0 ? 1.0 : 0.0;
			}
			variable_set(v, cur, kind == 4 ? 4 : n, interp);
			valued = true;
		}
		if (nsel > 0 && !identity) {
			mid_key = string_tmp("select %s", mm_controls_list(sel, nsel));
			for (i32 i = 0; i < nmids && from == v; ++i) {
				from = string_equals(mid_keys[i], mid_key) ? mids[i] : from;
			}
			ui_node_t *s = from == v ? mm_controls_node(canvas, "SELECT_VALUE", x - MM_CONTROLS_MID_W, mid_y) : NULL;
			if (s != NULL) {
				s->buttons->buffer[0]->data = u8_array_create_from_string(mm_controls_list(sel, nsel));
				mm_controls_link(canvas, v, 0, s, 0);
				mid_y += 160;
				from = s;
			}
		}
		if (from != v && nmids < 16 && mid_key != NULL) {
			bool known = false;
			for (i32 i = 0; i < nmids; ++i) {
				known = known || mids[i] == from;
			}
			if (!known) {
				mids[nmids]       = from;
				mid_keys[nmids++] = string_copy(mid_key);
			}
		}
		i32 socket = mm_node_param_socket(node, param, stop, canvas);
		if (socket < 0) {
			continue;
		}
		mm_controls_link(canvas, from, 0, node, socket);
		linked++;
	}
	for (i32 i = 0; i < nmids; ++i) {
		free(mid_keys[i]);
	}
	if (kind == 3) {
		f32 d = current >= 0 ? current : 0;
		variable_set(v, &d, 1, 0);
	}
	if (linked == 0) {
		ui_canvas_remove_node(canvas, v, false);
		return false;
	}
	f32 bottom = *y + (kind == 5 ? 330 : 240);
	*y         = bottom > mid_y ? bottom : mid_y;
	return true;
}

// The library material's controls into its graph (just imported, before it compiles). False when it has none.
bool mm_controls_apply(slot_material_t *m, i32 id) {
	buffer_t *blob = data_get_blob("material_maker/controls.json");
	if (m == NULL || m->canvas == NULL || blob == NULL || blob->length == 0) {
		return false;
	}
	ui_node_canvas_t *canvas = m->canvas;
	mm_mark_t         mk     = mm_arena_mark();
	mm_json_t        *all    = mm_json_parse((char *)blob->buffer, blob->length);
	mm_json_t        *list   = mm_json_get(all, string_tmp("%d", id));
	bool              any    = false;
	if (list != NULL && list->type == MM_JSON_ARRAY) {
		f32 x = 1e9, y = 1e9;
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *n = canvas->nodes->buffer[i];
			x            = n->x < x ? n->x : x;
			y            = n->y < y ? n->y : y;
		}
		for (i32 i = 0; i < list->count; ++i) {
			any = mm_controls_add(canvas, mm_json_at(list, i), i, x, &y) || any;
		}
	}
	mm_arena_release(mk);
	return any;
}

// The common controls of every library material (controls.md), shown before the material's own in their groups. They act
// after the Material Maker graph, between its Material node and the output, in native nodes the Variables feed, so they
// preview live: Hue, Saturation and Brightness through a Hue/Saturation/Value node, Roughness and Metallic added to the
// material's (clamped), Bumps as the strength of a Normal Map node, Height and Glow multiplying the material's (Glow only
// on a material that emits). Shuffle drives no node: its value is the seed variation of every random node of the graph
// (mm_bridge.c, mm_plan.c). Scale and Rotation are the material's UV Scale and UV Rotation, not Variables.

static ui_node_t *mm_controls_common_var(ui_node_canvas_t *canvas, char *label, char *id, char *group, char *unit, i32 order, f32 min,
                                         f32 max, f32 value, f32 x, f32 y) {
	ui_node_t *v = mm_controls_node(canvas, "VARIABLE", x, y);
	if (v != NULL) {
		variable_setup(v, 0, label, id, group, unit, order - 100); // Before the material's own
		variable_set_range(v, min, max, 0.0);
		variable_set(v, &value, 1, 0);
	}
	return v;
}

// node in the link of the material's output socket to the output: material -> node input in, node output 0 -> output
static bool mm_controls_splice(ui_node_canvas_t *canvas, ui_node_t *material, ui_node_t *output, i32 socket, ui_node_t *node, i32 in) {
	for (i32 i = 0; i < canvas->links->length; ++i) {
		ui_node_link_t *l = canvas->links->buffer[i];
		if (l->from_id == material->id && l->from_socket == socket && l->to_id == output->id && l->to_socket == socket) {
			array_remove(canvas->links, l);
			mm_controls_link(canvas, material, socket, node, in);
			mm_controls_link(canvas, node, 0, output, socket);
			return true;
		}
	}
	return false;
}

static ui_node_t *mm_controls_math(ui_node_canvas_t *canvas, i32 op, bool clamp, f32 x, f32 y) {
	ui_node_t *n = mm_controls_node(canvas, "MATH", x, y);
	if (n != NULL) {
		n->buttons->buffer[0]->default_value->buffer[0] = op; // 0 Add, 2 Multiply
		n->buttons->buffer[1]->default_value->buffer[0] = clamp ? 1 : 0;
	}
	return n;
}

// A Material Maker material without the common controls: its Material node wired straight to the output (materials
// imported before they existed, saved copies of those, .ptex files), they are added when it opens in the Studio
bool mm_controls_common_missing(ui_node_canvas_t *canvas) {
	if (canvas == NULL || variable_find(canvas, "shuffle") != NULL) {
		return false;
	}
	for (i32 i = 0; i < canvas->links->length; ++i) {
		ui_node_link_t *l    = canvas->links->buffer[i];
		ui_node_t      *from = ui_get_node(canvas->nodes, l->from_id);
		ui_node_t      *to   = ui_get_node(canvas->nodes, l->to_id);
		if (from != NULL && to != NULL && string_equals(from->type, "MM_MATERIAL") && string_equals(to->type, "OUTPUT_MATERIAL_PBR") &&
		    l->from_socket == 0 && l->to_socket == 0) {
			return true;
		}
	}
	return false;
}

void mm_controls_common(slot_material_t *m) {
	ui_node_canvas_t *canvas   = m != NULL ? m->canvas : NULL;
	ui_node_t        *material = NULL;
	ui_node_t        *output   = NULL;
	for (i32 i = 0; canvas != NULL && i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		material     = string_equals(n->type, "MM_MATERIAL") ? n : material;
		output       = string_equals(n->type, "OUTPUT_MATERIAL_PBR") ? n : output;
	}
	if (material == NULL || output == NULL) {
		return; // An unlit material, or none
	}
	bool emits = false;
	for (i32 i = 0; i < canvas->links->length; ++i) {
		ui_node_link_t *l = canvas->links->buffer[i];
		emits             = emits || (l->to_id == material->id && l->to_socket == 3); // emission_tex
	}
	// A column of adjustments between the material and the output, their Variables left of it
	f32 ax    = material->x + 300;
	f32 y     = material->y - 200;
	f32 vx    = material->x;
	f32 vy    = material->y + 900;
	output->x = ax + 600;

	// Base Color: Hue (degrees to the node's 0..1, 0.5 none), Saturation, Brightness
	ui_node_t *hsv = mm_controls_node(canvas, "HUE_SAT", ax + 300, y);
	if (hsv != NULL && mm_controls_splice(canvas, material, output, 0, hsv, 4)) {
		ui_node_t *hue = mm_controls_common_var(canvas, "Hue", "hue", "color", "degrees", 0, -180, 180, 0, vx, vy);
		ui_node_t *map = mm_controls_node(canvas, "MAPRANGE", ax, y);
		f32        r[5] = {0, -180, 180, 0, 1};
		for (i32 i = 1; i < 5 && map != NULL; ++i) {
			map->inputs->buffer[i]->default_value->buffer[0] = r[i];
		}
		if (map != NULL) {
			map->buttons->buffer[0]->default_value->buffer[0] = 0;
		}
		mm_controls_link(canvas, hue, 0, map, 0);
		mm_controls_link(canvas, map, 0, hsv, 0);
		ui_node_t *sat = mm_controls_common_var(canvas, "Saturation", "saturation", "color", "percent", 1, 0, 2, 1, vx, vy + 240);
		mm_controls_link(canvas, sat, 0, hsv, 1);
		ui_node_t *bri = mm_controls_common_var(canvas, "Brightness", "brightness", "color", "percent", 2, 0, 2, 1, vx, vy + 480);
		mm_controls_link(canvas, bri, 0, hsv, 2);
		vy += 720;
	}
	// Roughness and Metallic: shifted, kept in 0..1
	char *shift_label[2] = {"Roughness", "Metallic"};
	char *shift_id[2]    = {"roughness", "metallic"};
	i32   shift_socket[2] = {3, 4};
	for (i32 k = 0; k < 2; ++k) {
		ui_node_t *add = mm_controls_math(canvas, 0, true, ax + 300, y + 400 + k * 260);
		if (add != NULL && mm_controls_splice(canvas, material, output, shift_socket[k], add, 0)) {
			ui_node_t *v = mm_controls_common_var(canvas, shift_label[k], shift_id[k], "surface", "percent", k, -1, 1, 0, vx, vy);
			mm_controls_link(canvas, v, 0, add, 1);
			vy += 240;
		}
	}
	// Bumps: the normal map's strength
	ui_node_t *nm = mm_controls_node(canvas, "NORMAL_MAP", ax + 300, y + 920);
	if (nm != NULL && mm_controls_splice(canvas, material, output, 5, nm, 1)) {
		ui_node_t *v = mm_controls_common_var(canvas, "Bumps", "bumps", "surface", "percent", 2, 0, 2, 1, vx, vy);
		mm_controls_link(canvas, v, 0, nm, 0);
		vy += 240;
	}
	// Height and Glow: scaled
	ui_node_t *hm = mm_controls_math(canvas, 2, false, ax + 300, y + 1180);
	if (hm != NULL && mm_controls_splice(canvas, material, output, 7, hm, 0)) {
		ui_node_t *v = mm_controls_common_var(canvas, "Height", "height", "surface", "percent", 3, 0, 2, 1, vx, vy);
		mm_controls_link(canvas, v, 0, hm, 1);
		vy += 240;
	}
	if (emits) {
		ui_node_t *gm = mm_controls_math(canvas, 2, false, ax + 300, y + 1440);
		if (gm != NULL && mm_controls_splice(canvas, material, output, 6, gm, 0)) {
			ui_node_t *v = mm_controls_common_var(canvas, "Glow", "glow", "color", "percent", 3, 0, 4, 1, vx, vy);
			mm_controls_link(canvas, v, 0, gm, 1);
			vy += 240;
		}
	}
	// Shuffle: a seed variation in 0..1, the Studio shows a button
	mm_controls_common_var(canvas, "Shuffle", "shuffle", "detail", "", 0, 0, 1, 0, vx, vy);
}

// For scripts: the library materials with controls (ids in the order of controls.json) and their number of controls
static i32 *mm_controls_ids    = NULL;
static i32 *mm_controls_counts = NULL;
static i32  mm_controls_n      = -1;

static void mm_controls_index(void) {
	if (mm_controls_n >= 0) {
		return;
	}
	mm_controls_n  = 0;
	buffer_t *blob = data_get_blob("material_maker/controls.json");
	if (blob == NULL || blob->length == 0) {
		return;
	}
	mm_mark_t  mk  = mm_arena_mark();
	mm_json_t *all = mm_json_parse((char *)blob->buffer, blob->length);
	if (all != NULL && all->type == MM_JSON_OBJECT) {
		mm_controls_ids    = malloc(sizeof(i32) * (all->count + 1));
		mm_controls_counts = malloc(sizeof(i32) * (all->count + 1));
		for (i32 i = 0; i < all->count; ++i) {
			mm_json_t *list                   = all->items[i];
			mm_controls_ids[mm_controls_n]    = atoi(all->keys[i]);
			mm_controls_counts[mm_controls_n] = list != NULL && list->type == MM_JSON_ARRAY ? list->count : 0;
			mm_controls_n++;
		}
	}
	mm_arena_release(mk);
}

i32 mm_controls_material_count(void) {
	mm_controls_index();
	return mm_controls_n;
}

i32 mm_controls_material_id(i32 i) {
	mm_controls_index();
	return i >= 0 && i < mm_controls_n ? mm_controls_ids[i] : -1;
}

i32 mm_controls_material_controls(i32 i) {
	mm_controls_index();
	return i >= 0 && i < mm_controls_n ? mm_controls_counts[i] : 0;
}

// For scripts: one float of a parameter of the node named name (stop >= 0: of that gradient stop)
f32 mm_node_param_value(ui_node_canvas_t *canvas, char *name, char *param, i32 stop, i32 i) {
	ui_node_t *node = mm_node_named(canvas, name);
	f32        v[320];
	i32        n = node != NULL ? mm_node_param_get(node, param, stop, v, 320, NULL) : 0;
	return i >= 0 && i < n ? v[i] : 0.0;
}

// "Export Parameter": a Variable of the parameter's current value (stop >= 0: the color of that stop of a gradient), wired
// to it, left of the node. NULL when the parameter cannot be driven.
ui_node_t *mm_node_export_param(ui_node_canvas_t *canvas, ui_node_t *node, char *param, i32 stop) {
	i32 pk = node != NULL ? mm_node_param_kind(node, param) : -1;
	i32 kind;
	switch (pk) {
	case 1: // Float
		kind = 0;
		break;
	case 2: // Enum
		kind = 3;
		break;
	case 3: // Bool
		kind = 2;
		break;
	case 5: // Color
		kind = 4;
		break;
	case 6: // Gradient
		kind = stop >= 0 ? 4 : 5;
		break;
	default:
		return NULL;
	}
	char *label = param;
	for (i32 i = 0; i < mm_node_export_count(node); ++i) {
		char *n = NULL, *l = NULL;
		i32   k = 0;
		if (mm_node_export_param_at(node, i, &n, &l, &k) && string_equals(n, param)) {
			label = string_copy(l);
		}
	}
	if (stop >= 0) {
		label = string_tmp("%s %d", label, stop + 1);
	}
	f32 cur[320];
	i32 interp = 1;
	i32 n      = mm_node_param_get(node, param, stop, cur, 320, &interp);
	i32 socket = n > 0 ? mm_node_param_socket(node, param, stop, canvas) : -1;
	if (socket < 0) {
		return NULL;
	}
	ui_node_t *v = mm_controls_node(canvas, "VARIABLE", node->x - 400, node->y);
	if (v == NULL) {
		return NULL;
	}
	char      *id = string_tmp("%s_%s%s", mm_node_name(node) != NULL ? mm_node_name(node) : "node", param, stop >= 0 ? string_tmp("_%d", stop) : "");
	variable_setup(v, kind, label, id, kind >= 4 ? "color" : "pattern", "", variable_count(canvas));
	if (kind == 0) {
		f32 min = 0, max = 1, step = 0;
		mm_node_param_range(node, param, &min, &max, &step);
		variable_set_range(v, cur[0] < min ? cur[0] : min, cur[0] > max ? cur[0] : max, step);
	}
	if (kind == 3) {
		char *o = string_copy(mm_node_param_options(node, param));
		for (char *p = o; *p != 0; ++p) {
			*p = *p == '\n' ? ',' : *p;
		}
		variable_set_options(v, o);
		free(o);
	}
	variable_set(v, cur, kind == 4 ? 4 : n, interp);
	mm_controls_link(canvas, v, 0, node, socket);
	return v;
}
