#include "global.h"

// Material snapshots (stroke_rec.h): what a material was when a stroke painted with it, so editing it later does not
// change past strokes. HSTORE_SNAPSHOT blob "PMS2": a header of the paint settings, the files the material reads, then
// its canvases (armpack), the material's own and the closure of the groups and Material node materials it reaches.
//   files     image node assets as the bytes of their file (HSTORE_ASSET hashed as imported, the blobs of packed assets
//             in saved projects), or their texels after a "PAS1" header when an importer decoded them; files of Material
//             Maker image and text nodes by their absolute path in the node (mm_buffers_read_file)
//   canvases  kind 0 the material, 1 a group (by name, as parser_material_get_group), 2 a material of a Material node,
//             3 the material itself picked by one of its Material nodes (no bytes)
// Image and Material nodes index the snapshot's tables: images its files in order, materials its canvases of kinds 2
// and 3 in order from 1. Replay parses with the project's group, material and asset lists swapped for the snapshot's
// (paint_mat_snapshot_parse_begin) and reads Material Maker files from their blobs (paint_mat_snapshot_replay_begin).

#define PAINT_MAT_SNAPSHOT_MAGIC 0x32534d50 // "PMS2"

typedef struct paint_mat_snapshot_head {
	u32 magic;
	i32 id;
	f32 uv_scale;
	f32 uv_angle;
	u8  paint[9]; // base opac occ rough met nor height emis subs
	u8  pad[3];
	u32 n_canvases; // The number of files follows, a u32
} paint_mat_snapshot_head_t;

#define PAINT_MAT_SNAPSHOT_SCRATCH (4 * 1024 * 1024)
#define PMS_MAX                    64 // Canvases, images and Material Maker files of a snapshot

static u8 *paint_mat_snapshot_scratch = NULL;

// What a snapshot's material reads besides its canvas, for its parse
typedef struct paint_mat_snapshot_deps {
	node_group_t_array_t    *groups;
	slot_material_t_array_t *mats;   // [0] the project's first material when parsing (its shader data is read), then nested
	asset_t_array_t         *assets; // Image files, texture keys snapshot/<hash>/<file name> in data_cached_textures
	char                   **mm_paths;
	hash128_t               *mm_files;
	i32                      n_mm;
} paint_mat_snapshot_deps_t;

// Materials made from snapshots, and the project's own by the hash of what they are now
typedef struct {
	hash128_t                  h;
	slot_material_t           *m;
	bool                       own; // Instantiated here
	paint_mat_snapshot_deps_t *deps;
} paint_mat_snapshot_entry_t;

static paint_mat_snapshot_entry_t *paint_mat_snapshot_cache = NULL;
static i32                         paint_mat_snapshot_count = 0;
static i32                         paint_mat_snapshot_cap   = 0;

// The closure of a material: its canvases, the images and Material Maker files they read
typedef struct {
	ui_node_canvas_t *canvas[PMS_MAX];
	u8                kind[PMS_MAX];
	i32               mat[PMS_MAX]; // Project index of kinds 2 and 3
	i32               n;
	asset_t          *images[PMS_MAX];
	i32               n_images;
	char             *mm[PMS_MAX]; // malloc'd
	i32               n_mm;
} pms_closure_t;

static pms_closure_t pms_cl;

static i32 pms_button_index(ui_node_t *n) {
	if (n->buttons->length == 0 || n->buttons->buffer[0]->default_value == NULL || n->buttons->buffer[0]->default_value->length == 0) {
		return -1;
	}
	return (i32)n->buttons->buffer[0]->default_value->buffer[0];
}

static i32 pms_find_canvas(pms_closure_t *cl, ui_node_canvas_t *c) {
	for (i32 i = 0; i < cl->n; ++i) {
		if (cl->canvas[i] == c && cl->kind[i] != 3) {
			return i;
		}
	}
	return -1;
}

// Local index of project material mi in Material nodes, -1 when not listed
static i32 pms_mat_local(pms_closure_t *cl, i32 mi) {
	i32 k = 0;
	for (i32 i = 0; i < cl->n; ++i) {
		if (cl->kind[i] >= 2) {
			k++;
			if (cl->mat[i] == mi) {
				return k;
			}
		}
	}
	return -1;
}

static i32 pms_image_local(pms_closure_t *cl, asset_t *a) {
	for (i32 i = 0; i < cl->n_images; ++i) {
		if (cl->images[i] == a) {
			return i;
		}
	}
	return -1;
}

static void pms_add(pms_closure_t *cl, ui_node_canvas_t *c, u8 kind, i32 mi) {
	if (cl->n < PMS_MAX) {
		cl->canvas[cl->n] = c;
		cl->kind[cl->n]   = kind;
		cl->mat[cl->n]    = mi;
		cl->n++;
	}
}

// An absolute file path of a Material Maker node's text button (image and text nodes), malloc'd, else NULL
static char *pms_mm_path(ui_node_button_t *b) {
	if (b->data == NULL || b->data->length < 2 || !string_equals(b->name, "mm_string") || b->data->buffer[0] != '/') {
		return NULL;
	}
	return u8_array_to_string(b->data);
}

static void pms_collect(pms_closure_t *cl, slot_material_t *m) {
	for (i32 i = 0; i < cl->n_mm; ++i) {
		free(cl->mm[i]);
	}
	cl->n = cl->n_images = cl->n_mm = 0;
	pms_add(cl, m->canvas, 0, -1);
	slot_material_t_array_t *mats   = g_project->_->materials;
	asset_t_array_t         *assets = g_project->_->assets;
	for (i32 i = 0; i < cl->n; ++i) {
		if (cl->kind[i] == 3) {
			continue;
		}
		ui_node_array_t *nodes = cl->canvas[i]->nodes;
		for (i32 j = 0; j < nodes->length; ++j) {
			ui_node_t *n = nodes->buffer[j];
			if (string_equals(n->type, "GROUP")) {
				ui_node_canvas_t *g = parser_material_get_group(n->name);
				if (g != NULL && pms_find_canvas(cl, g) < 0) {
					pms_add(cl, g, 1, -1);
				}
			}
			else if (string_equals(n->type, "MATERIAL")) {
				i32 mi = pms_button_index(n);
				if (mi >= 0 && mi < mats->length && mats->buffer[mi]->canvas != NULL && pms_mat_local(cl, mi) < 0) {
					ui_node_canvas_t *c = mats->buffer[mi]->canvas;
					pms_add(cl, c, c == m->canvas ? 3 : 2, mi);
				}
			}
			else if (string_equals(n->type, "TEX_IMAGE")) {
				i32 ai = pms_button_index(n);
				if (ai >= 0 && ai < assets->length && pms_image_local(cl, assets->buffer[ai]) < 0 && cl->n_images < PMS_MAX) {
					cl->images[cl->n_images++] = assets->buffer[ai];
				}
			}
			else if (starts_with(n->type, "MM_")) {
				for (i32 k = 0; k < n->buttons->length; ++k) {
					char *path = pms_mm_path(n->buttons->buffer[k]);
					bool  seen = path == NULL;
					for (i32 q = 0; !seen && q < cl->n_mm; ++q) {
						seen = string_equals(cl->mm[q], path);
					}
					if (!seen && cl->n_mm < PMS_MAX) {
						cl->mm[cl->n_mm++] = path;
					}
					else {
						free(path);
					}
				}
			}
		}
	}
}

// Image and Material node indices of the closure's canvases point into its tables while it is encoded
typedef struct {
	f32 *p;
	f32  v;
} pms_patch_t;

static pms_patch_t *pms_patches     = NULL;
static i32          pms_patch_count = 0;
static i32          pms_patch_cap   = 0;

static void pms_patch(f32 *p, f32 v) {
	if (pms_patch_count == pms_patch_cap) {
		pms_patch_cap = pms_patch_cap == 0 ? 64 : pms_patch_cap * 2;
		pms_patches   = realloc(pms_patches, sizeof(pms_patch_t) * pms_patch_cap);
	}
	pms_patches[pms_patch_count++] = (pms_patch_t){.p = p, .v = *p};
	*p                             = v;
}

static void pms_rewrite(pms_closure_t *cl) {
	pms_patch_count                 = 0;
	slot_material_t_array_t *mats   = g_project->_->materials;
	asset_t_array_t         *assets = g_project->_->assets;
	for (i32 i = 0; i < cl->n; ++i) {
		if (cl->kind[i] == 3) { // The material's canvas, done
			continue;
		}
		ui_node_array_t *nodes = cl->canvas[i]->nodes;
		for (i32 j = 0; j < nodes->length; ++j) {
			ui_node_t *n = nodes->buffer[j];
			bool       image = string_equals(n->type, "TEX_IMAGE");
			if (!image && !string_equals(n->type, "MATERIAL")) {
				continue;
			}
			if (n->buttons->length == 0 || n->buttons->buffer[0]->default_value == NULL || n->buttons->buffer[0]->default_value->length == 0) {
				continue;
			}
			i32 x     = pms_button_index(n);
			i32 local = -1; // Missing, as an index out of the project's list
			if (image) {
				local = x >= 0 && x < assets->length ? pms_image_local(cl, assets->buffer[x]) : -1;
			}
			else {
				local = x >= 0 && x < mats->length ? pms_mat_local(cl, x) : -1;
			}
			pms_patch(&n->buttons->buffer[0]->default_value->buffer[0], (f32)local);
		}
	}
}

static void pms_restore() {
	for (i32 i = pms_patch_count - 1; i >= 0; --i) {
		*pms_patches[i].p = pms_patches[i].v;
	}
	pms_patch_count = 0;
}

// Files hashed: the bytes of a file are read once a session, by asset (and its texture) or by path
typedef struct {
	void          *key; // asset_t, NULL for a path
	gpu_texture_t *image;
	char          *file;
	hash128_t      h;
} pms_file_hash_t;

static pms_file_hash_t pms_file_hashes[128];
static i32             pms_file_hash_count = 0;


// Texels as an asset blob (malloc'd): a tip generated in code has no file, an imported image is kept as it was decoded (and
// BC7 compressed): the bytes replay reads
#define PAINT_ASSET_MAGIC 0x31534150 // "PAS1"

typedef struct paint_asset_head {
	u32 magic;
	i32 width;
	i32 height;
	i32 format;
} paint_asset_head_t;

static u8 *paint_asset_texels(gpu_texture_t *t, u32 *len) {
	buffer_t *px  = gpu_get_texture_pixels(t);
	i32       fmt = t->format;
#ifdef WITH_BC7
	if (fmt == GPU_TEXTURE_FORMAT_RGBA32_BC7) {
		fmt = GPU_TEXTURE_FORMAT_RGBA32; // Decompressed by the read
	}
#endif
	*len                    = sizeof(paint_asset_head_t) + px->length;
	u8                *blob = malloc(*len);
	paint_asset_head_t head = {.magic = PAINT_ASSET_MAGIC, .width = t->width, .height = t->height, .format = fmt};
	memcpy(blob, &head, sizeof(head));
	memcpy(blob + sizeof(head), px->buffer, px->length);
	return blob;
}

// The file hash of an image asset or a Material Maker path; put: stored, a reference added (*added)
static hash128_t pms_file_hash(asset_t *a, char *path, bool put, bool *added) {
	*added              = false;
	char          *file = a != NULL ? a->file : path;
	gpu_texture_t *img  = a != NULL ? a->image : NULL;
	for (i32 i = 0; i < pms_file_hash_count; ++i) {
		pms_file_hash_t *e = &pms_file_hashes[i];
		if (e->key == (void *)a && e->image == img && string_equals(e->file, file) && (!put || hstore_has(e->h))) {
			return e->h;
		}
	}
	buffer_t *bytes = NULL;
	bool      own   = false;
	if (a != NULL && g_project->packed_assets != NULL) {
		for (i32 i = 0; i < g_project->packed_assets->length; ++i) {
			packed_asset_t *pa = g_project->packed_assets->buffer[i];
			if (string_equals(pa->name, a->file)) {
				bytes = (buffer_t *)pa->bytes;
				break;
			}
		}
	}
	if (bytes == NULL && (a == NULL || any_map_get(import_texture_importers, path_ext(file)) == NULL) && iron_file_exists(file)) {
		bytes = iron_load_blob(file);
		own   = bytes != NULL;
	}
	hash128_t h = HASH128_NONE;
	if (bytes != NULL && bytes->length > 0) {
		h = put ? hstore_put(HSTORE_ASSET, bytes->buffer, bytes->length) : hstore_hash(bytes->buffer, bytes->length);
	}
	else if (a != NULL && a->image != NULL) { // Decoded by an importer, or its file gone: its texels
		u32 len  = 0;
		u8 *blob = paint_asset_texels(a->image, &len);
		h        = put ? hstore_put(HSTORE_ASSET, blob, len) : hstore_hash(blob, len);
		free(blob);
	}
	if (own) {
		iron_delete_blob(bytes);
	}
	*added = put && !hash128_is_none(h);
	if (hash128_is_none(h)) {
		return h;
	}
	pms_file_hash_t *e = NULL;
	for (i32 i = 0; i < pms_file_hash_count && e == NULL; ++i) {
		if (pms_file_hashes[i].key == (void *)a && string_equals(pms_file_hashes[i].file, file)) {
			e = &pms_file_hashes[i];
			free(e->file);
		}
	}
	if (e == NULL) {
		if (pms_file_hash_count == 128) {
			free(pms_file_hashes[0].file);
			memmove(pms_file_hashes, pms_file_hashes + 1, sizeof(pms_file_hash_t) * 127);
			pms_file_hash_count--;
		}
		e = &pms_file_hashes[pms_file_hash_count++];
	}
	*e = (pms_file_hash_t){.key = a, .image = img, .file = string_copy(file), .h = h};
	return e->h;
}

// The snapshot of the closure into the scratch; put: its files stored, the references added listed in refs
static u32 pms_encode(slot_material_t *m, pms_closure_t *cl, bool put, hash128_t *refs, i32 *n_refs) {
	if (paint_mat_snapshot_scratch == NULL) {
		paint_mat_snapshot_scratch = malloc(PAINT_MAT_SNAPSHOT_SCRATCH);
	}
	paint_mat_snapshot_head_t h = {.magic = PAINT_MAT_SNAPSHOT_MAGIC, .id = m->id, .uv_scale = m->uv_scale, .uv_angle = m->uv_angle};
	bool flags[9] = {m->paint_base, m->paint_opac, m->paint_occ, m->paint_rough, m->paint_met, m->paint_nor, m->paint_height, m->paint_emis, m->paint_subs};
	for (i32 i = 0; i < 9; ++i) {
		h.paint[i] = flags[i];
	}
	h.n_canvases = cl->n;
	memcpy(paint_mat_snapshot_scratch, &h, sizeof(h));
	u8 *p       = paint_mat_snapshot_scratch + sizeof(h);
	u32 n_files = cl->n_images + cl->n_mm;
	memcpy(p, &n_files, 4);
	p += 4;
	*n_refs = 0;
	for (u32 i = 0; i < n_files; ++i) {
		bool      image = i < (u32)cl->n_images;
		bool      added = false;
		hash128_t fh    = image ? pms_file_hash(cl->images[i], NULL, put, &added) : pms_file_hash(NULL, cl->mm[i - cl->n_images], put, &added);
		if (added) {
			refs[(*n_refs)++] = fh;
		}
		char *name = image ? cl->images[i]->file : cl->mm[i - cl->n_images];
		u16   nl   = (u16)strlen(name);
		u8    kind = image ? 0 : 1;
		memcpy(p, &fh, sizeof(fh));
		p[16] = kind;
		p[17] = 0;
		memcpy(p + 18, &nl, 2);
		memcpy(p + 20, name, nl);
		p += 20 + nl;
	}
	pms_rewrite(cl);
	for (i32 i = 0; i < cl->n; ++i) {
		u32 len = 0;
		p[0]    = cl->kind[i];
		p[1] = p[2] = p[3] = 0;
		if (cl->kind[i] != 3) {
			armpack_encode_start(p + 8);
			ui_node_canvas_encode(cl->canvas[i]);
			len = armpack_encode_end();
		}
		memcpy(p + 4, &len, 4);
		p += 8 + len;
	}
	pms_restore();
	return (u32)(p - paint_mat_snapshot_scratch);
}

// The files listed by a snapshot blob, *cur past them; false when it is not one
static bool pms_files(const u8 *data, u32 len, u32 *cur, u32 *n_canvases, u32 *n_files) {
	paint_mat_snapshot_head_t h;
	if (len < sizeof(h) + 4) {
		return false;
	}
	memcpy(&h, data, sizeof(h));
	if (h.magic != PAINT_MAT_SNAPSHOT_MAGIC) {
		return false;
	}
	*n_canvases = h.n_canvases;
	memcpy(n_files, data + sizeof(h), 4);
	*cur = sizeof(h) + 4;
	return true;
}

// Children of a stored snapshot (hstore_set_children): its files
static void paint_mat_snapshot_children(const u8 *data, u32 len, hstore_visit_fn visit, void *ud) {
	u32 cur, nc, nf;
	if (!pms_files(data, len, &cur, &nc, &nf)) {
		return;
	}
	for (u32 i = 0; i < nf && cur + 20 <= len; ++i) {
		hash128_t fh;
		u16       nl;
		memcpy(&fh, data + cur, sizeof(fh));
		memcpy(&nl, data + cur + 18, 2);
		if (!hash128_is_none(fh)) {
			visit(fh, ud);
		}
		cur += 20 + nl;
	}
}

static void paint_mat_snapshot_cache_add(hash128_t h, slot_material_t *m, bool own, paint_mat_snapshot_deps_t *deps) {
	for (i32 i = 0; i < paint_mat_snapshot_count; ++i) {
		if (hash128_eq(paint_mat_snapshot_cache[i].h, h) && !own) {
			paint_mat_snapshot_cache[i].m = m; // The project's material as it is now
			return;
		}
	}
	if (paint_mat_snapshot_count == paint_mat_snapshot_cap) {
		paint_mat_snapshot_cap   = paint_mat_snapshot_cap == 0 ? 16 : paint_mat_snapshot_cap * 2;
		paint_mat_snapshot_cache = realloc(paint_mat_snapshot_cache, sizeof(paint_mat_snapshot_entry_t) * paint_mat_snapshot_cap);
	}
	paint_mat_snapshot_cache[paint_mat_snapshot_count++] = (paint_mat_snapshot_entry_t){.h = h, .m = m, .own = own, .deps = deps};
}

// The snapshot of m as it is now, a reference added
hash128_t paint_mat_snapshot_intern(slot_material_t *m) {
	if (m == NULL || m->canvas == NULL) {
		return HASH128_NONE;
	}
	hash128_t refs[PMS_MAX * 2];
	i32       n_refs = 0;
	pms_collect(&pms_cl, m);
	u32       len = pms_encode(m, &pms_cl, true, refs, &n_refs);
	hash128_t h   = hstore_put(HSTORE_SNAPSHOT, paint_mat_snapshot_scratch, len); // Its files are its children now
	for (i32 i = 0; i < n_refs; ++i) {
		hstore_unref(refs[i]);
	}
	paint_mat_snapshot_cache_add(h, m, false, NULL);
	return h;
}

// Texture keys of snapshot images made here, deleted with the project
static char **pms_texture_keys     = NULL;
static i32    pms_texture_key_count = 0;

// The texture of an image file of a snapshot, in data_cached_textures under its key; NULL when its blob is missing
static char *pms_image_texture(hash128_t h, char *file) {
	char *name = file;
	for (char *c = file; *c != 0; ++c) {
		if (*c == '/' || *c == '\\') {
			name = c + 1;
		}
	}
	char *key = string("snapshot/%016llx%016llx/%s", (unsigned long long)h.hi, (unsigned long long)h.lo, name);
	if (data_cached_textures == NULL) {
		data_cached_textures = any_map_create();
	}
	if (any_map_get(data_cached_textures, key) != NULL) {
		return key;
	}
	u32 len  = 0;
	u8 *blob = hstore_get(h, &len);
	if (blob == NULL) {
		return NULL;
	}
	gpu_texture_t     *t = NULL;
	paint_asset_head_t head;
	if (len >= sizeof(head) && memcmp(blob, "PAS1", 4) == 0) {
		memcpy(&head, blob, sizeof(head));
		buffer_t b = {.buffer = blob + sizeof(head), .length = len - sizeof(head)};
		if (sizeof(head) + (u64)gpu_texture_format_size(head.format) * head.width * head.height <= len) {
			t = gpu_create_texture_from_bytes_raw(&b, head.width, head.height, head.format);
		}
	}
	else {
		buffer_t b = {.buffer = blob, .length = len};
		t          = gpu_create_texture_from_encoded_bytes(&b, name); // As iron_load_texture decodes the file
	}
	free(blob);
	if (t == NULL) {
		return NULL;
	}
	any_map_set(data_cached_textures, key, t);
	pms_texture_keys                          = realloc(pms_texture_keys, sizeof(char *) * (pms_texture_key_count + 1));
	pms_texture_keys[pms_texture_key_count++] = key;
	return key;
}

static ui_node_canvas_t *pms_decode_canvas(const u8 *data, u32 len) {
	buffer_t b = {.buffer = (u8 *)data, .length = len};
	return util_clone_canvas(armpack_decode(&b));
}

// A hidden material from a snapshot blob, its dependencies in *deps (NULL when it has none)
static slot_material_t *pms_decode(u8 *data, u32 len, paint_mat_snapshot_deps_t **deps_out) {
	*deps_out = NULL;
	paint_mat_snapshot_head_t head;
	if (len < sizeof(head)) {
		return NULL;
	}
	memcpy(&head, data, sizeof(head));
	ui_node_canvas_t          *canvas = NULL;
	paint_mat_snapshot_deps_t *deps   = NULL;
	u32 cur, nc, nf;
	if (!pms_files(data, len, &cur, &nc, &nf) || nc == 0) {
		return NULL;
	}
	deps         = calloc(1, sizeof(paint_mat_snapshot_deps_t));
	deps->groups = any_array_create_from_raw((void *[]){}, 0);
	deps->mats   = any_array_create_from_raw((void *[]){}, 0);
	deps->assets = any_array_create_from_raw((void *[]){}, 0);
	any_array_push(deps->mats, NULL); // The project's first material, when parsing
	bool ok = true;
	for (u32 i = 0; i < nf && ok; ++i) {
		hash128_t fh;
		u16       nl;
		ok = cur + 20 <= len;
		if (!ok) {
			break;
		}
		memcpy(&fh, data + cur, sizeof(fh));
		u8 kind = data[cur + 16];
		memcpy(&nl, data + cur + 18, 2);
		ok = cur + 20 + nl <= len;
		if (!ok) {
			break;
		}
		char *name = calloc(1, nl + 1);
		memcpy(name, data + cur + 20, nl);
		cur += 20 + nl;
		if (kind == 0) {
			char *key = hash128_is_none(fh) ? "" : pms_image_texture(fh, name);
			ok        = key != NULL;
			if (ok) {
				asset_t *a = ALLOC_INIT(asset_t, {.id = -1, .name = name, .file = key, .image = key[0] != 0 ? any_map_get(data_cached_textures, key) : NULL});
				any_array_push(deps->assets, a);
			}
		}
		else {
			ok              = hash128_is_none(fh) || hstore_has(fh);
			deps->mm_paths  = realloc(deps->mm_paths, sizeof(char *) * (deps->n_mm + 1));
			deps->mm_files  = realloc(deps->mm_files, sizeof(hash128_t) * (deps->n_mm + 1));
			deps->mm_paths[deps->n_mm] = name;
			deps->mm_files[deps->n_mm] = fh;
			deps->n_mm++;
		}
	}
	for (u32 i = 0; i < nc && ok; ++i) {
		u32 clen = 0;
		ok       = cur + 8 <= len;
		if (!ok) {
			break;
		}
		u8 kind = data[cur];
		memcpy(&clen, data + cur + 4, 4);
		ok = cur + 8 + clen <= len && (i == 0) == (kind == 0);
		if (!ok) {
			break;
		}
		ui_node_canvas_t *c = kind != 3 ? pms_decode_canvas(data + cur + 8, clen) : NULL;
		cur += 8 + clen;
		if (kind == 0) {
			canvas = c;
		}
		else if (kind == 1) {
			node_group_t *g = ALLOC_INIT(node_group_t, {.nodes = ui_nodes_create(), .canvas = c});
			any_array_push(deps->groups, g);
		}
		else {
			slot_material_t *nm = calloc(1, sizeof(slot_material_t));
			nm->canvas          = kind == 3 ? canvas : c;
			any_array_push(deps->mats, nm);
		}
	}
	if (!ok || canvas == NULL) {
		return NULL; // Leaks what was made, a corrupt or incomplete snapshot is rare
	}
	if (deps->groups->length == 0 && deps->mats->length == 1 && deps->assets->length == 0 && deps->n_mm == 0) {
		deps = NULL;
	}
	slot_material_t *m = slot_material_create(g_project->_->materials->buffer[0]->data, canvas);
	m->id              = head.id;
	m->uv_scale        = head.uv_scale;
	m->uv_angle        = head.uv_angle;
	bool *flags[9]     = {&m->paint_base, &m->paint_opac, &m->paint_occ, &m->paint_rough, &m->paint_met, &m->paint_nor, &m->paint_height, &m->paint_emis, &m->paint_subs};
	for (i32 i = 0; i < 9; ++i) {
		*flags[i] = head.paint[i] != 0;
	}
	*deps_out = deps;
	return m;
}

// A material that paints as the snapshot did: the project's material when it is still the same, else a hidden one
slot_material_t *paint_mat_snapshot_instantiate(hash128_t h) {
	for (i32 i = 0; i < paint_mat_snapshot_count; ++i) {
		paint_mat_snapshot_entry_t *e = &paint_mat_snapshot_cache[i];
		if (!hash128_eq(e->h, h)) {
			continue;
		}
		if (e->own) {
			return e->m;
		}
		if (array_index_of(g_project->_->materials, e->m) >= 0 && e->m->canvas != NULL) { // Still what it was?
			i32 n_refs = 0;
			pms_collect(&pms_cl, e->m);
			u32 len = pms_encode(e->m, &pms_cl, false, NULL, &n_refs);
			if (hash128_eq(hstore_hash(paint_mat_snapshot_scratch, len), h)) {
				return e->m;
			}
		}
	}
	u32 len  = 0;
	u8 *data = hstore_get(h, &len);
	if (data == NULL) {
		return NULL;
	}
	paint_mat_snapshot_deps_t *deps = NULL;
	slot_material_t           *m    = pms_decode(data, len, &deps);
	free(data);
	if (m == NULL) {
		return NULL;
	}
	hstore_ref(h);
	paint_mat_snapshot_cache_add(h, m, true, deps);
	return m;
}

static paint_mat_snapshot_deps_t *pms_deps_of(slot_material_t *m) {
	for (i32 i = 0; m != NULL && i < paint_mat_snapshot_count; ++i) {
		if (paint_mat_snapshot_cache[i].own && paint_mat_snapshot_cache[i].m == m) {
			return paint_mat_snapshot_cache[i].deps;
		}
	}
	return NULL;
}

// Parsing m: the project's group, material and asset lists are the snapshot's while it parses
static node_group_t_array_t    *pms_saved_groups = NULL;
static slot_material_t_array_t *pms_saved_mats   = NULL;
static asset_t_array_t         *pms_saved_assets = NULL;
static bool                     pms_parsing      = false;

void paint_mat_snapshot_parse_begin(slot_material_t *m) {
	paint_mat_snapshot_deps_t *d = pms_deps_of(m);
	if (d == NULL || pms_parsing) {
		return;
	}
	pms_parsing                   = true;
	pms_saved_groups              = g_project->_->material_groups;
	pms_saved_mats                = g_project->_->materials;
	pms_saved_assets              = g_project->_->assets;
	d->mats->buffer[0]            = pms_saved_mats->buffer[0];
	g_project->_->material_groups = d->groups;
	g_project->_->materials       = d->mats;
	g_project->_->assets          = d->assets;
}

void paint_mat_snapshot_parse_end() {
	if (!pms_parsing) {
		return;
	}
	pms_parsing                   = false;
	g_project->_->material_groups = pms_saved_groups;
	g_project->_->materials       = pms_saved_mats;
	g_project->_->assets          = pms_saved_assets;
}

// Material Maker image and text node files of the snapshot replayed, from their blobs
static paint_mat_snapshot_deps_t *pms_replay_deps = NULL;

static uint8_t *pms_mm_read(const char *path, size_t *size) {
	for (i32 i = 0; pms_replay_deps != NULL && i < pms_replay_deps->n_mm; ++i) {
		if (strcmp(pms_replay_deps->mm_paths[i], path) == 0) {
			if (hash128_is_none(pms_replay_deps->mm_files[i])) {
				return NULL; // Missing when recorded too
			}
			u32 len  = 0;
			u8 *blob = hstore_get(pms_replay_deps->mm_files[i], &len);
			*size    = len;
			return blob;
		}
	}
	// As mm_buffers_read_file
	char     *file = path[0] == '/' ? (char *)path : data_resolve_path(string_tmp("material_maker/%s", path));
	buffer_t *b    = iron_load_blob(file);
	if (b == NULL) {
		return NULL;
	}
	uint8_t *data = malloc(b->length > 0 ? b->length : 1);
	memcpy(data, b->buffer, b->length);
	*size = b->length;
	iron_delete_blob(b);
	return data;
}

void paint_mat_snapshot_replay_begin(slot_material_t *m) {
	paint_mat_snapshot_deps_t *d = pms_deps_of(m);
	if (d == NULL || d->n_mm == 0) {
		return;
	}
	mm_buffers_init(); // Its reader set first, this one after
	mm_set_file_reader(pms_mm_read);
	pms_replay_deps = d;
}

void paint_mat_snapshot_replay_end() {
	pms_replay_deps = NULL;
}

// New or opened project: materials made here belong to the old one
void paint_mat_snapshot_reset() {
	paint_mat_snapshot_count = 0;
	for (i32 i = 0; i < pms_texture_key_count; ++i) {
		data_delete_texture(pms_texture_keys[i]);
	}
	pms_texture_key_count = 0;
	for (i32 i = 0; i < pms_file_hash_count; ++i) {
		free(pms_file_hashes[i].file);
	}
	pms_file_hash_count = 0;
}

void paint_mat_snapshot_init() {
	hstore_set_children(HSTORE_SNAPSHOT, paint_mat_snapshot_children);
}

// Brush tips and stencils of recorded strokes (HSTORE_ASSET): the texture's texels as painted with, after a header
// (paint_asset_texels).

typedef struct {
	gpu_texture_t *t;
	hash128_t      h;
	bool           own; // Made from the blob here
} paint_asset_entry_t;

static paint_asset_entry_t paint_assets[64];
static i32                 paint_asset_count = 0;

// The asset of a texture painted with, a reference held for the session (kept by the texture's entry)
hash128_t paint_asset_intern(gpu_texture_t *t) {
	if (t == NULL) {
		return HASH128_NONE;
	}
	for (i32 i = 0; i < paint_asset_count; ++i) {
		if (paint_assets[i].t == t) {
			return paint_assets[i].h;
		}
	}
	u32       len  = 0;
	u8       *blob = paint_asset_texels(t, &len);
	hash128_t h    = hstore_put(HSTORE_ASSET, blob, len);
	free(blob);
	if (paint_asset_count == 64) { // Oldest entry goes, its reference with it
		hstore_unref(paint_assets[0].h);
		memmove(paint_assets, paint_assets + 1, sizeof(paint_asset_entry_t) * 63);
		paint_asset_count--;
	}
	paint_assets[paint_asset_count++] = (paint_asset_entry_t){.t = t, .h = h, .own = false};
	return h;
}

// The texture of an asset: one painted with this session, or made from its blob
gpu_texture_t *paint_asset_texture(hash128_t h) {
	if (hash128_is_none(h)) {
		return NULL;
	}
	for (i32 i = 0; i < paint_asset_count; ++i) {
		if (hash128_eq(paint_assets[i].h, h)) {
			return paint_assets[i].t;
		}
	}
	u32 len  = 0;
	u8 *blob = hstore_get(h, &len);
	if (blob == NULL || len < sizeof(paint_asset_head_t)) {
		free(blob);
		return NULL;
	}
	paint_asset_head_t head;
	memcpy(&head, blob, sizeof(head));
	if (head.magic != PAINT_ASSET_MAGIC || sizeof(head) + (u64)gpu_texture_format_size(head.format) * head.width * head.height > len) {
		free(blob);
		return NULL;
	}
	buffer_t       b = {.buffer = blob + sizeof(head), .length = len - sizeof(head)};
	gpu_texture_t *t = gpu_create_texture_from_bytes_raw(&b, head.width, head.height, head.format);
	free(blob);
	if (paint_asset_count < 64) {
		hstore_ref(h);
		paint_assets[paint_asset_count++] = (paint_asset_entry_t){.t = t, .h = h, .own = true};
	}
	return t;
}

// A stroke starts: textures painted with are read again (a tip may be drawn again into the same texture)
void paint_asset_stroke_begin() {
	i32 kept = 0;
	for (i32 i = 0; i < paint_asset_count; ++i) {
		if (paint_assets[i].own) {
			paint_assets[kept++] = paint_assets[i];
		}
		else {
			hstore_unref(paint_assets[i].h);
		}
	}
	paint_asset_count = kept;
}
