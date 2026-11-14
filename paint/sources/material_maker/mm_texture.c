// CPU textures: the outputs of Material Maker's image and text nodes (gen_image.gd, gen_text.gd), made on the CPU rather
// than by a shader. Material Maker loads the image file into an RGBA half float texture (bytes / 255, no colour space
// conversion), and renders the text with a Godot Label (renderer.gd render_text) into a 2048x2048 2D viewport: sRGB
// values blended as is, read back as RGBA8. Both are sampled with linear filtering, repeat, no mipmaps.

#include "mm_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stb_image.h>
#include <stb_truetype.h>

#define TEXT_SIZE 2048 // renderer.gd: size = Vector2(2048, 2048)
#define MAX_FONTS 4

static struct {
	const uint8_t *data;
	size_t         size;
} fonts[MAX_FONTS];
static int    nfonts;
static mm_read_file_t read_file;

void mm_add_font(const uint8_t *ttf, size_t size) {
	if (nfonts < MAX_FONTS) {
		fonts[nfonts].data = ttf;
		fonts[nfonts].size = size;
		nfonts++;
	}
}

void mm_set_file_reader(mm_read_file_t read) {
	read_file = read;
}

// What the pixels of an image or text node depend on, cheap to compare (caches): its kind and parameters. An image whose
// file changes on disk under the same path keeps its key.
const char *mm_texture_key(mm_gen_t *g) {
	return mm_printf("%s:%d:%s", g->kind == MM_G_IMAGE ? "image" : "text", nfonts, mm_json_text(g->params));
}

// Files as saved: %MATERIAL_MAKER_PATH%/... are Material Maker's own resources, read by their file name (shipped in
// assets/material_maker: examples/mm_icon.png is mm_icon.png); other paths go to the reader as they are
static uint8_t *read_resource(const char *path, size_t *size) {
	if (read_file == NULL || path == NULL || path[0] == 0) {
		return NULL;
	}
	if (mm_starts_with(path, "%MATERIAL_MAKER_PATH%/")) {
		const char *name = strrchr(path, '/');
		path             = name + 1;
	}
	return read_file(path, size);
}

// A float parameter: missing, its default; an expression (calculate_float_param) that does not evaluate, invalid
static double float_param(mm_gen_t *g, const char *name, double missing, double invalid) {
	mm_json_t *v = mm_json_get(g->params, name);
	if (v == NULL) {
		return missing;
	}
	if (v->type == MM_JSON_NUMBER) {
		return v->number;
	}
	if (v->type == MM_JSON_STRING) { // Only plain numbers evaluate here
		char  *end;
		double d = strtod(v->string, &end);
		return end != v->string && *mm_strip(end) == 0 ? d : invalid;
	}
	return invalid;
}

static void color_param(mm_gen_t *g, const char *name, const double fallback[4], double c[4]) {
	mm_json_t *v = mm_json_get(g->params, name);
	for (int i = 0; i < 4; ++i) {
		static const char *keys[4] = {"r", "g", "b", "a"};
		c[i]                       = v != NULL && v->type == MM_JSON_OBJECT ? mm_json_num(mm_json_get(v, keys[i]), fallback[i]) : fallback[i];
	}
}

// Image

static uint8_t *image_load(mm_gen_t *g, int *w, int *h) {
	size_t   size;
	uint8_t *file = read_resource(mm_json_str(mm_json_get(g->params, "image"), ""), &size);
	uint8_t *rgba = NULL;
	if (file != NULL) {
		int      c;
		uint8_t *pixels = stbi_load_from_memory(file, (int)size, w, h, &c, 4);
		if (pixels != NULL) {
			rgba = malloc((size_t)*w * *h * 4);
			memcpy(rgba, pixels, (size_t)*w * *h * 4);
			stbi_image_free(pixels);
		}
		free(file);
	}
	return rgba;
}

bool mm_texture_size(mm_gen_t *g, int *w, int *h) {
	*w = *h = 1;
	if (g->kind == MM_G_TEXT) {
		*w = *h = TEXT_SIZE;
		return true;
	}
	size_t   size;
	uint8_t *file = read_resource(mm_json_str(mm_json_get(g->params, "image"), ""), &size);
	int      c;
	bool     ok = file != NULL && stbi_info_from_memory(file, (int)size, w, h, &c);
	free(file);
	if (!ok) {
		*w = *h = 1;
	}
	return ok;
}

// gen_image.gd get_adjusted_uv
char *mm_image_adjusted_uv(mm_gen_t *g, const char *uv) {
	bool fix_ar = mm_json_bool(mm_json_get(g->params, "fix_ar"), false);
	bool clamp  = mm_json_bool(mm_json_get(g->params, "clamp"), false);
	bool filter = mm_json_bool(mm_json_get(g->params, "filter"), true);
	int  w = 0, h = 0;
	if (fix_ar || !filter) {
		mm_texture_size(g, &w, &h);
	}
	char *rv = mm_strdup(uv);
	if (fix_ar) {
		double ar = w != 0 && h != 0 ? (double)h / w : 1.0;
		rv        = mm_printf("((%s) - vec2(0.0, %f)) * vec2(1.0, %f)", rv, (1 - ar) / 2, 1 / ar);
	}
	rv = clamp ? mm_printf("clamp(%s, 0.0, 1.0)", rv) : mm_printf("mod(%s, 1.0)", rv);
	if (!filter) {
		char *ts = mm_printf("vec2(%d, %d)", w, h);
		rv       = mm_printf("(floor(fract(%s) * %s)+vec2(0.5))/%s", rv, ts, ts);
	}
	return rv;
}

// Text

typedef struct {
	stbtt_fontinfo info;
	float          scale;
	int            ascent, descent; // Pixels, FreeType's grid fitted metrics (ceil)
	uint8_t       *owned;           // A font file loaded for the node
} font_t;

typedef struct {
	int font;
	int glyph;
	int x; // Pen position from the line's start
} glyph_t;

static bool font_init(font_t *f, const uint8_t *data, int size) {
	memset(f, 0, sizeof(*f));
	if (data == NULL || !stbtt_InitFont(&f->info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
		return false;
	}
	int a, d, gap;
	stbtt_GetFontVMetrics(&f->info, &a, &d, &gap);
	f->scale   = stbtt_ScaleForMappingEmToPixels(&f->info, (float)size); // Godot's font size is the em size in pixels
	f->ascent  = (int)ceil(a * f->scale);
	f->descent = (int)ceil(-d * f->scale);
	return true;
}

static int utf8_next(const char **s) {
	const uint8_t *p = (const uint8_t *)*s;
	int            c = *p++, n = 0;
	if (c >= 0xF0) {
		c &= 0x07, n = 3;
	}
	else if (c >= 0xE0) {
		c &= 0x0F, n = 2;
	}
	else if (c >= 0xC0) {
		c &= 0x1F, n = 1;
	}
	for (; n > 0 && (*p & 0xC0) == 0x80; --n) {
		c = (c << 6) | (*p++ & 0x3F);
	}
	*s = (const char *)p;
	return c;
}

// Glyphs of one line with the fonts that have them (the first font, then the fallbacks, as Godot's font fallback),
// advances rounded to pixels as with Godot's default (no subpixel positioning at these sizes). Returns the line width.
static int layout_line(font_t *fs, int nf, const char *s, const char *end, glyph_t *out, int *count, int *ascent, int *descent) {
	int x = 0, n = 0, prev_font = -1, prev = 0;
	while (s < end) {
		int cp = utf8_next(&s);
		int fi = 0, gi = 0;
		for (int k = 0; k < nf && gi == 0; ++k) {
			gi = stbtt_FindGlyphIndex(&fs[k].info, cp);
			fi = gi != 0 ? k : 0;
		}
		if (gi == 0) { // Missing everywhere: the main font's .notdef
			fi = 0;
		}
		if (prev_font == fi && n > 0) {
			x += (int)lroundf(stbtt_GetGlyphKernAdvance(&fs[fi].info, prev, gi) * fs[fi].scale);
		}
		int adv, lsb;
		stbtt_GetGlyphHMetrics(&fs[fi].info, gi, &adv, &lsb);
		out[n].font  = fi;
		out[n].glyph = gi;
		out[n].x     = x;
		n++;
		x += (int)lroundf(adv * fs[fi].scale);
		*ascent  = fs[fi].ascent > *ascent ? fs[fi].ascent : *ascent;
		*descent = fs[fi].descent > *descent ? fs[fi].descent : *descent;
		prev_font = fi;
		prev      = gi;
	}
	*count = n;
	return x;
}

static uint8_t *text_render(mm_gen_t *g) {
	const char *text      = mm_replace(mm_json_str(mm_json_get(g->params, "text"), "Text"), "\\n", "\n");
	int         font_size    = (int)float_param(g, "font_size", 32, 64);
	double      line_spacing = float_param(g, "line_spacing", 0, 0);
	int         alignment    = (int)mm_json_num(mm_json_get(g->params, "alignment"), 0);
	bool        center       = mm_json_bool(mm_json_get(g->params, "center"), false);
	double      x            = float_param(g, "x", 0.1, 0);
	double      y            = float_param(g, "y", 0.1, 0);
	static const double white[4] = {1, 1, 1, 1}, black[4] = {0, 0, 0, 1};
	double fg[4], bg[4];
	color_param(g, "fg", white, fg);
	color_param(g, "bg", black, bg);

	// Fonts: the node's font file when it loads, else the default; then the fallbacks
	font_t fs[MAX_FONTS + 1];
	int    nf = 0;
	size_t size;
	uint8_t *own = read_resource(mm_json_str(mm_json_get(g->params, "font"), ""), &size);
	if (own != NULL && font_init(&fs[0], own, font_size)) {
		fs[0].owned = own;
		nf          = 1;
	}
	else {
		free(own);
	}
	for (int i = 0; i < nfonts; ++i) {
		if (font_init(&fs[nf], fonts[i].data, font_size)) {
			nf++;
		}
	}

	float *cover = calloc((size_t)TEXT_SIZE * TEXT_SIZE, sizeof(float));
	if (nf > 0 && font_size > 0) {
		// Lines, as Label: each line's height is its ascent + descent, then line_spacing (at least -the text's height)
		int         nlines = 1;
		const char *s;
		for (s = text; *s; ++s) {
			nlines += *s == '\n';
		}
		size_t   len    = strlen(text);
		glyph_t *glyphs = malloc(sizeof(glyph_t) * (len + 1));
		struct {
			const char *start, *end;
			int         width, ascent, descent;
		} *lines = calloc(nlines, sizeof(*lines));
		int width = 0, height = 0, count;
		s         = text;
		for (int l = 0; l < nlines; ++l) {
			const char *e = strchr(s, '\n');
			e             = e != NULL ? e : s + strlen(s);
			lines[l].start = s;
			lines[l].end   = e;
			lines[l].ascent  = fs[0].ascent;
			lines[l].descent = fs[0].descent;
			lines[l].width   = layout_line(fs, nf, s, e, glyphs, &count, &lines[l].ascent, &lines[l].descent);
			width            = lines[l].width > width ? lines[l].width : width;
			height += lines[l].ascent + lines[l].descent;
			s = *e ? e + 1 : e;
		}
		double min_spacing = -(fs[0].ascent + fs[0].descent); // -font.get_string_size(text).y
		line_spacing       = line_spacing > min_spacing ? line_spacing : min_spacing;
		height += (int)(line_spacing * (nlines - 1));

		double px = TEXT_SIZE * (0.5 + x), py = TEXT_SIZE * (0.5 + y);
		if (center) {
			px -= width * 0.5;
			py -= height * 0.5;
		}
		double ly = py;
		for (int l = 0; l < nlines; ++l) {
			int    n  = 0;
			int    a = fs[0].ascent, d = fs[0].descent;
			layout_line(fs, nf, lines[l].start, lines[l].end, glyphs, &n, &a, &d);
			double lx = px;
			if (alignment == 1) {
				lx += floor((width - lines[l].width) / 2.0);
			}
			else if (alignment == 2) {
				lx += width - lines[l].width;
			}
			int baseline = (int)floor(ly + lines[l].ascent);
			for (int i = 0; i < n; ++i) {
				font_t  *f = &fs[glyphs[i].font];
				int      bw, bh, bx, by;
				uint8_t *bm = stbtt_GetGlyphBitmap(&f->info, f->scale, f->scale, glyphs[i].glyph, &bw, &bh, &bx, &by);
				if (bm == NULL) {
					continue;
				}
				int ox = (int)floor(lx + glyphs[i].x) + bx, oy = baseline + by;
				for (int j = 0; j < bh; ++j) {
					int ty = oy + j;
					if (ty < 0 || ty >= TEXT_SIZE) {
						continue;
					}
					for (int k = 0; k < bw; ++k) {
						int tx = ox + k;
						if (tx < 0 || tx >= TEXT_SIZE) {
							continue;
						}
						float  c = bm[j * bw + k] / 255.0f;
						float *t = &cover[(size_t)ty * TEXT_SIZE + tx];
						*t       = c + *t * (1.0f - c); // Glyphs blend over each other
					}
				}
				stbtt_FreeBitmap(bm, NULL);
			}
			ly += lines[l].ascent + lines[l].descent + line_spacing;
		}
		free(lines);
		free(glyphs);
	}
	for (int i = 0; i < nf; ++i) {
		free(fs[i].owned);
	}

	// The background over a transparent viewport, then the text modulated by fg (Godot's 2D mix blending)
	uint8_t *rgba = malloc((size_t)TEXT_SIZE * TEXT_SIZE * 4);
	for (size_t i = 0; i < (size_t)TEXT_SIZE * TEXT_SIZE; ++i) {
		double t = cover[i] * fg[3];
		double a = t + bg[3] * (1.0 - t);
		for (int k = 0; k < 3; ++k) {
			double v        = fg[k] * t + bg[k] * bg[3] * (1.0 - t);
			v               = v < 0 ? 0 : v > 1 ? 1 : v;
			rgba[i * 4 + k] = (uint8_t)lround(v * 255.0);
		}
		a               = a < 0 ? 0 : a > 1 ? 1 : a;
		rgba[i * 4 + 3] = (uint8_t)lround(a * 255.0);
	}
	free(cover);
	return rgba;
}

bool mm_texture_pixels(mm_gen_t *g, int *w, int *h, uint8_t **rgba) {
	*rgba = NULL;
	if (g->kind == MM_G_TEXT) {
		mm_mark_t mark = mm_arena_mark();
		*w = *h = TEXT_SIZE;
		*rgba   = text_render(g);
		mm_arena_release(mark);
		return true;
	}
	if (g->kind == MM_G_IMAGE) {
		*rgba = image_load(g, w, h);
		if (*rgba != NULL) {
			return true;
		}
	}
	// No image: Material Maker's empty texture is 1x1 R16F, read as (0, 0, 0, 1)
	*w = *h = 1;
	*rgba   = malloc(4);
	(*rgba)[0] = (*rgba)[1] = (*rgba)[2] = 0;
	(*rgba)[3] = 255;
	return false;
}
