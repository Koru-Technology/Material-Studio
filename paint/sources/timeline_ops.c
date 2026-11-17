#include "global.h"

// Timeline entries of ops other than strokes (stroke_rec.h): recorded where their history step is pushed, re-rendered at
// any resolution and uv rectangle by timeline_ops_replay. At the working resolution their state after is a checkpoint
// too (timeline_finalize), replay is what export and resolution changes use.
//
// HSTORE_PARAMS blobs, little endian: u32 magic, u32 n; hash128 children[n] (blobs the op reads, held by the params:
// assets, material snapshots), then the op's bytes:
//   TL_CLEAR      tl_clear_params_t
//   TL_TRANSLATE  f32 du, dv (the contents move by it, what leaves the layer is gone)
//   TL_IMAGE      children[0] the image file's bytes (HSTORE_ASSET), then its file extension (decoder), 0 ended
//   TL_FILL       children: snapshots of the materials it reads, then tl_fill_params_t: the fill (fill_encode by id), the
//                 layer props it renders with
//   TL_ID_MASK    picked colors, not replayed yet
//   TL_APPLY_MASK f32 the mask's opacity; the mask is the entry's src timeline at src_pos (no params: a mask with effects
//                 or height blend, a floor)
//   TL_MERGE      tl_merge_params_t; the layer merged in is the src timeline at src_pos (no params: masks, groups, a
//                 floor)
// TL_INVERT replays without params. TL_ID_MASK, TL_PATH, TL_BAKE_OPAC: floors.

#define TL_PARAMS_MAGIC 0x31504c54 // "TLP1"
#define TL_PARAMS_HEAD  8
#define TL_PARAMS_MAX   4          // Children

typedef struct tl_clear_params {
	u32 color; // Base (mask: value) as slot_layer_clear takes it
	f32 occlusion;
	f32 roughness;
	f32 metallic;
} tl_clear_params_t;

typedef struct tl_fill_params {
	i32 uv_map;
	i32 object_mask;
	f32 decal_mat[16];
	u8  paint[11]; // base opac occ rough met nor nor_blend height height_blend emis subs
	u8  pad;
	u64 material_sigs[FILL_CHANNEL_COUNT]; // Graph signature of each channel's material then, 0 for none: without a snapshot it
	                                       // replays while it is the same
	u32 snapshot_uids[TL_PARAMS_MAX];      // The material of each child snapshot
	f32 fill[FILL_ENCODE_LENGTH];
} tl_fill_params_t;

typedef struct tl_merge_params {
	f32 opacity_below; // The layer merged into: its opacity is baked first
	f32 opacity;       // The layer merged in: its props as layers_merge_layer reads them
	i32 blending;
	u8  visible;
	u8  paint[8]; // base nor nor_blend occ rough met height height_blend
	u8  linear;   // The project's linear blending
	u8  pad[2];
} tl_merge_params_t;

typedef struct tl_params {
	u8       *blob; // malloc'd
	hash128_t children[TL_PARAMS_MAX];
	u32       n;
	u8       *body;
	u32       len;
} tl_params_t;

static hash128_t tl_params_put(const hash128_t *children, u32 n, const void *body, u32 len) {
	u32 size = TL_PARAMS_HEAD + n * 16 + len;
	u8 *b    = malloc(size);
	u32 m    = TL_PARAMS_MAGIC;
	memcpy(b, &m, 4);
	memcpy(b + 4, &n, 4);
	for (u32 i = 0; i < n; ++i) {
		memcpy(b + TL_PARAMS_HEAD + i * 16, &children[i].lo, 8);
		memcpy(b + TL_PARAMS_HEAD + i * 16 + 8, &children[i].hi, 8);
	}
	if (len > 0) {
		memcpy(b + TL_PARAMS_HEAD + n * 16, body, len);
	}
	hash128_t h = hstore_put(HSTORE_PARAMS, b, size); // A new blob refs its children
	free(b);
	return h;
}

static bool tl_params_parse(const u8 *b, u32 size, hash128_t *children, u32 *n, u32 *body_at) {
	u32 m = 0;
	if (b == NULL || size < TL_PARAMS_HEAD) {
		return false;
	}
	memcpy(&m, b, 4);
	memcpy(n, b + 4, 4);
	if (m != TL_PARAMS_MAGIC || *n > TL_PARAMS_MAX || TL_PARAMS_HEAD + *n * 16 > size) {
		return false;
	}
	for (u32 i = 0; children != NULL && i < *n; ++i) {
		memcpy(&children[i].lo, b + TL_PARAMS_HEAD + i * 16, 8);
		memcpy(&children[i].hi, b + TL_PARAMS_HEAD + i * 16 + 8, 8);
	}
	*body_at = TL_PARAMS_HEAD + *n * 16;
	return true;
}

// The params of entry e, false when it has none this build reads; free p->blob after
static bool tl_params_get(tl_entry_t *e, tl_params_t *p) {
	memset(p, 0, sizeof(tl_params_t));
	if (hash128_is_none(e->payload)) {
		return false;
	}
	u32 size = 0;
	p->blob  = hstore_get(e->payload, &size);
	u32 at   = 0;
	if (!tl_params_parse(p->blob, size, p->children, &p->n, &at)) {
		free(p->blob);
		p->blob = NULL;
		return false;
	}
	p->body = p->blob + at;
	p->len  = size - at;
	return true;
}

// Children of a params blob (hstore_set_children)
void timeline_ops_children(const u8 *data, u32 len, hstore_visit_fn visit, void *ud) {
	hash128_t c[TL_PARAMS_MAX];
	u32       n  = 0;
	u32       at = 0;
	if (tl_params_parse(data, len, c, &n, &at)) {
		for (u32 i = 0; i < n; ++i) {
			visit(c[i], ud);
		}
	}
}

static void tl_ops_record(slot_layer_t *l, tl_kind_t kind, const hash128_t *children, u32 n, const void *body, u32 len, const f32 *uv_bounds,
                          u16 flags) {
	hash128_t payload = n > 0 || len > 0 ? tl_params_put(children, n, body, len) : HASH128_NONE;
	timeline_record(l, kind, payload, uv_bounds, flags);
}

// The fill of l as it is now with the layer props it renders with
static void tl_ops_record_fill(slot_layer_t *l) {
	if (l->fill == NULL) {
		timeline_record(l, TL_FILL, HASH128_NONE, NULL, 0);
		return;
	}
	tl_fill_params_t p = {.uv_map = l->uv_map, .object_mask = l->object_mask};
	memcpy(p.decal_mat, &l->decal_mat, sizeof(p.decal_mat));
	bool flags[11] = {l->paint_base, l->paint_opac,   l->paint_occ,          l->paint_rough, l->paint_met, l->paint_nor,
	                  l->paint_nor_blend, l->paint_height, l->paint_height_blend, l->paint_emis,  l->paint_subs};
	for (i32 i = 0; i < 11; ++i) {
		p.paint[i] = flags[i];
	}
	for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
		slot_material_t *m = fill_source_material(&l->fill->ch[c]);
		p.material_sigs[c] = m != NULL ? material_cache_graph_signature(m) : 0;
	}
	f32_array_t *a = fill_encode(l->fill, false);
	memcpy(p.fill, a->buffer, sizeof(p.fill));
	free(a->buffer);
	free(a);
	// Its materials as they are now, replays render them so whatever they become
	hash128_t snaps[TL_PARAMS_MAX];
	u32       n = 0;
	for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
		slot_material_t *m = fill_source_material(&l->fill->ch[c]);
		bool             seen = m == NULL;
		for (u32 k = 0; !seen && k < n; ++k) {
			seen = p.snapshot_uids[k] == m->uid;
		}
		if (seen || n == TL_PARAMS_MAX) {
			continue;
		}
		hash128_t h = paint_mat_snapshot_intern(m);
		if (!hash128_is_none(h)) {
			p.snapshot_uids[n] = m->uid;
			snaps[n++]         = h;
		}
	}
	tl_ops_record(l, TL_FILL, snaps, n, &p, sizeof(p), NULL, TL_FLAG_UPSCALABLE);
	for (u32 k = 0; k < n; ++k) {
		hstore_unref(snaps[k]); // The params hold them
	}
}

// An entry reading timeline src at its cursor (apply mask, merge): the entry keeps src
static void tl_ops_record_src(slot_layer_t *l, tl_kind_t kind, timeline_t *src, const void *body, u32 len) {
	hash128_t   payload = body != NULL && src != NULL ? tl_params_put(NULL, 0, body, len) : HASH128_NONE;
	tl_entry_t *e       = timeline_record(l, kind, payload, NULL, !hash128_is_none(payload) ? TL_FLAG_UPSCALABLE : 0);
	if (e != NULL && src != NULL) {
		e->src_tl  = src->uid;
		e->src_pos = src->cursor;
		timeline_ref(src);
	}
}

// Mask m (its timeline src) applied to l. It replays when the mask texture is its pixels: no effects, no height blend.
void timeline_record_apply_mask(slot_layer_t *l, slot_layer_t *m, timeline_t *src) {
	f32 opacity = m->mask_opacity;
	tl_ops_record_src(l, TL_APPLY_MASK, src, mask_stack_is_trivial(m) && !m->height_blend ? &opacity : NULL, sizeof(opacity));
}

// l1 merged down into l0 (layers_merge_down). It replays when both are layers without masks: the merge is then l0's
// opacity baked and layers_merge_layer.
void timeline_record_merge(slot_layer_t *l0, slot_layer_t *l1) {
	bool              plain = slot_layer_is_layer(l0) && slot_layer_is_layer(l1) && !slot_layer_is_filter(l1) && !slot_layer_has_masks(l0, false) &&
	                          !slot_layer_has_masks(l1, false);
	tl_merge_params_t p     = {.opacity_below = l0->mask_opacity, .opacity = l1->mask_opacity, .blending = l1->blending, .visible = l1->visible,
	                           .linear = g_project->linear_blending};
	bool              f[8]  = {l1->paint_base, l1->paint_nor, l1->paint_nor_blend, l1->paint_occ, l1->paint_rough, l1->paint_met, l1->paint_height, l1->paint_height_blend};
	for (i32 i = 0; i < 8; ++i) {
		p.paint[i] = f[i];
	}
	tl_ops_record_src(l0, TL_MERGE, timeline_of(l1), plain ? &p : NULL, sizeof(p));
}

void timeline_record_op(slot_layer_t *l, tl_kind_t kind, const void *params, u32 len, const f32 *uv_bounds, u16 flags) {
	if (!timeline_layer_paints(l)) {
		return;
	}
	if (kind == TL_CLEAR && params == NULL) { // As the clear ops do
		tl_clear_params_t p = {.color = slot_layer_is_mask(l) ? 0xffffffff : 0x00000000, .occlusion = 1.0, .roughness = layers_default_rough, .metallic = 0.0};
		tl_ops_record(l, kind, NULL, 0, &p, sizeof(p), uv_bounds, flags | TL_FLAG_UPSCALABLE);
		return;
	}
	tl_ops_record(l, kind, NULL, 0, params, params != NULL ? len : 0, uv_bounds, flags);
}

// The fill of fill layer or fill mask l in its timeline: converted to fill (the timeline ends with it and waits for a
// conversion back), or back to paint (its start)
void timeline_record_fill(slot_layer_t *l) {
	timeline_allow_fill(true);
	if (timeline_layer_paints(l)) {
		tl_ops_record_fill(l);
	}
	timeline_allow_fill(false);
}

// The bytes of an asset's file: packed in the project, or on disk. NULL when neither has them.
static buffer_t *tl_ops_asset_bytes(asset_t *asset, bool *owned) {
	*owned = false;
	for (i32 i = 0; g_project->packed_assets != NULL && i < g_project->packed_assets->length; ++i) {
		packed_asset_t *pa = g_project->packed_assets->buffer[i];
		if (string_equals(pa->name, asset->file)) {
			return (buffer_t *)pa->bytes;
		}
	}
	*owned = true;
	return iron_load_blob(asset->file);
}

// A layer or mask drawn from an image asset: its bytes by content hash, it replays when the file is gone
void timeline_record_image(slot_layer_t *l, asset_t *asset) {
	if (!timeline_layer_paints(l) || asset == NULL) {
		return;
	}
	bool      owned = false;
	buffer_t *b     = tl_ops_asset_bytes(asset, &owned);
	if (b == NULL || b->length == 0) {
		timeline_record(l, TL_IMAGE, HASH128_NONE, NULL, 0);
		return;
	}
	hash128_t a   = hstore_put(HSTORE_ASSET, b->buffer, b->length);
	char     *ext = string_last_index_of(asset->file, ".") >= 0 ? substring(asset->file, string_last_index_of(asset->file, "."), string_length(asset->file)) : ".png";
	tl_ops_record(l, TL_IMAGE, &a, 1, ext, string_length(ext) + 1, NULL, TL_FLAG_UPSCALABLE);
	hstore_unref(a);
	if (owned) {
		iron_delete_blob(b);
	}
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
	e->payload = params != NULL && len > 0 ? tl_params_put(NULL, 0, params, len) : HASH128_NONE;
	tl->floor  = 0;
	for (i32 i = tl->count - 1; i >= 0; --i) {
		if (tl->entries[i].kind == TL_RASTER) {
			tl->floor = i + 1;
			break;
		}
	}
}

// Entry index of tl becomes kind with payload (the reference handed over): a stroke recorded as TL_RASTER proven
// replayable (paint_rec.c). A tile state the entry held already moves to a checkpoint after it, nothing is lost.
void timeline_entry_upgrade(timeline_t *tl, i32 index, i32 kind, hash128_t payload) {
	if (tl == NULL || index < 0 || index >= tl->count) {
		hstore_unref(payload);
		return;
	}
	tl_entry_t *e = &tl->entries[index];
	if (e->kind == TL_RASTER && !hash128_is_none(e->payload)) {
		timeline_checkpoint_set(tl, index + 1, e->payload, false);
	}
	else {
		hstore_unref(e->payload);
	}
	e->kind    = kind;
	e->payload = payload;
	tl->floor  = 0;
	for (i32 i = tl->count - 1; i >= 0; --i) {
		if (tl->entries[i].kind == TL_RASTER) {
			tl->floor = i + 1;
			break;
		}
	}
}

// Params known to parse, by hash: floors are found often
#define TL_OPS_OK_CACHE 64

static hash128_t tl_ops_ok[TL_OPS_OK_CACHE];
static i32       tl_ops_ok_next = 0;

static bool tl_ops_params_ok(tl_entry_t *e, u32 min_len, u32 min_children) {
	for (i32 i = 0; i < TL_OPS_OK_CACHE; ++i) {
		if (!hash128_is_none(tl_ops_ok[i]) && hash128_eq(tl_ops_ok[i], e->payload)) {
			return true;
		}
	}
	tl_params_t p;
	bool        ok = tl_params_get(e, &p) && p.len >= min_len && p.n >= min_children;
	free(p.blob);
	if (ok) {
		tl_ops_ok[tl_ops_ok_next] = e->payload;
		tl_ops_ok_next            = (tl_ops_ok_next + 1) % TL_OPS_OK_CACHE;
	}
	return ok;
}

// The materials a fill entry reads are as they were recorded
static bool tl_ops_fill_materials_same(tl_entry_t *e) {
	tl_params_t p;
	if (!tl_params_get(e, &p)) {
		return false;
	}
	tl_fill_params_t f;
	memcpy(&f, p.body, sizeof(f));
	u32 n = p.n;
	free(p.blob);
	fill_t *fill = fill_decode(&(f32_array_t){.buffer = f.fill, .length = FILL_ENCODE_LENGTH, .capacity = FILL_ENCODE_LENGTH}, false);
	bool    same = fill != NULL;
	for (i32 c = 0; same && c < FILL_CHANNEL_COUNT; ++c) {
		bool snapped = false; // Its snapshot renders it
		for (u32 k = 0; k < n && k < TL_PARAMS_MAX; ++k) {
			snapped = snapped || (fill->ch[c].kind == FILL_SOURCE_MATERIAL && fill->ch[c].material == f.snapshot_uids[k]);
		}
		if (f.material_sigs[c] != 0 && !snapped) {
			slot_material_t *m = fill_source_material(&fill->ch[c]);
			same               = m != NULL && material_cache_graph_signature(m) == f.material_sigs[c];
		}
	}
	free(fill);
	return same;
}

// Entry index re-renders at any resolution
bool timeline_ops_replays(timeline_t *tl, i32 index) {
	tl_entry_t *e = &tl->entries[index];
	switch (e->kind) {
	case TL_INVERT:
		return true;
	case TL_CLEAR:
		return tl_ops_params_ok(e, sizeof(tl_clear_params_t), 0);
	case TL_TRANSLATE:
		return tl_ops_params_ok(e, 8, 0);
	case TL_IMAGE:
		return tl_ops_params_ok(e, 1, 1);
	case TL_FILL:
		return tl_ops_params_ok(e, sizeof(tl_fill_params_t), 0) && tl_ops_fill_materials_same(e);
	case TL_APPLY_MASK:
		return tl_ops_params_ok(e, sizeof(f32), 0) && timeline_find(e->src_tl) != NULL;
	case TL_MERGE:
		return tl_ops_params_ok(e, sizeof(tl_merge_params_t), 0) && timeline_find(e->src_tl) != NULL;
	default:
		return false;
	}
}

// Entry index writes every texel when it replays: the state before it is not needed
bool timeline_ops_overwrites(timeline_t *tl, i32 index) {
	i32 kind = tl->entries[index].kind;
	return (kind == TL_CLEAR || kind == TL_FILL || kind == TL_IMAGE) && timeline_ops_replays(tl, index);
}

static gpu_texture_t *tl_ops_scratch[5];

static void tl_ops_clear(gpu_texture_t *t, u32 color) {
	_gpu_begin(t, NULL, NULL, GPU_CLEAR_COLOR, color, 0.0);
	gpu_end();
}

static bool tl_ops_replay_clear(tl_params_t *p, paint_target_t *t) {
	tl_clear_params_t c;
	memcpy(&c, p->body, sizeof(c));
	draw_saved_t saved = draw_suspend();
	tl_ops_clear(t->out[0], c.color);
	if (t->channels == 3) {
		tl_ops_clear(t->out[1], color_from_floats(0.5, 0.5, 1.0, 0.0));
		tl_ops_clear(t->out[2], color_from_floats(c.occlusion, c.roughness, c.metallic, 0.0));
	}
	draw_resume(saved);
	return true;
}

static bool tl_ops_replay_invert(paint_target_t *t) {
	gpu_texture_t *src = t->out[0];
	util_texture_ensure_target(&tl_ops_scratch[0], src->width, src->height, src->format);
	draw_saved_t saved = draw_suspend();
	gpu_copy_regions_begin();
	gpu_copy_region(tl_ops_scratch[0], 0, 0, src, 0, 0, src->width, src->height);
	gpu_copy_regions_end();
	util_copy_texture(src, tl_ops_scratch[0], pipes_invert_mask); // As slot_layer_invert_mask
	draw_resume(saved);
	return true;
}

// The contents moved by du, dv: the state before the move, read where the moved texels come from
static bool tl_ops_replay_translate(timeline_t *tl, i32 index, tl_params_t *p, paint_target_t *t) {
	f32 d[2];
	memcpy(d, p->body, sizeof(d));
	paint_target_t from = *t;
	from.uv_rect[0] -= d[0];
	from.uv_rect[1] -= d[1];
	from.uv_rect[2] -= d[0];
	from.uv_rect[3] -= d[1];
	return timeline_materialize(tl, index, &from);
}

// Decoded image of the last asset replayed
static hash128_t      tl_ops_image_hash = {0};
static gpu_texture_t *tl_ops_image      = NULL;

static bool tl_ops_replay_image(tl_params_t *p, paint_target_t *t) {
	if (tl_ops_image == NULL || !hash128_eq(tl_ops_image_hash, p->children[0])) {
		u32 len   = 0;
		u8 *bytes = hstore_get(p->children[0], &len);
		if (bytes == NULL) {
			return false;
		}
		buffer_t b = {.buffer = bytes, .length = len};
		char    *ext = (char *)p->body;
		p->body[p->len - 1] = 0;
		gpu_texture_t *img = gpu_create_texture_from_encoded_bytes(&b, ext);
		free(bytes);
		if (img == NULL) {
			return false;
		}
		if (tl_ops_image != NULL) {
			gpu_delete_texture(tl_ops_image);
		}
		tl_ops_image      = img;
		tl_ops_image_hash = p->children[0];
	}
	// As slot_layer_clear(l, 0, image, 1.0, layers_default_rough, 0.0): the image scaled over the whole layer
	f32          du    = t->uv_rect[2] - t->uv_rect[0];
	f32          dv    = t->uv_rect[3] - t->uv_rect[1];
	f32          w     = t->w / du;
	f32          h     = t->h / dv;
	draw_saved_t saved = draw_suspend();
	tl_ops_clear(t->out[0], 0x00000000);
	draw_begin(t->out[0], false, 0);
	draw_scaled_image(tl_ops_image, -t->uv_rect[0] * w, -t->uv_rect[1] * h, w, h);
	draw_end();
	if (t->channels == 3) {
		tl_ops_clear(t->out[1], color_from_floats(0.5, 0.5, 1.0, 0.0));
		tl_ops_clear(t->out[2], color_from_floats(1.0, layers_default_rough, 0.0, 0.0));
	}
	draw_resume(saved);
	return true;
}

// Render targets the dilation reads by name, pointed at t's textures for a while
typedef struct tl_ops_swap {
	char            *name;
	render_target_t *was;
	render_target_t  rt;
} tl_ops_swap_t;

static void tl_ops_swap(tl_ops_swap_t *s, char *name, gpu_texture_t *t) {
	s->name = string_copy(name);
	s->was  = any_map_get(render_path_render_targets, s->name);
	s->rt   = (render_target_t){.name = s->name, .width = t->width, .height = t->height, ._image = t}; // As paint_replay.c
	any_map_set(render_path_render_targets, s->name, &s->rt);
}

static void tl_ops_unswap(tl_ops_swap_t *s) {
	if (s->was != NULL) {
		any_map_set(render_path_render_targets, s->name, s->was);
	}
	else {
		map_delete(render_path_render_targets, s->name);
	}
}

// t dilated as fill_rasterize dilates the live layer l: over the uv islands of t's part of the layer
static void tl_ops_dilate(slot_layer_t *l, paint_target_t *t) {
	if (g_config->dilate_radius <= 0) {
		return;
	}
	draw_saved_t  saved = draw_suspend();
	tl_ops_swap_t swaps[4];
	i32           n = 0;
	util_texture_ensure_target(&tl_ops_scratch[0], t->w, t->h, t->out[0]->format);
	tl_ops_swap(&swaps[n++], string_tmp("texpaint%d", l->id), t->out[0]);
	if (t->channels == 3) {
		tl_ops_swap(&swaps[n++], string_tmp("texpaint_nor%d", l->id), t->out[1]);
		tl_ops_swap(&swaps[n++], string_tmp("texpaint_pack%d", l->id), t->out[2]);
	}
	tl_ops_swap(&swaps[n++], "temptex0", tl_ops_scratch[0]);
	slot_layer_t  *layer   = g_context->layer;
	bool           paint2d = g_context->paint2d;
	bool           replay  = paint_exec_replay;
	gpu_texture_t *map     = paint_replay_dilatemap;
	g_context->layer       = l;
	g_context->paint2d     = false;
	paint_exec_replay      = true; // The dilate map of t's size and part (uniforms.c)
	paint_replay_dilatemap = paint_replay_dilate_map(t->w, t->h, t->uv_rect);
	render_path_paint_dilate_now(true, true);
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	paint_replay_dilatemap = map;
	paint_exec_replay      = replay;
	g_context->paint2d     = paint2d;
	g_context->layer       = layer;
	for (i32 i = n - 1; i >= 0; --i) {
		tl_ops_unswap(&swaps[i]);
	}
	draw_resume(saved);
}

// The fill rendered for t (fill_render) by the layer for a while: its fill and props the recorded ones
static bool tl_ops_replay_fill(timeline_t *tl, tl_params_t *p, paint_target_t *t) {
	slot_layer_t *l = slot_layer_find_uid(tl->layer_uid);
	if (l == NULL) {
		return false;
	}
	tl_fill_params_t f;
	memcpy(&f, p->body, sizeof(f));
	fill_t *fill = fill_decode(&(f32_array_t){.buffer = f.fill, .length = FILL_ENCODE_LENGTH, .capacity = FILL_ENCODE_LENGTH}, false);
	if (fill == NULL) {
		return false;
	}
	// Materials changed since: their snapshots stand in, under uids of their own (a tile each)
	u32 stand_ins[TL_PARAMS_MAX];
	u32 nstand = 0;
	for (u32 k = 0; k < p->n && k < TL_PARAMS_MAX; ++k) {
		slot_material_t *m = paint_mat_snapshot_instantiate(p->children[k]);
		if (m == NULL || array_index_of(g_project->_->materials, m) >= 0) {
			continue; // Gone (the entry checked it does not need it), or the project's own still
		}
		u32 uid = 0x80000000u | (u32)(p->children[k].lo & 0x7fffffffu);
		fill_stand_in_set(uid, m);
		stand_ins[nstand++] = uid;
		for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
			if (fill->ch[c].kind == FILL_SOURCE_MATERIAL && fill->ch[c].material == f.snapshot_uids[k]) {
				fill->ch[c].material = uid;
			}
		}
	}
	slot_layer_t saved = *l; // The layer itself renders it (its row of fill parameters), as the fill it was
	l->fill            = fill;
	l->uv_map          = f.uv_map;
	l->object_mask     = f.object_mask;
	memcpy(&l->decal_mat, f.decal_mat, sizeof(f.decal_mat));
	bool *flags[11] = {&l->paint_base, &l->paint_opac,   &l->paint_occ,          &l->paint_rough, &l->paint_met, &l->paint_nor,
	                   &l->paint_nor_blend, &l->paint_height, &l->paint_height_blend, &l->paint_emis,  &l->paint_subs};
	for (i32 i = 0; i < 11; ++i) {
		*flags[i] = f.paint[i] != 0;
	}
	fill_params_invalidate();
	bool ok = fill_render(l, t->out, t->uv_rect);
	*l      = saved;
	fill_params_invalidate();
	for (u32 k = 0; k < nstand; ++k) {
		fill_stand_in_set(stand_ins[k], NULL); // Its tile goes with the next update
	}
	free(fill);
	if (ok) {
		tl_ops_dilate(l, t);
	}
	return ok;
}

#define TL_OPS_SRC_DEPTH 4 // Merges of layers holding merges

static gpu_texture_t *tl_ops_src[TL_OPS_SRC_DEPTH][3]; // The src timeline materialized at t's size and part
static i32            tl_ops_src_depth = 0;
static gpu_texture_t *tl_ops_temp      = NULL; // Copy of t's base for the passes

// The entry's src timeline at its src_pos, materialized for t's part of the layer into tl_ops_src[depth] (src entries
// reading timelines of their own use the next depth). False when it can not be.
static bool tl_ops_src_materialize(tl_entry_t *e, paint_target_t *t, i32 channels, paint_target_t *s) {
	timeline_t *src = timeline_find(e->src_tl);
	if (src == NULL || src->channels != channels || tl_ops_src_depth >= TL_OPS_SRC_DEPTH) {
		return false;
	}
	i32 d       = tl_ops_src_depth;
	*s          = *t;
	s->channels = channels;
	for (i32 k = 0; k < 3; ++k) {
		s->out[k] = NULL;
	}
	for (i32 k = 0; k < channels; ++k) {
		util_texture_ensure_target(&tl_ops_src[d][k], t->w, t->h, channels == 1 ? GPU_TEXTURE_FORMAT_RGBA32 : t->out[k]->format); // Masks: RGBA32
		s->out[k] = tl_ops_src[d][k];
	}
	tl_ops_src_depth++;
	bool ok = timeline_materialize(src, e->src_pos, s);
	tl_ops_src_depth--;
	return ok;
}

// The mask's pixels masking t's base, as layers_apply_mask
static bool tl_ops_replay_apply_mask(tl_entry_t *e, tl_params_t *p, paint_target_t *t) {
	paint_target_t s;
	if (t->channels != 3 || !tl_ops_src_materialize(e, t, 1, &s)) {
		return false;
	}
	f32 opacity;
	memcpy(&opacity, p->body, sizeof(opacity));
	draw_saved_t saved = draw_suspend();
	util_texture_ensure_target(&tl_ops_temp, t->w, t->h, t->out[0]->format);
	layers_apply_mask_pass(t->out[0], tl_ops_temp, s.out[0], opacity);
	draw_resume(saved);
	return true;
}

// The merged layer's pixels merged into t, as layers_merge_down for layers without masks
static bool tl_ops_replay_merge(tl_entry_t *e, tl_params_t *p, paint_target_t *t) {
	paint_target_t s;
	if (t->channels != 3 || !tl_ops_src_materialize(e, t, 3, &s)) {
		return false;
	}
	tl_merge_params_t mp;
	memcpy(&mp, p->body, sizeof(mp));
	draw_saved_t saved = draw_suspend();
	util_texture_ensure_target(&tl_ops_temp, t->w, t->h, t->out[0]->format);
	if (mp.opacity_below < 1.0) { // layers_bake_opacity
		layers_apply_mask_pass(t->out[0], tl_ops_temp, render_path_empty_black(), 1.0 - mp.opacity_below);
	}
	slot_layer_t l0 = {.texpaint = t->out[0], .texpaint_nor = t->out[1], .texpaint_pack = t->out[2], .visible = true, .mask_opacity = 1.0};
	slot_layer_t l1 = {.texpaint = s.out[0], .texpaint_nor = s.out[1], .texpaint_pack = s.out[2], .visible = mp.visible, .mask_opacity = mp.opacity,
	                   .blending = mp.blending};
	bool *f[8] = {&l1.paint_base, &l1.paint_nor, &l1.paint_nor_blend, &l1.paint_occ, &l1.paint_rough, &l1.paint_met, &l1.paint_height, &l1.paint_height_blend};
	for (i32 i = 0; i < 8; ++i) {
		*f[i] = mp.paint[i] != 0;
	}
	bool linear                = g_project->linear_blending;
	g_project->linear_blending = mp.linear != 0;
	layers_merge_layer_ex(&l0, &l1, false, tl_ops_temp);
	g_project->linear_blending = linear;
	draw_resume(saved);
	return true;
}

bool timeline_ops_replay(timeline_t *tl, i32 index, paint_target_t *t) {
	tl_entry_t *e = &tl->entries[index];
	if (e->kind == TL_INVERT) {
		return tl_ops_replay_invert(t);
	}
	if (!timeline_ops_replays(tl, index)) {
		return false;
	}
	tl_params_t p;
	if (!tl_params_get(e, &p)) {
		return false;
	}
	bool ok = false;
	switch (e->kind) {
	case TL_CLEAR:
		ok = tl_ops_replay_clear(&p, t);
		break;
	case TL_TRANSLATE:
		ok = tl_ops_replay_translate(tl, index, &p, t);
		break;
	case TL_IMAGE:
		ok = tl_ops_replay_image(&p, t);
		break;
	case TL_FILL:
		ok = tl_ops_replay_fill(tl, &p, t);
		break;
	case TL_APPLY_MASK:
		ok = tl_ops_replay_apply_mask(e, &p, t);
		break;
	case TL_MERGE:
		ok = tl_ops_replay_merge(e, &p, t);
		break;
	default:
		break;
	}
	free(p.blob);
	return ok;
}

// New or opened project: decoded images and known params belong to the old one
void timeline_ops_reset() {
	if (tl_ops_image != NULL) {
		gpu_delete_texture(tl_ops_image);
		tl_ops_image = NULL;
	}
	tl_ops_image_hash = (hash128_t){0};
	memset(tl_ops_ok, 0, sizeof(tl_ops_ok));
}
