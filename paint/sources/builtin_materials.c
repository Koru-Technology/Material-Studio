#include "global.h"

// Built-in procedural materials shown at the top of the Library tab (creatures and sci-fi). Each one is a studio material
// (studio_graph.c): layers of pattern, color ramp and mask nodes the Material Studio edits. A preset table per kind gives the
// colors and amounts, a builder per kind turns them into layers. Previews are rendered once and cached in <save>/builtin.

#define BUILTIN_VERSION 14 // Bump to re-render the cached previews after changing a preset

// ██████╗ ██╗   ██╗██╗██╗     ██████╗ ███████╗██████╗
// ██╔══██╗██║   ██║██║██║     ██╔══██╗██╔════╝██╔══██╗
// ██████╔╝██║   ██║██║██║     ██║  ██║█████╗  ██████╔╝
// ██╔══██╗██║   ██║██║██║     ██║  ██║██╔══╝  ██╔══██╗
// ██████╔╝╚██████╔╝██║███████╗██████╔╝███████╗██║  ██║
// ╚═════╝  ╚═════╝ ╚═╝╚══════╝╚═════╝ ╚══════╝╚═╝  ╚═╝

static ui_node_canvas_t *bs_c     = NULL; // Canvas being built
static i32               bs_k     = -1;   // Current layer
static bool              bs_first = true; // The next layer is the base

// A new layer on top, the base first. pattern NULL for a plain layer.
static void LAYER(char *name, char *pattern) {
	bs_k     = bs_first ? 0 : studio_graph_add_layer(bs_c);
	bs_first = false;
	studio_graph_set_pattern(bs_c, bs_k, pattern);
	if (name != NULL && bs_k > 0) {
		studio_graph_set_layer_name(bs_c, bs_k, tr(name));
	}
}

// Input of the layer's pattern node by name
static void IN(char *name, f32 v) {
	studio_graph_set_pattern_input(bs_c, bs_k, name, v);
}

static void V(i32 value, f32 v) {
	studio_graph_set(bs_c, bs_k, value, v);
}

// Stops: r, g, b, position, ..
static void RAMP(bool hard, i32 count, f32 *stops) {
	f32 color[8][3];
	f32 pos[8];
	for (i32 i = 0; i < count && i < 8; ++i) {
		color[i][0] = stops[i * 4 + 0];
		color[i][1] = stops[i * 4 + 1];
		color[i][2] = stops[i * 4 + 2];
		pos[i]      = stops[i * 4 + 3];
	}
	studio_graph_set_ramp(bs_c, bs_k, count, color, pos, hard);
}

static void COLOR(const f32 *c) {
	RAMP(false, 1, (f32[]){c[0], c[1], c[2], 1.0});
}

// Where the layer shows
static void MASK(i32 mask, f32 amount, f32 soft, f32 breakup, bool invert) {
	studio_graph_set_mask(bs_c, bs_k, mask);
	V(STUDIO_VALUE_MASK_INVERT, invert ? 1.0 : 0.0);
	V(STUDIO_VALUE_MASK_AMOUNT, amount);
	V(STUDIO_VALUE_MASK_SOFT, soft);
	V(STUDIO_VALUE_MASK_BREAKUP, breakup);
}

enum { CH_COLOR = 1, CH_ROUGH = 2, CH_METAL = 4, CH_HEIGHT = 8, CH_OCC = 16, CH_EMIS = 32, CH_SUBS = 64, CH_OPAC = 128 };

// Channels the layer changes, and how its height goes over the layers below
static void CHANNELS(i32 bits, i32 height_blend) {
	for (i32 ch = 0; ch < 8; ++ch) {
		studio_graph_set_channel(bs_c, bs_k, ch, (bits >> ch) & 1);
	}
	studio_graph_set_height_blend(bs_c, bs_k, height_blend);
}

enum { HB_MIX, HB_ADD, HB_CARVE };

static void lerp3(const f32 *a, const f32 *b, f32 t, f32 *out) {
	for (i32 i = 0; i < 3; ++i) {
		out[i] = a[i] + (b[i] - a[i]) * t;
	}
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
	// Scales: cells with grooves, domed, a tint per scale between the base and accent colors
	LAYER(NULL, "PATTERN_CELLS");
	IN("Scale", s->size);
	IN("Stretch", s->stretch);
	IN("Randomness", s->randomness);
	IN("Edge Width", s->groove_width + s->rounding * 0.5);
	IN("Rounding", 0.35);
	IN("Variation", 0.0);
	IN("Distort", s->wobble * 0.3);
	IN("Distort Scale", fmaxf(s->size * 0.5, 1.0));
	f32 var = fminf(s->variation, 0.65);
	f32 accent[3];
	lerp3(s->base, s->accent, var > 0.0 ? s->variation / var : 0.0, accent);
	f32 groove[3];
	lerp3(s->base, s->groove, 0.75, groove);
	if (s->stylized) { // Flat bands from the groove to the center, a light spot at the top of each scale
		f32 dark[3], mid[3], light[3];
		lerp3(s->groove, s->base, 0.45, dark);
		lerp3(s->groove, s->base, 0.8, mid);
		lerp3(s->base, (f32[]){1.0, 0.95, 0.75}, 0.15, light);
		RAMP(true, 5,
		     (f32[]){s->groove[0], s->groove[1], s->groove[2], 0.0, dark[0], dark[1], dark[2], 0.12, mid[0], mid[1], mid[2], 0.4, s->base[0], s->base[1],
		             s->base[2], 0.7, light[0], light[1], light[2], 0.97});
		V(STUDIO_VALUE_CELL_VAR, 0.15);
	}
	else {
		RAMP(false, 3,
		     (f32[]){groove[0], groove[1], groove[2], 0.0, s->base[0], s->base[1], s->base[2], fmaxf(1.0 - var, 0.3), accent[0], accent[1], accent[2],
		             1.0});
		V(STUDIO_VALUE_CELL_VAR, var);
	}
	V(STUDIO_VALUE_ROUGHNESS, s->roughness + 0.1);
	V(STUDIO_VALUE_ROUGH_VAR, 0.2);
	V(STUDIO_VALUE_METALLIC, s->metallic);
	V(STUDIO_VALUE_METAL_VAR, 1.0);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, s->bump);

	if (s->spots > 0.0) { // Soft patches of the marking color
		LAYER("Spots", "PATTERN_NOISE");
		IN("Scale", fmaxf(s->size * 0.12, 1.0));
		IN("Contrast", 1.6);
		COLOR(s->mark);
		MASK(STUDIO_MASK_PATTERN, 0.4, s->stylized ? 0.02 : 0.12, 0.0, false);
		V(STUDIO_VALUE_OPACITY, s->spots);
		CHANNELS(CH_COLOR, HB_MIX);
	}
	if (s->stripes > 0.0) { // Wavy bands across the scales
		LAYER("Stripes", "PATTERN_STRIPES");
		IN("Scale", fmaxf(floorf(s->size * 0.08 + 0.5), 1.0));
		IN("Waviness", 0.7);
		IN("Wave Length", 2.0);
		IN("Distort", 0.25);
		COLOR(s->mark);
		MASK(STUDIO_MASK_PATTERN, 0.38, s->stylized ? 0.02 : 0.15, 0.0, false);
		V(STUDIO_VALUE_OPACITY, s->stripes);
		CHANNELS(CH_COLOR, HB_MIX);
	}
	if (!s->stylized) { // Fine bumps over everything
		LAYER("Fine Bumps", "PATTERN_NOISE");
		IN("Scale", s->size * 4.0);
		IN("Persistence", 0.6);
		COLOR(s->base);
		MASK(STUDIO_MASK_FULL, 0.5, 0.35, 0.0, false);
		V(STUDIO_VALUE_DEPTH, s->bump * 0.15);
		CHANNELS(CH_HEIGHT, HB_ADD);
	}
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
	f32 soft = s->stylized ? 0.03 : 0.3;
	// Skin with a gentle undulation
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 5.0);
	COLOR(s->base);
	V(STUDIO_VALUE_ROUGHNESS, s->roughness);
	V(STUDIO_VALUE_DEPTH, s->bump * 0.3);
	V(STUDIO_VALUE_SUBSURFACE, s->subsurface);

	if (s->blotchiness > 0.0) {
		LAYER("Blotches", "PATTERN_NOISE");
		IN("Scale", 5.0);
		IN("Seed", 3.0);
		COLOR(s->blotch);
		MASK(STUDIO_MASK_PATTERN, 0.45, s->stylized ? 0.05 : 0.35, 0.0, false);
		V(STUDIO_VALUE_OPACITY, s->stylized ? s->blotchiness * 0.45 : s->blotchiness);
		CHANNELS(CH_COLOR, HB_MIX);
	}
	if (s->veins > 0.0) { // Thin ridges of the noise
		LAYER("Veins", "PATTERN_NOISE");
		IN("Scale", 2.5);
		IN("Ridged", 1.0);
		IN("Persistence", 0.5);
		IN("Distort", 0.2);
		COLOR(s->vein);
		MASK(STUDIO_MASK_PATTERN, 0.06, s->stylized ? 0.02 : 0.08, 0.0, false);
		V(STUDIO_VALUE_OPACITY, s->veins * 0.5);
		CHANNELS(CH_COLOR, HB_MIX);
	}
	if (s->pore_depth > 0.0) { // Small dark dots carved in
		f32 pore[3];
		lerp3(s->base, (f32[]){0.45, 0.3, 0.26}, 0.6, pore);
		LAYER("Pores", "PATTERN_DOTS");
		IN("Scale", 250.0 / fmaxf(s->pore_size, 0.1));
		IN("Size", 0.45);
		IN("Randomness", 1.0);
		COLOR(pore);
		MASK(STUDIO_MASK_PATTERN, 0.6, 0.4, 0.0, false);
		V(STUDIO_VALUE_OPACITY, s->pore_depth * 0.5);
		V(STUDIO_VALUE_ROUGHNESS, s->roughness + 0.1);
		V(STUDIO_VALUE_DEPTH, s->pore_depth * 0.35 * s->bump);
		CHANNELS(CH_COLOR | CH_ROUGH | CH_HEIGHT, HB_CARVE);
	}
	if (s->wrinkles > 0.0) { // Fine lines in one direction
		LAYER("Wrinkles", "PATTERN_NOISE");
		IN("Scale", 40.0);
		IN("Stretch", 6.0);
		IN("Angle", 35.0);
		IN("Contrast", 1.6);
		COLOR(s->base);
		MASK(STUDIO_MASK_FULL, 0.5, 0.35, 0.0, false);
		V(STUDIO_VALUE_DEPTH, s->wrinkles * 0.3 * s->bump);
		CHANNELS(CH_HEIGHT, HB_CARVE);
	}
	if (s->spots > 0.0) { // Round patches: freckles, the markings of an alien
		LAYER("Spots", "PATTERN_DOTS");
		IN("Scale", s->spot_size);
		IN("Size", 0.45);
		IN("Randomness", 1.0);
		IN("Density", 0.25);
		IN("Softness", s->stylized ? 0.05 : 0.5);
		COLOR(s->spot);
		MASK(STUDIO_MASK_PATTERN, 0.6, soft, 0.0, false);
		V(STUDIO_VALUE_OPACITY, s->spots * 0.6);
		V(STUDIO_VALUE_DEPTH, 0.25 * s->bump);
		CHANNELS(CH_COLOR | CH_HEIGHT, HB_ADD);
	}
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
	// Fine wavy grain
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", s->grain_scale * 2.0);
	IN("Ridged", 1.0);
	IN("Stretch", 1.5);
	IN("Distort", 0.3);
	f32 tint[3];
	lerp3(s->base, s->stain, 0.15, tint);
	RAMP(false, 2, (f32[]){s->base[0], s->base[1], s->base[2], 0.0, tint[0], tint[1], tint[2], 1.0});
	V(STUDIO_VALUE_ROUGHNESS, s->roughness);
	V(STUDIO_VALUE_DEPTH, s->bump * (s->stylized ? 0.1 : 0.2));

	LAYER("Stains", "PATTERN_NOISE");
	IN("Scale", 3.0);
	COLOR(s->stain);
	MASK(STUDIO_MASK_PATTERN, 0.4, s->stylized ? 0.03 : 0.3, 0.0, false);
	V(STUDIO_VALUE_OPACITY, s->staining);
	V(STUDIO_VALUE_ROUGHNESS, s->roughness + 0.15);
	CHANNELS(CH_COLOR | CH_ROUGH, HB_MIX);

	LAYER("Pits", "PATTERN_DOTS");
	IN("Scale", s->stylized ? 40.0 : 60.0);
	IN("Size", 0.35);
	IN("Randomness", 1.0);
	IN("Density", 0.6);
	IN("Softness", 0.6);
	COLOR(s->dark);
	MASK(STUDIO_MASK_PATTERN, 0.5, s->stylized ? 0.02 : 0.3, 0.0, false);
	V(STUDIO_VALUE_OPACITY, s->porosity * 0.8);
	V(STUDIO_VALUE_DEPTH, 0.4 * s->bump);
	CHANNELS(CH_COLOR | CH_HEIGHT, HB_CARVE);

	LAYER("Cracks", "PATTERN_CELLS");
	IN("Scale", 4.0);
	IN("Edge Width", 0.03);
	IN("Variation", 0.0);
	IN("Distort", 0.15);
	COLOR(s->dark);
	MASK(STUDIO_MASK_PATTERN, 0.1, 0.08, 0.5, true);
	V(STUDIO_VALUE_MASK_SCALE, 8.0);
	V(STUDIO_VALUE_OPACITY, s->cracks);
	V(STUDIO_VALUE_DEPTH, 0.3 * s->bump);
	CHANNELS(CH_COLOR | CH_HEIGHT, HB_CARVE);
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
	// Muscle fibers twisting around
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", s->fiber_scale * 2.0);
	IN("Ridged", 1.0);
	IN("Persistence", 0.5);
	IN("Contrast", 1.2);
	IN("Distort", 0.3);
	IN("Distort Scale", 4.0);
	f32 mid[3];
	lerp3(s->dark, s->base, 0.6, mid);
	if (s->stylized) {
		RAMP(true, 3, (f32[]){s->dark[0], s->dark[1], s->dark[2], 0.0, mid[0], mid[1], mid[2], 0.35, s->base[0], s->base[1], s->base[2], 0.7});
	}
	else {
		f32 light[3];
		lerp3(s->base, (f32[]){1.0, 0.85, 0.85}, 0.35, light);
		RAMP(false, 3, (f32[]){s->dark[0], s->dark[1], s->dark[2], 0.0, s->base[0], s->base[1], s->base[2], 0.55, light[0], light[1], light[2], 1.0});
	}
	V(STUDIO_VALUE_ROUGHNESS, 1.0 - s->wetness * 0.85);
	V(STUDIO_VALUE_DEPTH, 0.5 * s->bump);
	V(STUDIO_VALUE_SUBSURFACE, s->subsurface);

	LAYER("Shading", "PATTERN_NOISE");
	IN("Scale", 2.0);
	COLOR(s->dark);
	MASK(STUDIO_MASK_PATTERN, 0.5, s->stylized ? 0.05 : 0.6, 0.0, true);
	V(STUDIO_VALUE_OPACITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.4 * s->bump);
	CHANNELS(CH_COLOR, HB_MIX);

	LAYER("Fat", "PATTERN_NOISE");
	IN("Scale", 4.0);
	IN("Distort", 0.4);
	IN("Seed", 7.0);
	COLOR(s->fat);
	MASK(STUDIO_MASK_PATTERN, 0.18 + s->fat_amount * 0.3, s->stylized ? 0.02 : 0.05, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 1.0 - s->wetness * 0.85);
	V(STUDIO_VALUE_DEPTH, 0.15 * s->bump);
	CHANNELS(CH_COLOR | CH_HEIGHT, HB_ADD);
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
	LAYER(NULL, "PATTERN_FIBERS");
	IN("Scale", s->density);
	IN("Angle", fmodf(s->direction, 180.0));
	IN("Length", s->length);
	IN("Clumping", s->clumping);
	IN("Clump Size", fmaxf(s->density / s->clump_size, 1.0));
	IN("Bend", 0.3);
	if (s->stylized) {
		f32 mid[3];
		lerp3(s->root, s->base, 0.55, mid);
		RAMP(true, 4,
		     (f32[]){s->root[0], s->root[1], s->root[2], 0.0, mid[0], mid[1], mid[2], 0.3, s->base[0], s->base[1], s->base[2], 0.55, s->tip[0], s->tip[1],
		             s->tip[2], 0.85});
	}
	else {
		RAMP(false, 3, (f32[]){s->root[0], s->root[1], s->root[2], 0.0, s->base[0], s->base[1], s->base[2], 0.6, s->tip[0], s->tip[1], s->tip[2], 1.0});
	}
	V(STUDIO_VALUE_ROUGHNESS, s->roughness + 0.08);
	V(STUDIO_VALUE_ROUGH_VAR, 0.15);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, s->strand_height);
}

// ███████╗ ██████╗██╗      ███████╗██╗
// ██╔════╝██╔════╝██║      ██╔════╝██║
// ███████╗██║     ██║█████╗█████╗  ██║
// ╚════██║██║     ██║╚════╝██╔══╝  ██║
// ███████║╚██████╗██║      ██║     ██║
// ╚══════╝ ╚═════╝╚═╝      ╚═╝     ╚═╝

enum {
	SF_RIVETS  = 1,  // Rivets near the panel corners
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

// The panel grid of the preset: bricks size / width across, size / row height rows (two rows of bricks a cell)
static f32 bm_panel_grid(const bm_scifi_t *s, f32 k) {
	f32 sx   = s->size / s->width * k;
	f32 rows = s->size / s->row_height * k;
	IN("Scale", sx);
	IN("Stretch", rows / (2.0 * sx));
	return fmaxf(floorf(sx + 0.5), 1.0);
}

// Seam and bevel widths the same on the model whatever the panel size: in cells of the grid
static void bm_panel_seams(const bm_scifi_t *s, f32 cells, f32 seam, f32 bevel) {
	IN("Mortar", s->seam_width * 0.9 * cells * seam);
	IN("Bevel", fmaxf(0.03 * cells * bevel, 0.005));
}

static void bm_build_scifi(const bm_scifi_t *s) {
	bool stripes = s->features & SF_STRIPES;
	bool grating = s->features & SF_GRATING;

	if (stripes) { // Diagonal bands under the panel seams
		LAYER(NULL, "PATTERN_STRIPES");
		IN("Scale", 8.0);
		IN("Angle", 45.0);
		IN("Profile", 2.0);
		IN("Width", 0.5);
		RAMP(true, 2, (f32[]){s->panel[0], s->panel[1], s->panel[2], 0.0, s->accent[0], s->accent[1], s->accent[2], 0.5});
		V(STUDIO_VALUE_ROUGHNESS, s->roughness);
		V(STUDIO_VALUE_METALLIC, s->metallic);
		V(STUDIO_VALUE_DEPTH, 0.0);
	}
	else if (grating) { // Bars of the panel color
		LAYER(NULL, NULL);
		COLOR(s->panel);
		V(STUDIO_VALUE_ROUGHNESS, s->roughness);
		V(STUDIO_VALUE_METALLIC, s->metallic);
	}
	else { // Panels: a tint each between the two panel colors, a bright rim along the seams
		LAYER(NULL, "PATTERN_BRICKS");
		bm_panel_grid(s, 1.0);
		IN("Mortar", 0.0);
		IN("Bevel", 0.0);
		bm_panel_seams(s, bm_panel_grid(s, 1.0), 1.0, 0.0);
		IN("Bevel", 0.12 / s->width);
		IN("Offset", s->offset);
		IN("Variation", 0.0);
		f32 rim[3];
		lerp3(s->panel, s->wear, s->wear_amount, rim);
		RAMP(true, 4,
		     (f32[]){s->seam[0], s->seam[1], s->seam[2], 0.0, rim[0], rim[1], rim[2], 0.01, s->panel2[0], s->panel2[1], s->panel2[2], 0.94, s->panel[0],
		             s->panel[1], s->panel[2], 0.97});
		V(STUDIO_VALUE_CELL_VAR, 0.06);
		V(STUDIO_VALUE_ROUGHNESS, s->roughness + 0.15);
		V(STUDIO_VALUE_ROUGH_VAR, 0.3);
		V(STUDIO_VALUE_METALLIC, s->metallic);
		V(STUDIO_VALUE_METAL_VAR, 1.0);
		V(STUDIO_VALUE_CAVITY, 0.5);
		V(STUDIO_VALUE_DEPTH, 0.6 * s->bump);
	}

	if (grating) { // Square holes
		LAYER("Holes", "PATTERN_TILES");
		IN("Scale", s->size * 12.0);
		IN("Gap", 0.25);
		IN("Bevel", 0.02);
		IN("Variation", 0.0);
		COLOR((f32[]){0.03, 0.03, 0.035});
		MASK(STUDIO_MASK_PATTERN, 0.5, 0.05, 0.0, false);
		V(STUDIO_VALUE_ROUGHNESS, 0.9);
		V(STUDIO_VALUE_DEPTH, 0.6 * s->bump);
		CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL | CH_HEIGHT, HB_CARVE);
	}
	if (stripes || grating) { // The panel seams over the stripes or the holes
		LAYER("Seams", "PATTERN_BRICKS");
		bm_panel_seams(s, bm_panel_grid(s, 1.0), 1.0, 0.2);
		IN("Offset", s->offset);
		IN("Variation", 0.0);
		COLOR(s->seam);
		MASK(STUDIO_MASK_PATTERN, 0.05, 0.05, 0.0, true);
		V(STUDIO_VALUE_ROUGHNESS, s->roughness + 0.3);
		V(STUDIO_VALUE_DEPTH, 0.6 * s->bump);
		CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL | CH_HEIGHT, HB_CARVE);
	}
	if (s->features & SF_DETAIL) { // Finer panel lines here and there
		LAYER("Detail Lines", "PATTERN_BRICKS");
		bm_panel_seams(s, bm_panel_grid(s, 2.0 / 0.7), 0.6, 0.2);
		IN("Offset", 0.3);
		IN("Variation", 0.0);
		IN("Seed", 4.0);
		COLOR(s->seam);
		MASK(STUDIO_MASK_PATTERN, 0.05, 0.05, 0.7, true);
		V(STUDIO_VALUE_MASK_SCALE, fmaxf(s->size * 0.8, 1.0));
		V(STUDIO_VALUE_OPACITY, s->detail);
		V(STUDIO_VALUE_DEPTH, 0.3 * s->bump);
		CHANNELS(CH_COLOR | CH_HEIGHT, HB_CARVE);
	}
	if (s->features & SF_TRACES) { // Glowing lines of two grids, broken up
		for (i32 i = 0; i < 2; ++i) {
			LAYER(i == 0 ? "Traces" : "Fine Traces", "PATTERN_BRICKS");
			IN("Scale", s->size * (i == 0 ? 3.0 : 5.3));
			IN("Stretch", i == 0 ? 1.0 : 1.3);
			IN("Mortar", i == 0 ? 0.05 : 0.06);
			IN("Bevel", 0.01);
			IN("Offset", i == 0 ? 0.5 : 0.3);
			IN("Variation", 0.0);
			IN("Seed", 11.0 + i * 5.0);
			COLOR(s->accent);
			MASK(STUDIO_MASK_PATTERN, 0.05, 0.05, i == 0 ? 0.45 : 0.6, true);
			V(STUDIO_VALUE_MASK_SCALE, fmaxf(s->size * 1.5, 1.0));
			V(STUDIO_VALUE_EMISSION, s->glow);
			V(STUDIO_VALUE_DEPTH, 0.1 * s->bump);
			CHANNELS(CH_COLOR | CH_HEIGHT | CH_EMIS, HB_ADD);
		}
	}
	if (s->features & SF_LIGHTS) { // Glowing dots here and there
		LAYER("Lights", "PATTERN_DOTS");
		IN("Scale", s->size * 6.0);
		IN("Size", 0.15);
		IN("Density", 0.12);
		IN("Softness", 0.2);
		COLOR(s->accent);
		MASK(STUDIO_MASK_PATTERN, 0.7, 0.1, 0.0, false);
		V(STUDIO_VALUE_EMISSION, s->glow);
		CHANNELS(CH_COLOR | CH_EMIS, HB_MIX);
	}
	if (s->features & SF_RIVETS) { // Dots at a quarter of the panel from its corners
		LAYER("Rivets", "PATTERN_DOTS");
		bm_panel_grid(s, 2.0);
		IN("Size", 0.08);
		IN("Softness", 0.6);
		COLOR(s->wear);
		MASK(STUDIO_MASK_PATTERN, 0.8, 0.2, 0.0, false);
		V(STUDIO_VALUE_ROUGHNESS, s->roughness);
		V(STUDIO_VALUE_METALLIC, s->metallic);
		V(STUDIO_VALUE_DEPTH, 0.3 * s->bump);
		CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL | CH_HEIGHT, HB_ADD);
	}
}


// ███████╗██╗   ██╗██████╗ ███████╗ █████╗  ██████╗███████╗███████╗
// ██╔════╝██║   ██║██╔══██╗██╔════╝██╔══██╗██╔════╝██╔════╝██╔════╝
// ███████╗██║   ██║██████╔╝█████╗  ███████║██║     █████╗  ███████╗
// ╚════██║██║   ██║██╔══██╗██╔══╝  ██╔══██║██║     ██╔══╝  ╚════██║
// ███████║╚██████╔╝██║  ██║██║     ██║  ██║╚██████╗███████╗███████║
// ╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝ ╚═════╝╚══════╝╚══════╝

// Nature and surfaces: one builder each

static void bm_fine_bumps(f32 scale, f32 depth) {
	LAYER("Fine Bumps", "PATTERN_NOISE");
	IN("Scale", scale);
	IN("Persistence", 0.6);
	COLOR((f32[]){0.5, 0.5, 0.5});
	MASK(STUDIO_MASK_FULL, 0.5, 0.35, 0.0, false);
	V(STUDIO_VALUE_DEPTH, depth);
	CHANNELS(CH_HEIGHT, HB_ADD);
}

static void bm_build_planks() {
	LAYER(NULL, "PATTERN_PLANKS");
	IN("Scale", 12.0);
	IN("Board Length", 4.0);
	IN("Gap", 0.05);
	IN("Bevel", 0.06);
	IN("Grain", 0.7);
	RAMP(false, 4, (f32[]){0.08, 0.05, 0.03, 0.0, 0.32, 0.18, 0.09, 0.2, 0.5, 0.31, 0.16, 0.65, 0.66, 0.45, 0.26, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.35);
	V(STUDIO_VALUE_ROUGHNESS, 0.55);
	V(STUDIO_VALUE_ROUGH_VAR, 0.25);
	V(STUDIO_VALUE_CAVITY, 0.5);
	V(STUDIO_VALUE_DEPTH, 0.2);
	bm_fine_bumps(60.0, 0.03);
}

static void bm_build_bark() {
	// Deep vertical furrows between long ridges
	LAYER(NULL, "PATTERN_CELLS");
	IN("Scale", 24.0);
	IN("Stretch", 0.15);
	IN("Edge Width", 0.25);
	IN("Rounding", 0.5);
	IN("Variation", 0.3);
	IN("Distort", 0.25);
	IN("Distort Scale", 8.0);
	RAMP(false, 4, (f32[]){0.06, 0.04, 0.03, 0.0, 0.22, 0.16, 0.1, 0.15, 0.36, 0.28, 0.21, 0.6, 0.47, 0.41, 0.34, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.2);
	V(STUDIO_VALUE_ROUGHNESS, 0.85);
	V(STUDIO_VALUE_ROUGH_VAR, 0.15);
	V(STUDIO_VALUE_CAVITY, 0.7);
	V(STUDIO_VALUE_DEPTH, 0.7);

	LAYER("Lichen", "PATTERN_NOISE");
	IN("Scale", 6.0);
	IN("Distort", 0.3);
	IN("Seed", 9.0);
	RAMP(false, 2, (f32[]){0.32, 0.38, 0.22, 0.0, 0.5, 0.55, 0.38, 1.0});
	MASK(STUDIO_MASK_PATTERN, 0.3, 0.1, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.95);
	V(STUDIO_VALUE_DEPTH, 0.05);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_HEIGHT, HB_ADD);
	bm_fine_bumps(40.0, 0.08);
}

static void bm_build_dirt() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 6.0);
	IN("Persistence", 0.6);
	RAMP(false, 3, (f32[]){0.16, 0.11, 0.07, 0.0, 0.3, 0.21, 0.14, 0.55, 0.42, 0.32, 0.22, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	V(STUDIO_VALUE_DEPTH, 0.25);

	LAYER("Damp", "PATTERN_NOISE");
	IN("Scale", 3.0);
	IN("Seed", 4.0);
	COLOR((f32[]){0.12, 0.08, 0.05});
	MASK(STUDIO_MASK_PATTERN, 0.35, 0.3, 0.0, false);
	V(STUDIO_VALUE_OPACITY, 0.6);
	V(STUDIO_VALUE_ROUGHNESS, 0.6);
	CHANNELS(CH_COLOR | CH_ROUGH, HB_MIX);

	LAYER("Pebbles", "PATTERN_PEBBLES");
	IN("Scale", 22.0);
	IN("Size", 0.4);
	IN("Size Jitter", 0.7);
	IN("Softness", 0.5);
	RAMP(false, 2, (f32[]){0.26, 0.21, 0.16, 0.0, 0.44, 0.39, 0.33, 1.0});
	MASK(STUDIO_MASK_PATTERN, 0.8, 0.25, 0.5, false);
	V(STUDIO_VALUE_CELL_VAR, 0.5);
	V(STUDIO_VALUE_OPACITY, 0.85);
	V(STUDIO_VALUE_ROUGHNESS, 0.75);
	V(STUDIO_VALUE_DEPTH, 0.2);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_HEIGHT, HB_ADD);
	bm_fine_bumps(50.0, 0.1);
}

static void bm_build_sand() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 30.0);
	IN("Contrast", 0.7);
	RAMP(false, 2, (f32[]){0.66, 0.53, 0.36, 0.0, 0.82, 0.7, 0.52, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.95);
	V(STUDIO_VALUE_DEPTH, 0.03);

	LAYER("Ripples", "PATTERN_STRIPES");
	IN("Scale", 10.0);
	IN("Angle", 8.0);
	IN("Waviness", 0.8);
	IN("Wave Length", 3.0);
	IN("Distort", 0.35);
	IN("Distort Scale", 3.0);
	RAMP(false, 2, (f32[]){0.6, 0.47, 0.31, 0.0, 0.84, 0.72, 0.54, 1.0});
	MASK(STUDIO_MASK_FULL, 0.5, 0.35, 0.0, false);
	V(STUDIO_VALUE_OPACITY, 0.55);
	V(STUDIO_VALUE_ROUGHNESS, 0.95);
	V(STUDIO_VALUE_DEPTH, 0.3);
	CHANNELS(CH_COLOR | CH_HEIGHT, HB_ADD);
	bm_fine_bumps(120.0, 0.04);
}

static void bm_build_grass() {
	LAYER(NULL, "PATTERN_FIBERS");
	IN("Scale", 140.0);
	IN("Length", 3.0);
	IN("Clumping", 0.5);
	IN("Clump Size", 8.0);
	IN("Bend", 0.7);
	RAMP(false, 3, (f32[]){0.1, 0.2, 0.05, 0.0, 0.2, 0.4, 0.09, 0.5, 0.45, 0.6, 0.18, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.85);
	V(STUDIO_VALUE_ROUGH_VAR, 0.1);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.6);

	LAYER("Blades", "PATTERN_FIBERS");
	IN("Scale", 110.0);
	IN("Length", 5.0);
	IN("Clumping", 0.4);
	IN("Clump Size", 10.0);
	IN("Bend", 0.9);
	IN("Angle", 12.0);
	IN("Seed", 5.0);
	RAMP(false, 2, (f32[]){0.12, 0.28, 0.05, 0.0, 0.42, 0.62, 0.15, 1.0});
	MASK(STUDIO_MASK_PATTERN, 0.45, 0.2, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.8);
	V(STUDIO_VALUE_DEPTH, 0.7);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_HEIGHT | CH_OCC, HB_MIX);

	LAYER("Dry Patches", "PATTERN_NOISE");
	IN("Scale", 3.0);
	IN("Seed", 2.0);
	COLOR((f32[]){0.5, 0.48, 0.2});
	MASK(STUDIO_MASK_PATTERN, 0.3, 0.3, 0.0, false);
	V(STUDIO_VALUE_OPACITY, 0.5);
	CHANNELS(CH_COLOR, HB_MIX);
}

static void bm_build_roof() {
	LAYER(NULL, "PATTERN_SHINGLES");
	IN("Scale", 8.0);
	IN("Stretch", 1.5);
	IN("Roundness", 1.0);
	IN("Slope", 0.6);
	IN("Variation", 0.15);
	RAMP(false, 4, (f32[]){0.12, 0.05, 0.03, 0.0, 0.42, 0.16, 0.09, 0.2, 0.62, 0.27, 0.15, 0.7, 0.72, 0.36, 0.22, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.35);
	V(STUDIO_VALUE_ROUGHNESS, 0.7);
	V(STUDIO_VALUE_ROUGH_VAR, 0.2);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.5);

	LAYER("Dirt", "PATTERN_NOISE");
	IN("Scale", 5.0);
	COLOR((f32[]){0.2, 0.15, 0.1});
	MASK(STUDIO_MASK_PATTERN, 0.35, 0.4, 0.0, false);
	V(STUDIO_VALUE_OPACITY, 0.45);
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	CHANNELS(CH_COLOR | CH_ROUGH, HB_MIX);
	bm_fine_bumps(60.0, 0.05);
}

static void bm_build_marble() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 4.0);
	IN("Contrast", 0.6);
	IN("Distort", 0.4);
	RAMP(false, 2, (f32[]){0.84, 0.83, 0.8, 0.0, 0.95, 0.94, 0.92, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.12);
	V(STUDIO_VALUE_ROUGH_VAR, 0.05);
	V(STUDIO_VALUE_SUBSURFACE, 0.3);

	LAYER("Veins", "PATTERN_NOISE");
	IN("Scale", 3.0);
	IN("Ridged", 1.0);
	IN("Distort", 0.7);
	IN("Distort Scale", 3.0);
	IN("Seed", 3.0);
	COLOR((f32[]){0.5, 0.5, 0.52});
	MASK(STUDIO_MASK_PATTERN, 0.035, 0.05, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.12);
	CHANNELS(CH_COLOR, HB_MIX);

	LAYER("Fine Veins", "PATTERN_NOISE");
	IN("Scale", 9.0);
	IN("Ridged", 1.0);
	IN("Distort", 0.5);
	IN("Seed", 8.0);
	COLOR((f32[]){0.66, 0.65, 0.66});
	MASK(STUDIO_MASK_PATTERN, 0.03, 0.04, 0.4, false);
	V(STUDIO_VALUE_OPACITY, 0.7);
	V(STUDIO_VALUE_ROUGHNESS, 0.12);
	CHANNELS(CH_COLOR, HB_MIX);
}

static void bm_build_cliff() {
	// Strata of rock, cracked, moss on the ledges
	LAYER(NULL, "PATTERN_STRIPES");
	IN("Scale", 9.0);
	IN("Angle", 8.0);
	IN("Profile", 1.0);
	IN("Waviness", 0.3);
	IN("Wave Length", 4.0);
	IN("Distort", 0.3);
	IN("Distort Scale", 5.0);
	RAMP(false, 4, (f32[]){0.2, 0.18, 0.16, 0.0, 0.36, 0.33, 0.29, 0.3, 0.5, 0.46, 0.41, 0.7, 0.62, 0.59, 0.54, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.85);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.6);

	LAYER("Cracks", "PATTERN_CELLS");
	IN("Scale", 4.0);
	IN("Stretch", 1.6);
	IN("Edge Width", 0.06);
	IN("Variation", 0.0);
	IN("Distort", 0.3);
	COLOR((f32[]){0.1, 0.09, 0.08});
	MASK(STUDIO_MASK_PATTERN, 0.12, 0.08, 0.2, true);
	V(STUDIO_VALUE_DEPTH, 0.4);
	CHANNELS(CH_COLOR | CH_HEIGHT, HB_CARVE);

	LAYER("Moss", "PATTERN_NOISE");
	IN("Scale", 12.0);
	IN("Contrast", 1.3);
	RAMP(false, 2, (f32[]){0.12, 0.2, 0.05, 0.0, 0.28, 0.4, 0.1, 1.0});
	MASK(STUDIO_MASK_UP, 0.3, 0.3, 0.6, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.95);
	V(STUDIO_VALUE_DEPTH, 0.04);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_HEIGHT, HB_ADD);
	bm_fine_bumps(30.0, 0.12);
}

static void bm_build_lava() {
	// Dark crust plates, molten rock glowing in the cracks between them
	LAYER(NULL, "PATTERN_CELLS");
	IN("Scale", 6.0);
	IN("Edge Width", 0.2);
	IN("Rounding", 0.4);
	IN("Variation", 0.3);
	IN("Distort", 0.45);
	RAMP(false, 3, (f32[]){0.25, 0.04, 0.01, 0.0, 0.06, 0.05, 0.05, 0.3, 0.16, 0.14, 0.13, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.85);
	V(STUDIO_VALUE_CAVITY, 0.3);
	V(STUDIO_VALUE_DEPTH, 0.5);

	LAYER("Molten", "PATTERN_CELLS");
	IN("Scale", 6.0);
	IN("Edge Width", 0.2);
	IN("Rounding", 0.4);
	IN("Variation", 0.3);
	IN("Distort", 0.45);
	RAMP(false, 3, (f32[]){1.0, 0.75, 0.2, 0.0, 0.95, 0.3, 0.03, 0.12, 0.45, 0.06, 0.01, 0.3});
	MASK(STUDIO_MASK_PATTERN, 0.22, 0.25, 0.0, true);
	V(STUDIO_VALUE_ROUGHNESS, 0.4);
	V(STUDIO_VALUE_EMISSION, 1.0);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_EMIS, HB_MIX);
	bm_fine_bumps(25.0, 0.1);
}

static void bm_build_carpet() {
	LAYER(NULL, "PATTERN_FIBERS");
	IN("Scale", 220.0);
	IN("Length", 1.5);
	IN("Clumping", 0.8);
	IN("Clump Size", 5.0);
	IN("Bend", 0.6);
	RAMP(false, 3, (f32[]){0.18, 0.03, 0.04, 0.0, 0.42, 0.08, 0.1, 0.6, 0.56, 0.14, 0.15, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.95);
	V(STUDIO_VALUE_CAVITY, 0.5);
	V(STUDIO_VALUE_DEPTH, 0.3);

	LAYER("Wear", "PATTERN_NOISE");
	IN("Scale", 3.0);
	COLOR((f32[]){0.6, 0.2, 0.2});
	MASK(STUDIO_MASK_PATTERN, 0.3, 0.4, 0.0, false);
	V(STUDIO_VALUE_OPACITY, 0.35);
	CHANNELS(CH_COLOR, HB_MIX);
}

static void bm_build_wool() {
	LAYER(NULL, "PATTERN_WEAVE");
	IN("Scale", 14.0);
	IN("Thread Width", 0.9);
	IN("Fiber", 0.7);
	RAMP(false, 3, (f32[]){0.42, 0.38, 0.32, 0.0, 0.76, 0.71, 0.62, 0.6, 0.9, 0.86, 0.78, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 1.0);
	V(STUDIO_VALUE_CAVITY, 0.5);
	V(STUDIO_VALUE_DEPTH, 0.45);
	V(STUDIO_VALUE_SUBSURFACE, 0.2);

	LAYER("Fuzz", "PATTERN_FIBERS");
	IN("Scale", 250.0);
	IN("Length", 1.5);
	IN("Clumping", 0.3);
	IN("Bend", 1.0);
	COLOR((f32[]){0.92, 0.89, 0.82});
	MASK(STUDIO_MASK_PATTERN, 0.35, 0.3, 0.0, false);
	V(STUDIO_VALUE_OPACITY, 0.4);
	V(STUDIO_VALUE_DEPTH, 0.05);
	CHANNELS(CH_COLOR | CH_HEIGHT, HB_ADD);
}

static void bm_build_gold() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 10.0);
	IN("Contrast", 1.2);
	RAMP(false, 2, (f32[]){0.82, 0.58, 0.22, 0.0, 1.0, 0.78, 0.36, 1.0});
	V(STUDIO_VALUE_METALLIC, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.28);
	V(STUDIO_VALUE_ROUGH_VAR, 0.25);
	V(STUDIO_VALUE_DEPTH, 0.01);

	LAYER("Scratches", "PATTERN_SCRATCHES");
	IN("Scale", 8.0);
	COLOR((f32[]){1.0, 0.84, 0.45});
	MASK(STUDIO_MASK_PATTERN, 0.5, 0.3, 0.0, true);
	V(STUDIO_VALUE_METALLIC, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.5);
	V(STUDIO_VALUE_OPACITY, 0.7);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL, HB_MIX);

	LAYER("Polished Edges", NULL);
	COLOR((f32[]){1.0, 0.86, 0.5});
	MASK(STUDIO_MASK_EDGES, 0.5, 0.35, 0.5, false);
	V(STUDIO_VALUE_METALLIC, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.12);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL, HB_MIX);

	LAYER("Tarnish", NULL);
	COLOR((f32[]){0.22, 0.16, 0.08});
	MASK(STUDIO_MASK_CAVITIES, 0.5, 0.35, 0.5, false);
	V(STUDIO_VALUE_OPACITY, 0.8);
	V(STUDIO_VALUE_ROUGHNESS, 0.7);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL, HB_MIX);
}

static void bm_build_copper() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 8.0);
	IN("Contrast", 1.3);
	RAMP(false, 2, (f32[]){0.62, 0.32, 0.2, 0.0, 0.95, 0.6, 0.45, 1.0});
	V(STUDIO_VALUE_METALLIC, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.35);
	V(STUDIO_VALUE_ROUGH_VAR, 0.3);
	V(STUDIO_VALUE_DEPTH, 0.01);

	LAYER("Patina", "PATTERN_NOISE");
	IN("Scale", 5.0);
	IN("Distort", 0.4);
	IN("Seed", 6.0);
	RAMP(false, 2, (f32[]){0.2, 0.45, 0.38, 0.0, 0.42, 0.7, 0.6, 1.0});
	MASK(STUDIO_MASK_PATTERN, 0.45, 0.15, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	V(STUDIO_VALUE_DEPTH, 0.03);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL | CH_HEIGHT, HB_ADD);

	LAYER("Patina in Cavities", NULL);
	COLOR((f32[]){0.3, 0.58, 0.5});
	MASK(STUDIO_MASK_CAVITIES, 0.6, 0.35, 0.5, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL, HB_MIX);
}


// ██████╗  █████╗ ████████╗ ██████╗██╗  ██╗    ██████╗
// ██╔══██╗██╔══██╗╚══██╔══╝██╔════╝██║  ██║    ╚════██╗
// ██████╔╝███████║   ██║   ██║     ███████║     █████╔╝
// ██╔══██╗██╔══██║   ██║   ██║     ██╔══██║     ╚═══██╗
// ██████╔╝██║  ██║   ██║   ╚██████╗██║  ██║    ██████╔╝
// ╚═════╝ ╚═╝  ╚═╝   ╚═╝    ╚═════╝╚═╝  ╚═╝    ╚═════╝

// Floors, walls, fabric, industrial, nature and fantasy materials built on the newer patterns

static void bm_grime(f32 scale, f32 opacity) {
	LAYER("Grime", "PATTERN_NOISE");
	IN("Scale", scale);
	IN("Seed", 13.0);
	COLOR((f32[]){0.1, 0.08, 0.06});
	MASK(STUDIO_MASK_PATTERN, 0.35, 0.4, 0.0, false);
	V(STUDIO_VALUE_OPACITY, opacity);
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	CHANNELS(CH_COLOR | CH_ROUGH, HB_MIX);
}

static void bm_wood_ramp() {
	RAMP(false, 4, (f32[]){0.08, 0.05, 0.03, 0.0, 0.34, 0.19, 0.09, 0.2, 0.52, 0.32, 0.16, 0.65, 0.68, 0.46, 0.26, 1.0});
}

static void bm_build_herringbone() {
	LAYER(NULL, "PATTERN_HERRINGBONE");
	IN("Scale", 12.0);
	IN("Angle", 45.0);
	IN("Length", 4.0);
	bm_wood_ramp();
	V(STUDIO_VALUE_CELL_VAR, 0.35);
	V(STUDIO_VALUE_ROUGHNESS, 0.5);
	V(STUDIO_VALUE_ROUGH_VAR, 0.2);
	V(STUDIO_VALUE_CAVITY, 0.5);
	V(STUDIO_VALUE_DEPTH, 0.2);
	bm_fine_bumps(80.0, 0.03);
}

static void bm_build_parquet() {
	LAYER(NULL, "PATTERN_PARQUET");
	IN("Scale", 6.0);
	IN("Strips", 3.0);
	RAMP(false, 3, (f32[]){0.12, 0.08, 0.05, 0.0, 0.56, 0.4, 0.24, 0.3, 0.74, 0.58, 0.38, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.3);
	V(STUDIO_VALUE_ROUGHNESS, 0.45);
	V(STUDIO_VALUE_ROUGH_VAR, 0.2);
	V(STUDIO_VALUE_CAVITY, 0.5);
	V(STUDIO_VALUE_DEPTH, 0.15);
	bm_fine_bumps(80.0, 0.03);
}

static void bm_build_castle() {
	LAYER(NULL, "PATTERN_UNEVEN");
	IN("Scale", 3.0);
	IN("Splits", 3.0);
	IN("Gap", 0.025);
	IN("Bevel", 0.06);
	IN("Variation", 0.3);
	IN("Distort", 0.06);
	RAMP(false, 4, (f32[]){0.12, 0.11, 0.1, 0.0, 0.32, 0.3, 0.27, 0.2, 0.45, 0.43, 0.39, 0.6, 0.58, 0.55, 0.5, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.4);
	V(STUDIO_VALUE_ROUGHNESS, 0.85);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.5);
	bm_grime(4.0, 0.4);
	bm_fine_bumps(40.0, 0.1);
}

static void bm_build_seigaiha() {
	LAYER(NULL, "PATTERN_SEIGAIHA");
	IN("Scale", 6.0);
	IN("Rings", 4.0);
	RAMP(true, 3, (f32[]){0.1, 0.18, 0.42, 0.0, 0.86, 0.88, 0.92, 0.45, 0.3, 0.5, 0.8, 0.75});
	V(STUDIO_VALUE_CELL_VAR, 0.85);
	V(STUDIO_VALUE_CELL_COLORS, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.2);
	V(STUDIO_VALUE_ROUGH_VAR, 0.3);
	V(STUDIO_VALUE_DEPTH, 0.1);
}

static void bm_build_arcs() {
	LAYER(NULL, "PATTERN_ARCS");
	IN("Scale", 3.0);
	IN("Rings", 5.0);
	IN("Gap", 0.015);
	RAMP(false, 3, (f32[]){0.2, 0.19, 0.17, 0.0, 0.48, 0.46, 0.42, 0.25, 0.66, 0.63, 0.58, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.5);
	V(STUDIO_VALUE_ROUGHNESS, 0.8);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.35);
	bm_grime(5.0, 0.35);
}

static void bm_build_moroccan() {
	// Grout, then the crosses and the stars told apart by the cell value
	LAYER(NULL, "PATTERN_STARS");
	IN("Scale", 6.0);
	IN("Star Size", 0.7);
	RAMP(true, 3, (f32[]){0.85, 0.82, 0.75, 0.0, 0.1, 0.35, 0.45, 0.5, 0.75, 0.45, 0.2, 0.78});
	V(STUDIO_VALUE_CELL_VAR, 0.5);
	V(STUDIO_VALUE_CELL_COLORS, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.25);
	V(STUDIO_VALUE_ROUGH_VAR, 0.4);
	V(STUDIO_VALUE_DEPTH, 0.12);
}

static void bm_build_truchet() {
	LAYER(NULL, "PATTERN_TRUCHET");
	IN("Scale", 8.0);
	IN("Width", 0.3);
	RAMP(true, 2, (f32[]){0.9, 0.87, 0.8, 0.0, 0.15, 0.3, 0.5, 0.4});
	V(STUDIO_VALUE_ROUGHNESS, 0.3);
	V(STUDIO_VALUE_DEPTH, 0.12);
}

static void bm_build_checker() {
	LAYER(NULL, "PATTERN_TILES");
	IN("Scale", 8.0);
	IN("Gap", 0.03);
	IN("Checker", 1.0);
	IN("Variation", 0.03);
	RAMP(true, 3, (f32[]){0.5, 0.5, 0.48, 0.0, 0.06, 0.06, 0.06, 0.48, 0.9, 0.9, 0.88, 0.8});
	V(STUDIO_VALUE_CELL_VAR, 0.5);
	V(STUDIO_VALUE_CELL_COLORS, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.2);
	V(STUDIO_VALUE_ROUGH_VAR, 0.4);
	V(STUDIO_VALUE_DEPTH, 0.08);
}

static void bm_build_terrazzo() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 20.0);
	IN("Contrast", 0.5);
	RAMP(false, 2, (f32[]){0.82, 0.8, 0.76, 0.0, 0.9, 0.88, 0.85, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.25);

	LAYER("Chips", "PATTERN_SCATTER");
	IN("Scale", 14.0);
	IN("Shape", 0.0);
	IN("Size", 0.45);
	IN("Size Jitter", 0.6);
	IN("Density", 0.9);
	RAMP(true, 4, (f32[]){0.2, 0.2, 0.22, 0.0, 0.75, 0.35, 0.25, 0.3, 0.45, 0.55, 0.5, 0.55, 0.95, 0.85, 0.6, 0.8});
	V(STUDIO_VALUE_CELL_VAR, 1.0);
	V(STUDIO_VALUE_CELL_COLORS, 1.0);
	MASK(STUDIO_MASK_PATTERN, 0.5, 0.05, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.2);
	CHANNELS(CH_COLOR | CH_ROUGH, HB_MIX);
}

static void bm_build_mosaic() {
	LAYER(NULL, "PATTERN_TILES");
	IN("Scale", 30.0);
	IN("Gap", 0.1);
	IN("Bevel", 0.1);
	IN("Distort", 0.12);
	RAMP(true, 4, (f32[]){0.1, 0.25, 0.5, 0.0, 0.2, 0.45, 0.65, 0.3, 0.85, 0.7, 0.3, 0.6, 0.9, 0.9, 0.85, 0.85});
	V(STUDIO_VALUE_CELL_VAR, 1.0);
	V(STUDIO_VALUE_CELL_COLORS, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.3);
	V(STUDIO_VALUE_DEPTH, 0.15);

	LAYER("Grout", "PATTERN_TILES");
	IN("Scale", 30.0);
	IN("Gap", 0.1);
	IN("Bevel", 0.1);
	IN("Distort", 0.12);
	COLOR((f32[]){0.55, 0.53, 0.5});
	MASK(STUDIO_MASK_PATTERN, 0.1, 0.1, 0.0, true);
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	CHANNELS(CH_COLOR | CH_ROUGH, HB_MIX);
}

static void bm_build_chevron() {
	LAYER(NULL, "PATTERN_STRIPES");
	IN("Scale", 8.0);
	IN("Profile", 2.0);
	IN("Width", 0.5);
	IN("Zigzag", 1.0);
	RAMP(true, 2, (f32[]){0.92, 0.9, 0.85, 0.0, 0.15, 0.3, 0.32, 0.5});
	V(STUDIO_VALUE_ROUGHNESS, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.02);
}

static void bm_build_damask() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 40.0);
	IN("Contrast", 0.4);
	RAMP(false, 2, (f32[]){0.32, 0.04, 0.06, 0.0, 0.4, 0.06, 0.08, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.7);

	LAYER("Ornament", "PATTERN_MOTIF");
	IN("Scale", 5.0);
	IN("Petals", 6.0);
	IN("Size", 0.85);
	RAMP(false, 2, (f32[]){0.55, 0.42, 0.2, 0.0, 0.8, 0.65, 0.35, 1.0});
	MASK(STUDIO_MASK_PATTERN, 0.6, 0.1, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.35);
	V(STUDIO_VALUE_METALLIC, 0.5);
	V(STUDIO_VALUE_DEPTH, 0.04);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL | CH_HEIGHT, HB_ADD);
}

static void bm_build_thatch() {
	LAYER(NULL, "PATTERN_THATCH");
	IN("Scale", 5.0);
	IN("Strand Density", 40.0);
	IN("Slope", 0.7);
	RAMP(false, 3, (f32[]){0.18, 0.13, 0.07, 0.0, 0.5, 0.4, 0.22, 0.5, 0.72, 0.62, 0.38, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.95);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.6);
}

static void bm_build_osb() {
	LAYER(NULL, NULL);
	COLOR((f32[]){0.55, 0.42, 0.26});
	V(STUDIO_VALUE_ROUGHNESS, 0.8);
	for (i32 i = 0; i < 2; ++i) {
		LAYER(i == 0 ? "Flakes" : "More Flakes", "PATTERN_SCATTER");
		IN("Scale", i == 0 ? 8.0 : 11.0);
		IN("Shape", 0.0);
		IN("Size", 1.0);
		IN("Size Jitter", 0.4);
		IN("Density", 1.0);
		IN("Seed", 3.0 + i * 7.0);
		RAMP(false, 2, (f32[]){0.45, 0.32, 0.17, 0.0, 0.78, 0.63, 0.42, 1.0});
		V(STUDIO_VALUE_CELL_VAR, 0.6);
		MASK(STUDIO_MASK_PATTERN, 0.5, 0.05, 0.0, false);
		V(STUDIO_VALUE_ROUGHNESS, 0.75);
		V(STUDIO_VALUE_DEPTH, 0.08);
		CHANNELS(CH_COLOR | CH_ROUGH | CH_HEIGHT, HB_ADD);
	}
}

// Fabric and leather

static void bm_build_denim() {
	LAYER(NULL, "PATTERN_WEAVE");
	IN("Scale", 60.0);
	IN("Thread Width", 1.0);
	IN("Fiber", 0.5);
	IN("Twill", 1.0);
	RAMP(false, 3, (f32[]){0.05, 0.08, 0.16, 0.0, 0.12, 0.2, 0.38, 0.6, 0.45, 0.52, 0.65, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.95);
	V(STUDIO_VALUE_DEPTH, 0.2);

	LAYER("Fading", "PATTERN_NOISE");
	IN("Scale", 3.0);
	COLOR((f32[]){0.5, 0.58, 0.72});
	MASK(STUDIO_MASK_PATTERN, 0.4, 0.5, 0.0, false);
	V(STUDIO_VALUE_OPACITY, 0.4);
	CHANNELS(CH_COLOR, HB_MIX);
}

static void bm_build_wicker() {
	LAYER(NULL, "PATTERN_WEAVE");
	IN("Scale", 10.0);
	IN("Thread Width", 0.75);
	IN("Fiber", 0.6);
	RAMP(false, 3, (f32[]){0.1, 0.07, 0.04, 0.0, 0.45, 0.33, 0.18, 0.3, 0.72, 0.58, 0.36, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.7);
	V(STUDIO_VALUE_CAVITY, 0.7);
	V(STUDIO_VALUE_DEPTH, 0.6);
}

static void bm_build_rope() {
	LAYER(NULL, "PATTERN_ROPE");
	IN("Scale", 6.0);
	IN("Strands", 3.0);
	IN("Twist", 2.5);
	RAMP(false, 3, (f32[]){0.12, 0.09, 0.05, 0.0, 0.48, 0.38, 0.22, 0.5, 0.7, 0.6, 0.4, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.7);
}

static void bm_leather_grain(f32 scale) {
	LAYER(NULL, "PATTERN_CELLS");
	IN("Scale", scale);
	IN("Contrast", 0.7);
	IN("Edge Width", 0.3);
	IN("Variation", 0.2);
	IN("Distort", 0.2);
}

static void bm_build_stitched() {
	bm_leather_grain(60.0);
	RAMP(false, 2, (f32[]){0.2, 0.1, 0.05, 0.0, 0.36, 0.19, 0.1, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.55);
	V(STUDIO_VALUE_ROUGH_VAR, 0.2);
	V(STUDIO_VALUE_DEPTH, 0.04);

	LAYER("Stitches", "PATTERN_STITCHES");
	IN("Scale", 8.0);
	IN("Double", 1.0);
	IN("Width", 0.05);
	IN("Length", 0.5);
	COLOR((f32[]){0.88, 0.82, 0.68});
	MASK(STUDIO_MASK_PATTERN, 0.5, 0.1, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.8);
	V(STUDIO_VALUE_DEPTH, 0.1);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_HEIGHT, HB_ADD);
}

static void bm_build_chesterfield() {
	LAYER(NULL, "PATTERN_TUFTED");
	IN("Scale", 8.0);
	RAMP(false, 3, (f32[]){0.08, 0.03, 0.02, 0.0, 0.3, 0.1, 0.06, 0.5, 0.45, 0.18, 0.1, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.45);
	V(STUDIO_VALUE_ROUGH_VAR, 0.2);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.6);

	LAYER("Grain", "PATTERN_CELLS");
	IN("Scale", 90.0);
	IN("Edge Width", 0.3);
	COLOR((f32[]){0.4, 0.15, 0.08});
	MASK(STUDIO_MASK_FULL, 0.5, 0.35, 0.0, false);
	V(STUDIO_VALUE_DEPTH, 0.03);
	CHANNELS(CH_HEIGHT, HB_ADD);
}

static void bm_build_lace() {
	// A net with holes, flowers filling them
	LAYER(NULL, "PATTERN_MESH");
	IN("Scale", 36.0);
	IN("Thickness", 0.18);
	IN("Weave", 0.3);
	COLOR((f32[]){0.95, 0.93, 0.88});
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	V(STUDIO_VALUE_DEPTH, 0.15);
	V(STUDIO_VALUE_CUTOUT, 0.15);

	LAYER("Flowers", "PATTERN_MOTIF");
	IN("Scale", 6.0);
	IN("Petals", 5.0);
	IN("Size", 0.8);
	COLOR((f32[]){0.97, 0.95, 0.9});
	MASK(STUDIO_MASK_PATTERN, 0.6, 0.05, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	V(STUDIO_VALUE_DEPTH, 0.2);
	CHANNELS(CH_COLOR | CH_HEIGHT | CH_OPAC, HB_ADD);
}

static void bm_build_gauze() {
	LAYER(NULL, "PATTERN_WEAVE");
	IN("Scale", 40.0);
	IN("Thread Width", 0.35);
	IN("Fiber", 0.4);
	COLOR((f32[]){0.92, 0.9, 0.84});
	V(STUDIO_VALUE_ROUGHNESS, 1.0);
	V(STUDIO_VALUE_DEPTH, 0.2);
	V(STUDIO_VALUE_CUTOUT, 0.3);
}

static void bm_build_plaid() {
	LAYER(NULL, "PATTERN_STRIPES");
	IN("Scale", 6.0);
	IN("Angle", 90.0);
	IN("Profile", 2.0);
	IN("Width", 0.45);
	RAMP(true, 2, (f32[]){0.55, 0.06, 0.06, 0.0, 0.12, 0.08, 0.08, 0.5});
	V(STUDIO_VALUE_ROUGHNESS, 0.9);

	LAYER("Cross Bands", "PATTERN_STRIPES");
	IN("Scale", 6.0);
	IN("Profile", 2.0);
	IN("Width", 0.45);
	COLOR((f32[]){0.05, 0.2, 0.12});
	MASK(STUDIO_MASK_PATTERN, 0.5, 0.02, 0.0, false);
	V(STUDIO_VALUE_OPACITY, 0.55);
	CHANNELS(CH_COLOR, HB_MIX);

	LAYER("Thin Lines", "PATTERN_STRIPES");
	IN("Scale", 12.0);
	IN("Angle", 90.0);
	IN("Profile", 2.0);
	IN("Width", 0.08);
	COLOR((f32[]){0.9, 0.75, 0.2});
	MASK(STUDIO_MASK_PATTERN, 0.5, 0.02, 0.0, false);
	V(STUDIO_VALUE_OPACITY, 0.7);
	CHANNELS(CH_COLOR, HB_MIX);
	bm_fine_bumps(120.0, 0.04);
}

static void bm_build_paper() {
	LAYER(NULL, "PATTERN_FACETS");
	IN("Scale", 12.0);
	IN("Slope", 0.8);
	IN("Crease", 0.06);
	RAMP(false, 2, (f32[]){0.8, 0.78, 0.72, 0.0, 0.93, 0.92, 0.88, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	V(STUDIO_VALUE_DEPTH, 0.25);
	bm_fine_bumps(150.0, 0.02);
}

// Industrial

static void bm_build_carbon() {
	LAYER(NULL, "PATTERN_WEAVE");
	IN("Scale", 40.0);
	IN("Thread Width", 1.0);
	IN("Fiber", 0.0);
	IN("Twill", 1.0);
	RAMP(false, 2, (f32[]){0.01, 0.01, 0.012, 0.0, 0.1, 0.1, 0.11, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.2);
	V(STUDIO_VALUE_ROUGH_VAR, 0.15);
	V(STUDIO_VALUE_METALLIC, 0.3);
	V(STUDIO_VALUE_DEPTH, 0.05);
}

static void bm_build_tread() {
	LAYER(NULL, "PATTERN_TREAD");
	IN("Scale", 10.0);
	RAMP(false, 2, (f32[]){0.45, 0.46, 0.48, 0.0, 0.7, 0.71, 0.73, 1.0});
	V(STUDIO_VALUE_METALLIC, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.4);
	V(STUDIO_VALUE_ROUGH_VAR, 0.2);
	V(STUDIO_VALUE_DEPTH, 0.3);
	bm_grime(5.0, 0.3);
}

static void bm_build_chainmail() {
	LAYER(NULL, "PATTERN_CHAINMAIL");
	IN("Scale", 14.0);
	RAMP(false, 2, (f32[]){0.1, 0.1, 0.11, 0.0, 0.62, 0.63, 0.65, 1.0});
	V(STUDIO_VALUE_METALLIC, 1.0);
	V(STUDIO_VALUE_METAL_VAR, 0.8);
	V(STUDIO_VALUE_ROUGHNESS, 0.35);
	V(STUDIO_VALUE_CAVITY, 0.8);
	V(STUDIO_VALUE_DEPTH, 0.6);
	V(STUDIO_VALUE_CUTOUT, 0.05);
}

static void bm_build_fence() {
	LAYER(NULL, "PATTERN_MESH");
	IN("Scale", 12.0);
	IN("Thickness", 0.18);
	RAMP(false, 2, (f32[]){0.35, 0.37, 0.38, 0.0, 0.7, 0.72, 0.73, 1.0});
	V(STUDIO_VALUE_METALLIC, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.45);
	V(STUDIO_VALUE_DEPTH, 0.4);
	V(STUDIO_VALUE_CUTOUT, 0.1);

	LAYER("Rust", "PATTERN_NOISE");
	IN("Scale", 6.0);
	RAMP(false, 2, (f32[]){0.3, 0.12, 0.05, 0.0, 0.55, 0.28, 0.1, 1.0});
	MASK(STUDIO_MASK_PATTERN, 0.3, 0.15, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL, HB_MIX);
}

static void bm_build_nuts() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 10.0);
	RAMP(false, 2, (f32[]){0.12, 0.12, 0.13, 0.0, 0.22, 0.22, 0.24, 1.0});
	V(STUDIO_VALUE_METALLIC, 0.8);
	V(STUDIO_VALUE_ROUGHNESS, 0.6);

	LAYER("Nuts", "PATTERN_SCATTER");
	IN("Scale", 6.0);
	IN("Shape", 3.0);
	IN("Size", 0.6);
	IN("Size Jitter", 0.3);
	IN("Density", 0.7);
	RAMP(false, 2, (f32[]){0.4, 0.4, 0.42, 0.0, 0.75, 0.75, 0.78, 1.0});
	MASK(STUDIO_MASK_PATTERN, 0.55, 0.05, 0.0, false);
	V(STUDIO_VALUE_METALLIC, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.35);
	V(STUDIO_VALUE_DEPTH, 0.5);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL | CH_HEIGHT, HB_ADD);
}

static void bm_build_radial_copper() {
	LAYER(NULL, "PATTERN_RADIAL");
	IN("Scale", 1.0);
	IN("Density", 250.0);
	RAMP(false, 2, (f32[]){0.7, 0.38, 0.25, 0.0, 0.97, 0.65, 0.5, 1.0});
	V(STUDIO_VALUE_METALLIC, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.3);
	V(STUDIO_VALUE_ROUGH_VAR, 0.3);
	V(STUDIO_VALUE_DEPTH, 0.02);
}

static void bm_build_streaks() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 25.0);
	RAMP(false, 2, (f32[]){0.45, 0.45, 0.43, 0.0, 0.62, 0.62, 0.6, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	V(STUDIO_VALUE_DEPTH, 0.06);

	LAYER("Rust Streaks", "PATTERN_STREAKS");
	IN("Scale", 1.0);
	IN("Density", 15.0);
	studio_graph_set_mapping(bs_c, bs_k, true); // Down in the world
	RAMP(false, 2, (f32[]){0.32, 0.14, 0.05, 0.0, 0.52, 0.26, 0.1, 1.0});
	MASK(STUDIO_MASK_PATTERN, 0.75, 0.5, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.8);
	CHANNELS(CH_COLOR | CH_ROUGH, HB_MIX);
	bm_fine_bumps(60.0, 0.05);
}

static void bm_build_bullets() {
	LAYER(NULL, "PATTERN_CRATERS");
	IN("Scale", 4.0);
	IN("Size", 0.25);
	IN("Density", 0.5);
	IN("Rim", 0.2);
	IN("Cracks", 1.0);
	IN("Depth", 1.0);
	RAMP(false, 3, (f32[]){0.08, 0.08, 0.08, 0.0, 0.5, 0.5, 0.48, 0.45, 0.62, 0.62, 0.6, 0.65});
	V(STUDIO_VALUE_ROUGHNESS, 0.85);
	V(STUDIO_VALUE_CAVITY, 0.7);
	V(STUDIO_VALUE_DEPTH, 0.4);
	bm_fine_bumps(50.0, 0.08);
}

// More nature

static void bm_build_leaves() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 6.0);
	RAMP(false, 2, (f32[]){0.14, 0.1, 0.06, 0.0, 0.28, 0.2, 0.12, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	for (i32 i = 0; i < 2; ++i) {
		LAYER(i == 0 ? "Leaves" : "More Leaves", "PATTERN_SCATTER");
		IN("Scale", i == 0 ? 6.0 : 9.0);
		IN("Shape", 1.0);
		IN("Size", 0.8);
		IN("Size Jitter", 0.4);
		IN("Density", 0.9);
		IN("Seed", 5.0 + i * 9.0);
		RAMP(true, 4, (f32[]){0.45, 0.16, 0.05, 0.0, 0.7, 0.38, 0.08, 0.3, 0.55, 0.45, 0.12, 0.55, 0.35, 0.18, 0.08, 0.8});
		V(STUDIO_VALUE_CELL_VAR, 1.0);
		V(STUDIO_VALUE_CELL_COLORS, 1.0);
		MASK(STUDIO_MASK_PATTERN, 0.5, 0.05, 0.0, false);
		V(STUDIO_VALUE_ROUGHNESS, 0.75);
		V(STUDIO_VALUE_DEPTH, 0.15);
		CHANNELS(CH_COLOR | CH_ROUGH | CH_HEIGHT, HB_ADD);
	}
}

static void bm_build_ivy() {
	LAYER(NULL, "PATTERN_BRICKS");
	IN("Scale", 6.0);
	RAMP(false, 3, (f32[]){0.6, 0.58, 0.54, 0.0, 0.4, 0.16, 0.1, 0.15, 0.58, 0.25, 0.16, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.4);
	V(STUDIO_VALUE_ROUGHNESS, 0.85);
	V(STUDIO_VALUE_DEPTH, 0.2);

	LAYER("Ivy", "PATTERN_SCATTER");
	IN("Scale", 10.0);
	IN("Shape", 1.0);
	IN("Size", 0.9);
	IN("Size Jitter", 0.3);
	IN("Density", 1.0);
	RAMP(true, 3, (f32[]){0.08, 0.2, 0.05, 0.0, 0.15, 0.35, 0.08, 0.35, 0.25, 0.48, 0.12, 0.7});
	V(STUDIO_VALUE_CELL_VAR, 1.0);
	V(STUDIO_VALUE_CELL_COLORS, 1.0);
	MASK(STUDIO_MASK_PATTERN, 0.5, 0.05, 0.4, false);
	V(STUDIO_VALUE_MASK_SCALE, 3.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.3);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_HEIGHT, HB_ADD);
}

static void bm_build_moon() {
	LAYER(NULL, "PATTERN_CRATERS");
	IN("Scale", 4.0);
	IN("Density", 0.7);
	IN("Rim", 0.6);
	IN("Depth", 0.7);
	RAMP(false, 2, (f32[]){0.2, 0.2, 0.2, 0.0, 0.62, 0.61, 0.6, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	V(STUDIO_VALUE_DEPTH, 0.6);
	bm_fine_bumps(40.0, 0.1);
}

static void bm_build_amethyst() {
	LAYER(NULL, "PATTERN_FACETS");
	IN("Scale", 10.0);
	IN("Slope", 1.2);
	IN("Crease", 0.03);
	RAMP(false, 3, (f32[]){0.12, 0.04, 0.2, 0.0, 0.42, 0.18, 0.62, 0.55, 0.8, 0.65, 0.95, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.1);
	V(STUDIO_VALUE_SUBSURFACE, 0.4);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.6);
}

static void bm_build_basalt() {
	LAYER(NULL, "PATTERN_HEX");
	IN("Scale", 6.0);
	IN("Gap", 0.05);
	IN("Bevel", 0.15);
	IN("Variation", 0.5);
	RAMP(false, 3, (f32[]){0.05, 0.05, 0.05, 0.0, 0.18, 0.18, 0.19, 0.3, 0.3, 0.3, 0.31, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.3);
	V(STUDIO_VALUE_ROUGHNESS, 0.85);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.6);
	bm_fine_bumps(40.0, 0.1);
}

static void bm_build_malachite() {
	LAYER(NULL, "PATTERN_RINGS");
	IN("Scale", 3.0);
	IN("Size", 2.0);
	IN("Rings", 14.0);
	IN("Wobble", 1.2);
	IN("Bark", 0.0);
	IN("Distort", 0.4);
	RAMP(false, 3, (f32[]){0.02, 0.18, 0.1, 0.0, 0.05, 0.45, 0.25, 0.5, 0.35, 0.75, 0.5, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.8);
	V(STUDIO_VALUE_CELL_COLORS, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.15);
}

static void bm_build_ice() {
	LAYER(NULL, "PATTERN_NOISE");
	IN("Scale", 4.0);
	IN("Contrast", 0.6);
	RAMP(false, 2, (f32[]){0.32, 0.52, 0.7, 0.0, 0.6, 0.76, 0.88, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.05);
	V(STUDIO_VALUE_SUBSURFACE, 0.6);

	LAYER("Cracks", "PATTERN_CELLS");
	IN("Scale", 4.0);
	IN("Edge Width", 0.03);
	IN("Distort", 0.3);
	COLOR((f32[]){0.92, 0.96, 1.0});
	MASK(STUDIO_MASK_PATTERN, 0.12, 0.06, 0.2, true);
	V(STUDIO_VALUE_ROUGHNESS, 0.3);
	CHANNELS(CH_COLOR | CH_ROUGH, HB_MIX);

	LAYER("Frost", "PATTERN_NOISE");
	IN("Scale", 8.0);
	IN("Seed", 4.0);
	COLOR((f32[]){0.9, 0.94, 0.97});
	MASK(STUDIO_MASK_PATTERN, 0.3, 0.3, 0.0, false);
	V(STUDIO_VALUE_ROUGHNESS, 0.7);
	V(STUDIO_VALUE_OPACITY, 0.7);
	CHANNELS(CH_COLOR | CH_ROUGH, HB_MIX);
}

static void bm_build_logs() {
	LAYER(NULL, "PATTERN_RINGS");
	IN("Scale", 4.0);
	IN("Size", 0.58);
	IN("Rings", 8.0);
	IN("Bark", 0.15);
	RAMP(false, 4, (f32[]){0.04, 0.03, 0.02, 0.0, 0.3, 0.18, 0.1, 0.15, 0.6, 0.42, 0.25, 0.5, 0.78, 0.62, 0.42, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.6);
	V(STUDIO_VALUE_CELL_COLORS, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.8);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.5);
}

// Fantasy and organic

static void bm_build_cobweb() {
	LAYER(NULL, "PATTERN_BRICKS");
	IN("Scale", 5.0);
	RAMP(false, 3, (f32[]){0.08, 0.08, 0.08, 0.0, 0.22, 0.21, 0.2, 0.15, 0.35, 0.34, 0.32, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.4);
	V(STUDIO_VALUE_ROUGHNESS, 0.9);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.35);

	LAYER("Webs", "PATTERN_WEB");
	IN("Scale", 3.0);
	IN("Thickness", 0.3);
	COLOR((f32[]){0.85, 0.85, 0.82});
	MASK(STUDIO_MASK_PATTERN, 0.5, 0.3, 0.3, false);
	V(STUDIO_VALUE_OPACITY, 0.7);
	CHANNELS(CH_COLOR, HB_MIX);
}

static void bm_build_coins() {
	LAYER(NULL, NULL);
	COLOR((f32[]){0.2, 0.13, 0.04});
	V(STUDIO_VALUE_ROUGHNESS, 0.5);
	V(STUDIO_VALUE_METALLIC, 1.0);
	for (i32 i = 0; i < 2; ++i) {
		LAYER(i == 0 ? "Coins" : "More Coins", "PATTERN_SCATTER");
		IN("Scale", i == 0 ? 6.0 : 8.0);
		IN("Shape", 2.0);
		IN("Size", 0.8);
		IN("Size Jitter", 0.15);
		IN("Density", 1.0);
		IN("Seed", 2.0 + i * 5.0);
		RAMP(false, 2, (f32[]){0.75, 0.52, 0.18, 0.0, 1.0, 0.8, 0.38, 1.0});
		V(STUDIO_VALUE_CELL_VAR, 0.3);
		MASK(STUDIO_MASK_PATTERN, 0.55, 0.05, 0.0, false);
		V(STUDIO_VALUE_METALLIC, 1.0);
		V(STUDIO_VALUE_ROUGHNESS, 0.25);
		V(STUDIO_VALUE_DEPTH, 0.4);
		CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL | CH_HEIGHT, HB_ADD);
	}
}

static void bm_build_stained_glass() {
	LAYER(NULL, "PATTERN_CELLS");
	IN("Scale", 10.0);
	IN("Edge Width", 0.08);
	IN("Variation", 0.0);
	RAMP(true, 6, (f32[]){0.75, 0.1, 0.1, 0.0, 0.95, 0.6, 0.1, 0.17, 0.2, 0.6, 0.2, 0.33, 0.1, 0.3, 0.8, 0.5, 0.5, 0.15, 0.6, 0.67, 0.95, 0.85, 0.5, 0.83});
	V(STUDIO_VALUE_CELL_VAR, 0.85);
	V(STUDIO_VALUE_CELL_COLORS, 1.0);
	V(STUDIO_VALUE_ROUGHNESS, 0.1);
	V(STUDIO_VALUE_EMISSION, 0.5);
	V(STUDIO_VALUE_DEPTH, 0.1);

	LAYER("Lead", "PATTERN_CELLS");
	IN("Scale", 10.0);
	IN("Edge Width", 0.08);
	IN("Variation", 0.0);
	COLOR((f32[]){0.12, 0.12, 0.13});
	MASK(STUDIO_MASK_PATTERN, 0.15, 0.1, 0.0, true);
	V(STUDIO_VALUE_METALLIC, 0.6);
	V(STUDIO_VALUE_ROUGHNESS, 0.5);
	V(STUDIO_VALUE_DEPTH, 0.15);
	CHANNELS(CH_COLOR | CH_ROUGH | CH_METAL | CH_HEIGHT | CH_EMIS, HB_ADD);
}

static void bm_build_feathers() {
	LAYER(NULL, "PATTERN_FEATHERS");
	IN("Scale", 10.0);
	RAMP(false, 3, (f32[]){0.1, 0.07, 0.05, 0.0, 0.45, 0.32, 0.2, 0.5, 0.85, 0.8, 0.72, 1.0});
	V(STUDIO_VALUE_CELL_VAR, 0.3);
	V(STUDIO_VALUE_ROUGHNESS, 0.7);
	V(STUDIO_VALUE_CAVITY, 0.5);
	V(STUDIO_VALUE_DEPTH, 0.3);
}

static void bm_build_leopard() {
	LAYER(NULL, "PATTERN_FIBERS");
	IN("Scale", 160.0);
	IN("Length", 2.0);
	IN("Clumping", 0.3);
	RAMP(false, 2, (f32[]){0.55, 0.38, 0.18, 0.0, 0.85, 0.68, 0.4, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.85);
	V(STUDIO_VALUE_DEPTH, 0.2);

	LAYER("Rosettes", "PATTERN_SCATTER");
	IN("Scale", 7.0);
	IN("Shape", 4.0);
	IN("Size", 0.7);
	IN("Size Jitter", 0.3);
	IN("Density", 0.9);
	IN("Distort", 0.2);
	COLOR((f32[]){0.12, 0.07, 0.04});
	MASK(STUDIO_MASK_PATTERN, 0.5, 0.15, 0.0, false);
	CHANNELS(CH_COLOR, HB_MIX);
}

static void bm_build_brain() {
	// Parallel ridges winding around: stripes bent far by the distortion
	LAYER(NULL, "PATTERN_STRIPES");
	IN("Scale", 16.0);
	IN("Waviness", 0.5);
	IN("Distort", 3.5);
	IN("Distort Scale", 4.0);
	RAMP(false, 3, (f32[]){0.4, 0.12, 0.12, 0.0, 0.85, 0.45, 0.38, 0.55, 0.97, 0.75, 0.62, 1.0});
	V(STUDIO_VALUE_ROUGHNESS, 0.6);
	V(STUDIO_VALUE_SUBSURFACE, 0.4);
	V(STUDIO_VALUE_CAVITY, 0.6);
	V(STUDIO_VALUE_DEPTH, 0.6);
}

// ██████╗ ██████╗ ███████╗███████╗███████╗████████╗███████╗
// ██╔══██╗██╔══██╗██╔════╝██╔════╝██╔════╝╚══██╔══╝██╔════╝
// ██████╔╝██████╔╝█████╗  ███████╗█████╗     ██║   ███████╗
// ██╔═══╝ ██╔══██╗██╔══╝  ╚════██║██╔══╝     ██║   ╚════██║
// ██║     ██║  ██║███████╗███████║███████╗   ██║   ███████║
// ╚═╝     ╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝   ╚═╝   ╚══════╝

enum { BM_SCALES, BM_SKIN, BM_BONE, BM_FLESH, BM_SCIFI, BM_FUR, BM_OWN };

typedef struct builtin_preset {
	char       *name;
	i32         section;
	i32         family;
	const void *cfg;
	void (*build)(void); // BM_OWN
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
	{"Wood Planks",            3, BM_OWN,    NULL, bm_build_planks},
	{"Tree Bark",              3, BM_OWN,    NULL, bm_build_bark},
	{"Dirt Ground",            3, BM_OWN,    NULL, bm_build_dirt},
	{"Sand",                   3, BM_OWN,    NULL, bm_build_sand},
	{"Grass",                  3, BM_OWN,    NULL, bm_build_grass},
	{"Roof Tiles",             3, BM_OWN,    NULL, bm_build_roof},
	{"Marble",                 3, BM_OWN,    NULL, bm_build_marble},
	{"Cliff Rock",             3, BM_OWN,    NULL, bm_build_cliff},
	{"Lava",                   3, BM_OWN,    NULL, bm_build_lava},
	{"Carpet",                 3, BM_OWN,    NULL, bm_build_carpet},
	{"Wool Fabric",            3, BM_OWN,    NULL, bm_build_wool},
	{"Gold",                   4, BM_OWN,    NULL, bm_build_gold},
	{"Copper",                 4, BM_OWN,    NULL, bm_build_copper},
	{"Herringbone Floor",      5, BM_OWN,    NULL, bm_build_herringbone},
	{"Parquet Floor",          5, BM_OWN,    NULL, bm_build_parquet},
	{"Castle Stone Wall",      5, BM_OWN,    NULL, bm_build_castle},
	{"Seigaiha Tiles",         5, BM_OWN,    NULL, bm_build_seigaiha},
	{"Arc Pavement",           5, BM_OWN,    NULL, bm_build_arcs},
	{"Moroccan Star Tiles",    5, BM_OWN,    NULL, bm_build_moroccan},
	{"Truchet Tiles",          5, BM_OWN,    NULL, bm_build_truchet},
	{"Checkered Tiles",        5, BM_OWN,    NULL, bm_build_checker},
	{"Terrazzo",               5, BM_OWN,    NULL, bm_build_terrazzo},
	{"Mosaic",                 5, BM_OWN,    NULL, bm_build_mosaic},
	{"Chevron Wallpaper",      5, BM_OWN,    NULL, bm_build_chevron},
	{"Damask Wallpaper",       5, BM_OWN,    NULL, bm_build_damask},
	{"Thatch Roof",            5, BM_OWN,    NULL, bm_build_thatch},
	{"OSB Board",              5, BM_OWN,    NULL, bm_build_osb},
	{"Denim",                  6, BM_OWN,    NULL, bm_build_denim},
	{"Wicker",                 6, BM_OWN,    NULL, bm_build_wicker},
	{"Rope",                   6, BM_OWN,    NULL, bm_build_rope},
	{"Stitched Leather",       6, BM_OWN,    NULL, bm_build_stitched},
	{"Chesterfield Leather",   6, BM_OWN,    NULL, bm_build_chesterfield},
	{"Lace",                   6, BM_OWN,    NULL, bm_build_lace},
	{"Gauze",                  6, BM_OWN,    NULL, bm_build_gauze},
	{"Tartan",                 6, BM_OWN,    NULL, bm_build_plaid},
	{"Crumpled Paper",         6, BM_OWN,    NULL, bm_build_paper},
	{"Carbon Fiber",           7, BM_OWN,    NULL, bm_build_carbon},
	{"Diamond Tread Plate",    7, BM_OWN,    NULL, bm_build_tread},
	{"Chain Mail",             7, BM_OWN,    NULL, bm_build_chainmail},
	{"Chain-Link Fence",       7, BM_OWN,    NULL, bm_build_fence},
	{"Nuts and Bolts",         7, BM_OWN,    NULL, bm_build_nuts},
	{"Brushed Copper Disc",    7, BM_OWN,    NULL, bm_build_radial_copper},
	{"Rust Streaked Concrete", 7, BM_OWN,    NULL, bm_build_streaks},
	{"Bullet Holes",           7, BM_OWN,    NULL, bm_build_bullets},
	{"Leaf Litter",            3, BM_OWN,    NULL, bm_build_leaves},
	{"Ivy Wall",               3, BM_OWN,    NULL, bm_build_ivy},
	{"Moon Craters",           3, BM_OWN,    NULL, bm_build_moon},
	{"Amethyst Crystals",      3, BM_OWN,    NULL, bm_build_amethyst},
	{"Basalt Columns",         3, BM_OWN,    NULL, bm_build_basalt},
	{"Malachite",              3, BM_OWN,    NULL, bm_build_malachite},
	{"Cracked Ice",            3, BM_OWN,    NULL, bm_build_ice},
	{"Firewood Ends",          3, BM_OWN,    NULL, bm_build_logs},
	{"Cobweb Bricks",          8, BM_OWN,    NULL, bm_build_cobweb},
	{"Gold Coins",             8, BM_OWN,    NULL, bm_build_coins},
	{"Stained Glass",          8, BM_OWN,    NULL, bm_build_stained_glass},
	{"Feathers",               8, BM_OWN,    NULL, bm_build_feathers},
	{"Leopard Print",          8, BM_OWN,    NULL, bm_build_leopard},
	{"Brain Coral",            8, BM_OWN,    NULL, bm_build_brain},
};
// clang-format on

#define BUILTIN_COUNT ((i32)(sizeof(builtin_presets) / sizeof(builtin_presets[0])))

static char *builtin_section_names[] = {"Creatures", "Stylized Creatures", "Stylized Sci-Fi", "Nature & Surfaces", "Metals", "Floors & Walls", "Fabric & Leather", "Industrial", "Fantasy & Organic"};

// Studio material of the preset, not added to the project
slot_material_t *builtin_make_material(i32 i) {
	builtin_preset_t *p = &builtin_presets[i];
	nodes_material_init();
	slot_material_t *m = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	m->canvas->name    = string_copy(slot_material_unique_name(m->canvas, p->name));
	studio_graph_create(m->canvas);
	bs_c     = m->canvas;
	bs_first = true;
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
	case BM_OWN:
		p->build();
		break;
	default:
		bm_build_scifi(p->cfg);
		break;
	}
	bs_c = NULL;
	return m;
}

// ██╗   ██╗███████╗███████╗
// ██║   ██║██╔════╝██╔════╝
// ██║   ██║███████╗█████╗
// ██║   ██║╚════██║██╔══╝
// ╚██████╔╝███████║███████╗
//  ╚═════╝ ╚══════╝╚══════╝

// The material of the preset, made and picked, or picked when the project has it already
slot_material_t *builtin_material_add(i32 i) {
	char *name = builtin_presets[i].name;
	for (i32 j = 0; j < g_project->_->materials->length; ++j) {
		if (string_equals(g_project->_->materials->buffer[j]->canvas->name, name)) {
			context_select_material(j);
			return g_project->_->materials->buffer[j];
		}
	}
	slot_material_t *m = builtin_make_material(i);
	any_array_push(g_project->_->materials, m);
	context_set_material(m);
	tab_materials_update_material();
	history_new_material();
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	return m;
}

static void builtin_material_use_on_next_frame(void *data) {
	builtin_material_add((i32)(size_t)data);
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
	return (i32)(sizeof(builtin_section_names) / sizeof(builtin_section_names[0]));
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

static gpu_texture_t *builtin_icons[128];
static i32            builtin_icon_state[128]; // 0 none, 1 requested, 2 ready, 3 failed
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
	slot_material_t *m              = builtin_make_material(i);
	g_context->material             = m;
	bool height_used                = make_material_height_used;
	make_material_height_used       = true; // Show the relief in the preview
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
