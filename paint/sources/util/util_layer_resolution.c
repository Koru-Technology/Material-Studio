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

// History reset (new or opened project, mesh import): the kept states go, their blobs can be collected
void layers_resolution_reset() {
	for (i32 i = 0; i < layers_resolution_nkept; ++i) {
		hstore_unref(layers_resolution_kept[i].at);
		hstore_unref(layers_resolution_kept[i].to);
	}
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

// The change runs as a job, a layer a step (layers_resolution_step, each frame): the step is pushed once all are done.
// Undo and redo run it at once (layers_resolution_apply).

static struct layers_resolution_job {
	bool active;
	bool push; // A history step when done (not for undo and redo)
	i32  w0;
	i32  h0;
	i32  w1;
	i32  h1;
	i32  next; // Layer index
} lr;

static void layers_resolution_update(void *_);

static bool layers_resolution_start(i32 w, i32 h, bool push) {
	if (lr.active || g_project->_->layers->length == 0) {
		return false;
	}
	slot_layer_t *first = g_project->_->layers->buffer[0];
	lr                  = (struct layers_resolution_job){.active = true, .push = push, .w0 = first->texpaint->width, .h0 = first->texpaint->height, .w1 = w, .h1 = h};
	if (w == lr.w0 && h == lr.h0) {
		lr.active = false;
		return false;
	}
	timeline_finalize();
	history_flush_restores(NULL);
	return true;
}

// The rest once every layer is at the new size: the project's resolution, targets that follow it, fill layers
static void layers_resolution_finish() {
	i32 w = lr.w1, h = lr.h1;
	base_res_x = w;
	base_res_y = h;
	base_res   = w == h ? config_get_texture_res_pos(w) : TEXTURE_RES_CUSTOM;
	layers_resize_targets(); // Live layer, paint scratch
	render_path_raytrace_ready = false;
	g_context->ddirty          = 2;
	layers_crop                = false;

	slot_layer_t    *_layer    = g_context->layer;
	slot_material_t *_material = g_context->material;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
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
	g_context->layer_preview_dirty = true;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	lr.active = false;
	if (lr.push) {
		history_resolution(lr.w0, lr.h0, lr.w1, lr.h1);
	}
}

// Converts the next layer, finishes after the last. True once the change is done.
bool layers_resolution_step() {
	if (!lr.active) {
		return true;
	}
	draw_saved_t saved = draw_suspend();
	while (lr.next < g_project->_->layers->length) {
		slot_layer_t *l = g_project->_->layers->buffer[lr.next++];
		if ((slot_layer_is_layer(l) || slot_layer_is_mask(l)) && l->texpaint != NULL && (l->texpaint->width != lr.w1 || l->texpaint->height != lr.h1)) {
			layers_resolution_layer(l, l->texpaint->width, l->texpaint->height, lr.w1, lr.h1);
			break; // One a step
		}
	}
	if (lr.next >= g_project->_->layers->length) {
		layers_resolution_finish();
	}
	draw_resume(saved);
	return !lr.active;
}

bool layers_resolution_running() {
	return lr.active;
}

f32 layers_resolution_progress() {
	i32 n = g_project->_->layers->length;
	return !lr.active || n == 0 ? 1.0 : lr.next / (f32)(n + 1);
}

// The project's layers to w x h now (undo and redo of the step)
void layers_resolution_apply(i32 w, i32 h) {
	if (lr.active) { // One in progress ends first
		while (!layers_resolution_step()) {
		}
		sys_remove_update(&layers_resolution_update);
	}
	if (!layers_resolution_start(w, h, false)) {
		return;
	}
	while (!layers_resolution_step()) {
	}
}

static void layers_resolution_update(void *_) {
	if (layers_resolution_step()) {
		sys_remove_update(&layers_resolution_update);
	}
}

// The working resolution goes to w x h over the next frames, a layer each, undoable once done. The resolution controls
// call it instead of layers_on_resized with the tiled export.
bool layers_resolution_begin(i32 w, i32 h) {
	if (!layers_resolution_start(w, h, true)) {
		return false;
	}
	sys_notify_on_update(&layers_resolution_update, NULL);
	return true;
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
