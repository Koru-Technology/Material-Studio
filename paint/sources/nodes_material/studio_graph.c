#include "../global.h"

// Studio materials are ordinary node graphs the Material Studio builds and edits. Each layer is a few nodes:
//
//   Pattern ─Height─┬─ Mix Color (cell variation) ─ Color Ramp ─────── Material Mix (B) ── .. ── Material Output
//           ─Cell───┘─ Map Range (roughness) ──────────────────────────┤
//                   └─ Math multiply (height) ─────────────────────────┤
//                                                    Mask (layers 1+) ─┘ Mask
//
// Map Range nodes from the pattern height also give the metallic and the occlusion (cavities). The Material Mix of a layer
// takes the one below it as A, the top one feeds the output. Emission and subsurface are B inputs of the mix.
// Every studio node carries its role and layer in the high bits of its flags, so the studio finds them however they were
// moved, and nodes the user adds in the node editor are left alone.

#define STUDIO_FLAG_SHIFT 8
#define STUDIO_FLAG_MASK  0xff00

// Inputs of the nodes the studio uses
#define MIX_IN_OPACITY 0
#define MIX_IN_MASK    1
#define MIX_IN_A       2 // Base Color, Roughness, Metallic, Height, Occlusion, Emission, Subsurface
#define MIX_IN_B       10
#define MIX_CHANNELS   8
#define MIX_BLEND      8 // Height blend button

i32 studio_role(ui_node_t *n) {
	return (n->flags >> STUDIO_FLAG_SHIFT) & 0xf;
}

i32 studio_layer(ui_node_t *n) {
	return (n->flags >> (STUDIO_FLAG_SHIFT + 4)) & 0xf;
}

static void studio_tag(ui_node_t *n, i32 role, i32 layer) {
	n->flags = (n->flags & ~STUDIO_FLAG_MASK) | (role << STUDIO_FLAG_SHIFT) | (layer << (STUDIO_FLAG_SHIFT + 4));
}

static ui_node_t *studio_find_output(ui_node_canvas_t *c) {
	for (i32 i = 0; i < c->nodes->length; ++i) {
		if (string_equals(c->nodes->buffer[i]->type, "OUTPUT_MATERIAL_PBR")) {
			return c->nodes->buffer[i];
		}
	}
	return NULL;
}

// The studio nodes of the canvas, true when it is a studio material: an output and layers from 0 up, each with its ramp,
// roughness, height and mix nodes
bool studio_graph_read(ui_node_canvas_t *c, studio_graph_t *g) {
	memset(g, 0, sizeof(studio_graph_t));
	if (c == NULL) {
		return false;
	}
	g->output = studio_find_output(c);
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n    = c->nodes->buffer[i];
		i32        role = studio_role(n);
		i32        k    = studio_layer(n);
		if (role > STUDIO_ROLE_NONE && role < STUDIO_ROLE_COUNT && k < STUDIO_LAYERS && g->layers[k].n[role] == NULL) {
			g->layers[k].n[role] = n;
		}
	}
	for (i32 k = 0; k < STUDIO_LAYERS; ++k) {
		studio_layer_t *l = &g->layers[k];
		if (l->n[STUDIO_ROLE_RAMP] == NULL || l->n[STUDIO_ROLE_ROUGH] == NULL || l->n[STUDIO_ROLE_HEIGHT] == NULL || l->n[STUDIO_ROLE_MIX] == NULL ||
		    l->n[STUDIO_ROLE_METAL] == NULL || l->n[STUDIO_ROLE_OCC] == NULL || l->n[STUDIO_ROLE_ALPHA] == NULL) {
			break;
		}
		g->count = k + 1;
	}
	return g->output != NULL && g->count > 0;
}

bool studio_graph_is(ui_node_canvas_t *c) {
	studio_graph_t g;
	return studio_graph_read(c, &g);
}

// ███████╗██████╗ ██╗████████╗
// ██╔════╝██╔══██╗██║╚══██╔══╝
// █████╗  ██║  ██║██║   ██║
// ██╔══╝  ██║  ██║██║   ██║
// ███████╗██████╔╝██║   ██║
// ╚══════╝╚═════╝ ╚═╝   ╚═╝

static ui_node_canvas_t *sg_c = NULL;

static void sg_unlink(ui_node_t *to, i32 in) {
	for (i32 i = sg_c->links->length - 1; i >= 0; --i) {
		ui_node_link_t *l = sg_c->links->buffer[i];
		if (l->to_id == to->id && l->to_socket == in) {
			array_splice(sg_c->links, i, 1);
		}
	}
}

static void sg_link(ui_node_t *from, i32 out, ui_node_t *to, i32 in) {
	sg_unlink(to, in);
	any_array_push(sg_c->links, project_create_node_link(sg_c->links, from->id, out, to->id, in));
}

static void sg_delete(ui_node_t *n) {
	if (n == NULL) {
		return;
	}
	for (i32 i = sg_c->links->length - 1; i >= 0; --i) {
		ui_node_link_t *l = sg_c->links->buffer[i];
		if (l->from_id == n->id || l->to_id == n->id) {
			array_splice(sg_c->links, i, 1);
		}
	}
	i32 i = array_index_of(sg_c->nodes, n);
	if (i >= 0) {
		array_splice(sg_c->nodes, i, 1);
	}
}

#define STUDIO_LAYER_H 1400 // Rows of nodes a layer takes in the node editor, a material mix is tall

// Columns from the left: pattern, cell variation, ramp / roughness / height / metallic / occlusion, mask, mix, then the output
static void sg_place(ui_node_t *n, ui_node_t *out, i32 role, i32 k) {
	f32 ox    = out != NULL ? out->x : 900;
	f32 oy    = out != NULL ? out->y : 100;
	f32 col[] = {0, 5, 4, 3, 3, 3, 2, 1, 3, 3, 3};
	f32 row[] = {0, 120, 0, 0, 220, 440, 140, 0, 600, 820, 1040};
	n->x      = ox - col[role] * 240;
	n->y      = oy + k * STUDIO_LAYER_H + row[role];
}

static ui_node_t *sg_node(studio_graph_t *g, char *type, i32 role, i32 k) {
	ui_node_t *n = ui_nodes_make_node(nodes_material_get_node_t(type), NULL, sg_c);
	n->flags     = UI_NODE_FLAG_NONE;
	studio_tag(n, role, k);
	sg_place(n, g->output, role, k);
	any_array_push(sg_c->nodes, n);
	g->layers[k].n[role] = n;
	return n;
}

static f32 *sg_in(ui_node_t *n, i32 i) {
	return n->inputs->buffer[i]->default_value->buffer;
}

static void sg_button(ui_node_t *n, i32 b, f32 v) {
	n->buttons->buffer[b]->default_value->buffer[0] = v;
}

static i32 sg_output_input(ui_node_t *out, char *name) {
	for (i32 i = 0; i < out->inputs->length; ++i) {
		if (string_equals(out->inputs->buffer[i]->name, name)) {
			return i;
		}
	}
	return -1;
}

static bool sg_linked(ui_node_t *to, i32 in) {
	for (i32 i = 0; i < sg_c->links->length; ++i) {
		ui_node_link_t *l = sg_c->links->buffer[i];
		if (l->to_id == to->id && l->to_socket == in) {
			return true;
		}
	}
	return false;
}

// Links of a layer: pattern to cell variation, ramp, roughness and height, those to the mix, the mask to the mix
static void sg_wire_layer(studio_graph_t *g, i32 k) {
	ui_node_t **n    = g->layers[k].n;
	ui_node_t  *pat  = n[STUDIO_ROLE_PATTERN];
	ui_node_t  *mix  = n[STUDIO_ROLE_MIX];
	ui_node_t  *mask = n[STUDIO_ROLE_MASK];
	if (pat != NULL) {
		if (n[STUDIO_ROLE_CELLVAR] != NULL) {
			sg_link(pat, 0, n[STUDIO_ROLE_CELLVAR], 1);
			sg_link(pat, 1, n[STUDIO_ROLE_CELLVAR], 2);
			sg_link(n[STUDIO_ROLE_CELLVAR], 0, n[STUDIO_ROLE_RAMP], 0);
		}
		else {
			sg_link(pat, 0, n[STUDIO_ROLE_RAMP], 0);
		}
		sg_link(pat, 0, n[STUDIO_ROLE_ROUGH], 0);
		sg_link(pat, 0, n[STUDIO_ROLE_HEIGHT], 0);
		sg_link(pat, 0, n[STUDIO_ROLE_METAL], 0);
		sg_link(pat, 0, n[STUDIO_ROLE_OCC], 0);
		sg_link(pat, 0, n[STUDIO_ROLE_ALPHA], 0);
	}
	else {
		sg_unlink(n[STUDIO_ROLE_RAMP], 0);
		sg_unlink(n[STUDIO_ROLE_ROUGH], 0);
		sg_unlink(n[STUDIO_ROLE_HEIGHT], 0);
		sg_unlink(n[STUDIO_ROLE_METAL], 0);
		sg_unlink(n[STUDIO_ROLE_OCC], 0);
		sg_unlink(n[STUDIO_ROLE_ALPHA], 0);
	}
	sg_link(n[STUDIO_ROLE_RAMP], 0, mix, MIX_IN_B + 0);
	sg_link(n[STUDIO_ROLE_ROUGH], 0, mix, MIX_IN_B + 1);
	sg_link(n[STUDIO_ROLE_METAL], 0, mix, MIX_IN_B + 2);
	sg_link(n[STUDIO_ROLE_HEIGHT], 0, mix, MIX_IN_B + 3);
	sg_link(n[STUDIO_ROLE_OCC], 0, mix, MIX_IN_B + 4);
	sg_link(n[STUDIO_ROLE_ALPHA], 0, mix, MIX_IN_B + 7);
	if (mask == NULL || k == 0) {
		sg_unlink(mix, MIX_IN_MASK);
		return;
	}
	sg_link(mask, 0, mix, MIX_IN_MASK);
	if (string_equals(mask->type, "HEIGHT_MASK")) { // The layer's own pattern
		if (pat != NULL) {
			sg_link(pat, 0, mask, 0);
		}
		else {
			sg_unlink(mask, 0);
		}
	}
	else if (string_equals(mask->type, "CURVATURE_MASK")) { // Dirt also settles in the recesses of the base pattern
		ui_node_t *base = g->layers[0].n[STUDIO_ROLE_PATTERN];
		if (base != NULL) {
			sg_link(base, 0, mask, 4);
		}
		else {
			sg_unlink(mask, 4);
		}
	}
}

// All the links the studio owns, from the layers up to the output
static void sg_wire(studio_graph_t *g) {
	for (i32 k = 0; k < g->count; ++k) {
		sg_wire_layer(g, k);
		ui_node_t *mix = g->layers[k].n[STUDIO_ROLE_MIX];
		for (i32 ch = 0; ch < MIX_CHANNELS; ++ch) {
			if (k > 0) {
				sg_link(g->layers[k - 1].n[STUDIO_ROLE_MIX], ch, mix, MIX_IN_A + ch);
			}
			else {
				sg_unlink(mix, MIX_IN_A + ch);
			}
		}
	}
	if (g->output == NULL) {
		return;
	}
	ui_node_t *top     = g->layers[g->count - 1].n[STUDIO_ROLE_MIX];
	char      *names[] = {"Base Color", "Roughness", "Metallic", "Height", "Occlusion", "Emission", "Subsurface", "Opacity"};
	for (i32 ch = 0; ch < MIX_CHANNELS; ++ch) {
		i32 in = sg_output_input(g->output, names[ch]);
		if (in >= 0) {
			sg_link(top, ch, g->output, in);
		}
	}
}

static void sg_begin(ui_node_canvas_t *c, studio_graph_t *g) {
	sg_c = c;
	studio_graph_read(c, g);
}

static void sg_end(studio_graph_t *g) {
	studio_graph_read(sg_c, g);
	sg_wire(g);
	sg_c                  = NULL;
}

static void sg_ramp(ui_node_t *ramp, i32 count, f32 (*color)[3], f32 *pos) {
	f32_array_t *v = ramp->buttons->buffer[0]->default_value;
	v->length      = 0;
	for (i32 i = 0; i < count; ++i) {
		f32_array_push(v, color[i][0]);
		f32_array_push(v, color[i][1]);
		f32_array_push(v, color[i][2]);
		f32_array_push(v, 1.0);
		f32_array_push(v, pos[i]);
	}
}

// The nodes every layer has, a plain one without pattern
static void sg_create_layer(studio_graph_t *g, i32 k) {
	ui_node_t *ramp = sg_node(g, "VALTORGB", STUDIO_ROLE_RAMP, k);
	sg_in(ramp, 0)[0] = 1.0; // Without pattern the surface color: the stop at the right
	f32 col[2][3]   = {{0.4, 0.4, 0.4}, {0.8, 0.8, 0.8}};
	f32 pos[2]      = {0.0, 1.0};
	sg_ramp(ramp, 2, col, pos);

	ui_node_t *rough = sg_node(g, "MAPRANGE", STUDIO_ROLE_ROUGH, k);
	sg_in(rough, 0)[0] = 1.0;
	sg_in(rough, 3)[0] = 0.5; // Recesses
	sg_in(rough, 4)[0] = 0.5; // Surface
	sg_button(rough, 0, 1);   // Clamp

	ui_node_t *height = sg_node(g, "MATH", STUDIO_ROLE_HEIGHT, k);
	sg_button(height, 0, 2); // Multiply
	sg_in(height, 0)[0] = 0.0;
	sg_in(height, 1)[0] = 0.0;

	ui_node_t *metal = sg_node(g, "MAPRANGE", STUDIO_ROLE_METAL, k);
	sg_in(metal, 0)[0] = 1.0;
	sg_in(metal, 3)[0] = 0.0; // Recesses
	sg_in(metal, 4)[0] = 0.0; // Surface
	sg_button(metal, 0, 1);

	ui_node_t *occ = sg_node(g, "MAPRANGE", STUDIO_ROLE_OCC, k);
	sg_in(occ, 0)[0] = 1.0;
	sg_in(occ, 3)[0] = 1.0; // Recesses, darker with cavity
	sg_in(occ, 4)[0] = 1.0;
	sg_button(occ, 0, 1);

	ui_node_t *alpha = sg_node(g, "MAPRANGE", STUDIO_ROLE_ALPHA, k);
	sg_in(alpha, 0)[0] = 1.0;
	sg_in(alpha, 1)[0] = -2.0; // No cutout: always opaque
	sg_in(alpha, 2)[0] = -1.0;
	sg_in(alpha, 3)[0] = 0.0;
	sg_in(alpha, 4)[0] = 1.0;
	sg_button(alpha, 0, 1);

	sg_node(g, "MATERIAL_MIX", STUDIO_ROLE_MIX, k);
}

void studio_graph_create(ui_node_canvas_t *c) {
	studio_graph_t g;
	sg_c = c;
	for (i32 i = c->nodes->length - 1; i >= 0; --i) { // Only the output stays
		if (!string_equals(c->nodes->buffer[i]->type, "OUTPUT_MATERIAL_PBR")) {
			array_splice(c->nodes, i, 1);
		}
	}
	c->links->length = 0;
	studio_graph_read(c, &g);
	sg_create_layer(&g, 0);
	sg_end(&g);
}

static ui_node_t *studio_mix_rgb(studio_graph_t *g, i32 k) {
	ui_node_t *cv = sg_node(g, "MIX_RGB", STUDIO_ROLE_CELLVAR, k);
	sg_button(cv, 0, 2); // Multiply: the height times the cell value, recesses keep their color
	sg_button(cv, 1, 1); // Clamp factor
	sg_in(cv, 0)[0] = 0.0;
	return cv;
}

// Pattern node type, NULL for none
void studio_graph_set_pattern(ui_node_canvas_t *c, i32 k, char *type) {
	studio_graph_t g;
	sg_begin(c, &g);
	if (k >= g.count) {
		sg_c = NULL;
		return;
	}
	ui_node_t **n   = g.layers[k].n;
	ui_node_t  *old = n[STUDIO_ROLE_PATTERN];
	if (type == NULL) {
		sg_delete(old);
		sg_delete(n[STUDIO_ROLE_CELLVAR]);
		n[STUDIO_ROLE_PATTERN] = n[STUDIO_ROLE_CELLVAR] = NULL;
		sg_in(n[STUDIO_ROLE_RAMP], 0)[0]   = 1.0;
		sg_in(n[STUDIO_ROLE_ROUGH], 0)[0]  = 1.0;
		sg_in(n[STUDIO_ROLE_HEIGHT], 0)[0] = 0.0;
		sg_in(n[STUDIO_ROLE_METAL], 0)[0]  = 1.0;
		sg_in(n[STUDIO_ROLE_OCC], 0)[0]    = 1.0;
		sg_in(n[STUDIO_ROLE_ALPHA], 0)[0]  = 1.0;
		ui_node_t *mask = n[STUDIO_ROLE_MASK];
		if (mask != NULL && string_equals(mask->type, "HEIGHT_MASK")) { // A pattern mask needs the pattern
			sg_delete(mask);
		}
	}
	else if (old == NULL || !string_equals(old->type, type)) {
		ui_node_t *pat = sg_node(&g, type, STUDIO_ROLE_PATTERN, k);
		if (old != NULL) { // The shared inputs, the mapping and links the user made carry over
			for (i32 i = 0; i < 8 && i < old->inputs->length && i < pat->inputs->length; ++i) {
				if (i != 1) { // Each pattern has its own scale
					sg_in(pat, i)[0] = sg_in(old, i)[0];
				}
			}
			sg_button(pat, 0, old->buttons->buffer[0]->default_value->buffer[0]);
			pat->x = old->x;
			pat->y = old->y;
			for (i32 i = 0; i < sg_c->links->length; ++i) {
				ui_node_link_t *l = sg_c->links->buffer[i];
				if (l->from_id == old->id && l->from_socket < 2) {
					l->from_id = pat->id;
				}
				if (l->to_id == old->id && l->to_socket < 8) {
					l->to_id = pat->id;
				}
			}
			sg_delete(old);
		}
		if (n[STUDIO_ROLE_CELLVAR] == NULL) {
			studio_mix_rgb(&g, k);
		}
	}
	sg_end(&g);
}

void studio_graph_set_mask(ui_node_canvas_t *c, i32 k, i32 mask) {
	studio_graph_t g;
	sg_begin(c, &g);
	if (k < 1 || k >= g.count) {
		sg_c = NULL;
		return;
	}
	ui_node_t *old    = g.layers[k].n[STUDIO_ROLE_MASK];
	f32        amount = old != NULL ? studio_graph_get(c, k, STUDIO_VALUE_MASK_AMOUNT) : 0.5;
	f32        soft   = old != NULL ? studio_graph_get(c, k, STUDIO_VALUE_MASK_SOFT) : 0.35;
	f32        brk    = old != NULL ? studio_graph_get(c, k, STUDIO_VALUE_MASK_BREAKUP) : 0.5;
	sg_delete(old);
	g.layers[k].n[STUDIO_ROLE_MASK] = NULL;
	if (mask == STUDIO_MASK_PATTERN && g.layers[k].n[STUDIO_ROLE_PATTERN] == NULL) {
		mask = STUDIO_MASK_FULL;
	}
	ui_node_t *m = NULL;
	if (mask == STUDIO_MASK_EDGES || mask == STUDIO_MASK_CAVITIES) {
		m = sg_node(&g, "CURVATURE_MASK", STUDIO_ROLE_MASK, k);
		sg_button(m, 0, mask == STUDIO_MASK_CAVITIES ? 1 : 0);
	}
	else if (mask == STUDIO_MASK_UP) {
		m = sg_node(&g, "DIRECTION_MASK", STUDIO_ROLE_MASK, k);
	}
	else if (mask == STUDIO_MASK_PATTERN) {
		m = sg_node(&g, "HEIGHT_MASK", STUDIO_ROLE_MASK, k);
	}
	sg_end(&g);
	if (m != NULL) {
		studio_graph_set(c, k, STUDIO_VALUE_MASK_AMOUNT, amount);
		studio_graph_set(c, k, STUDIO_VALUE_MASK_SOFT, soft);
		studio_graph_set(c, k, STUDIO_VALUE_MASK_BREAKUP, brk);
	}
}

i32 studio_graph_mask(ui_node_canvas_t *c, i32 k) {
	studio_graph_t g;
	studio_graph_read(c, &g);
	ui_node_t *m = k > 0 && k < g.count ? g.layers[k].n[STUDIO_ROLE_MASK] : NULL;
	if (m == NULL) {
		return STUDIO_MASK_FULL;
	}
	if (string_equals(m->type, "CURVATURE_MASK")) {
		return m->buttons->buffer[0]->default_value->buffer[0] > 0.5 ? STUDIO_MASK_CAVITIES : STUDIO_MASK_EDGES;
	}
	if (string_equals(m->type, "DIRECTION_MASK")) {
		return STUDIO_MASK_UP;
	}
	return STUDIO_MASK_PATTERN;
}

// A new plain layer on top, its index, -1 when there are four already
i32 studio_graph_add_layer(ui_node_canvas_t *c) {
	studio_graph_t g;
	sg_begin(c, &g);
	if (g.count >= STUDIO_LAYERS) {
		sg_c = NULL;
		return -1;
	}
	i32 k = g.count;
	sg_create_layer(&g, k);
	sg_end(&g);
	return k;
}

void studio_graph_remove_layer(ui_node_canvas_t *c, i32 k) {
	studio_graph_t g;
	sg_begin(c, &g);
	if (k < 1 || k >= g.count) {
		sg_c = NULL;
		return;
	}
	for (i32 r = 1; r < STUDIO_ROLE_COUNT; ++r) {
		sg_delete(g.layers[k].n[r]);
	}
	for (i32 j = k + 1; j < g.count; ++j) {
		for (i32 r = 1; r < STUDIO_ROLE_COUNT; ++r) {
			ui_node_t *n = g.layers[j].n[r];
			if (n != NULL) {
				studio_tag(n, r, j - 1);
				n->y -= STUDIO_LAYER_H;
			}
		}
	}
	sg_end(&g);
}

// Swaps layer k with the one above (dir 1) or below (dir -1), the base stays at the bottom
void studio_graph_move_layer(ui_node_canvas_t *c, i32 k, i32 dir) {
	studio_graph_t g;
	sg_begin(c, &g);
	i32 j = k + dir;
	if (k < 1 || j < 1 || k >= g.count || j >= g.count) {
		sg_c = NULL;
		return;
	}
	for (i32 r = 1; r < STUDIO_ROLE_COUNT; ++r) {
		ui_node_t *a = g.layers[k].n[r];
		ui_node_t *b = g.layers[j].n[r];
		if (a != NULL) {
			studio_tag(a, r, j);
			a->y += (j - k) * STUDIO_LAYER_H;
		}
		if (b != NULL) {
			studio_tag(b, r, k);
			b->y += (k - j) * STUDIO_LAYER_H;
		}
	}
	sg_end(&g);
}

// ██╗   ██╗ █████╗ ██╗     ██╗   ██╗███████╗███████╗
// ██║   ██║██╔══██╗██║     ██║   ██║██╔════╝██╔════╝
// ██║   ██║███████║██║     ██║   ██║█████╗  ███████╗
// ╚██╗ ██╔╝██╔══██║██║     ██║   ██║██╔══╝  ╚════██║
//  ╚████╔╝ ██║  ██║███████╗╚██████╔╝███████╗███████║
//   ╚═══╝  ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚══════╝╚══════╝

ui_node_t *studio_graph_node(ui_node_canvas_t *c, i32 k, i32 role) {
	studio_graph_t g;
	studio_graph_read(c, &g);
	return k >= 0 && k < g.count && role > 0 && role < STUDIO_ROLE_COUNT ? g.layers[k].n[role] : NULL;
}

i32 studio_graph_layers(ui_node_canvas_t *c) {
	studio_graph_t g;
	studio_graph_read(c, &g);
	return g.count;
}

f32 studio_graph_get(ui_node_canvas_t *c, i32 k, i32 value) {
	studio_graph_t g;
	studio_graph_read(c, &g);
	if (k < 0 || k >= g.count) {
		return 0.0;
	}
	ui_node_t **n    = g.layers[k].n;
	ui_node_t  *mask = n[STUDIO_ROLE_MASK];
	switch (value) {
	case STUDIO_VALUE_ROUGHNESS:
		return (sg_in(n[STUDIO_ROLE_ROUGH], 3)[0] + sg_in(n[STUDIO_ROLE_ROUGH], 4)[0]) * 0.5;
	case STUDIO_VALUE_ROUGH_VAR:
		return sg_in(n[STUDIO_ROLE_ROUGH], 3)[0] - sg_in(n[STUDIO_ROLE_ROUGH], 4)[0];
	case STUDIO_VALUE_METALLIC:
		return sg_in(n[STUDIO_ROLE_METAL], 4)[0];
	case STUDIO_VALUE_METAL_VAR: { // How much less metal the recesses are
		f32 m = sg_in(n[STUDIO_ROLE_METAL], 4)[0];
		return m > 0.0 ? 1.0 - sg_in(n[STUDIO_ROLE_METAL], 3)[0] / m : 0.0;
	}
	case STUDIO_VALUE_CAVITY:
		return 1.0 - sg_in(n[STUDIO_ROLE_OCC], 3)[0];
	case STUDIO_VALUE_EMISSION:
		return sg_in(n[STUDIO_ROLE_MIX], MIX_IN_B + 5)[0];
	case STUDIO_VALUE_SUBSURFACE:
		return sg_in(n[STUDIO_ROLE_MIX], MIX_IN_B + 6)[0];
	case STUDIO_VALUE_CUTOUT: { // Holes where the pattern is below it
		f32 hi = sg_in(n[STUDIO_ROLE_ALPHA], 2)[0];
		return hi < 0.0 ? 0.0 : (sg_in(n[STUDIO_ROLE_ALPHA], 1)[0] + hi) * 0.5;
	}
	case STUDIO_VALUE_CELL_COLORS:
		return n[STUDIO_ROLE_CELLVAR] != NULL && n[STUDIO_ROLE_CELLVAR]->buttons->buffer[0]->default_value->buffer[0] < 1.0 ? 1.0 : 0.0;
	case STUDIO_VALUE_DEPTH:
		return sg_in(n[STUDIO_ROLE_HEIGHT], 1)[0];
	case STUDIO_VALUE_CELL_VAR:
		return n[STUDIO_ROLE_CELLVAR] != NULL ? sg_in(n[STUDIO_ROLE_CELLVAR], 0)[0] : 0.0;
	case STUDIO_VALUE_OPACITY:
		return sg_in(n[STUDIO_ROLE_MIX], MIX_IN_OPACITY)[0];
	}
	if (mask == NULL) {
		return 0.0;
	}
	bool height_mask = string_equals(mask->type, "HEIGHT_MASK");
	if (value == STUDIO_VALUE_MASK_INVERT) {
		return height_mask && mask->buttons->buffer[0]->default_value->buffer[0] > 0.5 ? 1.0 : 0.0;
	}
	if (value == STUDIO_VALUE_MASK_SCALE) {
		return sg_in(mask, height_mask ? 4 : 3)[0];
	}
	i32 in = value == STUDIO_VALUE_MASK_AMOUNT ? 0 : value == STUDIO_VALUE_MASK_SOFT ? 1 : 2;
	return sg_in(mask, in + (height_mask ? 1 : 0))[0];
}

void studio_graph_set(ui_node_canvas_t *c, i32 k, i32 value, f32 v) {
	studio_graph_t g;
	studio_graph_read(c, &g);
	if (k < 0 || k >= g.count) {
		return;
	}
	ui_node_t **n    = g.layers[k].n;
	ui_node_t  *mask = n[STUDIO_ROLE_MASK];
	if (value == STUDIO_VALUE_ROUGHNESS || value == STUDIO_VALUE_ROUGH_VAR) {
		f32 r   = value == STUDIO_VALUE_ROUGHNESS ? v : studio_graph_get(c, k, STUDIO_VALUE_ROUGHNESS);
		f32 var = value == STUDIO_VALUE_ROUGH_VAR ? v : studio_graph_get(c, k, STUDIO_VALUE_ROUGH_VAR);
		if (n[STUDIO_ROLE_PATTERN] == NULL) {
			var = 0.0;
		}
		sg_in(n[STUDIO_ROLE_ROUGH], 3)[0] = r + var * 0.5; // Recesses rougher
		sg_in(n[STUDIO_ROLE_ROUGH], 4)[0] = r - var * 0.5;
		return;
	}
	if (value == STUDIO_VALUE_METALLIC || value == STUDIO_VALUE_METAL_VAR) {
		f32 m   = value == STUDIO_VALUE_METALLIC ? v : studio_graph_get(c, k, STUDIO_VALUE_METALLIC);
		f32 var = value == STUDIO_VALUE_METAL_VAR ? v : studio_graph_get(c, k, STUDIO_VALUE_METAL_VAR);
		sg_in(n[STUDIO_ROLE_METAL], 3)[0] = m * (1.0 - var);
		sg_in(n[STUDIO_ROLE_METAL], 4)[0] = m;
		return;
	}
	switch (value) {
	case STUDIO_VALUE_CAVITY:
		sg_in(n[STUDIO_ROLE_OCC], 3)[0] = 1.0 - v;
		sg_in(n[STUDIO_ROLE_OCC], 4)[0] = 1.0;
		return;
	case STUDIO_VALUE_EMISSION:
		sg_in(n[STUDIO_ROLE_MIX], MIX_IN_B + 5)[0] = v;
		return;
	case STUDIO_VALUE_SUBSURFACE:
		sg_in(n[STUDIO_ROLE_MIX], MIX_IN_B + 6)[0] = v;
		return;
	case STUDIO_VALUE_CUTOUT:
		sg_in(n[STUDIO_ROLE_ALPHA], 1)[0] = v <= 0.0 ? -2.0 : v - 0.03;
		sg_in(n[STUDIO_ROLE_ALPHA], 2)[0] = v <= 0.0 ? -1.0 : v + 0.03;
		return;
	case STUDIO_VALUE_CELL_COLORS: // Mix: the ramp follows the cell value, multiply: a tint over the pattern
		if (n[STUDIO_ROLE_CELLVAR] != NULL) {
			n[STUDIO_ROLE_CELLVAR]->buttons->buffer[0]->default_value->buffer[0] = v > 0.5 ? 0 : 2;
		}
		return;
	case STUDIO_VALUE_DEPTH:
		sg_in(n[STUDIO_ROLE_HEIGHT], 1)[0] = v;
		return;
	case STUDIO_VALUE_CELL_VAR:
		if (n[STUDIO_ROLE_CELLVAR] != NULL) {
			sg_in(n[STUDIO_ROLE_CELLVAR], 0)[0] = v;
		}
		return;
	case STUDIO_VALUE_OPACITY:
		sg_in(n[STUDIO_ROLE_MIX], MIX_IN_OPACITY)[0] = v;
		return;
	}
	if (mask == NULL) {
		return;
	}
	bool height_mask = string_equals(mask->type, "HEIGHT_MASK");
	if (value == STUDIO_VALUE_MASK_INVERT) {
		if (height_mask) {
			mask->buttons->buffer[0]->default_value->buffer[0] = v > 0.5 ? 1 : 0;
		}
		return;
	}
	if (value == STUDIO_VALUE_MASK_SCALE) {
		sg_in(mask, height_mask ? 4 : 3)[0] = v;
		return;
	}
	i32 in                                  = value == STUDIO_VALUE_MASK_AMOUNT ? 0 : value == STUDIO_VALUE_MASK_SOFT ? 1 : 2;
	sg_in(mask, in + (height_mask ? 1 : 0))[0] = v;
}

// How the height of the layer goes over the ones below: 0 mix, 1 add, 2 subtract
i32 studio_graph_height_blend(ui_node_canvas_t *c, i32 k) {
	ui_node_t *mix = studio_graph_node(c, k, STUDIO_ROLE_MIX);
	return mix != NULL ? (i32)mix->buttons->buffer[MIX_BLEND]->default_value->buffer[0] : 0;
}

void studio_graph_set_height_blend(ui_node_canvas_t *c, i32 k, i32 blend) {
	ui_node_t *mix = studio_graph_node(c, k, STUDIO_ROLE_MIX);
	if (mix != NULL) {
		mix->buttons->buffer[MIX_BLEND]->default_value->buffer[0] = blend;
	}
}

// Input of the layer's pattern node by name, false when the pattern has none
bool studio_graph_set_pattern_input(ui_node_canvas_t *c, i32 k, char *name, f32 v) {
	ui_node_t *pat = studio_graph_node(c, k, STUDIO_ROLE_PATTERN);
	for (i32 i = 0; pat != NULL && i < pat->inputs->length; ++i) {
		if (string_equals(pat->inputs->buffer[i]->name, name)) {
			pat->inputs->buffer[i]->default_value->buffer[0] = v;
			return true;
		}
	}
	return false;
}

void studio_graph_set_mapping(ui_node_canvas_t *c, i32 k, bool triplanar) {
	ui_node_t *pat = studio_graph_node(c, k, STUDIO_ROLE_PATTERN);
	if (pat != NULL) {
		pat->buttons->buffer[0]->default_value->buffer[0] = triplanar ? 1 : 0;
	}
}

// Stops of the layer colors: count colors at their positions, hard steps between them or a smooth gradient
void studio_graph_set_ramp(ui_node_canvas_t *c, i32 k, i32 count, f32 (*color)[3], f32 *pos, bool hard) {
	ui_node_t *ramp = studio_graph_node(c, k, STUDIO_ROLE_RAMP);
	if (ramp == NULL) {
		return;
	}
	sg_ramp(ramp, count, color, pos);
	ramp->buttons->buffer[0]->data->buffer[0] = hard ? 1 : 0;
}

bool studio_graph_ramp_hard(ui_node_canvas_t *c, i32 k) {
	ui_node_t *ramp = studio_graph_node(c, k, STUDIO_ROLE_RAMP);
	return ramp != NULL && ramp->buttons->buffer[0]->data->buffer[0] == 1;
}

void studio_graph_set_ramp_hard(ui_node_canvas_t *c, i32 k, bool hard) {
	ui_node_t *ramp = studio_graph_node(c, k, STUDIO_ROLE_RAMP);
	if (ramp != NULL) {
		ramp->buttons->buffer[0]->data->buffer[0] = hard ? 1 : 0;
	}
}

// The layer name is the name of its Material Mix node, shown in the node editor too
void studio_graph_set_layer_name(ui_node_canvas_t *c, i32 k, char *name) {
	ui_node_t *mix = studio_graph_node(c, k, STUDIO_ROLE_MIX);
	if (mix != NULL) {
		mix->name = string_copy(name);
	}
}

// Channels of the layer that cover the ones below: 0 color, 1 roughness, 2 metallic, 3 height, 4 occlusion, 5 emission, 6 subsurface
bool studio_graph_channel(ui_node_canvas_t *c, i32 k, i32 ch) {
	ui_node_t *mix = studio_graph_node(c, k, STUDIO_ROLE_MIX);
	return mix != NULL && ch < mix->buttons->length && mix->buttons->buffer[ch]->default_value->buffer[0] > 0.5;
}

void studio_graph_set_channel(ui_node_canvas_t *c, i32 k, i32 ch, bool on) {
	ui_node_t *mix = studio_graph_node(c, k, STUDIO_ROLE_MIX);
	if (mix != NULL && ch < mix->buttons->length) {
		mix->buttons->buffer[ch]->default_value->buffer[0] = on ? 1 : 0;
	}
}

// ██████╗ ██████╗ ███████╗███████╗███████╗████████╗███████╗
// ██╔══██╗██╔══██╗██╔════╝██╔════╝██╔════╝╚══██╔══╝██╔════╝
// ██████╔╝██████╔╝█████╗  ███████╗█████╗     ██║   ███████╗
// ██╔═══╝ ██╔══██╗██╔══╝  ╚════██║██╔══╝     ██║   ╚════██║
// ██║     ██║  ██║███████╗███████║███████╗   ██║   ███████║
// ╚═╝     ╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝   ╚═╝   ╚══════╝

#define LP_NONE -1.0 // Keep the pattern default

typedef struct studio_layer_preset {
	char *name;
	char *pattern; // NULL for a plain layer
	f32   scale;
	f32   stretch;
	f32   contrast;
	f32   distort;
	f32   extra[4]; // LP_NONE keeps the default
	i32   stops;
	f32   color[4][3];
	f32   pos[4];
	f32   rough;
	f32   rough_var;
	f32   metal;
	f32   depth;
	f32   cell_var;
	i32   mask;
	f32   mask_amount;
	f32   mask_soft;
	f32   mask_breakup;
	i32   channels; // Bits: color, roughness, metallic, height, occlusion, emission, subsurface
	f32   opacity;
	i32   invert; // A pattern mask where the pattern is low
} studio_layer_preset_t;

#define CH_ALL   255
#define CH_NOH   247 // All but height
#define CH_PAINT 7   // Color, roughness, metallic

// clang-format off
static studio_layer_preset_t studio_layer_presets[] = {
	// Base layers
	{"Plastic",       NULL,                0,   1, 1.0, 0.0,  {LP_NONE, LP_NONE, LP_NONE, LP_NONE}, 1, {{0.80, 0.15, 0.12}},                                       {1.0},           0.35, 0.0,  0.0, 0.0,  0.0, 0, 0, 0, 0, CH_ALL, 1},
	{"Metal",         "PATTERN_NOISE",     10,  1, 1.4, 0.0,  {0.5, LP_NONE, LP_NONE, LP_NONE},     2, {{0.58, 0.58, 0.61}, {0.82, 0.82, 0.85}},                   {0.0, 1.0},      0.30, 0.35, 1.0, 0.01, 0.0, 0, 0, 0, 0, CH_ALL, 1},
	{"Brushed Metal", "PATTERN_NOISE",     4,   40, 1.5, 0.0, {0.6, LP_NONE, LP_NONE, LP_NONE},     2, {{0.66, 0.66, 0.69}, {0.82, 0.82, 0.84}},                   {0.0, 1.0},      0.35, 0.25, 1.0, 0.01, 0.0, 0, 0, 0, 0, CH_ALL, 1},
	{"Wood",          "PATTERN_GRAIN",     3,   1, 1.2, 0.0,  {2.0, 3.0, LP_NONE, LP_NONE},         3, {{0.30, 0.16, 0.07}, {0.45, 0.26, 0.12}, {0.58, 0.36, 0.18}}, {0.0, 0.5, 1.0}, 0.55, 0.2,  0.0, 0.05, 0.0, 0, 0, 0, 0, CH_ALL, 1},
	{"Stone",         "PATTERN_CELLS",     5,   1, 1.3, 0.3,  {0.12, 1.0, 0.3, LP_NONE},            3, {{0.28, 0.27, 0.26}, {0.42, 0.41, 0.39}, {0.55, 0.54, 0.51}}, {0.0, 0.4, 1.0}, 0.80, 0.2,  0.0, 0.25, 0.4, 0, 0, 0, 0, CH_ALL, 1},
	{"Concrete",      "PATTERN_NOISE",     25,  1, 1.0, 0.0,  {0.6, LP_NONE, LP_NONE, LP_NONE},     2, {{0.45, 0.45, 0.43}, {0.62, 0.62, 0.60}},                   {0.0, 1.0},      0.90, 0.15, 0.0, 0.06, 0.0, 0, 0, 0, 0, CH_ALL, 1},
	{"Bricks",        "PATTERN_BRICKS",    6,   1, 1.0, 0.03,  {0.06, 0.06, 0.5, 0.15},              3, {{0.66, 0.64, 0.60}, {0.42, 0.17, 0.11}, {0.60, 0.25, 0.16}}, {0.0, 0.15, 1.0}, 0.85, 0.1,  0.0, 0.20, 0.5, 0, 0, 0, 0, CH_ALL, 1},
	{"Ceramic Tiles", "PATTERN_TILES",     8,   1, 1.0, 0.0,  {0.04, 0.05, 0.08, LP_NONE},          2, {{0.55, 0.55, 0.53}, {0.92, 0.92, 0.90}},                   {0.0, 0.3},      0.30, 0.4,  0.0, 0.15, 0.15, 0, 0, 0, 0, CH_ALL, 1},
	{"Fabric",        "PATTERN_WEAVE",     40,  1, 1.0, 0.0,  {0.9, 0.3, LP_NONE, LP_NONE},         2, {{0.10, 0.16, 0.33}, {0.20, 0.30, 0.55}},                   {0.0, 1.0},      0.90, 0.1,  0.0, 0.10, 0.2, 0, 0, 0, 0, CH_ALL, 1},
	{"Leather",       "PATTERN_CELLS",     70,  1, 0.7, 0.2,  {0.3, 1.0, 0.2, LP_NONE},             2, {{0.24, 0.12, 0.06}, {0.35, 0.18, 0.10}},                   {0.0, 1.0},      0.55, 0.2,  0.0, 0.03, 0.0, 0, 0, 0, 0, CH_ALL, 1},
	{"Rubber",        "PATTERN_NOISE",     60,  1, 0.8, 0.0,  {0.5, LP_NONE, LP_NONE, LP_NONE},     2, {{0.06, 0.06, 0.06}, {0.10, 0.10, 0.10}},                   {0.0, 1.0},      0.85, 0.1,  0.0, 0.02, 0.0, 0, 0, 0, 0, CH_ALL, 1},
	{"Cobblestone",   "PATTERN_PEBBLES",   7,   1, 1.0, 0.15, {0.62, 0.3, 0.3, 0.4},               3, {{0.22, 0.20, 0.18}, {0.42, 0.40, 0.37}, {0.58, 0.56, 0.52}}, {0.0, 0.3, 1.0}, 0.75, 0.3,  0.0, 0.30, 0.5, 0, 0, 0, 0, CH_ALL, 1},
	{"Hex Tiles",     "PATTERN_HEX",       8,   1, 1.0, 0.0,  {0.06, 0.08, 0.1, LP_NONE},           2, {{0.15, 0.15, 0.16}, {0.20, 0.42, 0.45}},                   {0.0, 0.3},      0.25, 0.5,  0.0, 0.15, 0.3, 0, 0, 0, 0, CH_ALL, 1},
	{"Painted",       "PATTERN_NOISE",     6,   1, 0.6, 0.0,  {0.5, LP_NONE, LP_NONE, LP_NONE},     2, {{0.22, 0.40, 0.31}, {0.25, 0.45, 0.35}},                   {0.0, 1.0},      0.45, 0.1,  0.0, 0.0,  0.0, 0, 0, 0, 0, CH_ALL, 1},
	// Layers
	{"Edge Wear",     NULL,                0,   1, 1.0, 0.0,  {LP_NONE, LP_NONE, LP_NONE, LP_NONE}, 1, {{0.80, 0.78, 0.75}},                                       {1.0},           0.35, 0.0,  1.0, 0.0,  0.0, STUDIO_MASK_EDGES,    0.5,  0.35, 0.5, CH_PAINT, 1},
	{"Dirt",          NULL,                0,   1, 1.0, 0.0,  {LP_NONE, LP_NONE, LP_NONE, LP_NONE}, 1, {{0.12, 0.10, 0.08}},                                       {1.0},           0.95, 0.0,  0.0, 0.0,  0.0, STUDIO_MASK_CAVITIES, 0.5,  0.35, 0.5, CH_PAINT, 0.85},
	{"Rust",          "PATTERN_NOISE",     8,   1, 1.8, 0.3,  {0.6, LP_NONE, LP_NONE, LP_NONE},     3, {{0.25, 0.09, 0.03}, {0.48, 0.20, 0.07}, {0.62, 0.33, 0.13}}, {0.0, 0.5, 1.0}, 0.90, 0.1,  0.0, 0.02, 0.0, STUDIO_MASK_PATTERN,  0.45, 0.15, 0.5, CH_ALL, 1},
	{"Moss",          "PATTERN_NOISE",     14,  1, 1.3, 0.2,  {0.6, LP_NONE, LP_NONE, LP_NONE},     2, {{0.12, 0.20, 0.05}, {0.28, 0.40, 0.10}},                   {0.0, 1.0},      0.95, 0.0,  0.0, 0.04, 0.0, STUDIO_MASK_UP,       0.35, 0.35, 0.6, CH_ALL, 1},
	{"Dust",          "PATTERN_NOISE",     20,  1, 0.8, 0.0,  {0.5, LP_NONE, LP_NONE, LP_NONE},     2, {{0.55, 0.52, 0.47}, {0.68, 0.65, 0.60}},                   {0.0, 1.0},      0.95, 0.0,  0.0, 0.0,  0.0, STUDIO_MASK_UP,       0.2,  0.6,  0.5, CH_PAINT, 0.5},
	{"Snow",          "PATTERN_NOISE",     10,  1, 1.0, 0.0,  {0.5, LP_NONE, LP_NONE, LP_NONE},     2, {{0.82, 0.85, 0.90}, {0.97, 0.98, 1.00}},                   {0.0, 1.0},      0.60, 0.2,  0.0, 0.03, 0.0, STUDIO_MASK_UP,       0.4,  0.2,  0.3, CH_ALL, 1},
	{"Scratches",     "PATTERN_SCRATCHES", 6,   1, 1.0, 0.0,  {LP_NONE, LP_NONE, LP_NONE, LP_NONE}, 1, {{0.75, 0.75, 0.77}},                                       {1.0},           0.30, 0.0,  1.0, 0.0,  0.0, STUDIO_MASK_PATTERN,  0.5,  0.3,  0.5, CH_PAINT, 1, 1},
	{"Paint",         NULL,                0,   1, 1.0, 0.0,  {LP_NONE, LP_NONE, LP_NONE, LP_NONE}, 1, {{0.70, 0.20, 0.15}},                                       {1.0},           0.45, 0.0,  0.0, 0.0,  0.0, STUDIO_MASK_FULL,     0.5,  0.35, 0.5, CH_PAINT, 1},
};
// clang-format on

#define STUDIO_LAYER_PRESET_COUNT ((i32)(sizeof(studio_layer_presets) / sizeof(studio_layer_presets[0])))
#define STUDIO_FIRST_LAYER_PRESET 14 // Edge Wear

static i32 studio_layer_preset_index(char *name) {
	for (i32 i = 0; i < STUDIO_LAYER_PRESET_COUNT; ++i) {
		if (string_equals(studio_layer_presets[i].name, name)) {
			return i;
		}
	}
	return -1;
}

// A whole material: its base layer and layers over it
typedef struct studio_preset {
	char *name;
	char *base;
	char *layers[3];
	f32   amounts[3];
} studio_preset_t;

// clang-format off
static studio_preset_t studio_presets[] = {
	{"Plastic",       "Plastic"},
	{"Metal",         "Metal"},
	{"Brushed Metal", "Brushed Metal"},
	{"Painted Metal", "Painted",       {"Edge Wear", "Dirt"},  {0.45, 0.35}},
	{"Rusty Metal",   "Metal",         {"Rust", "Dirt"},       {0.3, 0.4}},
	{"Wood",          "Wood"},
	{"Stone",         "Stone"},
	{"Mossy Stone",   "Stone",         {"Moss"},               {0.35}},
	{"Concrete",      "Concrete",      {"Dirt"},               {0.3}},
	{"Bricks",        "Bricks",        {"Dirt"},               {0.35}},
	{"Cobblestone",   "Cobblestone",   {"Dirt"},               {0.4}},
	{"Ceramic Tiles", "Ceramic Tiles"},
	{"Hex Tiles",     "Hex Tiles"},
	{"Fabric",        "Fabric"},
	{"Leather",       "Leather",       {"Edge Wear"},          {0.3}},
	{"Rubber",        "Rubber"},
};
// clang-format on

#define STUDIO_PRESET_COUNT ((i32)(sizeof(studio_presets) / sizeof(studio_presets[0])))

i32 studio_preset_count() {
	return STUDIO_PRESET_COUNT;
}

char *studio_preset_name(i32 i) {
	return tr(studio_presets[i].name);
}

i32 studio_layer_preset_count() {
	return STUDIO_LAYER_PRESET_COUNT - STUDIO_FIRST_LAYER_PRESET;
}

char *studio_layer_preset_name(i32 i) {
	return tr(studio_layer_presets[STUDIO_FIRST_LAYER_PRESET + i].name);
}

static void studio_apply_layer_preset(ui_node_canvas_t *c, i32 k, studio_layer_preset_t *p) {
	studio_graph_set_pattern(c, k, p->pattern);
	if (k > 0) {
		studio_graph_set_mask(c, k, p->mask);
		studio_graph_set(c, k, STUDIO_VALUE_MASK_INVERT, p->invert);
		studio_graph_set(c, k, STUDIO_VALUE_MASK_AMOUNT, p->mask_amount);
		studio_graph_set(c, k, STUDIO_VALUE_MASK_SOFT, p->mask_soft);
		studio_graph_set(c, k, STUDIO_VALUE_MASK_BREAKUP, p->mask_breakup);
		studio_graph_set(c, k, STUDIO_VALUE_OPACITY, p->opacity);
		for (i32 ch = 0; ch < MIX_CHANNELS; ++ch) {
			studio_graph_set_channel(c, k, ch, (p->channels >> ch) & 1);
		}
	}
	ui_node_t *pat = studio_graph_node(c, k, STUDIO_ROLE_PATTERN);
	if (pat != NULL) {
		ui_node_t *def = nodes_material_get_node_t(pat->type);
		for (i32 i = 1; i < pat->inputs->length; ++i) { // Defaults, then the preset's
			sg_in(pat, i)[0] = def->inputs->buffer[i]->default_value->buffer[0];
		}
		sg_button(pat, 0, 0);
		if (p->scale > 0) {
			sg_in(pat, 1)[0] = p->scale;
		}
		sg_in(pat, 2)[0] = p->stretch;
		sg_in(pat, 4)[0] = p->contrast;
		sg_in(pat, 6)[0] = p->distort;
		for (i32 i = 0; i < 4 && 8 + i < pat->inputs->length; ++i) {
			if (p->extra[i] != LP_NONE) {
				sg_in(pat, 8 + i)[0] = p->extra[i];
			}
		}
	}
	sg_c = c;
	sg_ramp(studio_graph_node(c, k, STUDIO_ROLE_RAMP), p->stops, p->color, p->pos);
	sg_c = NULL;
	studio_graph_set(c, k, STUDIO_VALUE_ROUGHNESS, p->rough);
	studio_graph_set(c, k, STUDIO_VALUE_ROUGH_VAR, p->rough_var);
	studio_graph_set(c, k, STUDIO_VALUE_METALLIC, p->metal);
	studio_graph_set(c, k, STUDIO_VALUE_DEPTH, p->depth);
	studio_graph_set(c, k, STUDIO_VALUE_CELL_VAR, p->cell_var);
}

// Layer preset i of the Add Layer menu on top, the new layer index or -1
i32 studio_graph_add_layer_preset(ui_node_canvas_t *c, i32 i) {
	i32 k = studio_graph_add_layer(c);
	if (k >= 0 && i >= 0 && STUDIO_FIRST_LAYER_PRESET + i < STUDIO_LAYER_PRESET_COUNT) {
		studio_layer_preset_t *p = &studio_layer_presets[STUDIO_FIRST_LAYER_PRESET + i];
		studio_apply_layer_preset(c, k, p);
		studio_graph_node(c, k, STUDIO_ROLE_MIX)->name = string_copy(tr(p->name)); // The layer name
	}
	return k;
}

// Replaces the whole graph of the studio material with preset i
void studio_graph_apply_preset(ui_node_canvas_t *c, i32 i) {
	if (i < 0 || i >= STUDIO_PRESET_COUNT) {
		return;
	}
	studio_preset_t *p = &studio_presets[i];
	while (studio_graph_layers(c) > 1) {
		studio_graph_remove_layer(c, studio_graph_layers(c) - 1);
	}
	studio_apply_layer_preset(c, 0, &studio_layer_presets[studio_layer_preset_index(p->base)]);
	for (i32 j = 0; j < 3 && p->layers[j] != NULL; ++j) {
		i32 k = studio_graph_add_layer(c);
		if (k < 0) {
			break;
		}
		studio_layer_preset_t *lp = &studio_layer_presets[studio_layer_preset_index(p->layers[j])];
		studio_apply_layer_preset(c, k, lp);
		studio_graph_node(c, k, STUDIO_ROLE_MIX)->name = string_copy(tr(lp->name));
		studio_graph_set(c, k, STUDIO_VALUE_MASK_AMOUNT, p->amounts[j]);
	}
}
