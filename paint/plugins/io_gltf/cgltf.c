
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "engine.h"
#include "iron_array.h"
#include "iron_obj.h"
#include <math.h>
#include <stdarg.h>

static bool  has_next     = false;
static int   current_node = 0;
static float scale_pos    = 1.0;

static cgltf_size io_gltf_index_count(cgltf_primitive *prim) {
	if (prim->indices != NULL) {
		return prim->indices->count;
	}
	for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
		if (prim->attributes[i].type == cgltf_attribute_type_position && prim->attributes[i].data != NULL) {
			return prim->attributes[i].data->count;
		}
	}
	return 0;
}

static uint32_t io_gltf_read_index(cgltf_primitive *prim, cgltf_size i) {
	return prim->indices != NULL ? (uint32_t)cgltf_accessor_read_index(prim->indices, i) : (uint32_t)i;
}

static cgltf_accessor *io_gltf_attribute(cgltf_primitive *prim, cgltf_attribute_type type) {
	for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
		cgltf_attribute *a = &prim->attributes[i];
		if (a->type == type && a->index == 0 && a->data != NULL) {
			return a->data;
		}
	}
	return NULL;
}

// Primitives drawn as triangles, with positions
static bool io_gltf_takes_primitive(cgltf_primitive *prim) {
	return prim->type == cgltf_primitive_type_triangles && io_gltf_attribute(prim, cgltf_attribute_type_position) != NULL;
}

// All triangle primitives of mesh become one mesh: their vertices follow each other, their indices are offset to them.
// Normals are kept when every primitive has them, else computed. Uvs (the first set) are zero where a primitive has none.
void io_gltf_parse_mesh(raw_mesh_t *raw, cgltf_mesh *mesh, float *to_world, float *scale) {
	cgltf_size vertex_total = 0;
	cgltf_size index_total  = 0;
	bool       all_normals  = true;
	bool       any_uvs      = false;
	for (cgltf_size p = 0; p < mesh->primitives_count; ++p) {
		cgltf_primitive *prim = &mesh->primitives[p];
		if (!io_gltf_takes_primitive(prim)) {
			continue;
		}
		cgltf_accessor *pos = io_gltf_attribute(prim, cgltf_attribute_type_position);
		cgltf_accessor *nor = io_gltf_attribute(prim, cgltf_attribute_type_normal);
		vertex_total += pos->count;
		index_total += io_gltf_index_count(prim) / 3 * 3;
		all_normals = all_normals && nor != NULL && nor->count == pos->count;
		any_uvs     = any_uvs || io_gltf_attribute(prim, cgltf_attribute_type_texcoord) != NULL;
	}
	if (vertex_total == 0 || index_total == 0 || vertex_total > INT32_MAX / 4 || index_total > INT32_MAX) {
		return;
	}

	int       vertex_count = (int)vertex_total;
	int       index_count  = (int)index_total;
	uint32_t *inda         = malloc(sizeof(uint32_t) * index_total);
	float    *posa32       = malloc(sizeof(float) * vertex_total * 3);
	float    *nora32       = all_normals ? malloc(sizeof(float) * vertex_total * 3) : NULL;
	float    *texa32       = any_uvs ? calloc(vertex_total * 2, sizeof(float)) : NULL;

	cgltf_size voff = 0;
	cgltf_size ioff = 0;
	for (cgltf_size p = 0; p < mesh->primitives_count; ++p) {
		cgltf_primitive *prim = &mesh->primitives[p];
		if (!io_gltf_takes_primitive(prim)) {
			continue;
		}
		cgltf_accessor *pos    = io_gltf_attribute(prim, cgltf_attribute_type_position);
		cgltf_accessor *nor    = io_gltf_attribute(prim, cgltf_attribute_type_normal);
		cgltf_accessor *tex    = io_gltf_attribute(prim, cgltf_attribute_type_texcoord);
		cgltf_size      vcount = pos->count;
		for (cgltf_size i = 0; i < vcount; ++i) {
			cgltf_accessor_read_float(pos, i, posa32 + (voff + i) * 3, 3);
		}
		if (nora32 != NULL) {
			for (cgltf_size i = 0; i < vcount; ++i) {
				cgltf_accessor_read_float(nor, i, nora32 + (voff + i) * 3, 3);
			}
		}
		if (texa32 != NULL && tex != NULL) {
			for (cgltf_size i = 0; i < vcount && i < tex->count; ++i) {
				cgltf_accessor_read_float(tex, i, texa32 + (voff + i) * 2, 2);
			}
		}
		cgltf_size icount = io_gltf_index_count(prim) / 3 * 3;
		for (cgltf_size i = 0; i < icount; ++i) {
			uint32_t index   = io_gltf_read_index(prim, i);
			inda[ioff + i] = (uint32_t)voff + (index < vcount ? index : 0); // Out of range: a degenerate triangle
		}
		voff += vcount;
		ioff += icount;
	}

	float *m = to_world;
	for (int i = 0; i < vertex_count; ++i) {
		float x           = posa32[i * 3 + 0];
		float y           = -posa32[i * 3 + 2];
		float z           = posa32[i * 3 + 1];
		posa32[i * 3 + 0] = m[0] * x + m[4] * y + m[8] * z + m[12];
		posa32[i * 3 + 1] = m[1] * x + m[5] * y + m[9] * z + m[13];
		posa32[i * 3 + 2] = m[2] * x + m[6] * y + m[10] * z + m[14];
	}

	if (nora32 != NULL) {
		for (int i = 0; i < vertex_count; ++i) {
			float x   = nora32[i * 3 + 0] / scale[0];
			float y   = -nora32[i * 3 + 2] / scale[2];
			float z   = nora32[i * 3 + 1] / scale[1];
			float tx  = m[0] * x + m[4] * y + m[8] * z;
			float ty  = m[1] * x + m[5] * y + m[9] * z;
			float tz  = m[2] * x + m[6] * y + m[10] * z;
			float len = sqrtf(tx * tx + ty * ty + tz * tz);
			if (len > 1e-6f) {
				tx /= len;
				ty /= len;
				tz /= len;
			}
			nora32[i * 3 + 0] = tx;
			nora32[i * 3 + 1] = ty;
			nora32[i * 3 + 2] = tz;
		}
	}

	// Pack positions to (-1, 1) range
	float hx = 0.0;
	float hy = 0.0;
	float hz = 0.0;
	for (int i = 0; i < vertex_count; ++i) {
		float f = fabsf(posa32[i * 3]);
		if (hx < f)
			hx = f;
		f = fabsf(posa32[i * 3 + 1]);
		if (hy < f)
			hy = f;
		f = fabsf(posa32[i * 3 + 2]);
		if (hz < f)
			hz = f;
	}

	float _scale_pos = fmax(hx, fmax(hy, hz));
	if (_scale_pos > scale_pos)
		scale_pos = _scale_pos;
	float inv = 1 / scale_pos;

	// Pack into 16bit
	short *posa = malloc(sizeof(short) * vertex_count * 4);
	for (int i = 0; i < vertex_count; ++i) {
		posa[i * 4]     = posa32[i * 3] * 32767 * inv;
		posa[i * 4 + 1] = posa32[i * 3 + 1] * 32767 * inv;
		posa[i * 4 + 2] = posa32[i * 3 + 2] * 32767 * inv;
	}

	short *nora = malloc(sizeof(short) * vertex_count * 2);
	if (nora32 != NULL) {
		for (int i = 0; i < vertex_count; ++i) {
			nora[i * 2]     = nora32[i * 3] * 32767;
			nora[i * 2 + 1] = nora32[i * 3 + 1] * 32767;
			posa[i * 4 + 3] = nora32[i * 3 + 2] * 32767;
		}
	}
	else {
		// Calc normals
		for (int i = 0; i < index_count / 3; ++i) {
			int   i1  = inda[i * 3];
			int   i2  = inda[i * 3 + 1];
			int   i3  = inda[i * 3 + 2];
			float vax = posa32[i1 * 3];
			float vay = posa32[i1 * 3 + 1];
			float vaz = posa32[i1 * 3 + 2];
			float vbx = posa32[i2 * 3];
			float vby = posa32[i2 * 3 + 1];
			float vbz = posa32[i2 * 3 + 2];
			float vcx = posa32[i3 * 3];
			float vcy = posa32[i3 * 3 + 1];
			float vcz = posa32[i3 * 3 + 2];
			float cbx = vcx - vbx;
			float cby = vcy - vby;
			float cbz = vcz - vbz;
			float abx = vax - vbx;
			float aby = vay - vby;
			float abz = vaz - vbz;
			float x = cbx, y = cby, z = cbz;
			cbx     = y * abz - z * aby;
			cby     = z * abx - x * abz;
			cbz     = x * aby - y * abx;
			float n = sqrt(cbx * cbx + cby * cby + cbz * cbz);
			if (n > 0.0) {
				float inv_n = 1.0 / n;
				cbx *= inv_n;
				cby *= inv_n;
				cbz *= inv_n;
			}
			nora[i1 * 2]     = (int)(cbx * 32767);
			nora[i1 * 2 + 1] = (int)(cby * 32767);
			posa[i1 * 4 + 3] = (int)(cbz * 32767);
			nora[i2 * 2]     = (int)(cbx * 32767);
			nora[i2 * 2 + 1] = (int)(cby * 32767);
			posa[i2 * 4 + 3] = (int)(cbz * 32767);
			nora[i3 * 2]     = (int)(cbx * 32767);
			nora[i3 * 2 + 1] = (int)(cby * 32767);
			posa[i3 * 4 + 3] = (int)(cbz * 32767);
		}
	}

	short *texa = NULL;
	if (texa32 != NULL) {
		texa = malloc(sizeof(short) * vertex_count * 2);
		for (int i = 0; i < vertex_count; ++i) {
			texa[i * 2]     = texa32[i * 2] * 32767;
			texa[i * 2 + 1] = texa32[i * 2 + 1] * 32767;
		}
	}

	raw->posa         = (i16_array_t *)malloc(sizeof(i16_array_t));
	raw->posa->buffer = posa;
	raw->posa->length = raw->posa->capacity = vertex_count * 4;

	raw->nora         = (i16_array_t *)malloc(sizeof(i16_array_t));
	raw->nora->buffer = nora;
	raw->nora->length = raw->nora->capacity = vertex_count * 2;

	if (texa != NULL) {
		raw->texa         = (i16_array_t *)malloc(sizeof(i16_array_t));
		raw->texa->buffer = texa;
		raw->texa->length = raw->texa->capacity = vertex_count * 2;
	}

	raw->inda         = (u32_array_t *)malloc(sizeof(u32_array_t));
	raw->inda->buffer = inda;
	raw->inda->length = raw->inda->capacity = index_count;

	raw->scale_pos = scale_pos;
	raw->scale_tex = 1.0;

	free(posa32);
	free(nora32);
	free(texa32);
}

void *io_gltf_parse(char *buf, size_t size, const char *path) {
	cgltf_options options = {0};
	cgltf_data   *data    = NULL;
	cgltf_result  result  = cgltf_parse(&options, buf, size, &data);
	if (result == cgltf_result_success) {
		result = cgltf_load_buffers(&options, data, path);
	}
	if (result == cgltf_result_success) {
		result = cgltf_validate(data); // Accessors and indices in range of their buffers
	}
	if (result != cgltf_result_success) {
		if (data != NULL) {
			cgltf_free(data);
		}
		current_node = 0;
		scale_pos    = 1.0;
		return NULL;
	}
	if (current_node == 0) { // A new file
		scale_pos = 1.0;
	}

	raw_mesh_t *raw = (raw_mesh_t *)calloc(sizeof(raw_mesh_t), 1);

	for (; current_node < data->nodes_count; ++current_node) {
		cgltf_node *n = &data->nodes[current_node];
		if (n->mesh != NULL) {
			const char *node_name = n->name != NULL ? n->name : (n->mesh->name != NULL ? n->mesh->name : "mesh");
			raw->name             = malloc(strlen(node_name) + 1);
			strcpy(raw->name, node_name);
			float m[16];
			cgltf_node_transform_world(n, m);
			float scale[3] = {1.0f, 1.0f, 1.0f};
			if (n->has_scale) {
				scale[0] = n->scale[0];
				scale[1] = n->scale[1];
				scale[2] = n->scale[2];
			}
			io_gltf_parse_mesh(raw, n->mesh, m, scale);
			break;
		}
	}

	current_node++;
	has_next = false;
	for (size_t i = current_node; i < data->nodes_count; ++i) {
		cgltf_node *n = &data->nodes[i];
		if (n->mesh != NULL) {
			has_next = true;
			break;
		}
	}

	cgltf_free(data);

	if (!has_next) {
		current_node = 0;
	}

	raw->has_next = has_next;

	return raw;
}


typedef struct {
	char  *buf;
	size_t len;
	size_t cap;
} json_buf_t;

static void json_buf_append(json_buf_t *j, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int tl = vsnprintf(NULL, 0, fmt, ap); // Any length, names included
	va_end(ap);
	if (tl <= 0) {
		return;
	}
	while (j->len + (size_t)tl + 1 > j->cap) {
		j->cap *= 2;
		j->buf = (char *)realloc(j->buf, j->cap);
	}
	va_start(ap, fmt);
	vsnprintf(j->buf + j->len, (size_t)tl + 1, fmt, ap);
	va_end(ap);
	j->len += (size_t)tl;
}

// name as a json string body: quotes, backslashes and control characters escaped
static char *json_escape(const char *name) {
	size_t n   = strlen(name);
	char  *out = malloc(n * 6 + 1);
	char  *o   = out;
	for (size_t i = 0; i < n; ++i) {
		unsigned char c = (unsigned char)name[i];
		if (c == '"' || c == '\\') {
			*o++ = '\\';
			*o++ = (char)c;
		}
		else if (c < 0x20) {
			o += snprintf(o, 7, "\\u%04x", c);
		}
		else {
			*o++ = (char)c;
		}
	}
	*o = '\0';
	return out;
}

// The bytes of file, NULL when it can not be read
static uint8_t *export_glb_read_file(const char *file, size_t *len) {
	FILE *f = fopen(file, "rb");
	if (f == NULL) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = n > 0 ? (uint8_t *)malloc((size_t)n) : NULL;
	if (b != NULL && fread(b, 1, (size_t)n, f) != (size_t)n) {
		free(b);
		b = NULL;
	}
	fclose(f);
	*len = b != NULL ? (size_t)n : 0;
	return b;
}

void export_glb_run_textured(char *path, any_array_t *paint_objects, char *material_name, char **images);

void export_glb_run(char *path, any_array_t *paint_objects) {
	export_glb_run_textured(path, paint_objects, NULL, NULL);
}

// With a material: images (base color, normal, occlusion / roughness / metallic in r g b; png or jpg files, NULL for
// none) embedded in the file, the material on every mesh
void export_glb_run_textured(char *path, any_array_t *paint_objects, char *material_name, char **images) {
	int n = (int)paint_objects->length;

	int      *vcs     = (int *)malloc(n * sizeof(int));
	int      *ics     = (int *)malloc(n * sizeof(int));
	uint32_t *pos_off = (uint32_t *)malloc(n * sizeof(uint32_t));
	uint32_t *nor_off = (uint32_t *)malloc(n * sizeof(uint32_t));
	uint32_t *tex_off = (uint32_t *)malloc(n * sizeof(uint32_t));
	uint32_t *idx_off = (uint32_t *)malloc(n * sizeof(uint32_t));
	float    *pmins   = (float *)malloc(n * 3 * sizeof(float));
	float    *pmaxs   = (float *)malloc(n * 3 * sizeof(float));

	// binary buffer: per object [positions][normals][texcoords][indices]
	size_t   bin_cap = 64 * 1024;
	size_t   bin_len = 0;
	uint8_t *bin     = (uint8_t *)malloc(bin_cap);

	for (int oi = 0; oi < n; oi++) {
		mesh_object_t *p    = (mesh_object_t *)paint_objects->buffer[oi];
		mesh_data_t   *mesh = p->data;
		float          inv  = 1.0f / 32767.0f;
		float          sc   = mesh->scale_pos * inv;
		i16_array_t   *posa = mesh->vertex_arrays->buffer[0]->values;
		i16_array_t   *nora = mesh->vertex_arrays->buffer[1]->values;
		i16_array_t   *texa = mesh->vertex_arrays->buffer[2]->values;
		int            vc   = (int)(posa->length / 4);
		u32_array_t   *inda = mesh->index_array;
		int            ic   = (int)inda->length;

		vcs[oi] = vc;
		ics[oi] = ic;

		// Positions (float32, VEC3), z-up to y-up
		pos_off[oi]       = (uint32_t)bin_len;
		pmins[oi * 3 + 0] = pmins[oi * 3 + 1] = pmins[oi * 3 + 2] = 1e30f;
		pmaxs[oi * 3 + 0] = pmaxs[oi * 3 + 1] = pmaxs[oi * 3 + 2] = -1e30f;
		for (int i = 0; i < vc; i++) {
			float x = posa->buffer[i * 4 + 0] * sc;
			float y = posa->buffer[i * 4 + 2] * sc;
			float z = -posa->buffer[i * 4 + 1] * sc;
			if (x < pmins[oi * 3 + 0])
				pmins[oi * 3 + 0] = x;
			if (y < pmins[oi * 3 + 1])
				pmins[oi * 3 + 1] = y;
			if (z < pmins[oi * 3 + 2])
				pmins[oi * 3 + 2] = z;
			if (x > pmaxs[oi * 3 + 0])
				pmaxs[oi * 3 + 0] = x;
			if (y > pmaxs[oi * 3 + 1])
				pmaxs[oi * 3 + 1] = y;
			if (z > pmaxs[oi * 3 + 2])
				pmaxs[oi * 3 + 2] = z;
			if (bin_len + 12 > bin_cap) {
				bin_cap *= 2;
				bin = (uint8_t *)realloc(bin, bin_cap);
			}
			memcpy(bin + bin_len, &x, 4);
			bin_len += 4;
			memcpy(bin + bin_len, &y, 4);
			bin_len += 4;
			memcpy(bin + bin_len, &z, 4);
			bin_len += 4;
		}

		// Normals (float32, VEC3), nz packed into posa[i*4+3]
		nor_off[oi] = (uint32_t)bin_len;
		for (int i = 0; i < vc; i++) {
			float x = nora->buffer[i * 2 + 0] * inv;
			float y = posa->buffer[i * 4 + 3] * inv;
			float z = -nora->buffer[i * 2 + 1] * inv;
			if (bin_len + 12 > bin_cap) {
				bin_cap *= 2;
				bin = (uint8_t *)realloc(bin, bin_cap);
			}
			memcpy(bin + bin_len, &x, 4);
			bin_len += 4;
			memcpy(bin + bin_len, &y, 4);
			bin_len += 4;
			memcpy(bin + bin_len, &z, 4);
			bin_len += 4;
		}

		// Texcoords (float32, VEC2)
		tex_off[oi] = (uint32_t)bin_len;
		for (int i = 0; i < vc; i++) {
			float u = texa->buffer[i * 2 + 0] * inv;
			float v = texa->buffer[i * 2 + 1] * inv;
			if (bin_len + 8 > bin_cap) {
				bin_cap *= 2;
				bin = (uint8_t *)realloc(bin, bin_cap);
			}
			memcpy(bin + bin_len, &u, 4);
			bin_len += 4;
			memcpy(bin + bin_len, &v, 4);
			bin_len += 4;
		}

		// Indices (uint32)
		idx_off[oi] = (uint32_t)bin_len;
		for (int i = 0; i < ic; i++) {
			uint32_t idx = inda->buffer[i];
			if (bin_len + 4 > bin_cap) {
				bin_cap *= 2;
				bin = (uint8_t *)realloc(bin, bin_cap);
			}
			memcpy(bin + bin_len, &idx, 4);
			bin_len += 4;
		}
	}

	// Pad to 4-byte boundary with zeros
	while (bin_len % 4 != 0) {
		if (bin_len >= bin_cap) {
			bin_cap *= 2;
			bin = (uint8_t *)realloc(bin, bin_cap);
		}
		bin[bin_len++] = 0;
	}

	// Images, each 4-byte aligned after the meshes
	uint32_t    img_off[3]   = {0, 0, 0};
	uint32_t    img_len[3]   = {0, 0, 0};
	const char *img_mime[3]  = {NULL, NULL, NULL};
	int         img_index[3] = {-1, -1, -1}; // In images and textures
	int         nimages      = 0;
	for (int k = 0; material_name != NULL && k < 3; k++) {
		if (images[k] == NULL) {
			continue;
		}
		size_t   len;
		uint8_t *data = export_glb_read_file(images[k], &len);
		if (data == NULL) {
			continue;
		}
		while (bin_len + len + 3 > bin_cap) {
			bin_cap *= 2;
		}
		bin = (uint8_t *)realloc(bin, bin_cap);
		img_off[k] = (uint32_t)bin_len;
		img_len[k] = (uint32_t)len;
		memcpy(bin + bin_len, data, len);
		bin_len += len;
		while (bin_len % 4 != 0) {
			bin[bin_len++] = 0;
		}
		free(data);
		size_t pl   = strlen(images[k]);
		img_mime[k] = pl >= 4 && (strcmp(images[k] + pl - 4, ".jpg") == 0 || strcmp(images[k] + pl - 4, ".JPG") == 0) ? "image/jpeg" : "image/png";
		img_index[k] = nimages++;
	}

	// Build JSON string
	json_buf_t j = {(char *)malloc(4096), 0, 4096};
	json_buf_append(&j, "{\"asset\":{\"generator\":\"Material Studio\",\"version\":\"2.0\"},\"scene\":0,");

	// scenes
	// With a material (Godot): the scene named after the file. Godot names the root after the file and a mesh resource
	// <file>_<mesh>: a single object is Mesh / mesh there, not the name of the object it was imported as (Plane.001)
	bool  generic    = material_name != NULL && n == 1;
	char *scene_name = json_escape(material_name != NULL ? material_name : "Scene");
	json_buf_append(&j, "\"scenes\":[{\"name\":\"%s\",\"nodes\":[", scene_name);
	for (int i = 0; i < n; i++) {
		if (i > 0)
			json_buf_append(&j, ",");
		json_buf_append(&j, "%d", i);
	}
	json_buf_append(&j, "]}],");

	// nodes
	json_buf_append(&j, "\"nodes\":[");
	for (int i = 0; i < n; i++) {
		if (i > 0)
			json_buf_append(&j, ",");
		mesh_object_t *p = (mesh_object_t *)paint_objects->buffer[i];
		char          *name = json_escape(generic ? "Mesh" : p->base->name);
		json_buf_append(&j, "{\"mesh\":%d,\"name\":\"%s\"}", i, name);
		free(name);
	}
	json_buf_append(&j, "],");

	// meshes
	json_buf_append(&j, "\"meshes\":[");
	for (int i = 0; i < n; i++) {
		if (i > 0)
			json_buf_append(&j, ",");
		mesh_object_t *p       = (mesh_object_t *)paint_objects->buffer[i];
		int            base_ac = i * 4;
		char          *name    = json_escape(generic ? "mesh" : p->base->name);
		json_buf_append(&j, "{\"name\":\"%s\",\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,\"TEXCOORD_0\":%d},\"indices\":%d%s}]}",
		                name, base_ac, base_ac + 1, base_ac + 2, base_ac + 3, material_name != NULL ? ",\"material\":0" : "");
		free(name);
	}
	json_buf_append(&j, "],");

	// accessors
	json_buf_append(&j, "\"accessors\":[");
	for (int i = 0; i < n; i++) {
		if (i > 0)
			json_buf_append(&j, ",");
		int bv = i * 4;
		int vc = vcs[i];
		int ic = ics[i];
		// position
		json_buf_append(&j,
		                "{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\","
		                "\"min\":[%.7g,%.7g,%.7g],\"max\":[%.7g,%.7g,%.7g]}",
		                bv, vc, pmins[i * 3 + 0], pmins[i * 3 + 1], pmins[i * 3 + 2], pmaxs[i * 3 + 0], pmaxs[i * 3 + 1], pmaxs[i * 3 + 2]);
		// normal
		json_buf_append(&j, ",{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\"}", bv + 1, vc);
		// texcoord
		json_buf_append(&j, ",{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC2\"}", bv + 2, vc);
		// indices
		json_buf_append(&j, ",{\"bufferView\":%d,\"componentType\":5125,\"count\":%d,\"type\":\"SCALAR\"}", bv + 3, ic);
	}
	json_buf_append(&j, "],");

	// bufferViews
	json_buf_append(&j, "\"bufferViews\":[");
	for (int i = 0; i < n; i++) {
		if (i > 0)
			json_buf_append(&j, ",");
		int vc = vcs[i];
		int ic = ics[i];
		json_buf_append(&j, "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", pos_off[i], (uint32_t)(vc * 12));
		json_buf_append(&j, ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", nor_off[i], (uint32_t)(vc * 12));
		json_buf_append(&j, ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", tex_off[i], (uint32_t)(vc * 8));
		json_buf_append(&j, ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", idx_off[i], (uint32_t)(ic * 4));
	}
	for (int k = 0; k < 3; k++) { // Images' views follow the meshes'
		if (img_index[k] >= 0) {
			json_buf_append(&j, ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", img_off[k], img_len[k]);
		}
	}
	json_buf_append(&j, "],");

	// The material, a texture per image
	if (material_name != NULL) {
		char *name = json_escape(material_name);
		json_buf_append(&j, "\"materials\":[{\"name\":\"%s\",\"pbrMetallicRoughness\":{", name);
		free(name);
		bool comma = false;
		if (img_index[0] >= 0) {
			json_buf_append(&j, "\"baseColorTexture\":{\"index\":%d}", img_index[0]);
			comma = true;
		}
		if (img_index[2] >= 0) {
			json_buf_append(&j, "%s\"metallicRoughnessTexture\":{\"index\":%d}", comma ? "," : "", img_index[2]);
		}
		json_buf_append(&j, "}");
		if (img_index[1] >= 0) {
			json_buf_append(&j, ",\"normalTexture\":{\"index\":%d}", img_index[1]);
		}
		if (img_index[2] >= 0) {
			json_buf_append(&j, ",\"occlusionTexture\":{\"index\":%d}", img_index[2]);
		}
		json_buf_append(&j, "}],");
		if (nimages > 0) {
			json_buf_append(&j, "\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}],");
			json_buf_append(&j, "\"textures\":[");
			for (int t = 0; t < nimages; t++) {
				json_buf_append(&j, "%s{\"sampler\":0,\"source\":%d}", t > 0 ? "," : "", t);
			}
			json_buf_append(&j, "],\"images\":[");
			int         view     = n * 4;
			const char *names[3] = {"base", "nor", "orm"}; // Godot extracts them as <file>_<name>
			for (int k = 0; k < 3; k++) {
				if (img_index[k] >= 0) {
					json_buf_append(&j, "%s{\"name\":\"%s\",\"bufferView\":%d,\"mimeType\":\"%s\"}", img_index[k] > 0 ? "," : "", names[k], view++, img_mime[k]);
				}
			}
			json_buf_append(&j, "],");
		}
	}

	// buffers
	json_buf_append(&j, "\"buffers\":[{\"byteLength\":%u}]}", (uint32_t)bin_len);

	// Pad to 4-byte boundary with spaces
	while (j.len % 4 != 0) {
		if (j.len >= j.cap) {
			j.cap *= 2;
			j.buf = (char *)realloc(j.buf, j.cap);
		}
		j.buf[j.len++] = ' ';
	}

	// Write glb
	uint32_t json_chunk_len = (uint32_t)j.len;
	uint32_t bin_chunk_len  = (uint32_t)bin_len;
	uint32_t total_len      = 12 + 8 + json_chunk_len + 8 + bin_chunk_len;

	char out_path[4096];
	int  plen = (int)strlen(path);
	if (plen >= 4 && strcmp(path + plen - 4, ".glb") == 0) {
		snprintf(out_path, sizeof(out_path), "%s", path);
	}
	else {
		snprintf(out_path, sizeof(out_path), "%s.glb", path);
	}

	FILE *f = fopen(out_path, "wb");
	if (f != NULL) {
		uint32_t magic     = 0x46546C67; // "glTF"
		uint32_t version   = 2;
		uint32_t json_type = 0x4E4F534A; // "JSON"
		uint32_t bin_type  = 0x004E4942; // "BIN\0"
		fwrite(&magic, 4, 1, f);
		fwrite(&version, 4, 1, f);
		fwrite(&total_len, 4, 1, f);
		fwrite(&json_chunk_len, 4, 1, f);
		fwrite(&json_type, 4, 1, f);
		fwrite(j.buf, 1, j.len, f);
		fwrite(&bin_chunk_len, 4, 1, f);
		fwrite(&bin_type, 4, 1, f);
		fwrite(bin, 1, bin_len, f);
		fclose(f);
	}

	free(vcs);
	free(ics);
	free(pos_off);
	free(nor_off);
	free(tex_off);
	free(idx_off);
	free(pmins);
	free(pmaxs);
	free(bin);
	free(j.buf);
	free(scene_name);
}
