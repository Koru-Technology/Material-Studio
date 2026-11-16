#include "../global.h"

// Working resolution changes that keep detail (with g_config->export_tiled; layers_resize resamples otherwise). A change
// is a history step (HISTORY_ACTION_RESOLUTION): undo goes back to the resolution before, redo forth again.
//
// Each paint layer and mask goes to the new size in turn. The state it leaves is kept (its tile map, with what it went to,
// layers_resolution_kept). Back to a size it left, when it still holds what it went to from there, the kept state comes
// back exactly (2K to 4K to 2K, and undo and redo of the change). Otherwise it is scaled: down box-filtered (halved while
// at least twice as large: a texel between four is their average), up bilinear until timelines materialize at any
// resolution.
// The timeline of the layer gets the new state as a checkpoint at its cursor. Fill layers render again at the new size,
// mask stacks evaluate again.

typedef struct layers_resolution_kept {
	u32       uid;
	i32       w;    // A size the layer left
	i32       h;
	hash128_t at;   // Tile map of the layer at that size
	i32       to_w; // The size it went to
	i32       to_h;
	hash128_t to;   // and what it held there
} layers_resolution_kept_t;

static layers_resolution_kept_t *layers_resolution_kept     = NULL;
static i32                       layers_resolution_nkept    = 0;
static i32                       layers_resolution_restores = 0; // Layers that came back exactly (tests)

static layers_resolution_kept_t *layers_resolution_find(u32 uid, i32 w, i32 h) {
	for (i32 i = 0; i < layers_resolution_nkept; ++i) {
		layers_resolution_kept_t *k = &layers_resolution_kept[i];
		if (k->uid == uid && k->w == w && k->h == h) {
			return k;
		}
	}
	return NULL;
}

static void layers_resolution_keep(u32 uid, i32 w, i32 h, hash128_t at, i32 to_w, i32 to_h, hash128_t to) {
	layers_resolution_kept_t *k = layers_resolution_find(uid, w, h);
	if (k == NULL) {
		layers_resolution_kept = realloc(layers_resolution_kept, sizeof(layers_resolution_kept_t) * (layers_resolution_nkept + 1));
		k                      = &layers_resolution_kept[layers_resolution_nkept++];
	}
	else {
		hstore_unref(k->at);
		hstore_unref(k->to);
	}
	*k = (layers_resolution_kept_t){.uid = uid, .w = w, .h = h, .at = at, .to_w = to_w, .to_h = to_h, .to = to};
}

// New project: the kept states go (the blob store is reset with it)
void layers_resolution_reset() {
	free(layers_resolution_kept);
	layers_resolution_kept  = NULL;
	layers_resolution_nkept = 0;
}

static hash128_t layers_resolution_capture(slot_layer_t *l) {
	paint_target_t t;
	timeline_target_live(l, &t);
	return timeline_tiles_capture(&t, HASH128_NONE, NULL);
}

// dst becomes src scaled: halved by 2 x 2 averages while src is at least twice as large, then scaled (bilinear)
static void layers_resolution_scale(gpu_texture_t *dst, gpu_texture_t *src) {
	gpu_texture_t *cur  = src;
	gpu_texture_t *tmp  = NULL;
	gpu_texture_t *tmp2 = NULL;
	while (cur->width >= dst->width * 2 && cur->height >= dst->height * 2 && (cur->width / 2 != dst->width || cur->height / 2 != dst->height)) {
		gpu_texture_t **t = tmp == NULL || cur == tmp2 ? &tmp : &tmp2;
		util_texture_ensure_target(t, cur->width / 2, cur->height / 2, src->format);
		draw_begin(*t, false, 0);
		draw_set_pipeline(layers_pipe_for(pipes_copy, src->format));
		draw_scaled_image(cur, 0, 0, (*t)->width, (*t)->height); // A texel between four: their average
		draw_set_pipeline(NULL);
		draw_end();
		cur = *t;
	}
	draw_begin(dst, false, 0);
	draw_set_pipeline(layers_pipe_for(pipes_copy, dst->format));
	draw_scaled_image(cur, 0, 0, dst->width, dst->height);
	draw_set_pipeline(NULL);
	draw_end();
	util_texture_release_target(&tmp);
	util_texture_release_target(&tmp2);
}

// Points the render target <prefix><ext> of l at t
static void layers_resolution_set_target(slot_layer_t *l, char *prefix, gpu_texture_t *t) {
	render_target_t *rt = any_map_get(render_path_render_targets, string_tmp("%s%s", prefix, l->ext));
	if (rt != NULL) {
		rt->_image = t;
		rt->width  = t->width;
		rt->height = t->height;
	}
}

// Layer or mask l from w0 x h0 (its textures) to w1 x h1
static void layers_resolution_layer(slot_layer_t *l, i32 w0, i32 h0, i32 w1, i32 h1) {
	bool           mask    = slot_layer_is_mask(l);
	gpu_texture_t *old[3]  = {l->texpaint, mask ? NULL : l->texpaint_nor, mask ? NULL : l->texpaint_pack};
	gpu_texture_t *now[3]  = {NULL, NULL, NULL};
	char          *name[3] = {"texpaint", "texpaint_nor", "texpaint_pack"};
	for (i32 k = 0; k < 3; ++k) {
		if (old[k] != NULL) {
			now[k] = gpu_create_render_target(w1, h1, old[k]->format);
		}
	}
	hash128_t before = layers_resolution_capture(l);

	// Back to a size the layer left, and it holds what it went to from there: the state kept there
	layers_resolution_kept_t *k     = layers_resolution_find(l->uid, w1, h1);
	bool                      exact = false;
	if (k != NULL && k->to_w == w0 && k->to_h == h0 && hash128_eq(k->to, before)) {
		paint_target_t t = {.w = w1, .h = h1, .res_w = w1, .res_h = h1, .channels = mask ? 1 : 3};
		t.uv_rect[2]     = 1.0;
		t.uv_rect[3]     = 1.0;
		for (i32 c = 0; c < 3; ++c) {
			t.out[c] = now[c];
		}
		exact = timeline_tiles_restore(k->at, &t);
	}
	if (exact) {
		layers_resolution_restores++;
	}
	else {
		for (i32 c = 0; c < 3; ++c) {
			if (old[c] != NULL) {
				layers_resolution_scale(now[c], old[c]);
			}
		}
	}

	for (i32 c = 0; c < 3; ++c) {
		if (old[c] != NULL) {
			gpu_delete_texture(old[c]);
			layers_resolution_set_target(l, name[c], now[c]);
		}
	}
	l->texpaint = now[0];
	if (!mask) {
		l->texpaint_nor  = now[1];
		l->texpaint_pack = now[2];
	}
	else {
		mask_stack_free_texture(l); // Evaluated again at the new size
		mask_stack_mark_dirty(l);
	}

	hash128_t after = layers_resolution_capture(l);
	hstore_ref(after); // The state left is kept, and what it went to
	layers_resolution_keep(l->uid, w0, h0, before, w1, h1, after);
	timeline_t *tl = timeline_of(l);
	if (tl != NULL) { // Its state at the cursor, at the new size (one size a position until checkpoints are sized)
		timeline_checkpoint_set(tl, tl->cursor, after, true);
	}
	else {
		hstore_unref(after);
	}
	mask_stack_layer_changed(l);
}

// The project's layers to w x h now. Not a history step: undo and redo call it.
void layers_resolution_apply(i32 w, i32 h) {
	if (g_project->_->layers->length == 0) {
		return;
	}
	slot_layer_t *first = g_project->_->layers->buffer[0];
	i32           w0    = first->texpaint->width;
	i32           h0    = first->texpaint->height;
	base_res_x          = w;
	base_res_y          = h;
	base_res            = w == h ? config_get_texture_res_pos(w) : TEXTURE_RES_CUSTOM;
	if (w == w0 && h == h0) {
		return;
	}
	timeline_finalize();
	history_flush_restores(NULL);
	draw_saved_t saved = draw_suspend();
	i32          n     = g_project->_->layers->length;
	i32          ring  = g_config->undo_steps; // Fewer whole layer copies at 8K and above, as layers_resize
	if (w >= 8192 || h >= 8192) {
		i32 cap = w == 8192 ? 4 : 1;
		ring    = ring < cap ? ring : cap;
	}
	history_ring_set_size(ring);
	for (i32 i = 0; i < n; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if ((slot_layer_is_layer(l) || slot_layer_is_mask(l)) && l->texpaint != NULL) {
			layers_resolution_layer(l, l->texpaint->width, l->texpaint->height, w, h);
		}
	}
	layers_resize_targets(); // Undo ring, live layer, paint scratch
	render_path_raytrace_ready = false;
	g_context->ddirty          = 2;
	layers_crop                = false;

	// Fill layers render at the new size
	slot_layer_t    *_layer    = g_context->layer;
	slot_material_t *_material = g_context->material;
	for (i32 i = 0; i < n; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill_material != NULL) {
			g_context->layer    = l;
			g_context->material = l->fill_material;
			layers_set_object_mask();
			layers_update_fill_layer(true);
		}
	}
	g_context->layer    = _layer;
	g_context->material = _material;
	layers_set_object_mask();
	make_material_parse_paint_material(true);
	draw_resume(saved);
	g_context->layer_preview_dirty = true;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

static i32 layers_resolution_pending_w = 0;
static i32 layers_resolution_pending_h = 0;

static void layers_resolution_run(void *_) {
	i32 w0 = g_project->_->layers->buffer[0]->texpaint->width;
	i32 h0 = g_project->_->layers->buffer[0]->texpaint->height;
	i32 w1 = layers_resolution_pending_w;
	i32 h1 = layers_resolution_pending_h;
	if (w1 == w0 && h1 == h0) {
		return;
	}
	history_resolution(w0, h0, w1, h1);
	layers_resolution_apply(w1, h1);
}

// The working resolution set (base_res_x, base_res_y) changes on the next frame, undoable. The resolution controls call it
// instead of layers_on_resized with the tiled export.
void layers_resolution_begin(i32 w, i32 h) {
	layers_resolution_pending_w = w;
	layers_resolution_pending_h = h;
	sys_notify_on_next_frame(&layers_resolution_run, NULL);
}

// The resolution controls: base_res_x and base_res_y set
void layers_on_resized_by_user() {
	if (g_config->export_tiled) {
		layers_resolution_begin((i32)base_res_x, (i32)base_res_y);
	}
	else {
		layers_on_resized();
	}
}

// Tests: layers that came back exactly from a kept state so far
i32 layers_resolution_test_restores() {
	return layers_resolution_restores;
}
