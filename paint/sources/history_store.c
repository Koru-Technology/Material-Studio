#include "global.h"

// Content-addressed blobs of the stroke recorded history (stroke_rec.h): an open addressed table of entries keyed by
// hash, payloads LZ4 compressed in RAM (T1) and appended to a journal file per project (T2). Main thread only.
//
// Chunk, the unit of the journal and of the v31 container (io/project_container.c), little endian:
//   "CHNK" u8 kind, u8 codec, u16 flags, u64 hash.lo, u64 hash.hi, u32 raw_len, u32 comp_len, comp_len payload bytes
// A chunk is valid when it fits in the file and its decoded payload hashes to its hash.
//
// Journal: <Application Support>/journals/<project uuid>.msj, "MSJN" u32 version u32 0 u32 0, then chunks. A mark
// chunk (kind 0, HSTORE_CHUNK_MARK) is appended when the project is saved: chunks after the last mark are unsaved work,
// a crash left them when the project is opened again (hstore_journal_pending, hstore_recover).

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef IRON_IOS
#include <os/proc.h>
#endif

#define HSTORE_MB          (1024 * 1024)
#define HSTORE_RAW_BELOW   64 // Smaller payloads are not compressed (lz4_encode reads past inputs under 12 bytes)
#define HSTORE_JOURNAL_VER 31

enum {
	HSTORE_E_RECOVERED = 1, // Indexed by hstore_recover, kept by trim until hstore_recover_end
	HSTORE_E_MARK      = 2, // Reached by hstore_each
	HSTORE_E_UNOWNED   = 4, // Holds no references on its children (indexed from disk, or unreached by a rebuild): its
	                        // removal does not drop theirs
	HSTORE_E_PENDING   = 8, // Reached by a rebuild while only on disk: holds its references, its children are counted
	                        // when its bytes are read (hstore_get, hstore_put) or at a trim with io (opening reads none)
};

typedef struct hstore_entry {
	hash128_t h;        // HASH128_NONE: empty slot
	u8       *ram;      // Compressed payload, NULL when only in the journal
	i64       disk_off; // Chunk offset in the journal, -1 when not journaled
	u32       refs;
	u32       raw_len;
	u32       comp_len;
	u32       tick; // Last use, spills go oldest first
	u8        kind;
	u8        codec;
	u8        state;
	u8        src; // File of disk_off: HSTORE_SRC_JOURNAL or HSTORE_SRC_PROJECT
} hstore_entry_t;

enum {
	HSTORE_SRC_JOURNAL = 0,
	HSTORE_SRC_PROJECT = 1, // The v31 project file it was opened from or saved to (io/project_container.c)
};

static hstore_entry_t *hstore_table       = NULL;
static u32             hstore_cap         = 0; // Power of two
static u32             hstore_count       = 0;
static u32             hstore_tick        = 0;
static i64             hstore_ram         = 0;
static i64             hstore_dirty       = 0;
static i64             hstore_dead        = 0;
static i64             hstore_budget_ram  = 0; // Refreshed by trim and reset
static int             hstore_fd          = -1;
static char           *hstore_fd_path     = NULL;
static i64             hstore_jsize       = 0;
static i64             hstore_jmark       = 0; // Journal offset after the last mark chunk
static bool            hstore_recovering  = false;
static f64             hstore_flush_ms    = 0.0; // Duration of the last hstore_flush
static i64             hstore_disk_reads  = 0;   // Chunks read back from the journal or the project
static int             hstore_pfd         = -1;  // The v31 project file
static bool            hstore_jowned      = false; // The journal holds this session's writes: reset deletes it

// ---- Chunks ----

void hstore_chunk_header_write(u8 *p, u8 kind, u8 codec, u16 flags, hash128_t h, u32 raw_len, u32 comp_len) {
	memcpy(p, "CHNK", 4);
	p[4] = kind;
	p[5] = codec;
	memcpy(p + 6, &flags, 2);
	memcpy(p + 8, &h.lo, 8);
	memcpy(p + 16, &h.hi, 8);
	memcpy(p + 24, &raw_len, 4);
	memcpy(p + 28, &comp_len, 4);
}

bool hstore_chunk_header_read(const u8 *p, hstore_chunk_t *c) {
	if (memcmp(p, "CHNK", 4) != 0) {
		return false;
	}
	c->kind  = p[4];
	c->codec = p[5];
	memcpy(&c->flags, p + 6, 2);
	memcpy(&c->h.lo, p + 8, 8);
	memcpy(&c->h.hi, p + 16, 8);
	memcpy(&c->raw_len, p + 24, 4);
	memcpy(&c->comp_len, p + 28, 4);
	return c->codec <= HSTORE_CODEC_LZ4;
}

// The raw bytes of a payload (malloc'd, at least 1 byte), NULL when it does not decode to raw_len bytes of hash h
u8 *hstore_chunk_decode(const hstore_chunk_t *c, const u8 *payload, bool verify) {
	u8 *out = NULL;
	if (c->codec == HSTORE_CODEC_RAW) {
		if (c->comp_len != c->raw_len) {
			return NULL;
		}
		out = malloc(c->raw_len > 0 ? c->raw_len : 1);
		memcpy(out, payload, c->raw_len);
	}
	else {
		buffer_t  in  = {.buffer = (u8 *)payload, .length = c->comp_len, .capacity = c->comp_len};
		buffer_t *dec = lz4_decode(&in, c->raw_len);
		if (dec == NULL) {
			return NULL;
		}
		out = dec->buffer;
		free(dec);
	}
	if (verify && !hash128_eq(hstore_hash(out, c->raw_len), c->h)) {
		free(out);
		return NULL;
	}
	return out;
}

// The payload of data, compressed when that makes it smaller (malloc'd)
static u8 *hstore_encode(const void *data, u32 len, u32 *comp_len, u8 *codec) {
	if (len >= HSTORE_RAW_BELOW) {
		buffer_t  in  = {.buffer = (u8 *)data, .length = len, .capacity = len};
		buffer_t *enc = lz4_encode(&in);
		if (enc != NULL && enc->length < len) {
			u8 *p     = enc->buffer;
			*comp_len = enc->length;
			*codec    = HSTORE_CODEC_LZ4;
			free(enc);
			return p;
		}
		if (enc != NULL) {
			array_free(enc);
			free(enc);
		}
	}
	u8 *p = malloc(len > 0 ? len : 1);
	memcpy(p, data, len);
	*comp_len = len;
	*codec    = HSTORE_CODEC_RAW;
	return p;
}

hash128_t hstore_hash(const void *data, u32 len) {
	hash128_t h = hash128(data, len);
	h.hi &= ~HASH128_INLINE_BIT;
	return h;
}

static bool hstore_is_stored_hash(hash128_t h) {
	return !hash128_is_none(h) && (h.hi & HASH128_INLINE_BIT) == 0;
}

// ---- Table ----

static u32 hstore_slot_of(hash128_t h) {
	return (u32)h.lo & (hstore_cap - 1);
}

static hstore_entry_t *hstore_find(hash128_t h) {
	if (hstore_cap == 0 || !hstore_is_stored_hash(h)) {
		return NULL;
	}
	for (u32 i = hstore_slot_of(h);; i = (i + 1) & (hstore_cap - 1)) {
		hstore_entry_t *e = &hstore_table[i];
		if (hash128_is_none(e->h)) {
			return NULL;
		}
		if (hash128_eq(e->h, h)) {
			return e;
		}
	}
}

static hstore_entry_t *hstore_insert_slot(hash128_t h) {
	for (u32 i = hstore_slot_of(h);; i = (i + 1) & (hstore_cap - 1)) {
		if (hash128_is_none(hstore_table[i].h)) {
			return &hstore_table[i];
		}
	}
}

static void hstore_grow() {
	hstore_entry_t *old     = hstore_table;
	u32             old_cap = hstore_cap;
	hstore_cap              = hstore_cap == 0 ? 1024 : hstore_cap * 2;
	hstore_table            = calloc(hstore_cap, sizeof(hstore_entry_t));
	for (u32 i = 0; i < old_cap; ++i) {
		if (!hash128_is_none(old[i].h)) {
			*hstore_insert_slot(old[i].h) = old[i];
		}
	}
	free(old);
}

static hstore_entry_t *hstore_insert(hash128_t h) {
	if ((hstore_count + 1) * 10 > hstore_cap * 7) {
		hstore_grow();
	}
	hstore_entry_t *e = hstore_insert_slot(h);
	memset(e, 0, sizeof(*e));
	e->h        = h;
	e->disk_off = -1;
	hstore_count++;
	return e;
}

// Frees the entry and closes the gap it leaves in its probe run (backward shift, no tombstones)
static void hstore_remove(hstore_entry_t *e) {
	if (e->ram != NULL) {
		hstore_ram -= e->comp_len;
		if (e->disk_off < 0) {
			hstore_dirty -= e->comp_len;
		}
		free(e->ram);
	}
	if (e->disk_off >= 0 && e->src == HSTORE_SRC_JOURNAL) {
		hstore_dead += HSTORE_CHUNK_HEADER + e->comp_len;
	}
	hstore_count--;
	u32 mask = hstore_cap - 1;
	u32 i    = (u32)(e - hstore_table);
	u32 j    = i;
	while (true) {
		hstore_table[i].h = HASH128_NONE;
		while (true) {
			j = (j + 1) & mask;
			if (hash128_is_none(hstore_table[j].h)) {
				return;
			}
			u32 k = hstore_slot_of(hstore_table[j].h);
			if (i <= j ? (i < k && k <= j) : (i < k || k <= j)) {
				continue; // Its home is between the gap and it, it stays
			}
			break;
		}
		hstore_table[i] = hstore_table[j];
		i               = j;
	}
}

// ---- Budgets ----

// iOS: a tenth of the memory the system still lets the app use, 0 when unknown or elsewhere
static i64 hstore_ram_cap() {
#ifdef IRON_IOS
	if (__builtin_available(iOS 13.0, *)) {
		return (i64)os_proc_available_memory() / 10;
	}
#endif
	return 0;
}

static i64 hstore_ram_budget_now() {
	i64 b   = (i64)g_config->history_ram_mb * HSTORE_MB;
	i64 cap = hstore_ram_cap();
	return cap > 0 && b > cap ? cap : b;
}

static i64 hstore_disk_budget() {
	return (i64)g_config->history_disk_mb * HSTORE_MB;
}

// ---- Journal ----

static char *hstore_journal_dir() {
	return string("%sjournals", iron_internal_save_path());
}

char *hstore_journal_path(char *uuid) {
	return string("%s/%s.msj", hstore_journal_dir(), uuid);
}

static bool hstore_pread_all(int fd, void *p, i64 len, i64 off) {
	u8 *b = p;
	while (len > 0) {
		ssize_t n = pread(fd, b, (size_t)len, (off_t)off);
		if (n <= 0) {
			return false;
		}
		b += n;
		len -= n;
		off += n;
	}
	return true;
}

static bool hstore_pwrite_all(int fd, const void *p, i64 len, i64 off) {
	const u8 *b = p;
	while (len > 0) {
		ssize_t n = pwrite(fd, b, (size_t)len, (off_t)off);
		if (n <= 0) {
			return false;
		}
		b += n;
		len -= n;
		off += n;
	}
	return true;
}

static bool hstore_journal_header_valid(int fd) {
	u8 head[16];
	return hstore_pread_all(fd, head, 16, 0) && memcmp(head, "MSJN", 4) == 0;
}

static bool hstore_journal_header_write(int fd) {
	u8  head[16] = {'M', 'S', 'J', 'N'};
	u32 version  = HSTORE_JOURNAL_VER;
	memcpy(head + 4, &version, 4);
	return hstore_pwrite_all(fd, head, 16, 0);
}

typedef struct hstore_scan {
	i64 end;  // Offset after the last whole chunk
	i64 mark; // Offset after the last mark chunk
	i32 after_mark; // Chunks after it
} hstore_scan_t;

// Walks the chunk headers of a journal (their payloads are not read): where the whole chunks end, where the last mark is
static hstore_scan_t hstore_journal_scan(int fd, i64 size) {
	hstore_scan_t s = {.end = HSTORE_JOURNAL_HEADER, .mark = HSTORE_JOURNAL_HEADER, .after_mark = 0};
	u8            p[HSTORE_CHUNK_HEADER];
	while (s.end + HSTORE_CHUNK_HEADER <= size) {
		hstore_chunk_t c;
		if (!hstore_pread_all(fd, p, HSTORE_CHUNK_HEADER, s.end) || !hstore_chunk_header_read(p, &c)) {
			break;
		}
		i64 next = s.end + HSTORE_CHUNK_HEADER + c.comp_len;
		if (next > size) {
			break; // Torn tail
		}
		s.end = next;
		if (c.kind == 0 && (c.flags & HSTORE_CHUNK_MARK)) {
			s.mark       = s.end;
			s.after_mark = 0;
		}
		else {
			s.after_mark++;
		}
	}
	return s;
}

static void hstore_journal_close() {
	if (hstore_fd >= 0) {
		close(hstore_fd);
		hstore_fd = -1;
	}
	free(hstore_fd_path);
	hstore_fd_path = NULL;
	hstore_jsize   = 0;
	hstore_jmark   = 0;
}

// Opens the journal of the project for appending: an existing one is kept up to its last whole chunk, its chunks count
// as dead until hstore_recover indexes them
static bool hstore_journal_open() {
	if (hstore_fd >= 0) {
		return true;
	}
	iron_create_directory(hstore_journal_dir());
	char *path = hstore_journal_path(project_uuid_get());
	int   fd   = open(path, O_RDWR | O_CREAT, 0644);
	if (fd < 0) {
		iron_log("Could not open the history journal %s", path);
		return false;
	}
	struct stat st;
	i64         size = fstat(fd, &st) == 0 ? (i64)st.st_size : 0;
	if (size < HSTORE_JOURNAL_HEADER || !hstore_journal_header_valid(fd)) {
		if (ftruncate(fd, 0) != 0 || !hstore_journal_header_write(fd)) {
			close(fd);
			return false;
		}
		hstore_jsize  = HSTORE_JOURNAL_HEADER;
		hstore_jmark  = HSTORE_JOURNAL_HEADER;
		hstore_jowned = true;
	}
	else { // Left by a session that did not end: kept by reset until this one writes to it
		hstore_scan_t s = hstore_journal_scan(fd, size);
		if (s.end < size && ftruncate(fd, (off_t)s.end) != 0) {
			close(fd);
			return false;
		}
		hstore_jsize  = s.end;
		hstore_jmark  = s.mark;
		hstore_jowned = false;
		hstore_dead += s.end - HSTORE_JOURNAL_HEADER;
	}
	hstore_fd      = fd;
	hstore_fd_path = string_copy(path);
	return true;
}

// Appends the chunk of a blob held in RAM
static bool hstore_journal_append(hstore_entry_t *e) {
	if (!hstore_journal_open()) {
		return false;
	}
	u8 head[HSTORE_CHUNK_HEADER];
	hstore_chunk_header_write(head, e->kind, e->codec, 0, e->h, e->raw_len, e->comp_len);
	i64 off = hstore_jsize;
	if (!hstore_pwrite_all(hstore_fd, head, HSTORE_CHUNK_HEADER, off) || !hstore_pwrite_all(hstore_fd, e->ram, e->comp_len, off + HSTORE_CHUNK_HEADER)) {
		if (ftruncate(hstore_fd, (off_t)off) != 0) { // A full disk: the blob stays dirty in RAM
			iron_log("History journal truncate failed");
		}
		return false;
	}
	hstore_jsize  = off + HSTORE_CHUNK_HEADER + e->comp_len;
	hstore_jowned = true;
	e->disk_off   = off;
	hstore_dirty -= e->comp_len;
	return true;
}

// The compressed payload of a blob on disk, in the journal or the project (malloc'd)
static u8 *hstore_journal_read_payload(hstore_entry_t *e) {
	int fd = e->src == HSTORE_SRC_PROJECT ? hstore_pfd : hstore_fd;
	if (fd < 0 || e->disk_off < 0) {
		return NULL;
	}
	u8 *p = malloc(e->comp_len > 0 ? e->comp_len : 1);
	if (!hstore_pread_all(fd, p, e->comp_len, e->disk_off + HSTORE_CHUNK_HEADER)) {
		free(p);
		return NULL;
	}
	hstore_disk_reads++;
	return p;
}

static int hstore_compare_tick(const void *a, const void *b) {
	u32 ta = (*(hstore_entry_t *const *)a)->tick;
	u32 tb = (*(hstore_entry_t *const *)b)->tick;
	return ta < tb ? -1 : ta > tb ? 1 : 0;
}

static int hstore_compare_disk_off(const void *a, const void *b) {
	i64 oa = (*(hstore_entry_t *const *)a)->disk_off;
	i64 ob = (*(hstore_entry_t *const *)b)->disk_off;
	return oa < ob ? -1 : oa > ob ? 1 : 0;
}

// Entries matching the filter, sorted by cmp: pointers into the table, valid until it changes
static hstore_entry_t **hstore_collect(bool (*filter)(hstore_entry_t *), int (*cmp)(const void *, const void *), u32 *n) {
	hstore_entry_t **list = malloc(sizeof(hstore_entry_t *) * (hstore_count > 0 ? hstore_count : 1));
	*n                    = 0;
	for (u32 i = 0; i < hstore_cap; ++i) {
		hstore_entry_t *e = &hstore_table[i];
		if (!hash128_is_none(e->h) && filter(e)) {
			list[(*n)++] = e;
		}
	}
	if (cmp != NULL && *n > 1) {
		qsort(list, *n, sizeof(hstore_entry_t *), cmp);
	}
	return list;
}

static bool hstore_is_in_ram(hstore_entry_t *e) {
	return e->ram != NULL;
}

static bool hstore_is_dirty(hstore_entry_t *e) {
	return e->ram != NULL && e->disk_off < 0;
}

static bool hstore_is_journaled(hstore_entry_t *e) {
	return e->disk_off >= 0 && e->src == HSTORE_SRC_JOURNAL;
}

static bool hstore_is_referenced(hstore_entry_t *e) {
	return e->refs > 0;
}

static bool hstore_is_garbage(hstore_entry_t *e) {
	return e->refs == 0 && (e->state & HSTORE_E_RECOVERED) == 0;
}

static void hstore_drop_ram(hstore_entry_t *e) {
	hstore_ram -= e->comp_len;
	free(e->ram);
	e->ram = NULL;
}

// Journals and drops RAM copies, least recently used first, until RAM holds target bytes
static void hstore_spill(i64 target) {
	if (hstore_ram <= target) {
		return;
	}
	u32              n;
	hstore_entry_t **list = hstore_collect(hstore_is_in_ram, hstore_compare_tick, &n);
	for (u32 i = 0; i < n && hstore_ram > target; ++i) {
		hstore_entry_t *e = list[i];
		if (e->disk_off < 0 && !hstore_journal_append(e)) {
			break;
		}
		hstore_drop_ram(e);
	}
	free(list);
}

// Rewrites the journal with the chunks of stored blobs only (copied verbatim), the mark kept between saved and unsaved
static void hstore_journal_compact() {
	if (hstore_fd < 0) {
		return;
	}
	char *tmp = string("%s.compact", hstore_fd_path);
	int   fd  = open(tmp, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0 || !hstore_journal_header_write(fd)) {
		if (fd >= 0) {
			close(fd);
		}
		return;
	}
	u32              n;
	hstore_entry_t **list = hstore_collect(hstore_is_journaled, hstore_compare_disk_off, &n);
	i64             *offs = malloc(sizeof(i64) * (n > 0 ? n : 1));
	i64              size = HSTORE_JOURNAL_HEADER;
	i64              mark = HSTORE_JOURNAL_HEADER;
	bool             ok   = true;
	bool             mark_written = hstore_jmark <= HSTORE_JOURNAL_HEADER;
	u8              *buf  = NULL;
	for (u32 i = 0; i <= n && ok; ++i) {
		if (!mark_written && (i == n || list[i]->disk_off >= hstore_jmark)) {
			u8 head[HSTORE_CHUNK_HEADER];
			hstore_chunk_header_write(head, 0, HSTORE_CODEC_RAW, HSTORE_CHUNK_MARK, HASH128_NONE, 0, 0);
			ok           = hstore_pwrite_all(fd, head, HSTORE_CHUNK_HEADER, size);
			size        += HSTORE_CHUNK_HEADER;
			mark         = size;
			mark_written = true;
		}
		if (i == n || !ok) {
			break;
		}
		i64 len = HSTORE_CHUNK_HEADER + list[i]->comp_len;
		buf     = realloc(buf, (size_t)len);
		ok      = hstore_pread_all(hstore_fd, buf, len, list[i]->disk_off) && hstore_pwrite_all(fd, buf, len, size);
		offs[i] = size;
		size += len;
	}
	free(buf);
	ok = ok && fsync(fd) == 0;
	close(fd);
	if (ok && rename(tmp, hstore_fd_path) == 0) {
		for (u32 i = 0; i < n; ++i) {
			list[i]->disk_off = offs[i];
		}
		close(hstore_fd);
		hstore_fd    = open(hstore_fd_path, O_RDWR);
		hstore_jsize = size;
		hstore_jmark = mark;
		hstore_dead  = 0;
	}
	else {
		unlink(tmp);
	}
	free(list);
	free(offs);
}

static void hstore_free_table() {
	for (u32 i = 0; i < hstore_cap; ++i) {
		if (!hash128_is_none(hstore_table[i].h)) { // Empty slots keep stale pointers
			free(hstore_table[i].ram);
		}
	}
	free(hstore_table);
}

// ---- Children (blobs owning blobs) ----

#define HSTORE_KINDS 32

static hstore_children_fn hstore_children[HSTORE_KINDS];
static hash128_t          hstore_recovered_kind[HSTORE_KINDS]; // Last chunk of each kind hstore_recover read

typedef struct hstore_list {
	hash128_t *h;
	u32        n;
	u32        cap;
} hstore_list_t;

static void hstore_list_push(hstore_list_t *l, hash128_t h) {
	if (l->n == l->cap) {
		l->cap = l->cap == 0 ? 256 : l->cap * 2;
		l->h   = realloc(l->h, sizeof(hash128_t) * l->cap);
	}
	l->h[l->n++] = h;
}

static hstore_children_fn hstore_children_of(u8 kind) {
	return kind < HSTORE_KINDS ? hstore_children[kind] : NULL;
}

// The raw bytes of a blob (malloc'd), NULL when unreadable or, without io, only in the journal
static u8 *hstore_entry_bytes(hstore_entry_t *e, bool io) {
	hstore_chunk_t c = {.kind = e->kind, .codec = e->codec, .h = e->h, .raw_len = e->raw_len, .comp_len = e->comp_len};
	if (e->ram != NULL) {
		return hstore_chunk_decode(&c, e->ram, false);
	}
	if (!io) {
		return NULL;
	}
	u8 *payload = hstore_journal_read_payload(e);
	if (payload == NULL) {
		return NULL;
	}
	u8 *out = hstore_chunk_decode(&c, payload, true);
	free(payload);
	return out;
}

static void hstore_visit_ref(hash128_t child, void *ud) {
	hstore_ref(child);
}

// Unrefs a child of a removed blob, an orphan joins the worklist
static void hstore_visit_unref(hash128_t child, void *ud) {
	hstore_entry_t *e = hstore_find(child);
	if (e != NULL && e->refs > 0 && --e->refs == 0) {
		hstore_list_push(ud, child);
	}
}

// Refs a reachable blob, the first ref queues its children (counted: it owns them)
static void hstore_visit_reach(hash128_t child, void *ud) {
	hstore_entry_t *e = hstore_find(child);
	if (e != NULL && ++e->refs == 1) {
		e->state &= ~HSTORE_E_UNOWNED;
		hstore_list_push(ud, child);
	}
}

// Counts the children of the reached blobs of the worklist, and theirs. Without io, a blob only on disk is left
// pending: its children are counted when its bytes are read.
static void hstore_reach_drain(hstore_list_t *work, bool io) {
	while (work->n > 0) {
		hstore_entry_t    *e  = hstore_find(work->h[--work->n]);
		hstore_children_fn fn = e != NULL ? hstore_children_of(e->kind) : NULL;
		if (fn == NULL) {
			continue;
		}
		if (e->ram == NULL && !io) {
			e->state |= HSTORE_E_PENDING;
			continue;
		}
		e->state &= ~HSTORE_E_PENDING;
		u8 *raw = hstore_entry_bytes(e, io);
		if (raw != NULL) {
			fn(raw, e->raw_len, hstore_visit_reach, work); // Changes refs only, e stays valid
			free(raw);
		}
	}
}

// A pending blob's bytes were read: its children are counted
static void hstore_resolve(hstore_entry_t *e, const u8 *raw, u32 len) {
	e->state &= ~HSTORE_E_PENDING;
	hstore_children_fn fn = hstore_children_of(e->kind);
	if (fn == NULL) {
		return;
	}
	hstore_list_t work = {0};
	fn(raw, len, hstore_visit_reach, &work);
	hstore_reach_drain(&work, false);
	free(work.h);
}

static bool hstore_is_pending(hstore_entry_t *e) {
	return (e->state & HSTORE_E_PENDING) != 0;
}

// The children of the pending blobs are counted, reading them (with io). False when some stay pending.
static bool hstore_resolve_pending(bool io) {
	u32              n;
	hstore_entry_t **list = hstore_collect(hstore_is_pending, NULL, &n);
	hstore_list_t    work = {0};
	for (u32 i = 0; io && i < n; ++i) {
		hstore_list_push(&work, list[i]->h);
	}
	free(list);
	hstore_reach_drain(&work, true);
	free(work.h);
	return io || n == 0;
}

void hstore_set_children(hstore_kind_t kind, hstore_children_fn fn) {
	if ((u32)kind < HSTORE_KINDS) {
		hstore_children[kind] = fn;
	}
}

// Removes the unreferenced blobs and, cascading, the children only they held. Without io, a blob with children whose
// bytes are only in the journal waits for a trim with io.
static void hstore_collect_garbage(bool io) {
	if (!hstore_resolve_pending(io)) {
		return; // Unreferenced blobs may be children of a pending one
	}
	hstore_list_t    work = {0};
	u32              n;
	hstore_entry_t **list = hstore_collect(hstore_is_garbage, NULL, &n);
	for (u32 i = 0; i < n; ++i) {
		hstore_list_push(&work, list[i]->h);
	}
	free(list);
	while (work.n > 0) {
		hash128_t       h = work.h[--work.n];
		hstore_entry_t *e = hstore_find(h); // By hash: a removal moves entries
		if (e == NULL || !hstore_is_garbage(e)) {
			continue;
		}
		hstore_children_fn fn = (e->state & HSTORE_E_UNOWNED) == 0 ? hstore_children_of(e->kind) : NULL;
		if (fn != NULL) {
			if (e->ram == NULL && !io) {
				continue; // Deferred
			}
			u8 *raw = hstore_entry_bytes(e, io);
			if (raw != NULL) { // Unreadable: its children stay until hstore_rebuild_refs
				fn(raw, e->raw_len, hstore_visit_unref, &work); // Changes refs only, e stays valid
				free(raw);
			}
		}
		hstore_remove(e);
	}
	free(work.h);
}

void hstore_rebuild_refs(const hash128_t *roots, i32 n) {
	for (u32 i = 0; i < hstore_cap; ++i) {
		hstore_table[i].refs = 0;
		hstore_table[i].state &= ~(HSTORE_E_RECOVERED | HSTORE_E_PENDING);
		hstore_table[i].state |= HSTORE_E_UNOWNED; // Until reached: then its children are counted
	}
	hstore_recovering  = false;
	hstore_list_t work = {0};
	for (i32 i = 0; i < n; ++i) {
		hstore_visit_reach(roots[i], &work);
	}
	hstore_reach_drain(&work, false); // Blobs only on disk are read for their children when used, or at a trim
	free(work.h);
}

// ---- API (stroke_rec.h) ----

hash128_t hstore_put(hstore_kind_t kind, const void *data, u32 len) {
	hash128_t       h = hstore_hash(data, len);
	hstore_entry_t *e = hstore_find(h);
	if (e != NULL) {
		e->refs++;
		e->tick = ++hstore_tick;
		if (e->state & HSTORE_E_UNOWNED) { // Read from disk and not owning its children yet: it does from now on
			e->state &= ~HSTORE_E_UNOWNED;
			hstore_children_fn fn = hstore_children_of((u8)kind);
			if (fn != NULL) {
				fn(data, len, hstore_visit_ref, NULL);
			}
		}
		else if (e->state & HSTORE_E_PENDING) {
			hstore_resolve(e, data, len);
		}
		return h;
	}
	if (hstore_budget_ram == 0) {
		hstore_budget_ram = hstore_ram_budget_now();
	}
	e           = hstore_insert(h);
	e->kind     = (u8)kind;
	e->raw_len  = len;
	e->refs     = 1;
	e->tick     = ++hstore_tick;
	e->ram      = hstore_encode(data, len, &e->comp_len, &e->codec);
	hstore_ram += e->comp_len;
	hstore_dirty += e->comp_len;
	hstore_children_fn fn = hstore_children_of((u8)kind);
	if (fn != NULL) {
		fn(data, len, hstore_visit_ref, NULL); // Refs only, e stays valid
	}
	if (hstore_ram > hstore_budget_ram) {
		hstore_spill(hstore_budget_ram * 9 / 10); // Moves table entries: e is not used after this
	}
	return h;
}

u8 *hstore_get(hash128_t h, u32 *len) {
	hstore_entry_t *e = hstore_find(h);
	if (e == NULL) {
		return NULL;
	}
	e->tick          = ++hstore_tick;
	hstore_chunk_t c = {.kind = e->kind, .codec = e->codec, .h = e->h, .raw_len = e->raw_len, .comp_len = e->comp_len};
	u8            *out;
	if (e->ram != NULL) {
		out = hstore_chunk_decode(&c, e->ram, false);
	}
	else {
		u8 *payload = hstore_journal_read_payload(e);
		if (payload == NULL) {
			return NULL;
		}
		out = hstore_chunk_decode(&c, payload, true); // The disk can be corrupt
		if (out != NULL && hstore_ram + e->comp_len <= hstore_budget_ram) {
			e->ram = payload; // Clean copy, undo often reads the same blobs again
			hstore_ram += e->comp_len;
		}
		else {
			free(payload);
		}
	}
	if (out != NULL && (e->state & HSTORE_E_PENDING)) {
		hstore_resolve(e, out, c.raw_len);
	}
	if (out != NULL && len != NULL) {
		*len = c.raw_len;
	}
	return out;
}

bool hstore_has(hash128_t h) {
	return hstore_find(h) != NULL;
}

// hstore_kind_t of a stored blob, 0 when missing
i32 hstore_kind_of(hash128_t h) {
	hstore_entry_t *e = hstore_find(h);
	return e != NULL ? e->kind : 0;
}

void hstore_ref(hash128_t h) {
	hstore_entry_t *e = hstore_find(h);
	if (e != NULL) {
		e->refs++;
	}
}

void hstore_unref(hash128_t h) {
	hstore_entry_t *e = hstore_find(h);
	if (e != NULL && e->refs > 0) {
		e->refs--;
	}
}

// With memory_warning nothing is read from the journal (garbage needing it waits), RAM is journaled and dropped
void hstore_trim(bool memory_warning) {
	hstore_collect_garbage(!memory_warning);

	hstore_budget_ram = hstore_ram_budget_now();
	if (memory_warning) {
		hstore_flush();
		hstore_spill(0);
	}
	else if (hstore_ram > hstore_budget_ram) {
		hstore_spill(hstore_budget_ram * 3 / 4);
	}

	bool mostly_dead = hstore_dead * 2 > hstore_jsize && hstore_jsize > 64 * HSTORE_MB;
	bool over_budget = hstore_jsize > hstore_disk_budget() && hstore_dead > 0;
	if (mostly_dead || over_budget) {
		hstore_journal_compact();
	}
}

void hstore_flush() {
	f64              t = iron_time();
	u32              n;
	hstore_entry_t **list = hstore_collect(hstore_is_dirty, hstore_compare_tick, &n);
	for (u32 i = 0; i < n; ++i) {
		if (!hstore_journal_append(list[i])) {
			break;
		}
	}
	free(list);
	if (n > 0 && hstore_fd >= 0) {
		fsync(hstore_fd);
	}
	hstore_flush_ms = (iron_time() - t) * 1000.0;
}

// Drops every blob, the journal of the closed project is deleted
void hstore_reset() {
	hstore_free_table();
	hstore_table = NULL;
	hstore_cap   = 0;
	hstore_count = 0;
	hstore_ram   = 0;
	hstore_dirty = 0;
	hstore_dead  = 0;
	hstore_tick  = 0;
	if (hstore_fd >= 0) {
		char *path  = string_copy(hstore_fd_path);
		bool  owned = hstore_jowned;
		hstore_journal_close();
		if (owned) {
			unlink(path); // Not one left by a crash that this session only read: it may still be recovered
		}
		free(path);
	}
	if (hstore_pfd >= 0) {
		close(hstore_pfd);
		hstore_pfd = -1;
	}
	memset(hstore_recovered_kind, 0, sizeof(hstore_recovered_kind));
	hstore_recovering = false;
	hstore_budget_ram = hstore_ram_budget_now();
}

// Compressed bytes of every stored blob, wherever it is: what a save with history writes at most
i64 hstore_live_bytes() {
	i64 n = 0;
	for (u32 i = 0; i < hstore_cap; ++i) {
		if (!hash128_is_none(hstore_table[i].h)) {
			n += hstore_table[i].comp_len;
		}
	}
	return n;
}

hstore_stats_t hstore_stats() {
	return (hstore_stats_t){
	    .blobs       = hstore_count,
	    .ram_bytes   = hstore_ram,
	    .disk_bytes  = hstore_fd >= 0 ? hstore_jsize : 0,
	    .dirty_bytes = hstore_dirty,
	    .dead_bytes  = hstore_dead,
	    .ram_budget  = hstore_budget_ram > 0 ? hstore_budget_ram : hstore_ram_budget_now(),
	    .disk_budget = hstore_disk_budget(),
	};
}

// ---- Project files (io/project_container.c) ----

// Blobs of a v31 project file: stored ones read from it from now on, the others indexed to be read on demand, kept
// without references until hstore_rebuild_refs (as recovered ones). Called after an open and after each save.
void hstore_attach_project(char *path, const hstore_index_entry_t *entries, i32 n) {
	if (hstore_pfd >= 0) {
		close(hstore_pfd);
	}
	hstore_pfd = open(path, O_RDONLY);
	if (hstore_pfd < 0) {
		return;
	}
	for (i32 i = 0; i < n; ++i) {
		const hstore_index_entry_t *x = &entries[i];
		if (x->kind == 0 || !hstore_is_stored_hash(x->h)) {
			continue;
		}
		hstore_entry_t *e = hstore_find(x->h);
		if (e == NULL) {
			e           = hstore_insert(x->h);
			e->kind     = x->kind;
			e->codec    = x->codec;
			e->raw_len  = x->raw_len;
			e->comp_len = x->comp_len;
			e->state    = HSTORE_E_RECOVERED | HSTORE_E_UNOWNED;
		}
		else if (e->disk_off >= 0 && e->src == HSTORE_SRC_JOURNAL) {
			continue; // Its journal copy stays
		}
		if (e->ram != NULL && e->disk_off < 0) {
			hstore_dirty -= e->comp_len; // Clean now
		}
		e->src      = HSTORE_SRC_PROJECT;
		e->disk_off = (i64)x->off;
	}
}

static void hstore_visit_mark(hash128_t child, void *ud) {
	hstore_entry_t *e = hstore_find(child);
	if (e != NULL && (e->state & HSTORE_E_MARK) == 0) {
		e->state |= HSTORE_E_MARK;
		hstore_list_push(ud, child);
	}
}

static i64 hstore_each_decodes = 0; // Blobs hstore_each read children from

// Blobs a save writes: the roots, the referenced blobs (unless !referenced) and what they own (children), each once. Refs alone miss
// children a blob recovered or read from a project file owns: their refs come back with hstore_rebuild_refs only.
// fn returns whether to go into the children of a blob (not for one the file has with its children already).
void hstore_each(const hash128_t *roots, i32 n, bool referenced, bool (*fn)(hash128_t h, void *ud), void *ud) {
	hstore_list_t work = {0};
	for (u32 i = 0; i < hstore_cap; ++i) {
		hstore_table[i].state &= ~HSTORE_E_MARK;
	}
	for (i32 i = 0; i < n; ++i) {
		hstore_visit_mark(roots[i], &work);
	}
	for (u32 i = 0; referenced && i < hstore_cap; ++i) {
		if (!hash128_is_none(hstore_table[i].h) && hstore_table[i].refs > 0) {
			hstore_visit_mark(hstore_table[i].h, &work);
		}
	}
	while (work.n > 0) {
		hash128_t          h           = work.h[--work.n];
		bool               descend     = fn(h, ud); // Writes, the table does not change
		hstore_entry_t    *e           = hstore_find(h);
		hstore_children_fn fn_children = e != NULL ? hstore_children_of(e->kind) : NULL;
		if (descend && fn_children != NULL) {
			u8 *raw = hstore_entry_bytes(e, true);
			if (raw != NULL) {
				hstore_each_decodes++;
				fn_children(raw, e->raw_len, hstore_visit_mark, &work);
				free(raw);
			}
		}
	}
	free(work.h);
}

// The chunk of a stored blob: its header fields and its payload as stored (malloc'd), NULL when missing or unreadable
u8 *hstore_chunk_payload(hash128_t h, hstore_chunk_t *c) {
	hstore_entry_t *e = hstore_find(h);
	if (e == NULL) {
		return NULL;
	}
	*c = (hstore_chunk_t){.kind = e->kind, .codec = e->codec, .h = e->h, .raw_len = e->raw_len, .comp_len = e->comp_len};
	if (e->ram != NULL) {
		u8 *p = malloc(e->comp_len > 0 ? e->comp_len : 1);
		memcpy(p, e->ram, e->comp_len);
		return p;
	}
	return hstore_journal_read_payload(e);
}

// ---- Saves and recovery ----

// The project file now holds the session: chunks before the mark need no recovery
void hstore_journal_mark_saved() {
	if (hstore_fd < 0) {
		return;
	}
	u8 head[HSTORE_CHUNK_HEADER];
	hstore_chunk_header_write(head, 0, HSTORE_CODEC_RAW, HSTORE_CHUNK_MARK, HASH128_NONE, 0, 0);
	if (hstore_pwrite_all(hstore_fd, head, HSTORE_CHUNK_HEADER, hstore_jsize)) {
		hstore_jowned = true;
		hstore_jsize += HSTORE_CHUNK_HEADER;
		hstore_jmark = hstore_jsize;
		hstore_dead += HSTORE_CHUNK_HEADER;
		fsync(hstore_fd);
	}
}

// A journal of the project with chunks after its last mark: unsaved work of a session that did not end
bool hstore_journal_pending(char *uuid) {
	if (uuid == NULL) {
		return false;
	}
	int fd = open(hstore_journal_path(uuid), O_RDONLY);
	if (fd < 0) {
		return false;
	}
	struct stat st;
	bool        pending = false;
	if (fstat(fd, &st) == 0 && st.st_size > HSTORE_JOURNAL_HEADER && hstore_journal_header_valid(fd)) {
		pending = hstore_journal_scan(fd, (i64)st.st_size).after_mark > 0;
	}
	close(fd);
	return pending;
}

// Indexes the blobs journaled after the last mark of the project's journal, the session continues appending to it. A
// torn or corrupt tail is cut off. Recovered blobs have no reference and stay until hstore_recover_end. Their count.
i32 hstore_recover() {
	hstore_journal_close();
	if (!hstore_journal_open()) {
		return 0;
	}
	i64 off   = hstore_jmark;
	i32 count = 0;
	u8  head[HSTORE_CHUNK_HEADER];
	memset(hstore_recovered_kind, 0, sizeof(hstore_recovered_kind));
	while (off + HSTORE_CHUNK_HEADER <= hstore_jsize) {
		hstore_chunk_t c;
		if (!hstore_pread_all(hstore_fd, head, HSTORE_CHUNK_HEADER, off) || !hstore_chunk_header_read(head, &c)) {
			break;
		}
		u8 *payload = malloc(c.comp_len > 0 ? c.comp_len : 1);
		u8 *raw     = hstore_pread_all(hstore_fd, payload, c.comp_len, off + HSTORE_CHUNK_HEADER) ? hstore_chunk_decode(&c, payload, true) : NULL;
		free(payload);
		if (raw == NULL) {
			break; // Corrupt: it and what follows are dropped
		}
		free(raw);
		if (c.kind < HSTORE_KINDS && c.kind != 0) {
			hstore_recovered_kind[c.kind] = c.h;
		}
		if (hstore_find(c.h) == NULL && c.kind != 0) {
			hstore_entry_t *e = hstore_insert(c.h);
			e->kind           = c.kind;
			e->codec          = c.codec;
			e->raw_len        = c.raw_len;
			e->comp_len       = c.comp_len;
			e->disk_off       = off;
			e->state          = HSTORE_E_RECOVERED | HSTORE_E_UNOWNED;
			hstore_dead -= HSTORE_CHUNK_HEADER + c.comp_len;
			count++;
		}
		off += HSTORE_CHUNK_HEADER + c.comp_len;
	}
	if (off < hstore_jsize) {
		hstore_dead -= hstore_jsize - off;
		if (ftruncate(hstore_fd, (off_t)off) != 0) {
			iron_log("History journal truncate failed");
		}
		hstore_jsize = off;
	}
	hstore_recovering = count > 0;
	return count;
}

// The last blob of kind the last hstore_recover read (the newest session head), NONE when none
hash128_t hstore_recovered_last(i32 kind) {
	return (u32)kind < HSTORE_KINDS ? hstore_recovered_kind[kind] : HASH128_NONE;
}

// Recovered blobs nothing referenced go at the next trim
void hstore_recover_end() {
	for (u32 i = 0; i < hstore_cap; ++i) {
		hstore_table[i].state &= ~HSTORE_E_RECOVERED;
	}
	hstore_recovering = false;
}

void hstore_journal_delete(char *uuid) {
	if (uuid == NULL) {
		return;
	}
	char *path = hstore_journal_path(uuid);
	if (hstore_fd_path != NULL && string_equals(path, hstore_fd_path)) {
		hstore_journal_close();
	}
	unlink(path);
}

// The project got another uuid (saved as another file): the session's journal follows it
void hstore_journal_rebind(char *old_uuid) {
	if (hstore_fd < 0 || old_uuid == NULL) {
		return;
	}
	char *to = hstore_journal_path(project_uuid_get());
	if (string_equals(to, hstore_fd_path)) {
		return;
	}
	if (rename(hstore_fd_path, to) == 0) {
		free(hstore_fd_path);
		hstore_fd_path = string_copy(to);
	}
}

// ---- Test hooks (paint/tests/history_store.c) ----

#define HSTORE_TEST_MAX 4096
static hash128_t hstore_test_hashes[HSTORE_TEST_MAX];
static i32       hstore_test_count = 0;

// Deterministic bytes: noise, or a smooth ramp that compresses
static u8 *hstore_test_data(i32 seed, i32 len, bool noise) {
	u8 *p = malloc(len > 0 ? len : 1);
	u32 x = (u32)seed * 2654435761u + 1;
	for (i32 i = 0; i < len; ++i) {
		if (noise) {
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			p[i] = (u8)x;
		}
		else {
			p[i] = (u8)(seed + i / 97);
		}
	}
	return p;
}

// Puts seed's bytes, the id of its hash for the other hooks
i32 hstore_test_put(i32 kind, i32 seed, i32 len, bool noise) {
	if (hstore_test_count >= HSTORE_TEST_MAX) {
		return -1;
	}
	u8 *p                                  = hstore_test_data(seed, len, noise);
	hstore_test_hashes[hstore_test_count] = hstore_put((hstore_kind_t)kind, p, (u32)len);
	free(p);
	return hstore_test_count++;
}

// 1 when id's blob reads back as seed's bytes, 0 when missing, -1 when different
i32 hstore_test_get(i32 id, i32 seed, i32 len, bool noise) {
	u32 n;
	u8 *got = hstore_get(hstore_test_hashes[id], &n);
	if (got == NULL) {
		return 0;
	}
	u8  *want = hstore_test_data(seed, len, noise);
	bool same = n == (u32)len && memcmp(got, want, len) == 0;
	free(got);
	free(want);
	return same ? 1 : -1;
}

bool hstore_test_has(i32 id) {
	return hstore_has(hstore_test_hashes[id]);
}

void hstore_test_ref(i32 id) {
	hstore_ref(hstore_test_hashes[id]);
}

void hstore_test_unref(i32 id) {
	hstore_unref(hstore_test_hashes[id]);
}

// Field of hstore_stats in KB: 0 blobs (a count), 1 ram, 2 disk, 3 dirty, 4 dead, 5 ram budget, 6 disk budget,
// 7 duration of the last flush in ms, 8 chunks read from the journal or project (a count), 9 blobs saves read the
// children of (a count), 10 the cap of the ram budget now (iOS: a tenth of the memory available, 0 elsewhere)
f32 hstore_test_stat(i32 field) {
	hstore_stats_t s = hstore_stats();
	switch (field) {
	case 0:
		return (f32)s.blobs;
	case 1:
		return s.ram_bytes / 1024.0f;
	case 2:
		return s.disk_bytes / 1024.0f;
	case 3:
		return s.dirty_bytes / 1024.0f;
	case 4:
		return s.dead_bytes / 1024.0f;
	case 5:
		return s.ram_budget / 1024.0f;
	case 6:
		return s.disk_budget / 1024.0f;
	case 7:
		return (f32)hstore_flush_ms;
	case 8:
		return (f32)hstore_disk_reads;
	case 9:
		return (f32)hstore_each_decodes;
	case 10:
		return hstore_ram_cap() / 1024.0f;
	}
	return 0.0f;
}

// 1: T1, every blob journaled and its RAM copy dropped, reads come from the journal. Other tiers are not this store's.
void hstore_drop_tier(i32 tier) {
	if (tier == 1) {
		hstore_flush();
		hstore_spill(0);
	}
}

void hstore_test_flush() {
	hstore_flush();
}

void hstore_test_trim(bool memory_warning) {
	hstore_trim(memory_warning);
}

void hstore_test_reset() {
	hstore_reset();
}

// Blobs of kind list the hashes of their children, 16 bytes each (lo, hi)
static void hstore_test_enumerate(const u8 *data, u32 len, hstore_visit_fn visit, void *ud) {
	for (u32 i = 0; i + 16 <= len; i += 16) {
		hash128_t h;
		memcpy(&h.lo, data + i, 8);
		memcpy(&h.hi, data + i + 8, 8);
		visit(h, ud);
	}
}

void hstore_test_children(i32 kind, bool on) {
	hstore_set_children((hstore_kind_t)kind, on ? hstore_test_enumerate : NULL);
}

static hstore_list_t hstore_test_list = {0};

void hstore_test_list_clear() {
	hstore_test_list.n = 0;
}

void hstore_test_list_add(i32 id) {
	hstore_list_push(&hstore_test_list, hstore_test_hashes[id]);
}

// A blob of the listed hashes, its id
i32 hstore_test_put_list(i32 kind) {
	if (hstore_test_count >= HSTORE_TEST_MAX) {
		return -1;
	}
	hstore_test_hashes[hstore_test_count] = hstore_put((hstore_kind_t)kind, hstore_test_list.h, hstore_test_list.n * sizeof(hash128_t));
	return hstore_test_count++;
}

// The listed hashes are the roots
void hstore_test_rebuild() {
	hstore_rebuild_refs(hstore_test_list.h, (i32)hstore_test_list.n);
}

// References of id's blob, -1 when not stored
i32 hstore_test_refs(i32 id) {
	hstore_entry_t *e = hstore_find(hstore_test_hashes[id]);
	return e != NULL ? (i32)e->refs : -1;
}

// A crash: the table is forgotten, the journal stays as it is on disk
void hstore_test_crash() {
	if (hstore_fd >= 0) {
		hstore_journal_close();
	}
	if (hstore_pfd >= 0) {
		close(hstore_pfd);
		hstore_pfd = -1;
	}
	hstore_free_table();
	hstore_table = NULL;
	hstore_cap   = 0;
	hstore_count = 0;
	hstore_ram   = 0;
	hstore_dirty = 0;
	hstore_dead  = 0;
}

// Cuts bytes off the end of the project's journal (a write torn by a crash)
void hstore_test_tear(i32 bytes) {
	char       *path = hstore_journal_path(project_uuid_get());
	struct stat st;
	if (stat(path, &st) == 0 && st.st_size > bytes) {
		if (truncate(path, st.st_size - bytes) != 0) {
			iron_log("Truncate failed");
		}
	}
}

char *hstore_test_journal_path() {
	return hstore_journal_path(project_uuid_get());
}
