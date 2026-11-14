// Polygon, polyline, splines, pixels and lattice parameters (mm_internal.h): ports of Material Maker's
// addons/material_maker/types/{polygon,splines,pixels,lattice}.gd and of how gen_shader.gd process_parameters uses them.
//
// The values are deserialized as the .gd deserialize does. GDScript stops a function at a runtime error (a missing
// member, a member of something that is not a dictionary), keeping what was done before: the readers below stop at the
// same places. Godot stores Vector2, Color and PackedFloat32Array components as 32 bit floats, so values are rounded to
// float before becoming uniforms, Material Maker writes them with %.9f.

#include "mm_internal.h"
#include <stdint.h>
#include <string.h>

static double mm_f32(double v) {
	return (double)(float)v;
}

// GDScript int(): truncation of the double to a 64 bit integer, PackedInt32Array keeps the low 32 bits
static int32_t mm_i32(double v) {
	int64_t i;
	if (v != v) {
		i = 0;
	}
	else if (v >= 9223372036854775807.0) {
		i = INT64_MAX;
	}
	else if (v <= -9223372036854775808.0) {
		i = INT64_MIN;
	}
	else {
		i = (int64_t)v;
	}
	return (int32_t)(uint32_t)(uint64_t)i;
}

static int mm_int(double v) {
	return (int)mm_i32(v);
}

// v.<key> on a dictionary, NULL (the GDScript runtime error) when v is not a dictionary or has no such member
static mm_json_t *mm_member(mm_json_t *v, const char *key) {
	if (v == NULL || v->type != MM_JSON_OBJECT) {
		return NULL;
	}
	return mm_json_get(v, key);
}

// A number member, false when missing
static bool mm_is_num(mm_json_t *v) {
	return v != NULL && v->type == MM_JSON_NUMBER;
}

static bool mm_member_num(mm_json_t *v, const char *key, double *out) {
	mm_json_t *m = mm_member(v, key);
	if (!mm_is_num(m)) {
		return false;
	}
	*out = m->number;
	return true;
}

// typeof(v) == TYPE_DICTIONARY and v.has("type") and v.type == <type>
static bool mm_is_type(mm_json_t *v, const char *type) {
	mm_json_t *t = mm_member(v, "type");
	return t != NULL && t->type == MM_JSON_STRING && t->string != NULL && strcmp(t->string, type) == 0;
}

// Reads { x, y } points into a float array (2 per point), stops at the first that is not one (GDScript error)
static int mm_read_points(mm_json_t *points, double **out) {
	int n = points != NULL && points->type == MM_JSON_ARRAY ? points->count : 0;
	*out  = mm_alloc(sizeof(double) * 2 * (n > 0 ? n : 1));
	int count = 0;
	for (int i = 0; i < n; ++i) {
		double x, y;
		mm_json_t *p = mm_json_at(points, i);
		if (!mm_member_num(p, "x", &x) || !mm_member_num(p, "y", &y)) {
			break;
		}
		(*out)[count * 2]     = mm_f32(x);
		(*out)[count * 2 + 1] = mm_f32(y);
		++count;
	}
	return count;
}

// An array uniform. Material Maker declares a 0 size array as a plain variable initialized with an empty array
// constructor, which does not compile: a single zero element is declared instead.
static void mm_add_array(mm_code_t *c, const char *name, mm_utype_t type, int components, int size, const double *values) {
	if (size <= 0) {
		static const double zeros[4] = {0, 0, 0, 0};
		mm_code_add_uniform(c, name, type, 1, zeros, components);
		return;
	}
	mm_code_add_uniform(c, name, type, size, values, size * components);
}

// Polygon / polyline (polygon.gd). Both are MMPolygon, closed only changes how the editor inserts points.

char *mm_type_polygon(mm_code_t *c, mm_json_t *value, const char *name, bool declare, bool closed) {
	(void)closed;
	// deserialize: clear(), then the points; anything else prints "Cannot deserialize polygon" and leaves it empty
	double *points = NULL;
	int     count  = 0;
	if (mm_is_type(value, "Polygon")) {
		count = mm_read_points(mm_member(value, "points"), &points);
	}
	if (declare) {
		mm_add_array(c, mm_printf("p_%s_pos", name), MM_U_VEC2, 2, count, points);
	}
	return mm_printf("p_%s_pos", name);
}

// Splines (splines.gd)

char *mm_type_splines(mm_code_t *c, mm_json_t *value, const char *name, bool declare) {
	// deserialize: splines of 4 points, 4 floats each (position, width, offset), from whole groups of 16 values. On
	// failure splines stays empty (the default).
	int     nsplines = 0;
	double *values   = NULL;
	if (mm_is_type(value, "Splines")) {
		mm_json_t *v = mm_member(value, "values");
		int        n = v != NULL && v->type == MM_JSON_ARRAY ? v->count : 0;
		values       = mm_alloc(sizeof(double) * (n > 0 ? n : 1));
		for (int i = 0; i + 16 <= n; i += 16) {
			// The Bezier is appended with default points (0, 0), width 0.01, offset 0, then each point is set:
			// position, width, offset. A value that is not a number stops everything there.
			++nsplines;
			for (int pi = 0; pi < 4; ++pi) {
				double *d = values + i + pi * 4;
				d[0] = 0.0;
				d[1] = 0.0;
				d[2] = mm_f32(0.01);
				d[3] = 0.0;
			}
			for (int pi = 0; pi < 4; ++pi) {
				double    *d  = values + i + pi * 4;
				mm_json_t *ex = mm_json_at(v, i + pi * 4);
				mm_json_t *ey = mm_json_at(v, i + pi * 4 + 1);
				mm_json_t *ew = mm_json_at(v, i + pi * 4 + 2);
				mm_json_t *eo = mm_json_at(v, i + pi * 4 + 3);
				if (!mm_is_num(ex) || !mm_is_num(ey)) {
					goto splines_done;
				}
				d[0] = mm_f32(ex->number);
				d[1] = mm_f32(ey->number);
				if (!mm_is_num(ew)) {
					goto splines_done;
				}
				d[2] = mm_f32(ew->number);
				if (!mm_is_num(eo)) {
					goto splines_done;
				}
				d[3] = mm_f32(eo->number);
			}
		}
	}
splines_done:;
	// get_packed_array: 4 zeros when there are no splines
	int     nvalues = nsplines * 16;
	double  empty[4] = {0, 0, 0, 0};
	double *packed   = nsplines > 0 ? values : empty;
	if (nvalues == 0) {
		nvalues = 4;
	}
	if (declare) {
		mm_code_add_uniform(c, mm_printf("p_%s_points", name), MM_U_VEC4, nvalues / 4, packed, nvalues);
	}
	return mm_printf("p_%s_points", name);
}

// Pixels (pixels.gd)

char *mm_type_pixels(mm_code_t *c, mm_json_t *value, const char *name, bool declare, char **size_expr) {
	// _init: 8x8, 1 bpp, palette black and white, 2 words of pixels
	int     w = 8, h = 8, bpp = 1;
	int     npalette = 2;
	double *palette  = mm_alloc(sizeof(double) * 8);
	palette[3] = 1.0;
	palette[4] = palette[5] = palette[6] = palette[7] = 1.0;
	int      npixels = 2;
	int32_t *pixels  = mm_alloc(sizeof(int32_t) * 2);

	if (mm_is_type(value, "Pixels")) {
		double d;
		do {
			if (!mm_member_num(value, "w", &d)) {
				break;
			}
			w = mm_int(d);
			if (!mm_member_num(value, "h", &d)) {
				break;
			}
			h = mm_int(d);
			if (!mm_member_num(value, "bpp", &d)) {
				break;
			}
			bpp = mm_int(d);
			mm_json_t *pl = mm_member(value, "pl");
			npalette      = 0;
			if (pl == NULL || pl->type != MM_JSON_ARRAY) {
				break;
			}
			palette = mm_alloc(sizeof(double) * 4 * (pl->count > 0 ? pl->count : 1));
			bool ok = true;
			for (int i = 0; i < pl->count; ++i) {
				mm_json_t *col = mm_json_at(pl, i);
				double     r, g, b, a;
				if (!mm_member_num(col, "r", &r) || !mm_member_num(col, "g", &g) || !mm_member_num(col, "b", &b) ||
				    !mm_member_num(col, "a", &a)) {
					ok = false;
					break;
				}
				palette[npalette * 4]     = mm_f32(r);
				palette[npalette * 4 + 1] = mm_f32(g);
				palette[npalette * 4 + 2] = mm_f32(b);
				palette[npalette * 4 + 3] = mm_f32(a);
				++npalette;
			}
			if (!ok) {
				break;
			}
			mm_json_t *px = mm_member(value, "px");
			npixels       = 0;
			if (px == NULL || px->type != MM_JSON_ARRAY) {
				break;
			}
			pixels = mm_alloc(sizeof(int32_t) * (px->count > 0 ? px->count : 1));
			for (int i = 0; i < px->count; ++i) {
				mm_json_t *p = mm_json_at(px, i);
				if (!mm_is_num(p)) {
					break;
				}
				pixels[npixels++] = mm_i32(p->number);
			}
		} while (false);
	}

	if (declare) {
		mm_add_array(c, mm_printf("p_%s_palette", name), MM_U_VEC4, 4, npalette, palette);
		double *ipixels = mm_alloc(sizeof(double) * (npixels > 0 ? npixels : 1));
		for (int i = 0; i < npixels; ++i) {
			ipixels[i] = (double)pixels[i];
		}
		mm_add_array(c, mm_printf("p_%s_pixels", name), MM_U_INT, 1, npixels, ipixels);

		// get_shader (Material Maker fails on a bpp < 1, 1 is used then)
		int b   = bpp >= 1 ? bpp : 1;
		int ppw = 32 / b;
		if (ppw < 1) {
			ppw = 1;
		}
		long long mask = (1LL << (b < 63 ? b : 63)) - 1;
		mm_buf_t *d    = &c->defs;
		mm_buf_printf(d, "const ivec2 %s_size = ivec2(%d, %d);\n", name, w, h);
		mm_buf_printf(d, "vec4 %s_pixels_fct(ivec2 v) {\n", name);
		mm_buf_printf(d, "\tif (v.x < 0 || v.x >= %d || v.y < 0 || v.y >= %d) {\n", w, h);
		mm_buf_printf(d, "\t\treturn p_%s_palette[0];\n", name);
		mm_buf_add(d, "\t} else {\n");
		mm_buf_printf(d, "\t\tint i = v.x+%d*v.y;\n", w);
		mm_buf_printf(d, "\t\tint c = %lld & (p_%s_pixels[i/%d] >> (%d*(i%%%d)));\n", mask, name, ppw, b, ppw);
		mm_buf_printf(d, "\t\treturn p_%s_palette[c];\n", name);
		mm_buf_add(d, "\t}\n");
		mm_buf_add(d, "}\n");
	}
	if (size_expr != NULL) {
		*size_expr = mm_printf("%s_size", name);
	}
	return mm_printf("%s_pixels_fct", name);
}

// Lattice (lattice.gd)

char *mm_type_lattice(mm_code_t *c, mm_json_t *value, const char *name, bool declare, char **size_expr) {
	// Default: 1x1, the unit square corners
	int     sx = 1, sy = 1;
	double *points = mm_alloc(sizeof(double) * 8);
	int     count  = 4;
	points[2] = 1.0;
	points[5] = 1.0;
	points[6] = 1.0;
	points[7] = 1.0;
	if (mm_is_type(value, "Lattice")) {
		mm_json_t *size = mm_member(value, "size");
		double     x, y;
		if (mm_member_num(size, "x", &x) && mm_member_num(size, "y", &y)) {
			sx    = mm_int(x);
			sy    = mm_int(y);
			count = mm_read_points(mm_member(value, "points"), &points);
		}
	}
	if (declare) {
		mm_add_array(c, mm_printf("p_%s_pos", name), MM_U_VEC2, 2, count, points);
	}
	if (size_expr != NULL) {
		*size_expr = mm_printf("ivec2(%d, %d)", sx, sy);
	}
	return mm_printf("p_%s_pos", name);
}
