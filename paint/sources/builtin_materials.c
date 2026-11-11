
#include "global.h"

// Built-in procedural materials shown at the top of the Library tab (creatures and sci-fi). Each one is a material with a single
// group node wired into the Material Output: the group inputs are the parameters (sliders and colors in the node editor), the group
// holds the node graph. Previews are rendered once and cached in <save>/builtin.

#define BUILTIN_VERSION 2 // Bump to re-render the cached previews after changing a preset

// ██████╗ ██╗   ██╗██╗██╗     ██████╗ ███████╗██████╗
// ██╔══██╗██║   ██║██║██║     ██╔══██╗██╔════╝██╔══██╗
// ██████╔╝██║   ██║██║██║     ██║  ██║█████╗  ██████╔╝
// ██╔══██╗██║   ██║██║██║     ██║  ██║██╔══╝  ██╔══██╗
// ██████╔╝╚██████╔╝██║███████╗██████╔╝███████╗██║  ██║
// ╚═════╝  ╚═════╝ ╚═╝╚══════╝╚═════╝ ╚══════╝╚═╝  ╚═╝

// A node input: a constant value, a constant color / vector, or the output of a node
typedef struct bm {
	ui_node_t *n;
	i32        out;
	f32        v[3];
	i32        kind; // 1 value, 2 color, 3 link
} bm_t;

static ui_node_canvas_t *bm_c;
static ui_node_t        *bm_gin;
static ui_node_t        *bm_gout;
static bm_t              bm_uv;

enum { M_ADD = 0, M_SUB = 1, M_MUL = 2, M_DIV = 3, M_POW = 4, M_ABS = 8, M_MIN = 10, M_MAX = 11, M_GT = 13, M_FLOOR = 16 };
enum { V_ADD = 0, V_SUB = 1, V_MUL = 2 };
enum { MIX_MIX = 0, MIX_MULTIPLY = 2, MIX_SCREEN = 5, MIX_ADD = 7, MIX_OVERLAY = 8 };

static bm_t K(f32 v) {
	return (bm_t){.v = {v, v, v}, .kind = 1};
}

static bm_t C(f32 r, f32 g, f32 b) {
	return (bm_t){.v = {r, g, b}, .kind = 2};
}

static bm_t L(ui_node_t *n, i32 out) {
	return (bm_t){.n = n, .out = out, .kind = 3};
}

static ui_node_t *bm_node(char *type) {
	ui_node_t *n = ui_nodes_make_node(nodes_material_get_node_t(type), NULL, bm_c);
	n->flags     = UI_NODE_FLAG_NONE;
	any_array_push(bm_c->nodes, n);
	return n;
}

static void bm_in(ui_node_t *n, i32 i, bm_t r) {
	if (r.kind == 3) {
		any_array_push(bm_c->links, project_create_node_link(bm_c->links, r.n->id, r.out, n->id, i));
		return;
	}
	f32_array_t *d = n->inputs->buffer[i]->default_value;
	for (i32 j = 0; j < d->length && j < 3; ++j) {
		d->buffer[j] = r.v[r.kind == 2 ? j : 0];
	}
}

static void bm_button(ui_node_t *n, i32 b, f32 v) {
	n->buttons->buffer[b]->default_value->buffer[0] = v;
}

// Group parameter, shown on the group node as a slider (VALUE) or a color swatch (RGBA)
static bm_t P(char *name, f32 min, f32 max, f32 v) {
	ui_node_socket_t *s = nodes_material_create_socket(NULL, bm_gin, name, "VALUE", bm_c, min, max, f32_array_create_x(v));
	any_array_push(bm_gin->outputs, s);
	return L(bm_gin, bm_gin->outputs->length - 1);
}

static bm_t PC(char *name, f32 r, f32 g, f32 b) {
	ui_node_socket_t *s = nodes_material_create_socket(NULL, bm_gin, name, "RGBA", bm_c, 0.0, 1.0, f32_array_create_xyzw(r, g, b, 1.0));
	any_array_push(bm_gin->outputs, s);
	return L(bm_gin, bm_gin->outputs->length - 1);
}

// Group output, linked to the Material Output socket of the same name
static void OUT(char *name, char *type, bm_t r) {
	ui_node_socket_t *s = nodes_material_create_socket(NULL, bm_gout, name, type, bm_c, 0.0, 1.0, NULL);
	any_array_push(bm_gout->inputs, s);
	bm_in(bm_gout, bm_gout->inputs->length - 1, r);
}

static bm_t math(i32 op, bm_t a, bm_t b) {
	ui_node_t *n = bm_node("MATH");
	bm_button(n, 0, op);
	bm_in(n, 0, a);
	bm_in(n, 1, b);
	return L(n, 0);
}

static bm_t add(bm_t a, bm_t b) {
	return math(M_ADD, a, b);
}

static bm_t sub(bm_t a, bm_t b) {
	return math(M_SUB, a, b);
}

static bm_t mul(bm_t a, bm_t b) {
	return math(M_MUL, a, b);
}

static bm_t vmax(bm_t a, bm_t b) {
	return math(M_MAX, a, b);
}

// Clamped linear remap of v from [a, b] to [c, d]
static bm_t remap(bm_t v, bm_t a, bm_t b, f32 c, f32 d) {
	ui_node_t *n = bm_node("MAPRANGE");
	bm_in(n, 0, v);
	bm_in(n, 1, a);
	bm_in(n, 2, b);
	bm_in(n, 3, K(c));
	bm_in(n, 4, K(d));
	bm_button(n, 0, 1);
	return L(n, 0);
}

// floor(v * bands) / bands, flat color bands for the stylized look
static bm_t steps(bm_t v, bm_t bands) {
	return math(M_DIV, math(M_FLOOR, mul(v, bands), K(0)), bands);
}

static bm_t mix(i32 blend, bm_t fac, bm_t a, bm_t b) {
	ui_node_t *n = bm_node("MIX_RGB");
	bm_button(n, 0, blend);
	bm_button(n, 1, 1); // Clamp factor
	bm_in(n, 0, fac);
	bm_in(n, 1, a);
	bm_in(n, 2, b);
	return L(n, 0);
}

static bm_t lerp_col(bm_t fac, bm_t a, bm_t b) {
	return mix(MIX_MIX, fac, a, b);
}

static bm_t vmath(i32 op, bm_t a, bm_t b) {
	ui_node_t *n = bm_node("VECT_MATH");
	bm_button(n, 0, op);
	bm_in(n, 0, a);
	bm_in(n, 1, b);
	return L(n, 0);
}

static bm_t combine(bm_t x, bm_t y, bm_t z) {
	ui_node_t *n = bm_node("COMBXYZ");
	bm_in(n, 0, x);
	bm_in(n, 1, y);
	bm_in(n, 2, z);
	return L(n, 0);
}

// 2D Voronoi, outputs: 0 distance, 1 random cell color
static ui_node_t *voronoi(bm_t co, bm_t scale, bm_t randomness, bool f2) {
	ui_node_t *n = bm_node("TEX_VORONOI");
	bm_button(n, 0, 0); // 2D
	bm_button(n, 1, f2 ? 1 : 0);
	bm_in(n, 0, co);
	bm_in(n, 1, scale);
	bm_in(n, 2, K(0));
	bm_in(n, 5, randomness);
	return n;
}

// Outputs: 0 factor, 1 color
static ui_node_t *noise(bm_t co, bm_t scale, f32 detail, f32 roughness, f32 distortion) {
	ui_node_t *n = bm_node("TEX_NOISE");
	bm_in(n, 0, co);
	bm_in(n, 1, scale);
	bm_in(n, 2, K(detail));
	bm_in(n, 3, K(roughness));
	bm_in(n, 5, K(distortion));
	return n;
}

// Outputs: 0 color, 1 factor. Direction 0 X, 1 Y, 3 diagonal. Profile 0 sine, 1 saw, 2 triangle
static ui_node_t *wave(bm_t co, bm_t scale, bm_t distortion, f32 detail_scale, i32 direction, i32 profile) {
	ui_node_t *n = bm_node("TEX_WAVE");
	bm_button(n, 0, 0); // Bands
	bm_button(n, 1, direction);
	bm_button(n, 2, profile);
	bm_in(n, 0, co);
	bm_in(n, 1, scale);
	bm_in(n, 2, distortion);
	bm_in(n, 3, K(detail_scale));
	return n;
}

// 2D Gabor, output 0 value (about -1..1)
static ui_node_t *gabor(bm_t co, bm_t scale, f32 frequency, f32 anisotropy, f32 orientation) {
	ui_node_t *n = bm_node("TEX_GABOR");
	bm_button(n, 0, 0); // 2D
	bm_in(n, 0, co);
	bm_in(n, 1, scale);
	bm_in(n, 2, K(frequency));
	bm_in(n, 3, K(anisotropy));
	bm_in(n, 5, K(orientation));
	return n;
}

// Outputs: 0 color (panel tint between c1 and c2, mortar color in the seams), 1 factor (1 in the seams)
static ui_node_t *brick(bm_t co, bm_t c1, bm_t c2, bm_t cm, bm_t scale, bm_t mortar, f32 smooth, bm_t width, f32 row_height, f32 offset) {
	ui_node_t *n = bm_node("TEX_BRICK");
	bm_in(n, 0, co);
	bm_in(n, 1, c1);
	bm_in(n, 2, c2);
	bm_in(n, 3, cm);
	bm_in(n, 4, scale);
	bm_in(n, 5, mortar);
	bm_in(n, 6, K(smooth));
	bm_in(n, 7, K(0));
	bm_in(n, 8, width);
	bm_in(n, 9, K(row_height));
	bm_in(n, 10, K(offset));
	bm_in(n, 11, K(2));
	bm_in(n, 12, K(1));
	bm_in(n, 13, K(2));
	return n;
}

// UV coordinates moved by the seed
static bm_t bm_coords(bm_t seed) {
	ui_node_t *tc = bm_node("TEX_COORD");
	bm_uv         = L(tc, 2);
	return vmath(V_ADD, bm_uv, mul(seed, K(7.31)));
}

static ui_node_t *bm_io_node(i32 id, char *name, char *type, char *button) {
	return ALLOC_INIT(ui_node_t, {.id      = id,
	                              .name    = name,
	                              .type    = type,
	                              .x       = 0,
	                              .y       = 0,
	                              .color   = 0xff448c6d,
	                              .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                              .outputs = any_array_create_from_raw((void *[]){}, 0),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_button_t, {.name          = button,
	                                                                    .type          = "CUSTOM",
	                                                                    .height        = 1,
	                                                                    .output        = -1,
	                                                                    .default_value = f32_array_create_x(0.0)}),
	                                  },
	                                  1),
	                              .width   = 0,
	                              .flags   = 0});
}

static void bm_begin(char *name) {
	bm_c    = ALLOC_INIT(ui_node_canvas_t, {.name  = string_copy(name),
	                                        .nodes = any_array_create_from_raw((void *[]){}, 0),
	                                        .links = any_array_create_from_raw((void *[]){}, 0)});
	bm_gin  = bm_io_node(0, _tr("Group Input"), "GROUP_INPUT", "nodes_material_group_input_button");
	bm_gout = bm_io_node(1, _tr("Group Output"), "GROUP_OUTPUT", "nodes_material_group_output_button");
	any_array_push(bm_c->nodes, bm_gin);
	any_array_push(bm_c->nodes, bm_gout);
}

// Columns by link depth so the group stays readable when opened
static ui_node_canvas_t *bm_end() {
	i32  count = bm_c->nodes->length;
	i32 *depth = calloc(count, sizeof(i32));
	for (i32 pass = 0; pass < count; ++pass) {
		bool changed = false;
		for (i32 i = 0; i < bm_c->links->length; ++i) {
			ui_node_link_t *l    = bm_c->links->buffer[i];
			i32             from = -1;
			i32             to   = -1;
			for (i32 j = 0; j < count; ++j) {
				if (bm_c->nodes->buffer[j]->id == l->from_id) {
					from = j;
				}
				if (bm_c->nodes->buffer[j]->id == l->to_id) {
					to = j;
				}
			}
			if (from >= 0 && to >= 0 && depth[to] < depth[from] + 1) {
				depth[to] = depth[from] + 1;
				changed   = true;
			}
		}
		if (!changed) {
			break;
		}
	}
	i32 max_depth = 0;
	for (i32 i = 0; i < count; ++i) {
		if (depth[i] > max_depth) {
			max_depth = depth[i];
		}
	}
	for (i32 i = 0; i < count; ++i) {
		if (bm_c->nodes->buffer[i] == bm_gout) {
			depth[i] = max_depth + 1;
		}
	}
	f32 *col_y = calloc(max_depth + 2, sizeof(f32));
	for (i32 i = 0; i < count; ++i) {
		ui_node_t *n = bm_c->nodes->buffer[i];
		n->x         = 50 + depth[i] * 220;
		n->y         = 50 + col_y[depth[i]];
		col_y[depth[i]] += 60 + 28 * (n->inputs->length + n->outputs->length + n->buttons->length);
	}
	free(col_y);
	free(depth);
	ui_nodes_node_changed = NULL;
	return bm_c;
}

// ███████╗ ██████╗ █████╗ ██╗     ███████╗███████╗
// ██╔════╝██╔════╝██╔══██╗██║     ██╔════╝██╔════╝
// ███████╗██║     ███████║██║     █████╗  ███████╗
// ╚════██║██║     ██╔══██║██║     ██╔══╝  ╚════██║
// ███████║╚██████╗██║  ██║███████╗███████╗███████║
// ╚══════╝ ╚═════╝╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝

typedef struct bm_scales {
	f32  base[3], accent[3], groove[3], mark[3];
	f32  size, stretch, randomness, wobble, groove_width, rounding, variation, spots, stripes, bump, roughness, metallic;
	bool stylized;
} bm_scales_t;

static void bm_build_scales(const bm_scales_t *s) {
	bm_t base       = PC(_tr("Base Color"), s->base[0], s->base[1], s->base[2]);
	bm_t accent     = PC(_tr("Accent Color"), s->accent[0], s->accent[1], s->accent[2]);
	bm_t groove_col = PC(_tr("Groove Color"), s->groove[0], s->groove[1], s->groove[2]);
	bm_t mark_col   = PC(_tr("Marking Color"), s->mark[0], s->mark[1], s->mark[2]);
	bm_t size       = P(_tr("Scale Size"), 1, 100, s->size);
	bm_t stretch    = P(_tr("Stretch"), 0.2, 3, s->stretch);
	bm_t randomness = P(_tr("Randomness"), 0, 1, s->randomness);
	bm_t groove_w   = P(_tr("Groove Width"), 0, 0.3, s->groove_width);
	bm_t variation  = P(_tr("Color Variation"), 0, 1, s->variation);
	bm_t spots      = P(_tr("Spots"), 0, 1, s->spots);
	bm_t stripes    = P(_tr("Stripes"), 0, 1, s->stripes);
	bm_t bump       = P(_tr("Bumpiness"), 0, 1, s->bump);
	bm_t rough      = P(_tr("Roughness"), 0, 1, s->roughness);
	bm_t metal      = P(_tr("Metallic"), 0, 1, s->metallic);
	bm_t bands      = s->stylized ? P(_tr("Color Bands"), 1, 8, 3) : K(0);
	bm_t hl_col     = s->stylized ? PC(_tr("Highlight Color"), 1.0, 0.95, 0.75) : K(0);
	bm_t hl         = s->stylized ? P(_tr("Highlight"), 0, 1, 0.6) : K(0);
	bm_t seed       = P(_tr("Seed"), 0, 100, 0);

	bm_t co = bm_coords(seed);
	co      = vmath(V_MUL, co, combine(K(1), stretch, K(1)));
	// Wobbly cell borders
	ui_node_t *wn = noise(co, mul(size, K(0.5)), 2, 0.5, 0);
	bm_t       wo = vmath(V_MUL, vmath(V_SUB, L(wn, 1), K(0.5)), math(M_DIV, K(s->wobble), size));
	co            = vmath(V_ADD, co, wo);

	ui_node_t *v1   = voronoi(co, size, randomness, false);
	ui_node_t *v2   = voronoi(co, size, randomness, true);
	bm_t       edge = sub(L(v2, 0), L(v1, 0)); // 0 on the cell borders
	// 1 in the groove, 0 on the scale
	bm_t groove = remap(edge, K(0), groove_w, 1, 0);
	// Scale profile: rises from the groove to a plateau, plus a dome towards the center
	bm_t plate = remap(edge, groove_w, add(groove_w, K(s->rounding)), 0, 1);
	bm_t dome  = remap(L(v1, 0), K(0), K(0.75), 1, 0);
	bm_t shape = add(mul(plate, K(0.7)), mul(mul(dome, plate), K(0.3)));

	// Per scale tint
	bm_t col = lerp_col(mul(L(v1, 1), variation), base, accent);
	// Markings: soft spots and wavy stripes across the scales
	ui_node_t *sn    = noise(co, mul(size, K(0.12)), 3, 0.55, 0);
	bm_t       spot  = mul(remap(L(sn, 0), K(0.52), K(0.6), 0, 1), spots);
	ui_node_t *wv    = wave(co, mul(size, K(0.08)), K(3), 1.5, 1, 0);
	bm_t       strip = mul(remap(L(wv, 1), K(0.55), K(0.7), 0, 1), stripes);
	bm_t       mark  = vmax(spot, strip);
	col              = lerp_col(mark, col, mark_col);

	if (s->stylized) {
		// Flat bands of shading from the groove to the center, a bright rim and a crisp groove
		bm_t shade = steps(add(mul(shape, K(0.75)), K(0.25)), bands);
		col        = lerp_col(shade, groove_col, col);
		bm_t rim   = mul(mul(remap(L(v1, 0), K(0.05), K(0.3), 1, 0), plate), hl);
		col        = lerp_col(steps(rim, K(2)), col, hl_col);
		col        = lerp_col(math(M_GT, groove, K(0.5)), col, groove_col);
	}
	else {
		col = lerp_col(mul(sub(K(1), plate), K(0.75)), col, groove_col);
	}

	ui_node_t *fine   = noise(co, mul(size, K(4)), 3, 0.6, 0);
	bm_t       height = add(shape, mul(sub(L(fine, 0), K(0.5)), K(s->stylized ? 0.0 : 0.15)));
	OUT(_tr("Base Color"), "RGBA", col);
	OUT(_tr("Roughness"), "VALUE", add(rough, mul(groove, K(0.2))));
	OUT(_tr("Metallic"), "VALUE", mul(metal, plate));
	OUT(_tr("Occlusion"), "VALUE", sub(K(1), mul(groove, K(0.6))));
	OUT(_tr("Height"), "VALUE", mul(height, bump));
}

// ███████╗██╗  ██╗██╗███╗   ██╗
// ██╔════╝██║ ██╔╝██║████╗  ██║
// ███████╗█████╔╝ ██║██╔██╗ ██║
// ╚════██║██╔═██╗ ██║██║╚██╗██║
// ███████║██║  ██╗██║██║ ╚████║
// ╚══════╝╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝

typedef struct bm_skin {
	f32  base[3], blotch[3], vein[3], spot[3];
	f32  blotchiness, veins, pore_size, pore_depth, wrinkles, spots, spot_size, subsurface, roughness, bump;
	bool stylized;
} bm_skin_t;

static void bm_build_skin(const bm_skin_t *s) {
	bm_t base       = PC(_tr("Skin Color"), s->base[0], s->base[1], s->base[2]);
	bm_t blotch_col = PC(_tr("Blotch Color"), s->blotch[0], s->blotch[1], s->blotch[2]);
	bm_t vein_col   = PC(_tr("Vein Color"), s->vein[0], s->vein[1], s->vein[2]);
	bm_t spot_col   = PC(_tr("Spot Color"), s->spot[0], s->spot[1], s->spot[2]);
	bm_t blotchy    = P(_tr("Blotchiness"), 0, 1, s->blotchiness);
	bm_t veins      = P(_tr("Veins"), 0, 1, s->veins);
	bm_t pore_size  = P(_tr("Pore Size"), 0.1, 4, s->pore_size);
	bm_t pore_depth = P(_tr("Pore Depth"), 0, 1, s->pore_depth);
	bm_t wrinkles   = P(_tr("Wrinkles"), 0, 1, s->wrinkles);
	bm_t spots      = P(_tr("Spots"), 0, 1, s->spots);
	bm_t spot_size  = P(_tr("Spot Size"), 1, 60, s->spot_size);
	bm_t subs       = P(_tr("Subsurface"), 0, 1, s->subsurface);
	bm_t bump       = P(_tr("Bumpiness"), 0, 1, s->bump);
	bm_t rough      = P(_tr("Roughness"), 0, 1, s->roughness);
	bm_t bands      = s->stylized ? P(_tr("Color Bands"), 1, 8, 3) : K(0);
	bm_t seed       = P(_tr("Seed"), 0, 100, 0);

	bm_t co = bm_coords(seed);

	ui_node_t *bn     = noise(co, K(5), 4, 0.55, 0);
	bm_t       blotch = mul(remap(L(bn, 0), K(0.4), K(0.75), 0, 1), blotchy);
	// Ridged noise for the veins
	ui_node_t *vn   = noise(co, K(2.5), 3, 0.5, 0.5);
	bm_t       ridg = sub(K(1), math(M_ABS, sub(mul(L(vn, 0), K(2)), K(1)), K(0)));
	bm_t       vein = mul(remap(ridg, K(0.86), K(0.97), 0, 1), veins);
	// Pores: small dark dots
	ui_node_t *pv   = voronoi(co, math(M_DIV, K(250), pore_size), K(1), false);
	bm_t       pore = remap(L(pv, 0), K(0), K(0.22), 1, 0);
	// Wrinkles: fine directional lines
	ui_node_t *gb      = gabor(co, K(40), 2, 0.85, 0.6);
	bm_t       wrinkle = mul(remap(L(gb, 0), K(-0.6), K(0.6), 0, 1), wrinkles);
	// Spots: round patches (freckles, scales of an alien)
	ui_node_t *sv   = voronoi(co, spot_size, K(1), false);
	ui_node_t *sm   = noise(co, mul(spot_size, K(0.3)), 2, 0.5, 0);
	bm_t       spot = mul(mul(remap(L(sv, 0), K(0.25), K(0.35), 1, 0), remap(L(sm, 0), K(0.45), K(0.55), 0, 1)), spots);

	bm_t col = base;
	if (s->stylized) {
		col = lerp_col(steps(blotch, bands), col, blotch_col);
		col = lerp_col(steps(spot, K(1)), col, spot_col);
		col = lerp_col(steps(vein, K(1)), col, vein_col);
	}
	else {
		col = lerp_col(blotch, col, blotch_col);
		col = lerp_col(mul(vein, K(0.5)), col, vein_col);
		col = lerp_col(spot, col, spot_col);
		col = mix(MIX_MULTIPLY, mul(mul(pore, pore_depth), K(0.35)), col, C(0.55, 0.4, 0.35));
	}

	bm_t height = add(K(0.5), mul(sub(L(bn, 0), K(0.5)), K(0.3)));
	height      = sub(height, mul(mul(pore, pore_depth), K(0.35)));
	height      = sub(height, mul(wrinkle, K(0.3)));
	height      = add(height, mul(spot, K(0.25)));
	height      = sub(height, mul(vein, K(-0.1)));
	OUT(_tr("Base Color"), "RGBA", col);
	OUT(_tr("Roughness"), "VALUE", add(rough, mul(pore, K(0.1))));
	OUT(_tr("Height"), "VALUE", mul(height, bump));
	OUT(_tr("Subsurface"), "VALUE", subs);
}

// ██████╗  ██████╗ ███╗   ██╗███████╗
// ██╔══██╗██╔═══██╗████╗  ██║██╔════╝
// ██████╔╝██║   ██║██╔██╗ ██║█████╗
// ██╔══██╗██║   ██║██║╚██╗██║██╔══╝
// ██████╔╝╚██████╔╝██║ ╚████║███████╗
// ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝╚══════╝

typedef struct bm_bone {
	f32  base[3], stain[3], dark[3];
	f32  grain_scale, staining, porosity, cracks, roughness, bump;
	bool stylized;
} bm_bone_t;

static void bm_build_bone(const bm_bone_t *s) {
	bm_t base     = PC(_tr("Bone Color"), s->base[0], s->base[1], s->base[2]);
	bm_t stain    = PC(_tr("Stain Color"), s->stain[0], s->stain[1], s->stain[2]);
	bm_t dark     = PC(_tr("Crack Color"), s->dark[0], s->dark[1], s->dark[2]);
	bm_t grain    = P(_tr("Grain Scale"), 1, 60, s->grain_scale);
	bm_t staining = P(_tr("Staining"), 0, 1, s->staining);
	bm_t porosity = P(_tr("Porosity"), 0, 1, s->porosity);
	bm_t cracks   = P(_tr("Cracks"), 0, 1, s->cracks);
	bm_t bump     = P(_tr("Bumpiness"), 0, 1, s->bump);
	bm_t rough    = P(_tr("Roughness"), 0, 1, s->roughness);
	bm_t bands    = s->stylized ? P(_tr("Color Bands"), 1, 8, 3) : K(0);
	bm_t seed     = P(_tr("Seed"), 0, 100, 0);

	bm_t co = bm_coords(seed);

	// Lengthwise grain
	ui_node_t *gw = wave(co, grain, K(6), 2, 0, 0);
	bm_t       gr = L(gw, 1);
	// Stains
	ui_node_t *sn = noise(co, K(3), 4, 0.6, 0);
	bm_t       st = mul(remap(L(sn, 0), K(0.45), K(0.75), 0, 1), staining);
	// Pits
	ui_node_t *pv  = voronoi(co, K(60), K(1), false);
	ui_node_t *pm  = noise(co, K(6), 2, 0.5, 0);
	bm_t       pit = mul(mul(remap(L(pv, 0), K(0), K(0.2), 1, 0), remap(L(pm, 0), K(0.5), K(0.6), 0, 1)), porosity);
	// Hairline cracks on the cell borders
	ui_node_t *c1    = voronoi(co, K(4), K(1), false);
	ui_node_t *c2    = voronoi(co, K(4), K(1), true);
	ui_node_t *cm    = noise(co, K(8), 2, 0.5, 0);
	bm_t       crack = mul(mul(remap(sub(L(c2, 0), L(c1, 0)), K(0), K(0.025), 1, 0), remap(L(cm, 0), K(0.45), K(0.55), 0, 1)), cracks);

	bm_t col;
	if (s->stylized) {
		col = lerp_col(steps(add(mul(gr, K(0.4)), mul(st, K(0.6))), bands), base, stain);
		col = lerp_col(math(M_GT, vmax(crack, pit), K(0.4)), col, dark);
	}
	else {
		col = lerp_col(mul(gr, K(0.15)), base, stain);
		col = lerp_col(st, col, stain);
		col = lerp_col(mul(vmax(crack, pit), K(0.8)), col, dark);
	}
	bm_t height = add(K(0.6), mul(gr, K(s->stylized ? 0.05 : 0.15)));
	height      = sub(height, mul(pit, K(0.4)));
	height      = sub(height, mul(crack, K(0.5)));
	OUT(_tr("Base Color"), "RGBA", col);
	OUT(_tr("Roughness"), "VALUE", add(rough, mul(st, K(0.15))));
	OUT(_tr("Occlusion"), "VALUE", sub(K(1), mul(vmax(crack, pit), K(0.5))));
	OUT(_tr("Height"), "VALUE", mul(height, bump));
}

// ███████╗██╗     ███████╗███████╗██╗  ██╗
// ██╔════╝██║     ██╔════╝██╔════╝██║  ██║
// █████╗  ██║     █████╗  ███████╗███████║
// ██╔══╝  ██║     ██╔══╝  ╚════██║██╔══██║
// ██║     ███████╗███████╗███████║██║  ██║
// ╚═╝     ╚══════╝╚══════╝╚══════╝╚═╝  ╚═╝

typedef struct bm_flesh {
	f32  base[3], dark[3], fat[3];
	f32  fiber_scale, fat_amount, wetness, subsurface, bump;
	bool stylized;
} bm_flesh_t;

static void bm_build_flesh(const bm_flesh_t *s) {
	bm_t base    = PC(_tr("Flesh Color"), s->base[0], s->base[1], s->base[2]);
	bm_t dark    = PC(_tr("Dark Color"), s->dark[0], s->dark[1], s->dark[2]);
	bm_t fat_col = PC(_tr("Fat Color"), s->fat[0], s->fat[1], s->fat[2]);
	bm_t fibers  = P(_tr("Fiber Scale"), 1, 80, s->fiber_scale);
	bm_t fat_amt = P(_tr("Fat"), 0, 1, s->fat_amount);
	bm_t wet     = P(_tr("Wetness"), 0, 1, s->wetness);
	bm_t subs    = P(_tr("Subsurface"), 0, 1, s->subsurface);
	bm_t bump    = P(_tr("Bumpiness"), 0, 1, s->bump);
	bm_t bands   = s->stylized ? P(_tr("Color Bands"), 1, 8, 3) : K(0);
	bm_t seed    = P(_tr("Seed"), 0, 100, 0);

	bm_t co = bm_coords(seed);

	// Muscle fibers bending around
	ui_node_t *fw  = wave(co, fibers, K(4), 1.5, 1, 0);
	bm_t       fib = L(fw, 1);
	// Marbled fat
	ui_node_t *fn  = noise(co, K(4), 4, 0.6, 1);
	bm_t       fat = remap(L(fn, 0), sub(K(0.75), mul(fat_amt, K(0.3))), sub(K(0.8), mul(fat_amt, K(0.3))), 0, 1);
	// Large shading
	ui_node_t *ln  = noise(co, K(2), 3, 0.5, 0);
	bm_t       lum = add(mul(fib, K(0.5)), mul(L(ln, 0), K(0.5)));

	bm_t col;
	if (s->stylized) {
		col = lerp_col(steps(lum, bands), dark, base);
		col = lerp_col(math(M_GT, fat, K(0.5)), col, fat_col);
	}
	else {
		col = lerp_col(lum, dark, base);
		col = lerp_col(fat, col, fat_col);
	}
	bm_t height = add(mul(fib, K(0.35)), mul(L(ln, 0), K(0.4)));
	height      = add(height, mul(fat, K(0.15)));
	OUT(_tr("Base Color"), "RGBA", col);
	OUT(_tr("Roughness"), "VALUE", sub(K(1), mul(wet, K(0.85))));
	OUT(_tr("Height"), "VALUE", mul(height, bump));
	OUT(_tr("Subsurface"), "VALUE", subs);
}

// ███████╗ ██████╗██╗      ███████╗██╗
// ██╔════╝██╔════╝██║      ██╔════╝██║
// ███████╗██║     ██║█████╗█████╗  ██║
// ╚════██║██║     ██║╚════╝██╔══╝  ██║
// ███████║╚██████╗██║      ██║     ██║
// ╚══════╝ ╚═════╝╚═╝      ╚═╝     ╚═╝

enum {
	SF_RIVETS  = 1,  // Rivets along the panel seams
	SF_DETAIL  = 2,  // Second, finer grid of panel lines
	SF_STRIPES = 4,  // Diagonal hazard stripes
	SF_GRATING = 8,  // Holes between bars
	SF_LIGHTS  = 16, // Glowing dots
	SF_TRACES  = 32, // Glowing circuit traces
};

typedef struct bm_scifi {
	f32 panel[3], panel2[3], seam[3], accent[3], wear[3];
	f32 size, width, row_height, offset, seam_width, detail, wear_amount, glow, metallic, roughness, bump;
	i32 features;
} bm_scifi_t;

static void bm_build_scifi(const bm_scifi_t *s) {
	bool stripes  = s->features & SF_STRIPES;
	bool grating  = s->features & SF_GRATING;
	bool glowing  = s->features & (SF_LIGHTS | SF_TRACES);
	bm_t panel    = PC(_tr("Panel Color"), s->panel[0], s->panel[1], s->panel[2]);
	bm_t panel2   = PC(_tr("Panel Color 2"), s->panel2[0], s->panel2[1], s->panel2[2]);
	bm_t seam_col = PC(_tr("Seam Color"), s->seam[0], s->seam[1], s->seam[2]);
	bm_t accent   = PC(stripes ? _tr("Stripe Color") : glowing ? _tr("Glow Color") : _tr("Accent Color"), s->accent[0], s->accent[1], s->accent[2]);
	bm_t wear_col = PC(_tr("Edge Color"), s->wear[0], s->wear[1], s->wear[2]);
	bm_t size     = P(_tr("Panel Size"), 0.5, 30, s->size);
	bm_t width    = P(_tr("Panel Width"), 0.1, 4, s->width);
	bm_t seam_w   = P(_tr("Seam Width"), 0, 0.2, s->seam_width);
	bm_t detail   = (s->features & SF_DETAIL) ? P(_tr("Detail Lines"), 0, 1, s->detail) : K(0);
	bm_t rivets   = (s->features & SF_RIVETS) ? P(_tr("Rivets"), 0, 1, 1) : K(0);
	bm_t stripe_s = stripes ? P(_tr("Stripe Scale"), 1, 40, 8) : K(0);
	bm_t glow     = glowing ? P(_tr("Glow"), 0, 1, s->glow) : K(0);
	bm_t wear     = P(_tr("Edge Highlight"), 0, 1, s->wear_amount);
	bm_t metal    = P(_tr("Metallic"), 0, 1, s->metallic);
	bm_t rough    = P(_tr("Roughness"), 0, 1, s->roughness);
	bm_t bump     = P(_tr("Bumpiness"), 0, 1, s->bump);
	bm_t seed     = P(_tr("Seed"), 0, 100, 0);

	bm_t co = bm_coords(seed);

	// Panels: tint per panel, seams in between
	ui_node_t *pb   = brick(co, panel, panel2, seam_col, size, seam_w, 0, width, s->row_height, s->offset);
	bm_t       col  = L(pb, 0);
	bm_t       seam = L(pb, 1);
	// Band along the seams for the edge highlight and the rivets
	ui_node_t *eb   = brick(co, K(0), K(0), K(0), size, add(seam_w, K(0.09)), 1, width, s->row_height, s->offset);
	bm_t       band = L(eb, 1);
	bm_t       rim  = mul(math(M_GT, band, K(0.35)), sub(K(1), seam));

	bm_t height = sub(K(0.7), mul(seam, K(0.6)));
	bm_t glow_m = K(0);

	if (s->features & SF_DETAIL) {
		ui_node_t *db = brick(co, K(0), K(0), K(0), mul(size, K(2)), mul(seam_w, K(0.6)), 0, mul(width, K(0.7)), s->row_height * 1.5, 0.3);
		ui_node_t *dm = noise(co, mul(size, K(0.8)), 1, 0.5, 0);
		bm_t       dl = mul(mul(L(db, 1), math(M_GT, L(dm, 0), K(0.5))), detail);
		col           = lerp_col(dl, col, seam_col);
		height        = sub(height, mul(dl, K(0.3)));
	}
	if (stripes) {
		ui_node_t *sw = wave(co, stripe_s, K(0), 1, 3, 1);
		bm_t       sm = math(M_GT, L(sw, 1), K(0.5));
		col           = lerp_col(mul(sm, sub(K(1), seam)), col, accent);
	}
	if (grating) {
		// The panel interior becomes dark holes, the seams are the bars
		ui_node_t *gb   = brick(co, K(0), K(0), K(0), mul(size, K(6)), K(0.06), 0, K(0.5), 0.5, 0);
		bm_t       hole = sub(K(1), L(gb, 1));
		col             = lerp_col(mul(hole, sub(K(1), seam)), col, C(0.03, 0.03, 0.035));
		height          = sub(height, mul(mul(hole, sub(K(1), seam)), K(0.6)));
	}
	if (s->features & SF_TRACES) {
		ui_node_t *ta = brick(co, K(0), K(0), K(0), mul(size, K(3)), K(0.025), 0, K(1.0), 0.5, 0.5);
		ui_node_t *tb = brick(co, K(0), K(0), K(0), mul(size, K(5.3)), K(0.03), 0, K(1.3), 0.3, 0.3);
		ui_node_t *na = noise(co, mul(size, K(1.5)), 1, 0.5, 0);
		ui_node_t *nb = noise(vmath(V_ADD, co, K(3.7)), mul(size, K(1.5)), 1, 0.5, 0);
		bm_t       tr = vmax(mul(L(ta, 1), math(M_GT, L(na, 0), K(0.48))), mul(L(tb, 1), math(M_GT, L(nb, 0), K(0.55))));
		tr            = mul(tr, sub(K(1), seam));
		glow_m        = vmax(glow_m, tr);
		height        = add(height, mul(tr, K(0.1)));
	}
	if (s->features & SF_LIGHTS) {
		ui_node_t *lv = voronoi(co, mul(size, K(6)), K(0), false);
		ui_node_t *ln = noise(co, mul(size, K(2)), 1, 0.5, 0);
		bm_t       lt = mul(remap(L(lv, 0), K(0.12), K(0.18), 1, 0), math(M_GT, L(ln, 0), K(0.6)));
		glow_m        = vmax(glow_m, mul(lt, sub(K(1), band)));
	}
	if (s->features & SF_RIVETS) {
		// Dots on a grid shifted half a step, so they sit just inside the seams
		bm_t       rs  = mul(size, K(8));
		bm_t       rco = vmath(V_ADD, co, math(M_DIV, K(0.5), rs));
		ui_node_t *rv  = voronoi(rco, rs, K(0), false);
		bm_t       dot = remap(L(rv, 0), K(0.12), K(0.2), 1, 0);
		bm_t       rvt = mul(mul(dot, math(M_GT, band, K(0.05))), mul(sub(K(1), seam), rivets));
		col            = lerp_col(rvt, col, wear_col);
		height         = add(height, mul(rvt, K(0.3)));
	}
	col = lerp_col(mul(rim, wear), col, wear_col);
	if (glowing) {
		col = lerp_col(mul(glow_m, math(M_GT, glow, K(0))), col, accent);
	}

	OUT(_tr("Base Color"), "RGBA", col);
	OUT(_tr("Roughness"), "VALUE", add(rough, mul(seam, K(0.3))));
	OUT(_tr("Metallic"), "VALUE", mul(metal, sub(K(1), seam)));
	OUT(_tr("Occlusion"), "VALUE", sub(K(1), mul(seam, K(0.5))));
	if (glowing) {
		OUT(_tr("Emission"), "VALUE", mul(glow_m, glow));
	}
	OUT(_tr("Height"), "VALUE", mul(height, bump));
}

// ███████╗██╗   ██╗██████╗
// ██╔════╝██║   ██║██╔══██╗
// █████╗  ██║   ██║██████╔╝
// ██╔══╝  ██║   ██║██╔══██╗
// ██║     ╚██████╔╝██║  ██║
// ╚═╝      ╚═════╝ ╚═╝  ╚═╝

typedef struct bm_fur {
	f32  base[3], root[3], tip[3];
	f32  density, length, direction, clumping, clump_size, strand_height, roughness;
	bool stylized;
} bm_fur_t;

// Strands with a strong height: tall on the strands, low between them, so a height blended mask lets them hang over the layers below
static void bm_build_fur(const bm_fur_t *s) {
	bm_t base       = PC(_tr("Fur Color"), s->base[0], s->base[1], s->base[2]);
	bm_t root       = PC(_tr("Root Color"), s->root[0], s->root[1], s->root[2]);
	bm_t tip        = PC(_tr("Tip Color"), s->tip[0], s->tip[1], s->tip[2]);
	bm_t density    = P(_tr("Density"), 5, 400, s->density);
	bm_t length     = P(_tr("Strand Length"), 1, 30, s->length);
	bm_t direction  = P(_tr("Direction"), 0, 360, s->direction);
	bm_t clumping   = P(_tr("Clumping"), 0, 1, s->clumping);
	bm_t clump_size = P(_tr("Clump Size"), 1, 60, s->clump_size);
	bm_t bump       = P(_tr("Strand Height"), 0, 1, s->strand_height);
	bm_t rough      = P(_tr("Roughness"), 0, 1, s->roughness);
	bm_t bands      = s->stylized ? P(_tr("Color Bands"), 1, 8, 3) : K(0);
	bm_t seed       = P(_tr("Seed"), 0, 100, 0);

	bm_t       co = bm_coords(seed);
	ui_node_t *mp = bm_node("MAPPING");
	bm_in(mp, 0, co);
	bm_in(mp, 2, combine(K(0), K(0), direction));
	co = L(mp, 0);

	// Clumps: strands gather around stretched cells, taller at their centers
	bm_t       cco   = vmath(V_MUL, co, combine(clump_size, math(M_DIV, clump_size, math(M_DIV, length, K(2))), K(1)));
	ui_node_t *cv    = voronoi(cco, K(1), K(1), false);
	bm_t       clump = remap(L(cv, 0), K(0), K(0.8), 1, 0);
	// Bend the strands a little inside each clump
	ui_node_t *bn = noise(co, mul(clump_size, K(0.5)), 2, 0.5, 0);
	co            = vmath(V_ADD, co, vmath(V_MUL, vmath(V_SUB, L(bn, 1), K(0.5)), math(M_DIV, K(0.3), clump_size)));

	// Strands: noise stretched along the direction, high contrast
	bm_t       sco     = vmath(V_MUL, co, combine(density, math(M_DIV, density, length), K(1)));
	ui_node_t *sn      = noise(sco, K(1), s->stylized ? 1 : 3, 0.5, 0);
	bm_t       strands = remap(L(sn, 0), K(s->stylized ? 0.4 : 0.42), K(s->stylized ? 0.6 : 0.68), 0, 1);
	ui_node_t *fn      = noise(vmath(V_MUL, sco, K(2.3)), K(1), 2, 0.5, 0);
	bm_t       fine    = remap(L(fn, 0), K(0.45), K(0.7), 0, 1);
	bm_t       hair    = vmax(strands, mul(fine, K(0.6)));
	bm_t       height  = mul(hair, add(sub(K(1), clumping), mul(clumping, clump)));

	bm_t col;
	if (s->stylized) {
		// Painted tufts: flat bands from root to tip following the clumps
		bm_t shade = steps(add(mul(clump, K(0.6)), mul(hair, K(0.4))), bands);
		col        = lerp_col(shade, root, base);
		col        = lerp_col(math(M_GT, mul(shade, clump), K(0.75)), col, tip);
	}
	else {
		col = lerp_col(height, root, base);
		col = lerp_col(mul(mul(height, height), K(0.8)), col, tip);
	}
	OUT(_tr("Base Color"), "RGBA", col);
	OUT(_tr("Roughness"), "VALUE", add(rough, mul(sub(K(1), height), K(0.15))));
	OUT(_tr("Occlusion"), "VALUE", add(K(0.4), mul(height, K(0.6))));
	OUT(_tr("Height"), "VALUE", mul(height, bump));
}

// ██████╗ ██████╗ ███████╗███████╗███████╗████████╗███████╗
// ██╔══██╗██╔══██╗██╔════╝██╔════╝██╔════╝╚══██╔══╝██╔════╝
// ██████╔╝██████╔╝█████╗  ███████╗█████╗     ██║   ███████╗
// ██╔═══╝ ██╔══██╗██╔══╝  ╚════██║██╔══╝     ██║   ╚════██║
// ██║     ██║  ██║███████╗███████║███████╗   ██║   ███████║
// ╚═╝     ╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝   ╚═╝   ╚══════╝

enum { BM_SCALES, BM_SKIN, BM_BONE, BM_FLESH, BM_SCIFI, BM_FUR };

typedef struct builtin_preset {
	char       *name;
	i32         section;
	i32         family;
	const void *cfg;
} builtin_preset_t;

// clang-format off
static const bm_scales_t bm_lizard     = {{0.32, 0.42, 0.16}, {0.55, 0.58, 0.25}, {0.08, 0.1, 0.05}, {0.12, 0.14, 0.06}, 40, 1.0, 0.9, 0.25, 0.04, 0.12, 0.6, 0.4, 0.2, 0.5, 0.55, 0.0, false};
static const bm_scales_t bm_dragon     = {{0.35, 0.05, 0.04}, {0.75, 0.45, 0.12}, {0.05, 0.01, 0.01}, {0.1, 0.02, 0.02}, 12, 1.2, 0.7, 0.2, 0.05, 0.2, 0.5, 0.0, 0.3, 0.8, 0.35, 0.25, false};
static const bm_scales_t bm_croc       = {{0.24, 0.26, 0.15}, {0.36, 0.34, 0.2}, {0.06, 0.06, 0.04}, {0.1, 0.11, 0.06}, 8, 1.3, 1.0, 0.35, 0.06, 0.06, 0.4, 0.3, 0.0, 0.8, 0.6, 0.0, false};
static const bm_scales_t bm_snake      = {{0.45, 0.38, 0.22}, {0.6, 0.5, 0.3}, {0.12, 0.09, 0.05}, {0.15, 0.1, 0.06}, 30, 1.6, 0.35, 0.1, 0.03, 0.15, 0.3, 0.0, 0.7, 0.4, 0.3, 0.0, false};
static const bm_scales_t bm_fish       = {{0.55, 0.6, 0.62}, {0.3, 0.45, 0.5}, {0.18, 0.2, 0.22}, {0.15, 0.2, 0.28}, 30, 0.7, 0.3, 0.1, 0.02, 0.25, 0.3, 0.0, 0.5, 0.35, 0.25, 0.6, false};
static const bm_scales_t bm_s_dragon   = {{0.75, 0.15, 0.12}, {0.95, 0.55, 0.15}, {0.25, 0.04, 0.06}, {0.4, 0.08, 0.1}, 10, 1.2, 0.6, 0.15, 0.04, 0.12, 0.5, 0.0, 0.0, 0.6, 0.45, 0.0, true};
static const bm_scales_t bm_s_lizard   = {{0.35, 0.7, 0.3}, {0.65, 0.85, 0.3}, {0.1, 0.3, 0.15}, {0.15, 0.4, 0.2}, 18, 1.0, 0.8, 0.2, 0.04, 0.1, 0.4, 0.5, 0.0, 0.4, 0.6, 0.0, true};
static const bm_scales_t bm_s_fish     = {{0.3, 0.65, 0.85}, {0.55, 0.85, 0.95}, {0.1, 0.25, 0.45}, {0.95, 0.55, 0.2}, 16, 0.7, 0.3, 0.05, 0.03, 0.2, 0.3, 0.0, 0.4, 0.4, 0.35, 0.0, true};
static const bm_skin_t   bm_human      = {{0.85, 0.62, 0.52}, {0.8, 0.45, 0.4}, {0.45, 0.42, 0.6}, {0.55, 0.33, 0.22}, 0.5, 0.2, 1.0, 0.5, 0.3, 0.0, 30, 0.6, 0.55, 0.3, false};
static const bm_skin_t   bm_s_skin     = {{0.95, 0.72, 0.6}, {0.95, 0.55, 0.5}, {0.6, 0.5, 0.7}, {0.75, 0.45, 0.35}, 0.5, 0.0, 1.0, 0.0, 0.0, 0.3, 25, 0.5, 0.6, 0.15, true};
static const bm_skin_t   bm_s_alien    = {{0.35, 0.65, 0.45}, {0.25, 0.45, 0.55}, {0.75, 0.95, 0.4}, {0.55, 0.3, 0.7}, 0.6, 0.6, 1.0, 0.0, 0.3, 0.8, 12, 0.4, 0.4, 0.4, true};
static const bm_bone_t   bm_bone       = {{0.86, 0.82, 0.7}, {0.62, 0.52, 0.35}, {0.25, 0.2, 0.14}, 18, 0.5, 0.5, 0.4, 0.6, 0.4, false};
static const bm_bone_t   bm_s_bone     = {{0.95, 0.92, 0.8}, {0.8, 0.72, 0.55}, {0.35, 0.28, 0.22}, 10, 0.5, 0.3, 0.5, 0.7, 0.3, true};
static const bm_flesh_t  bm_flesh      = {{0.65, 0.12, 0.12}, {0.3, 0.03, 0.04}, {0.92, 0.85, 0.75}, 22, 0.3, 0.75, 0.7, 0.5, false};
static const bm_flesh_t  bm_s_flesh    = {{0.9, 0.3, 0.35}, {0.55, 0.1, 0.18}, {1.0, 0.9, 0.8}, 14, 0.3, 0.6, 0.5, 0.4, true};
static const bm_fur_t    bm_fur        = {{0.42, 0.28, 0.16}, {0.12, 0.07, 0.04}, {0.75, 0.6, 0.42}, 140, 8, 90, 0.6, 10, 0.8, 0.7, false};
static const bm_fur_t    bm_s_fur      = {{0.55, 0.35, 0.2}, {0.25, 0.13, 0.08}, {0.85, 0.65, 0.4}, 50, 6, 90, 0.9, 8, 0.7, 0.75, true};
static const bm_scifi_t  bm_panels     = {{0.55, 0.6, 0.65}, {0.48, 0.53, 0.58}, {0.12, 0.13, 0.15}, {0.2, 0.7, 1.0}, {0.9, 0.95, 1.0}, 4, 1.0, 0.5, 0.5, 0.02, 0.6, 0.5, 0.0, 0.8, 0.4, 0.5, SF_DETAIL | SF_RIVETS};
static const bm_scifi_t  bm_armor      = {{0.3, 0.38, 0.3}, {0.27, 0.34, 0.27}, {0.06, 0.07, 0.06}, {0.9, 0.6, 0.1}, {0.8, 0.82, 0.8}, 2, 1.4, 0.7, 0.3, 0.03, 0.0, 0.8, 0.0, 0.6, 0.5, 0.7, SF_RIVETS};
static const bm_scifi_t  bm_hazard     = {{0.1, 0.1, 0.11}, {0.12, 0.12, 0.13}, {0.05, 0.05, 0.05}, {0.95, 0.75, 0.1}, {0.85, 0.85, 0.85}, 2, 2.0, 0.5, 0.0, 0.02, 0.0, 0.6, 0.0, 0.3, 0.5, 0.4, SF_STRIPES};
static const bm_scifi_t  bm_grating    = {{0.4, 0.42, 0.45}, {0.38, 0.4, 0.43}, {0.15, 0.15, 0.17}, {0.2, 0.7, 1.0}, {0.85, 0.88, 0.9}, 3, 1.0, 1.0, 0.0, 0.03, 0.0, 0.5, 0.0, 0.9, 0.35, 0.7, SF_GRATING};
static const bm_scifi_t  bm_greeble    = {{0.35, 0.37, 0.42}, {0.28, 0.3, 0.35}, {0.08, 0.08, 0.1}, {0.2, 0.9, 1.0}, {0.8, 0.85, 0.9}, 5, 0.8, 0.3, 0.5, 0.015, 1.0, 0.5, 0.8, 0.7, 0.45, 0.6, SF_DETAIL | SF_LIGHTS | SF_RIVETS};
static const bm_scifi_t  bm_circuits   = {{0.04, 0.12, 0.1}, {0.03, 0.1, 0.09}, {0.02, 0.03, 0.03}, {0.1, 1.0, 0.6}, {0.7, 0.6, 0.3}, 2, 1.0, 1.0, 0.0, 0.01, 0.0, 0.3, 1.0, 0.0, 0.5, 0.3, SF_TRACES | SF_LIGHTS};
static const bm_scifi_t  bm_riveted    = {{0.5, 0.45, 0.4}, {0.45, 0.4, 0.36}, {0.15, 0.12, 0.1}, {0.9, 0.5, 0.1}, {0.85, 0.75, 0.6}, 3, 1.2, 0.6, 0.0, 0.02, 0.0, 0.6, 0.0, 0.85, 0.45, 0.6, SF_RIVETS};

static builtin_preset_t builtin_presets[] = {
	{"Lizard Scales",          0, BM_SCALES, &bm_lizard},
	{"Dragon Scales",          0, BM_SCALES, &bm_dragon},
	{"Crocodile Hide",         0, BM_SCALES, &bm_croc},
	{"Snake Skin",             0, BM_SCALES, &bm_snake},
	{"Fish Scales",            0, BM_SCALES, &bm_fish},
	{"Human Skin",             0, BM_SKIN,   &bm_human},
	{"Bone",                   0, BM_BONE,   &bm_bone},
	{"Raw Flesh",              0, BM_FLESH,  &bm_flesh},
	{"Fur",                    0, BM_FUR,    &bm_fur},
	{"Stylized Dragon Scales", 1, BM_SCALES, &bm_s_dragon},
	{"Stylized Lizard Skin",   1, BM_SCALES, &bm_s_lizard},
	{"Stylized Fish Scales",   1, BM_SCALES, &bm_s_fish},
	{"Stylized Skin",          1, BM_SKIN,   &bm_s_skin},
	{"Stylized Alien Skin",    1, BM_SKIN,   &bm_s_alien},
	{"Stylized Bone",          1, BM_BONE,   &bm_s_bone},
	{"Stylized Flesh",         1, BM_FLESH,  &bm_s_flesh},
	{"Stylized Fur",           1, BM_FUR,    &bm_s_fur},
	{"Sci-Fi Panels",          2, BM_SCIFI,  &bm_panels},
	{"Armor Plating",          2, BM_SCIFI,  &bm_armor},
	{"Hazard Stripes",         2, BM_SCIFI,  &bm_hazard},
	{"Floor Grating",          2, BM_SCIFI,  &bm_grating},
	{"Tech Greeble",           2, BM_SCIFI,  &bm_greeble},
	{"Glowing Circuits",       2, BM_SCIFI,  &bm_circuits},
	{"Riveted Plates",         2, BM_SCIFI,  &bm_riveted},
};
// clang-format on

#define BUILTIN_COUNT ((i32)(sizeof(builtin_presets) / sizeof(builtin_presets[0])))

static char *builtin_section_names[] = {"Creatures", "Stylized Creatures", "Stylized Sci-Fi"};

static ui_node_canvas_t *builtin_build_group(i32 i) {
	builtin_preset_t *p = &builtin_presets[i];
	bm_begin(p->name);
	switch (p->family) {
	case BM_SCALES:
		bm_build_scales(p->cfg);
		break;
	case BM_SKIN:
		bm_build_skin(p->cfg);
		break;
	case BM_BONE:
		bm_build_bone(p->cfg);
		break;
	case BM_FLESH:
		bm_build_flesh(p->cfg);
		break;
	case BM_FUR:
		bm_build_fur(p->cfg);
		break;
	default:
		bm_build_scifi(p->cfg);
		break;
	}
	return bm_end();
}

static node_group_t *builtin_find_group(char *name) {
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *g = g_project->_->material_groups->buffer[i];
		if (string_equals(g->canvas->name, name)) {
			return g;
		}
	}
	return NULL;
}

// Group of the preset in the project, added when missing (an existing one is kept, the user may have edited it)
static node_group_t *builtin_ensure_group(i32 i, bool *added) {
	node_group_t *g = builtin_find_group(builtin_presets[i].name);
	*added          = g == NULL;
	if (g == NULL) {
		g = ALLOC_INIT(node_group_t, {.canvas = builtin_build_group(i), .nodes = ui_nodes_create()});
		any_array_push(g_project->_->material_groups, g);
	}
	return g;
}

static i32 builtin_output_index(char *name) {
	char *names[] = {"Base Color", "Opacity", "Occlusion", "Roughness", "Metallic", "Normal Map", "Emission", "Height", "Subsurface"};
	for (i32 i = 0; i < 9; ++i) {
		if (string_equals(name, names[i])) {
			return i;
		}
	}
	return -1;
}

// Material with the group node wired into the Material Output, not added to the project
static slot_material_t *builtin_make_material(i32 i, node_group_t *g) {
	slot_material_t  *m      = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	ui_node_canvas_t *canvas = m->canvas;
	canvas->name             = string_copy(slot_material_unique_name(canvas, builtin_presets[i].name));
	ui_node_t *out           = NULL;
	for (i32 j = 0; j < canvas->nodes->length; ++j) {
		if (string_equals(canvas->nodes->buffer[j]->type, "OUTPUT_MATERIAL_PBR")) {
			out = canvas->nodes->buffer[j];
		}
	}
	ui_node_t *node = ui_nodes_make_group_node_from(nodes_material_group->buffer[0], g->canvas, m->nodes, canvas);
	node->x         = out != NULL ? out->x - 260 : 100;
	node->y         = out != NULL ? out->y : 100;
	any_array_push(canvas->nodes, node);
	for (i32 j = 0; out != NULL && j < node->outputs->length; ++j) {
		i32 to = builtin_output_index(node->outputs->buffer[j]->name);
		if (to >= 0) {
			any_array_push(canvas->links, project_create_node_link(canvas->links, node->id, j, out->id, to));
		}
	}
	ui_nodes_node_changed = NULL;
	return m;
}

// ██╗   ██╗███████╗███████╗
// ██║   ██║██╔════╝██╔════╝
// ██║   ██║███████╗█████╗
// ██║   ██║╚════██║██╔══╝
// ╚██████╔╝███████║███████╗
//  ╚═════╝ ╚══════╝╚══════╝

static void builtin_material_use_on_next_frame(void *data) {
	i32   i    = (i32)(size_t)data;
	char *name = builtin_presets[i].name;
	for (i32 j = 0; j < g_project->_->materials->length; ++j) {
		if (string_equals(g_project->_->materials->buffer[j]->canvas->name, name)) {
			context_select_material(j);
			return;
		}
	}
	bool          added;
	node_group_t *g      = builtin_ensure_group(i, &added);
	slot_material_t *m   = builtin_make_material(i, g);
	any_array_push(g_project->_->materials, m);
	g_context->material = m;
	tab_materials_update_material();
	history_new_material();
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

void builtin_material_use(i32 i) {
	if (i < 0 || i >= BUILTIN_COUNT) {
		return;
	}
	sys_notify_on_next_frame(&builtin_material_use_on_next_frame, (void *)(size_t)i);
}

i32 builtin_material_count() {
	return BUILTIN_COUNT;
}

char *builtin_material_name(i32 i) {
	return builtin_presets[i].name;
}

i32 builtin_material_section(i32 i) {
	return builtin_presets[i].section;
}

i32 builtin_material_section_count() {
	return 3;
}

char *builtin_material_section_name(i32 s) {
	return tr(builtin_section_names[s]);
}

// ██████╗ ██████╗ ███████╗██╗   ██╗██╗███████╗██╗    ██╗███████╗
// ██╔══██╗██╔══██╗██╔════╝██║   ██║██║██╔════╝██║    ██║██╔════╝
// ██████╔╝██████╔╝█████╗  ██║   ██║██║█████╗  ██║ █╗ ██║███████╗
// ██╔═══╝ ██╔══██╗██╔══╝  ╚██╗ ██╔╝██║██╔══╝  ██║███╗██║╚════██║
// ██║     ██║  ██║███████╗ ╚████╔╝ ██║███████╗╚███╔███╔╝███████║
// ╚═╝     ╚═╝  ╚═╝╚══════╝  ╚═══╝  ╚═╝╚══════╝ ╚══╝╚══╝ ╚══════╝

static gpu_texture_t *builtin_icons[64];
static i32            builtin_icon_state[64]; // 0 none, 1 requested, 2 ready, 3 failed
static bool           builtin_update_registered = false;

static char *builtin_icon_dir() {
	return string("%sbuiltin", iron_internal_save_path());
}

static char *builtin_icon_path(i32 i) {
	char *slug = to_lower_case(string_replace_all(builtin_presets[i].name, " ", "_"));
	return string("%s/%s_v%d.png", builtin_icon_dir(), slug, BUILTIN_VERSION);
}

static bool builtin_load_icon(i32 i) {
	iron_image_pixels_t px = {0};
	if (!library_decode(builtin_icon_path(i), &px, LIBRARY_ICON_RES)) {
		return false;
	}
	builtin_icons[i] = gpu_create_texture_from_pixels(&px); // Frees the pixels
	return true;
}

static void builtin_render_icon(i32 i) {
	slot_material_t *saved_material = g_context->material;
	bool             added;
	node_group_t    *g = builtin_ensure_group(i, &added);
	slot_material_t *m = builtin_make_material(i, g);
	g_context->material = m;
	bool height_used    = make_material_height_used;
	make_material_height_used = true; // Show the relief in the preview
	util_render_make_material_preview();
	make_material_height_used = height_used;

	builtin_icons[i] = gpu_create_render_target(LIBRARY_ICON_RES, LIBRARY_ICON_RES, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(builtin_icons[i], true, 0x00000000);
	draw_scaled_image(m->image, 0, 0, LIBRARY_ICON_RES, LIBRARY_ICON_RES);
	draw_end();

	buffer_t *pixels = buffer_half_to_u8(gpu_get_texture_pixels(m->image));
#ifdef IRON_BGRA
	pixels = buffer_bgra_swap(pixels);
#endif
	buffer_t *png = iron_encode_png(pixels, m->image->width, m->image->height, 0);
	iron_create_directory(builtin_icon_dir());
	iron_folder_write(builtin_icon_path(i), png->buffer, png->length);

	g_context->material = saved_material;
	slot_material_unload(m);
	if (added) {
		array_remove(g_project->_->material_groups, g);
	}
	g_context->ddirty = 2;
}

// One preview per frame, cached files first
static void builtin_update(void *_) {
	for (i32 i = 0; i < BUILTIN_COUNT; ++i) {
		if (builtin_icon_state[i] != 1) {
			continue;
		}
		if (builtin_load_icon(i)) {
			builtin_icon_state[i] = 2;
		}
		else {
			builtin_render_icon(i);
			builtin_icon_state[i] = builtin_icons[i] != NULL ? 2 : 3;
		}
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
		return;
	}
}

gpu_texture_t *builtin_material_icon(i32 i) {
	if (builtin_icon_state[i] == 0) {
		builtin_icon_state[i] = 1;
		if (!builtin_update_registered) {
			builtin_update_registered = true;
			sys_notify_on_update(builtin_update, NULL);
		}
	}
	return builtin_icon_state[i] == 2 ? builtin_icons[i] : NULL;
}
