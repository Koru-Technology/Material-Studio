
#include "global.h"

// Built-in brushes of the Brushes tab: a brush tip generated in code (white, shape in the alpha) and the stroke settings that go with it.
// Picking one sets the tip and the spacing, scatter, jitter, flow and hardness of the tool options. The brush nodes keep working, a mask
// image wired in the brush nodes wins over the tip.

#define BUILTIN_TIP_RES 256

typedef enum {
	TIP_NONE,
	TIP_CHALK,
	TIP_BRISTLE,
	TIP_DRY,
	TIP_SPLATTER,
	TIP_SPECKLE,
	TIP_STAR,
	TIP_STRANDS,
	TIP_SCALE,
	TIP_SQUARE,
	TIP_COUNT,
} builtin_tip_t;

typedef struct builtin_brush {
	char *name;
	i32   tip;
	f32   hardness;
	f32   spacing;
	f32   scatter;
	f32   jitter_size;
	f32   jitter_angle;
	f32   jitter_opacity;
	f32   flow;
} builtin_brush_t;

// clang-format off
static builtin_brush_t builtin_brushes[] = {
	{"Round Hard", TIP_NONE,     1.0, 0.0,  0.0, 0.0, 0.0,  0.0, 1.0},
	{"Round Soft", TIP_NONE,     0.0, 0.0,  0.0, 0.0, 0.0,  0.0, 1.0},
	{"Airbrush",   TIP_NONE,     0.0, 0.08, 0.0, 0.0, 0.0,  0.0, 0.08},
	{"Chalk",      TIP_CHALK,    1.0, 0.15, 0.0, 0.0, 1.0,  0.3, 1.0},
	{"Bristle",    TIP_BRISTLE,  1.0, 0.04, 0.0, 0.0, 0.02, 0.0, 1.0},
	{"Dry Brush",  TIP_DRY,      1.0, 0.05, 0.0, 0.0, 0.0,  0.2, 1.0},
	{"Splatter",   TIP_SPLATTER, 1.0, 1.2,  0.8, 0.7, 1.0,  0.2, 1.0},
	{"Speckle",    TIP_SPECKLE,  1.0, 0.7,  1.5, 0.8, 1.0,  0.5, 1.0},
	{"Star",       TIP_STAR,     1.0, 1.5,  0.0, 0.4, 1.0,  0.0, 1.0},
	{"Fur",        TIP_STRANDS,  1.0, 0.35, 0.3, 0.3, 0.1,  0.2, 1.0},
	{"Scales",     TIP_SCALE,    1.0, 0.8,  0.0, 0.1, 0.0,  0.0, 1.0},
	{"Square",     TIP_SQUARE,   1.0, 0.0,  0.0, 0.0, 0.0,  0.0, 1.0},
};
// clang-format on

#define BUILTIN_BRUSH_COUNT ((i32)(sizeof(builtin_brushes) / sizeof(builtin_brushes[0])))

static gpu_texture_t *builtin_tip_textures[TIP_COUNT];

static f32 builtin_tip_hash(i32 x, i32 y, i32 seed) {
	u32 h = (u32)(x * 374761393 + y * 668265263 + seed * 362437);
	h     = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}

// Smooth value noise, cells of the given size in pixels
static f32 builtin_tip_noise(f32 x, f32 y, f32 cell, i32 seed) {
	f32 fx = x / cell;
	f32 fy = y / cell;
	i32 ix = (i32)floorf(fx);
	i32 iy = (i32)floorf(fy);
	f32 tx = fx - ix;
	f32 ty = fy - iy;
	tx     = tx * tx * (3.0f - 2.0f * tx);
	ty     = ty * ty * (3.0f - 2.0f * ty);
	f32 a  = builtin_tip_hash(ix, iy, seed);
	f32 b  = builtin_tip_hash(ix + 1, iy, seed);
	f32 c  = builtin_tip_hash(ix, iy + 1, seed);
	f32 d  = builtin_tip_hash(ix + 1, iy + 1, seed);
	return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

static f32 builtin_tip_clamp(f32 v) {
	return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

// Soft edged disc, coverage of a pixel at distance d from the center, radius r, in pixels
static f32 builtin_tip_disc(f32 d, f32 r) {
	return builtin_tip_clamp(r - d + 0.5f);
}

static void builtin_tip_stamp_disc(f32 *a, f32 cx, f32 cy, f32 r, f32 alpha) {
	i32 n = BUILTIN_TIP_RES;
	for (i32 y = (i32)(cy - r - 2); y <= (i32)(cy + r + 2); ++y) {
		for (i32 x = (i32)(cx - r - 2); x <= (i32)(cx + r + 2); ++x) {
			if (x < 0 || y < 0 || x >= n || y >= n) {
				continue;
			}
			f32 d   = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
			f32 v   = builtin_tip_disc(d, r) * alpha;
			f32 *pa = &a[y * n + x];
			*pa     = *pa + v * (1.0f - *pa);
		}
	}
}

static void builtin_tip_generate(i32 tip, f32 *a) {
	i32 n = BUILTIN_TIP_RES;
	f32 c = n * 0.5f;
	f32 r = n * 0.46f;
	if (tip == TIP_SPLATTER) {
		builtin_tip_stamp_disc(a, c, c, r * 0.45f, 1.0f);
		for (i32 i = 0; i < 22; ++i) {
			f32 ang = builtin_tip_hash(i, 1, 7) * 6.2831853f;
			f32 dst = r * (0.35f + 0.6f * builtin_tip_hash(i, 2, 7));
			f32 rad = r * (0.03f + 0.14f * builtin_tip_hash(i, 3, 7) * (1.0f - dst / r + 0.3f));
			builtin_tip_stamp_disc(a, c + cosf(ang) * dst, c + sinf(ang) * dst, rad, 1.0f);
		}
		return;
	}
	if (tip == TIP_SPECKLE) {
		for (i32 i = 0; i < 40; ++i) {
			f32 ang = builtin_tip_hash(i, 1, 11) * 6.2831853f;
			f32 dst = r * sqrtf(builtin_tip_hash(i, 2, 11)) * 0.9f;
			f32 rad = r * (0.02f + 0.05f * builtin_tip_hash(i, 3, 11));
			builtin_tip_stamp_disc(a, c + cosf(ang) * dst, c + sinf(ang) * dst, rad, 0.5f + 0.5f * builtin_tip_hash(i, 4, 11));
		}
		return;
	}
	if (tip == TIP_BRISTLE) {
		for (i32 i = 0; i < 60; ++i) {
			f32 ang = builtin_tip_hash(i, 1, 3) * 6.2831853f;
			f32 dst = r * sqrtf(builtin_tip_hash(i, 2, 3)) * 0.88f;
			f32 rad = r * (0.04f + 0.05f * builtin_tip_hash(i, 3, 3));
			builtin_tip_stamp_disc(a, c + cosf(ang) * dst, c + sinf(ang) * dst, rad, 0.4f + 0.6f * builtin_tip_hash(i, 4, 3));
		}
		return;
	}
	if (tip == TIP_STRANDS) {
		// Tapered, slightly curved strands from a common root, like a tuft of fur
		for (i32 i = 0; i < 14; ++i) {
			f32 spread = (builtin_tip_hash(i, 1, 5) - 0.5f) * 1.1f;
			f32 len    = r * (1.2f + 0.6f * builtin_tip_hash(i, 2, 5));
			f32 bend   = (builtin_tip_hash(i, 3, 5) - 0.5f) * 0.6f;
			f32 x0     = c + (builtin_tip_hash(i, 4, 5) - 0.5f) * r * 0.5f;
			f32 y0     = c + r * 0.85f;
			for (i32 s = 0; s <= 40; ++s) {
				f32 t   = s / 40.0f;
				f32 ang = -1.5707963f + spread + bend * t;
				f32 x   = x0 + cosf(ang) * len * t;
				f32 y   = y0 + sinf(ang) * len * t;
				builtin_tip_stamp_disc(a, x, y, r * 0.045f * (1.0f - t * 0.85f), 0.9f);
			}
		}
		return;
	}
	for (i32 y = 0; y < n; ++y) {
		for (i32 x = 0; x < n; ++x) {
			f32 px = x + 0.5f - c;
			f32 py = y + 0.5f - c;
			f32 d  = sqrtf(px * px + py * py);
			f32 v  = 0.0f;
			if (tip == TIP_CHALK) {
				f32 grain = builtin_tip_noise(x, y, 6.0f, 1) * 0.6f + builtin_tip_noise(x, y, 2.5f, 2) * 0.4f;
				f32 edge  = builtin_tip_noise(x, y, 20.0f, 3) * 0.25f;
				v         = builtin_tip_disc(d, r * (0.8f + edge)) * builtin_tip_clamp((grain - 0.35f) * 4.0f);
			}
			else if (tip == TIP_DRY) {
				f32 streak = builtin_tip_noise(0.0f, y, 3.0f, 4);
				f32 broken = builtin_tip_noise(x, y, 14.0f, 5);
				v          = builtin_tip_disc(d, r) * builtin_tip_clamp((streak - 0.4f) * 5.0f) * builtin_tip_clamp((broken - 0.25f) * 3.0f);
			}
			else if (tip == TIP_STAR) {
				f32 ang  = atan2f(py, px) + 1.5707963f;
				f32 k    = fabsf(fmodf(ang * 5.0f / 6.2831853f + 100.0f, 1.0f) - 0.5f) * 2.0f; // 1 at the points
				f32 edge = r * (0.42f + 0.58f * k);
				v        = builtin_tip_disc(d, edge);
			}
			else if (tip == TIP_SCALE) {
				// A rounded scale: disc, its top cut by a larger disc above
				f32 cut = sqrtf(px * px + (py + r * 1.15f) * (py + r * 1.15f));
				v       = builtin_tip_disc(d, r * 0.9f) * builtin_tip_clamp(cut - r * 1.2f);
				v *= 0.65f + 0.35f * builtin_tip_clamp((py + r * 0.2f) / r); // Darker towards the overlap
			}
			else if (tip == TIP_SQUARE) {
				f32 m = fmaxf(fabsf(px), fabsf(py));
				v     = builtin_tip_clamp(r * 0.82f - m + 0.5f);
			}
			a[y * n + x] = v;
		}
	}
}

static gpu_texture_t *builtin_tip_texture(i32 tip) {
	if (tip <= TIP_NONE || tip >= TIP_COUNT) {
		return NULL;
	}
	if (builtin_tip_textures[tip] == NULL) {
		i32  n     = BUILTIN_TIP_RES;
		f32 *alpha = calloc(n * n, sizeof(f32));
		builtin_tip_generate(tip, alpha);
		u8 *pixels = malloc(n * n * 4);
		for (i32 i = 0; i < n * n; ++i) {
			pixels[i * 4 + 0] = 255;
			pixels[i * 4 + 1] = 255;
			pixels[i * 4 + 2] = 255;
			pixels[i * 4 + 3] = (u8)(builtin_tip_clamp(alpha[i]) * 255.0f + 0.5f);
		}
		free(alpha);
		iron_image_pixels_t px    = {.data = pixels, .width = n, .height = n, .format = GPU_TEXTURE_FORMAT_RGBA32};
		builtin_tip_textures[tip] = gpu_create_texture_from_pixels(&px); // Frees the pixels
	}
	return builtin_tip_textures[tip];
}

i32 builtin_brush_count() {
	return BUILTIN_BRUSH_COUNT;
}

char *builtin_brush_name(i32 i) {
	return tr(builtin_brushes[i].name);
}

static gpu_texture_t *builtin_round_icons[32];

// Round brushes show a disc with their hardness
gpu_texture_t *builtin_brush_icon(i32 i) {
	if (builtin_brushes[i].tip != TIP_NONE) {
		return builtin_tip_texture(builtin_brushes[i].tip);
	}
	if (builtin_round_icons[i] == NULL) {
		i32 n      = 64;
		u8 *pixels = malloc(n * n * 4);
		f32 h      = builtin_brushes[i].hardness;
		f32 r      = n * 0.42f;
		for (i32 y = 0; y < n; ++y) {
			for (i32 x = 0; x < n; ++x) {
				f32 d = sqrtf((x + 0.5f - n * 0.5f) * (x + 0.5f - n * 0.5f) + (y + 0.5f - n * 0.5f) * (y + 0.5f - n * 0.5f)) / r;
				f32 t = builtin_tip_clamp((d - h) / fmaxf(1.0f - h, 0.001f));
				f32 v = d > 1.0f ? builtin_tip_clamp((1.0f - d) * r + 1.0f) * (h > 0.99f ? 1.0f : 0.0f) : 1.0f - t * t * (3.0f - 2.0f * t);
				if (builtin_brushes[i].flow < 1.0f) {
					v *= 0.6f;
				}
				u8 *p = pixels + (y * n + x) * 4;
				p[0] = p[1] = p[2] = 255;
				p[3]               = (u8)(builtin_tip_clamp(v) * 255.0f + 0.5f);
			}
		}
		iron_image_pixels_t px     = {.data = pixels, .width = n, .height = n, .format = GPU_TEXTURE_FORMAT_RGBA32};
		builtin_round_icons[i] = gpu_create_texture_from_pixels(&px);
	}
	return builtin_round_icons[i];
}

// Tip of the picked built-in brush, NULL for a round brush
gpu_texture_t *builtin_brush_tip() {
	i32 i = g_context->brush_builtin;
	if (i < 0 || i >= BUILTIN_BRUSH_COUNT) {
		return NULL;
	}
	return builtin_tip_texture(builtin_brushes[i].tip);
}

// Sets the tip and stroke settings of built-in brush i, nothing else
void builtin_brush_settings(i32 i) {
	builtin_brush_t *b              = &builtin_brushes[i];
	g_context->brush_builtin        = i;
	g_context->brush_hardness       = b->hardness;
	g_context->brush_spacing        = b->spacing;
	g_context->brush_scatter        = b->scatter;
	g_context->brush_jitter_size    = b->jitter_size;
	g_context->brush_jitter_angle   = b->jitter_angle;
	g_context->brush_jitter_opacity = b->jitter_opacity;
	g_context->brush_flow           = b->flow;
}

void builtin_brush_use(i32 i) {
	if (i < 0 || i >= BUILTIN_BRUSH_COUNT) {
		return;
	}
	builtin_brush_t *b              = &builtin_brushes[i];
	g_context->brush_builtin        = i;
	ui_quick_menu_brush_used(i);
	g_context->brush_hardness       = b->hardness;
	g_context->brush_spacing        = b->spacing;
	g_context->brush_scatter        = b->scatter;
	g_context->brush_jitter_size    = b->jitter_size;
	g_context->brush_jitter_angle   = b->jitter_angle;
	g_context->brush_jitter_opacity = b->jitter_opacity;
	g_context->brush_flow           = b->flow;
	if (g_context->tool != TOOL_TYPE_BRUSH && g_context->tool != TOOL_TYPE_ERASER) {
		context_select_tool(TOOL_TYPE_BRUSH);
	}
	brush_output_node_parse_inputs(); // Picks up the tip, recompiles the paint shader when it changed
	make_material_parse_paint_material(true);
	g_context->brush->preview_ready = false; // The parameters panel renders the stroke again
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	ui_header_handle->redraws = 2;
	config_save();
}
