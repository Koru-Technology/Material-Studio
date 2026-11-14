#include "../global.h"
#include "../material_maker/mm_internal.h"
#include <iron_folder.h>
#include <iron_profile.h>


// Material Maker's library in the Library tab: the materials of library.pack (mm_import.c) by style (stylized or
// realistic) and section, searched by name, tags, author and style, with thumbnails from thumbs.pack. A thumbnail missing from the pack is rendered when first shown and
// cached in the save folder. Thumbnails load a few per frame as they are shown, the ones not seen for the longest are
// unloaded past MM_LIBRARY_MAX_ICONS.
//
// thumbs.pack (made by paint/tools/material_maker/mm_thumbnails.c): the pack format of build_library.py, an entry per
// material ("id") holding a JPEG of its preview sphere composited over black, and an entry "mask" holding the sphere's
// coverage (8 bit, MM_THUMB_RES squared), the same for every preview: the alpha of the thumbnails. Blobs are deflate
// "stored" blocks, JPEG does not compress further.

#define MM_THUMB_RES         128
#define MM_THUMB_QUALITY     88
#define MM_LIBRARY_MAX_ICONS 300

typedef struct mm_library_entry {
	i32            id;
	char          *name;
	char          *author;
	char          *license;
	i32            section;
	bool           stylized;
	char          *search; // Name, tags, author and style, lower case
	gpu_texture_t *icon;
	i32            icon_state; // 0 none, 1 requested, 2 ready, 3 failed
	i32            seen;       // Frame it was last drawn
} mm_library_entry_t;

static const char *mm_library_sections[] = {"Bricks & Walls", "Stone & Rock",     "Tiles & Floors", "Metal",    "Ground & Nature", "Wood",
                                            "Food",           "Fabric & Leather", "Sci-Fi & Tech",  "Patterns", "Creatures & Organic"};
#define MM_LIBRARY_SECTIONS ((i32)(sizeof(mm_library_sections) / sizeof(mm_library_sections[0])))

static mm_library_entry_t *mm_library_entries = NULL;
static i32                 mm_library_n       = 0;
static i32                 mm_library_frame   = 0;
static i32                 mm_library_icons   = 0;
static bool                mm_library_update_registered = false;
static mm_pack_t          *mm_thumbs_pack     = NULL;
static bool                mm_thumbs_tried    = false;
static u8                 *mm_thumbs_mask     = NULL; // MM_THUMB_RES squared
static i32                *mm_thumbs_ids      = NULL; // Library id of each pack entry, -1 for others
static i32                 mm_thumbs_n        = 0;

static i32 mm_thumbs_entry(i32 id) {
	for (i32 i = 0; i < mm_thumbs_n; ++i) {
		if (mm_thumbs_ids[i] == id) {
			return i;
		}
	}
	return -1;
}

static char *mm_library_lower(const char *s) {
	char *r = string_copy((char *)s);
	for (char *p = r; *p; ++p) {
		*p = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
	}
	return r;
}

static void mm_library_open() {
	if (mm_library_entries != NULL) {
		return;
	}
	mm_pack_t *p = mm_import_library_open();
	i32        n = p != NULL ? mm_pack_count(p) : 0;
	mm_library_entries = calloc(n + 1, sizeof(mm_library_entry_t));
	for (i32 i = 0; i < n; ++i) {
		mm_json_t          *e = mm_pack_entry(p, i);
		mm_library_entry_t *l = &mm_library_entries[mm_library_n];
		const char         *s = mm_json_str(mm_json_get(e, "section"), "");
		l->section            = -1;
		for (i32 k = 0; k < MM_LIBRARY_SECTIONS; ++k) {
			if (strcmp(s, mm_library_sections[k]) == 0) {
				l->section = k;
			}
		}
		if (l->section < 0) {
			continue;
		}
		l->id      = (i32)mm_json_num(mm_json_get(e, "id"), -1);
		l->name    = string_copy((char *)mm_json_str(mm_json_get(e, "name"), ""));
		l->author  = string_copy((char *)mm_json_str(mm_json_get(e, "author"), ""));
		l->license  = string_copy((char *)mm_json_str(mm_json_get(e, "license"), ""));
		l->stylized = strcmp(mm_json_str(mm_json_get(e, "style"), ""), "stylized") == 0;
		l->search   = mm_library_lower(string_tmp("%s %s %s %s", l->name, mm_json_str(mm_json_get(e, "tags"), ""), l->author,
		                                          l->stylized ? "stylized" : "realistic"));
		mm_library_n++;
	}
}

i32 mm_library_count() {
	mm_library_open();
	return mm_library_n;
}

i32 mm_library_section_count() {
	return MM_LIBRARY_SECTIONS;
}

char *mm_library_section_name(i32 s) {
	return tr((char *)mm_library_sections[s]);
}

i32 mm_library_section(i32 i) {
	return mm_library_entries[i].section;
}

bool mm_library_stylized(i32 i) {
	return mm_library_entries[i].stylized;
}

i32 mm_library_id(i32 i) {
	return mm_library_entries[i].id;
}

char *mm_library_name(i32 i) {
	return mm_library_entries[i].name;
}

// Name, author and licence, for the tooltip
char *mm_library_credit(i32 i) {
	mm_library_entry_t *l = &mm_library_entries[i];
	return string("%s\n%s %s, %s", l->name, tr("by"), l->author, l->license);
}

// search is lower case
bool mm_library_matches(i32 i, char *search) {
	return search[0] == 0 || strstr(mm_library_entries[i].search, search) != NULL;
}

// Index of the entry with that library id, -1 when none
i32 mm_library_find(i32 id) {
	mm_library_open();
	for (i32 i = 0; i < mm_library_n; ++i) {
		if (mm_library_entries[i].id == id) {
			return i;
		}
	}
	return -1;
}

static bool mm_library_thumb_preview(slot_material_t *m, i32 id);

static void mm_library_use_on_next_frame(void *data) {
	iron_profile_mark(string("library tap %d start", (i32)(size_t)data));
	double           t0 = iron_time();
	slot_material_t *m  = mm_import_library((i32)(size_t)data); // Compiles on the next frame
	if (m != NULL) {
		if (mm_library_thumb_preview(m, (i32)(size_t)data)) { // Its preview: the library thumbnail, not rendered again
			import_arm_set_preview_given(m);
		}
		if (getenv("IRON_SHADER_TIMING") != NULL) {
			iron_log("[shader] library tap %d: import %.1f ms", (i32)(size_t)data, (iron_time() - t0) * 1000.0);
		}
		// The Materials tab, where the new material is selected
		if (ui_popovers_enabled()) {
			ui_popovers_show_materials_tab(0);
		}
		else {
			ui_base_show_tab(TAB_MATERIALS);
		}
	}
	iron_profile_mark(string("library tap %d end", (i32)(size_t)data));
}

// Imports the material as a new material of the project and selects it
void mm_library_use(i32 i) {
	sys_notify_on_next_frame(&mm_library_use_on_next_frame, (void *)(size_t)mm_library_entries[i].id);
}

// Thumbnails

static mm_pack_t *mm_thumbs_open() {
	if (!mm_thumbs_tried) {
		mm_thumbs_tried = true;
		buffer_t *blob  = data_get_blob("material_maker/thumbs.pack");
		if (blob != NULL && blob->length > 16) {
			u8 *data = malloc(blob->length); // The pack keeps pointing into it
			memcpy(data, blob->buffer, blob->length);
			mm_thumbs_pack = mm_pack_open(data, blob->length);
		}
		if (mm_thumbs_pack != NULL) {
			mm_thumbs_n   = mm_pack_count(mm_thumbs_pack);
			mm_thumbs_ids = malloc(sizeof(i32) * (mm_thumbs_n + 1));
			for (i32 i = 0; i < mm_thumbs_n; ++i) {
				mm_thumbs_ids[i] = (i32)mm_json_num(mm_json_get(mm_pack_entry(mm_thumbs_pack, i), "id"), -1);
			}
			i32 k = mm_pack_find(mm_thumbs_pack, "name", "mask");
			size_t len = 0;
			u8    *m   = k >= 0 ? (u8 *)mm_pack_read(mm_thumbs_pack, k, &len) : NULL;
			if (m != NULL && len == MM_THUMB_RES * MM_THUMB_RES) {
				mm_thumbs_mask = m;
			}
		}
	}
	return mm_thumbs_pack;
}

static char *mm_thumbs_cache_path(i32 id) {
	return string("%smm_thumbs/%d.jpg", iron_internal_save_path(), id);
}

// RGBA texels (straight alpha) of a JPEG composited over black and the sphere's coverage
static u8 *mm_thumbs_decode(u8 *jpg, size_t size, u8 *mask) {
	int w, h, comp;
	u8 *px = stbi_load_from_memory(jpg, (int)size, &w, &h, &comp, 4);
	if (px == NULL || w != MM_THUMB_RES || h != MM_THUMB_RES) {
		if (px != NULL) {
			stbi_image_free(px);
		}
		return NULL;
	}
	u8 *out = malloc(w * h * 4);
	for (i32 i = 0; i < w * h; ++i) {
		u8 a = mask != NULL ? mask[i] : 255;
		for (i32 c = 0; c < 3; ++c) {
			i32 v          = a > 0 ? px[i * 4 + c] * 255 / a : 0;
			out[i * 4 + c] = (u8)(v > 255 ? 255 : v);
		}
		out[i * 4 + 3] = a;
	}
	stbi_image_free(px);
	return out;
}

static gpu_texture_t *mm_thumbs_texture(u8 *rgba) {
	iron_image_pixels_t px = {.data = rgba, .width = MM_THUMB_RES, .height = MM_THUMB_RES, .format = GPU_TEXTURE_FORMAT_RGBA32};
	return gpu_create_texture_from_pixels(&px); // Frees the pixels
}

// A material added from the library gets its thumbnail (shipped, or cached when rendered on the device) as its preview
// instead of a rendered one: the thumbnail is that same render. It is MM_THUMB_RES squared, upscaled to the preview size.
// False when there is none: the preview is rendered as for any material.
static bool mm_library_thumb_preview(slot_material_t *m, i32 id) {
	u8        *rgba = NULL;
	mm_pack_t *pack = mm_thumbs_open();
	i32        k    = pack != NULL ? mm_thumbs_entry(id) : -1;
	if (k >= 0) {
		size_t len = 0;
		u8    *jpg = (u8 *)mm_pack_read(pack, k, &len);
		rgba       = jpg != NULL && len > 0 ? mm_thumbs_decode(jpg, len, mm_thumbs_mask) : NULL;
		free(jpg);
	}
	if (rgba == NULL) {
		size_t size = 0;
		u8    *jpg  = iron_folder_read(mm_thumbs_cache_path(id), &size);
		if (jpg != NULL) {
			size_t mlen = 0;
			u8    *mask = iron_folder_read(string("%smm_thumbs/mask.raw", iron_internal_save_path()), &mlen);
			rgba        = mm_thumbs_decode(jpg, size, mask != NULL && mlen == MM_THUMB_RES * MM_THUMB_RES ? mask : mm_thumbs_mask);
			free(mask);
			free(jpg);
		}
	}
	if (rgba == NULL) {
		return false;
	}
	gpu_texture_t *t = mm_thumbs_texture(rgba);
	util_render_set_material_preview(m, t);
	gpu_delete_texture(t);
	return true;
}

// The preview sphere of a library material, as the Materials tab renders material icons: RGBA texels (straight alpha),
// MM_THUMB_RES squared, malloc'd. NULL when the material does not import. failures: shader failures while rendering.
u8 *mm_thumbs_render(i32 id, i32 *failures) {
	if (g_project == NULL || g_project->_->materials->length == 0) {
		return NULL;
	}
	i32              before = gpu_shader_failures();
	slot_material_t *m      = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	if (!mm_import_library_into(id, m)) {
		slot_material_unload(m);
		return NULL;
	}
	slot_material_t *saved       = g_context->material;
	bool             height_used = make_material_height_used;
	g_context->material          = m;
	make_material_height_used    = true; // Show the relief in the preview
	i32 need                     = mm_buffers_require(MM_BUFFERS_FULL); // Thumbnails are kept: full Material Maker buffers
	util_render_make_material_preview();
	mm_buffers_require(need);
	make_material_height_used = height_used;
	g_context->material       = saved;

	buffer_t *px = buffer_half_to_u8(gpu_get_texture_pixels(m->image)); // RGBA64 reads back in RGBA order
	i32 w   = m->image->width;
	i32 f   = w / MM_THUMB_RES; // Box filter down to the thumbnail size
	u8 *out = malloc(MM_THUMB_RES * MM_THUMB_RES * 4);
	for (i32 y = 0; y < MM_THUMB_RES; ++y) {
		for (i32 x = 0; x < MM_THUMB_RES; ++x) {
			i32 s[4] = {0, 0, 0, 0};
			for (i32 j = 0; j < f; ++j) {
				for (i32 i = 0; i < f; ++i) {
					u8 *p = &px->buffer[((y * f + j) * w + x * f + i) * 4];
					i32 a = p[3];
					for (i32 c = 0; c < 3; ++c) {
						s[c] += p[c] * a; // Premultiplied while averaging
					}
					s[3] += a;
				}
			}
			u8 *o = &out[(y * MM_THUMB_RES + x) * 4];
			for (i32 c = 0; c < 3; ++c) {
				o[c] = s[3] > 0 ? (u8)(s[c] / s[3]) : 0;
			}
			o[3] = (u8)(s[3] / (f * f));
		}
	}
	slot_material_unload(m);
	g_context->ddirty = 2;
	if (failures != NULL) {
		*failures = gpu_shader_failures() - before;
	}
	return out;
}

// JPEG of the texels composited over black
static buffer_t *mm_thumbs_jpg(u8 *rgba) {
	buffer_t *b = buffer_create(MM_THUMB_RES * MM_THUMB_RES * 4);
	for (i32 i = 0; i < MM_THUMB_RES * MM_THUMB_RES; ++i) {
		for (i32 c = 0; c < 3; ++c) {
			b->buffer[i * 4 + c] = (u8)(rgba[i * 4 + c] * rgba[i * 4 + 3] / 255);
		}
		b->buffer[i * 4 + 3] = 255;
	}
#ifdef IRON_BGRA
	b = buffer_bgra_swap(b); // The encoder takes the order 8 bit textures read back in
#endif
	buffer_t *jpg = iron_encode_jpg(b, MM_THUMB_RES, MM_THUMB_RES, 0, MM_THUMB_QUALITY);
	free(b->buffer);
	free(b);
	return jpg;
}

static void mm_library_unload_oldest() {
	i32 oldest = -1;
	for (i32 i = 0; i < mm_library_n; ++i) {
		mm_library_entry_t *l = &mm_library_entries[i];
		if (l->icon_state == 2 && l->seen < mm_library_frame - 2 && (oldest < 0 || l->seen < mm_library_entries[oldest].seen)) {
			oldest = i;
		}
	}
	if (oldest >= 0) {
		gpu_delete_texture(mm_library_entries[oldest].icon);
		mm_library_entries[oldest].icon       = NULL;
		mm_library_entries[oldest].icon_state = 0;
		mm_library_icons--;
	}
}

// Requested thumbnails: up to 8 from the pack or the cache per frame, or one rendered
static void mm_library_update(void *_) {
	mm_library_frame++;
	mm_pack_t *pack     = mm_thumbs_open();
	i32        loaded   = 0;
	bool       rendered = false;
	for (i32 i = 0; i < mm_library_n && loaded < 8; ++i) {
		mm_library_entry_t *l = &mm_library_entries[i];
		if (l->icon_state != 1) {
			continue;
		}
		if (mm_library_icons >= MM_LIBRARY_MAX_ICONS) {
			mm_library_unload_oldest();
		}
		u8  *rgba = NULL;
		i32  k    = pack != NULL ? mm_thumbs_entry(l->id) : -1;
		if (k >= 0) {
			size_t len = 0;
			u8    *jpg = (u8 *)mm_pack_read(pack, k, &len);
			rgba       = jpg != NULL && len > 0 ? mm_thumbs_decode(jpg, len, mm_thumbs_mask) : NULL;
			free(jpg);
			if (len == 0) { // Left empty on purpose: rendering it fails
				l->icon_state = 3;
				continue;
			}
		}
		if (rgba == NULL) {
			size_t size = 0;
			u8    *jpg  = iron_folder_read(mm_thumbs_cache_path(l->id), &size);
			if (jpg != NULL) {
				size_t mlen = 0;
				u8    *mask = iron_folder_read(string("%smm_thumbs/mask.raw", iron_internal_save_path()), &mlen);
				rgba        = mm_thumbs_decode(jpg, size, mask != NULL && mlen == MM_THUMB_RES * MM_THUMB_RES ? mask : mm_thumbs_mask);
				free(mask);
				free(jpg);
			}
		}
		if (rgba == NULL && !rendered) { // Not shipped: rendered once, cached
			rendered = true;
			rgba     = mm_thumbs_render(l->id, NULL);
			if (rgba != NULL) {
				iron_create_directory(string("%smm_thumbs", iron_internal_save_path()));
				buffer_t *jpg = mm_thumbs_jpg(rgba);
				iron_folder_write(mm_thumbs_cache_path(l->id), jpg->buffer, jpg->length);
				u8 *mask = malloc(MM_THUMB_RES * MM_THUMB_RES);
				for (i32 p = 0; p < MM_THUMB_RES * MM_THUMB_RES; ++p) {
					mask[p] = rgba[p * 4 + 3];
				}
				iron_folder_write(string("%smm_thumbs/mask.raw", iron_internal_save_path()), mask, MM_THUMB_RES * MM_THUMB_RES);
				free(mask);
			}
		}
		else if (rgba == NULL) {
			continue; // Rendered one this frame already
		}
		l->icon       = rgba != NULL ? mm_thumbs_texture(rgba) : NULL;
		l->icon_state = l->icon != NULL ? 2 : 3;
		mm_library_icons += l->icon != NULL ? 1 : 0;
		loaded++;
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
	}
}

// The thumbnail, NULL while it loads
gpu_texture_t *mm_library_icon(i32 i) {
	mm_library_entry_t *l = &mm_library_entries[i];
	l->seen               = mm_library_frame;
	if (l->icon_state == 0) {
		l->icon_state = 1;
		if (!mm_library_update_registered) {
			mm_library_update_registered = true;
			sys_notify_on_update(mm_library_update, NULL);
		}
	}
	return l->icon_state == 2 ? l->icon : NULL;
}

i32 mm_library_icons_loaded() {
	return mm_library_icons;
}

// Thumbnail pack generation (paint/tools/material_maker/mm_thumbnails.c): renders a thumbnail per update into the cache
// of first shown thumbnails (<save>/mm_thumbs, skipping those already there), then writes the pack from the cache

typedef struct {
	i32       id;
	buffer_t *jpg;
} mm_thumbs_item_t;

static struct {
	bool              running;
	bool              finished;
	char             *out;
	i32              *ids;
	i32               count;
	i32               next;
	mm_thumbs_item_t *items;
	i32               nitems;
	u8               *mask;
	mm_buf_t          report;
	i32               failed;
	i32               flat;
	i32               shader_failures;
} mm_thumbs_gen = {0};

static void mm_thumbs_note(const char *fmt, ...) {
	char    line[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	mm_buf_t *r = &mm_thumbs_gen.report;
	size_t    n = strlen(line);
	if (r->len + n + 2 > r->cap) {
		r->cap  = (r->len + n + 2) * 2;
		r->data = realloc(r->data, r->cap);
	}
	memcpy(r->data + r->len, line, n);
	r->len += n;
	r->data[r->len++] = '\n';
	r->data[r->len]   = 0;
}

// Raw deflate of stored blocks
static void mm_thumbs_store(mm_buf_t *b, u8 *data, size_t len) {
	size_t o = 0;
	do {
		size_t n     = len - o > 65535 ? 65535 : len - o;
		u8     hd[5] = {(u8)(o + n >= len ? 1 : 0), (u8)(n & 255), (u8)(n >> 8), (u8)(~n & 255), (u8)((~n >> 8) & 255)};
		mm_buf_addn(b, (char *)hd, 5);
		mm_buf_addn(b, (char *)data + o, n);
		o += n;
	} while (o < len);
}

// The pack, from the cached thumbnails of every library material (runs over parts of the library add up)
static void mm_thumbs_write() {
	mm_buf_t index = {0};
	mm_buf_t blobs = {0};
	i32      count = 0;
	mm_buf_add(&index, "[");
	i32 n = mm_import_library_count();
	for (i32 i = 0; i <= n; ++i) {
		bool   mask = i == n;
		size_t len  = 0;
		u8    *data = iron_folder_read(mask ? string("%smm_thumbs/mask.raw", iron_internal_save_path()) : mm_thumbs_cache_path(mm_import_library_id(i)), &len);
		if (data == NULL && !mask && iron_file_exists(string("%smm_thumbs/%d.rendering", iron_internal_save_path(), mm_import_library_id(i)))) {
			data = malloc(1); // Crashed the renderer: an empty entry, the Library tab shows no thumbnail rather than rendering it
			len  = 0;
		}
		if (data == NULL) {
			continue;
		}
		size_t o = blobs.len;
		mm_thumbs_store(&blobs, data, len);
		if (mask) {
			mm_buf_printf(&index, "%s{\"name\":\"mask\",\"o\":%zu,\"n\":%zu,\"u\":%zu}", count > 0 ? "," : "", o, blobs.len - o, len);
		}
		else {
			mm_buf_printf(&index, "%s{\"id\":%d,\"o\":%zu,\"n\":%zu,\"u\":%zu}", count > 0 ? "," : "", mm_import_library_id(i), o, blobs.len - o, len);
		}
		count++;
		free(data);
	}
	mm_buf_add(&index, "]");
	u32      head[3] = {1, (u32)count, (u32)index.len};
	mm_buf_t file    = {0};
	mm_buf_addn(&file, "MMPK", 4);
	mm_buf_addn(&file, (char *)head, 12);
	mm_buf_addn(&file, index.data, index.len);
	mm_buf_addn(&file, blobs.data, blobs.len);
	iron_folder_write(mm_thumbs_gen.out, file.data, file.len);
	mm_thumbs_note("wrote %s: %d entries, %.1f MB", mm_thumbs_gen.out, count, file.len / 1e6);
}

static void mm_thumbs_update(void *_) {
	if (!mm_thumbs_gen.running) {
		return;
	}
	if (mm_thumbs_gen.next >= mm_thumbs_gen.count) {
		mm_thumbs_gen.running  = false;
		mm_thumbs_gen.finished = true;
		mm_thumbs_write();
		return;
	}
	i32 id = mm_thumbs_gen.ids[mm_thumbs_gen.next++];
	if (iron_file_exists(mm_thumbs_cache_path(id))) { // Done by an earlier run
		mm_thumbs_gen.nitems++;
		return;
	}
	// A marker while it renders: a material that brings the app down is skipped by the next run
	char *marker = string("%smm_thumbs/%d.rendering", iron_internal_save_path(), id);
	if (iron_file_exists(marker)) {
		mm_thumbs_gen.failed++;
		mm_thumbs_note("%d %s: crashed an earlier run, skipped", id, mm_import_library_name(id));
		return;
	}
	iron_create_directory(string("%smm_thumbs", iron_internal_save_path()));
	iron_folder_write(marker, "1", 1);
	mm_mark_t mk       = mm_arena_mark();
	i32       failures = 0;
	u8       *rgba     = mm_thumbs_render(id, &failures);
	iron_delete_file(marker);
	if (rgba == NULL) {
		mm_thumbs_gen.failed++;
		mm_thumbs_note("%d %s: does not import (%s)", id, mm_import_library_name(id), mm_import_report());
		mm_arena_release(mk);
		return;
	}
	if (failures > 0) {
		mm_thumbs_gen.shader_failures++;
		mm_thumbs_note("%d %s: %d shader failures", id, mm_import_library_name(id), failures);
	}
	// Flat: the sphere's colors barely change (lighting alone varies a flat material a little)
	i32 lo = 255, hi = 0;
	for (i32 p = 0; p < MM_THUMB_RES * MM_THUMB_RES; ++p) {
		if (rgba[p * 4 + 3] == 255) {
			i32 v = (rgba[p * 4] + rgba[p * 4 + 1] * 2 + rgba[p * 4 + 2]) / 4;
			lo    = v < lo ? v : lo;
			hi    = v > hi ? v : hi;
		}
	}
	bool unlinked = mm_import_count(1) <= 1; // Nothing but the material node
	if (hi - lo < 40 && !unlinked) {
		mm_thumbs_gen.flat++;
		mm_thumbs_note("%d %s: looks flat (spread %d)", id, mm_import_library_name(id), hi - lo);
	}
	iron_create_directory(string("%smm_thumbs", iron_internal_save_path()));
	char *mask_path = string("%smm_thumbs/mask.raw", iron_internal_save_path());
	if (!iron_file_exists(mask_path)) {
		u8 *mask = malloc(MM_THUMB_RES * MM_THUMB_RES);
		for (i32 p = 0; p < MM_THUMB_RES * MM_THUMB_RES; ++p) {
			mask[p] = rgba[p * 4 + 3];
		}
		iron_folder_write(mask_path, mask, MM_THUMB_RES * MM_THUMB_RES);
		free(mask);
	}
	buffer_t *jpg = mm_thumbs_jpg(rgba);
	iron_folder_write(mm_thumbs_cache_path(id), jpg->buffer, jpg->length);
	free(jpg->buffer);
	free(jpg);
	mm_thumbs_gen.nitems++;
	free(rgba);
	mm_arena_release(mk);
}

// Renders the thumbnails of the library materials first .. first + count - 1 (count < 0: all) into a pack at out
void mm_thumbs_generate(char *out, i32 first, i32 count) {
	i32 n = mm_import_library_count();
	if (count < 0 || first + count > n) {
		count = n - first;
	}
	memset(&mm_thumbs_gen, 0, sizeof(mm_thumbs_gen));
	mm_thumbs_gen.out     = string_copy(out);
	mm_thumbs_gen.ids     = malloc(sizeof(i32) * (count > 0 ? count : 1));
	mm_thumbs_gen.count   = count > 0 ? count : 0;
	mm_thumbs_gen.running = true;
	for (i32 i = 0; i < mm_thumbs_gen.count; ++i) {
		mm_thumbs_gen.ids[i] = mm_import_library_id(first + i);
	}
	static bool registered = false;
	if (!registered) {
		registered = true;
		sys_notify_on_update(mm_thumbs_update, NULL);
	}
}

i32 mm_thumbs_progress() {
	return mm_thumbs_gen.next;
}

bool mm_thumbs_finished() {
	return mm_thumbs_gen.finished;
}

char *mm_thumbs_report() {
	return mm_thumbs_gen.report.data != NULL ? mm_thumbs_gen.report.data : "";
}

i32 mm_thumbs_count(i32 what) { // 0 rendered, 1 not imported, 2 flat, 3 with shader failures
	return what == 0 ? mm_thumbs_gen.nitems : what == 1 ? mm_thumbs_gen.failed : what == 2 ? mm_thumbs_gen.flat : mm_thumbs_gen.shader_failures;
}

// Whether the shipped pack has a thumbnail for the library material
bool mm_thumbs_has(i32 id) {
	mm_pack_t *p = mm_thumbs_open();
	return p != NULL && mm_thumbs_entry(id) >= 0;
}

// Time of a search through every material, ms (the tab searches on every frame it draws)
f32 mm_library_search_ms(char *search) {
	mm_library_open();
	double t0 = iron_time();
	i32    n  = 0;
	for (i32 k = 0; k < 20; ++k) {
		for (i32 i = 0; i < mm_library_n; ++i) {
			n += mm_library_matches(i, search) ? 1 : 0;
		}
	}
	return (f32)((iron_time() - t0) * 1000.0 / 20.0) + (n < 0 ? 1 : 0);
}
