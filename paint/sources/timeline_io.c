#include "global.h"

// Timelines saved with v31 projects (project_t.timelines): the timelines of the layers, and of the layers merged or
// applied into them, with their entries and checkpoints. Payloads and tile maps are blobs of the store, written into
// the container with it (io/project_container.c). Little endian:
//   "MSTL" u32 version, u32 count, per timeline:
//     u32 uid, u32 layer_uid, u32 channels, u32 parent_tl, i32 parent_pos, i32 count, i32 cursor, i32 floor,
//     i32 cp_count, f32 cost_since_cp
//     count entries of TIMELINE_IO_ENTRY bytes: u16 kind, u16 flags, u32 step_serial, u64 payload lo, hi,
//       f32 uv_bounds[4], f32 halo_uv, u32 mesh_epoch, u32 src_tl, i32 src_pos, i32 res_w, i32 res_h, f32 cost_ms,
//       u8 mm_tier, 3 bytes 0
//     cp_count checkpoints of TIMELINE_IO_CP bytes: i32 pos, i32 w, i32 h, i32 format, u64 tilemap lo, hi,
//       u8 exact_tier, u8 pinned, 2 bytes 0

#define TIMELINE_IO_VERSION 1
#define TIMELINE_IO_HEAD    40
#define TIMELINE_IO_ENTRY   72
#define TIMELINE_IO_CP      36

typedef struct timeline_io_buf {
	u8 *p;
	u32 n;
	u32 cap;
} timeline_io_buf_t;

static void tio_put(timeline_io_buf_t *b, const void *v, u32 len) {
	if (b->n + len > b->cap) {
		b->cap = (b->n + len) * 2 + 256;
		b->p   = realloc(b->p, b->cap);
	}
	memcpy(b->p + b->n, v, len);
	b->n += len;
}

static void tio_u32(timeline_io_buf_t *b, u32 v) {
	tio_put(b, &v, 4);
}

static void tio_hash(timeline_io_buf_t *b, hash128_t h) {
	tio_put(b, &h.lo, 8);
	tio_put(b, &h.hi, 8);
}

// Timelines of the project: the layers', then the ones their entries read (merge, apply mask), each once
static i32 timeline_io_collect(timeline_t ***out) {
	i32          n    = 0;
	i32          cap  = 16;
	timeline_t **list = malloc(sizeof(timeline_t *) * cap);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		timeline_t *tl = timeline_find(g_project->_->layers->buffer[i]->timeline);
		bool        in = tl == NULL;
		for (i32 j = 0; j < n && !in; ++j) {
			in = list[j] == tl;
		}
		if (!in) {
			if (n == cap) {
				cap *= 2;
				list = realloc(list, sizeof(timeline_t *) * cap);
			}
			list[n++] = tl;
		}
	}
	for (i32 k = 0; k < n; ++k) { // Grows as sources are found
		timeline_t *tl = list[k];
		for (i32 e = -1; e < tl->count; ++e) {
			timeline_t *src = timeline_find(e < 0 ? tl->parent_tl : tl->entries[e].src_tl);
			bool        in  = src == NULL;
			for (i32 j = 0; j < n && !in; ++j) {
				in = list[j] == src;
			}
			if (!in) {
				if (n == cap) {
					cap *= 2;
					list = realloc(list, sizeof(timeline_t *) * cap);
				}
				list[n++] = src;
			}
		}
	}
	*out = list;
	return n;
}

// The timelines as bytes (project_t.timelines), NULL when there are none
u8_array_t *timeline_io_encode() {
	timeline_finalize();
	timeline_t **list;
	i32          n = timeline_io_collect(&list);
	if (n == 0) {
		free(list);
		return NULL;
	}
	timeline_io_buf_t b = {0};
	tio_put(&b, "MSTL", 4);
	tio_u32(&b, TIMELINE_IO_VERSION);
	tio_u32(&b, (u32)n);
	for (i32 i = 0; i < n; ++i) {
		timeline_t *tl = list[i];
		u32         head[10] = {tl->uid, tl->layer_uid, tl->channels, tl->parent_tl, (u32)tl->parent_pos, (u32)tl->count, (u32)tl->cursor,
		                        (u32)tl->floor, (u32)tl->cp_count, 0};
		memcpy(&head[9], &tl->cost_since_cp, 4);
		tio_put(&b, head, TIMELINE_IO_HEAD);
		for (i32 k = 0; k < tl->count; ++k) {
			tl_entry_t *e   = &tl->entries[k];
			u32         ks  = (u32)e->kind | ((u32)e->flags << 16);
			u8          tier[4] = {e->mm_tier, 0, 0, 0};
			tio_u32(&b, ks);
			tio_u32(&b, e->step_serial);
			tio_hash(&b, e->payload);
			tio_put(&b, e->uv_bounds, 16);
			tio_put(&b, &e->halo_uv, 4);
			tio_u32(&b, e->mesh_epoch);
			tio_u32(&b, e->src_tl);
			tio_u32(&b, (u32)e->src_pos);
			tio_u32(&b, (u32)e->res_w);
			tio_u32(&b, (u32)e->res_h);
			tio_put(&b, &e->cost_ms, 4);
			tio_put(&b, tier, 4);
		}
		for (i32 k = 0; k < tl->cp_count; ++k) {
			tl_checkpoint_t *c     = &tl->cps[k];
			u8               f[4]  = {c->exact_tier, c->pinned ? 1 : 0, 0, 0};
			tio_u32(&b, (u32)c->pos);
			tio_u32(&b, (u32)c->w);
			tio_u32(&b, (u32)c->h);
			tio_u32(&b, (u32)c->format);
			tio_hash(&b, c->tilemap);
			tio_put(&b, f, 4);
		}
	}
	free(list);
	u8_array_t *a = calloc(1, sizeof(u8_array_t));
	a->buffer     = b.p;
	a->length     = b.n;
	a->capacity   = b.cap;
	return a;
}

static bool tio_get(const u8 *p, u32 len, u32 *at, void *v, u32 n) {
	if (*at + n > len) {
		return false;
	}
	memcpy(v, p + *at, n);
	*at += n;
	return true;
}

// The timelines of a loaded project, after its layers (their timeline uids) are made. Each entry and checkpoint takes
// the reference of its blob back at hstore_rebuild_refs (timeline_roots). False when the bytes do not read.
bool timeline_io_decode(u8_array_t *a) {
	if (a == NULL || a->length < 12 || memcmp(a->buffer, "MSTL", 4) != 0) {
		return false;
	}
	const u8 *p   = a->buffer;
	u32       len = a->length;
	u32       at  = 4;
	u32       version;
	u32       n;
	tio_get(p, len, &at, &version, 4);
	tio_get(p, len, &at, &n, 4);
	if (version != TIMELINE_IO_VERSION) {
		return false;
	}
	for (u32 i = 0; i < n; ++i) {
		u32 head[10];
		if (!tio_get(p, len, &at, head, TIMELINE_IO_HEAD)) {
			return false;
		}
		i32 count    = (i32)head[5];
		i32 cp_count = (i32)head[8];
		if (count < 0 || cp_count < 0 || (u64)count * TIMELINE_IO_ENTRY + (u64)cp_count * TIMELINE_IO_CP > len - at) {
			return false;
		}
		timeline_t *tl = timeline_create(head[1], (i32)head[2]);
		tl->uid        = head[0]; // As saved: layers, entries and steps name it
		tl->parent_tl  = head[3];
		tl->parent_pos = (i32)head[4];
		tl->floor      = (i32)head[7];
		memcpy(&tl->cost_since_cp, &head[9], 4);
		tl->refs    = 0; // Counted below
		tl->cap     = count > 0 ? count : 1;
		tl->entries = calloc(tl->cap, sizeof(tl_entry_t));
		for (i32 k = 0; k < count; ++k) {
			tl_entry_t *e = &tl->entries[k];
			u32         ks;
			u8          tier[4];
			tio_get(p, len, &at, &ks, 4);
			e->kind  = (u16)(ks & 0xffff);
			e->flags = (u16)(ks >> 16);
			tio_get(p, len, &at, &e->step_serial, 4);
			tio_get(p, len, &at, &e->payload.lo, 8);
			tio_get(p, len, &at, &e->payload.hi, 8);
			tio_get(p, len, &at, e->uv_bounds, 16);
			tio_get(p, len, &at, &e->halo_uv, 4);
			tio_get(p, len, &at, &e->mesh_epoch, 4);
			tio_get(p, len, &at, &e->src_tl, 4);
			tio_get(p, len, &at, &e->src_pos, 4);
			tio_get(p, len, &at, &e->res_w, 4);
			tio_get(p, len, &at, &e->res_h, 4);
			tio_get(p, len, &at, &e->cost_ms, 4);
			tio_get(p, len, &at, tier, 4);
			e->mm_tier = tier[0];
		}
		tl->count  = count;
		tl->cursor = (i32)head[6] <= count ? (i32)head[6] : count;
		tl->cp_cap = cp_count > 0 ? cp_count : 1;
		tl->cps    = calloc(tl->cp_cap, sizeof(tl_checkpoint_t));
		for (i32 k = 0; k < cp_count; ++k) {
			tl_checkpoint_t *c = &tl->cps[k];
			u8               f[4];
			tio_get(p, len, &at, &c->pos, 4);
			tio_get(p, len, &at, &c->w, 4);
			tio_get(p, len, &at, &c->h, 4);
			tio_get(p, len, &at, &c->format, 4);
			tio_get(p, len, &at, &c->tilemap.lo, 8);
			tio_get(p, len, &at, &c->tilemap.hi, 8);
			tio_get(p, len, &at, f, 4);
			c->exact_tier = f[0];
			c->pinned     = f[1] != 0;
		}
		tl->cp_count = cp_count;
	}

	// References: the layers owning a timeline, the entries reading one
	timeline_t **list;
	i32          m = timeline_io_collect(&list);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		timeline_ref(timeline_find(g_project->_->layers->buffer[i]->timeline));
	}
	for (i32 i = 0; i < m; ++i) {
		for (i32 k = 0; k < list[i]->count; ++k) {
			timeline_ref(timeline_find(list[i]->entries[k].src_tl));
		}
	}
	free(list);
	return true;
}

// The blobs the timelines hold, one per entry payload and checkpoint tile map, for hstore_rebuild_refs and saves.
// malloc'd, NULL when none.
hash128_t *timeline_roots(i32 *n) {
	timeline_t **list;
	i32          m     = timeline_io_collect(&list);
	i32          cap   = 64;
	hash128_t   *roots = malloc(sizeof(hash128_t) * cap);
	*n                 = 0;
	for (i32 i = 0; i < m; ++i) {
		timeline_t *tl = list[i];
		for (i32 k = 0; k < tl->count + tl->cp_count; ++k) {
			hash128_t h = k < tl->count ? tl->entries[k].payload : tl->cps[k - tl->count].tilemap;
			if (hash128_is_none(h) || (h.hi & HASH128_INLINE_BIT) != 0) {
				continue;
			}
			if (*n == cap) {
				cap *= 2;
				roots = realloc(roots, sizeof(hash128_t) * cap);
			}
			roots[(*n)++] = h;
		}
	}
	free(list);
	return roots;
}

// ---- Test hooks (paint/tests/timeline_persist.c) ----

// 31 bits of the tile map kept for the state of l at pos, 0 when none
i32 timeline_io_test_state(slot_layer_t *l, i32 pos) {
	timeline_t *tl = timeline_of(l);
	hash128_t   h  = tl != NULL ? timeline_state(tl, pos) : HASH128_NONE;
	return (i32)(h.lo & 0x7fffffff);
}
