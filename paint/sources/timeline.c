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

static timeline_t  **timeline_all       = NULL;
static i32           timeline_all_count = 0;
static i32           timeline_all_cap   = 0;
static tl_pending_t *timeline_pending   = NULL;
static i32           timeline_pending_count = 0;
static i32           timeline_pending_cap   = 0;

bool timeline_active() {
	return g_config->timeline_shadow || history_v2_active();
}

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

// The checkpoint at pos is tilemap (the reference handed over), one there already is replaced
void timeline_checkpoint_set(timeline_t *tl, i32 pos, hash128_t tilemap, bool pinned) {
	i32 w = 0, h = 0, format = 0, channels = 0;
	timeline_tiles_info(tilemap, &w, &h, &format, &channels);
	tl_checkpoint_t cp = {.pos = pos, .w = w, .h = h, .format = format, .tilemap = tilemap, .exact_tier = 0, .pinned = pinned};
	tl->cost_since_cp  = 0.0;
	for (i32 i = 0; i < tl->cp_count; ++i) {
		if (tl->cps[i].pos == pos) {
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

// Tile map of the state at pos when one is kept (a TL_RASTER payload or a checkpoint), NONE otherwise
hash128_t timeline_state(timeline_t *tl, i32 pos) {
	if (tl == NULL || pos < 0 || pos > tl->count) {
		return HASH128_NONE;
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

// The timeline of l, made at its first change: the pinned checkpoint at 0 is l as it is now
timeline_t *timeline_ensure(slot_layer_t *l) {
	if (!timeline_active() || !timeline_layer_paints(l)) {
		return NULL;
	}
	timeline_t *tl = timeline_find(l->timeline);
	if (tl != NULL) {
		return tl;
	}
	tl          = timeline_create(l->uid, slot_layer_is_mask(l) ? 1 : 3);
	l->timeline = tl->uid;
	paint_target_t t;
	timeline_target_live(l, &t);
	timeline_checkpoint_set(tl, 0, timeline_tiles_capture(&t, HASH128_NONE, NULL), true);
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
void timeline_finalize() {
	paint_rec_flush(); // A stroke whose dabs are still read back becomes its entry first
	for (i32 i = 0; i < timeline_pending_count; ++i) { // In order: a capture reads the state the one before kept
		tl_pending_t p = timeline_pending[i];
		timeline_t  *tl = timeline_find(p.tl);
		slot_layer_t *l = slot_layer_find_uid(p.layer);
		if (tl == NULL || l == NULL || l->timeline != tl->uid || p.index >= tl->count || !timeline_layer_paints(l)) {
			continue; // Gone or turned procedural meanwhile, the entry stays without a state
		}
		paint_target_t t;
		timeline_target_live(l, &t);
		hash128_t prev = timeline_state(tl, p.index);
		hash128_t h    = timeline_tiles_capture(&t, prev, p.has_dirty ? p.dirty : NULL);
		tl_entry_t *e  = &tl->entries[p.index];
		if (e->kind == TL_RASTER && hash128_is_none(e->payload)) {
			e->payload = h;
		}
		else {
			timeline_checkpoint_set(tl, p.index + 1, h, false);
		}
	}
	timeline_pending_count = 0;
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

// Entry i applied to t: a stroke replayed (paint_replay.c), an op re-rendered (timeline_ops.c). False when it can not be.
bool timeline_entry_replay(timeline_t *tl, i32 i, paint_target_t *t) {
	if (tl->entries[i].kind == TL_STROKE) {
		paint_replay_opts_t o = {.exact = t->res_w == tl->entries[i].res_w && t->res_h == tl->entries[i].res_h};
		return paint_replay_entry(tl, i, t, &o);
	}
	return timeline_ops_replay(tl, i, t);
}

// Writes the state at pos into t: the best kept state at pos or before, the entries after it replayed
bool timeline_materialize(timeline_t *tl, i32 pos, paint_target_t *t) {
	if (tl == NULL || pos < 0 || pos > tl->count) {
		return false;
	}
	timeline_finalize();
	i32 from = pos;
	while (from >= 0 && hash128_is_none(timeline_state(tl, from))) {
		from--;
	}
	if (from < 0 || !timeline_tiles_restore(timeline_state(tl, from), t)) {
		return false;
	}
	for (i32 i = from; i < pos; ++i) {
		if (!timeline_entry_replay(tl, i, t)) {
			return false;
		}
	}
	return true;
}

// The live layer of tl goes from the state at its cursor to the one at pos, only the tiles that differ are written
bool timeline_materialize_live(timeline_t *tl, slot_layer_t *l, i32 pos) {
	if (tl == NULL || l == NULL || !timeline_layer_paints(l)) {
		return false;
	}
	timeline_finalize();
	paint_target_t t;
	timeline_target_live(l, &t);
	hash128_t to   = timeline_state(tl, pos);
	hash128_t from = timeline_state(tl, tl->cursor);
	bool      ok   = !hash128_is_none(to) ? timeline_tiles_restore_from(to, from, &t) : timeline_materialize(tl, pos, &t);
	tl->cursor     = pos;
	return ok;
}

void timeline_reset_all() {
	timeline_pending_count = 0;
	while (timeline_all_count > 0) {
		timeline_t *tl = timeline_all[--timeline_all_count];
		timeline_free(tl);
	}
	for (i32 i = 0; g_project != NULL && g_project->_ != NULL && g_project->_->layers != NULL && i < g_project->_->layers->length; ++i) {
		g_project->_->layers->buffer[i]->timeline = 0;
	}
	timeline_tiles_cache_reset();
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
