#include "global.h"

// Strokes painted before their material's Material Maker buffers were full sharpen by replay (stroke_rec.h). A stored
// stroke that sampled buffers below the full tier (tl_entry_t.mm_tier) is noted with the buffers it sampled. Once they
// are all full, its timeline is replayed from the state kept before it: every entry from there to the end, strokes of
// other materials on top keeping their place, each stroke with its material at the full tier. The states kept after it
// (checkpoints, what undo restores) are captured again as the replay passes them, the live layer becomes the state at
// the cursor. A job across frames, a frame's share bounded by the entries' measured cost: the state it starts from
// restored a part a frame, the captures again in frames of their own (each a readback). Strokes replay composite-fast:
// their material rendered once into a replay cache (util_material_cache.c), a band of it a frame, the strokes read it.
// Not an undo step: the history does not change, its states are sharper. Intermediate tiers do not sharpen, a replay
// needs full buffers.
//
// A stroke on the layer, an undo or redo moving its cursor, an entry added or dropped stops the job: it starts again from
// the same stroke later. A replay that fails ends the note (the stroke keeps the tier it painted with).

#define PAINT_REFINE_MAX       16
#define PAINT_REFINE_KEYS      32
#define PAINT_REFINE_BUDGET_MS 12.0 // Measured GPU cost of the entries replayed a frame, at least one
#define PAINT_REFINE_RESTORE   8    // Batches of tiles (timeline_tiles_restore_step) the start state restores a frame

typedef struct {
	u32 tl;
	i32 index; // First stroke below full
	u32 serial;
	u64 keys[PAINT_REFINE_KEYS]; // Buffers of the strokes noted, from index on
	i32 nkeys;
} paint_refine_t;

static paint_refine_t paint_refine_notes[PAINT_REFINE_MAX];
static i32            paint_refine_count      = 0;
static i32            paint_refine_stats[4]; // Jobs done, jobs stopped, notes failed, entries replayed

typedef struct {
	bool           active;
	u32            tl;
	i32            i0;
	u32            serial;
	i32            pos;    // Next entry to replay
	i32            count;  // The timeline's entries when started
	i32            cursor; // and its cursor
	paint_target_t t;
	gpu_texture_t *tex[3];
	hash128_t      prev; // State of t kept last (restored or captured again)
	u64            dirty[HISTORY_TILES_GRID * HISTORY_TILES_GRID / 64]; // Tiles written since
	i32            restoring; // prev is being restored into t from this tile on (timeline_tiles_restore_step), -1 done
	bool           capture;   // The checkpoint at pos is captured again next frame
} paint_refine_job_t;

static paint_refine_job_t paint_refine_job = {0};

static void paint_refine_update(void *_);

static i32 paint_refine_find(u32 tl) {
	for (i32 i = 0; i < paint_refine_count; ++i) {
		if (paint_refine_notes[i].tl == tl) {
			return i;
		}
	}
	return -1;
}

static void paint_refine_remove(i32 i) {
	paint_refine_notes[i] = paint_refine_notes[--paint_refine_count];
}

// A stored stroke (paint_rec_finish) sampled buffers below full: keys, n of them
void paint_refine_note(timeline_t *tl, i32 index, const u64 *keys, i32 n) {
	if (tl == NULL || index < 0 || index >= tl->count || n <= 0) {
		return;
	}
	i32 i = paint_refine_find(tl->uid);
	if (i < 0) {
		if (paint_refine_count == PAINT_REFINE_MAX) {
			paint_refine_remove(0); // The oldest keeps its tier
			paint_refine_stats[2]++;
		}
		i                        = paint_refine_count++;
		paint_refine_notes[i]    = (paint_refine_t){.tl = tl->uid, .index = index, .serial = tl->entries[index].step_serial};
	}
	paint_refine_t *r = &paint_refine_notes[i];
	if (index < r->index) {
		r->index  = index;
		r->serial = tl->entries[index].step_serial;
	}
	for (i32 k = 0; k < n; ++k) {
		bool seen = false;
		for (i32 j = 0; j < r->nkeys && !seen; ++j) {
			seen = r->keys[j] == keys[k];
		}
		if (!seen && r->nkeys < PAINT_REFINE_KEYS) {
			r->keys[r->nkeys++] = keys[k];
		}
	}
}

static bool paint_refine_valid(paint_refine_t *r, timeline_t *tl) {
	return tl != NULL && r->index < tl->count && tl->entries[r->index].step_serial == r->serial && tl->entries[r->index].kind == TL_STROKE &&
	       slot_layer_find_uid(tl->layer_uid) != NULL;
}

// The tiles entry e writes and reads around it
static void paint_refine_dirty_add(u64 *dirty, tl_entry_t *e) {
	i32 g = HISTORY_TILES_GRID;
	f32 h = e->halo_uv;
	i32 x0 = (i32)floorf((e->uv_bounds[0] - h) * g) - 1, y0 = (i32)floorf((e->uv_bounds[1] - h) * g) - 1;
	i32 x1 = (i32)ceilf((e->uv_bounds[2] + h) * g) + 1, y1 = (i32)ceilf((e->uv_bounds[3] + h) * g) + 1;
	x0 = x0 < 0 ? 0 : x0;
	y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > g ? g : x1;
	y1 = y1 > g ? g : y1;
	for (i32 y = y0; y < y1; ++y) {
		for (i32 x = x0; x < x1; ++x) {
			i32 t = y * g + x;
			dirty[t >> 6] |= 1ULL << (t & 63);
		}
	}
}

static bool paint_refine_has_checkpoint(timeline_t *tl, i32 pos) {
	for (i32 i = 0; i < tl->cp_count; ++i) {
		if (tl->cps[i].pos == pos) {
			return true;
		}
	}
	return false;
}

// The job of note r: t gets the state kept at or before its stroke over the next frames (every state was captured)
static bool paint_refine_start(paint_refine_t *r) {
	timeline_t   *tl = timeline_find(r->tl);
	slot_layer_t *l  = tl != NULL ? slot_layer_find_uid(tl->layer_uid) : NULL;
	if (l == NULL || !paint_refine_valid(r, tl)) {
		return false;
	}
	i32 from = r->index;
	while (from >= 0 && hash128_is_none(timeline_state(tl, from))) {
		from--;
	}
	if (from < 0) {
		return false;
	}
	paint_refine_job_t *j = &paint_refine_job;
	paint_target_t      live;
	timeline_target_live(l, &live);
	for (i32 k = 0; k < live.channels; ++k) {
		util_texture_ensure_target(&j->tex[k], live.w, live.h, live.out[k]->format);
	}
	j->t = live;
	for (i32 k = 0; k < 3; ++k) {
		j->t.out[k] = k < live.channels ? j->tex[k] : NULL;
	}
	hash128_t s = timeline_state(tl, from);
	j->active    = true;
	j->restoring = 0;
	j->capture   = false;
	j->tl     = r->tl;
	j->i0     = r->index;
	j->serial = r->serial;
	j->pos    = from;
	j->count  = tl->count;
	j->cursor = tl->cursor;
	j->prev   = s;
	memset(j->dirty, 0, sizeof(j->dirty));
	return true;
}

static void paint_refine_stop(bool done) {
	paint_refine_job.active = false;
	material_cache_replay_release(); // Its replay caches
	paint_refine_stats[done ? 0 : 1]++;
}

// The live layer of the job's timeline as t
static void paint_refine_to_live(slot_layer_t *l) {
	paint_target_t live;
	timeline_target_live(l, &live);
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	gpu_copy_regions_begin();
	for (i32 k = 0; k < live.channels; ++k) {
		gpu_copy_region(live.out[k], 0, 0, paint_refine_job.t.out[k], 0, 0, live.w, live.h);
	}
	gpu_copy_regions_end();
	l->mask_stack_dirty            = true;
	g_context->layer_preview_dirty = true;
	g_context->ddirty              = 2;
	mask_stack_layer_changed(l);
}

// The job reached pos: the live layer when it is the cursor, done at the end
static void paint_refine_reached(paint_refine_job_t *j, slot_layer_t *l) {
	if (j->pos == j->cursor) {
		paint_refine_to_live(l);
	}
	if (j->pos == j->count) {
		paint_refine_stop(true);
		i32 i = paint_refine_find(j->tl);
		if (i >= 0) {
			paint_refine_remove(i);
		}
	}
}

static void paint_refine_fail() {
	paint_refine_stop(false);
	i32 i = paint_refine_find(paint_refine_job.tl);
	if (i >= 0) {
		paint_refine_remove(i); // Keeps the tier it reached
		paint_refine_stats[2]++;
	}
}

// A frame of the job: a part of the start state restored, or a checkpoint captured again, or entries replayed up to the
// budget
static void paint_refine_step() {
	paint_refine_job_t *j  = &paint_refine_job;
	timeline_t         *tl = timeline_find(j->tl);
	slot_layer_t       *l  = tl != NULL ? slot_layer_find_uid(tl->layer_uid) : NULL;
	if (l == NULL || tl->count != j->count || tl->cursor != j->cursor || j->i0 >= tl->count || tl->entries[j->i0].step_serial != j->serial ||
	    l->texpaint->width != j->t.w || l->texpaint->height != j->t.h) {
		paint_refine_stop(false); // Changed meanwhile: again from the start
		return;
	}
	if (j->restoring >= 0) {
		j->restoring = timeline_tiles_restore_step(j->prev, &j->t, j->restoring, PAINT_REFINE_RESTORE);
		if (j->restoring < 0) {
			paint_refine_stop(false);
			return;
		}
		j->restoring = j->restoring == 0 ? -1 : j->restoring;
		return;
	}
	if (j->capture) { // Undo's state there, sharper
		j->capture  = false;
		hash128_t h = timeline_tiles_capture(&j->t, j->prev, j->dirty);
		if (!hash128_is_none(h)) {
			timeline_checkpoint_set(tl, j->pos, h, false);
			j->prev = h;
			memset(j->dirty, 0, sizeof(j->dirty));
		}
		paint_refine_reached(j, l);
		return;
	}
	f64 cost = 0.0;
	while (j->pos < j->count && (cost == 0.0 || cost < PAINT_REFINE_BUDGET_MS)) {
		tl_entry_t *e = &tl->entries[j->pos];
		if (e->kind == TL_STROKE && paint_replay_prepare(tl, j->pos, &j->t) == 0) { // Its material's replay cache a step on a frame
			return;
		}
		bool ok = e->kind == TL_RASTER ? timeline_tiles_restore(e->payload, &j->t) : timeline_entry_replay(tl, j->pos, &j->t, true); // A keyframe: its state
		if (!ok) {
			paint_refine_fail();
			return;
		}
		paint_refine_stats[3]++;
		cost += e->cost_ms > 0.0 ? e->cost_ms : 1.0;
		if (e->kind == TL_RASTER) {
			j->prev = e->payload;
			memset(j->dirty, 0, sizeof(j->dirty));
		}
		else {
			paint_refine_dirty_add(j->dirty, e);
		}
		j->pos++;
		if (j->pos > j->i0 && paint_refine_has_checkpoint(tl, j->pos)) {
			j->capture = true; // Next frame: a capture reads the layer back
			return;
		}
		paint_refine_reached(j, l);
	}
}

static void paint_refine_update(void *_) {
	if (g_context->brush_time > 0 || util_particle_alive()) { // A stroke in progress (particles still painting): after it
		return;
	}
	if (paint_rec_active()) {
		paint_rec_flush(); // A stroke that ended is stored, noted when it sampled buffers below full
		if (paint_rec_active()) {
			return;
		}
	}
	if (paint_refine_count == 0 && !paint_refine_job.active) {
		return;
	}
	if (paint_refine_job.active) {
		paint_refine_step();
		return;
	}
	bool ready = false; // A note whose buffers are full: the states it starts from captured first, in a frame of their own
	for (i32 i = 0; i < paint_refine_count && !ready; ++i) {
		timeline_t *tl = timeline_find(paint_refine_notes[i].tl);
		ready          = paint_refine_valid(&paint_refine_notes[i], tl) && mm_buffers_links_tier(paint_refine_notes[i].keys, paint_refine_notes[i].nkeys) >= MM_BUFFERS_FULL;
	}
	if (ready && timeline_captures_pending()) {
		timeline_finalize();
		return;
	}
	for (i32 i = 0; i < paint_refine_count; ++i) {
		paint_refine_t *r  = &paint_refine_notes[i];
		timeline_t     *tl = timeline_find(r->tl);
		if (!paint_refine_valid(r, tl)) {
			paint_refine_remove(i--); // Undone and dropped, or its layer gone
			continue;
		}
		i32 tier = mm_buffers_links_tier(r->keys, r->nkeys);
		if (tier < MM_BUFFERS_FULL) { // Not yet, or its buffers gone (-1): when its material compiles again
			continue;
		}
		if (paint_refine_start(r)) {
			paint_refine_step(); // The first part of its start state
		}
		else {
			paint_refine_remove(i);
			paint_refine_stats[2]++;
		}
		return; // One job at a time
	}
}

// At startup: strokes that ended are stored and their timelines sharpened, a frame at a time
void paint_refine_init() {
	sys_notify_on_update(paint_refine_update, NULL);
}

// New project: notes and the job go
void paint_refine_reset() {
	paint_refine_count      = 0;
	paint_refine_job.active = false;
}

// Tests: 0 notes waiting, 1 a job running, 2 jobs done, 3 jobs stopped, 4 notes failed, 5 entries replayed
i32 paint_refine_stat(i32 what) {
	if (what == 0) {
		return paint_refine_count;
	}
	if (what == 1) {
		return paint_refine_job.active ? 1 : 0;
	}
	return what >= 2 && what < 6 ? paint_refine_stats[what - 2] : 0;
}
