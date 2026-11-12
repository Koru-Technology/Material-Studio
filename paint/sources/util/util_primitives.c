#include "../global.h"

// Procedural meshes: the new project templates and the empty root of multi-mesh imports

#define SHAPE_MIN_SIZE 0.0001

// From TOOL_TYPE_EMPTY to TOOL_TYPE_TORUS, see util_shape_make_data
static char *_shape_names[] = {"empty", "plane", "cube", "sphere", "cone", "cylinder", "capsule", "torus"};
static i32   _shape_default_segments[] = {0, 1, 0, 128, 64, 64, 32, 64};

raw_mesh_t *geom_make_plane(f32 size_x, f32 size_y, i32 verts_x, i32 verts_y, f32 uv_scale) {

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {0});
	mesh->scale_pos  = 1.0;
	mesh->scale_tex  = uv_scale;
	mesh->name       = "Plane";
	mesh->has_next   = false;

	// Pack positions to (-1, 1) range
	f32 half_x      = size_x / 2.0;
	f32 half_y      = size_y / 2.0;
	mesh->scale_pos = math_max(size_x, size_y);
	f32 inv         = (1 / (float)mesh->scale_pos) * 32767;

	mesh->posa = i16_array_create(verts_x * verts_y * 4);
	mesh->nora = i16_array_create(verts_x * verts_y * 2);
	mesh->texa = i16_array_create(verts_x * verts_y * 2);
	mesh->inda = u32_array_create((verts_x - 1) * (verts_y - 1) * 6);
	f32 step_x = size_x / (float)(verts_x - 1);
	f32 step_y = size_y / (float)(verts_y - 1);
	for (i32 i = 0; i < verts_x * verts_y; ++i) {
		f32 x                         = (i % verts_x) * step_x - half_x;
		f32 y                         = math_floor(i / (float)verts_x) * step_y - half_y;
		mesh->posa->buffer[i * 4]     = math_floor(x * inv);
		mesh->posa->buffer[i * 4 + 1] = math_floor(y * inv);
		mesh->posa->buffer[i * 4 + 2] = 0;
		mesh->nora->buffer[i * 2]     = 0;
		mesh->nora->buffer[i * 2 + 1] = 0;
		mesh->posa->buffer[i * 4 + 3] = 32767;
		x                             = (i % verts_x) / (float)(verts_x - 1);
		y                             = 1.0 - math_floor(i / (float)verts_x) / (float)(verts_y - 1);
		mesh->texa->buffer[i * 2]     = math_floor(x * 32767);
		mesh->texa->buffer[i * 2 + 1] = math_floor(y * 32767);
	}
	for (i32 i = 0; i < (verts_x - 1) * (verts_y - 1); ++i) {
		f32 x                         = i % (verts_x - 1);
		f32 y                         = math_floor(i / (float)(verts_y - 1));
		mesh->inda->buffer[i * 6]     = y * verts_x + x;
		mesh->inda->buffer[i * 6 + 1] = y * verts_x + x + 1;
		mesh->inda->buffer[i * 6 + 2] = (y + 1) * verts_x + x;
		mesh->inda->buffer[i * 6 + 3] = y * verts_x + x + 1;
		mesh->inda->buffer[i * 6 + 4] = (y + 1) * verts_x + x + 1;
		mesh->inda->buffer[i * 6 + 5] = (y + 1) * verts_x + x;
	}

	return mesh;
}

raw_mesh_t *geom_make_uv_sphere(f32 radius, i32 width_segments, i32 height_segments, bool stretch_uv, f32 uv_scale) {

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {0});
	mesh->scale_pos  = 1.0;
	mesh->scale_tex  = 1.0;
	mesh->name       = "Sphere";
	mesh->has_next   = false;

	// Pack positions to (-1, 1) range
	mesh->scale_pos = radius;
	mesh->scale_tex = uv_scale;
	f32 inv         = (1 / (float)mesh->scale_pos) * 32767;
	f32 pi2         = math_pi() * 2;

	i32 width_verts  = width_segments + 1;
	i32 height_verts = height_segments + 1;
	mesh->posa       = i16_array_create(width_verts * height_verts * 4);
	mesh->nora       = i16_array_create(width_verts * height_verts * 2);
	mesh->texa       = i16_array_create(width_verts * height_verts * 2);
	mesh->inda       = u32_array_create(width_segments * height_segments * 6 - width_segments * 6);

	vec4_t nor = (vec4_t){0.0, 0.0, 0.0, 1.0};
	i32    pos = 0;
	for (i32 y = 0; y < height_verts; ++y) {
		f32 v      = y / (float)height_segments;
		f32 v_flip = 1.0 - v;
		if (!stretch_uv) {
			v_flip /= 2;
		}
		f32 u_off = y == 0 ? 0.5 / (float)width_segments : y == height_segments ? -0.5 / (float)width_segments : 0.0;
		for (i32 x = 0; x < width_verts; ++x) {
			f32 u                      = x / (float)width_segments;
			f32 u_pi2                  = u * pi2;
			f32 v_pi                   = v * math_pi();
			f32 v_pi_sin               = math_sin(v_pi);
			f32 vx                     = -radius * math_cos(u_pi2) * v_pi_sin;
			f32 vy                     = radius * math_sin(u_pi2) * v_pi_sin;
			f32 vz                     = -radius * math_cos(v_pi);
			i32 i4                     = pos * 4;
			i32 i2                     = pos * 2;
			mesh->posa->buffer[i4]     = math_floor(vx * inv);
			mesh->posa->buffer[i4 + 1] = math_floor(vy * inv);
			mesh->posa->buffer[i4 + 2] = math_floor(vz * inv);
			nor                        = (vec4_t){vx, vy, vz, 1.0};
			nor                        = vec4_norm(nor);
			mesh->posa->buffer[i4 + 3] = math_floor(nor.z * 32767);
			mesh->nora->buffer[i2]     = math_floor(nor.x * 32767);
			mesh->nora->buffer[i2 + 1] = math_floor(nor.y * 32767);
			i32 tx                     = (math_floor((u + u_off) * 32767) - 1);
			i32 ty                     = (math_floor(v_flip * 32767) - 1);
			tx                         = tx < 0 ? 0 : (tx > 32767 ? 32767 : tx);
			ty                         = ty < 0 ? 0 : (ty > 32767 ? 32767 : ty);
			mesh->texa->buffer[i2]     = tx;
			mesh->texa->buffer[i2 + 1] = ty;
			pos++;
		}
	}

	pos                  = 0;
	i32 height_segments1 = height_segments - 1;
	for (i32 y = 0; y < height_segments; ++y) {
		for (i32 x = 0; x < width_segments; ++x) {
			i32 x1 = x + 1;
			i32 y1 = y + 1;
			f32 a  = y * width_verts + x1;
			f32 b  = y * width_verts + x;
			f32 c  = y1 * width_verts + x;
			f32 d  = y1 * width_verts + x1;
			if (y > 0) {
				mesh->inda->buffer[pos++] = a;
				mesh->inda->buffer[pos++] = b;
				mesh->inda->buffer[pos++] = d;
			}
			if (y < height_segments1) {
				mesh->inda->buffer[pos++] = b;
				mesh->inda->buffer[pos++] = c;
				mesh->inda->buffer[pos++] = d;
			}
		}
	}

	return mesh;
}

mesh_geo_t *util_geo_create(i32 vertex_count, i32 index_count) {
	mesh_geo_t *g      = calloc(1, sizeof(mesh_geo_t));
	g->vertex_count    = vertex_count;
	g->index_count     = index_count;
	g->vertex_capacity = vertex_count;
	g->index_capacity  = index_count;
	g->pos             = calloc(vertex_count * 3 + 1, sizeof(f32));
	g->nor             = calloc(vertex_count * 3 + 1, sizeof(f32));
	g->tex             = calloc(vertex_count * 2 + 1, sizeof(f32));
	g->ind             = calloc(index_count + 1, sizeof(u32));
	return g;
}

void util_geo_free(mesh_geo_t *g) {
	free(g->pos);
	free(g->nor);
	free(g->tex);
	free(g->ind);
	free(g);
}

static i16 nodes_mesh_pack(f32 f) {
	f     = f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
	f32 x = f * 32767.0;
	i32 t = (i32)x;
	f32 r = x - (f32)t;
	return (i16)(t + (r >= 0.5f) - (r <= -0.5f));
}

mesh_data_t *nodes_mesh_geo_to_data(mesh_geo_t *g, char *name) {
	f32 scale_pos = 0.0;
	f32 scale_tex = 1.0;
	for (i32 i = 0; i < g->vertex_count; ++i) {
		scale_pos = math_max(scale_pos, math_abs(g->pos[i * 3]));
		scale_pos = math_max(scale_pos, math_abs(g->pos[i * 3 + 1]));
		scale_pos = math_max(scale_pos, math_abs(g->pos[i * 3 + 2]));
		scale_tex = math_max(scale_tex, math_abs(g->tex[i * 2]));
		scale_tex = math_max(scale_tex, math_abs(g->tex[i * 2 + 1]));
	}
	if (scale_pos <= 0.0) {
		scale_pos = 1.0;
	}

	i16_array_t *va0  = i16_array_create(g->vertex_count * 4);
	i16_array_t *va1  = i16_array_create(g->vertex_count * 2);
	i16_array_t *va2  = i16_array_create(g->vertex_count * 2);
	u32_array_t *inda = u32_array_create(g->index_count);
	for (i32 i = 0; i < g->vertex_count; ++i) {
		va0->buffer[i * 4]     = nodes_mesh_pack(g->pos[i * 3] / scale_pos);
		va0->buffer[i * 4 + 1] = nodes_mesh_pack(g->pos[i * 3 + 1] / scale_pos);
		va0->buffer[i * 4 + 2] = nodes_mesh_pack(g->pos[i * 3 + 2] / scale_pos);
		va0->buffer[i * 4 + 3] = nodes_mesh_pack(g->nor[i * 3 + 2]);
		va1->buffer[i * 2]     = nodes_mesh_pack(g->nor[i * 3]);
		va1->buffer[i * 2 + 1] = nodes_mesh_pack(g->nor[i * 3 + 1]);
		va2->buffer[i * 2]     = nodes_mesh_pack(g->tex[i * 2] / scale_tex);
		va2->buffer[i * 2 + 1] = nodes_mesh_pack(g->tex[i * 2 + 1] / scale_tex);
	}
	memcpy(inda->buffer, g->ind, g->index_count * sizeof(u32));

	mesh_data_t *raw   = ALLOC_INIT(mesh_data_t, {.name          = string_copy(name),
	                                              .vertex_arrays = any_array_create_from_raw(
                                                    (void *[]){
                                                        ALLOC_INIT(vertex_array_t, {.values = va0, .attrib = "pos", .data = "short4norm"}),
                                                        ALLOC_INIT(vertex_array_t, {.values = va1, .attrib = "nor", .data = "short2norm"}),
                                                        ALLOC_INIT(vertex_array_t, {.values = va2, .attrib = "tex", .data = "short2norm"}),
                                                    },
                                                    3),
	                                              .index_array = inda,
	                                              .scale_pos   = scale_pos,
	                                              .scale_tex   = scale_tex});
	mesh_data_t *md    = mesh_data_create(raw);
	md->_->owns_arrays = true;
	return md;
}

static void nodes_mesh_geo_copy_into(mesh_geo_t *dst, mesh_geo_t *src, i32 vertex_offset, i32 index_offset) {
	memcpy(dst->pos + vertex_offset * 3, src->pos, src->vertex_count * 3 * sizeof(f32));
	memcpy(dst->nor + vertex_offset * 3, src->nor, src->vertex_count * 3 * sizeof(f32));
	memcpy(dst->tex + vertex_offset * 2, src->tex, src->vertex_count * 2 * sizeof(f32));
	for (i32 i = 0; i < src->index_count; ++i) {
		dst->ind[index_offset + i] = src->ind[i] + vertex_offset;
	}
}

// Takes ownership of a and b
static mesh_geo_t *nodes_mesh_geo_join(mesh_geo_t *a, mesh_geo_t *b) {
	mesh_geo_t *g = util_geo_create(a->vertex_count + b->vertex_count, a->index_count + b->index_count);
	nodes_mesh_geo_copy_into(g, a, 0, 0);
	nodes_mesh_geo_copy_into(g, b, a->vertex_count, a->index_count);
	util_geo_free(a);
	util_geo_free(b);
	return g;
}

// Shapes, each one fits into a unit box

static void nodes_mesh_shape_sphere(f32 u, f32 v, f32 *pos, f32 *nor) {
	f32 theta = u * 2.0 * math_pi();
	f32 phi   = (1.0 - v) * math_pi();
	nor[0]    = math_sin(phi) * math_cos(theta);
	nor[1]    = math_sin(phi) * math_sin(theta);
	nor[2]    = math_cos(phi);
	pos[0]    = nor[0] * 0.5;
	pos[1]    = nor[1] * 0.5;
	pos[2]    = nor[2] * 0.5;
}

static void nodes_mesh_shape_cylinder(f32 u, f32 v, f32 *pos, f32 *nor) {
	f32 theta = u * 2.0 * math_pi();
	nor[0]    = math_cos(theta);
	nor[1]    = math_sin(theta);
	nor[2]    = 0.0;
	pos[0]    = nor[0] * 0.5;
	pos[1]    = nor[1] * 0.5;
	pos[2]    = v - 0.5;
}

static void nodes_mesh_shape_cone(f32 u, f32 v, f32 *pos, f32 *nor) {
	f32 theta = u * 2.0 * math_pi();
	f32 r     = 0.5 * (1.0 - v);
	f32 len   = math_sqrt(1.25);
	nor[0]    = math_cos(theta) / len;
	nor[1]    = math_sin(theta) / len;
	nor[2]    = 0.5 / len;
	pos[0]    = math_cos(theta) * r;
	pos[1]    = math_sin(theta) * r;
	pos[2]    = v - 0.5;
}

static void nodes_mesh_shape_torus(f32 u, f32 v, f32 *pos, f32 *nor) {
	f32 theta = u * 2.0 * math_pi();
	f32 phi   = v * 2.0 * math_pi();
	f32 major = 0.375;
	f32 minor = 0.125;
	nor[0]    = math_cos(phi) * math_cos(theta);
	nor[1]    = math_cos(phi) * math_sin(theta);
	nor[2]    = math_sin(phi);
	pos[0]    = (major + minor * math_cos(phi)) * math_cos(theta);
	pos[1]    = (major + minor * math_cos(phi)) * math_sin(theta);
	pos[2]    = minor * math_sin(phi);
}

static void nodes_mesh_shape_plane(f32 u, f32 v, f32 *pos, f32 *nor) {
	pos[0] = u - 0.5;
	pos[1] = v - 0.5;
	pos[2] = 0.0;
	nor[0] = 0.0;
	nor[1] = 0.0;
	nor[2] = 1.0;
}

static mesh_geo_t *nodes_mesh_shape_grid(i32 nu, i32 nv, void (*f)(f32, f32, f32 *, f32 *)) {
	mesh_geo_t *g = util_geo_create((nu + 1) * (nv + 1), nu * nv * 6);
	for (i32 j = 0; j <= nv; ++j) {
		for (i32 i = 0; i <= nu; ++i) {
			i32 vi = j * (nu + 1) + i;
			f32 u  = i / (f32)nu;
			f32 v  = j / (f32)nv;
			f(u, v, g->pos + vi * 3, g->nor + vi * 3);
			g->tex[vi * 2]     = u;
			g->tex[vi * 2 + 1] = 1.0 - v;
		}
	}
	i32 ii = 0;
	for (i32 j = 0; j < nv; ++j) {
		for (i32 i = 0; i < nu; ++i) {
			u32 a        = j * (nu + 1) + i;
			u32 b        = a + 1;
			u32 c        = b + nu + 1;
			u32 d        = a + nu + 1;
			g->ind[ii++] = a;
			g->ind[ii++] = b;
			g->ind[ii++] = c;
			g->ind[ii++] = a;
			g->ind[ii++] = c;
			g->ind[ii++] = d;
		}
	}
	return g;
}

static mesh_geo_t *nodes_mesh_shape_cap(i32 segments, f32 z) {
	mesh_geo_t *g = util_geo_create(segments + 2, segments * 3);
	for (i32 i = 0; i <= segments + 1; ++i) {
		// Last vertex is the center
		f32 theta         = i / (f32)segments * 2.0 * math_pi();
		f32 r             = i > segments ? 0.0 : 0.5;
		g->pos[i * 3]     = math_cos(theta) * r;
		g->pos[i * 3 + 1] = math_sin(theta) * r;
		g->pos[i * 3 + 2] = z;
		g->nor[i * 3 + 2] = z > 0.0 ? 1.0 : -1.0;
		g->tex[i * 2]     = (z > 0.0 ? 0.25 : 0.75) + g->pos[i * 3] * 0.5;
		g->tex[i * 2 + 1] = 0.25 - g->pos[i * 3 + 1] * 0.5;
	}
	for (i32 i = 0; i < segments; ++i) {
		g->ind[i * 3]     = segments + 1;
		g->ind[i * 3 + 1] = z > 0.0 ? i : i + 1;
		g->ind[i * 3 + 2] = z > 0.0 ? i + 1 : i;
	}
	return g;
}

static mesh_geo_t *nodes_mesh_shape_box() {
	mesh_geo_t *g = util_geo_create(24, 36);
	for (i32 face = 0; face < 6; ++face) {
		i32 axis  = face / 2;
		f32 sign  = face % 2 == 0 ? 1.0 : -1.0;
		i32 u     = (axis + 1) % 3;
		i32 v     = (axis + 2) % 3;
		f32 cu[4] = {-0.5, 0.5, 0.5, -0.5};
		f32 cv[4] = {-0.5, -0.5, 0.5, 0.5};
		for (i32 i = 0; i < 4; ++i) {
			i32 vi                = face * 4 + i;
			i32 corner            = sign > 0.0 ? i : 3 - i;
			g->pos[vi * 3 + axis] = 0.5 * sign;
			g->pos[vi * 3 + u]    = cu[corner];
			g->pos[vi * 3 + v]    = cv[corner];
			g->nor[vi * 3 + axis] = sign;
			// Faces are laid out in a 3x2 grid
			g->tex[vi * 2]     = (face % 3 + cu[corner] + 0.5) / 3.0;
			g->tex[vi * 2 + 1] = (face / 3 + 0.5 - cv[corner]) / 3.0;
		}
		u32 quad[6] = {0, 1, 2, 0, 2, 3};
		for (i32 i = 0; i < 6; ++i) {
			g->ind[face * 6 + i] = face * 4 + quad[i];
		}
	}
	return g;
}

// Moves the side of a capped shape into the lower half of the texture
static void nodes_mesh_shape_side_uvs(mesh_geo_t *g) {
	for (i32 i = 0; i < g->vertex_count; ++i) {
		g->tex[i * 2 + 1] = 0.5 + g->tex[i * 2 + 1] * 0.5;
	}
}

mesh_geo_t *nodes_mesh_shape(i32 shape, f32 *size, i32 segments) {
	segments      = segments < 3 ? 3 : (segments > 512 ? 512 : segments);
	mesh_geo_t *g = NULL;
	if (shape == 1) {
		g = nodes_mesh_shape_grid(segments, math_max(segments / 2, 2), nodes_mesh_shape_sphere);
	}
	else if (shape == 2) {
		g = nodes_mesh_shape_grid(segments, 1, nodes_mesh_shape_cylinder);
		nodes_mesh_shape_side_uvs(g);
		g = nodes_mesh_geo_join(g, nodes_mesh_shape_cap(segments, 0.5));
		g = nodes_mesh_geo_join(g, nodes_mesh_shape_cap(segments, -0.5));
	}
	else if (shape == 3) {
		g = nodes_mesh_shape_grid(segments, 1, nodes_mesh_shape_cone);
		nodes_mesh_shape_side_uvs(g);
		g = nodes_mesh_geo_join(g, nodes_mesh_shape_cap(segments, -0.5));
	}
	else if (shape == 4) {
		g = nodes_mesh_shape_grid(segments, math_max(segments / 2, 3), nodes_mesh_shape_torus);
	}
	else if (shape == 5) {
		g = nodes_mesh_shape_grid(1, 1, nodes_mesh_shape_plane);
	}
	else {
		g = nodes_mesh_shape_box();
	}

	// Drop the collapsed triangles at poles and tips
	i32 index_count = 0;
	for (i32 i = 0; i + 2 < g->index_count; i += 3) {
		f32 *a  = g->pos + g->ind[i] * 3;
		f32 *b  = g->pos + g->ind[i + 1] * 3;
		f32 *c  = g->pos + g->ind[i + 2] * 3;
		f32  cx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
		f32  cy = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
		f32  cz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
		if (cx * cx + cy * cy + cz * cz < 1e-16) {
			continue;
		}
		g->ind[index_count++] = g->ind[i];
		g->ind[index_count++] = g->ind[i + 1];
		g->ind[index_count++] = g->ind[i + 2];
	}
	g->index_count = index_count;

	for (i32 i = 0; i < g->vertex_count; ++i) {
		f32 *p   = g->pos + i * 3;
		f32 *n   = g->nor + i * 3;
		f32  len = 0.0;
		for (i32 k = 0; k < 3; ++k) {
			p[k] *= size[k];
			n[k] /= math_max(math_abs(size[k]), 0.0001);
			len += n[k] * n[k];
		}
		len = math_sqrt(len);
		for (i32 k = 0; k < 3; ++k) {
			n[k] /= len;
		}
	}
	return g;
}

static void _shape_capsule_vertex(mesh_geo_t *g, i32 i, f32 x, f32 y, f32 z, vec4_t nor, f32 u, f32 v) {
	g->pos[i * 3]     = x;
	g->pos[i * 3 + 1] = y;
	g->pos[i * 3 + 2] = z;
	g->nor[i * 3]     = nor.x;
	g->nor[i * 3 + 1] = nor.y;
	g->nor[i * 3 + 2] = nor.z;
	g->tex[i * 2]     = u;
	g->tex[i * 2 + 1] = v;
}

// Upright capsule centered at the origin, the caps stay round where the box allows it
static mesh_geo_t *_shape_make_capsule(f32 size_x, f32 size_y, f32 size_z, i32 sides) {
	i32         rings = math_max(sides / 4, 1); // Per cap
	i32         rows  = (rings + 1) * 2;
	f32         rx    = size_x / 2.0;
	f32         ry    = size_y / 2.0;
	f32         cap   = math_min(math_min(rx, ry), size_z / 2.0);
	f32         mid   = size_z / 2.0 - cap; // Half height of the straight part
	f32         arc   = cap * math_pi() / 2.0;
	mesh_geo_t *g     = util_geo_create(rows * (sides + 1), (rows - 1) * sides * 6);
	i32         vi    = 0;
	i32         ii    = 0;
	for (i32 r = 0; r < rows; ++r) {
		bool top = r > rings;
		f32  phi = ((top ? r - 1 : r) / (f32)rings - 1.0) * math_pi() / 2.0;
		f32  cr  = math_cos(phi);
		f32  sr  = math_sin(phi);
		f32  z   = (top ? mid : -mid) + sr * cap;
		f32  v   = ((phi + math_pi() / 2.0) / (math_pi() / 2.0) * arc + (top ? mid * 2.0 : 0.0)) / (arc * 2.0 + mid * 2.0);
		for (i32 k = 0; k <= sides; ++k) {
			f32    a   = k / (f32)sides * math_pi() * 2.0;
			vec4_t nor = vec4_norm((vec4_t){math_cos(a) * cr / rx, math_sin(a) * cr / ry, sr / cap, 1.0});
			_shape_capsule_vertex(g, vi++, math_cos(a) * cr * rx, math_sin(a) * cr * ry, z, nor, k / (f32)sides, v);
		}
	}
	for (i32 r = 0; r < rows - 1; ++r) {
		if (r == rings && mid < SHAPE_MIN_SIZE) {
			continue; // No straight part
		}
		for (i32 k = 0; k < sides; ++k) {
			i32 v00      = r * (sides + 1) + k;
			i32 v01      = v00 + sides + 1;
			g->ind[ii++] = v00;
			g->ind[ii++] = v00 + 1;
			g->ind[ii++] = v01 + 1;
			g->ind[ii++] = v00;
			g->ind[ii++] = v01 + 1;
			g->ind[ii++] = v01;
		}
	}
	g->vertex_count = vi;
	g->index_count  = ii;
	return g;
}

static mesh_data_t *_shape_geo_data(mesh_geo_t *g, char *name) {
	mesh_data_t *md = nodes_mesh_geo_to_data(g, name);
	util_geo_free(g);
	return md;
}

// Shape of a template
static mesh_data_t *_shape_make_data(i32 tool, i32 segments) {
	if (tool == TOOL_TYPE_PLANE) {
		return mesh_data_create(import_mesh_raw_mesh(geom_make_plane(1, 1, segments + 1, segments + 1, 1.0)));
	}
	if (tool == TOOL_TYPE_CUBE) {
		return _shape_geo_data(nodes_mesh_shape(0, (f32[]){2.0, 2.0, 2.0}, 0), "Cube");
	}
	if (tool == TOOL_TYPE_SPHERE) {
		return mesh_data_create(import_mesh_raw_mesh(geom_make_uv_sphere(1, segments, math_max(segments / 2, 2), true, 1.0)));
	}
	if (tool == TOOL_TYPE_CONE) {
		return _shape_geo_data(nodes_mesh_shape(3, (f32[]){1.0, 1.0, 1.0}, segments), "Cone");
	}
	if (tool == TOOL_TYPE_CYLINDER) {
		return _shape_geo_data(nodes_mesh_shape(2, (f32[]){1.0, 1.0, 1.0}, segments), "Cylinder");
	}
	if (tool == TOOL_TYPE_CAPSULE) {
		return _shape_geo_data(_shape_make_capsule(1.0, 1.0, 2.0, segments), "Capsule");
	}
	if (tool == TOOL_TYPE_TORUS) {
		return _shape_geo_data(nodes_mesh_shape(4, (f32[]){2.5, 2.5, 2.0}, segments), "Torus");
	}
	return NULL;
}

// Procedural shapes of the project templates, NULL when the name is not one of them
mesh_data_t *util_shape_make_data(char *name) {
	if (string_equals(name, "empty")) {
		// No geometry, the object only carries a transform
		raw_mesh_t  *mesh  = ALLOC_INIT(raw_mesh_t, {.name      = "Empty",
		                                             .posa      = i16_array_create(0),
		                                             .nora      = i16_array_create(0),
		                                             .texa      = i16_array_create(0),
		                                             .inda      = u32_array_create(0),
		                                             .scale_pos = 1.0,
		                                             .scale_tex = 1.0});
		mesh_data_t *md    = mesh_data_create(import_mesh_raw_mesh(mesh));
		md->_->owns_arrays = true;
		return md;
	}
	if (string_equals(name, "plane_2048")) {
		return mesh_data_create(import_mesh_raw_mesh(geom_make_plane(1, 1, 2048, 2048, 1.0)));
	}
	if (string_equals(name, "sphere_2048")) {
		return mesh_data_create(import_mesh_raw_mesh(geom_make_uv_sphere(1, 4096, 2048, true, 1.0)));
	}
	for (i32 tool = TOOL_TYPE_PLANE; tool <= TOOL_TYPE_TORUS; ++tool) {
		if (string_equals(name, _shape_names[tool - TOOL_TYPE_EMPTY])) {
			return _shape_make_data(tool, _shape_default_segments[tool - TOOL_TYPE_EMPTY]);
		}
	}
	return NULL;
}
