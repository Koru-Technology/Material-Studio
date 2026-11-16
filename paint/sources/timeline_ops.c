#include "global.h"

// Timeline entries of ops other than strokes (stroke_rec.h): recorded where their history step is pushed. Until replay
// exists their state after is a checkpoint (timeline_finalize), nothing replays them.

void timeline_record_op(slot_layer_t *l, tl_kind_t kind, const void *params, u32 len, const f32 *uv_bounds, u16 flags) {
	if (!timeline_active() || !timeline_layer_paints(l)) {
		return;
	}
	hash128_t payload = params != NULL && len > 0 ? hstore_put(HSTORE_PARAMS, params, len) : HASH128_NONE;
	timeline_record(l, kind, payload, uv_bounds, flags);
}

// The entry history_paint() appended for l is an op instead (ID mask, 2D move): its state after becomes a checkpoint
void timeline_retag_last(slot_layer_t *l, i32 kind, const void *params, u32 len, u16 flags) {
	timeline_t *tl = timeline_of(l);
	if (tl == NULL || tl->count == 0 || tl->cursor != tl->count) {
		return;
	}
	tl_entry_t *e = &tl->entries[tl->count - 1];
	if (e->kind != TL_RASTER || !hash128_is_none(e->payload)) {
		return;
	}
	e->kind    = kind;
	e->flags   = flags;
	e->payload = params != NULL && len > 0 ? hstore_put(HSTORE_PARAMS, params, len) : HASH128_NONE;
	tl->floor  = 0;
	for (i32 i = tl->count - 1; i >= 0; --i) {
		if (tl->entries[i].kind == TL_RASTER) {
			tl->floor = i + 1;
			break;
		}
	}
}

bool timeline_ops_replay(timeline_t *tl, i32 index, paint_target_t *t) {
	return false;
}
