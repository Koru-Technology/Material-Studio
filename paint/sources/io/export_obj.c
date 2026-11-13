
#include "../global.h"

static void export_obj_reserve(u8_array_t *out, u32 size) {
	if (out->length + size > out->capacity) {
		u32 cap = out->capacity < 1024 ? 1024 : out->capacity * 2;
		while (cap < out->length + size) {
			cap *= 2;
		}
		out->buffer   = realloc(out->buffer, cap);
		out->capacity = cap;
	}
}

static void export_obj_write_string(u8_array_t *out, char *str) {
	u32 len = strlen(str);
	export_obj_reserve(out, len);
	memcpy(out->buffer + out->length, str, len);
	out->length += len;
}

static void export_obj_write_char(u8_array_t *out, char c) {
	export_obj_reserve(out, 1);
	out->buffer[out->length++] = c;
}

static void export_obj_write_i32(u8_array_t *out, i32 i) {
	char s[16];
	i32  l = 0;
	u32  u = i < 0 ? -(u32)i : (u32)i;
	do {
		s[l++] = '0' + u % 10;
		u /= 10;
	} while (u > 0);
	export_obj_reserve(out, l + 1);
	if (i < 0) {
		out->buffer[out->length++] = '-';
	}
	while (l > 0) {
		out->buffer[out->length++] = s[--l];
	}
}

static void export_obj_write_f32(u8_array_t *out, f32 f) {
	char s[64];
	i32  l = snprintf(s, sizeof(s), "%f", f);
	if (l <= 0 || l >= (i32)sizeof(s)) {
		return;
	}
	while (l > 1 && s[l - 1] == '0') {
		l--;
	}
	if (s[l - 1] == '.') {
		l--;
	}
	export_obj_reserve(out, l);
	memcpy(out->buffer + out->length, s, l);
	out->length += l;
}

static void export_obj_write_vec(u8_array_t *out, char *prefix, f32 x, f32 y, f32 z, bool has_z) {
	export_obj_write_string(out, prefix);
	export_obj_write_f32(out, x);
	export_obj_write_char(out, ' ');
	export_obj_write_f32(out, y);
	if (has_z) {
		export_obj_write_char(out, ' ');
		export_obj_write_f32(out, z);
	}
	export_obj_write_char(out, '\n');
}

static void export_obj_write_face(u8_array_t *out, i32 *p, i32 *t, i32 *n) {
	export_obj_write_char(out, 'f');
	for (i32 k = 0; k < 3; ++k) {
		export_obj_write_char(out, ' ');
		export_obj_write_i32(out, p[k]);
		export_obj_write_char(out, '/');
		export_obj_write_i32(out, t[k]);
		export_obj_write_char(out, '/');
		export_obj_write_i32(out, n[k]);
	}
	export_obj_write_char(out, '\n');
}

static u64 export_obj_key(i16 a, i16 b, i16 c) {
	return (u64)(u16)a | ((u64)(u16)b << 16) | ((u64)(u16)c << 32);
}

static i32 export_obj_dedupe(u64 *keys, i32 len, i32 *map, i32 *first) {
	u32 cap = 16;
	while (cap < (u32)len * 2) {
		cap *= 2;
	}
	i32 *table = calloc(cap, sizeof(i32)); // Unique index + 1, 0 is empty
	i32  count = 0;
	for (i32 i = 0; i < len; ++i) {
		u64 k = keys[i];
		u32 s = (u32)((k * 0x9E3779B97F4A7C15ull) >> 32) & (cap - 1);
		while (true) {
			i32 e = table[s];
			if (e == 0) {
				table[s]     = count + 1;
				first[count] = i;
				map[i]       = count++;
				break;
			}
			if (keys[first[e - 1]] == k) {
				map[i] = e - 1;
				break;
			}
			s = (s + 1) & (cap - 1);
		}
	}
	free(table);
	return count;
}

void export_obj_run(char *path, mesh_object_t_array_t *paint_objects) {
	u8_array_t *o = u8_array_create(0);
	export_obj_write_string(o, "# Material Studio\n");

	i32 poff = 0;
	i32 noff = 0;
	i32 toff = 0;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		mesh_object_t *p    = paint_objects->buffer[i];
		mesh_data_t   *mesh = p->data;
		f32            inv  = 1 / 32767.0;
		f32            sc   = p->data->scale_pos * inv;
		i16_array_t   *posa = mesh->vertex_arrays->buffer[0]->values;
		i16_array_t   *nora = mesh->vertex_arrays->buffer[1]->values;
		i16_array_t   *texa = mesh->vertex_arrays->buffer[2]->values;
		i32            len  = math_floor(posa->length / 4.0);
		i32            size = len > 0 ? len : 1;

		// Merge shared vertices and remap indices
		u64 *keys   = malloc(size * sizeof(u64));
		i32 *posmap = malloc(size * sizeof(i32));
		i32 *normap = malloc(size * sizeof(i32));
		i32 *texmap = malloc(size * sizeof(i32));
		i32 *posfst = malloc(size * sizeof(i32));
		i32 *norfst = malloc(size * sizeof(i32));
		i32 *texfst = malloc(size * sizeof(i32));
		for (i32 j = 0; j < len; ++j)
			keys[j] = export_obj_key(posa->buffer[j * 4], posa->buffer[j * 4 + 1], posa->buffer[j * 4 + 2]);
		i32 pi = export_obj_dedupe(keys, len, posmap, posfst);
		for (i32 j = 0; j < len; ++j)
			keys[j] = export_obj_key(nora->buffer[j * 2], nora->buffer[j * 2 + 1], posa->buffer[j * 4 + 3]);
		i32 ni = export_obj_dedupe(keys, len, normap, norfst);
		for (i32 j = 0; j < len; ++j)
			keys[j] = export_obj_key(texa->buffer[j * 2], texa->buffer[j * 2 + 1], 0);
		i32 ti = export_obj_dedupe(keys, len, texmap, texfst);

		export_obj_write_string(o, "o ");
		export_obj_write_string(o, p->base->name);
		export_obj_write_char(o, '\n');
		for (i32 j = 0; j < pi; ++j) {
			i32 v = posfst[j];
			export_obj_write_vec(o, "v ", posa->buffer[v * 4] * sc, posa->buffer[v * 4 + 2] * sc, -posa->buffer[v * 4 + 1] * sc, true);
		}
		for (i32 j = 0; j < ni; ++j) {
			i32 v = norfst[j];
			export_obj_write_vec(o, "vn ", nora->buffer[v * 2] * inv, posa->buffer[v * 4 + 3] * inv, -nora->buffer[v * 2 + 1] * inv, true);
		}
		for (i32 j = 0; j < ti; ++j) {
			i32 v = texfst[j];
			export_obj_write_vec(o, "vt ", texa->buffer[v * 2] * inv, 1.0 - texa->buffer[v * 2 + 1] * inv, 0.0, false);
		}

		u32_array_t *inda = mesh->index_array;
		for (i32 j = 0; j < math_floor(inda->length / 3.0); ++j) {
			i32 pf[3];
			i32 tf[3];
			i32 nf[3];
			for (i32 k = 0; k < 3; ++k) {
				i32 v = inda->buffer[j * 3 + k];
				pf[k] = posmap[v] + 1 + poff;
				tf[k] = texmap[v] + 1 + toff;
				nf[k] = normap[v] + 1 + noff;
			}
			export_obj_write_face(o, pf, tf, nf);
		}
		poff += pi;
		noff += ni;
		toff += ti;

		free(keys);
		free(posmap);
		free(normap);
		free(texmap);
		free(posfst);
		free(norfst);
		free(texfst);
	}

	if (!ends_with(path, ".obj")) {
		path = string_tmp("%s.obj", path);
	}
	iron_file_save_bytes(path, o, 0);
	array_delete(o);
}

