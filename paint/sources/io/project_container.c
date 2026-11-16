#include "../global.h"

// Project files, version 31 with project_v31 on: a container of chunks (history_store.c), read lazily, saved by
// appending. Little endian:
//   "MSPJ" u32 31, 8 bytes 0
//   chunks: "CHNK" ... (HSTORE_CHUNK_HEADER): the referenced blobs of the store, among them the tile maps of the live
//           layers and their tiles (timeline_tiles.c), the timelines' blobs; packed assets (HSTORE_ASSET)
//   head:   the project as armpack (util_encode_project), layers with the hash of their tile map instead of pixels,
//           packed assets with the hash of their bytes
//   index:  the chunks the head and history use, sorted by hash: u64 lo, u64 hi, u64 offset, u32 raw_len,
//           u32 comp_len, u8 kind, u8 codec, 6 bytes 0
//   footer: "MSPF" u32 31, u64 head_off, head_len, index_off, index_len, prev_footer, u32 crc32(head, index), u32 0,
//           u32 0, u32 crc32(the 60 bytes before)
// A save appends the chunks the file lacks, then a head, an index and a footer: until that footer lands the previous
// one is the file's state (a torn tail is skipped back over). Opening reads the footer, the head, the index and the live
// tiles; history blobs are read when used. Save As, compaction (40% dead bytes) and files outside the app (iCloud and
// other providers) are written whole, next to the file and moved over it.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define PC_HEADER  16
#define PC_FOOTER  64
#define PC_ENTRY   40
#define PC_VERSION 31

typedef struct pc_footer {
	u64 head_off;
	u64 head_len;
	u64 index_off;
	u64 index_len;
	u64 prev;
	u32 crc;
} pc_footer_t;

// A file, or its bytes in memory
typedef struct pc_src {
	int       fd;
	buffer_t *b;
	u64       size;
} pc_src_t;

// The file the project was opened from or last saved to, where saves append
typedef struct pc_state {
	char                 *path;
	i64                   size;
	u8                    footer[PC_FOOTER]; // Bytes of the last footer: the file is unchanged when it still ends with them
	u64                   footer_off;
	hstore_index_entry_t *index;
	i32                   count;
	i64                   live; // Bytes the last footer uses
	hash128_t            *maps; // Tile maps of the live layers in the head, referenced while they are its state
	i32                   maps_count;
} pc_state_t;

static pc_state_t pc         = {0};
static pc_state_t pc_opened  = {0}; // Of the open in progress, taken by project_container_attach
static i64        pc_reads   = 0;   // Chunks read by opens
static u32        pc_crc_table[256];

typedef struct pc_writer {
	iron_file_stream_t         *s;
	hstore_index_entry_t       *index;
	i32                         count;
	i32                         cap;
	const hstore_index_entry_t *old; // Chunks the file has (append)
	i32                         old_count;
	u64                         live;
	buffer_t_array_t           *assets; // Packed asset bytes swapped for their hash while the head is encoded
	hash128_t                  *maps;   // Referenced by the save
	i32                         maps_count;
	i32                         maps_cap;
	bool                        no_history; // A copy without history: the live layers' tile maps only
	bool                        session;    // A session head (project_session_write): no file, no packed assets
} pc_writer_t;

static pc_writer_t *pc_w = NULL;

static u32 pc_crc(u32 crc, const u8 *p, u64 n) {
	if (pc_crc_table[1] == 0) {
		for (u32 i = 0; i < 256; ++i) {
			u32 c = i;
			for (i32 k = 0; k < 8; ++k) {
				c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
			}
			pc_crc_table[i] = c;
		}
	}
	crc = ~crc;
	for (u64 i = 0; i < n; ++i) {
		crc = pc_crc_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
	}
	return ~crc;
}

static bool pc_read(pc_src_t *s, u64 off, u64 len, void *dst) {
	if (off + len > s->size || off + len < off) {
		return false;
	}
	if (s->b != NULL) {
		memcpy(dst, s->b->buffer + off, len);
		return true;
	}
	u8 *p = dst;
	while (len > 0) {
		ssize_t n = pread(s->fd, p, (size_t)len, (off_t)off);
		if (n <= 0) {
			return false;
		}
		p += n;
		off += n;
		len -= n;
	}
	return true;
}

static void pc_maps_push(hash128_t **maps, i32 *count, i32 *cap, hash128_t h) {
	if (*count == *cap) {
		*cap  = *cap == 0 ? 16 : *cap * 2;
		*maps = realloc(*maps, sizeof(hash128_t) * *cap);
	}
	(*maps)[(*count)++] = h;
}

static void pc_state_free(pc_state_t *st) {
	free(st->path);
	free(st->index);
	free(st->maps);
	*st = (pc_state_t){0};
}

// Drops the references the state holds on its maps (not after hstore_reset, they are gone)
static void pc_state_unref(pc_state_t *st) {
	for (i32 i = 0; i < st->maps_count; ++i) {
		hstore_unref(st->maps[i]);
	}
	st->maps_count = 0;
}

// ---- Index ----

static int pc_compare_entry(const void *a, const void *b) {
	const hstore_index_entry_t *x = a;
	const hstore_index_entry_t *y = b;
	if (x->h.hi != y->h.hi) {
		return x->h.hi < y->h.hi ? -1 : 1;
	}
	if (x->h.lo != y->h.lo) {
		return x->h.lo < y->h.lo ? -1 : 1;
	}
	return 0;
}

static const hstore_index_entry_t *pc_find(const hstore_index_entry_t *index, i32 count, hash128_t h) {
	if (index == NULL || count == 0) {
		return NULL;
	}
	hstore_index_entry_t key = {.h = h};
	return bsearch(&key, index, count, sizeof(hstore_index_entry_t), pc_compare_entry);
}

static void pc_entry_write(u8 *p, const hstore_index_entry_t *e) {
	memset(p, 0, PC_ENTRY);
	memcpy(p, &e->h.lo, 8);
	memcpy(p + 8, &e->h.hi, 8);
	memcpy(p + 16, &e->off, 8);
	memcpy(p + 24, &e->raw_len, 4);
	memcpy(p + 28, &e->comp_len, 4);
	p[32] = e->kind;
	p[33] = e->codec;
}

static void pc_entry_read(const u8 *p, hstore_index_entry_t *e) {
	memcpy(&e->h.lo, p, 8);
	memcpy(&e->h.hi, p + 8, 8);
	memcpy(&e->off, p + 16, 8);
	memcpy(&e->raw_len, p + 24, 4);
	memcpy(&e->comp_len, p + 28, 4);
	e->kind  = p[32];
	e->codec = p[33];
}

// ---- Reading ----

bool project_container_is(char *path) {
	u8 magic[4];
	return iron_file_read_range(path, 0, 4, magic) && memcmp(magic, "MSPJ", 4) == 0;
}

// The footer at offset at, with its head and index (malloc'd) when they check out
static bool pc_footer_at(pc_src_t *s, u64 at, pc_footer_t *f, u8 **head, u8 **index) {
	u8 p[PC_FOOTER];
	if (!pc_read(s, at, PC_FOOTER, p) || memcmp(p, "MSPF", 4) != 0) {
		return false;
	}
	u32 crc;
	memcpy(&crc, p + 60, 4);
	if (pc_crc(0, p, 60) != crc) {
		return false;
	}
	memcpy(&f->head_off, p + 8, 8);
	memcpy(&f->head_len, p + 16, 8);
	memcpy(&f->index_off, p + 24, 8);
	memcpy(&f->index_len, p + 32, 8);
	memcpy(&f->prev, p + 40, 8);
	memcpy(&f->crc, p + 48, 4);
	if (f->head_off < PC_HEADER || f->head_off + f->head_len > at || f->index_off + f->index_len > at || f->index_len % PC_ENTRY != 0) {
		return false;
	}
	*head  = malloc(f->head_len > 0 ? f->head_len : 1);
	*index = malloc(f->index_len > 0 ? f->index_len : 1);
	if (pc_read(s, f->head_off, f->head_len, *head) && pc_read(s, f->index_off, f->index_len, *index) &&
	    pc_crc(pc_crc(0, *head, f->head_len), *index, f->index_len) == f->crc) {
		return true;
	}
	free(*head);
	free(*index);
	return false;
}

// The last footer that checks out: the one at the end, or before a torn tail the one of the save before
static bool pc_footer_last(pc_src_t *s, u64 *at, pc_footer_t *f, u8 **head, u8 **index) {
	if (s->size < PC_HEADER + PC_FOOTER) {
		return false;
	}
	*at = s->size - PC_FOOTER;
	if (pc_footer_at(s, *at, f, head, index)) {
		return true;
	}
	u64 window = 1 << 20;
	u8 *buf    = malloc(window + 3);
	u64 end    = s->size - PC_FOOTER; // Candidates start before it
	while (end > PC_HEADER) {
		u64 lo = end > window + PC_HEADER ? end - window : PC_HEADER;
		u64 n  = end - lo;
		if (!pc_read(s, lo, n + 3 <= s->size - lo ? n + 3 : n, buf)) {
			break;
		}
		for (u64 i = n; i-- > 0;) {
			if (buf[i] == 'M' && memcmp(buf + i, "MSPF", 4) == 0 && pc_footer_at(s, lo + i, f, head, index)) {
				*at = lo + i;
				free(buf);
				return true;
			}
		}
		end = lo;
	}
	free(buf);
	return false;
}

static buffer_t *pc_payload(pc_src_t *s, const hstore_index_entry_t *index, i32 count, hash128_t h, hstore_chunk_t *c) {
	const hstore_index_entry_t *e = pc_find(index, count, h);
	u8                          p[HSTORE_CHUNK_HEADER];
	if (e == NULL || !pc_read(s, e->off, HSTORE_CHUNK_HEADER, p) || !hstore_chunk_header_read(p, c) || !hash128_eq(c->h, h)) {
		return NULL;
	}
	buffer_t *b = buffer_create(c->comp_len);
	if (!pc_read(s, e->off + HSTORE_CHUNK_HEADER, c->comp_len, b->buffer)) {
		array_free(b);
		free(b);
		return NULL;
	}
	pc_reads++;
	return b;
}

static hash128_t pc_hash_at(u8_array_t *a, i32 i) {
	hash128_t h = HASH128_NONE;
	if (a != NULL && a->length >= (u32)(i + 1) * 16) {
		memcpy(&h.lo, a->buffer + i * 16, 8);
		memcpy(&h.hi, a->buffer + i * 16 + 8, 8);
	}
	return h;
}

// The project of a container: layers with their raster chunks as texpaint* (LZ4 as in version 30, masks R8),
// packed assets with their bytes. NULL when no footer checks out.
static project_t *pc_load(pc_src_t *s, pc_state_t *st) {
	pc_footer_t f;
	u64         at;
	u8         *head;
	u8         *raw_index;
	if (!pc_footer_last(s, &at, &f, &head, &raw_index)) {
		return NULL;
	}
	i32                   count = (i32)(f.index_len / PC_ENTRY);
	hstore_index_entry_t *index = malloc(sizeof(hstore_index_entry_t) * (count > 0 ? count : 1));
	for (i32 i = 0; i < count; ++i) {
		pc_entry_read(raw_index + i * PC_ENTRY, &index[i]);
	}
	free(raw_index);

	buffer_t   hb      = {.buffer = head, .length = (u32)f.head_len, .capacity = (u32)f.head_len};
	project_t *project = import_arm_is_old(&hb) ? import_arm_from_old(&hb) : armpack_decode(&hb);
	free(head);
	if (project == NULL) {
		free(index);
		return NULL;
	}

	if (project->layer_datas != NULL) {
		for (i32 i = 0; i < project->layer_datas->length; ++i) {
			layer_data_t  *ld = project->layer_datas->buffer[i];
			buffer_t     **dst[3] = {(buffer_t **)&ld->texpaint, (buffer_t **)&ld->texpaint_nor, (buffer_t **)&ld->texpaint_pack};
			u8_array_t    *tm     = (u8_array_t *)ld->tilemap;
			if (tm != NULL && tm->length >= 16) { // Restored by the load from the store, empty buffers tell layers from groups
				hstore_chunk_t chunk;
				hash128_t      h        = pc_hash_at(tm, 0);
				buffer_t      *b        = hash128_is_none(h) ? NULL : pc_payload(s, index, count, h, &chunk);
				u8            *map      = b != NULL ? hstore_chunk_decode(&chunk, b->buffer, true) : NULL;
				i32            channels = map != NULL && chunk.raw_len >= 2 ? map[1] : 3; // Map header: tag, channels
				for (i32 c = 0; c < 3; ++c) {
					*dst[c] = c == 0 || channels == 3 ? buffer_create(0) : NULL;
				}
				free(map);
				if (b != NULL) {
					array_free(b);
					free(b);
				}
				if (st != NULL && !hash128_is_none(h)) {
					i32 cap = st->maps_count;
					pc_maps_push(&st->maps, &st->maps_count, &cap, h);
				}
				continue;
			}
		}
	}
	if (project->packed_assets != NULL) {
		for (i32 i = 0; i < project->packed_assets->length; ++i) {
			packed_asset_t *pa = project->packed_assets->buffer[i];
			hstore_chunk_t  c;
			buffer_t       *b = pc_payload(s, index, count, pc_hash_at((u8_array_t *)pa->bytes, 0), &c);
			if (b != NULL && c.codec != HSTORE_CODEC_RAW) {
				u8 *raw = hstore_chunk_decode(&c, b->buffer, false);
				array_free(b);
				free(b);
				b = NULL;
				if (raw != NULL) {
					b         = calloc(1, sizeof(buffer_t));
					b->buffer = raw;
					b->length = b->capacity = c.raw_len;
				}
			}
			pa->bytes = (struct buffer *)(b != NULL ? b : buffer_create(0));
		}
	}

	if (st != NULL) {
		free(st->path);
		free(st->index);
		st->path       = NULL;
		st->index      = index;
		st->count      = count;
		st->footer_off = at;
		pc_read(s, at, PC_FOOTER, st->footer);
		i64 live       = PC_HEADER + f.head_len + f.index_len + PC_FOOTER;
		for (i32 i = 0; i < count; ++i) {
			live += HSTORE_CHUNK_HEADER + index[i].comp_len;
		}
		st->live = live;
	}
	else {
		free(index);
	}
	return project;
}

// The project of a container file, NULL when it does not open. Its index is kept for project_container_attach.
project_t *project_container_open(char *path) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		return NULL;
	}
	struct stat st;
	fstat(fd, &st);
	pc_src_t   s       = {.fd = fd, .size = (u64)st.st_size};
	pc_state_free(&pc_opened);
	project_t *project = pc_load(&s, &pc_opened);
	close(fd);
	if (project != NULL) {
		pc_opened.path  = string_copy(path);
		pc_opened.size  = (i64)st.st_size;
	}
	return project;
}

// A container already in memory (append from a project), nothing kept
project_t *project_container_decode(buffer_t *b) {
	pc_src_t s = {.fd = -1, .b = b, .size = b->length};
	return pc_load(&s, NULL);
}

bool project_container_is_buffer(buffer_t *b) {
	return b != NULL && b->length >= 4 && memcmp(b->buffer, "MSPJ", 4) == 0;
}

// The project load reset the store (project_new): the opened file becomes where saves append, its history blobs are
// read from it when used. Nothing for other files.
void project_container_attach() {
	pc_state_free(&pc);
	pc        = pc_opened;
	pc_opened = (pc_state_t){0};
	if (pc.path != NULL) {
		hstore_attach_project(pc.path, pc.index, pc.count);
	}
}

// The blobs the timelines and the history steps hold, appended to a list (malloc'd), for walks and rebuilds
static void pc_history_roots(hash128_t **list, i32 *count, i32 *cap) {
	i32        tl_count;
	hash128_t *tl_roots = timeline_roots(&tl_count);
	for (i32 i = 0; i < tl_count; ++i) {
		pc_maps_push(list, count, cap, tl_roots[i]);
	}
	free(tl_roots);
	hash128_t *h_roots = NULL;
	i32        h_count = history_io_roots(&h_roots);
	for (i32 i = 0; i < h_count; ++i) {
		pc_maps_push(list, count, cap, h_roots[i]);
	}
	free(h_roots);
}

// The load is done: references are rebuilt from what the project holds, the live layers' maps, the timelines and the
// history steps. The file's other blobs (and recovered ones) go at the next trim.
void project_container_loaded() {
	if (pc.path == NULL) {
		return;
	}
	pc.maps_count = 0; // The live layers' maps, the file's or a recovered session's
	i32 cap       = 0;
	free(pc.maps);
	pc.maps = NULL;
	for (i32 i = 0; g_project->layer_datas != NULL && i < g_project->layer_datas->length; ++i) {
		u8_array_t *tm = (u8_array_t *)g_project->layer_datas->buffer[i]->tilemap;
		hash128_t   h  = pc_hash_at(tm, 0);
		if (!hash128_is_none(h)) {
			pc_maps_push(&pc.maps, &pc.maps_count, &cap, h);
		}
	}
	hash128_t *roots = NULL;
	i32        n     = 0;
	i32        rcap  = 0;
	for (i32 i = 0; i < pc.maps_count; ++i) {
		pc_maps_push(&roots, &n, &rcap, pc.maps[i]);
	}
	pc_history_roots(&roots, &n, &rcap);
	hstore_rebuild_refs(roots, n); // Each map gets the state's reference, each entry, checkpoint and step its own
	free(roots);
}

// A layer of a container gets its tiles, false for the others. has_alpha: whether the texture has transparent texels.
bool project_container_layer_restore(layer_data_t *ld, slot_layer_t *l, bool *has_alpha) {
	u8_array_t *tm = (u8_array_t *)ld->tilemap;
	if (tm == NULL || tm->length < 16) {
		return false;
	}
	paint_target_t t;
	timeline_target_live(l, &t);
	hash128_t h = pc_hash_at(tm, 0);
	if (hash128_is_none(h) || !timeline_tiles_restore(h, &t)) {
		iron_log("Layer tiles could not be read, the layer starts empty");
	}
	if (has_alpha != NULL) {
		*has_alpha = import_arm_texture_has_alpha(l->texpaint);
	}
	return true;
}

// Blobs the file's state holds (the tile maps of its live layers), roots for hstore_rebuild_refs
i32 project_container_roots(hash128_t *out, i32 cap) {
	for (i32 i = 0; i < pc.maps_count && i < cap; ++i) {
		out[i] = pc.maps[i];
	}
	return pc.maps_count;
}


// ---- Writing ----

bool project_container_writing() {
	return pc_w != NULL;
}

// Timelines and history blobs go in the save
bool project_container_writing_history() {
	return pc_w != NULL && !pc_w->no_history;
}

bool project_container_writing_session() {
	return pc_w != NULL && pc_w->session;
}

// The history steps as bytes (project_t.history), NULL when none
u8_array_t *project_container_history_encode() {
	u32 len  = 0;
	u8 *data = history_io_encode(&len);
	if (data == NULL) {
		return NULL;
	}
	u8_array_t *a = calloc(1, sizeof(u8_array_t));
	a->buffer     = data;
	a->length     = len;
	a->capacity   = len;
	return a;
}

// A chunk of the save: written unless the file or this save has it, listed in the new index either way
static void pc_put(u8 kind, u8 codec, hash128_t h, u32 raw_len, const u8 *payload, u32 comp_len) {
	pc_writer_t                *w   = pc_w;
	const hstore_index_entry_t *old = pc_find(w->old, w->old_count, h);
	hstore_index_entry_t        e;
	if (old != NULL) {
		e = *old;
	}
	else {
		e = (hstore_index_entry_t){.h = h, .off = w->s->pos, .raw_len = raw_len, .comp_len = comp_len, .kind = kind, .codec = codec};
		u8 head[HSTORE_CHUNK_HEADER];
		hstore_chunk_header_write(head, kind, codec, 0, h, raw_len, comp_len);
		iron_file_stream_write(w->s, head, HSTORE_CHUNK_HEADER);
		iron_file_stream_write(w->s, payload, comp_len);
	}
	if (w->count == w->cap) {
		w->cap   = w->cap == 0 ? 64 : w->cap * 2;
		w->index = realloc(w->index, sizeof(hstore_index_entry_t) * w->cap);
	}
	w->index[w->count++] = e;
}

// Rasters and assets are few: a linear look for one the save already has
static bool pc_has(hash128_t h) {
	if (pc_find(pc_w->old, pc_w->old_count, h) != NULL) {
		return false; // Listed again, nothing written
	}
	for (i32 i = 0; i < pc_w->count; ++i) {
		if (hash128_eq(pc_w->index[i].h, h)) {
			return true;
		}
	}
	return false;
}

// The tile map of a layer's live state (layer_data_t.tilemap): its timeline's state at the cursor, else a capture. Its
// tiles are written with the store's blobs. NULL for a group.
u8_array_t *project_container_layer_tilemap(slot_layer_t *l) {
	if (l->texpaint == NULL) {
		return NULL;
	}
	timeline_t *tl = !pc_w->no_history ? timeline_of(l) : NULL; // A copy: full maps, no delta chains
	hash128_t   h  = tl != NULL ? timeline_state(tl, tl->cursor) : HASH128_NONE;
	if (!hash128_is_none(h) && hstore_has(h)) {
		hstore_ref(h);
	}
	else {
		paint_target_t t;
		timeline_target_live(l, &t);
		h = timeline_tiles_capture(&t, HASH128_NONE, NULL); // Full: older saves' tiles go dead, unchanged ones dedupe
		if (hash128_is_none(h)) {
			iron_log("Layer %s could not be captured, it is saved empty", l->name);
		}
	}
	pc_maps_push(&pc_w->maps, &pc_w->maps_count, &pc_w->maps_cap, h);
	u8_array_t *a = u8_array_create(16);
	memcpy(a->buffer, &h.lo, 8);
	memcpy(a->buffer + 8, &h.hi, 8);
	return a;
}

// Packed asset bytes become HSTORE_ASSET chunks (hashed as imported, so history references dedupe with them), the
// head keeps their hash. project_container_unpack_assets puts the bytes back.
void project_container_pack_assets(project_t *p) {
	if (p->packed_assets == NULL) {
		return;
	}
	pc_w->assets = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < p->packed_assets->length; ++i) {
		packed_asset_t *pa = p->packed_assets->buffer[i];
		buffer_t       *b  = (buffer_t *)pa->bytes;
		hash128_t       h  = hstore_hash(b->buffer, b->length);
		if (!pc_has(h)) {
			pc_put(HSTORE_ASSET, HSTORE_CODEC_RAW, h, b->length, b->buffer, b->length);
		}
		any_array_push(pc_w->assets, b);
		buffer_t *hb = buffer_create(16);
		memcpy(hb->buffer, &h.lo, 8);
		memcpy(hb->buffer + 8, &h.hi, 8);
		pa->bytes = (struct buffer *)hb;
	}
}

void project_container_unpack_assets(project_t *p) {
	if (pc_w->assets == NULL) {
		return;
	}
	for (i32 i = 0; i < p->packed_assets->length && i < pc_w->assets->length; ++i) {
		packed_asset_t *pa = p->packed_assets->buffer[i];
		array_free(pa->bytes);
		free(pa->bytes);
		pa->bytes = (struct buffer *)pc_w->assets->buffer[i];
	}
	array_free(pc_w->assets);
	free(pc_w->assets);
	pc_w->assets = NULL;
}

// A blob the walk reached: written unless the file has it. The file has its children too (pc_carry), the walk stops.
static bool pc_put_blob(hash128_t h, void *ud) {
	if (hstore_kind_of(h) == HSTORE_SESSION) {
		return false; // Journal only: the saved file is the session's state
	}
	if (pc_find(pc_w->old, pc_w->old_count, h) != NULL) {
		pc_put(0, 0, h, 0, NULL, 0);
		return false;
	}
	hstore_chunk_t c;
	u8            *payload = hstore_chunk_payload(h, &c);
	if (payload != NULL) {
		pc_put(c.kind, c.codec, h, c.raw_len, payload, c.comp_len);
		free(payload);
	}
	return true;
}

// An append lists again the blobs of the file the store still holds: the children of the ones the walk stopped at are
// among them (a stored parent references its children, so they stay stored). Blobs nothing references any more left
// the store at a trim, their bytes count as dead. A superset at worst, a whole rewrite walks everything.
static void pc_carry() {
	for (i32 i = 0; i < pc_w->old_count; ++i) {
		const hstore_index_entry_t *e = &pc_w->old[i];
		if (hstore_has(e->h)) { // Rasters and packed assets are not store blobs, the save lists them itself
			pc_put(0, 0, e->h, 0, NULL, 0);
		}
	}
}

// Appends go to local files only: providers (iCloud Drive and others) sync whole files
static bool pc_local(char *path) {
#ifdef IRON_IOS
	char *home = getenv("HOME"); // The app's container
	return home != NULL && starts_with(path, home);
#else
	return strstr(path, "/Library/Mobile Documents/") == NULL && strstr(path, "/Library/CloudStorage/") == NULL;
#endif
}

// Whether a save of path appends: the file is the one opened or saved last, unchanged since (its size, and its end is
// the footer it had: no timestamps), local, and mostly live
static bool pc_can_append(char *path) {
	if (pc.path == NULL || !string_equals(pc.path, path)) {
		return false;
	}
	u8 tail[PC_FOOTER];
	if (iron_file_size(path) != pc.size || pc.footer_off + PC_FOOTER != (u64)pc.size ||
	    !iron_file_read_range(path, pc.size - PC_FOOTER, PC_FOOTER, tail) || memcmp(tail, pc.footer, PC_FOOTER) != 0) {
		return false; // Changed by something else, a failed append or a torn tail
	}
	return pc.live >= pc.size * 6 / 10 && pc_local(path);
}

i32 project_container_last_save_appended = 0; // 1 when the last save appended (tests)

// Live layer bytes: 4 times them, 1 GB at least, is the default history budget
static i64 pc_live_layer_bytes() {
	i64 n = 0;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t  *l     = g_project->_->layers->buffer[i];
		gpu_texture_t *ts[3] = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
		for (i32 c = 0; c < 3; ++c) {
			if (ts[c] != NULL) {
				n += (i64)ts[c]->width * ts[c]->height * gpu_texture_format_size(ts[c]->format);
			}
		}
	}
	return n;
}

i64 project_container_history_budget() {
	if (g_config->history_budget_mb > 0) {
		return (i64)g_config->history_budget_mb * 1024 * 1024;
	}
	i64 b = 4 * pc_live_layer_bytes();
	return b > (i64)1024 * 1024 * 1024 ? b : (i64)1024 * 1024 * 1024;
}

// A save keeps the history within its budget: the undo track thins it (history_io_thin)
static void pc_history_fit() {
	i64 budget = project_container_history_budget();
	if (hstore_live_bytes() > budget) {
		history_io_thin(budget);
		hstore_trim(false);
	}
}

// Clear history: steps and checkpoints go, timeline entries stay (hi-res export replays them)
void project_clear_history() {
	history_io_clear();
	hstore_trim(false);
}

static bool pc_save(char *path, bool copy) {
	bool                append = !copy && pc_can_append(path);
	char               *target = append ? path : string_copy(string("%s.saving", path));
	iron_file_stream_t *s      = iron_file_stream_open(target, append);
	if (s == NULL && !append) {
		target = path; // No file next to it (a sandboxed single file): written in place
		s      = iron_file_stream_open(target, false);
	}
	if (s == NULL) {
		return false;
	}
	u64 prev = append ? pc.footer_off : 0;
	if (!append) {
		u8  header[PC_HEADER] = {'M', 'S', 'P', 'J'};
		u32 version           = PC_VERSION;
		memcpy(header + 4, &version, 4);
		iron_file_stream_write(s, header, PC_HEADER);
	}

	history_flush_restores(NULL); // Deferred undo restores land: the live layers are the ones shown
	timeline_finalize(); // Pending entry states captured
	if (!copy) {
		pc_history_fit();
	}
	pc_writer_t w = {.s = s, .old = append ? pc.index : NULL, .old_count = append ? pc.count : 0, .no_history = copy};
	pc_w          = &w;
	char *uuid    = project_uuid_get();
	if (copy) {
		project_uuid_new(); // Another file: its own journal name
	}
	buffer_t *head = export_arm_encode_project(path); // Writes the packed assets, captures the tile maps
	if (copy) {
		project_uuid = uuid;
	}
	i32 live_maps = w.maps_count;
	if (!copy) {
		pc_history_roots(&w.maps, &w.maps_count, &w.maps_cap); // Roots of the walk, not referenced by the save
	}
	hstore_each(w.maps, w.maps_count, !copy, pc_put_blob, NULL); // Each blob once
	if (append) {
		pc_carry();
	}
	w.maps_count = live_maps;
	pc_w = NULL;

	qsort(w.index, w.count, sizeof(hstore_index_entry_t), pc_compare_entry);
	i32 n = 0;
	for (i32 i = 0; i < w.count; ++i) {
		if (n == 0 || !hash128_eq(w.index[n - 1].h, w.index[i].h)) {
			w.index[n++] = w.index[i];
		}
	}
	w.count = n;

	u64 head_off  = s->pos;
	iron_file_stream_write(s, head->buffer, head->length);
	u64 index_off = s->pos;
	u8 *raw_index = malloc(n > 0 ? (size_t)n * PC_ENTRY : 1);
	for (i32 i = 0; i < n; ++i) {
		pc_entry_write(raw_index + i * PC_ENTRY, &w.index[i]);
	}
	iron_file_stream_write(s, raw_index, (u64)n * PC_ENTRY);

	u8  footer[PC_FOOTER] = {'M', 'S', 'P', 'F'};
	u32 version           = PC_VERSION;
	u64 head_len          = head->length;
	u64 index_len         = (u64)n * PC_ENTRY;
	u32 crc               = pc_crc(pc_crc(0, head->buffer, head_len), raw_index, index_len);
	memcpy(footer + 4, &version, 4);
	memcpy(footer + 8, &head_off, 8);
	memcpy(footer + 16, &head_len, 8);
	memcpy(footer + 24, &index_off, 8);
	memcpy(footer + 32, &index_len, 8);
	memcpy(footer + 40, &prev, 8);
	memcpy(footer + 48, &crc, 4);
	u32 fcrc = pc_crc(0, footer, 60);
	memcpy(footer + 60, &fcrc, 4);
	u64 footer_off = s->pos;
	iron_file_stream_write(s, footer, PC_FOOTER);
	free(raw_index);
	array_free(head);
	free(head);

	bool ok = iron_file_stream_close(s, true);
	if (target != path) {
		ok = ok && rename(target, path) == 0;
		if (!ok) {
			remove(target);
		}
	}
	if (!ok || copy) { // A copy is not where the session saves
		for (i32 i = 0; i < w.maps_count; ++i) {
			hstore_unref(w.maps[i]);
		}
		free(w.maps);
		free(w.index);
		if (!ok && !copy) {
			pc.size = -1; // The next save writes the file whole
		}
		return ok;
	}

	struct stat st;
	stat(path, &st);
	char *p = string_copy(path);
	pc_state_unref(&pc); // The new state's maps are referenced by the save
	pc_state_free(&pc);
	pc.path       = p;
	pc.maps       = w.maps;
	pc.maps_count = w.maps_count;
	pc.size       = (i64)st.st_size;
	memcpy(pc.footer, footer, PC_FOOTER);
	pc.footer_off = footer_off;
	pc.index      = w.index;
	pc.count      = n;
	pc.live       = PC_HEADER + head_len + index_len + PC_FOOTER;
	for (i32 i = 0; i < n; ++i) {
		pc.live += HSTORE_CHUNK_HEADER + w.index[i].comp_len;
	}
	project_container_last_save_appended = append ? 1 : 0;
	hstore_attach_project(path, pc.index, pc.count);
	return true;
}

// Saves the project as a container: appended to the file it came from, else written whole
bool project_container_save(char *path) {
	return pc_save(path, false);
}

// "Save copy without history": a container of the project and its live layers, no timelines nor history blobs, with
// its own uuid. The session goes on saving to its file. project_v31 only.
bool project_container_save_copy(char *path) {
	if (!g_config->project_v31) {
		return false;
	}
	return pc_save(path, true);
}

// A new project (after hstore_reset): saves do not append to the file of the one before
void project_container_forget() {
	pc_state_free(&pc);
}

// The uuid saved in a project file (a version 31 container: its footer and head are read), NULL when it has none
char *project_file_uuid(char *path) {
	if (project_container_is(path)) {
		int         fd = open(path, O_RDONLY);
		struct stat st;
		if (fd < 0) {
			return NULL;
		}
		fstat(fd, &st);
		pc_src_t    s = {.fd = fd, .size = (u64)st.st_size};
		pc_footer_t f;
		u64         at;
		u8         *head;
		u8         *index;
		if (pc_footer_last(&s, &at, &f, &head, &index)) {
			buffer_t hb = {.buffer = head, .length = (u32)f.head_len, .capacity = (u32)f.head_len};
			any_map_t *m = armpack_decode_to_map(&hb);
			char *uuid   = m != NULL ? string_copy(any_map_get(m, "uuid")) : NULL;
			free(head);
			free(index);
			close(fd);
			return uuid;
		}
		close(fd);
		return NULL;
	}
	return NULL; // Version 30 files have none (version 31 is always a container)
}

// A project file deleted from the projects screen: its history journal goes too
void hstore_journal_delete_for_file(char *path) {
	char *uuid = project_file_uuid(path);
	if (uuid != NULL) {
		hstore_journal_delete(uuid);
	}
}

// ---- Test hooks (paint/tests/legacy_upgrade.c, paint/tests/project_container.c) ----

i32 project_container_test_reads() {
	return (i32)pc_reads;
}

i32 project_container_test_appended() {
	return project_container_last_save_appended;
}

// Entries of the last saved or opened file's index of a kind
i32 project_container_test_index_count(i32 kind) {
	i32 n = 0;
	for (i32 i = 0; i < pc.count; ++i) {
		n += pc.index[i].kind == kind;
	}
	return n;
}


u32 project_test_layer_uid(i32 i) {
	return i >= 0 && i < g_project->_->layers->length ? g_project->_->layers->buffer[i]->uid : 0;
}

u32 project_test_material_uid(i32 i) {
	return i >= 0 && i < g_project->_->materials->length ? g_project->_->materials->buffer[i]->uid : 0;
}

u32 project_test_uid_next() {
	return project_uid_next;
}

// 0 width, 1 height of the project's layers
i32 project_test_res(i32 axis) {
	return axis == 0 ? config_get_texture_res_x() : config_get_texture_res_y();
}

// Layers resized to w x h (the Custom resolution of the layers tab), on the next frame
void project_test_set_res(i32 w, i32 h) {
	base_res   = TEXTURE_RES_CUSTOM;
	base_res_x = (f32)w;
	base_res_y = (f32)h;
	layers_on_resized();
}

// 31 bits of the content hash of the pixels of layer i's texture: 0 base (mask: its value), 1 normal, 2 pack. 0 for none.
i32 project_test_layer_hash(i32 i, i32 channel) {
	if (i < 0 || i >= g_project->_->layers->length) {
		return 0;
	}
	slot_layer_t  *l = g_project->_->layers->buffer[i];
	gpu_texture_t *t = channel == 0 ? l->texpaint : channel == 1 ? l->texpaint_nor : l->texpaint_pack;
	if (t == NULL) {
		return 0;
	}
	buffer_t *b = gpu_get_texture_pixels(t);
	hash128_t h = hash128(b->buffer, b->length);
	gpu_release_texture_pixels(t);
	return (i32)(h.lo & 0x7fffffff);
}

// Cuts bytes off the end of a file (a save torn by a crash)
void project_test_truncate(char *path, i32 bytes) {
	i64 size = iron_file_size(path);
	if (size > bytes && truncate(path, size - bytes) != 0) {
		iron_log("Truncate failed");
	}
}

i32 project_test_file_size(char *path) {
	return (i32)iron_file_size(path);
}

// Texels of layer i's texpaint whose 4 channels differ
i32 project_test_mask_unequal(i32 i) {
	gpu_texture_t *t = g_project->_->layers->buffer[i]->texpaint;
	buffer_t      *b = gpu_get_texture_pixels(t);
	i32            n = 0;
	for (u32 k = 0; k + 3 < b->length; k += 4) {
		u8 *p = b->buffer + k;
		n += p[0] != p[1] || p[0] != p[2] || p[0] != p[3];
	}
	gpu_release_texture_pixels(t);
	return n;
}

// ---- Session heads: unsaved work recovered after a crash ----
//
// An HSTORE_SESSION blob, journaled: u32 count, count roots (hash128: the live layers' tile maps, the timelines' and
// history steps' blobs, its children), then the head as a container save writes it without meshes, packed assets and
// material previews (recovery takes those from the saved file). Written at most every PC_SESSION_SECONDS after the
// history changes, and when the app goes to the background without saving. The newest one after the journal's last
// mark (a save) is the unsaved work: project_session_recover opens the saved file with it laid over.

#define PC_SESSION_SECONDS 5.0

static hash128_t pc_session      = {0, 0}; // The last session head written, referenced
static u64       pc_session_sig  = 0;      // History when it was written
static f64       pc_session_time = 0.0;
static bool      pc_recovering   = false;

static void pc_session_children(const u8 *data, u32 len, hstore_visit_fn visit, void *ud) {
	u32 n = 0;
	if (len < 4) {
		return;
	}
	memcpy(&n, data, 4);
	for (u32 i = 0; i < n && 4 + (i + 1) * 16 <= len; ++i) {
		hash128_t h;
		memcpy(&h.lo, data + 4 + i * 16, 8);
		memcpy(&h.hi, data + 4 + i * 16 + 8, 8);
		visit(h, ud);
	}
}

static void pc_session_register() {
	hstore_set_children(HSTORE_SESSION, pc_session_children);
}

// Changes of the history: steps, undos, redos and the last step
static u64 pc_history_signature() {
	u64 h = HASH_FNV1A_BASIS;
	i32 n = history_steps != NULL ? history_steps->length : 0;
	h     = hash_fnv1a_i32(h, n);
	h     = hash_fnv1a_i32(h, history_undos);
	h     = hash_fnv1a_i32(h, history_redos);
	h     = hash_fnv1a_i32(h, n > 0 ? (i32)history_steps->buffer[n - 1]->serial : 0);
	return h;
}

// Session heads are written for v31 projects whose layers have timelines: their live states are tile maps already, a
// head costs no readback
static bool pc_session_on() {
	return g_config->project_v31 && g_context->paint_object != NULL;
}

// Journals the session head now, true when it was written
bool project_session_write() {
	if (!pc_session_on() || pc_w != NULL) {
		return false;
	}
	pc_session_register();
	history_flush_restores(NULL);
	timeline_finalize();
	pc_writer_t w = {.session = true};
	pc_w          = &w;
	buffer_t *head = export_arm_encode_project(g_project->_->filepath);
	pc_w           = NULL;
	i32 live_maps  = w.maps_count;
	pc_history_roots(&w.maps, &w.maps_count, &w.maps_cap);

	u32 len  = 4 + (u32)w.maps_count * 16 + head->length;
	u8 *blob = malloc(len);
	u32 n    = (u32)w.maps_count;
	memcpy(blob, &n, 4);
	for (i32 i = 0; i < w.maps_count; ++i) {
		memcpy(blob + 4 + i * 16, &w.maps[i].lo, 8);
		memcpy(blob + 4 + i * 16 + 8, &w.maps[i].hi, 8);
	}
	memcpy(blob + 4 + n * 16, head->buffer, head->length);
	hash128_t h = hstore_put(HSTORE_SESSION, blob, len); // References its roots
	free(blob);
	array_free(head);
	free(head);
	for (i32 i = 0; i < live_maps; ++i) {
		hstore_unref(w.maps[i]); // Held by the head now
	}
	free(w.maps);
	free(w.index);
	hstore_unref(pc_session); // The one before (the same one stays referenced: put took another)
	pc_session      = h;
	pc_session_sig  = pc_history_signature();
	pc_session_time = iron_time();
	hstore_flush(); // On disk before a crash can take it
	return true;
}

// The history changed since the last session head (resign-active writes it then, whenever)
bool project_session_changed() {
	return pc_session_on() && pc_history_signature() != pc_session_sig;
}

// The session head is due: the history changed PC_SESSION_SECONDS ago or more, nothing is painting
bool project_session_due() {
	return pc_session_on() && g_context->brush_time == 0 && pc_history_signature() != pc_session_sig &&
	       iron_time() - pc_session_time >= PC_SESSION_SECONDS;
}

// From the update loop
void project_session_update() {
	if (project_session_due()) {
		project_session_write();
	}
}

// A new project or an open: the session head belongs to the one before (hstore_reset dropped it)
void project_session_forget() {
	pc_session      = HASH128_NONE;
	pc_session_sig  = pc_history_signature();
	pc_session_time = 0.0;
}

// The journal of the project in path (a v31 file) has a session head after its last save
bool project_session_pending(char *path) {
	char *uuid = project_file_uuid(path);
	return uuid != NULL && hstore_journal_pending(uuid);
}

bool project_session_recovering() {
	return pc_recovering;
}

// Opens the project in path with its unsaved work: the newest session head after the last save, laid over the file.
// False when there is none.
bool project_session_recover(char *path) {
	if (!project_session_pending(path)) {
		return false;
	}
	pc_recovering = true;
	import_arm_run_project(path);
	pc_recovering = false;
	project_modified = true; // Not saved yet
	return true;
}

// Lays the newest recovered session head over p (the saved file's project): layers, materials, brushes, swatches,
// camera, uids, timelines, history. Meshes, assets and packed assets stay the file's. False without one.
bool project_session_overlay(project_t *p) {
	pc_session_register();
	hash128_t h   = hstore_recovered_last(HSTORE_SESSION);
	u32       len = 0;
	u8       *b   = hash128_is_none(h) ? NULL : hstore_get(h, &len);
	if (b == NULL || len < 4) {
		free(b);
		return false;
	}
	u32 n;
	memcpy(&n, b, 4);
	u32 off = 4 + n * 16;
	if (off > len) {
		free(b);
		return false;
	}
	buffer_t   hb = {.buffer = b + off, .length = len - off, .capacity = len - off};
	project_t *s  = import_arm_is_old(&hb) ? import_arm_from_old(&hb) : armpack_decode(&hb);
	if (s == NULL || s->layer_datas == NULL || s->layer_datas->length == 0) {
		free(b);
		return false;
	}
	p->assets                 = s->assets;
	p->swatches               = s->swatches;
	p->brush_nodes            = s->brush_nodes;
	p->brush_icons            = s->brush_icons;
	p->material_nodes         = s->material_nodes;
	p->material_groups        = s->material_groups;
	p->material_icons         = s->material_icons; // Empty: previews render again
	p->material_datas         = s->material_datas;
	p->font_assets            = s->font_assets;
	p->layer_datas            = s->layer_datas;
	p->camera_world           = s->camera_world;
	p->camera_origin          = s->camera_origin;
	p->camera_fov             = s->camera_fov;
	p->linear_blending        = s->linear_blending;
	p->id_map                 = s->id_map;
	p->mesh_maps              = s->mesh_maps;
	p->next_uid               = s->next_uid;
	p->timelines              = s->timelines;
	p->history                = s->history;
	for (i32 i = 0; i < p->layer_datas->length; ++i) { // Layers from groups: empty pixel buffers, as pc_load
		layer_data_t *ld = p->layer_datas->buffer[i];
		u8_array_t   *tm = (u8_array_t *)ld->tilemap;
		if (tm == NULL || tm->length < 16) {
			continue;
		}
		hash128_t mh       = pc_hash_at(tm, 0);
		u32       mlen     = 0;
		u8       *map      = hash128_is_none(mh) ? NULL : hstore_get(mh, &mlen);
		i32       channels = map != NULL && mlen >= 2 ? map[1] : 3; // Map header: tag, channels
		free(map);
		ld->texpaint      = (struct buffer *)buffer_create(0);
		ld->texpaint_nor  = channels == 3 ? (struct buffer *)buffer_create(0) : NULL;
		ld->texpaint_pack = channels == 3 ? (struct buffer *)buffer_create(0) : NULL;
	}
	return true; // b stays: the decoded head may point into it
}
