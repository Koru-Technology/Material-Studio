#include "global.h"
#include <pthread.h>
#include <stdatomic.h>
#include <zstd.h>

// Layer states as tile maps (stroke_rec.h). Each channel texture of a layer (base, nor, pack; a mask has one) is cut into
// HISTORY_TILES_GRID^2 tiles (sizes differ by a texel at most when 64 does not divide the texture's, tl_tiles_rect), a
// tile's raw texel bytes are an HSTORE_TILE blob. A tile map lists the tiles of a state.
//
// HSTORE_TILEMAP blobs start with a format tag, little endian, packed:
//   0 full:  u8 tag, u8 channels, u8 texel bytes, u8 depth (0); i32 width, height, format;
//            hash128 tiles[channels * grid^2] (index channel * grid^2 + tile, tile = y * grid + x)
//   1 delta: u8 tag, u8 channels, u8 texel bytes, u8 depth (deltas down to a full map, 1..TL_TILES_DEPTH); u32 count;
//            hash128 base (the map of the state before); count x {u32 index, hash128 tile}
// A delta lists the tiles that differ from its base. Past TL_TILES_DEPTH deltas in a row a capture is full again.
// Uniform tiles (every texel the same, texels of 8 bytes at most) are never stored: hi = HASH128_INLINE_BIT | texel
// bytes, lo = the texel.
// A stored map owns one reference on each tile it lists, a delta also on its base: hstore takes them when the map is
// stored first and drops them with it (timeline_tiles_children).
// Transfers go through gpu_transfer: Metal only, as tiled undo.

#define TL_TILES_DEPTH 64  // Deltas in a row before a capture is full again
#define TL_TILES_BATCH 256 // Tiles per transfer round trip
#define TL_TILES_CACHE 8   // Maps kept resolved

#define TL_TILES_FULL_HEADER  16
#define TL_TILES_DELTA_HEADER 24
#define TL_TILES_DELTA_ENTRY  20

typedef struct tl_tiles_map {
	hash128_t  key; // Of the blob, NONE for a free cache slot
	i32        channels;
	i32        bpt; // Texel bytes
	i32        depth;
	i32        width;
	i32        height;
	i32        format;
	hash128_t *tiles; // channels * grid^2
} tl_tiles_map_t;

static tl_tiles_map_t  tl_tiles_cache[TL_TILES_CACHE];
static i32             tl_tiles_cache_next = 0;
static gpu_transfer_t *tl_tiles_transfer = NULL;

static i32 tl_tiles_per_channel() {
	return HISTORY_TILES_GRID * HISTORY_TILES_GRID;
}

void timeline_tiles_children(const u8 *data, u32 len, hstore_visit_fn visit, void *ud);

// hstore lists the tiles and the delta base of a stored map as its children
static void tl_tiles_register() {
	static bool registered = false;
	if (!registered) {
		hstore_set_children(HSTORE_TILEMAP, timeline_tiles_children);
		hstore_set_children(HSTORE_LAYER_RECORD, history_record_children);
		hstore_set_children(HSTORE_PARAMS, timeline_ops_children);
		registered = true;
	}
}

static bool tl_tiles_inline(hash128_t h) {
	return (h.hi & HASH128_INLINE_BIT) != 0;
}

static void tl_tiles_cache_clear_slot(tl_tiles_map_t *m) {
	free(m->tiles);
	memset(m, 0, sizeof(tl_tiles_map_t));
}

static tl_tiles_map_t *tl_tiles_cache_find(hash128_t h) {
	for (i32 i = 0; i < TL_TILES_CACHE; ++i) {
		if (!hash128_is_none(tl_tiles_cache[i].key) && hash128_eq(tl_tiles_cache[i].key, h)) {
			return &tl_tiles_cache[i];
		}
	}
	return NULL;
}

// Takes m->tiles over
static tl_tiles_map_t *tl_tiles_cache_put(tl_tiles_map_t *m) {
	tl_tiles_map_t *slot = &tl_tiles_cache[tl_tiles_cache_next];
	tl_tiles_cache_next  = (tl_tiles_cache_next + 1) % TL_TILES_CACHE;
	tl_tiles_cache_clear_slot(slot);
	*slot = *m;
	return slot;
}

void timeline_tiles_cache_reset() {
	tl_tiles_register(); // Before a project's blobs are read
	for (i32 i = 0; i < TL_TILES_CACHE; ++i) {
		tl_tiles_cache_clear_slot(&tl_tiles_cache[i]);
	}
}

static hash128_t tl_tiles_read_hash(const u8 *p) {
	hash128_t h;
	memcpy(&h.lo, p, 8);
	memcpy(&h.hi, p + 8, 8);
	return h;
}

static void tl_tiles_write_hash(u8 *p, hash128_t h) {
	memcpy(p, &h.lo, 8);
	memcpy(p + 8, &h.hi, 8);
}

static u8 *tl_tiles_encode_full(tl_tiles_map_t *m, u32 *len) {
	i32 total = m->channels * tl_tiles_per_channel();
	*len      = TL_TILES_FULL_HEADER + total * 16;
	u8 *b     = malloc(*len);
	b[0]      = 0;
	b[1]      = m->channels;
	b[2]      = m->bpt;
	b[3]      = 0;
	memcpy(b + 4, &m->width, 4);
	memcpy(b + 8, &m->height, 4);
	memcpy(b + 12, &m->format, 4);
	for (i32 i = 0; i < total; ++i) {
		tl_tiles_write_hash(b + TL_TILES_FULL_HEADER + i * 16, m->tiles[i]);
	}
	return b;
}

static u8 *tl_tiles_encode_delta(tl_tiles_map_t *m, hash128_t base, u32 *idx, u32 count, u32 *len) {
	*len  = TL_TILES_DELTA_HEADER + count * TL_TILES_DELTA_ENTRY;
	u8 *b = malloc(*len);
	b[0]  = 1;
	b[1]  = m->channels;
	b[2]  = m->bpt;
	b[3]  = m->depth;
	memcpy(b + 4, &count, 4);
	tl_tiles_write_hash(b + 8, base);
	for (u32 i = 0; i < count; ++i) {
		u8 *e = b + TL_TILES_DELTA_HEADER + i * TL_TILES_DELTA_ENTRY;
		memcpy(e, &idx[i], 4);
		tl_tiles_write_hash(e + 4, m->tiles[idx[i]]);
	}
	return b;
}

// The map of state h, NULL when a blob of it is missing. Owned by the cache: valid until the next resolve or capture.
static tl_tiles_map_t *tl_tiles_resolve(hash128_t h) {
	if (hash128_is_none(h)) {
		return NULL;
	}
	tl_tiles_map_t *hit = tl_tiles_cache_find(h);
	if (hit != NULL) {
		return hit;
	}
	u8            *chain[TL_TILES_DEPTH + 2];
	u32            chain_len[TL_TILES_DEPTH + 2];
	i32            n     = 0;
	hash128_t      cur   = h;
	tl_tiles_map_t base  = {0};
	bool           found = false;
	while (n <= TL_TILES_DEPTH + 1) {
		tl_tiles_map_t *c = tl_tiles_cache_find(cur);
		if (c != NULL) {
			base       = *c;
			i32 total  = c->channels * tl_tiles_per_channel();
			base.tiles = malloc(total * sizeof(hash128_t));
			memcpy(base.tiles, c->tiles, total * sizeof(hash128_t));
			found = true;
			break;
		}
		u32 len = 0;
		u8 *b   = hstore_get(cur, &len);
		if (b == NULL || len < TL_TILES_FULL_HEADER) {
			free(b);
			break;
		}
		if (b[0] == 0) {
			base.key      = cur;
			base.channels = b[1];
			base.bpt      = b[2];
			base.depth    = 0;
			memcpy(&base.width, b + 4, 4);
			memcpy(&base.height, b + 8, 4);
			memcpy(&base.format, b + 12, 4);
			i32 total = base.channels * tl_tiles_per_channel();
			if (len < TL_TILES_FULL_HEADER + total * 16) {
				free(b);
				break;
			}
			base.tiles = malloc(total * sizeof(hash128_t));
			for (i32 i = 0; i < total; ++i) {
				base.tiles[i] = tl_tiles_read_hash(b + TL_TILES_FULL_HEADER + i * 16);
			}
			free(b);
			found = true;
			break;
		}
		chain[n]     = b;
		chain_len[n] = len;
		n++;
		cur = tl_tiles_read_hash(b + 8);
	}
	if (found) {
		i32 total = base.channels * tl_tiles_per_channel();
		for (i32 i = n - 1; i >= 0; --i) {
			u8 *b     = chain[i];
			u32 count = 0;
			memcpy(&count, b + 4, 4);
			if (chain_len[i] < TL_TILES_DELTA_HEADER + count * TL_TILES_DELTA_ENTRY) {
				found = false;
				break;
			}
			for (u32 j = 0; j < count; ++j) {
				u8 *e   = b + TL_TILES_DELTA_HEADER + j * TL_TILES_DELTA_ENTRY;
				u32 idx = 0;
				memcpy(&idx, e, 4);
				if (idx < (u32)total) {
					base.tiles[idx] = tl_tiles_read_hash(e + 4);
				}
			}
			base.depth = b[3];
		}
	}
	for (i32 i = 0; i < n; ++i) {
		free(chain[i]);
	}
	if (!found) {
		free(base.tiles);
		return NULL;
	}
	base.key = h;
	return tl_tiles_cache_put(&base);
}

// Header of the map h: size, format, channels. False when missing.
bool timeline_tiles_info(hash128_t h, i32 *width, i32 *height, i32 *format, i32 *channels) {
	tl_tiles_map_t *m = tl_tiles_resolve(h);
	if (m == NULL) {
		return false;
	}
	*width    = m->width;
	*height   = m->height;
	*format   = m->format;
	*channels = m->channels;
	return true;
}

// Each channel of map h is one value (its tiles inline and equal): a state that scales to any resolution as it is
bool timeline_tiles_uniform(hash128_t h) {
	tl_tiles_map_t *m = tl_tiles_resolve(h);
	if (m == NULL) {
		return false;
	}
	i32 per = tl_tiles_per_channel();
	for (i32 i = 0; i < m->channels * per; ++i) {
		hash128_t first = m->tiles[i - i % per];
		bool      empty = m->tiles[i].lo == 0 && m->tiles[i].hi == HASH128_INLINE_BIT; // A tile of no texels
		if (!tl_tiles_inline(m->tiles[i]) || (!empty && !hash128_eq(m->tiles[i], first))) {
			return false;
		}
	}
	return true;
}

// Map h is a delta on base
bool timeline_tiles_base(hash128_t h, hash128_t base) {
	u32 len = 0;
	u8 *b   = hash128_is_none(h) ? NULL : hstore_get(h, &len);
	bool on = b != NULL && len >= TL_TILES_DELTA_HEADER && b[0] == 1 && hash128_eq(tl_tiles_read_hash(b + 8), base);
	free(b);
	return on;
}

// State h encoded again: a delta on base (NONE, or another size: a full map), a reference added. NONE when a map is
// missing.
hash128_t timeline_tiles_rebase(hash128_t h, hash128_t base) {
	tl_tiles_map_t *m = tl_tiles_resolve(h);
	if (m == NULL) {
		return HASH128_NONE;
	}
	tl_tiles_map_t c     = *m;
	i32            total = c.channels * tl_tiles_per_channel();
	c.tiles              = malloc(total * sizeof(hash128_t));
	memcpy(c.tiles, m->tiles, total * sizeof(hash128_t));
	tl_tiles_map_t *p = tl_tiles_resolve(base);
	u32            *changed = malloc(total * sizeof(u32));
	u32             count   = 0;
	bool            delta   = p != NULL && p->channels == c.channels && p->width == c.width && p->height == c.height && p->format == c.format &&
	               p->depth < TL_TILES_DEPTH;
	for (i32 i = 0; delta && i < total; ++i) {
		if (!hash128_eq(p->tiles[i], c.tiles[i])) {
			changed[count++] = i;
		}
	}
	delta     = delta && count * TL_TILES_DELTA_ENTRY < (u32)total * 16;
	u32 len   = 0;
	u8 *blob  = NULL;
	if (delta) {
		c.depth = p->depth + 1;
		blob    = tl_tiles_encode_delta(&c, base, changed, count, &len);
	}
	else {
		c.depth = 0;
		blob    = tl_tiles_encode_full(&c, &len);
	}
	hash128_t r = hstore_put(HSTORE_TILEMAP, blob, len); // Takes references on its tiles and base
	free(blob);
	free(changed);
	c.key = r;
	if (tl_tiles_cache_find(r) == NULL) {
		tl_tiles_cache_put(&c);
	}
	else {
		free(c.tiles);
	}
	return r;
}

// Children of a stored map (hstore_set_children): its tiles, and the base of a delta
void timeline_tiles_children(const u8 *data, u32 len, hstore_visit_fn visit, void *ud) {
	if (len < TL_TILES_FULL_HEADER) {
		return;
	}
	if (data[0] == 0) {
		i32 total = data[1] * tl_tiles_per_channel();
		for (i32 i = 0; i < total && TL_TILES_FULL_HEADER + (i + 1) * 16 <= (i32)len; ++i) {
			visit(tl_tiles_read_hash(data + TL_TILES_FULL_HEADER + i * 16), ud);
		}
	}
	else {
		u32 count = 0;
		memcpy(&count, data + 4, 4);
		for (u32 j = 0; j < count && TL_TILES_DELTA_HEADER + (j + 1) * TL_TILES_DELTA_ENTRY <= len; ++j) {
			visit(tl_tiles_read_hash(data + TL_TILES_DELTA_HEADER + j * TL_TILES_DELTA_ENTRY + 4), ud);
		}
		visit(tl_tiles_read_hash(data + 8), ud);
	}
}

// The hash of a tile's bytes, the tile stored (a reference added) unless it is uniform. A tile of no texels (a texture
// narrower than the grid) is the empty inline tile.
static hash128_t tl_tiles_store(const u8 *p, i32 bytes, i32 tw, i32 bpt) {
	if (bytes == 0) {
		return (hash128_t){0, HASH128_INLINE_BIT};
	}
	if (bpt <= 8) {
		bool uniform = true;
		for (i32 i = bpt; i < bytes; i += bpt) {
			if (memcmp(p, p + i, bpt) != 0) {
				uniform = false;
				break;
			}
		}
		if (uniform) {
			hash128_t h = {0, HASH128_INLINE_BIT | (u64)bpt};
			memcpy(&h.lo, p, bpt);
			return h;
		}
	}
	return hstore_put_texels(HSTORE_TILE, p, bytes, tw, bpt);
}

// Texel bytes of tile h into p, false when missing
static bool tl_tiles_load(hash128_t h, u8 *p, i32 bytes, i32 bpt) {
	if (bytes == 0) {
		return true;
	}
	if (tl_tiles_inline(h)) {
		for (i32 i = 0; i < bytes; i += bpt) {
			memcpy(p + i, &h.lo, bpt);
		}
		return true;
	}
	u32 len = 0;
	u8 *b   = hstore_get(h, &len);
	if (b == NULL || len != (u32)bytes) {
		free(b);
		return false;
	}
	memcpy(p, b, bytes);
	free(b);
	return true;
}

// Texel rectangle of tile t in a w x h texture: tiles split the size evenly, they differ by a texel at most when 64 does
// not divide it. Below 64 texels some tiles are empty (no texels).
static void tl_tiles_rect(i32 w, i32 h, i32 t, i32 *x, i32 *y, i32 *tw, i32 *th) {
	i32 tx = t % HISTORY_TILES_GRID;
	i32 ty = t / HISTORY_TILES_GRID;
	*x     = tx * w / HISTORY_TILES_GRID;
	*y     = ty * h / HISTORY_TILES_GRID;
	*tw    = (tx + 1) * w / HISTORY_TILES_GRID - *x;
	*th    = (ty + 1) * h / HISTORY_TILES_GRID - *y;
}

// Bytes of the largest tile, the stride of tiles in transfers
static i32 tl_tiles_slot_bytes(i32 w, i32 h, i32 bpt) {
	return ((w + HISTORY_TILES_GRID - 1) / HISTORY_TILES_GRID) * ((h + HISTORY_TILES_GRID - 1) / HISTORY_TILES_GRID) * bpt;
}

static bool tl_tiles_target_ok(paint_target_t *t) {
	if (t == NULL || t->channels < 1 || t->channels > 3) {
		return false;
	}
	gpu_texture_t *tex = t->out[0];
	for (i32 c = 0; c < t->channels; ++c) {
		gpu_texture_t *o = t->out[c];
		if (o == NULL || o->width != tex->width || o->height != tex->height || o->format != tex->format) {
			return false;
		}
	}
	return tex->width >= 1 && tex->height >= 1;
}

static i32 tl_tiles_reads = 0; // Tiles read back so far (a tile of each channel counts once), for tests

i32 timeline_tiles_test_reads() {
	return tl_tiles_reads;
}

hash128_t timeline_tiles_capture(paint_target_t *src, hash128_t prev, const u64 *dirty) {
	if (!tl_tiles_target_ok(src)) {
		return HASH128_NONE;
	}
	tl_tiles_register();
	gpu_texture_t *tex      = src->out[0];
	i32            per      = tl_tiles_per_channel();
	i32            channels = src->channels;
	i32            total    = channels * per;
	i32            bpt      = gpu_texture_format_size(tex->format);
	i32            bytes    = tl_tiles_slot_bytes(tex->width, tex->height, bpt);

	tl_tiles_map_t m = {.channels = channels, .bpt = bpt, .width = tex->width, .height = tex->height, .format = tex->format};
	m.tiles          = malloc(total * sizeof(hash128_t));
	hash128_t *old   = NULL; // The state before, when known
	i32        depth = 0;
	tl_tiles_map_t *p = tl_tiles_resolve(prev);
	if (p != NULL && p->channels == channels && p->width == m.width && p->height == m.height && p->format == m.format) {
		old = malloc(total * sizeof(hash128_t));
		memcpy(old, p->tiles, total * sizeof(hash128_t));
		memcpy(m.tiles, old, total * sizeof(hash128_t));
		depth = p->depth;
	}

	// Tiles to read: the dirty ones when the state before is known, every one otherwise
	i32 *list = malloc(per * sizeof(i32));
	i32  n    = 0;
	for (i32 t = 0; t < per; ++t) {
		if (old == NULL || dirty == NULL || ((dirty[t >> 6] >> (t & 63)) & 1)) {
			list[n++] = t;
		}
	}
	bool *read = calloc(total, sizeof(bool)); // Tiles holding a reference from this capture
	tl_tiles_reads += n * channels;

	if (tl_tiles_transfer == NULL) {
		tl_tiles_transfer = gpu_transfer_create(0);
	}
	draw_saved_t saved = draw_suspend();
	for (i32 b0 = 0; b0 < n; b0 += TL_TILES_BATCH) {
		i32 bn = n - b0 < TL_TILES_BATCH ? n - b0 : TL_TILES_BATCH;
		gpu_transfer_map(tl_tiles_transfer, (size_t)bn * channels * bytes);
		gpu_copy_regions_begin();
		for (i32 i = 0; i < bn; ++i) {
			i32 x, y, tw, th;
			tl_tiles_rect(tex->width, tex->height, list[b0 + i], &x, &y, &tw, &th);
			for (i32 c = 0; c < channels && tw > 0 && th > 0; ++c) {
				gpu_copy_region_to_transfer(tl_tiles_transfer, src->out[c], x, y, tw, th, ((size_t)i * channels + c) * bytes);
			}
		}
		gpu_copy_regions_end();
		gpu_execute_and_wait();
		u8 *mem = gpu_transfer_map(tl_tiles_transfer, 0);
		for (i32 i = 0; i < bn; ++i) {
			i32 t = list[b0 + i];
			i32 x, y, tw, th;
			tl_tiles_rect(tex->width, tex->height, t, &x, &y, &tw, &th);
			for (i32 c = 0; c < channels; ++c) {
				i32 idx      = c * per + t;
				m.tiles[idx] = tl_tiles_store(mem + ((size_t)i * channels + c) * bytes, tw * th * bpt, tw, bpt);
				read[idx]    = true;
			}
		}
	}
	draw_resume(saved);

	// Changed tiles, the read ones that did not change hold a reference for nothing
	u32 *changed = malloc(total * sizeof(u32));
	u32  count   = 0;
	for (i32 idx = 0; idx < total; ++idx) {
		if (old != NULL && hash128_eq(old[idx], m.tiles[idx])) {
			if (read[idx]) {
				hstore_unref(m.tiles[idx]);
				read[idx] = false;
			}
		}
		else {
			changed[count++] = idx;
		}
	}

	hash128_t result = HASH128_NONE;
	if (old != NULL && count == 0) {
		hstore_ref(prev); // Same state
		result = prev;
	}
	else {
		bool delta = old != NULL && depth < TL_TILES_DEPTH && count * TL_TILES_DELTA_ENTRY < (u32)total * 16;
		u32  len   = 0;
		u8  *blob  = NULL;
		if (delta) {
			m.depth = depth + 1;
			blob    = tl_tiles_encode_delta(&m, prev, changed, count, &len);
		}
		else {
			m.depth = 0;
			blob    = tl_tiles_encode_full(&m, &len);
		}
		result = hstore_put(HSTORE_TILEMAP, blob, len); // A new map takes references on its tiles and base
		free(blob);
		for (i32 idx = 0; idx < total; ++idx) {
			if (read[idx]) {
				hstore_unref(m.tiles[idx]); // This capture's own
			}
		}
		m.key = result;
		tl_tiles_map_t *hit = tl_tiles_cache_find(result);
		if (hit == NULL) {
			tl_tiles_cache_put(&m);
			m.tiles = NULL;
		}
	}
	free(m.tiles);
	free(old);
	free(list);
	free(read);
	free(changed);
	return result;
}

// ---- Whole restores on threads ----
// An open restores every layer whole: its tiles are read and decoded on threads (hstore_read_located), straight into the
// transfer, and go to the GPU in one copy. One tile at a time on the main thread, a GPU wait every 256 of them, took 7 s
// of Book 1's open on the iPad.

#define TL_TILES_THREADS 6

typedef struct {
	const hash128_t *want;
	const i32       *list;
	i32              n;
	hstore_loc_t    *locs;
	u8              *status; // 0 to do on a thread, 1 inline, 2 on the main thread (not located, or failed), 3 a copy of same[i]
	i32             *same;   // The first slot of the same tile (read once)
	i32            (*rects)[4];
	u8              *mem;
	i32              bytes;
	i32              bpt;
	i32              lw;
	i32              lh;
	i32              per;
	atomic_int       next;
	atomic_long      disk;
} tl_tiles_job_t;

// Rows of the part of a decoded tile written (rect: x0 y0 w h in the layer), packed at out
static void tl_tiles_pack(const u8 *tile, i32 x, i32 y, i32 tw, const i32 *rect, i32 bpt, u8 *out) {
	for (i32 row = 0; row < rect[3]; ++row) {
		memcpy(out + (size_t)row * rect[2] * bpt, tile + ((size_t)(rect[1] - y + row) * tw + (rect[0] - x)) * bpt, (size_t)rect[2] * bpt);
	}
}

static void *tl_tiles_worker(void *param) {
	tl_tiles_job_t *j    = param;
	ZSTD_DCtx      *dctx = ZSTD_createDCtx();
	i64             disk = 0;
	for (;;) {
		i32 i0 = atomic_fetch_add(&j->next, 32);
		if (i0 >= j->n) {
			break;
		}
		for (i32 i = i0; i < i0 + 32 && i < j->n; ++i) {
			if (j->status[i] != 0) {
				continue;
			}
			i32 idx = j->list[i];
			i32 x, y, tw, th;
			tl_tiles_rect(j->lw, j->lh, idx % j->per, &x, &y, &tw, &th);
			bool from_disk = false;
			u8  *tile      = hstore_read_located(&j->locs[i], dctx, &from_disk);
			disk += from_disk;
			if (tile == NULL || j->locs[i].c.raw_len != (u32)(tw * th * j->bpt)) {
				free(tile);
				j->status[i] = 2;
				continue;
			}
			tl_tiles_pack(tile, x, y, tw, j->rects[i], j->bpt, j->mem + (size_t)i * j->bytes);
			free(tile);
		}
	}
	ZSTD_freeDCtx(dctx);
	atomic_fetch_add(&j->disk, disk);
	return NULL;
}

// Every tile of list into mem (slots of bytes), then copied to dst in one go. False when one could not be read (its part
// cleared, as a restore one at a time does).
static bool tl_tiles_restore_all(const hash128_t *want, const i32 *list, i32 n, paint_target_t *dst, i32 ox, i32 oy, i32 lw, i32 lh, i32 bytes,
                                 i32 bpt) {
	gpu_texture_t  *tex = dst->out[0];
	i32             per = tl_tiles_per_channel();
	tl_tiles_job_t  j   = {.want = want, .list = list, .n = n, .bytes = bytes, .bpt = bpt, .lw = lw, .lh = lh, .per = per};
	atomic_init(&j.next, 0);
	atomic_init(&j.disk, 0);
	j.locs   = malloc(sizeof(hstore_loc_t) * n);
	j.status = calloc(n, 1);
	j.same   = malloc(sizeof(i32) * n);
	i32  cap   = 1;
	while (cap < n * 2) {
		cap <<= 1;
	}
	i32 *first = malloc(sizeof(i32) * cap); // Open addressing by hash: a tile in several slots is read once
	memset(first, 0xff, sizeof(i32) * cap);
	j.rects  = malloc(sizeof(i32[4]) * n);
	j.mem    = gpu_transfer_map(tl_tiles_transfer, (size_t)n * bytes);
	for (i32 i = 0; i < n; ++i) { // The store is read on this thread only: where each tile is
		i32 idx = list[i];
		i32 x, y, tw, th;
		tl_tiles_rect(lw, lh, idx % per, &x, &y, &tw, &th);
		i32 x0      = x > ox ? x : ox;
		i32 y0      = y > oy ? y : oy;
		i32 x1      = x + tw < ox + tex->width ? x + tw : ox + tex->width;
		i32 y1      = y + th < oy + tex->height ? y + th : oy + tex->height;
		j.rects[i][0] = x0;
		j.rects[i][1] = y0;
		j.rects[i][2] = x1 - x0;
		j.rects[i][3] = y1 - y0;
		if (tl_tiles_inline(want[idx])) {
			j.status[i] = 1;
			continue;
		}
		u32 slot = (u32)(want[idx].lo ^ (want[idx].lo >> 29)) & (cap - 1);
		while (first[slot] >= 0 && !hash128_eq(want[list[first[slot]]], want[idx])) {
			slot = (slot + 1) & (cap - 1);
		}
		if (first[slot] >= 0 && j.rects[first[slot]][2] == j.rects[i][2] && j.rects[first[slot]][3] == j.rects[i][3]) {
			j.status[i] = 3;
			j.same[i]   = first[slot];
			continue;
		}
		if (first[slot] < 0) {
			first[slot] = i;
		}
		if (!hstore_locate(want[idx], &j.locs[i])) {
			j.status[i] = 2;
		}
	}
	free(first);
	pthread_t threads[TL_TILES_THREADS];
	i32       nt = 0;
	for (i32 t = 0; t < TL_TILES_THREADS; ++t) {
		if (pthread_create(&threads[nt], NULL, tl_tiles_worker, &j) == 0) {
			nt++;
		}
	}
	if (nt == 0) {
		tl_tiles_worker(&j);
	}
	for (i32 t = 0; t < nt; ++t) {
		pthread_join(threads[t], NULL);
	}
	hstore_count_disk_reads(atomic_load(&j.disk));

	bool ok   = true;
	u8  *tile = malloc(bytes);
	for (i32 i = 0; i < n; ++i) { // Inline tiles, and the ones a thread could not read: here, as one at a time
		if (j.status[i] == 0 || j.status[i] == 3) {
			continue;
		}
		i32 idx = list[i];
		i32 x, y, tw, th;
		tl_tiles_rect(lw, lh, idx % per, &x, &y, &tw, &th);
		if (!tl_tiles_load(want[idx], tile, tw * th * bpt, bpt)) {
			ok = false;
			memset(tile, 0, tw * th * bpt);
		}
		tl_tiles_pack(tile, x, y, tw, j.rects[i], bpt, j.mem + (size_t)i * bytes);
	}
	free(tile);
	for (i32 i = 0; i < n; ++i) { // Copies of a tile read once (the same size: same hash, same bytes)
		if (j.status[i] == 3) {
			memcpy(j.mem + (size_t)i * bytes, j.mem + (size_t)j.same[i] * bytes, (size_t)j.rects[i][2] * j.rects[i][3] * bpt);
		}
	}
	gpu_copy_regions_begin();
	for (i32 i = 0; i < n; ++i) {
		i32 idx = list[i];
		gpu_copy_region_from_transfer(tl_tiles_transfer, dst->out[idx / per], j.rects[i][0] - ox, j.rects[i][1] - oy, j.rects[i][2], j.rects[i][3],
		                              (size_t)i * bytes);
	}
	gpu_copy_regions_end();
	gpu_execute_and_wait(); // The memory is filled again by the next restore
	free(j.locs);
	free(j.status);
	free(j.same);
	free(j.rects);
	return ok;
}

// Writes the tiles of map into dst, only the ones that differ from prev when dst holds prev now (NONE: every tile).
// dst may cover a part of the layer (uv_rect) at the map's resolution, or reach past its edge: tiles go at an offset,
// texels outside the layer are cleared. Of the tiles to write the ones from from on, batches of them at most: the next
// from, 0 once every one is written, -1 when they can not be.
static i32 tl_tiles_restore_part(hash128_t map, hash128_t prev, paint_target_t *dst, i32 from, i32 batches) {
	if (!tl_tiles_target_ok(dst)) {
		return -1;
	}
	if (!hash128_is_none(prev) && hash128_eq(map, prev)) {
		return 0;
	}
	tl_tiles_map_t *m   = tl_tiles_resolve(map);
	gpu_texture_t  *tex = dst->out[0];
	if (m == NULL || m->channels != dst->channels || m->format != (i32)tex->format) {
		return -1;
	}
	i32  lw    = m->width;
	i32  lh    = m->height;
	bool whole = tex->width == lw && tex->height == lh && dst->uv_rect[0] == 0.0 && dst->uv_rect[1] == 0.0 && dst->uv_rect[2] == 1.0 &&
	             dst->uv_rect[3] == 1.0;
	if (!whole && (dst->res_w != lw || dst->res_h != lh)) {
		return -1; // Another resolution resamples, not tiles
	}
	i32 ox = whole ? 0 : (i32)floorf(dst->uv_rect[0] * lw + 0.5f); // Layer texel at the target's origin
	i32 oy = whole ? 0 : (i32)floorf(dst->uv_rect[1] * lh + 0.5f);
	i32 per   = tl_tiles_per_channel();
	i32 total = m->channels * per;
	hash128_t *want = malloc(total * sizeof(hash128_t));
	memcpy(want, m->tiles, total * sizeof(hash128_t));
	hash128_t *have = NULL;
	if (whole) {
		tl_tiles_map_t *p = tl_tiles_resolve(prev);
		if (p != NULL && p->channels == m->channels && p->width == lw && p->height == lh && p->format == m->format) {
			have = malloc(total * sizeof(hash128_t));
			memcpy(have, p->tiles, total * sizeof(hash128_t));
		}
	}
	i32 bpt   = gpu_texture_format_size(tex->format);
	i32 bytes = tl_tiles_slot_bytes(lw, lh, bpt);

	i32 *list = malloc(total * sizeof(i32));
	i32  n    = 0;
	for (i32 idx = 0; idx < total; ++idx) {
		i32 x, y, tw, th;
		tl_tiles_rect(lw, lh, idx % per, &x, &y, &tw, &th);
		bool inside = tw > 0 && th > 0 && x < ox + tex->width && x + tw > ox && y < oy + tex->height && y + th > oy;
		if (inside && (have == NULL || !hash128_eq(have[idx], want[idx]))) {
			list[n++] = idx;
		}
	}
	if (tl_tiles_transfer == NULL) {
		tl_tiles_transfer = gpu_transfer_create(0);
	}
	bool         ok    = true;
	u8          *tile  = malloc(bytes);
	draw_saved_t saved = draw_suspend();
	if (from == 0 && (ox < 0 || oy < 0 || ox + tex->width > lw || oy + tex->height > lh)) {
		for (i32 c = 0; c < dst->channels; ++c) { // Past the edge of the layer
			_gpu_begin(dst->out[c], NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
			gpu_end();
		}
	}
	i32 end = batches < (n - from + TL_TILES_BATCH - 1) / TL_TILES_BATCH ? from + batches * TL_TILES_BATCH : n;
	if (from == 0 && end == n && n > TL_TILES_BATCH) { // Whole: on threads, one copy
		ok  = tl_tiles_restore_all(want, list, n, dst, ox, oy, lw, lh, bytes, bpt);
		end = n;
		from = n; // The loop below has nothing left
	}
	for (i32 b0 = from; b0 < end; b0 += TL_TILES_BATCH) {
		i32 bn  = end - b0 < TL_TILES_BATCH ? end - b0 : TL_TILES_BATCH;
		u8 *mem = gpu_transfer_map(tl_tiles_transfer, (size_t)bn * bytes);
		i32 rects[TL_TILES_BATCH][4]; // Part of each tile written: x, y, w, h in the layer
		for (i32 i = 0; i < bn; ++i) {
			i32 idx = list[b0 + i];
			i32 x, y, tw, th;
			tl_tiles_rect(lw, lh, idx % per, &x, &y, &tw, &th);
			if (!tl_tiles_load(want[idx], tile, tw * th * bpt, bpt)) {
				ok = false;
				memset(tile, 0, tw * th * bpt);
			}
			i32 x0 = x > ox ? x : ox;
			i32 y0 = y > oy ? y : oy;
			i32 x1 = x + tw < ox + tex->width ? x + tw : ox + tex->width;
			i32 y1 = y + th < oy + tex->height ? y + th : oy + tex->height;
			rects[i][0] = x0;
			rects[i][1] = y0;
			rects[i][2] = x1 - x0;
			rects[i][3] = y1 - y0;
			u8 *out     = mem + (size_t)i * bytes;
			for (i32 row = 0; row < y1 - y0; ++row) { // Rows of the part, packed
				memcpy(out + (size_t)row * (x1 - x0) * bpt, tile + ((size_t)(y0 - y + row) * tw + (x0 - x)) * bpt, (size_t)(x1 - x0) * bpt);
			}
		}
		gpu_copy_regions_begin();
		for (i32 i = 0; i < bn; ++i) {
			i32 idx = list[b0 + i];
			gpu_copy_region_from_transfer(tl_tiles_transfer, dst->out[idx / per], rects[i][0] - ox, rects[i][1] - oy, rects[i][2], rects[i][3],
			                              (size_t)i * bytes);
		}
		gpu_copy_regions_end();
		gpu_execute_and_wait(); // The memory is filled again by the next batch
	}
	draw_resume(saved);
	free(tile);
	free(list);
	free(want);
	free(have);
	return !ok ? -1 : end < n ? end : 0;
}

bool timeline_tiles_restore_from(hash128_t map, hash128_t prev, paint_target_t *dst) {
	return tl_tiles_restore_part(map, prev, dst, 0, INT32_MAX) == 0;
}

// timeline_tiles_restore a part at a time (a job across frames): of the tiles to write the ones from from on (0 the
// first), batches of TL_TILES_BATCH at most. The next from, 0 once every one is written, -1 when they can not be.
i32 timeline_tiles_restore_step(hash128_t map, paint_target_t *dst, i32 from, i32 batches) {
	return tl_tiles_restore_part(map, HASH128_NONE, dst, from, batches);
}

bool timeline_tiles_restore(hash128_t tilemap, paint_target_t *dst) {
	return timeline_tiles_restore_from(tilemap, HASH128_NONE, dst);
}

// Scripts: a capture of a 100 x 80 part of l (partial tiles) restores byte for byte, a capture of l restores into a part
// of it and past its edge. Mismatches found, -1 when it could not run.
i32 timeline_tiles_test(slot_layer_t *l) {
	if (l == NULL || l->texpaint == NULL) {
		return -1;
	}
	gpu_texture_t *src  = l->texpaint;
	i32            w    = src->width;
	i32            h    = src->height;
	i32            bpt  = gpu_texture_format_size(src->format);
	i32            bad  = 0;
	draw_saved_t   save = draw_suspend();

	// Partial tiles, and textures smaller than the grid
	i32 sizes[3][2] = {{100, 80}, {20, 7}, {1, 1}};
	for (i32 k = 0; k < 3; ++k) {
		i32            sw = sizes[k][0];
		i32            sh = sizes[k][1];
		gpu_texture_t *a  = gpu_create_render_target(sw, sh, src->format);
		gpu_texture_t *b  = gpu_create_render_target(sw, sh, src->format);
		gpu_copy_regions_begin();
		gpu_copy_region(a, 0, 0, src, w / 2 - sw / 2, h / 2 - sh / 2, sw, sh);
		gpu_copy_regions_end();
		paint_target_t ta = {.w = sw, .h = sh, .uv_rect = {0, 0, 1, 1}, .res_w = sw, .res_h = sh, .channels = 1, .out = {a}};
		paint_target_t tb = ta;
		tb.out[0]         = b;
		hash128_t pm      = timeline_tiles_capture(&ta, HASH128_NONE, NULL);
		bad += hash128_is_none(pm) || !timeline_tiles_restore(pm, &tb);
		u8 *pa = malloc((size_t)sw * sh * bpt);
		memcpy(pa, gpu_get_texture_pixels(a)->buffer, (size_t)sw * sh * bpt);
		bad += memcmp(pa, gpu_get_texture_pixels(b)->buffer, (size_t)sw * sh * bpt) != 0;
		hstore_unref(pm);
		free(pa);
		gpu_delete_texture(a);
		gpu_delete_texture(b);
	}

	// Parts of l, inside and past the edge
	u8 *pl = malloc((size_t)w * h * bpt);
	memcpy(pl, gpu_get_texture_pixels(src)->buffer, (size_t)w * h * bpt);
	gpu_release_texture_pixels(src);
	paint_target_t tl  = {.w = w, .h = h, .uv_rect = {0, 0, 1, 1}, .res_w = w, .res_h = h, .channels = 1, .out = {src}};
	hash128_t      map = timeline_tiles_capture(&tl, HASH128_NONE, NULL);
	gpu_texture_t *c   = gpu_create_render_target(w / 2, h / 2, src->format);
	f32            rects[2][4] = {{0.25, 0.25, 0.75, 0.75}, {-0.25, -0.25, 0.25, 0.25}};
	for (i32 r = 0; r < 2; ++r) {
		paint_target_t tc = {.w = w / 2, .h = h / 2, .res_w = w, .res_h = h, .channels = 1, .out = {c}};
		memcpy(tc.uv_rect, rects[r], sizeof(tc.uv_rect));
		bad += !timeline_tiles_restore(map, &tc);
		u8 *pc = gpu_get_texture_pixels(c)->buffer;
		i32 ox = (i32)floorf(rects[r][0] * w + 0.5f);
		i32 oy = (i32)floorf(rects[r][1] * h + 0.5f);
		for (i32 y = 0; y < h / 2; ++y) {
			for (i32 x = 0; x < w / 2; ++x) {
				i32 lx      = x + ox;
				i32 ly      = y + oy;
				u8  zero[16] = {0};
				u8 *want     = lx >= 0 && ly >= 0 && lx < w && ly < h ? pl + ((size_t)ly * w + lx) * bpt : zero;
				if (memcmp(pc + ((size_t)y * (w / 2) + x) * bpt, want, bpt) != 0) {
					bad++;
					y = h;
					break;
				}
			}
		}
	}
	hstore_unref(map);
	free(pl);
	gpu_delete_texture(c);
	draw_resume(save);
	return bad;
}

// The uv box of the tiles that differ between two maps of the same size, grown by a texel (u0 v0 u1 v1). False when one
// is missing, they differ in size or no tile changed.
bool timeline_tiles_changed_bounds(hash128_t prev, hash128_t cur, f32 out[4]) {
	tl_tiles_map_t *a = tl_tiles_resolve(prev);
	if (a == NULL) {
		return false;
	}
	i32        n     = a->channels * tl_tiles_per_channel();
	i32        w     = a->width;
	i32        h     = a->height;
	i32        ch    = a->channels;
	hash128_t *tiles = malloc(sizeof(hash128_t) * n); // Resolving cur may take a's cache slot
	memcpy(tiles, a->tiles, sizeof(hash128_t) * n);
	tl_tiles_map_t *b = tl_tiles_resolve(cur);
	if (b == NULL || b->width != w || b->height != h || b->channels != ch) {
		free(tiles);
		return false;
	}
	i32 x0 = w, y0 = h, x1 = 0, y1 = 0;
	for (i32 i = 0; i < n; ++i) {
		if (hash128_eq(tiles[i], b->tiles[i])) {
			continue;
		}
		i32 x, y, tw, th;
		tl_tiles_rect(w, h, i % tl_tiles_per_channel(), &x, &y, &tw, &th);
		x0 = x < x0 ? x : x0;
		y0 = y < y0 ? y : y0;
		x1 = x + tw > x1 ? x + tw : x1;
		y1 = y + th > y1 ? y + th : y1;
	}
	free(tiles);
	if (x1 <= x0 || y1 <= y0) {
		return false;
	}
	out[0] = (x0 - 1) / (f32)w;
	out[1] = (y0 - 1) / (f32)h;
	out[2] = (x1 + 1) / (f32)w;
	out[3] = (y1 + 1) / (f32)h;
	for (i32 i = 0; i < 4; ++i) {
		out[i] = out[i] < 0.0 ? 0.0 : out[i] > 1.0 ? 1.0 : out[i];
	}
	return true;
}
