#include "../global.h"

// Binary and ASCII STL, one flat shaded mesh without uvs

extern string_array_t *_path_mesh_formats;

static char *import_stl_name(char *path) {
	i32 a = string_last_index_of(path, "/");
	i32 b = string_last_index_of(path, "\\");
	i32 s = (a > b ? a : b) + 1;
	i32 e = string_last_index_of(path, ".");
	if (e <= s) {
		e = string_length(path);
	}
	return substring(path, s, e);
}

static bool import_stl_is_ascii(buffer_t *b) {
	if (b->length < 84) {
		return true;
	}
	// Binary files may also start with "solid", trust the face count when it matches the size
	u32 faces = buffer_get_i32(b, 80);
	if ((u64)faces * 50 + 84 == b->length) {
		return false;
	}
	return strncmp((char *)b->buffer, "solid", 5) == 0;
}

// Positions are 9 floats and normals 3 floats per face
static i32 import_stl_read_binary(buffer_t *b, f32_array_t *pos, f32_array_t *nor) {
	i32 faces = buffer_get_i32(b, 80);
	if (faces <= 0 || (u64)faces * 50 + 84 > b->length) {
		return 0;
	}
	i32 p = 84;
	for (i32 i = 0; i < faces; ++i) {
		for (i32 k = 0; k < 3; ++k) {
			f32_array_push(nor, buffer_get_f32(b, p));
			p += 4;
		}
		for (i32 k = 0; k < 9; ++k) {
			f32_array_push(pos, buffer_get_f32(b, p));
			p += 4;
		}
		p += 2; // Attribute
	}
	return faces;
}

static i32 import_stl_read_ascii(buffer_t *b, f32_array_t *pos, f32_array_t *nor) {
	char *s     = (char *)b->buffer;
	char *end   = s + b->length;
	i32   verts = 0;
	while (s < end) {
		while (s < end && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
			s++;
		}
		if (end - s > 12 && strncmp(s, "facet normal", 12) == 0) {
			s += 12;
			for (i32 k = 0; k < 3; ++k) {
				f32_array_push(nor, strtof(s, &s));
			}
		}
		else if (end - s > 6 && strncmp(s, "vertex", 6) == 0) {
			s += 6;
			for (i32 k = 0; k < 3; ++k) {
				f32_array_push(pos, strtof(s, &s));
			}
			verts++;
		}
		while (s < end && *s != '\n') {
			s++;
		}
	}
	i32 faces = verts / 3;
	pos->length = faces * 9;
	while (nor->length < faces * 3) {
		f32_array_push(nor, 0.0);
	}
	return faces;
}

raw_mesh_t *import_stl_run_raw(char *path) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		return NULL;
	}
	f32_array_t *pos   = f32_array_create(0);
	f32_array_t *nor   = f32_array_create(0);
	i32          faces = import_stl_is_ascii(b) ? import_stl_read_ascii(b, pos, nor) : import_stl_read_binary(b, pos, nor);
	data_delete_blob(path);
	if (faces == 0) {
		return NULL;
	}

	// Facet normals are often missing, compute them from the winding
	for (i32 i = 0; i < faces; ++i) {
		f32 *n = nor->buffer + i * 3;
		f32 *v = pos->buffer + i * 9;
		f32  l = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
		if (l < 0.5) {
			vec4_t e1 = {v[3] - v[0], v[4] - v[1], v[5] - v[2], 0.0};
			vec4_t e2 = {v[6] - v[0], v[7] - v[1], v[8] - v[2], 0.0};
			vec4_t c  = vec4_norm(vec4_cross(e1, e2));
			n[0] = c.x, n[1] = c.y, n[2] = c.z;
		}
	}

	f32 scale_pos = 0.0;
	for (i32 i = 0; i < pos->length; ++i) {
		f32 f = math_abs(pos->buffer[i]);
		if (scale_pos < f) {
			scale_pos = f;
		}
	}
	if (scale_pos == 0.0) {
		scale_pos = 1.0;
	}
	f32 inv = 32767.0 * (1.0 / scale_pos);

	i32          verts = faces * 3;
	i16_array_t *posa  = i16_array_create(verts * 4);
	i16_array_t *nora  = i16_array_create(verts * 2);
	u32_array_t *inda  = u32_array_create(verts);
	for (i32 i = 0; i < verts; ++i) {
		i32 f                   = (i / 3) * 3; // Face normal
		posa->buffer[i * 4]     = (i16)(pos->buffer[i * 3] * inv);
		posa->buffer[i * 4 + 1] = (i16)(-pos->buffer[i * 3 + 2] * inv);
		posa->buffer[i * 4 + 2] = (i16)(pos->buffer[i * 3 + 1] * inv);
		posa->buffer[i * 4 + 3] = (i16)(nor->buffer[f + 1] * 32767.0);
		nora->buffer[i * 2]     = (i16)(nor->buffer[f] * 32767.0);
		nora->buffer[i * 2 + 1] = (i16)(-nor->buffer[f + 2] * 32767.0);
		inda->buffer[i]         = i;
	}
	array_free(pos);
	array_free(nor);
	return script_make_raw_mesh(import_stl_name(path), posa, nora, inda, scale_pos);
}

void import_stl_init() {
	path_mesh_formats(); // Init array
	any_map_set(import_mesh_importers, "stl", import_stl_run_raw);
	any_array_push((any_array_t *)_path_mesh_formats, "stl");
}
