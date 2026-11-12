
#include "../global.h"

// Paint draws cull the mesh: a dab draws the whole mesh in texture space and every texel of the uv islands runs the brush
// shader to find out it is too far. The triangles are grouped into chunks near each other in space, each with a bounding
// sphere. A draw only takes the chunks whose sphere can reach a dab on screen, as ranges of a reordered index buffer.
// Conservative: a chunk is kept unless its sphere is surely outside every dab (depth is not known on the cpu).

static bool paint_cull_enabled = true; // Off for comparisons
static f32  paint_cull_kept    = 1.0;  // Share of the mesh the last culled draw kept

void paint_cull_set_enabled(bool on) {
	paint_cull_enabled = on;
}

f32 paint_cull_last_kept() {
	return paint_cull_kept;
}

#ifdef IRON_METAL

#define PAINT_CULL_CHUNK    128 // Triangles a chunk
#define PAINT_CULL_MESHES   8
#define PAINT_CULL_MAX_KEEP 0.7 // Past this share of the mesh the whole mesh draws in one range

void gpu_draw_ranges_next(gpu_buffer_t *ib, i32 *starts, i32 *counts, i32 n);

typedef struct {
	mesh_data_t  *data;
	i32           index_count;
	gpu_buffer_t *ib;      // Indices by chunk
	i32           chunks;
	f32          *spheres; // Center xyz and radius of each chunk, normalized object space
} paint_cull_mesh_t;

static paint_cull_mesh_t paint_cull_meshes[PAINT_CULL_MESHES];
static i32               paint_cull_next = 0;
static i32              *paint_cull_starts = NULL;
static i32              *paint_cull_counts = NULL;
static i32               paint_cull_capacity = 0;

typedef struct {
	u32 code;
	u32 tri;
} paint_cull_key_t;

static int paint_cull_compare(const void *a, const void *b) {
	u32 x = ((paint_cull_key_t *)a)->code;
	u32 y = ((paint_cull_key_t *)b)->code;
	return x < y ? -1 : x > y ? 1 : 0;
}

static u32 paint_cull_spread(u32 v) { // 10 bits to every third bit
	v = (v | (v << 16)) & 0x030000FF;
	v = (v | (v << 8)) & 0x0300F00F;
	v = (v | (v << 4)) & 0x030C30C3;
	v = (v | (v << 2)) & 0x09249249;
	return v;
}

static i16_array_t *paint_cull_positions(mesh_data_t *data) {
	for (i32 i = 0; data->vertex_arrays != NULL && i < data->vertex_arrays->length; ++i) {
		vertex_array_t *va = data->vertex_arrays->buffer[i];
		if (string_equals(va->attrib, "pos")) {
			return va->values;
		}
	}
	return NULL;
}

static paint_cull_mesh_t *paint_cull_get(mesh_data_t *data) {
	u32_array_t *ia = data->index_array;
	if (ia == NULL || ia->length < PAINT_CULL_CHUNK * 3 * 8) {
		return NULL; // Small meshes draw whole
	}
	for (i32 i = 0; i < PAINT_CULL_MESHES; ++i) {
		paint_cull_mesh_t *m = &paint_cull_meshes[i];
		if (m->data == data && m->index_count == ia->length) {
			return m;
		}
	}
	i16_array_t *pos = paint_cull_positions(data);
	if (pos == NULL) {
		return NULL;
	}

	// Triangles sorted along a z-order curve of their centers, neighbours in the order are neighbours in space
	i32               tris = ia->length / 3;
	paint_cull_key_t *keys = malloc(sizeof(paint_cull_key_t) * tris);
	for (i32 t = 0; t < tris; ++t) {
		f32 c[3] = {0, 0, 0};
		for (i32 k = 0; k < 3; ++k) {
			u32 v = ia->buffer[t * 3 + k];
			for (i32 a = 0; a < 3; ++a) {
				c[a] += pos->buffer[v * 4 + a] / 32767.0f;
			}
		}
		u32 q[3];
		for (i32 a = 0; a < 3; ++a) {
			f32 n = (c[a] / 3.0f + 1.0f) * 0.5f; // Normalized positions are within -1..1
			n     = n < 0.0f ? 0.0f : n > 1.0f ? 1.0f : n;
			q[a]  = (u32)(n * 1023.0f);
		}
		keys[t] = (paint_cull_key_t){paint_cull_spread(q[0]) | (paint_cull_spread(q[1]) << 1) | (paint_cull_spread(q[2]) << 2), (u32)t};
	}
	qsort(keys, tris, sizeof(paint_cull_key_t), paint_cull_compare);

	paint_cull_mesh_t *m = &paint_cull_meshes[paint_cull_next];
	paint_cull_next      = (paint_cull_next + 1) % PAINT_CULL_MESHES;
	if (m->ib != NULL) {
		gpu_buffer_destroy(m->ib);
		free(m->ib);
		free(m->spheres);
	}
	m->data        = data;
	m->index_count = ia->length;
	m->chunks      = (tris + PAINT_CULL_CHUNK - 1) / PAINT_CULL_CHUNK;
	m->spheres     = malloc(sizeof(f32) * 4 * m->chunks);
	m->ib          = gpu_create_index_buffer(ia->length);
	u32 *indices   = gpu_index_buffer_lock(m->ib);
	for (i32 c = 0; c < m->chunks; ++c) {
		f32 lo[3] = {1e9, 1e9, 1e9};
		f32 hi[3] = {-1e9, -1e9, -1e9};
		i32 end   = (c + 1) * PAINT_CULL_CHUNK < tris ? (c + 1) * PAINT_CULL_CHUNK : tris;
		for (i32 s = c * PAINT_CULL_CHUNK; s < end; ++s) {
			u32 t = keys[s].tri;
			for (i32 k = 0; k < 3; ++k) {
				u32 v                = ia->buffer[t * 3 + k];
				indices[s * 3 + k]   = v;
				for (i32 a = 0; a < 3; ++a) {
					f32 p = pos->buffer[v * 4 + a] / 32767.0f;
					lo[a] = p < lo[a] ? p : lo[a];
					hi[a] = p > hi[a] ? p : hi[a];
				}
			}
		}
		f32 *sp = &m->spheres[c * 4];
		sp[0]   = (lo[0] + hi[0]) * 0.5f;
		sp[1]   = (lo[1] + hi[1]) * 0.5f;
		sp[2]   = (lo[2] + hi[2]) * 0.5f;
		f32 dx = hi[0] - sp[0], dy = hi[1] - sp[1], dz = hi[2] - sp[2];
		sp[3]   = sqrtf(dx * dx + dy * dy + dz * dz);
	}
	gpu_index_buffer_unlock(m->ib);
	free(keys);
	return m;
}

// Dab capsules on screen, ndc: from (x0, y0) to (x1, y1), world radius r
typedef struct {
	f32 x0, y0, x1, y1, r;
} paint_cull_dab_t;

// Before drawing the paint target o: limits the draw to the chunks the dabs can reach
void paint_cull_draw_next(mesh_object_t *o, paint_cull_dab_t *dabs, i32 count) {
	if (count == 0 || !paint_cull_enabled) {
		return;
	}
	paint_cull_mesh_t *m = paint_cull_get(o->data);
	if (m == NULL) {
		return;
	}
	if (paint_cull_capacity < m->chunks) {
		paint_cull_capacity = m->chunks;
		paint_cull_starts   = realloc(paint_cull_starts, sizeof(i32) * paint_cull_capacity);
		paint_cull_counts   = realloc(paint_cull_counts, sizeof(i32) * paint_cull_capacity);
	}

	transform_t *t  = o->base->transform;
	mat4_t       w  = t->world_unpack;
	mat4_t       vp = scene_camera->vp;
	mat4_t       p  = scene_camera->p;
	// World size of the normalized unit, the sphere radius scales with it
	f32 sx    = sqrtf(w.m00 * w.m00 + w.m01 * w.m01 + w.m02 * w.m02);
	f32 sy    = sqrtf(w.m10 * w.m10 + w.m11 * w.m11 + w.m12 * w.m12);
	f32 sz    = sqrtf(w.m20 * w.m20 + w.m21 * w.m21 + w.m22 * w.m22);
	f32 scale = sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz);
	f32 near  = scene_camera->data->near_plane;

	// Bounds of all dabs, most chunks are out of them
	f32 bx0 = 1e9, by0 = 1e9, bx1 = -1e9, by1 = -1e9, rmax = 0.0;
	for (i32 i = 0; i < count; ++i) {
		paint_cull_dab_t *d = &dabs[i];
		bx0                 = fminf(bx0, fminf(d->x0, d->x1));
		bx1                 = fmaxf(bx1, fmaxf(d->x0, d->x1));
		by0                 = fminf(by0, fminf(d->y0, d->y1));
		by1                 = fmaxf(by1, fmaxf(d->y0, d->y1));
		rmax                = fmaxf(rmax, d->r);
	}

	i32 ranges = 0;
	i32 kept   = 0;
	for (i32 c = 0; c < m->chunks; ++c) {
		f32   *sp     = &m->spheres[c * 4];
		vec4_t wc     = vec4_apply_mat4((vec4_t){sp[0], sp[1], sp[2], 1.0}, w);
		vec4_t clip   = vec4_apply_mat4((vec4_t){wc.x, wc.y, wc.z, 1.0}, vp);
		f32    radius = sp[3] * scale;
		bool   keep   = true;
		f32    wmin   = clip.w - radius; // Nearest the sphere gets to the camera
		if (wmin > near) {
			f32 cx = clip.x / clip.w;
			f32 cy = clip.y / clip.w;
			// The sphere and a dab radius around it, at the nearest depth they cover the most screen
			f32 ex = (radius + rmax) * fabsf(p.m00) / wmin;
			f32 ey = (radius + rmax) * fabsf(p.m11) / wmin;
			keep   = cx + ex >= bx0 && cx - ex <= bx1 && cy + ey >= by0 && cy - ey <= by1;
			if (keep && count > 1) {
				keep = false;
				for (i32 i = 0; i < count && !keep; ++i) {
					paint_cull_dab_t *d  = &dabs[i];
					f32               rx = (radius + d->r) * fabsf(p.m00) / wmin;
					f32               ry = (radius + d->r) * fabsf(p.m11) / wmin;
					keep = cx + rx >= fminf(d->x0, d->x1) && cx - rx <= fmaxf(d->x0, d->x1) && cy + ry >= fminf(d->y0, d->y1) &&
					       cy - ry <= fmaxf(d->y0, d->y1);
				}
			}
		}
		if (!keep) {
			continue;
		}
		kept++;
		i32 start = c * PAINT_CULL_CHUNK * 3;
		i32 len   = ((c + 1) * PAINT_CULL_CHUNK * 3 < m->index_count ? PAINT_CULL_CHUNK * 3 : m->index_count - start);
		if (ranges > 0 && paint_cull_starts[ranges - 1] + paint_cull_counts[ranges - 1] == start) {
			paint_cull_counts[ranges - 1] += len; // Next to the previous chunk
		}
		else {
			paint_cull_starts[ranges] = start;
			paint_cull_counts[ranges] = len;
			ranges++;
		}
	}
	paint_cull_kept = kept / (f32)m->chunks;
	if (kept > m->chunks * PAINT_CULL_MAX_KEEP) {
		return; // The whole mesh in one draw is as cheap
	}
	gpu_draw_ranges_next(m->ib, paint_cull_starts, paint_cull_counts, ranges);
}

#endif
