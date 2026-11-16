#pragma once

// Stroke-recorded layers: the contract between the modules of the subsystem. Each paint layer and paint mask owns a
// timeline, the ordered entries (recorded strokes and re-renderable ops) that define its pixels at any resolution.
// Its textures, undo tiles, checkpoints and Material Maker buffers are caches of it.
//
//   paint_exec.c, paint_rec.c, paint_replay.c, paint_mat_snapshot.c  record strokes and replay them (track paint)
//   timeline.c, timeline_ops.c, timeline_tiles.c, history*.c         timelines, checkpoints, undo (track undo)
//   history_store.c, io/project_container.c                          content-addressed blobs, journal, v31 projects
//   io/export_tiled.c                                                export at any resolution, in tiles (track export)
//
// Changes to this header go through the coordinator. Numbering marked frozen is persisted in projects: append only.
// The rest of the subsystem's declarations live with their module (functions.h sections, as elsewhere in the app).

#include "util/util_hash.h"

// ---- Blob store (history_store.c) ----
// Content-addressed blobs, refcounted, LZ4 in RAM (T1), spilled to a journal on disk (T2), written into the project
// container on save. GPU tile pages (T0) are history_tiles.c, not this store.

typedef enum hstore_kind { // Frozen
	HSTORE_TILE         = 1, // One tile of one texture (timeline_tiles.c)
	HSTORE_TILEMAP      = 2, // The tile hashes of a layer state (timeline_tiles.c)
	HSTORE_STROKE       = 3, // Stroke record (paint_rec.c)
	HSTORE_PARAMS       = 4, // Params of a timeline op (timeline_ops.c)
	HSTORE_SNAPSHOT     = 5, // Material snapshot (paint_mat_snapshot.c)
	HSTORE_ASSET        = 6, // Image or font bytes as imported (brush tips, stencils, decals, image layers)
	HSTORE_LAYER_RECORD = 7, // A removed layer: props, timeline uid, tilemap (history*.c)
	HSTORE_CANVAS       = 8, // Node canvas (history steps)
	HSTORE_SESSION      = 10, // Session head: what recovers unsaved work after a crash (io/project_container.c)
} hstore_kind_t;

// Bit of hash128_t.hi: set on inline uniform tiles (encoded by timeline_tiles.c, never stored), always clear on stored
// content hashes (hstore_hash)
#define HASH128_INLINE_BIT (1ULL << 63)

typedef struct hstore_stats {
	i64 blobs;
	i64 ram_bytes;   // Compressed bytes held in RAM
	i64 disk_bytes;  // Bytes in the journal
	i64 dirty_bytes; // Compressed bytes only in RAM, not journaled yet
	i64 dead_bytes;  // Journal bytes of blobs no longer stored (compaction reclaims them)
	i64 ram_budget;  // Bytes, history_ram_mb (and 1/10 of the available memory on iOS)
	i64 disk_budget; // Bytes, history_disk_mb
} hstore_stats_t;

hash128_t      hstore_hash(const void *data, u32 len); // hash128 with HASH128_INLINE_BIT cleared, what hstore_put returns
hash128_t      hstore_put(hstore_kind_t kind, const void *data, u32 len); // Adds a reference (stores it the first time)
u8            *hstore_get(hash128_t h, u32 *len);                          // malloc'd copy, the caller frees; NULL if missing
bool           hstore_has(hash128_t h);
void           hstore_ref(hash128_t h);
void           hstore_unref(hash128_t h); // Unreferenced blobs go at the next trim
void           hstore_trim(bool memory_warning);
void           hstore_flush(); // Journals every blob only held in RAM
void           hstore_reset(); // New or opened project
hstore_stats_t hstore_stats();

// Blobs owning other blobs (a tilemap its tiles, a stroke its snapshot): the enumerator of a kind lists the children in
// a blob's bytes. Storing a new blob refs each child listed (put the children first), a dedup hit refs nothing. Trim
// unrefs the children of the blobs it removes, cascading. Kinds without one are leaves.
typedef void (*hstore_visit_fn)(hash128_t child, void *ud);
typedef void (*hstore_children_fn)(const u8 *data, u32 len, hstore_visit_fn visit, void *ud);
void hstore_set_children(hstore_kind_t kind, hstore_children_fn fn);
// Refs rebuilt from reachability (after a load or a recovery): each root gets a ref per listing, each blob one per
// parent listing it, the rest is garbage at the next trim. Ends a recovery (hstore_recover_end).
void hstore_rebuild_refs(const hash128_t *roots, i32 n);

// ---- Timelines (timeline.c) ----

typedef enum tl_kind { // Frozen
	TL_STROKE     = 1,  // Recorded stroke, payload HSTORE_STROKE (paint_rec.c)
	TL_CLEAR      = 2,  // Params: color, occlusion, roughness, metallic (mask: value)
	TL_IMAGE      = 3,  // Params: asset hash, placement
	TL_FILL       = 4,  // Params: material snapshot, scale, angle, uv type, uv map, decal matrix, object mask
	TL_INVERT     = 5,  // Mask invert
	TL_TRANSLATE  = 6,  // Params: dx, dy in uv (2D view move)
	TL_ID_MASK    = 7,  // Params: id map asset hash, picked colors
	TL_PATH       = 8,  // Params: path points, text, material snapshot
	TL_APPLY_MASK = 9,  // src_tl at src_pos is the mask, params: mask props and stack
	TL_MERGE      = 10, // src_tl at src_pos is the layer merged in, params: its props and masks
	TL_BAKE_OPAC  = 11, // Opacity and mask stack baked into the layer
	TL_RASTER     = 12, // Keyframe, payload HSTORE_TILEMAP recorded at res_w x res_h, not upscalable
	TL_REFINE     = 13, // Marker: a stroke sharpened as its material's buffers landed, nothing to replay
} tl_kind_t;

typedef enum tl_flag {
	TL_FLAG_LOCAL       = 1,  // Writes inside uv_bounds, reads inside uv_bounds grown by halo_uv
	TL_FLAG_NONLOCAL    = 2,  // Reads anywhere in its layer (clone, blur, smudge, move): replays on the whole layer
	TL_FLAG_UPSCALABLE  = 4,  // Replays at any resolution, otherwise resampled from res_w x res_h
	TL_FLAG_CROSS_LAYER = 8,  // Reads other layers
	TL_FLAG_INVOLUTION  = 16, // Applied twice is identity
} tl_flag_t;

typedef struct tl_entry {
	u16       kind;  // tl_kind_t
	u16       flags; // tl_flag_t bits
	u32       step_serial;  // History step that appended it
	hash128_t payload;      // HSTORE_STROKE, HSTORE_PARAMS or HSTORE_TILEMAP blob
	f32       uv_bounds[4]; // u0, v0, u1, v1 written; 0, 0, 1, 1 when unknown
	f32       halo_uv;      // Read reach outside uv_bounds
	u32       mesh_epoch;   // Mesh the entry was recorded on, older epochs do not replay
	u32       src_tl;       // TL_MERGE, TL_APPLY_MASK: source timeline uid
	i32       src_pos;      // and its state
	i32       res_w;        // Layer resolution when recorded
	i32       res_h;
	f32       cost_ms; // GPU time measured live, at res_w x res_h
	u8        mm_tier; // Lowest Material Maker buffer tier it painted with live (mm_buffers.c tiers)
} tl_entry_t;

typedef struct tl_checkpoint {
	i32       pos;     // State after entries [0, pos)
	i32       w;       // Resolution of the state
	i32       h;
	i32       format;  // gpu_texture_format_t
	hash128_t tilemap; // HSTORE_TILEMAP
	u8        exact_tier; // Lowest tier of entries [0, pos), export skips checkpoints below full
	bool      pinned;     // Base of the oldest retained step, or the floor: never thinned
} tl_checkpoint_t;

typedef struct timeline {
	u32              uid;
	u32              layer_uid;
	u8               channels;   // 3 paint layer (base, nor, pack), 1 mask
	u32              parent_tl;  // Duplicates share entries [0, parent_pos) of their parent (copy on write)
	i32              parent_pos;
	tl_entry_t      *entries;
	i32              count;
	i32              cap;
	i32              cursor; // [0, cursor) is the layer, [cursor, count) the redo branch
	tl_checkpoint_t *cps;
	i32              cp_count;
	i32              cp_cap;
	i32              floor; // Last TL_RASTER: replay at another resolution starts here
	u32              refs;  // Owner layer, composite entries, layer records
	f32              cost_since_cp;
} timeline_t;

// A part of a layer's uv space at some resolution: the whole live layer, a tile of an export, a scratch layer.
// out[] holds the state before an entry is applied and receives the state after it.
typedef struct paint_target {
	i32                 w;           // Texels of out
	i32                 h;
	f32                 uv_rect[4];  // u0, v0, u1, v1 of the layer out covers; 0, 0, 1, 1 is the whole layer
	i32                 res_w;       // Resolution of the whole layer (w / (u1 - u0))
	i32                 res_h;
	i32                 channels;    // 3 or 1, as the timeline
	struct gpu_texture *out[3];      // base, nor, pack (mask: out[0])
} paint_target_t;

timeline_t *timeline_create(u32 layer_uid, i32 channels);
timeline_t *timeline_find(u32 uid);
timeline_t *timeline_of(slot_layer_t *l); // NULL for groups and procedural layers (fill layers and masks)
void        timeline_append(timeline_t *tl, tl_entry_t *e); // Drops the redo branch, cursor = count
void        timeline_ref(timeline_t *tl);
void        timeline_unref(timeline_t *tl);
bool        timeline_materialize(timeline_t *tl, i32 pos, paint_target_t *t); // Best checkpoint <= pos, replay the rest
void        timeline_reset_all();

// Ops other than strokes (timeline_ops.c): recorded where the history step is pushed, replayed by paint_replay_entry
void timeline_record_op(slot_layer_t *l, tl_kind_t kind, const void *params, u32 len, const f32 *uv_bounds, u16 flags);
bool timeline_ops_replay(timeline_t *tl, i32 index, paint_target_t *t);

// Layer states as tile maps (timeline_tiles.c): HISTORY_TILES_GRID^2 tiles per channel texture, uniform tiles inline
hash128_t timeline_tiles_capture(paint_target_t *src, hash128_t prev, const u64 *dirty); // prev and dirty may be NONE/NULL
bool      timeline_tiles_restore(hash128_t tilemap, paint_target_t *dst);

// ---- Recording and replay (paint_rec.c, paint_replay.c, paint_mat_snapshot.c) ----

typedef struct paint_replay_opts {
	bool exact;          // Same resolution and device as recorded: byte-equal to live is expected
	bool composite_fast; // Coverage-only replay, the material evaluated once (stroke refine, export)
	i32  mm_cap;         // Material Maker full tier size cap, 0 the default
	f32  aux_scale;      // Resolution of replay depth and uv maps relative to the recorded viewport
} paint_replay_opts_t;

void             paint_rec_stroke_begin(history_step_t *step, slot_layer_t *l); // history_paint()
void             paint_rec_flush(); // Waits for strokes whose dab readback has not landed (before undo, save, export)
bool             paint_replay_entry(timeline_t *tl, i32 index, paint_target_t *t, const paint_replay_opts_t *o);
i32              paint_replay_halo_px(const tl_entry_t *e, i32 res_w);
hash128_t        paint_mat_snapshot_intern(slot_material_t *m);
slot_material_t *paint_mat_snapshot_instantiate(hash128_t h); // Hidden material, not in the project's list
