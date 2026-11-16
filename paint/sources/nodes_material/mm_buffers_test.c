#include "../global.h"
#include "../material_maker/mm.h"
#include <iron_profile.h>

// Material Maker buffers (mm_buffers.c): hooks for tests/mm_buffers.c and the scripts (minic_api_list.h). Included after
// mm_buffers_render.c.

// Tests: renders a library material (its id in library.pack) or a .ptex file, reads back its maps

static float *mm_buffers_test_maps[MM_MAP_COUNT];
static i32    mm_buffers_test_sizes[MM_MAP_COUNT];
static char  *mm_buffers_test_links[MM_MAP_COUNT];
static bool   mm_buffers_test_grey[MM_MAP_COUNT];

// RGBA floats, greyscale as (r, r, r, 1) as the checker reads maps
static float *mm_buffers_read(gpu_texture_t *t, bool greyscale) {
	i32      n     = t->width * t->height;
	i32      fsize = gpu_texture_format_size(t->format);
	uint8_t *raw   = malloc((size_t)n * fsize);
	gpu_get_render_target_pixels(t, raw);
	float *out = malloc(sizeof(float) * 4 * n);
	for (i32 i = 0; i < n; ++i) {
		float v[4] = {0, 0, 0, 1};
		if (t->format == GPU_TEXTURE_FORMAT_R16) {
			v[0] = half_to_float(((uint16_t *)raw)[i]);
		}
		else if (t->format == GPU_TEXTURE_FORMAT_R32) {
			v[0] = ((float *)raw)[i];
		}
		else if (t->format == GPU_TEXTURE_FORMAT_RGBA64) {
			for (i32 c = 0; c < 4; ++c) {
				v[c] = half_to_float(((uint16_t *)raw)[i * 4 + c]);
			}
		}
		else if (t->format == GPU_TEXTURE_FORMAT_RGBA128) {
			for (i32 c = 0; c < 4; ++c) {
				v[c] = ((float *)raw)[i * 4 + c];
			}
		}
		else {
			for (i32 c = 0; c < 4; ++c) {
				v[c] = raw[i * fsize + c] / 255.0f;
			}
		}
		if (greyscale) {
			v[1] = v[2] = v[0];
			v[3]        = 1.0f;
		}
		memcpy(&out[i * 4], v, sizeof(v));
	}
	free(raw);
	return out;
}

static void mm_buffers_test_set_map(i32 k, gpu_texture_t *t) {
	free(mm_buffers_test_maps[k]);
	mm_buffers_test_maps[k]  = t != NULL ? mm_buffers_read(t, mm_buffers_test_grey[k]) : NULL;
	mm_buffers_test_sizes[k] = t != NULL ? t->width : 0;
}

static mm_pack_t *mm_buffers_library(void) {
	static mm_pack_t *library = NULL;
	if (library == NULL) {
		buffer_t *blob = data_get_blob("material_maker/library.pack"); // Kept: the pack points into it
		library        = blob != NULL ? mm_pack_open(blob->buffer, blob->length) : NULL;
	}
	return library;
}

// Returns a summary, "error: ..." on failure. size: log2 of the maps' resolution, 0 for the material's own. other: the
// last render's maps and links stay (a render that evicts them).
// A texture read back (any format) and written as an 8-bit PNG, values clamped to 0..1
static void mm_buffers_dump_png(gpu_texture_t *t, bool greyscale, char *path) {
	float    *v = mm_buffers_read(t, greyscale);
	i32       n = t->width * t->height;
	buffer_t *b = buffer_create(n * 4);
	for (i32 i = 0; i < n * 4; ++i) {
		float f      = v[i];
		f            = f != f ? 0.0f : f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
		b->buffer[i] = (uint8_t)(f * 255.0f + 0.5f);
	}
#ifdef IRON_BGRA
	for (i32 i = 0; i < n; ++i) {
		uint8_t c            = b->buffer[i * 4];
		b->buffer[i * 4]     = b->buffer[i * 4 + 2];
		b->buffer[i * 4 + 2] = c;
	}
#endif
	iron_write_png(path, b, t->width, t->height, 0);
	iron_delete_blob(b);
	free(v);
}

static char *mm_buffers_test_run(char *path_or_id, i32 size, bool other) {
	mm_nodes_init();
	mm_buffers_init();
	for (i32 m = 0; !other && m < MM_MAP_COUNT; ++m) {
		mm_buffers_test_grey[m] = false;
		mm_buffers_test_set_map(m, NULL);
		free(mm_buffers_test_links[m]);
		mm_buffers_test_links[m] = NULL;
	}
	char  *ptex = NULL;
	size_t len  = 0;
	if (ends_with(path_or_id, ".ptex")) {
		ptex = (char *)mm_buffers_read_file(path_or_id, &len);
	}
	else {
		mm_pack_t *library = mm_buffers_library();
		i32        id      = atoi(path_or_id);
		for (i32 i = 0; library != NULL && i < mm_pack_count(library); ++i) {
			if ((i32)mm_json_num(mm_json_get(mm_pack_entry(library, i), "id"), -1) == id) {
				ptex = mm_pack_read(library, i, &len);
				break;
			}
		}
	}
	if (ptex == NULL) {
		return string("error: %s not found", path_or_id);
	}
	mm_mark_t      mark = mm_arena_mark();
	mm_material_t *m    = mm_load(ptex, len);
	free(ptex);
	if (m == NULL || m->error != NULL) {
		char *e = string("error: %s", m != NULL ? m->error : "load");
		mm_arena_release(mark);
		return e;
	}
	mm_plan_t *plan = mm_plan(m, size);
	if (plan->error != NULL) {
		char *e = string("error: %s", plan->error);
		mm_arena_release(mark);
		return e;
	}
	f64 t0            = iron_time();
	mm_buffers_forced = true; // Rendered full now, as the checker's maps are
	gpu_texture_t **tex = mm_buffers_render(plan);
	mm_buffers_forced   = false;
	f64 ms              = (iron_time() - t0) * 1000.0;
	i32 need            = mm_buffers_require(MM_BUFFERS_FULL); // Links resolve to full textures
	// Every texture is found again by its link, through the app's texture link resolution
	i32 links = 0, bad_links = 0;
	for (i32 i = 0; i < plan->count; ++i) {
		char *link = mm_buffers_link(plan, i);
		if (link == NULL || tex[i] == NULL) {
			continue;
		}
		links++;
		bad_links += uniforms_ext_tex_link(NULL, NULL, link) != tex[i];
	}
	i32 maps = 0;
	for (i32 k = 0; !other && k < MM_MAP_COUNT; ++k) {
		i32 pi = plan->map_pass[k];
		if (pi < 0 || tex[pi] == NULL) {
			continue;
		}
		mm_buffers_test_grey[k]  = plan->passes[pi].greyscale;
		mm_buffers_test_links[k] = strdup(mm_buffers_link(plan, pi));
		mm_buffers_test_set_map(k, tex[pi]);
		maps++;
	}
	i64 kb = 0; // The plan's textures
	for (i32 i = 0; i < plan->count; ++i) {
		kb += tex[i] != NULL ? mm_buffers_bytes(tex[i]) / 1024 : 0;
	}
	char *s = string("passes %d rendered %d cached %d failed %d draws %d maps %d links %d bad_links %d kb %d ms %.1f", plan->count, mm_buffers_stats[0],
	                 mm_buffers_stats[1], mm_buffers_stats[2], mm_buffers_stats[3], maps, links, bad_links, (i32)kb, ms);
	if (getenv("MM_BUFFERS_VERBOSE") != NULL) {
		for (i32 i = 0; i < plan->count; ++i) {
			mm_pass_t *p = &plan->passes[i];
			iron_log("mm_buffers pass %d %s: size %d%s%s%s%s loop_of %d textures %d", i, p->name, p->size, p->greyscale ? " grey" : "", p->f32 ? " f32" : "",
			         p->iterate ? " iterate" : "", p->texture_gen != NULL ? " cpu" : "", p->loop_of, p->textures);
			if (getenv("MM_BUFFERS_DUMP") != NULL && tex[i] != NULL) { // Each pass's texture as a PNG in the save path
				mm_buffers_dump_png(tex[i], p->greyscale, string_tmp("%s/mm_pass_%02d.png", script_save_path(), i));
			}
		}
	}
	mm_buffers_require(need);
	mm_arena_release(mark);
	return s;
}

char *mm_buffers_test_ptex(char *path_or_id, i32 size) {
	return mm_buffers_test_run(path_or_id, size, false);
}

char *mm_buffers_test_other(char *path_or_id, i32 size) {
	return mm_buffers_test_run(path_or_id, size, true);
}

// The last test render's map links: 1 when its texture is in memory, 0 when evicted (its recipe kept), -1 when gone
i32 mm_buffers_test_resident(i32 map) {
	if (map < 0 || map >= MM_MAP_COUNT || mm_buffers_test_links[map] == NULL) {
		return -1;
	}
	mm_buffers_recipe_t *r = mm_buffers_find(strtoull(mm_buffers_test_links[map] + strlen(MM_BUFFERS_LINK), NULL, 16));
	return r == NULL ? -1 : r->tex != NULL ? 1 : 0;
}

// Resolves the last test render's map links with uniforms_ext_tex_link (rendering evicted ones again) and reads them
// back. in_pass: resolved inside a render pass, as a material draw does; returns -1 when that pass lost its content.
// Returns the maps read.
i32 mm_buffers_test_resolve(i32 in_pass) {
	gpu_texture_t *outer = NULL;
	u32            color = 0xff336699;
	if (in_pass) {
		outer = calloc(1, sizeof(gpu_texture_t));
		gpu_render_target_init(outer, 8, 8, GPU_TEXTURE_FORMAT_RGBA64);
		gpu_texture_t *targets[1] = {outer};
		gpu_begin(targets, 1, NULL, GPU_CLEAR_COLOR, color, 0.0f);
	}
	gpu_texture_t *got[MM_MAP_COUNT] = {0};
	i32            need              = mm_buffers_require(MM_BUFFERS_FULL); // Rendered full now, as for a bake
	for (i32 k = 0; k < MM_MAP_COUNT; ++k) {
		got[k] = mm_buffers_test_links[k] != NULL ? uniforms_ext_tex_link(NULL, NULL, mm_buffers_test_links[k]) : NULL;
	}
	mm_buffers_require(need);
	i32 n = 0;
	if (in_pass) {
		gpu_end();
		float  r, g, b, a;
		float *px = mm_buffers_read(outer, false);
		iron_color_components(color, &r, &g, &b, &a);
		bool kept = fabsf(px[0] - r) < 0.01f && fabsf(px[1] - g) < 0.01f && fabsf(px[2] - b) < 0.01f && fabsf(px[3] - a) < 0.01f;
		free(px);
		mm_buffers_texture_free(outer);
		if (!kept) {
			return -1;
		}
	}
	for (i32 k = 0; k < MM_MAP_COUNT; ++k) {
		if (mm_buffers_test_links[k] != NULL) {
			mm_buffers_test_set_map(k, got[k]);
			n += got[k] != NULL;
		}
	}
	return n;
}

// The resolution of a map of the last test render, 0 when the material has none (mm_map_t order: albedo, orm,
// emission, normal, height, sss)
i32 mm_buffers_test_map_size(i32 map) {
	return map >= 0 && map < MM_MAP_COUNT ? mm_buffers_test_sizes[map] : 0;
}

f32 mm_buffers_test_texel(i32 map, i32 x, i32 y, i32 channel) {
	if (mm_buffers_test_map_size(map) == 0) {
		return -1.0f;
	}
	i32 s = mm_buffers_test_sizes[map];
	x     = x < 0 ? 0 : x >= s ? s - 1 : x;
	y     = y < 0 ? 0 : y >= s ? s - 1 : y;
	return mm_buffers_test_maps[map][(y * s + x) * 4 + (channel & 3)];
}

// Writes the maps as <prefix>_albedo.png, ... (8 bits, clamped, as the checker writes them)
void mm_buffers_test_write(char *prefix) {
	static const char *names[MM_MAP_COUNT] = {"albedo", "orm", "emission", "normal", "heightmap", "sss"};
	for (i32 k = 0; k < MM_MAP_COUNT; ++k) {
		i32 s = mm_buffers_test_sizes[k];
		if (s == 0) {
			continue;
		}
		buffer_t *b = buffer_create(s * s * 4);
		for (i32 i = 0; i < s * s * 4; ++i) {
			float v      = mm_buffers_test_maps[k][i];
			v            = v != v ? 0.0f : v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
			b->buffer[i] = (uint8_t)(v * 255.0f + 0.5f);
		}
#ifdef IRON_BGRA // iron_write_png swaps red and blue there
		for (i32 i = 0; i < s * s; ++i) {
			uint8_t c            = b->buffer[i * 4];
			b->buffer[i * 4]     = b->buffer[i * 4 + 2];
			b->buffer[i * 4 + 2] = c;
		}
#endif
		iron_write_png(string_tmp("%s_%s.png", prefix, names[k]), b, s, s, 0);
		iron_delete_blob(b);
	}
}

// The export cap (mm_buffers_export_begin) over the last test render's maps: renders them full at the cap chosen for
// max_size and budget_kb, returns the cap
i32 mm_buffers_test_export(i32 max_size, i32 budget_kb) {
	u64 keys[MM_MAP_COUNT];
	i32 n = 0;
	for (i32 k = 0; k < MM_MAP_COUNT; ++k) {
		if (mm_buffers_test_links[k] != NULL) {
			keys[n++] = strtoull(mm_buffers_test_links[k] + strlen(MM_BUFFERS_LINK), NULL, 16);
		}
	}
	i32 cap = mm_buffers_export_begin(keys, n, max_size, (i64)budget_kb * 1024);
	while (mm_buffers_export_step() > 0) {
	}
	return cap;
}

// The size of the texture a map of the last test render has now, 0 for none
i32 mm_buffers_test_tex_size(i32 map) {
	if (map < 0 || map >= MM_MAP_COUNT || mm_buffers_test_links[map] == NULL) {
		return 0;
	}
	mm_buffers_recipe_t *r = mm_buffers_find(strtoull(mm_buffers_test_links[map] + strlen(MM_BUFFERS_LINK), NULL, 16));
	return r != NULL && r->tex != NULL ? r->tex_size : 0;
}
