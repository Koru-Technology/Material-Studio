#include "global.h"

// Timelines (stroke_rec.h): the entries that define the pixels of a paint layer or paint mask. Fill layers and groups
// have none. The registry holds every timeline of the project by uid, layers, steps and layer records point at them.
//
// Until replay exists every position of a timeline has a tile map: a TL_RASTER entry's payload is the state after it,
// any other entry gets a checkpoint after it, the pinned checkpoint at 0 is the state before the first entry. They are
// captured at finalize (the next push, undo or redo), once the change is drawn: timeline_finalize.
//
// Payload and tile map hashes held by entries and checkpoints are references of the timeline, released with them.

typedef struct tl_pending {
	u32  tl;      // Timeline uid
	i32  index;   // Entry whose state after is captured
	u32  layer;   // Layer uid
	u32  serial;  // Step
	bool has_dirty;
	u64  dirty[HISTORY_TILES_GRID * HISTORY_TILES_GRID / 64];
} tl_pending_t;

static void timeline_policy(struct timeline *tl);

static timeline_t  **timeline_all       = NULL;
static i32           timeline_all_count = 0;
static i32           timeline_all_cap   = 0;
static tl_pending_t *timeline_pending   = NULL;
static i32           timeline_pending_count = 0;
static i32           timeline_pending_cap   = 0;

timeline_t *timeline_create(u32 layer_uid, i32 channels) {
	timeline_t *tl = calloc(1, sizeof(timeline_t));
	tl->uid        = project_uid_take();
	tl->layer_uid  = layer_uid;
	tl->channels   = channels;
	tl->refs       = 1;
	if (timeline_all_count == timeline_all_cap) {
		timeline_all_cap = timeline_all_cap == 0 ? 16 : timeline_all_cap * 2;
		timeline_all     = realloc(timeline_all, sizeof(timeline_t *) * timeline_all_cap);
	}
	timeline_all[timeline_all_count++] = tl;
	return tl;
}

timeline_t *timeline_find(u32 uid) {
	for (i32 i = 0; uid != 0 && i < timeline_all_count; ++i) {
		if (timeline_all[i]->uid == uid) {
			return timeline_all[i];
		}
	}
	return NULL;
}

// The timeline made for the layer of uid last, NULL for none: a layer made again by redo takes its timeline back
timeline_t *timeline_find_layer(u32 layer_uid) {
	for (i32 i = timeline_all_count - 1; layer_uid != 0 && i >= 0; --i) {
		if (timeline_all[i]->layer_uid == layer_uid) {
			return timeline_all[i];
		}
	}
	return NULL;
}

static bool timeline_fill_ok = false; // timeline_allow_fill

// Paint layers and paint masks have a timeline, fill layers, fill masks and groups not
bool timeline_layer_paints(slot_layer_t *l) {
	return l != NULL && l->texpaint != NULL && (l->fill_material == NULL || timeline_fill_ok) &&
	       (slot_layer_is_mask(l) || (l->texpaint_nor != NULL && l->texpaint_pack != NULL));
}

// A fill layer or mask about to be painted into (apply mask) gets a timeline too: its render is the state it starts from
void timeline_allow_fill(bool on) {
	timeline_fill_ok = on;
}

timeline_t *timeline_of(slot_layer_t *l) {
	if (!timeline_layer_paints(l)) {
		return NULL;
	}
	return timeline_find(l->timeline);
}

static void timeline_entry_release(tl_entry_t *e) {
	hstore_unref(e->payload);
	if (e->src_tl != 0) {
		timeline_unref(timeline_find(e->src_tl));
	}
}

static void timeline_free(timeline_t *tl) {
	for (i32 i = 0; i < tl->count; ++i) {
		timeline_entry_release(&tl->entries[i]);
	}
	for (i32 i = 0; i < tl->cp_count; ++i) {
		hstore_unref(tl->cps[i].tilemap);
	}
	free(tl->entries);
	free(tl->cps);
	free(tl);
}

void timeline_ref(timeline_t *tl) {
	if (tl != NULL) {
		tl->refs++;
	}
}

void timeline_unref(timeline_t *tl) {
	if (tl == NULL || tl->refs == 0) {
		return;
	}
	if (--tl->refs > 0) {
		return;
	}
	for (i32 i = 0; i < timeline_all_count; ++i) {
		if (timeline_all[i] == tl) {
			timeline_all[i] = timeline_all[--timeline_all_count];
			break;
		}
	}
	timeline_free(tl);
}

// Entries past the cursor go (and their checkpoints): the redo branch
void timeline_truncate(timeline_t *tl) {
	for (i32 i = tl->cursor; i < tl->count; ++i) {
		timeline_entry_release(&tl->entries[i]);
	}
	tl->count = tl->cursor;
	i32 n     = 0;
	for (i32 i = 0; i < tl->cp_count; ++i) {
		if (tl->cps[i].pos <= tl->cursor) {
			tl->cps[n++] = tl->cps[i];
		}
		else {
			hstore_unref(tl->cps[i].tilemap);
		}
	}
	tl->cp_count = n;
	if (tl->floor > tl->count) {
		tl->floor = 0;
	}
	for (i32 i = 0; i < timeline_pending_count; ++i) {
		if (timeline_pending[i].tl == tl->uid && timeline_pending[i].index >= tl->count) {
			timeline_pending[i] = timeline_pending[--timeline_pending_count];
			--i;
		}
	}
}

void timeline_truncate_all() {
	for (i32 i = 0; i < timeline_all_count; ++i) {
		timeline_truncate(timeline_all[i]);
	}
}

void timeline_append(timeline_t *tl, tl_entry_t *e) {
	timeline_truncate(tl);
	if (tl->count == tl->cap) {
		tl->cap     = tl->cap == 0 ? 16 : tl->cap * 2;
		tl->entries = realloc(tl->entries, sizeof(tl_entry_t) * tl->cap);
	}
	tl->entries[tl->count++] = *e;
	tl->cursor               = tl->count;
	if (e->kind == TL_RASTER) {
		tl->floor = tl->count;
	}
	tl->cost_since_cp += e->cost_ms;
}

// The checkpoint at pos of the tile map's size is tilemap (the reference handed over), one there already is replaced.
// A position keeps a state per resolution: a resolution change goes back to the state it left byte for byte.
void timeline_checkpoint_set(timeline_t *tl, i32 pos, hash128_t tilemap, bool pinned) {
	i32 w = 0, h = 0, format = 0, channels = 0;
	timeline_tiles_info(tilemap, &w, &h, &format, &channels);
	tl_checkpoint_t cp = {.pos = pos, .w = w, .h = h, .format = format, .tilemap = tilemap, .exact_tier = 0, .pinned = pinned};
	tl->cost_since_cp  = 0.0;
	for (i32 i = 0; i < tl->cp_count; ++i) {
		if (tl->cps[i].pos == pos && tl->cps[i].w == w && tl->cps[i].h == h) {
			hstore_unref(tl->cps[i].tilemap);
			cp.pinned  = pinned || tl->cps[i].pinned;
			tl->cps[i] = cp;
			return;
		}
	}
	if (tl->cp_count == tl->cp_cap) {
		tl->cp_cap = tl->cp_cap == 0 ? 8 : tl->cp_cap * 2;
		tl->cps    = realloc(tl->cps, sizeof(tl_checkpoint_t) * tl->cp_cap);
	}
	i32 i = tl->cp_count++;
	for (; i > 0 && tl->cps[i - 1].pos > pos; --i) { // Sorted by position
		tl->cps[i] = tl->cps[i - 1];
	}
	tl->cps[i] = cp;
}

// Tile map of the state at pos at w x h when one is kept (a checkpoint, a TL_RASTER payload), NONE otherwise
hash128_t timeline_state_at(timeline_t *tl, i32 pos, i32 w, i32 h) {
	if (tl == NULL || pos < 0 || pos > tl->count) {
		return HASH128_NONE;
	}
	for (i32 i = 0; i < tl->cp_count; ++i) {
		if (tl->cps[i].pos == pos && tl->cps[i].w == w && tl->cps[i].h == h) {
			return tl->cps[i].tilemap;
		}
	}
	tl_entry_t *e = pos > 0 ? &tl->entries[pos - 1] : NULL;
	if (e != NULL && e->kind == TL_RASTER && !hash128_is_none(e->payload) && e->res_w == w && e->res_h == h) {
		return e->payload;
	}
	return HASH128_NONE;
}

// Tile map of the state at pos when one is kept, at the working resolution first, NONE otherwise
hash128_t timeline_state(timeline_t *tl, i32 pos) {
	hash128_t s = timeline_state_at(tl, pos, config_get_texture_res_x(), config_get_texture_res_y());
	if (!hash128_is_none(s) || tl == NULL || pos < 0 || pos > tl->count) {
		return s;
	}
	for (i32 i = 0; i < tl->cp_count; ++i) {
		if (tl->cps[i].pos == pos) {
			return tl->cps[i].tilemap;
		}
	}
	if (pos > 0 && tl->entries[pos - 1].kind == TL_RASTER) {
		return tl->entries[pos - 1].payload;
	}
	return HASH128_NONE;
}

// The live textures of l as a target
void timeline_target_live(slot_layer_t *l, paint_target_t *t) {
	memset(t, 0, sizeof(paint_target_t));
	t->w          = l->texpaint->width;
	t->h          = l->texpaint->height;
	t->uv_rect[2] = 1.0;
	t->uv_rect[3] = 1.0;
	t->res_w      = t->w;
	t->res_h      = t->h;
	t->channels   = slot_layer_is_mask(l) ? 1 : 3;
	t->out[0]     = l->texpaint;
	if (t->channels == 3) {
		t->out[1] = l->texpaint_nor;
		t->out[2] = l->texpaint_pack;
	}
}

// Write stamps of the live textures of l (gpu_texture_written): the same key, the same texels. Every write counts, a
// change no step recorded too.
static u64 timeline_live_key(slot_layer_t *l) {
	paint_target_t t;
	timeline_target_live(l, &t);
	u64 h = HASH_FNV1A_BASIS;
	for (i32 c = 0; c < t.channels; ++c) {
		u64 s = t.out[c] != NULL ? t.out[c]->written : 0;
		h     = hash_fnv1a(h, &s, sizeof(s));
	}
	return h;
}

// timeline_live_map reads nothing back for l
bool timeline_live_map_fresh(slot_layer_t *l) {
	return l->texpaint == NULL ||
	       (l->live_map_key == timeline_live_key(l) && !hash128_is_none(l->live_map) && hstore_has(l->live_map));
}

// The tile map of the live textures of l, a reference added: the last capture while no texture of l was written since,
// otherwise a capture (a readback of the whole layer). Saves and session heads read the layers without a timeline state
// this way: a layer costs a readback once a change, not once a head. NONE when it can not be captured.
hash128_t timeline_live_map(slot_layer_t *l) {
	if (timeline_live_map_fresh(l)) {
		hstore_ref(l->live_map);
		return l->live_map;
	}
	u64            key = timeline_live_key(l); // Before: the capture may suspend and resume a pass drawing into l
	paint_target_t t;
	timeline_target_live(l, &t);
	hash128_t h     = timeline_tiles_capture(&t, HASH128_NONE, NULL);
	l->live_map     = h;
	l->live_map_key = key;
	return h;
}

// The live textures of l were restored from map h (an open): timeline_live_map has it without a readback
void timeline_live_map_set(slot_layer_t *l, hash128_t h) {
	l->live_map     = h;
	l->live_map_key = timeline_live_key(l);
}

// The timeline of l, made at its first change: the pinned checkpoint at 0 is l as it is now
timeline_t *timeline_ensure(slot_layer_t *l) {
	if (!timeline_layer_paints(l)) {
		return NULL;
	}
	timeline_t *tl = timeline_find(l->timeline);
	if (tl != NULL) {
		return tl;
	}
	tl          = timeline_create(l->uid, slot_layer_is_mask(l) ? 1 : 3);
	l->timeline = tl->uid;
	timeline_checkpoint_set(tl, 0, timeline_live_map(l), true);
	return tl;
}

// The step moved the cursor of tl from before to after: undo and redo move it back and forth
static void timeline_step_move(history_step_t *step, timeline_t *tl, i32 before, i32 after) {
	if (step == NULL) {
		return;
	}
	if (step->tl_moves == NULL) {
		step->tl_moves = i32_array_create(0);
	}
	for (i32 i = 0; i < step->tl_moves->length; i += 3) {
		if ((u32)step->tl_moves->buffer[i] == tl->uid) {
			step->tl_moves->buffer[i + 2] = after; // Moved again by the same step
			return;
		}
	}
	i32_array_push(step->tl_moves, (i32)tl->uid);
	i32_array_push(step->tl_moves, before);
	i32_array_push(step->tl_moves, after);
}

static history_step_t *timeline_last_step() {
	return history_steps != NULL && history_steps->length > 0 ? history_steps->buffer[history_steps->length - 1] : NULL;
}

static void timeline_pending_add(timeline_t *tl, i32 index, slot_layer_t *l, history_step_t *step) {
	if (timeline_pending_count == timeline_pending_cap) {
		timeline_pending_cap = timeline_pending_cap == 0 ? 8 : timeline_pending_cap * 2;
		timeline_pending     = realloc(timeline_pending, sizeof(tl_pending_t) * timeline_pending_cap);
	}
	tl_pending_t *p = &timeline_pending[timeline_pending_count++];
	memset(p, 0, sizeof(tl_pending_t));
	p->tl     = tl->uid;
	p->index  = index;
	p->layer  = l->uid;
	p->serial = step != NULL ? step->serial : 0;
}

// Appends an entry of kind to the timeline of l for the step just pushed, its state after is captured at finalize
tl_entry_t *timeline_record(slot_layer_t *l, i32 kind, hash128_t payload, const f32 *uv_bounds, u16 flags) {
	timeline_t *tl = timeline_ensure(l);
	if (tl == NULL) {
		hstore_unref(payload);
		return NULL;
	}
	history_step_t *step = timeline_last_step();
	tl_entry_t      e    = {.kind        = kind,
	                        .flags       = flags,
	                        .step_serial = step != NULL ? step->serial : 0,
	                        .payload     = payload,
	                        .uv_bounds   = {0.0, 0.0, 1.0, 1.0},
	                        .res_w       = l->texpaint->width,
	                        .res_h       = l->texpaint->height};
	if (uv_bounds != NULL) {
		memcpy(e.uv_bounds, uv_bounds, sizeof(e.uv_bounds));
	}
	i32 before = tl->cursor;
	timeline_append(tl, &e);
	timeline_step_move(step, tl, before, tl->cursor);
	timeline_pending_add(tl, tl->count - 1, l, step);
	return &tl->entries[tl->count - 1];
}

// A path layer repainted (util_path.c): its pixels changed with no step of their own, the change goes with the last
// step as a floor (exports and undo read it from the timeline). Repaints in a row (a point dragged) share one entry,
// its state taken again.
void timeline_record_path(slot_layer_t *l) {
	timeline_t     *tl   = timeline_find(l->timeline);
	history_step_t *step = timeline_last_step();
	tl_entry_t     *last = tl != NULL && tl->count > 0 && tl->cursor == tl->count ? &tl->entries[tl->count - 1] : NULL;
	if (last != NULL && last->kind == TL_PATH && last->step_serial == (step != NULL ? step->serial : 0)) {
		timeline_pending_add(tl, tl->count - 1, l, step);
		return;
	}
	timeline_record(l, TL_PATH, HASH128_NONE, NULL, TL_FLAG_NONLOCAL);
}

// history_paint(): the stroke is a TL_RASTER entry, its tile delta comes at finalize
void timeline_stroke_begin(history_step_t *step, slot_layer_t *l) {
	timeline_record(l, TL_RASTER, HASH128_NONE, NULL, 0);
}

// The tiles the stroke of step changed (history_tiles_finalize), the capture reads only them
void timeline_stroke_dirty(history_step_t *step, i32_array_t *tiles) {
	for (i32 i = 0; i < timeline_pending_count; ++i) {
		tl_pending_t *p = &timeline_pending[i];
		if (p->serial != step->serial) {
			continue;
		}
		p->has_dirty = true;
		memset(p->dirty, 0, sizeof(p->dirty));
		for (i32 j = 0; tiles != NULL && j < tiles->length; j += 2) {
			i32 t = tiles->buffer[j];
			p->dirty[t >> 6] |= 1ULL << (t & 63);
		}
	}
}

// Captures the states after the entries recorded since the last finalize
// Captures timeline_finalize has to make: a state after an entry, a stroke still being stored
bool timeline_captures_pending() {
	return timeline_pending_count > 0 || paint_rec_active();
}

void timeline_finalize() {
	paint_rec_flush(); // A stroke whose dabs are still read back becomes its entry first
	for (i32 i = 0; i < timeline_pending_count; ++i) { // In order: a capture reads the state the one before kept
		tl_pending_t p = timeline_pending[i];
		timeline_t  *tl = timeline_find(p.tl);
		slot_layer_t *l = slot_layer_find_uid(p.layer);
		if (tl == NULL || l == NULL || l->timeline != tl->uid || p.index >= tl->count) {
			continue; // Gone meanwhile, the entry stays without a state
		}
		bool was         = timeline_fill_ok;
		timeline_fill_ok = was || tl->entries[p.index].kind == TL_FILL; // To fill: the layer shows the fill the entry renders
		bool paints      = timeline_layer_paints(l);
		timeline_fill_ok = was;
		if (!paints) {
			continue; // Turned procedural meanwhile
		}
		paint_target_t t;
		timeline_target_live(l, &t);
		hash128_t prev = timeline_state_at(tl, p.index, t.res_w, t.res_h);
		hash128_t h    = timeline_tiles_capture(&t, prev, p.has_dirty ? p.dirty : NULL);
		tl_entry_t *e  = &tl->entries[p.index];
		if (e->kind == TL_RASTER && hash128_is_none(e->payload)) {
			e->payload = h;
		}
		else {
			timeline_checkpoint_set(tl, p.index + 1, h, false);
		}
	}
	i32 n                  = timeline_pending_count;
	u32 done[64];
	for (i32 i = 0; i < n && i < 64; ++i) {
		done[i] = timeline_pending[i].tl;
	}
	timeline_pending_count = 0;
	for (i32 i = 0; i < n && i < 64; ++i) {
		timeline_t *tl = timeline_find(done[i]);
		if (tl != NULL && (i == 0 || done[i - 1] != done[i])) {
			timeline_policy(tl);
		}
	}
}

// Undo and redo of step: the cursors it moved go back or forth
void timeline_step_apply(history_step_t *step, bool undo) {
	for (i32 i = 0; step->tl_moves != NULL && i < step->tl_moves->length; i += 3) {
		timeline_t *tl = timeline_find((u32)step->tl_moves->buffer[i]);
		if (tl == NULL) {
			continue;
		}
		i32 pos = step->tl_moves->buffer[i + (undo ? 1 : 2)];
		if (pos >= 0 && pos <= tl->count) {
			tl->cursor = pos;
		}
	}
}

// Duplicate: dst starts with a copy of the entries and checkpoints of src up to its cursor (payloads shared by hash)
void timeline_fork(slot_layer_t *src, slot_layer_t *dst) {
	timeline_t *s = timeline_of(src);
	dst->timeline = 0;
	if (s == NULL || !timeline_layer_paints(dst)) {
		return;
	}
	timeline_finalize(); // The source's last state
	timeline_t *tl = timeline_create(dst->uid, s->channels);
	dst->timeline  = tl->uid;
	tl->cap        = s->cursor > 0 ? s->cursor : 1;
	tl->entries    = malloc(sizeof(tl_entry_t) * tl->cap);
	for (i32 i = 0; i < s->cursor; ++i) {
		tl_entry_t *e = &s->entries[i];
		hstore_ref(e->payload);
		timeline_ref(timeline_find(e->src_tl));
		tl->entries[tl->count++] = *e;
	}
	tl->cursor = tl->count;
	tl->floor  = s->floor <= tl->count ? s->floor : 0;
	for (i32 i = 0; i < s->cp_count; ++i) {
		if (s->cps[i].pos <= tl->count) {
			hstore_ref(s->cps[i].tilemap);
			timeline_checkpoint_set(tl, s->cps[i].pos, s->cps[i].tilemap, s->cps[i].pinned);
		}
	}
}

// Replays that failed (a stroke whose shader no longer hashes as recorded, a record missing): the entry is a floor from
// then on, materialize starts after it. Runtime only, until the project is reset.
static u64 *timeline_failed       = NULL; // uid << 32 | index
static i32  timeline_failed_count = 0;
static i32  timeline_failed_cap   = 0;

static bool timeline_entry_failed(timeline_t *tl, i32 i) {
	u64 key = (u64)tl->uid << 32 | (u32)i;
	for (i32 k = 0; k < timeline_failed_count; ++k) {
		if (timeline_failed[k] == key) {
			return true;
		}
	}
	return false;
}

static void timeline_entry_fail(timeline_t *tl, i32 i) {
	if (timeline_entry_failed(tl, i)) {
		return;
	}
	if (timeline_failed_count == timeline_failed_cap) {
		timeline_failed_cap = timeline_failed_cap == 0 ? 16 : timeline_failed_cap * 2;
		timeline_failed     = realloc(timeline_failed, sizeof(u64) * timeline_failed_cap);
	}
	timeline_failed[timeline_failed_count++] = (u64)tl->uid << 32 | (u32)i;
	iron_log("[timeline] entry %d of timeline %u does not replay, it is a floor now", i, tl->uid);
}

// Entry i can be re-rendered at any resolution: a recorded stroke, an op with what it needs (timeline_ops_replays)
bool timeline_entry_replays(timeline_t *tl, i32 i) {
	tl_entry_t *e = &tl->entries[i];
	if (timeline_entry_failed(tl, i)) {
		return false;
	}
	if (e->kind == TL_STROKE) {
		return !hash128_is_none(e->payload);
	}
	return timeline_ops_replays(tl, i);
}

// The last position at or before pos that can not be re-rendered: after the last entry that does not replay (TL_RASTER
// among them), 0 when every entry does. A state of another resolution is resampled there.
i32 timeline_floor(timeline_t *tl, i32 pos) {
	if (tl == NULL) {
		return 0;
	}
	pos = pos < tl->count ? pos : tl->count;
	for (i32 i = pos - 1; i >= 0; --i) {
		if (!timeline_entry_replays(tl, i)) {
			return i + 1;
		}
	}
	return 0;
}

// The state the floor of pos starts from: kept at the floor at any resolution (the working one first), else the
// earliest kept after it. -1 when none is.
static i32 timeline_floor_state(timeline_t *tl, i32 fl, i32 pos, hash128_t *out) {
	for (i32 p = fl; p <= pos; ++p) {
		hash128_t h = timeline_state(tl, p);
		if (!hash128_is_none(h)) {
			*out = h;
			return p;
		}
	}
	*out = HASH128_NONE;
	return -1;
}

// The state at pos re-renders at any resolution without loss: its floor state is uniform tiles (a new layer's base),
// or an op that writes every texel (clear, fill, image) replays after the floor
bool timeline_upscalable(timeline_t *tl, i32 pos) {
	if (tl == NULL || pos < 0 || pos > tl->count) {
		return false;
	}
	i32 fl = timeline_floor(tl, pos);
	for (i32 i = fl; i < pos; ++i) {
		if (timeline_ops_overwrites(tl, i)) {
			return true;
		}
	}
	hash128_t h;
	return timeline_floor_state(tl, fl, pos, &h) == fl && timeline_tiles_uniform(h);
}

// Entry i applied to t: a stroke replayed (paint_replay.c), an op re-rendered (timeline_ops.c). False when it can not be.
// Strokes replay composite-fast (their material from a replay cache) at another resolution or part of the layer, and
// when fast (stroke refine): at the layer's own, as recorded.
bool timeline_entry_replay(timeline_t *tl, i32 i, paint_target_t *t, bool fast) {
	if (tl->entries[i].kind == TL_STROKE) {
		bool whole            = t->uv_rect[0] == 0.0 && t->uv_rect[1] == 0.0 && t->uv_rect[2] == 1.0 && t->uv_rect[3] == 1.0;
		paint_replay_opts_t o = {.exact = t->res_w == tl->entries[i].res_w && t->res_h == tl->entries[i].res_h};
		o.composite_fast      = fast || !o.exact || !whole;
		return paint_replay_entry(tl, i, t, &o);
	}
	return timeline_ops_replay(tl, i, t);
}

static gpu_texture_t *timeline_lo[3]; // Part of a state at its own resolution, resampled into a target

gpu_pipeline_t *timeline_copy_pipe(gpu_texture_t *t) {
	switch (t->format) {
	case GPU_TEXTURE_FORMAT_R8:
		return pipes_copy8;
	case GPU_TEXTURE_FORMAT_RGBA64:
		return pipes_copy64;
	case GPU_TEXTURE_FORMAT_RGBA128:
		return pipes_copy128;
	default:
		return pipes_copy;
	}
}

// State h written into t at t's resolution: restored when it has it, otherwise the part t covers (and a texel around,
// bilinear reads it) restored at h's own resolution and scaled into t. As the live layer scales (export_tiled_blit): the
// layer's edge texels repeat up to its edge, texels of t past it are cleared.
bool timeline_state_resample(hash128_t h, paint_target_t *t) {
	i32 sw = 0, sh = 0, format = 0, channels = 0;
	if (!timeline_tiles_info(h, &sw, &sh, &format, &channels) || channels != t->channels) {
		return false;
	}
	if (sw == t->res_w && sh == t->res_h) {
		return timeline_tiles_restore(h, t);
	}
	f32 u0 = t->uv_rect[0], v0 = t->uv_rect[1], u1 = t->uv_rect[2], v1 = t->uv_rect[3];
	// The texels of t inside the layer
	f32 ox = u0 * t->res_w, oy = v0 * t->res_h; // t's origin in texels of the layer at t's resolution
	i32 dx0 = (i32)fmaxf(0.0, ceilf(-ox)), dy0 = (i32)fmaxf(0.0, ceilf(-oy));
	i32 dx1 = (i32)fminf((f32)t->w, floorf(t->res_w - ox)), dy1 = (i32)fminf((f32)t->h, floorf(t->res_h - oy));
	if (dx1 <= dx0 || dy1 <= dy0) { // All past the edge
		draw_saved_t saved = draw_suspend();
		for (i32 k = 0; k < channels; ++k) {
			draw_begin(t->out[k], true, 0x00000000);
			draw_end();
		}
		draw_resume(saved);
		return true;
	}
	f32 kx = sw / (f32)t->res_w, ky = sh / (f32)t->res_h;
	f32 su0 = (ox + dx0) * kx, sv0 = (oy + dy0) * ky, su1 = (ox + dx1) * kx, sv1 = (oy + dy1) * ky; // In h's texels
	i32 x0 = (i32)floorf(su0) - 1, y0 = (i32)floorf(sv0) - 1;
	i32 x1 = (i32)ceilf(su1) + 1, y1 = (i32)ceilf(sv1) + 1;
	x0 = x0 < 0 ? 0 : x0; // Within the layer: the sampler repeats its edge
	y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > sw ? sw : x1;
	y1 = y1 > sh ? sh : y1;
	paint_target_t lo = {.w = x1 - x0, .h = y1 - y0, .res_w = sw, .res_h = sh, .channels = channels};
	lo.uv_rect[0]     = x0 / (f32)sw;
	lo.uv_rect[1]     = y0 / (f32)sh;
	lo.uv_rect[2]     = x1 / (f32)sw;
	lo.uv_rect[3]     = y1 / (f32)sh;
	for (i32 k = 0; k < channels; ++k) {
		util_texture_ensure_target(&timeline_lo[k], lo.w, lo.h, format);
		lo.out[k] = timeline_lo[k];
	}
	if (!timeline_tiles_restore(h, &lo)) {
		return false;
	}
	bool         full  = dx0 == 0 && dy0 == 0 && dx1 == t->w && dy1 == t->h;
	draw_saved_t saved = draw_suspend();
	for (i32 k = 0; k < channels; ++k) {
		draw_begin(t->out[k], !full, 0x00000000);
		draw_set_pipeline(timeline_copy_pipe(t->out[k]));
		draw_scaled_sub_image(timeline_lo[k], su0 - x0, sv0 - y0, su1 - su0, sv1 - sv0, dx0, dy0, dx1 - dx0, dy1 - dy0);
		draw_set_pipeline(NULL);
		draw_end();
	}
	draw_resume(saved);
	return true;
}

// Read reach of the strokes a materialize to pos may replay, in uv: a part of the layer replays grown by it. Whole
// layer (1) when one reads anywhere in it.
static f32 timeline_halo(timeline_t *tl, i32 pos) {
	f32 halo = 0.0;
	for (i32 i = timeline_floor(tl, pos); i < pos; ++i) {
		tl_entry_t *e = &tl->entries[i];
		if (e->kind != TL_STROKE) {
			continue;
		}
		if (e->flags & TL_FLAG_NONLOCAL) {
			return 1.0;
		}
		halo = e->halo_uv > halo ? e->halo_uv : halo;
	}
	return halo;
}

#define TIMELINE_GROWN_DEPTH 4 // Translate materializes again from inside a materialize

static gpu_texture_t *timeline_grown[TIMELINE_GROWN_DEPTH][3];
static i32            timeline_grown_depth = 0;

// A materialize as a job (timeline_job_t): set up, its start state written by the first step
static void timeline_job_setup(timeline_job_t *j, timeline_t *tl, i32 pos, paint_target_t *t, gpu_texture_t **grown) {
	memset(j, 0, sizeof(*j));
	j->tl    = tl;
	j->pos   = pos;
	j->t     = *t;
	j->g     = *t;
	j->state = tl == NULL || pos < 0 || pos > tl->count ? -1 : 0;
	if (j->state < 0) {
		return;
	}
	timeline_finalize();
	bool whole = t->uv_rect[0] <= 0.0 && t->uv_rect[1] <= 0.0 && t->uv_rect[2] >= 1.0 && t->uv_rect[3] >= 1.0;
	f32  halo  = whole ? 0.0 : timeline_halo(tl, pos);
	if (halo <= 0.0 || grown == NULL) {
		return;
	}
	// A part of the layer: replayed with the texels its strokes read around it, the middle copied into t
	f32 du = (t->uv_rect[2] - t->uv_rect[0]) / t->w;
	f32 dv = (t->uv_rect[3] - t->uv_rect[1]) / t->h;
	i32 px = (i32)ceilf(halo / du);
	i32 py = (i32)ceilf(halo / dv);
	if (halo >= 1.0) { // The whole layer and t's part of it
		px = (i32)ceilf(t->uv_rect[0] / du) > 0 ? (i32)ceilf(t->uv_rect[0] / du) : 0;
		py = (i32)ceilf(t->uv_rect[1] / dv) > 0 ? (i32)ceilf(t->uv_rect[1] / dv) : 0;
		i32 qx = (i32)ceilf((1.0 - t->uv_rect[2]) / du), qy = (i32)ceilf((1.0 - t->uv_rect[3]) / dv);
		px = px > qx ? px : qx;
		py = py > qy ? py : qy;
	}
	j->grown      = true;
	j->px         = px;
	j->py         = py;
	j->g.w        = t->w + px * 2;
	j->g.h        = t->h + py * 2;
	j->g.uv_rect[0] = t->uv_rect[0] - px * du;
	j->g.uv_rect[1] = t->uv_rect[1] - py * dv;
	j->g.uv_rect[2] = t->uv_rect[2] + px * du;
	j->g.uv_rect[3] = t->uv_rect[3] + py * dv;
	for (i32 k = 0; k < t->channels; ++k) {
		util_texture_ensure_target(&grown[k], j->g.w, j->g.h, t->out[k]->format);
		j->g.out[k] = grown[k];
	}
}

// Writes the state at pos into t (timeline_materialize), at t's resolution for its uv_rect, as a job across frames:
// grown (3 targets, kept by the caller) holds a part of the layer grown by what its strokes read around it, NULL for
// none. The first step writes the start state.
void timeline_job_begin(timeline_job_t *j, timeline_t *tl, i32 pos, paint_target_t *t, gpu_texture_t **grown) {
	timeline_job_setup(j, tl, pos, t, grown);
}

// The start: the latest of a state kept at t's resolution, an op writing every texel; at the floor or after it.
// Without one the floor's state of another resolution is resampled.
static bool timeline_job_start(timeline_job_t *j) {
	timeline_t     *tl   = j->tl;
	paint_target_t *t    = &j->g;
	i32             fl   = timeline_floor(tl, j->pos);
	i32             from = -1;
	hash128_t       h    = HASH128_NONE;
	for (i32 p = j->pos; p >= fl && from < 0; --p) {
		h = timeline_state_at(tl, p, t->res_w, t->res_h);
		if (!hash128_is_none(h)) {
			from = p;
		}
		else if (p < j->pos && timeline_ops_overwrites(tl, p)) {
			from = p; // Replaying it writes every texel, nothing to restore
		}
	}
	bool ok = true;
	if (from < 0) {
		from = timeline_floor_state(tl, fl, j->pos, &h);
		ok   = from >= 0 && timeline_state_resample(h, t);
	}
	else if (!hash128_is_none(h)) {
		ok = timeline_tiles_restore(h, t);
	}
	j->next = from;
	return ok;
}

// Replays entries until deadline (iron_time, at least one a step), then 0; 1 once t holds the state, -1 when it can not
// (t undefined). An entry that fails becomes a floor and it starts again. Before a deadline, strokes replayed
// composite-fast render their replay caches a step at a time (paint_replay_prepare), and the GPU's work is waited for
// after each: the deadline is what the steps cost the frame, not what they took to issue (a heavy material's stroke
// issued in 3 ms took 90 of the GPU on the iPad).
i32 timeline_job_step(timeline_job_t *j, f64 deadline) {
	if (j->state != 0 && j->state != 1) {
		return j->state == 2 ? 1 : -1;
	}
	bool timed = deadline < 1.0e30;
	bool moved = false;
	i32  d     = j->grown ? timeline_grown_depth++ : -1; // Materializes from inside one (translate) grow into the next
	while (j->state == 0 || j->next < j->pos) {
		if (moved && timed && iron_time() >= deadline) {
			break;
		}
		if (j->state == 0) {
			if (!timeline_job_start(j)) {
				j->state = -1;
				break;
			}
			j->state = 1;
			moved    = true;
			continue;
		}
		i32  i     = j->next;
		bool whole = j->g.uv_rect[0] == 0.0 && j->g.uv_rect[1] == 0.0 && j->g.uv_rect[2] == 1.0 && j->g.uv_rect[3] == 1.0;
		if (timed && j->tl->entries[i].kind == TL_STROKE && (j->g.res_w != j->tl->entries[i].res_w || j->g.res_h != j->tl->entries[i].res_h || !whole)) {
			i32 r = paint_replay_prepare(j->tl, i, &j->g); // Composite-fast (timeline_entry_replay): its caches a step on
			gpu_execute_and_wait();
			moved = true;
			if (r == 0) {
				continue;
			}
		}
		if (!timeline_entry_replay(j->tl, i, &j->g, false)) {
			timeline_entry_fail(j->tl, i);
			j->state = 0;
			continue;
		}
		if (timed) {
			gpu_execute_and_wait();
		}
		j->next++;
		moved = true;
	}
	if (d >= 0) {
		timeline_grown_depth--;
	}
	if (j->state == 1 && j->next >= j->pos) {
		j->state = 2;
		if (j->grown) {
			draw_saved_t saved = draw_suspend();
			gpu_copy_regions_begin();
			for (i32 k = 0; k < j->t.channels; ++k) {
				gpu_copy_region(j->t.out[k], 0, 0, j->g.out[k], j->px, j->py, j->t.w, j->t.h);
			}
			gpu_copy_regions_end();
			draw_resume(saved);
		}
	}
	return j->state == 2 ? 1 : j->state < 0 ? -1 : 0;
}

bool timeline_materialize(timeline_t *tl, i32 pos, paint_target_t *t) {
	timeline_job_t j;
	i32            d = timeline_grown_depth;
	timeline_job_setup(&j, tl, pos, t, d < TIMELINE_GROWN_DEPTH ? timeline_grown[d] : NULL);
	return timeline_job_step(&j, 1.0e300) == 1;
}

// The live layer of tl goes from the state at its cursor to the one at pos, only the tiles that differ are written
bool timeline_materialize_live(timeline_t *tl, slot_layer_t *l, i32 pos) {
	if (tl == NULL || l == NULL || !timeline_layer_paints(l)) {
		return false;
	}
	timeline_finalize();
	paint_target_t t;
	timeline_target_live(l, &t);
	hash128_t to   = timeline_state_at(tl, pos, t.res_w, t.res_h);
	hash128_t from = timeline_state_at(tl, tl->cursor, t.res_w, t.res_h);
	bool      ok   = !hash128_is_none(to) ? timeline_tiles_restore_from(to, from, &t) : timeline_materialize(tl, pos, &t);
	tl->cursor     = pos;
	return ok;
}

void timeline_reset_all() {
	timeline_pending_count = 0;
	timeline_failed_count  = 0;
	while (timeline_all_count > 0) {
		timeline_t *tl = timeline_all[--timeline_all_count];
		timeline_free(tl);
	}
	for (i32 i = 0; g_project != NULL && g_project->_ != NULL && g_project->_->layers != NULL && i < g_project->_->layers->length; ++i) {
		g_project->_->layers->buffer[i]->timeline = 0;
	}
	timeline_tiles_cache_reset();
	timeline_ops_reset();
}

// ---- Checkpoint policy and thinning ----
// Undo needs a state at every position only where replay can not go: at 0, at the cursor, after an entry that does
// not replay. Elsewhere a state is a shortcut: kept while recent, then only when the replay cost since the last one
// kept passes half the deep undo target (cost-based checkpoints). A state kept after dropped ones is encoded again
// against the last one kept, so the dropped ones' tile maps and the tiles only they held go.

static i32 timeline_keep_recent = 32; // Positions behind the cursor whose states all stay, < 0: no policy

void timeline_set_keep_recent(i32 n) {
	timeline_keep_recent = n;
}

// Replay budget of a deep undo hop: a checkpoint about every half of it
static f32 timeline_cp_target_ms() {
#ifdef IRON_IOS
	return 400.0;
#else
	return 250.0;
#endif
}

// Replay cost of entry i: measured, else estimated from its size
static f32 timeline_entry_cost(tl_entry_t *e) {
	if (e->cost_ms > 0.0) {
		return e->cost_ms;
	}
	return 10.0 * ((f32)e->res_w * e->res_h) / (2048.0 * 2048.0);
}

// The state at pos is needed, not a shortcut
static bool timeline_state_needed(timeline_t *tl, tl_checkpoint_t *c) {
	return c->pinned || c->pos == 0 || c->pos == tl->cursor || !timeline_entry_replays(tl, c->pos - 1);
}

// Checkpoint i goes. The next state kept at its size, a delta on it, is encoded again on the one kept before.
static void timeline_checkpoint_drop(timeline_t *tl, i32 i) {
	tl_checkpoint_t c = tl->cps[i];
	for (i32 k = i; k < tl->cp_count - 1; ++k) {
		tl->cps[k] = tl->cps[k + 1];
	}
	tl->cp_count--;
	hash128_t base = HASH128_NONE; // Kept before it at its size
	for (i32 k = i - 1; k >= 0 && hash128_is_none(base); --k) {
		if (tl->cps[k].w == c.w && tl->cps[k].h == c.h) {
			base = tl->cps[k].tilemap;
		}
	}
	for (i32 k = i; k < tl->cp_count; ++k) {
		tl_checkpoint_t *n = &tl->cps[k];
		if (n->w != c.w || n->h != c.h) {
			continue;
		}
		if (timeline_tiles_base(n->tilemap, c.tilemap)) {
			hash128_t r = timeline_tiles_rebase(n->tilemap, base);
			if (!hash128_is_none(r)) {
				hstore_unref(n->tilemap);
				n->tilemap = r;
			}
		}
		break;
	}
	hstore_unref(c.tilemap);
}

// Cost-based checkpoints for the positions older than the recent ones
static void timeline_policy(timeline_t *tl) {
	if (timeline_keep_recent < 0) {
		return;
	}
	i32 last = tl->cursor - timeline_keep_recent;
	f32 acc  = 0.0;
	f32 half = timeline_cp_target_ms() * 0.5;
	i32 ci   = 0;
	for (i32 pos = 1; pos <= last; ++pos) {
		acc += timeline_entry_cost(&tl->entries[pos - 1]);
		while (ci < tl->cp_count && tl->cps[ci].pos < pos) {
			ci++;
		}
		bool kept = false;
		while (ci < tl->cp_count && tl->cps[ci].pos == pos) {
			if (timeline_state_needed(tl, &tl->cps[ci]) || acc >= half) {
				kept = true;
				ci++;
			}
			else {
				timeline_checkpoint_drop(tl, ci);
			}
		}
		if (kept || !timeline_entry_replays(tl, pos - 1)) {
			acc = 0.0;
		}
	}
}

// Thinning for a history budget (history_io_thin): every other state not needed behind the cursor goes, the oldest
// first. Count dropped.
i32 timeline_thin_all(i32 round) {
	i32 dropped = 0;
	for (i32 t = 0; t < timeline_all_count; ++t) {
		timeline_t *tl  = timeline_all[t];
		bool        odd = false;
		for (i32 i = 0; i < tl->cp_count; ++i) {
			tl_checkpoint_t *c = &tl->cps[i];
			if (c->pos >= tl->cursor || timeline_state_needed(tl, c)) {
				continue;
			}
			odd = !odd;
			if (odd) {
				timeline_checkpoint_drop(tl, i--);
				dropped++;
			}
		}
	}
	return dropped;
}

// Clear history: the states only undo needed go, the timelines stay
void timeline_clear_states_all() {
	for (i32 t = 0; t < timeline_all_count; ++t) {
		timeline_t *tl = timeline_all[t];
		for (i32 i = 0; i < tl->cp_count; ++i) {
			if (!timeline_state_needed(tl, &tl->cps[i])) {
				timeline_checkpoint_drop(tl, i--);
			}
		}
	}
}

// Scripts: checkpoints of l's timeline
i32 timeline_test_checkpoints(slot_layer_t *l) {
	timeline_t *tl = timeline_of(l);
	return tl != NULL ? tl->cp_count : -1;
}

// Scripts: entries of the timeline of l, the kind of entry i, the cursor, kept states
i32 timeline_test_count(slot_layer_t *l) {
	timeline_t *tl = timeline_of(l);
	return tl != NULL ? tl->count : -1;
}

i32 timeline_test_kind(slot_layer_t *l, i32 i) {
	timeline_t *tl = timeline_of(l);
	return tl != NULL && i >= 0 && i < tl->count ? tl->entries[i].kind : 0;
}

i32 timeline_test_cursor(slot_layer_t *l) {
	timeline_t *tl = timeline_of(l);
	return tl != NULL ? tl->cursor : -1;
}

// Positions 0..count with a kept state
i32 timeline_test_states(slot_layer_t *l) {
	timeline_t *tl = timeline_of(l);
	i32         n  = 0;
	for (i32 i = 0; tl != NULL && i <= tl->count; ++i) {
		n += !hash128_is_none(timeline_state(tl, i));
	}
	return n;
}

// Writes the state at pos into the live layer, true when it could
bool timeline_test_materialize(slot_layer_t *l, i32 pos) {
	timeline_t *tl = timeline_of(l);
	if (tl == NULL) {
		return false;
	}
	timeline_finalize();
	paint_target_t t;
	timeline_target_live(l, &t);
	return timeline_materialize(tl, pos, &t);
}

// Scripts: renders at another resolution and uv rectangle, into textures of the layer's format timeline_test_out reads
static gpu_texture_t *timeline_test_tex[3];
static gpu_texture_t *timeline_test_part[3];

static bool timeline_test_target(slot_layer_t *l, gpu_texture_t **tex, i32 res_w, i32 res_h, f32 u0, f32 v0, f32 u1, f32 v1, paint_target_t *t) {
	memset(t, 0, sizeof(paint_target_t));
	t->w          = (i32)roundf((u1 - u0) * res_w);
	t->h          = (i32)roundf((v1 - v0) * res_h);
	t->uv_rect[0] = u0;
	t->uv_rect[1] = v0;
	t->uv_rect[2] = u1;
	t->uv_rect[3] = v1;
	t->res_w      = res_w;
	t->res_h      = res_h;
	t->channels   = slot_layer_is_mask(l) ? 1 : 3;
	gpu_texture_t *live[3] = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
	for (i32 k = 0; k < t->channels; ++k) {
		util_texture_ensure_target(&tex[k], t->w, t->h, live[k]->format);
		t->out[k] = tex[k];
	}
	return t->w > 0 && t->h > 0;
}

// The timeline of l at pos (-1: its cursor) at res_w x res_h for the uv rectangle
bool timeline_test_render(slot_layer_t *l, i32 pos, i32 res_w, i32 res_h, f32 u0, f32 v0, f32 u1, f32 v1) {
	timeline_t    *tl = timeline_of(l);
	paint_target_t t;
	if (tl == NULL || !timeline_test_target(l, timeline_test_tex, res_w, res_h, u0, v0, u1, v1, &t)) {
		return false;
	}
	return timeline_materialize(tl, pos < 0 ? tl->cursor : pos, &t);
}

// Entry i of l's timeline replayed over the state before it, at the working resolution: the op's replay alone. False
// without a state before it.
bool timeline_test_replay_entry(slot_layer_t *l, i32 i) {
	timeline_t    *tl = timeline_of(l);
	paint_target_t t;
	if (tl == NULL || i < 0 || i >= tl->count || !timeline_test_target(l, timeline_test_tex, l->texpaint->width, l->texpaint->height, 0, 0, 1, 1, &t)) {
		return false;
	}
	hash128_t h = timeline_state_at(tl, i, t.res_w, t.res_h);
	return !hash128_is_none(h) && timeline_tiles_restore(h, &t) && timeline_entry_replay(tl, i, &t, false);
}

// The live textures of l scaled (bilinear) to res_w x res_h, what a resolution change did before timelines
bool timeline_test_scale_live(slot_layer_t *l, i32 res_w, i32 res_h) {
	paint_target_t t;
	if (!timeline_test_target(l, timeline_test_tex, res_w, res_h, 0, 0, 1, 1, &t)) {
		return false;
	}
	paint_target_t live;
	timeline_target_live(l, &live);
	hash128_t h  = timeline_tiles_capture(&live, HASH128_NONE, NULL);
	bool      ok = timeline_state_resample(h, &t);
	hstore_unref(h);
	return ok;
}

gpu_texture_t *timeline_test_out(i32 k) {
	return k >= 0 && k < 3 ? timeline_test_tex[k] : NULL;
}

// Rendering l at its cursor in four quadrants at res x res gives the render of the whole: channel values (8 bit
// textures) more than tol apart, -1 when it can not run
i32 timeline_test_quadrants(slot_layer_t *l, i32 res, i32 tol) {
	if (!timeline_test_render(l, -1, res, res, 0, 0, 1, 1)) {
		return -1;
	}
	i32 channels = slot_layer_is_mask(l) ? 1 : 3;
	i32 bpt      = gpu_texture_format_size(timeline_test_tex[0]->format);
	if (bpt != 4 && bpt != 1) {
		return -1;
	}
	u8 *whole[3] = {NULL, NULL, NULL};
	for (i32 k = 0; k < channels; ++k) {
		draw_saved_t saved = draw_suspend();
		buffer_t    *b     = gpu_get_texture_pixels(timeline_test_tex[k]);
		draw_resume(saved);
		whole[k] = malloc(b->length);
		memcpy(whole[k], b->buffer, b->length);
		gpu_release_texture_pixels(timeline_test_tex[k]);
	}
	timeline_t *tl   = timeline_of(l);
	i32         bad  = 0;
	i32         half = res / 2;
	for (i32 q = 0; q < 4 && bad >= 0; ++q) {
		f32            u0 = (q % 2) * 0.5, v0 = (q / 2) * 0.5;
		paint_target_t t;
		if (!timeline_test_target(l, timeline_test_part, res, res, u0, v0, u0 + 0.5, v0 + 0.5, &t) || !timeline_materialize(tl, tl->cursor, &t)) {
			bad = -1;
			break;
		}
		for (i32 k = 0; k < channels; ++k) {
			draw_saved_t saved = draw_suspend();
			u8          *p     = gpu_get_texture_pixels(timeline_test_part[k])->buffer;
			draw_resume(saved);
			for (i32 y = 0; y < half; ++y) {
				u8 *a = whole[k] + ((size_t)(y + (q / 2) * half) * res + (q % 2) * half) * bpt;
				u8 *b = p + (size_t)y * half * bpt;
				for (i32 x = 0; x < half * bpt; ++x) {
					bad += abs((i32)a[x] - (i32)b[x]) > tol;
				}
			}
			gpu_release_texture_pixels(timeline_test_part[k]);
		}
	}
	for (i32 k = 0; k < channels; ++k) {
		free(whole[k]);
	}
	return bad;
}

i32 timeline_test_floor(slot_layer_t *l) {
	timeline_t *tl = timeline_of(l);
	return tl != NULL ? timeline_floor(tl, tl->cursor) : -1;
}

bool timeline_test_upscalable(slot_layer_t *l) {
	timeline_t *tl = timeline_of(l);
	return tl != NULL && timeline_upscalable(tl, tl->cursor);
}

// Scripts: the test textures kept in slot (a project can be replaced meanwhile), and read back
static gpu_texture_t *timeline_test_kept_tex[8][3];

void timeline_test_keep(i32 slot) {
	if (slot < 0 || slot >= 8) {
		return;
	}
	draw_saved_t saved = draw_suspend();
	for (i32 k = 0; k < 3; ++k) {
		gpu_texture_t *t = timeline_test_tex[k];
		if (t == NULL) {
			continue;
		}
		util_texture_ensure_target(&timeline_test_kept_tex[slot][k], t->width, t->height, t->format);
		gpu_copy_regions_begin();
		gpu_copy_region(timeline_test_kept_tex[slot][k], 0, 0, t, 0, 0, t->width, t->height);
		gpu_copy_regions_end();
	}
	draw_resume(saved);
}

gpu_texture_t *timeline_test_kept(i32 slot, i32 k) {
	return slot >= 0 && slot < 8 && k >= 0 && k < 3 ? timeline_test_kept_tex[slot][k] : NULL;
}
