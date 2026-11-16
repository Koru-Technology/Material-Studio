#include "engine.h"
#include "iron_alloc.h"
#include "iron_array.h"
#include "iron_string.h"
#include <stdbool.h>
#include <string.h>

// Compass and gizmo meshes are generated: x / y / z colored arrows, gizmo adds scale cubes and rotate rings.
// The overlay shader reads only pos and col, so faces share vertices.

#define OVERLAY_ARROW_SEGS 32
#define OVERLAY_RING_SEGS  8

static i16_array_t *_overlay_pos;
static i16_array_t *_overlay_col;
static u32_array_t *_overlay_ind;
static uint32_t     _overlay_v;
static uint32_t     _overlay_i;
static float        _overlay_inv;

// sin(k * pi / 32), every generated mesh steps by a multiple of it
static float startup_sin(int k) {
	static const float q[17] = {0.00000000f, 0.09801714f, 0.19509032f, 0.29028468f, 0.38268343f, 0.47139674f, 0.55557023f, 0.63439328f, 0.70710678f,
	                            0.77301045f, 0.83146961f, 0.88192126f, 0.92387953f, 0.95694034f, 0.98078528f, 0.99518473f, 1.00000000f};
	k &= 63;
	return k <= 16 ? q[k] : k <= 32 ? q[32 - k] : k <= 48 ? -q[k - 32] : -q[64 - k];
}

static float startup_cos(int k) {
	return startup_sin(k + 16);
}

static int16_t startup_quantize(float f) {
	f *= 32767.0f;
	return (int16_t)(f + (f < 0.0f ? -0.5f : 0.5f));
}

// Buffers come zeroed from calloc, only xyz and the color channel are written
static uint32_t overlay_vert(float x, float y, float z) {
	int16_t *pos = _overlay_pos->buffer + _overlay_v * 4;
	pos[0]       = startup_quantize(x * _overlay_inv);
	pos[1]       = startup_quantize(y * _overlay_inv);
	pos[2]       = startup_quantize(z * _overlay_inv);
	return _overlay_v++;
}

static void overlay_color(uint32_t from, int color) {
	int16_t *col = _overlay_col->buffer + color;
	for (uint32_t v = from; v < _overlay_v; ++v) {
		col[v * 4] = 32767;
	}
}

static void overlay_tri(uint32_t a, uint32_t b, uint32_t c) {
	uint32_t *p = _overlay_ind->buffer + _overlay_i;
	p[0]        = a;
	p[1]        = b;
	p[2]        = c;
	_overlay_i += 3;
}

// Counter-clockwise when viewed from the front
static void overlay_quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
	overlay_tri(a, b, c);
	overlay_tri(a, c, d);
}

// Builds the x arrow, y and z arrows are rotated copies of it
static void overlay_arrows(float shaft_r, float shaft_len, float cone_r, float cone_start, float cone_end) {
	uint32_t from  = _overlay_v;
	uint32_t ifrom = _overlay_i;
	int      n     = OVERLAY_ARROW_SEGS;

	// Rings go from +y towards -z
	uint32_t s0 = _overlay_v;
	for (int k = 0; k < n; ++k) {
		overlay_vert(0, shaft_r * startup_cos(k * 64 / n), -shaft_r * startup_sin(k * 64 / n));
	}
	uint32_t s1 = _overlay_v;
	for (int k = 0; k < n; ++k) {
		overlay_vert(shaft_len, shaft_r * startup_cos(k * 64 / n), -shaft_r * startup_sin(k * 64 / n));
	}
	uint32_t c0 = _overlay_v;
	for (int k = 0; k < n; ++k) {
		overlay_vert(cone_start, cone_r * startup_cos(k * 64 / n), -cone_r * startup_sin(k * 64 / n));
	}
	uint32_t tip = overlay_vert(cone_end, 0, 0);

	for (int k = 0; k < n; ++k) {
		int k1 = (k + 1) % n;
		overlay_quad(s0 + k, s1 + k, s1 + k1, s0 + k1);
	}
	// Shaft end faces +x, shaft start and cone base face -x
	for (int k = 1; k < n - 1; ++k) {
		overlay_tri(s1, s1 + k + 1, s1 + k);
	}
	for (int k = 1; k < n - 1; ++k) {
		overlay_tri(s0, s0 + k, s0 + k + 1);
	}
	for (int k = 0; k < n; ++k) {
		overlay_tri(c0 + k, tip, c0 + (k + 1) % n);
	}
	for (int k = 1; k < n - 1; ++k) {
		overlay_tri(c0, c0 + k, c0 + k + 1);
	}
	overlay_color(from, 0);

	uint32_t verts = _overlay_v - from;
	uint32_t inds  = _overlay_i - ifrom;
	int16_t *src   = _overlay_pos->buffer + from * 4;
	int16_t *dst_y = src + verts * 4;
	int16_t *dst_z = dst_y + verts * 4;
	for (uint32_t v = 0; v < verts * 4; v += 4) {
		// (x, y, z) -> (-y, x, z)
		dst_y[v]     = -src[v + 1];
		dst_y[v + 1] = src[v];
		dst_y[v + 2] = src[v + 2];
	}
	for (uint32_t v = 0; v < verts * 4; v += 4) {
		// (x, y, z) -> (-z, y, x)
		dst_z[v]     = -src[v + 2];
		dst_z[v + 1] = src[v + 1];
		dst_z[v + 2] = src[v];
	}
	for (int axis = 1; axis < 3; ++axis) {
		uint32_t offset = _overlay_v - from;
		for (uint32_t i = 0; i < inds; ++i) {
			_overlay_ind->buffer[_overlay_i + i] = _overlay_ind->buffer[ifrom + i] + offset;
		}
		_overlay_v += verts;
		_overlay_i += inds;
		overlay_color(_overlay_v - verts, axis);
	}
}

static void overlay_cube(int axis, float dist, float half) {
	uint32_t from = _overlay_v;
	float    c[3] = {0, 0, 0};
	c[axis]       = dist;
	// Corner i has +half on axis a when bit a is set
	for (int i = 0; i < 8; ++i) {
		overlay_vert(c[0] + (i & 1 ? half : -half), c[1] + (i & 2 ? half : -half), c[2] + (i & 4 ? half : -half));
	}
	for (int a = 0; a < 3; ++a) {
		int u = (a + 1) % 3;
		int v = (a + 2) % 3;
		for (int side = 0; side < 2; ++side) {
			// Corners counter-clockwise around +a, reversed on the -a side
			uint32_t q[4];
			for (int j = 0; j < 4; ++j) {
				int bu = (j == 1 || j == 2) != (side == 0);
				int bv = j >= 2;
				q[j]   = from + (side << a) + (bu << u) + (bv << v);
			}
			overlay_quad(q[0], q[1], q[2], q[3]);
		}
	}
	overlay_color(from, axis);
}

// Quarter ring sweeping from axis s to axis t, with a diamond cross-section; s, t, n form a right-handed basis
static void overlay_ring(int color, float *s, float *t, float *n, float radius, float thickness) {
	uint32_t from = _overlay_v;
	for (int k = 0; k <= OVERLAY_RING_SEGS; ++k) {
		float cs = startup_cos(k * 16 / OVERLAY_RING_SEGS);
		float sn = startup_sin(k * 16 / OVERLAY_RING_SEGS);
		// Outer, +n, inner, -n
		for (int j = 0; j < 4; ++j) {
			float d_r = j == 0 ? thickness : j == 2 ? -thickness : 0;
			float d_n = j == 1 ? thickness : j == 3 ? -thickness : 0;
			float p[3];
			for (int a = 0; a < 3; ++a) {
				p[a] = (radius + d_r) * (cs * s[a] + sn * t[a]) + d_n * n[a];
			}
			overlay_vert(p[0], p[1], p[2]);
		}
	}
	for (int k = 0; k < OVERLAY_RING_SEGS; ++k) {
		uint32_t v = from + k * 4;
		for (int j = 0; j < 4; ++j) {
			int j1 = (j + 1) % 4;
			overlay_quad(v + j, v + 4 + j, v + 4 + j1, v + j1);
		}
	}
	overlay_color(from, color);
}

static void overlay_begin(mesh_data_t *md, uint32_t verts, uint32_t inds, float scale_pos) {
	md->scale_pos     = scale_pos;
	md->scale_tex     = 1.0f;
	md->vertex_arrays = (vertex_array_t_array_t *)any_array_create(2);
	_overlay_pos      = i16_array_create(verts * 4);
	_overlay_col      = i16_array_create(verts * 4);
	_overlay_ind      = u32_array_create(inds);
	_overlay_v        = 0;
	_overlay_i        = 0;
	_overlay_inv      = 1.0f / scale_pos;

	vertex_array_t *pos          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
	pos->attrib                  = "pos";
	pos->data                    = "short4norm";
	pos->values                  = _overlay_pos;
	md->vertex_arrays->buffer[0] = pos;

	vertex_array_t *col          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
	col->attrib                  = "col";
	col->data                    = "short4norm";
	col->values                  = _overlay_col;
	md->vertex_arrays->buffer[1] = col;
	md->index_array              = _overlay_ind;
}

// Per arrow: 3 rings + tip, 32 side quads, 3 fans and the cone
#define OVERLAY_ARROW_VERTS (OVERLAY_ARROW_SEGS * 3 + 1)
#define OVERLAY_ARROW_INDS  ((OVERLAY_ARROW_SEGS * 3 + (OVERLAY_ARROW_SEGS - 2) * 3) * 3)
#define OVERLAY_RING_VERTS  ((OVERLAY_RING_SEGS + 1) * 4)
#define OVERLAY_RING_INDS   (OVERLAY_RING_SEGS * 4 * 6)

static mesh_data_t *startup_make_compass(void) {
	mesh_data_t *md = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
	md->name        = ".Compass";
	overlay_begin(md, 3 * OVERLAY_ARROW_VERTS, 3 * OVERLAY_ARROW_INDS, 1.855f);
	overlay_arrows(0.035f, 1.25f, 0.145f, 1.2435f, 1.855f);
	return md;
}

static mesh_data_t *startup_make_gizmo(void) {
	mesh_data_t *md = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
	md->name        = ".Gizmo";
	overlay_begin(md, 3 * (OVERLAY_ARROW_VERTS + 8 + OVERLAY_RING_VERTS), 3 * (OVERLAY_ARROW_INDS + 36 + OVERLAY_RING_INDS), 2.005f);
	overlay_arrows(0.0175f, 0.95f, 0.145f, 1.3935f, 2.005f);
	for (int axis = 0; axis < 3; ++axis) {
		overlay_cube(axis, 1.0f, 0.1f);
	}
	float x[3]  = {1, 0, 0};
	float y[3]  = {0, 1, 0};
	float z[3]  = {0, 0, 1};
	float nx[3] = {-1, 0, 0};
	overlay_ring(2, x, y, z, 1.24f, 0.02f);
	overlay_ring(1, z, x, y, 1.24f, 0.02f);
	overlay_ring(0, z, y, nx, 1.24f, 0.02f);
	return md;
}

// 64 x 32 uv sphere of radius 0.5, same layout as geom_make_uv_sphere: the seam column and pole rows are duplicated for uvs
#define SPHERE_SEGS_U 64
#define SPHERE_SEGS_V 32

static mesh_data_t *startup_make_sphere(void) {
	mesh_data_t *md   = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
	md->name          = ".Sphere";
	md->scale_pos     = 1.0f;
	md->scale_tex     = 2.0f; // u spans 2 around the equator, v spans 1 between poles, texels stay square
	md->vertex_arrays = (vertex_array_t_array_t *)any_array_create(3);

	int          w   = SPHERE_SEGS_U + 1;
	int          h   = SPHERE_SEGS_V + 1;
	i16_array_t *pos = i16_array_create(w * h * 4);
	i16_array_t *nor = i16_array_create(w * h * 2);
	i16_array_t *tex = i16_array_create(w * h * 2);
	u32_array_t *ind = u32_array_create(SPHERE_SEGS_U * (SPHERE_SEGS_V - 1) * 6);

	// Steps of pi / 32 in both directions
	float   cu[SPHERE_SEGS_U + 1];
	float   su[SPHERE_SEGS_U + 1];
	int16_t tu[SPHERE_SEGS_U + 1];
	for (int x = 0; x < w; ++x) {
		cu[x] = -startup_cos(x * 64 / SPHERE_SEGS_U);
		su[x] = startup_sin(x * 64 / SPHERE_SEGS_U);
		tu[x] = startup_quantize(x / (float)SPHERE_SEGS_U);
	}

	for (int y = 0; y < h; ++y) {
		int16_t *po = pos->buffer + y * w * 4;
		int16_t *no = nor->buffer + y * w * 2;
		int16_t *te = tex->buffer + y * w * 2;
		int16_t  tv = startup_quantize((1.0f - y / (float)SPHERE_SEGS_V) / 2.0f);
		if (y > SPHERE_SEGS_V / 2) {
			// Mirror of the lower hemisphere
			int16_t *mpo = pos->buffer + (SPHERE_SEGS_V - y) * w * 4;
			int16_t *mno = nor->buffer + (SPHERE_SEGS_V - y) * w * 2;
			for (int x = 0; x < w; ++x) {
				po[x * 4]     = mpo[x * 4];
				po[x * 4 + 1] = mpo[x * 4 + 1];
				po[x * 4 + 2] = -mpo[x * 4 + 2];
				po[x * 4 + 3] = -mpo[x * 4 + 3];
				no[x * 2]     = mno[x * 2];
				no[x * 2 + 1] = mno[x * 2 + 1];
				te[x * 2]     = tu[x];
				te[x * 2 + 1] = tv;
			}
			continue;
		}
		float   sv = startup_sin(y * 32 / SPHERE_SEGS_V);
		float   nz = -startup_cos(y * 32 / SPHERE_SEGS_V);
		int16_t pz = startup_quantize(nz * 0.5f);
		int16_t pw = startup_quantize(nz);
		for (int x = 0; x < w; ++x) {
			float nx      = cu[x] * sv;
			float ny      = su[x] * sv;
			po[x * 4]     = startup_quantize(nx * 0.5f);
			po[x * 4 + 1] = startup_quantize(ny * 0.5f);
			po[x * 4 + 2] = pz;
			po[x * 4 + 3] = pw;
			no[x * 2]     = startup_quantize(nx);
			no[x * 2 + 1] = startup_quantize(ny);
			te[x * 2]     = tu[x];
			te[x * 2 + 1] = tv;
		}
	}
	// Pole vertices sit in the middle of their triangle's uv span
	for (int x = 0; x < SPHERE_SEGS_U; ++x) {
		tex->buffer[x * 2]                           = startup_quantize((x + 0.5f) / SPHERE_SEGS_U);
		tex->buffer[(SPHERE_SEGS_V * w + x + 1) * 2] = startup_quantize((x + 0.5f) / SPHERE_SEGS_U);
	}

	// Pole rows drop the degenerate triangle of each quad
	uint32_t *p = ind->buffer;
	for (int x = 0; x < SPHERE_SEGS_U; ++x) {
		p[0] = x;
		p[1] = w + x;
		p[2] = w + x + 1;
		p += 3;
	}
	for (int y = 1; y < SPHERE_SEGS_V - 1; ++y) {
		for (int x = 0; x < SPHERE_SEGS_U; ++x) {
			uint32_t a = y * w + x + 1;
			uint32_t b = y * w + x;
			uint32_t c = (y + 1) * w + x;
			uint32_t d = (y + 1) * w + x + 1;
			p[0]       = a;
			p[1]       = b;
			p[2]       = d;
			p[3]       = b;
			p[4]       = c;
			p[5]       = d;
			p += 6;
		}
	}
	for (int x = 0; x < SPHERE_SEGS_U; ++x) {
		uint32_t y = SPHERE_SEGS_V - 1;
		p[0]       = y * w + x + 1;
		p[1]       = y * w + x;
		p[2]       = (y + 1) * w + x + 1;
		p += 3;
	}

	char        *attribs[] = {"pos", "nor", "tex"};
	char        *datas[]   = {"short4norm", "short2norm", "short2norm"};
	i16_array_t *values[]  = {pos, nor, tex};
	for (int i = 0; i < 3; ++i) {
		vertex_array_t *vat          = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
		vat->attrib                  = attribs[i];
		vat->data                    = datas[i];
		vat->values                  = values[i];
		md->vertex_arrays->buffer[i] = vat;
	}
	md->index_array = ind;
	return md;
}

static int16_t  mesh__RotateX_pos[]     = {3276,   -16383, -16383, 0,      3276,   -16383, 16383,  0,      -3276,  -16383, 16383,  0,      -3276, -16383,
                                           -16383, 0,      -3276,  -16383, -16383, 0,      -3276,  -16383, 16383,  0,      -3276,  16383,  16383, 0,
                                           -3276,  16383,  -16383, 0,      -3276,  16383,  -16383, 0,      -3276,  16383,  16383,  0,      3276,  16383,
                                           16383,  0,      3276,   16383,  -16383, 0,      3276,   16383,  -16383, 0,      3276,   16383,  16383, 0,
                                           3276,   -16383, 16383,  0,      3276,   -16383, -16383, 0,      -3276,  -16383, -16383, -32767, -3276, 16383,
                                           -16383, -32767, 3276,   16383,  -16383, -32767, 3276,   -16383, -16383, -32767, -3276,  16383,  16383, 32767,
                                           -3276,  -16383, 16383,  32767,  3276,   -16383, 16383,  32767,  3276,   16383,  16383,  32767};
static int16_t  mesh__RotateX_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767, -32767, 0, -32767, 0, -32767, 0, -32767, 0,
                                           0, 32767,  0, 32767,  0, 32767,  0, 32767,  32767,  0, 32767,  0, 32767,  0, 32767,  0,
                                           0, 0,      0, 0,      0, 0,      0, 0,      0,      0, 0,      0, 0,      0, 0,      0};
static uint32_t mesh__RotateX_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                           12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__RotateY_pos[]     = {16383,  -3276,  -16383, 0,     16383,  -3276,  16383,  0,     -16383, -3276,  16383,  0,      -16383, -3276,
                                           -16383, 0,      -16383, -3276, -16383, 0,      -16383, -3276, 16383,  0,      -16383, 3276,   16383,  0,
                                           -16383, 3276,   -16383, 0,     -16383, 3276,   -16383, 0,     -16383, 3276,   16383,  0,      16383,  3276,
                                           16383,  0,      16383,  3276,  -16383, 0,      16383,  3276,  -16383, 0,      16383,  3276,   16383,  0,
                                           16383,  -3276,  16383,  0,     16383,  -3276,  -16383, 0,     -16383, -3276,  -16383, -32767, -16383, 3276,
                                           -16383, -32767, 16383,  3276,  -16383, -32767, 16383,  -3276, -16383, -32767, -16383, 3276,   16383,  32767,
                                           -16383, -3276,  16383,  32767, 16383,  -3276,  16383,  32767, 16383,  3276,   16383,  32767};
static int16_t  mesh__RotateY_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767, -32767, 0, -32767, 0, -32767, 0, -32767, 0,
                                           0, 32767,  0, 32767,  0, 32767,  0, 32767,  32767,  0, 32767,  0, 32767,  0, 32767,  0,
                                           0, 0,      0, 0,      0, 0,      0, 0,      0,      0, 0,      0, 0,      0, 0,      0};
static uint32_t mesh__RotateY_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                           12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__RotateZ_pos[]     = {16383,  -16383, -3276,  0,      16383,  -16383, 3276,   0,      -16383, -16383, 3276,   0,      -16383, -16383,
                                           -3276,  0,      -16383, -16383, -3276,  0,      -16383, -16383, 3276,   0,      -16383, 16383,  3276,   0,
                                           -16383, 16383,  -3276,  0,      -16383, 16383,  -3276,  0,      -16383, 16383,  3276,   0,      16383,  16383,
                                           3276,   0,      16383,  16383,  -3276,  0,      16383,  16383,  -3276,  0,      16383,  16383,  3276,   0,
                                           16383,  -16383, 3276,   0,      16383,  -16383, -3276,  0,      -16383, -16383, -3276,  -32767, -16383, 16383,
                                           -3276,  -32767, 16383,  16383,  -3276,  -32767, 16383,  -16383, -3276,  -32767, -16383, 16383,  3276,   32767,
                                           -16383, -16383, 3276,   32767,  16383,  -16383, 3276,   32767,  16383,  16383,  3276,   32767};
static int16_t  mesh__RotateZ_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767, -32767, 0, -32767, 0, -32767, 0, -32767, 0,
                                           0, 32767,  0, 32767,  0, 32767,  0, 32767,  32767,  0, 32767,  0, 32767,  0, 32767,  0,
                                           0, 0,      0, 0,      0, 0,      0, 0,      0,      0, 0,      0, 0,      0, 0,      0};
static uint32_t mesh__RotateZ_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                           12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__ScaleX_pos[]     = {-5898, -3276, -3276, 0,      -5898, -3276, 3276,  0,      -5898, 3276,  3276,  0,      -5898, 3276,  -3276, 0,
                                          -5898, 3276,  -3276, 0,      -5898, 3276,  3276,  0,      5898,  3276,  3276,  0,      5898,  3276,  -3276, 0,
                                          5898,  3276,  -3276, 0,      5898,  3276,  3276,  0,      5898,  -3276, 3276,  0,      5898,  -3276, -3276, 0,
                                          5898,  -3276, -3276, 0,      5898,  -3276, 3276,  0,      -5898, -3276, 3276,  0,      -5898, -3276, -3276, 0,
                                          -5898, 3276,  -3276, -32767, 5898,  3276,  -3276, -32767, 5898,  -3276, -3276, -32767, -5898, -3276, -3276, -32767,
                                          5898,  3276,  3276,  32767,  -5898, 3276,  3276,  32767,  -5898, -3276, 3276,  32767,  5898,  -3276, 3276,  32767};
static int16_t  mesh__ScaleX_nor[]     = {-32767, 0, -32767, 0, -32767, 0, -32767, 0, 0, 32767,  0, 32767,  0, 32767,  0, 32767,
                                          32767,  0, 32767,  0, 32767,  0, 32767,  0, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
                                          0,      0, 0,      0, 0,      0, 0,      0, 0, 0,      0, 0,      0, 0,      0, 0};
static uint32_t mesh__ScaleX_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                          12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__ScaleY_pos[]     = {3276,  -5898, -3276, 0,      3276,  -5898, 3276,  0,      -3276, -5898, 3276,  0,      -3276, -5898, -3276, 0,
                                          -3276, -5898, -3276, 0,      -3276, -5898, 3276,  0,      -3276, 5898,  3276,  0,      -3276, 5898,  -3276, 0,
                                          -3276, 5898,  -3276, 0,      -3276, 5898,  3276,  0,      3276,  5898,  3276,  0,      3276,  5898,  -3276, 0,
                                          3276,  5898,  -3276, 0,      3276,  5898,  3276,  0,      3276,  -5898, 3276,  0,      3276,  -5898, -3276, 0,
                                          -3276, -5898, -3276, -32767, -3276, 5898,  -3276, -32767, 3276,  5898,  -3276, -32767, 3276,  -5898, -3276, -32767,
                                          -3276, 5898,  3276,  32767,  -3276, -5898, 3276,  32767,  3276,  -5898, 3276,  32767,  3276,  5898,  3276,  32767};
static int16_t  mesh__ScaleY_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767, -32767, 0, -32767, 0, -32767, 0, -32767, 0,
                                          0, 32767,  0, 32767,  0, 32767,  0, 32767,  32767,  0, 32767,  0, 32767,  0, 32767,  0,
                                          0, 0,      0, 0,      0, 0,      0, 0,      0,      0, 0,      0, 0,      0, 0,      0};
static uint32_t mesh__ScaleY_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                          12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__ScaleZ_pos[]     = {-3276, -3276, 5898,  32767,  3276,  -3276, 5898,  32767,  3276,  3276,  5898,  32767,  -3276, 3276,  5898,  32767,
                                          -3276, 3276,  5898,  0,      3276,  3276,  5898,  0,      3276,  3276,  -5898, 0,      -3276, 3276,  -5898, 0,
                                          -3276, 3276,  -5898, -32767, 3276,  3276,  -5898, -32767, 3276,  -3276, -5898, -32767, -3276, -3276, -5898, -32767,
                                          -3276, -3276, -5898, 0,      3276,  -3276, -5898, 0,      3276,  -3276, 5898,  0,      -3276, -3276, 5898,  0,
                                          -3276, 3276,  5898,  0,      -3276, 3276,  -5898, 0,      -3276, -3276, -5898, 0,      -3276, -3276, 5898,  0,
                                          3276,  3276,  -5898, 0,      3276,  3276,  5898,  0,      3276,  -3276, 5898,  0,      3276,  -3276, -5898, 0};
static int16_t  mesh__ScaleZ_nor[]     = {0,      0, 0,      0, 0,      0, 0,      0, 0,     32767,  0,     32767,  0,     32767,  0,     32767,
                                          0,      0, 0,      0, 0,      0, 0,      0, 0,     -32767, 0,     -32767, 0,     -32767, 0,     -32767,
                                          -32767, 0, -32767, 0, -32767, 0, -32767, 0, 32767, 0,      32767, 0,      32767, 0,      32767, 0};
static uint32_t mesh__ScaleZ_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                          12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__TranslateX_pos[]     = {-11796, -3276, -3276, 0,      -11796, -3276, 3276,  0,      -11796, 3276,  3276,  0,      -11796, 3276,  -3276, 0,
                                              -11796, 3276,  -3276, 0,      -11796, 3276,  3276,  0,      11796,  3276,  3276,  0,      11796,  3276,  -3276, 0,
                                              11796,  3276,  -3276, 0,      11796,  3276,  3276,  0,      11796,  -3276, 3276,  0,      11796,  -3276, -3276, 0,
                                              11796,  -3276, -3276, 0,      11796,  -3276, 3276,  0,      -11796, -3276, 3276,  0,      -11796, -3276, -3276, 0,
                                              -11796, 3276,  -3276, -32767, 11796,  3276,  -3276, -32767, 11796,  -3276, -3276, -32767, -11796, -3276, -3276, -32767,
                                              11796,  3276,  3276,  32767,  -11796, 3276,  3276,  32767,  -11796, -3276, 3276,  32767,  11796,  -3276, 3276,  32767};
static int16_t  mesh__TranslateX_nor[]     = {-32767, 0, -32767, 0, -32767, 0, -32767, 0, 0, 32767,  0, 32767,  0, 32767,  0, 32767,
                                              32767,  0, 32767,  0, 32767,  0, 32767,  0, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
                                              0,      0, 0,      0, 0,      0, 0,      0, 0, 0,      0, 0,      0, 0,      0, 0};
static uint32_t mesh__TranslateX_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                              12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__TranslateY_pos[]     = {3276,  -11796, -3276, 0,      3276,  -11796, 3276,  0,      -3276, -11796, 3276,  0,      -3276, -11796, -3276, 0,
                                              -3276, -11796, -3276, 0,      -3276, -11796, 3276,  0,      -3276, 11796,  3276,  0,      -3276, 11796,  -3276, 0,
                                              -3276, 11796,  -3276, 0,      -3276, 11796,  3276,  0,      3276,  11796,  3276,  0,      3276,  11796,  -3276, 0,
                                              3276,  11796,  -3276, 0,      3276,  11796,  3276,  0,      3276,  -11796, 3276,  0,      3276,  -11796, -3276, 0,
                                              -3276, -11796, -3276, -32767, -3276, 11796,  -3276, -32767, 3276,  11796,  -3276, -32767, 3276,  -11796, -3276, -32767,
                                              -3276, 11796,  3276,  32767,  -3276, -11796, 3276,  32767,  3276,  -11796, 3276,  32767,  3276,  11796,  3276,  32767};
static int16_t  mesh__TranslateY_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767, -32767, 0, -32767, 0, -32767, 0, -32767, 0,
                                              0, 32767,  0, 32767,  0, 32767,  0, 32767,  32767,  0, 32767,  0, 32767,  0, 32767,  0,
                                              0, 0,      0, 0,      0, 0,      0, 0,      0,      0, 0,      0, 0,      0, 0,      0};
static uint32_t mesh__TranslateY_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                              12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__TranslateZ_pos[] = {-3276, -3276, 11796,  32767,  3276,  -3276, 11796,  32767,  3276,  3276,  11796,  32767,  -3276, 3276,  11796,  32767,
                                          -3276, 3276,  11796,  0,      3276,  3276,  11796,  0,      3276,  3276,  -11796, 0,      -3276, 3276,  -11796, 0,
                                          -3276, 3276,  -11796, -32767, 3276,  3276,  -11796, -32767, 3276,  -3276, -11796, -32767, -3276, -3276, -11796, -32767,
                                          -3276, -3276, -11796, 0,      3276,  -3276, -11796, 0,      3276,  -3276, 11796,  0,      -3276, -3276, 11796,  0,
                                          -3276, 3276,  11796,  0,      -3276, 3276,  -11796, 0,      -3276, -3276, -11796, 0,      -3276, -3276, 11796,  0,
                                          3276,  3276,  -11796, 0,      3276,  3276,  11796,  0,      3276,  -3276, 11796,  0,      3276,  -3276, -11796, 0};
static int16_t  mesh__TranslateZ_nor[] = {0,      0, 0,      0, 0,      0, 0,      0, 0,     32767,  0,     32767,  0,     32767,  0,     32767,
                                          0,      0, 0,      0, 0,      0, 0,      0, 0,     -32767, 0,     -32767, 0,     -32767, 0,     -32767,
                                          -32767, 0, -32767, 0, -32767, 0, -32767, 0, 32767, 0,      32767, 0,      32767, 0,      32767, 0};
static uint32_t mesh__TranslateZ_indices[] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                                              12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

static int16_t  mesh__Plane_pos[]     = {-16383, 0, -16383, 0, 16383, 0, -16383, 0, 16383, 0, 16383, 0, -16383, 0, 16383, 0};
static int16_t  mesh__Plane_nor[]     = {0, -32767, 0, -32767, 0, -32767, 0, -32767};
static int16_t  mesh__Plane_tex[]     = {3, 32763, 32763, 32763, 32763, 3, 3, 3};
static uint32_t mesh__Plane_indices[] = {3, 0, 1, 3, 1, 2};

#define STARTUP_LEN(a) (sizeof(a) / sizeof(a[0]))

static vertex_array_t *startup_vertex_array(char *attrib, char *data, i16_array_t *values) {
	vertex_array_t *vat = (vertex_array_t *)calloc(1, sizeof(vertex_array_t));
	vat->attrib         = attrib;
	vat->data           = data;
	vat->values         = values;
	return vat;
}

// The gizmo parts the picking renders (.RotateX...), and the plane
static mesh_data_t *startup_raw_mesh(char *name, int16_t *pos, int pos_n, int16_t *nor, int nor_n, int16_t *tex, int tex_n, uint32_t *ind, int ind_n) {
	mesh_data_t *md              = (mesh_data_t *)calloc(1, sizeof(mesh_data_t));
	md->name                     = name;
	md->scale_pos                = 1.0f;
	md->scale_tex                = 1.0f;
	md->vertex_arrays            = (vertex_array_t_array_t *)any_array_create(tex != NULL ? 3 : 2);
	md->vertex_arrays->buffer[0] = startup_vertex_array("pos", "short4norm", i16_array_create_from_raw(pos, pos_n));
	md->vertex_arrays->buffer[1] = startup_vertex_array("nor", "short2norm", i16_array_create_from_raw(nor, nor_n));
	if (tex != NULL) {
		md->vertex_arrays->buffer[2] = startup_vertex_array("tex", "short2norm", i16_array_create_from_raw(tex, tex_n));
	}
	md->index_array = u32_array_create_from_raw(ind, ind_n);
	return md;
}

#define STARTUP_GIZMO_MESH(n)                                                                                                                            \
	startup_raw_mesh("." #n, mesh__##n##_pos, STARTUP_LEN(mesh__##n##_pos), mesh__##n##_nor, STARTUP_LEN(mesh__##n##_nor), NULL, 0, mesh__##n##_indices, \
	                 STARTUP_LEN(mesh__##n##_indices))

// A shader context of the scene. Lists are space separated: attachments "RGBA64 R8", elements "name:data", constants
// "name:type:link", textures "name" or "name:link". Fields left out: no depth write, compare always, cull none, a pos:float2
// element (full screen passes). The contexts of a shader data follow each other.
typedef struct {
	char *data;
	char *name;
	char *shader; // <shader>.vert and <shader>.frag
	bool  depth_write;
	char *compare;
	char *cull;
	char *attachments;
	char *depth;
	char *elements;
	char *constants;
	char *textures;
} startup_context_t;

static const startup_context_t startup_contexts[] = {
    {.data        = "armdefault",
     .name        = "mesh",
     .shader      = "mesh_posnor",
     .depth_write = true,
     .compare     = "less",
     .cull        = "clockwise",
     .attachments = "RGBA64 RGBA64 RGBA64",
     .depth       = "D32",
     .elements    = "pos:short4norm nor:short2norm",
     .constants   = "WVP:float4x4:_world_view_proj_matrix N:float3x3:_normal_matrix"},
    {.data        = ".Gizmo",
     .name        = "overlay",
     .shader      = "mesh_poscol",
     .cull        = "clockwise",
     .attachments = "RGBA64",
     .elements    = "pos:short4norm col:short4norm",
     .constants   = "WVP:float4x4:_world_view_proj_matrix"},
    {.data        = "Material",
     .name        = "mesh",
     .shader      = "mesh_posnortex",
     .depth_write = true,
     .compare     = "less",
     .cull        = "clockwise",
     .attachments = "RGBA64 RGBA64 RGBA64",
     .depth       = "D32",
     .elements    = "pos:short4norm nor:short2norm tex:short2norm",
     .constants   = "WVP:float4x4:_world_view_proj_matrix N:float3x3:_normal_matrix tex_unpack:float:_tex_unpack",
     .textures    = "ImageTexture"},
    {.data        = "compositor_pass",
     .name        = "compositor_pass",
     .shader      = "compositor_pass",
     .attachments = "RGBA64",
     .constants   = "vignette_strength:float:_vignette_strength grain_strength:float:_grain_strength tonemap_strength:float:_tonemap_strength "
                    "lut_size:float:_lut_size contrast_strength:float:_contrast_strength gamma_strength:float:_gamma_strength",
     .textures    = "tex lut_tex:_lut_tex"},
    {.data        = "taa_pass",
     .name        = "taa_pass",
     .shader      = "taa_pass",
     .attachments = "RGBA64",
     .constants   = "inv_vp:float4x4:_taa_inv_vp prev_vp:float4x4:_taa_prev_vp texel:float2:_screen_size_inv taa_blend:float:_taa_blend",
     .textures    = "tex tex2 gbufferD"},
    {.data = "copy_pass", .name = "copy_pass", .shader = "copy_pass", .textures = "tex"},
    {.data = "copy_pass", .name = "copyR8_pass", .shader = "copy_pass", .attachments = "R8", .textures = "tex"},
    {.data = "copy_pass", .name = "copyR16_pass", .shader = "copy_pass", .attachments = "R16", .textures = "tex"},
    {.data = "copy_pass", .name = "copyRGBA64_pass", .shader = "copy_pass", .attachments = "RGBA64", .textures = "tex"},
    {.data = "copy_pass", .name = "copyRGBA128_pass", .shader = "copy_pass", .attachments = "RGBA128", .textures = "tex"},
    {.data        = "supersample_resolve",
     .name        = "supersample_resolve",
     .shader      = "supersample_resolve",
     .attachments = "RGBA32",
     .constants   = "screen_size_inv:float2:_screen_size_inv",
     .textures    = "tex"},
    {.data        = "supersample_resolve",
     .name        = "supersample_resolveRGBA64",
     .shader      = "supersample_resolve",
     .attachments = "RGBA64",
     .constants   = "screen_size_inv:float2:_screen_size_inv",
     .textures    = "tex"},
    {.data = "depth_to_ao_pass", .name = "depth_to_ao_pass", .shader = "depth_to_ao_pass", .attachments = "RGBA32", .textures = "height_map normal_map"},
    {.data        = "ssao_pass",
     .name        = "ssao_pass",
     .shader      = "ssao_pass",
     .attachments = "R8",
     .constants   = "invP:float4x4:_inv_proj_matrix V3:float3x3:_view_matrix3 camera_proj:float2:_camera_plane_proj screen_size_inv:float2:_screen_size_inv "
                    "frame_offset:float:_ssao_frame ssao_strength:float:_ssao_strength",
     .textures    = "gbufferD gbuffer0"},
    {.data        = "ssao_blur_pass",
     .name        = "ssao_blur_pass_x",
     .shader      = "ssao_blur_pass",
     .attachments = "R8",
     .constants   = "dir_inv:float2:_vec2x_inv",
     .textures    = "tex gbuffer0"},
    {.data        = "ssao_blur_pass",
     .name        = "ssao_blur_pass_y",
     .shader      = "ssao_blur_pass",
     .attachments = "R8",
     .constants   = "dir_inv:float2:_vec2y_inv",
     .textures    = "tex gbuffer0"},
    {.data        = "bloom_pass",
     .name        = "bloom_downsample_pass",
     .shader      = "bloom_downsample_pass",
     .attachments = "RGBA64",
     .constants   = "screen_size_inv:float2:_screen_size_inv current_mip_level:int:_bloom_current_mip",
     .textures    = "tex"},
    {.data        = "bloom_pass",
     .name        = "bloom_upsample_pass",
     .shader      = "bloom_upsample_pass",
     .attachments = "RGBA64",
     .constants   = "screen_size_inv:float2:_screen_size_inv current_mip_level:int:_bloom_current_mip sample_scale:float:_bloom_sample_scale "
                    "bloom_strength:float:_bloom_strength",
     .textures    = "tex"},
    {.data = "copy_mrt3_pass", .name = "copy_mrt3_pass", .shader = "copy_mrt3_pass", .attachments = "RGBA32 RGBA32 RGBA32", .textures = "tex0 tex1 tex2"},
    {.data = "copy_mrt3_pass", .name = "copy_mrt3RGBA64_pass", .shader = "copy_mrt3_pass", .attachments = "RGBA64 RGBA64 RGBA64", .textures = "tex0 tex1 tex2"},
    {.data        = "copy_mrt3_pass",
     .name        = "copy_mrt3RGBA128_pass",
     .shader      = "copy_mrt3_pass",
     .attachments = "RGBA128 RGBA128 RGBA128",
     .textures    = "tex0 tex1 tex2"},
    {.data      = "dilate_pass",
     .name      = "dilate_pass",
     .shader    = "dilate_pass",
     .constants = "dilate_radius:float:_dilate_radius tex_size:float2:_size(tex)",
     .textures  = "tex texdilate:_texdilatemap"},
    {.data        = "dilate_pass",
     .name        = "dilateRGBA64_pass",
     .shader      = "dilate_pass",
     .attachments = "RGBA64",
     .constants   = "dilate_radius:float:_dilate_radius tex_size:float2:_size(tex)",
     .textures    = "tex texdilate:_texdilatemap"},
    {.data        = "dilate_pass",
     .name        = "dilateRGBA128_pass",
     .shader      = "dilate_pass",
     .attachments = "RGBA128",
     .constants   = "dilate_radius:float:_dilate_radius tex_size:float2:_size(tex)",
     .textures    = "tex texdilate:_texdilatemap"},
    {.data        = "deferred_light",
     .name        = "deferred_light",
     .shader      = "deferred_light",
     .compare     = "greater",
     .attachments = "RGBA64",
     .depth       = "D32",
     .constants   = "invVP:float4x4:_inv_view_proj_matrix eye:float3:_camera_pos envmap_data:float4:_envmap_data envmap_num_mipmaps:int:_envmap_num_mipmaps "
                    "shirr0:float4:_envmap_irradiance0 shirr1:float4:_envmap_irradiance1 shirr2:float4:_envmap_irradiance2 shirr3:float4:_envmap_irradiance3 "
                    "shirr4:float4:_envmap_irradiance4 shirr5:float4:_envmap_irradiance5 shirr6:float4:_envmap_irradiance6",
     .textures    = "gbuffer0 gbuffer1 senvmap_radiance:_envmap_radiance senvmap_radiance0:_envmap_radiance0 senvmap_radiance1:_envmap_radiance1 "
                    "senvmap_radiance2:_envmap_radiance2 senvmap_radiance3:_envmap_radiance3 senvmap_radiance4:_envmap_radiance4 ssaotex"},
    {.data        = "world_pass",
     .name        = "world_pass",
     .shader      = "world_pass",
     .compare     = "less",
     .cull        = "clockwise",
     .attachments = "RGBA64",
     .depth       = "D32",
     .elements    = "pos:float3 nor:float3",
     .constants   = "SMVP:float4x4:_skydome_matrix envmap_data_world:float4:_envmap_data_world",
     .textures    = "envmap:_envmap"},
    {.data        = "depth_to_normal_pass",
     .name        = "depth_to_normal_pass",
     .shader      = "depth_to_normal_pass",
     .attachments = "RGBA32",
     .constants   = "texel_size:float2:_screen_size_inv",
     .textures    = "height_map"},
    {.data        = "undo_tiles_pass",
     .name        = "undo_tiles_pass",
     .shader      = "undo_tiles_pass",
     .attachments = "RGBA32",
     .constants   = "tile_size:float2:_undo_tile_size",
     .textures    = "tex0 tex1 tex2 tex3 tex4 tex5"},
};

// The words of a list, each split at ':' into up to 3 parts
static any_array_t *startup_words(char *list) {
	return list != NULL ? string_split(list, " ") : any_array_create(0);
}

static char *startup_part(char *word, int part) {
	any_array_t *parts = string_split(word, ":");
	return part < parts->length ? parts->buffer[part] : NULL;
}

static shader_context_t *startup_shader_context(const startup_context_t *t) {
	shader_context_t *sc   = (shader_context_t *)calloc(1, sizeof(shader_context_t));
	sc->name               = t->name;
	sc->depth_write        = t->depth_write;
	sc->compare_mode       = t->compare != NULL ? t->compare : "always";
	sc->cull_mode          = t->cull != NULL ? t->cull : "none";
	sc->vertex_shader      = string("%s.vert", t->shader);
	sc->fragment_shader    = string("%s.frag", t->shader);
	sc->shader_from_source = false;
	if (t->attachments != NULL) {
		sc->color_attachments = (string_array_t *)startup_words(t->attachments);
	}
	sc->depth_attachment = t->depth;

	any_array_t *elements = startup_words(t->elements != NULL ? t->elements : "pos:float2");
	sc->vertex_elements   = (vertex_element_t_array_t *)any_array_create(elements->length);
	for (int i = 0; i < elements->length; ++i) {
		vertex_element_t *ve           = (vertex_element_t *)calloc(1, sizeof(vertex_element_t));
		ve->name                       = startup_part(elements->buffer[i], 0);
		ve->data                       = startup_part(elements->buffer[i], 1);
		sc->vertex_elements->buffer[i] = ve;
	}

	any_array_t *constants = startup_words(t->constants);
	sc->constants          = (shader_const_t_array_t *)any_array_create(constants->length);
	for (int i = 0; i < constants->length; ++i) {
		shader_const_t *c        = (shader_const_t *)calloc(1, sizeof(shader_const_t));
		c->name                  = startup_part(constants->buffer[i], 0);
		c->type                  = startup_part(constants->buffer[i], 1);
		c->link                  = startup_part(constants->buffer[i], 2);
		sc->constants->buffer[i] = c;
	}

	any_array_t *textures = startup_words(t->textures);
	sc->texture_units     = (tex_unit_t_array_t *)any_array_create(textures->length);
	for (int i = 0; i < textures->length; ++i) {
		tex_unit_t *tu               = (tex_unit_t *)calloc(1, sizeof(tex_unit_t));
		tu->name                     = startup_part(textures->buffer[i], 0);
		tu->link                     = startup_part(textures->buffer[i], 1);
		sc->texture_units->buffer[i] = tu;
	}
	return sc;
}

static any_array_t *startup_shader_datas(void) {
	any_array_t *datas = any_array_create(0);
	for (int i = 0; i < (int)STARTUP_LEN(startup_contexts); ++i) {
		const startup_context_t *t  = &startup_contexts[i];
		shader_data_t           *sd = datas->length > 0 ? datas->buffer[datas->length - 1] : NULL;
		if (sd == NULL || !string_equals(sd->name, t->data)) {
			sd           = (shader_data_t *)calloc(1, sizeof(shader_data_t));
			sd->name     = t->data;
			sd->contexts = any_array_create(0);
			any_array_push(datas, sd);
		}
		shader_context_t *sc = startup_shader_context(t);
		any_array_push(sd->contexts, sc);

		if (string_equals(sc->name, "bloom_upsample_pass")) { // Adds to the target
			sc->blend_source            = "blend_one";
			sc->blend_destination       = "blend_one";
			sc->alpha_blend_source      = "blend_zero";
			sc->alpha_blend_destination = "blend_one";
		}
		if (string_equals(sd->name, "Material")) {
			sc->bind_textures            = (bind_tex_t_array_t *)any_array_create(1);
			bind_tex_t *bt               = (bind_tex_t *)calloc(1, sizeof(bind_tex_t));
			bt->name                     = string_copy("ImageTexture");
			bt->file                     = string_copy("");
			sc->bind_textures->buffer[0] = bt;
		}
	}
	return datas;
}

static object_desc_t *startup_object(char *name, char *type, char *material, f32 *transform, f32 *dimensions, bool visible) {
	object_desc_t *o = (object_desc_t *)calloc(1, sizeof(object_desc_t));
	o->name          = name;
	o->type          = type;
	o->data_ref      = name;
	o->transform     = f32_array_create_from_raw(transform, 16);
	o->dimensions    = dimensions != NULL ? f32_array_create_from_raw(dimensions, 3) : NULL;
	o->visible       = visible;
	o->spawn         = true;
	o->material_ref  = material;
	return o;
}

#define STARTUP_IDENTITY {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}

// The parts of the gizmo, at their offset from its center, and their size
typedef struct {
	char *name;
	f32   x, y, z;
	f32   w, h, d;
} startup_gizmo_part_t;

static const startup_gizmo_part_t startup_gizmo_parts[] = {
    {".RotateX", 0.0f, 0.75f, 0.75f, 0.20000000298023224f, 1.0f, 1.0f},
    {".RotateY", 0.75f, 0.0f, 0.75f, 1.0f, 0.20000000298023224f, 1.0f},
    {".RotateZ", 0.75f, 0.75f, 0.0f, 1.0f, 1.0f, 0.20000000298023224f},
    {".ScaleX", 0.949999988079071f, 0.0f, 0.0f, 0.36000001430511475f, 0.20000000298023224f, 0.20000000298023224f},
    {".ScaleY", 0.0f, 0.949999988079071f, 0.0f, 0.20000000298023224f, 0.36000001430511475f, 0.20000000298023224f},
    {".ScaleZ", 0.0f, 0.0f, 0.949999988079071f, 0.20000000298023224f, 0.20000000298023224f, 0.36000001430511475f},
    {".TranslateX", 1.7000000476837158f, 0.0f, 0.0f, 0.7200000286102295f, 0.20000000298023224f, 0.20000000298023224f},
    {".TranslateY", 0.0f, 1.7000000476837158f, 0.0f, 0.20000000298023224f, 0.7200000286102295f, 0.20000000298023224f},
    {".TranslateZ", 0.0f, 0.0f, 1.7000000476837158f, 0.20000000298023224f, 0.20000000298023224f, 0.7200000286102295f},
};

scene_t *startup_get_scene(void) {
	scene_t *scene    = (scene_t *)calloc(1, sizeof(scene_t));
	scene->name       = "Scene";
	scene->camera_ref = "Camera";
	scene->world_ref  = "World";

	scene->mesh_datas = any_array_create(0);
	any_array_push(scene->mesh_datas, startup_make_compass());
	any_array_push(scene->mesh_datas, startup_make_gizmo());
	any_array_push(scene->mesh_datas, STARTUP_GIZMO_MESH(RotateX));
	any_array_push(scene->mesh_datas, STARTUP_GIZMO_MESH(RotateY));
	any_array_push(scene->mesh_datas, STARTUP_GIZMO_MESH(RotateZ));
	any_array_push(scene->mesh_datas, STARTUP_GIZMO_MESH(ScaleX));
	any_array_push(scene->mesh_datas, STARTUP_GIZMO_MESH(ScaleY));
	any_array_push(scene->mesh_datas, STARTUP_GIZMO_MESH(ScaleZ));
	any_array_push(scene->mesh_datas, STARTUP_GIZMO_MESH(TranslateX));
	any_array_push(scene->mesh_datas, STARTUP_GIZMO_MESH(TranslateY));
	any_array_push(scene->mesh_datas, STARTUP_GIZMO_MESH(TranslateZ));
	any_array_push(scene->mesh_datas, startup_raw_mesh(".Plane", mesh__Plane_pos, STARTUP_LEN(mesh__Plane_pos), mesh__Plane_nor, STARTUP_LEN(mesh__Plane_nor),
	                                                   mesh__Plane_tex, STARTUP_LEN(mesh__Plane_tex), mesh__Plane_indices, STARTUP_LEN(mesh__Plane_indices)));
	any_array_push(scene->mesh_datas, startup_make_sphere());

	scene->camera_datas            = any_array_create(1);
	camera_data_t *cd              = (camera_data_t *)calloc(1, sizeof(camera_data_t));
	cd->name                       = "Camera";
	cd->near_plane                 = 0.009999999776482582f;
	cd->far_plane                  = 100.00006103515625f;
	cd->fov                        = 0.6909999847412109f;
	cd->aspect                     = 0.0f;
	cd->frustum_culling            = false;
	scene->camera_datas->buffer[0] = cd;

	scene->world_datas            = any_array_create(1);
	world_data_t *wd              = (world_data_t *)calloc(1, sizeof(world_data_t));
	wd->name                      = "World";
	wd->color                     = -15987700;
	wd->strength                  = 1.0f;
	wd->irradiance                = NULL; // Generated in util_render_make_default_world()
	wd->radiance                  = NULL;
	wd->radiance_mipmaps          = 0;
	wd->envmap                    = NULL;
	scene->world_datas->buffer[0] = wd;

	scene->shader_datas = startup_shader_datas();

	scene->objects = any_array_create(0);
	any_array_push(scene->objects, startup_object(".Compass", "mesh_object", ".Gizmo", (f32[])STARTUP_IDENTITY, (f32[]){2.0f, 2.0f, 2.0f}, false));
	object_desc_t *gizmo = startup_object(".Gizmo", "mesh_object", ".Gizmo", (f32[])STARTUP_IDENTITY,
	                                      (f32[]){2.1500000953674316f, 2.1500000953674316f, 2.1500000953674316f}, false);
	gizmo->children      = (object_desc_t_array_t *)any_array_create(0);
	for (int i = 0; i < (int)STARTUP_LEN(startup_gizmo_parts); ++i) {
		const startup_gizmo_part_t *p             = &startup_gizmo_parts[i];
		f32                         transform[16] = STARTUP_IDENTITY;
		transform[12]                             = p->x;
		transform[13]                             = p->y;
		transform[14]                             = p->z;
		any_array_push((any_array_t *)gizmo->children, startup_object(p->name, "mesh_object", "armdefault", transform, (f32[]){p->w, p->h, p->d}, false));
	}
	any_array_push(scene->objects, gizmo);
	any_array_push(scene->objects, startup_object("Camera", "camera_object", NULL,
	                                              (f32[]){0.8660249710083008f, 0.5f, -0.0f, 0.0f, 0.0f, -0.0f, 1.0f, 0.0f, 0.5f, -0.8660249710083008f, -0.0f,
	                                                      0.0f, 1.649999976158142f, -2.8628199100494385f, -0.0f, 1.0f},
	                                              NULL, true));
	any_array_push(scene->objects, startup_object(".Plane", "mesh_object", "Material", (f32[])STARTUP_IDENTITY, (f32[]){1.0f, 0.0f, 1.0f}, false));
	any_array_push(scene->objects, startup_object(".Sphere", "mesh_object", "Material", (f32[])STARTUP_IDENTITY, (f32[]){1.0f, 1.0f, 1.0f}, false));
	return scene;
}
