#include "../global.h"

// Project files, version 31 with project_v31 on: a container of chunks (history_store.c), read lazily, saved by
// appending. Little endian:
//   "MSPJ" u32 31, 8 bytes 0
//   chunks: "CHNK" ... (HSTORE_CHUNK_HEADER), live layer textures (HSTORE_RASTER), packed assets (HSTORE_ASSET), the
//           referenced history blobs
//   head:   the project as armpack (util_encode_project), layers with the hashes of their rasters instead of pixels,
//           packed assets with the hash of their bytes
//   index:  the chunks the head and history use, sorted by hash: u64 lo, u64 hi, u64 offset, u32 raw_len,
//           u32 comp_len, u8 kind, u8 codec, 6 bytes 0
//   footer: "MSPF" u32 31, u64 head_off, head_len, index_off, index_len, prev_footer, u32 crc32(head, index), u32 0,
//           u32 0, u32 crc32(the 60 bytes before)
// A save appends the chunks the file lacks, then a head, an index and a footer: until that footer lands the previous
// one is the file's state (a torn tail is skipped back over). Opening reads the footer, the head, the index and the live
// rasters; history blobs are read when used. Save As, compaction (40% dead bytes) and files outside the app (iCloud and
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
	i64                   mtime;
	u64                   footer_off;
	hstore_index_entry_t *index;
	i32                   count;
	i64                   live; // Bytes the last footer uses
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

static void pc_state_free(pc_state_t *st) {
	free(st->path);
	free(st->index);
	*st = (pc_state_t){0};
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
			for (i32 c = 0; c < 3; ++c) {
				hash128_t h = pc_hash_at(ld->rasters, c);
				if (!hash128_is_none(h)) {
					hstore_chunk_t chunk;
					*dst[c] = pc_payload(s, index, count, h, &chunk);
					if (*dst[c] == NULL) {
						*dst[c] = buffer_create(0); // Missing: the layer starts empty
					}
				}
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
		pc_state_free(st);
		st->index      = index;
		st->count      = count;
		st->footer_off = at;
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

static i64 pc_mtime_of(struct stat *st) {
	return (i64)st->st_mtimespec.tv_sec * 1000000000 + st->st_mtimespec.tv_nsec;
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
	project_t *project = pc_load(&s, &pc_opened);
	close(fd);
	if (project != NULL) {
		pc_opened.path  = string_copy(path);
		pc_opened.size  = (i64)st.st_size;
		pc_opened.mtime = pc_mtime_of(&st);
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

// Saved masks are one channel
bool project_container_mask_r8(layer_data_t *ld) {
	return ld->rasters != NULL;
}

// ---- Writing ----

bool project_container_writing() {
	return pc_w != NULL;
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

static hash128_t pc_put_raster(gpu_texture_t *t, bool mask) {
	buffer_t *px  = gpu_get_texture_pixels(t);
	buffer_t *raw = px;
	if (mask) { // One channel, the mask's value (the texture is RGBA32 for undo, its channels are equal)
		raw = buffer_create(px->length / 4);
		for (u32 i = 0; i < raw->length; ++i) {
			raw->buffer[i] = px->buffer[i * 4];
		}
	}
	hash128_t h = hstore_hash(raw->buffer, raw->length);
	if (!pc_has(h)) {
		if (pc_find(pc_w->old, pc_w->old_count, h) != NULL) {
			pc_put(HSTORE_RASTER, HSTORE_CODEC_LZ4, h, raw->length, NULL, 0);
		}
		else {
			buffer_t *enc = lz4_encode(raw); // Always LZ4: the load reads it as version 30 reads layers
			pc_put(HSTORE_RASTER, HSTORE_CODEC_LZ4, h, raw->length, enc->buffer, enc->length);
			array_free(enc);
			free(enc);
		}
	}
	gpu_release_texture_pixels(t);
	if (raw != px) {
		array_free(raw);
		free(raw);
	}
	return h;
}

// The hashes of a layer's textures, written as chunks of the save (layer_data_t.rasters), NULL for a group
u8_array_t *project_container_layer_rasters(slot_layer_t *l) {
	if (l->texpaint == NULL) {
		return NULL;
	}
	u8_array_t    *a      = u8_array_create(48);
	gpu_texture_t *ts[3]  = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
	bool           mask   = slot_layer_is_mask(l);
	for (i32 c = 0; c < 3; ++c) {
		hash128_t h = ts[c] != NULL ? pc_put_raster(ts[c], mask && c == 0) : HASH128_NONE;
		memcpy(a->buffer + c * 16, &h.lo, 8);
		memcpy(a->buffer + c * 16 + 8, &h.hi, 8);
	}
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

static void pc_put_blob(hash128_t h, void *ud) {
	if (pc_find(pc_w->old, pc_w->old_count, h) != NULL) {
		pc_put(0, 0, h, 0, NULL, 0);
		return;
	}
	hstore_chunk_t c;
	u8            *payload = hstore_chunk_payload(h, &c);
	if (payload != NULL) {
		pc_put(c.kind, c.codec, h, c.raw_len, payload, c.comp_len);
		free(payload);
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

// Whether a save of path appends: the file is the one opened or saved last, unchanged since, local, and mostly live
static bool pc_can_append(char *path) {
	struct stat st;
	if (pc.path == NULL || !string_equals(pc.path, path) || stat(path, &st) != 0) {
		return false;
	}
	if ((i64)st.st_size != pc.size || pc_mtime_of(&st) != pc.mtime) {
		return false; // Changed by something else, or a failed append
	}
	return pc.live >= pc.size * 6 / 10 && pc_local(path);
}

i32 project_container_last_save_appended = 0; // 1 when the last save appended (tests)

// Saves the project as a container: appended to the file it came from, else written whole
bool project_container_save(char *path) {
	bool                append = pc_can_append(path);
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

	pc_writer_t w = {.s = s, .old = append ? pc.index : NULL, .old_count = append ? pc.count : 0};
	pc_w          = &w;
	buffer_t *head = export_arm_encode_project(path); // Writes the rasters and assets
	hstore_each(pc_put_blob, NULL);
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
	if (!ok) {
		free(w.index);
		pc.size = -1; // The next save writes the file whole
		return false;
	}

	struct stat st;
	stat(path, &st);
	char *p = string_copy(path);
	pc_state_free(&pc);
	pc.path       = p;
	pc.size       = (i64)st.st_size;
	pc.mtime      = pc_mtime_of(&st);
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

// A new project: saves do not append to the file of the one before
void project_container_forget() {
	pc_state_free(&pc);
}

// The uuid saved in a project file (version 31), NULL when it has none
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
	buffer_t *b = iron_load_blob(path);
	if (b == NULL || !import_arm_has_version(b)) {
		return NULL;
	}
	any_map_t *m    = armpack_decode_to_map(b);
	return m != NULL ? string_copy(any_map_get(m, "uuid")) : NULL;
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
