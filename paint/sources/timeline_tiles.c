#include "global.h"

// Layer states as tile maps (stroke_rec.h). Each channel texture of a layer (base, nor, pack; a mask has one) is cut into
// HISTORY_TILES_GRID^2 tiles, a tile's raw texel bytes are an HSTORE_TILE blob. A tile map lists the tiles of a state.
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

// Temporary until hstore_set_children lands: captures keep their tile references (never freed) instead of handing them
// to the map. Delete with the merge of stroke-recording that brings the children API.
#define TL_TILES_CHILDREN_SHIM 1
#if TL_TILES_CHILDREN_SHIM
typedef void (*hstore_visit_fn)(hash128_t child, void *ud);
#endif

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
#ifdef IRON_METAL
static gpu_transfer_t *tl_tiles_transfer = NULL;
#endif

static i32 tl_tiles_per_channel() {
	return HISTORY_TILES_GRID * HISTORY_TILES_GRID;
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

// The hash of a tile's bytes, the tile stored (a reference added) unless it is uniform
static hash128_t tl_tiles_store(const u8 *p, i32 bytes, i32 bpt) {
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
	return hstore_put(HSTORE_TILE, p, bytes);
}

// Texel bytes of tile h into p, false when missing
static bool tl_tiles_load(hash128_t h, u8 *p, i32 bytes, i32 bpt) {
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
	return tex->width % HISTORY_TILES_GRID == 0 && tex->height % HISTORY_TILES_GRID == 0;
}

hash128_t timeline_tiles_capture(paint_target_t *src, hash128_t prev, const u64 *dirty) {
#ifdef IRON_METAL
	if (!tl_tiles_target_ok(src)) {
		return HASH128_NONE;
	}
	gpu_texture_t *tex      = src->out[0];
	i32            per      = tl_tiles_per_channel();
	i32            channels = src->channels;
	i32            total    = channels * per;
	i32            bpt      = gpu_texture_format_size(tex->format);
	i32            tw       = tex->width / HISTORY_TILES_GRID;
	i32            th       = tex->height / HISTORY_TILES_GRID;
	i32            bytes    = tw * th * bpt;

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

	if (tl_tiles_transfer == NULL) {
		tl_tiles_transfer = gpu_transfer_create(0);
	}
	draw_saved_t saved = draw_suspend();
	for (i32 b0 = 0; b0 < n; b0 += TL_TILES_BATCH) {
		i32 bn = n - b0 < TL_TILES_BATCH ? n - b0 : TL_TILES_BATCH;
		gpu_transfer_map(tl_tiles_transfer, (size_t)bn * channels * bytes);
		gpu_copy_regions_begin();
		for (i32 i = 0; i < bn; ++i) {
			i32 t = list[b0 + i];
			for (i32 c = 0; c < channels; ++c) {
				gpu_copy_region_to_transfer(tl_tiles_transfer, src->out[c], (t % HISTORY_TILES_GRID) * tw, (t / HISTORY_TILES_GRID) * th, tw, th,
				                            ((size_t)i * channels + c) * bytes);
			}
		}
		gpu_copy_regions_end();
		gpu_execute_and_wait();
		u8 *mem = gpu_transfer_map(tl_tiles_transfer, 0);
		for (i32 i = 0; i < bn; ++i) {
			i32 t = list[b0 + i];
			for (i32 c = 0; c < channels; ++c) {
				i32 idx       = c * per + t;
				m.tiles[idx]  = tl_tiles_store(mem + ((size_t)i * channels + c) * bytes, bytes, bpt);
				read[idx]     = true;
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
#if TL_TILES_CHILDREN_SHIM
		if (delta) {
			hstore_ref(prev);
		}
#else
		for (i32 idx = 0; idx < total; ++idx) {
			if (read[idx]) {
				hstore_unref(m.tiles[idx]); // This capture's own
			}
		}
#endif
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
#else
	return HASH128_NONE;
#endif
}

// Writes the tiles of map into dst, only the ones that differ from prev when dst holds prev now (NONE: every tile)
bool timeline_tiles_restore_from(hash128_t map, hash128_t prev, paint_target_t *dst) {
#ifdef IRON_METAL
	if (!tl_tiles_target_ok(dst)) {
		return false;
	}
	if (!hash128_is_none(prev) && hash128_eq(map, prev)) {
		return true;
	}
	tl_tiles_map_t *m   = tl_tiles_resolve(map);
	gpu_texture_t  *tex = dst->out[0];
	if (m == NULL || m->channels != dst->channels || m->width != tex->width || m->height != tex->height || m->format != (i32)tex->format) {
		return false;
	}
	i32        per   = tl_tiles_per_channel();
	i32        total = m->channels * per;
	hash128_t *want  = malloc(total * sizeof(hash128_t));
	memcpy(want, m->tiles, total * sizeof(hash128_t));
	hash128_t      *have = NULL;
	tl_tiles_map_t *p    = tl_tiles_resolve(prev);
	if (p != NULL && p->channels == m->channels && p->width == tex->width && p->height == tex->height && p->format == (i32)tex->format) {
		have = malloc(total * sizeof(hash128_t));
		memcpy(have, p->tiles, total * sizeof(hash128_t));
	}
	i32 bpt   = gpu_texture_format_size(tex->format);
	i32 tw    = tex->width / HISTORY_TILES_GRID;
	i32 th    = tex->height / HISTORY_TILES_GRID;
	i32 bytes = tw * th * bpt;

	i32 *list = malloc(total * sizeof(i32));
	i32  n    = 0;
	for (i32 idx = 0; idx < total; ++idx) {
		if (have == NULL || !hash128_eq(have[idx], want[idx])) {
			list[n++] = idx;
		}
	}
	if (tl_tiles_transfer == NULL) {
		tl_tiles_transfer = gpu_transfer_create(0);
	}
	bool         ok    = true;
	draw_saved_t saved = draw_suspend();
	for (i32 b0 = 0; b0 < n; b0 += TL_TILES_BATCH) {
		i32 bn  = n - b0 < TL_TILES_BATCH ? n - b0 : TL_TILES_BATCH;
		u8 *mem = gpu_transfer_map(tl_tiles_transfer, (size_t)bn * bytes);
		for (i32 i = 0; i < bn; ++i) {
			i32 idx = list[b0 + i];
			if (!tl_tiles_load(want[idx], mem + (size_t)i * bytes, bytes, bpt)) {
				ok = false;
				memset(mem + (size_t)i * bytes, 0, bytes);
			}
		}
		gpu_copy_regions_begin();
		for (i32 i = 0; i < bn; ++i) {
			i32 idx = list[b0 + i];
			i32 t   = idx % per;
			gpu_copy_region_from_transfer(tl_tiles_transfer, dst->out[idx / per], (t % HISTORY_TILES_GRID) * tw, (t / HISTORY_TILES_GRID) * th, tw, th,
			                              (size_t)i * bytes);
		}
		gpu_copy_regions_end();
		gpu_execute_and_wait(); // The memory is filled again by the next batch
	}
	draw_resume(saved);
	free(list);
	free(want);
	free(have);
	return ok;
#else
	return false;
#endif
}

bool timeline_tiles_restore(hash128_t tilemap, paint_target_t *dst) {
	return timeline_tiles_restore_from(tilemap, HASH128_NONE, dst);
}
