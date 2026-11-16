#include "global.h"

// Undo and redo: the list of steps, whole layer copies in the undo ring (paint steps keep tiles, history_tiles.c),
// steps checked against the project before they run. history_steps.c pushes them, history_swap.c swaps their state.

static void history_redo_smart_material(char *name);

static char *history_action_to_string(history_action_t action) {
	switch (action) {
	case HISTORY_ACTION_PAINT:
		return tr("Paint");
	case HISTORY_ACTION_EDIT_NODES:
		return tr("Edit Nodes");
	case HISTORY_ACTION_NEW_LAYER:
		return tr("New Layer");
	case HISTORY_ACTION_NEW_BLACK_MASK:
		return tr("New Black Mask");
	case HISTORY_ACTION_NEW_WHITE_MASK:
		return tr("New White Mask");
	case HISTORY_ACTION_NEW_FILL_MASK:
		return tr("New Fill Mask");
	case HISTORY_ACTION_NEW_GROUP:
		return tr("New Group");
	case HISTORY_ACTION_DELETE_LAYER:
		return tr("Delete Layer");
	case HISTORY_ACTION_CLEAR_LAYER:
		return tr("Clear Layer");
	case HISTORY_ACTION_DUPLICATE_LAYER:
		return tr("Duplicate Layer");
	case HISTORY_ACTION_ORDER_LAYERS:
		return tr("Order Layers");
	case HISTORY_ACTION_MERGE_LAYERS:
		return tr("Merge Layers");
	case HISTORY_ACTION_APPLY_MASK:
		return tr("Apply Mask");
	case HISTORY_ACTION_INVERT_MASK:
		return tr("Invert Mask");
	case HISTORY_ACTION_TO_FILL_LAYER:
		return tr("To Fill Layer");
	case HISTORY_ACTION_TO_FILL_MASK:
		return tr("To Fill Mask");
	case HISTORY_ACTION_TO_PAINT_LAYER:
		return tr("To Paint Layer");
	case HISTORY_ACTION_TO_PAINT_MASK:
		return tr("To Paint Mask");
	case HISTORY_ACTION_LAYER_OPACITY:
		return tr("Layer Opacity");
	case HISTORY_ACTION_LAYER_BLENDING:
		return tr("Layer Blending");
	case HISTORY_ACTION_DELETE_NODE_GROUP:
		return tr("Delete Node Group");
	case HISTORY_ACTION_NEW_MATERIAL:
		return tr("New Material");
	case HISTORY_ACTION_DELETE_MATERIAL:
		return tr("Delete Material");
	case HISTORY_ACTION_DUPLICATE_MATERIAL:
		return tr("Duplicate Material");
	case HISTORY_ACTION_LAYER_VISIBLE:
		return tr("Layer Visibility");
	case HISTORY_ACTION_LAYER_NAME:
		return tr("Rename Layer");
	case HISTORY_ACTION_LAYER_OBJECT:
		return tr("Layer Object");
	case HISTORY_ACTION_LAYER_SCALE:
		return tr("Layer UV Scale");
	case HISTORY_ACTION_LAYER_ANGLE:
		return tr("Layer Angle");
	case HISTORY_ACTION_LAYER_UV_TYPE:
		return tr("Layer UV Type");
	case HISTORY_ACTION_LAYER_FILL_MATERIAL:
		return tr("Layer Material");
	case HISTORY_ACTION_SMART_MATERIAL:
		return tr("Smart Material");
	case HISTORY_ACTION_NEW_BRUSH:
		return tr("New Brush");
	case HISTORY_ACTION_DELETE_BRUSH:
		return tr("Delete Brush");
	case HISTORY_ACTION_DUPLICATE_BRUSH:
		return tr("Duplicate Brush");
	case HISTORY_ACTION_NEW_SWATCH:
		return tr("New Swatch");
	case HISTORY_ACTION_DELETE_SWATCH:
		return tr("Delete Swatch");
	case HISTORY_ACTION_DUPLICATE_SWATCH:
		return tr("Duplicate Swatch");
	case HISTORY_ACTION_REPLACE_SWATCHES:
		return tr("Replace Swatches");
	case HISTORY_ACTION_EDIT_SWATCH:
		return tr("Edit Swatch");
	case HISTORY_ACTION_MASK_HEIGHT_BLEND:
		return tr("Mask Height Blend");
	case HISTORY_ACTION_MASK_STACK:
		return tr("Edit Mask Stack");
	case HISTORY_ACTION_RESOLUTION:
		return tr("Resolution");
	}
	return tr("Paint");
}

// Whole layer copies the ring holds: the undo_steps preference, fewer at 8K and above (history_ring_set_size)
i32 history_ring_size() {
	return history_undo_layers != NULL && history_undo_layers->length > 0 ? history_undo_layers->length : g_config->undo_steps;
}

// Slots of the undo ring before and after history_undo_i
i32 history_ring_prev() {
	return history_undo_i - 1 < 0 ? history_ring_size() - 1 : history_undo_i - 1;
}

i32 history_ring_next() {
	return (history_undo_i + 1) % history_ring_size();
}

static bool history_in(i32 i, i32 length) {
	return i >= 0 && i < length;
}

// A layer brought back keeps the uid steps and timelines know it by, unless another layer took it meanwhile
static void history_restore_uid(slot_layer_t *l, u32 uid) {
	if (uid != 0 && slot_layer_find_uid(uid) == NULL) {
		l->uid = uid;
	}
}

static void history_restore_material_uid(slot_material_t *m, u32 uid) {
	if (uid != 0 && slot_material_find_uid(uid) == NULL) {
		m->uid = uid;
	}
}

static void history_undo_invert_mask(history_step_t *step) {
	if (!history_in(step->layer, g_project->_->layers->length)) {
		return;
	}
	g_context->layer = g_project->_->layers->buffer[step->layer];
	slot_layer_invert_mask(g_context->layer);
	mask_stack_mark_dirty(g_context->layer);
}

static void history_step_free(history_step_t *step) {
	history_records_free(step);
	for (i32 i = 0; step->merge_layers != NULL && i < step->merge_layers->length; ++i) {
		slot_layer_t *l = step->merge_layers->buffer[i];
		if (l->texpaint != NULL || slot_layer_is_group(l)) { // Records freed their textures already
			slot_layer_unload(l);
		}
	}
	history_tiles_release(step);
	array_delete(step->mask_effects);
	if (step->tl_moves != NULL) {
		array_free(step->tl_moves);
		free(step->tl_moves);
	}
	free(step);
}

static i32  history_ring_pending = 0; // Whole layer copies made for the step about to be pushed
static u32  history_serial_next  = 1;
static bool history_v2_on        = false;

bool history_v2_active() {
	return history_v2_on;
}

// Steps kept at most, paint steps keep their tiles and can go much further back than whole layer copies
static i32 history_max_steps() {
	return history_v2_on ? g_config->history_max_steps : 64;
}

// Whole layer copies held by the steps that can still be undone or redone
static i32 history_ring_used() {
	i32 used = 0;
	for (i32 i = 1; i < history_steps->length; ++i) {
		used += history_steps->buffer[i]->undo_ring;
	}
	return used;
}

// The oldest step goes, the one after it becomes the base nothing undoes past
void history_drop_oldest() {
	if (history_steps->length < 2) {
		return;
	}
	history_step_free(array_shift(history_steps));
	if (history_undos + history_redos > history_steps->length - 1) {
		if (history_undos > 0) {
			history_undos--;
		}
		else {
			history_redos--;
		}
	}
}

// Whether the layer of the step is in the project when it runs: not when the step itself brings it back
static bool history_step_layer_present(history_step_t *step, bool undo) {
	switch (step->action) {
	case HISTORY_ACTION_ORDER_LAYERS: // Positions swap with each run
	case HISTORY_ACTION_DELETE_LAYER: // A range of positions
	case HISTORY_ACTION_MERGE_LAYERS:
		return false;
	case HISTORY_ACTION_NEW_LAYER:
	case HISTORY_ACTION_NEW_BLACK_MASK:
	case HISTORY_ACTION_NEW_WHITE_MASK:
	case HISTORY_ACTION_NEW_FILL_MASK:
	case HISTORY_ACTION_NEW_GROUP:
	case HISTORY_ACTION_SMART_MATERIAL:
		return undo;
	case HISTORY_ACTION_APPLY_MASK:
		return !undo;
	default:
		return true;
	}
}

// Steps find their layer, its parent and material by uid first: positions kept at push follow layers and materials moved
// or removed without history since. Without a uid match the positions stay as they are.
static void history_step_resolve(history_step_t *step, bool undo) {
	slot_layer_t_array_t *layers = g_project->_->layers;
	if (step->layer_uid != 0 && history_step_layer_present(step, undo)) {
		slot_layer_t *l = slot_layer_find_uid(step->layer_uid);
		if (l != NULL) {
			step->layer = array_index_of(layers, l);
		}
	}
	bool created = !undo && (step->action == HISTORY_ACTION_NEW_LAYER || step->action == HISTORY_ACTION_NEW_BLACK_MASK ||
	                         step->action == HISTORY_ACTION_NEW_WHITE_MASK || step->action == HISTORY_ACTION_NEW_FILL_MASK);
	if (created && step->parent_uid != 0) {
		slot_layer_t *p = slot_layer_find_uid(step->parent_uid);
		if (p != NULL) {
			step->layer_parent = array_index_of(layers, p) + 1; // The parent's position while the layer is out, plus one
		}
	}
	if (step->material_uid != 0) {
		slot_material_t *m = slot_material_find_uid(step->material_uid);
		if (m != NULL) {
			step->material = array_index_of(g_project->_->materials, m);
		}
	}
}

// The layers, materials and objects step points at by position are still there. A step left behind by a change made
// without history would read out of bounds, it is skipped instead.
static bool history_step_valid_last = false; // What the last check found, its cursors move with the step

static bool history_step_valid_check(history_step_t *step, bool undo);

static bool history_step_valid(history_step_t *step, bool undo) {
	history_step_valid_last = history_step_valid_check(step, undo);
	return history_step_valid_last;
}

static bool history_step_valid_check(history_step_t *step, bool undo) {
	history_step_resolve(step, undo);
	i32 layers    = g_project->_->layers->length;
	i32 materials = g_project->_->materials->length;
	switch (step->action) {
	case HISTORY_ACTION_EDIT_NODES:
		return step->canvas_type != 0 || step->canvas_group != -1 || history_in(step->material, materials);
	case HISTORY_ACTION_DELETE_NODE_GROUP:
	case HISTORY_ACTION_NEW_SWATCH:
	case HISTORY_ACTION_DELETE_SWATCH:
	case HISTORY_ACTION_DUPLICATE_SWATCH:
	case HISTORY_ACTION_REPLACE_SWATCHES:
	case HISTORY_ACTION_EDIT_SWATCH:
	case HISTORY_ACTION_RESOLUTION:
		return true;
	case HISTORY_ACTION_NEW_LAYER:
	case HISTORY_ACTION_NEW_BLACK_MASK:
	case HISTORY_ACTION_NEW_WHITE_MASK:
	case HISTORY_ACTION_NEW_FILL_MASK:
		if (undo) {
			return history_in(step->layer, layers);
		}
		return step->layer >= 0 && step->layer <= layers && step->layer_parent - 1 < layers &&
		       (step->action != HISTORY_ACTION_NEW_FILL_MASK || history_in(step->material, materials));
	case HISTORY_ACTION_SMART_MATERIAL:
		return !undo || history_in(step->layer, layers);
	case HISTORY_ACTION_NEW_GROUP:
		return history_in(step->layer - 1, layers) && (undo ? step->layer < layers : step->layer <= layers);
	case HISTORY_ACTION_DELETE_LAYER:
	case HISTORY_ACTION_MERGE_LAYERS: {
		if (step->merge_layers == NULL) {
			return false;
		}
		i32 count = layers - (step->merge_length - step->merge_layers->length);
		return step->merge_start >= 0 && count >= 0 && step->merge_start + count <= layers;
	}
	case HISTORY_ACTION_DUPLICATE_LAYER:
		return history_in(step->layer, layers) &&
		       (!undo || history_in(step->layer + 1 + slot_layer_count_descendants(g_project->_->layers->buffer[step->layer]), layers));
	case HISTORY_ACTION_ORDER_LAYERS:
		return history_in(step->prev_order, layers) && history_in(step->layer, layers) && step->layer_parent < layers;
	case HISTORY_ACTION_NEW_MATERIAL:
	case HISTORY_ACTION_DUPLICATE_MATERIAL:
		return undo ? history_in(step->material, materials) : step->material >= 0 && step->material <= materials;
	case HISTORY_ACTION_DELETE_MATERIAL:
		return undo ? step->material >= 0 && step->material <= materials : history_in(step->material, materials);
	case HISTORY_ACTION_TO_FILL_LAYER:
	case HISTORY_ACTION_TO_FILL_MASK:
		return history_in(step->layer, layers) && (undo || history_in(step->material, materials));
	case HISTORY_ACTION_PAINT:
		return history_in(step->layer, layers) && history_in(step->object, g_project->_->paint_objects->length);
	default:
		return history_in(step->layer, layers);
	}
}

// ---- history_v2: layer pixels from timelines ----
// Undo and redo of a raster step swap its tiles while they are valid. Otherwise the layer goes to the state of its
// timeline at the cursor the step moved it to. Those restores are deferred and coalesced: rapid undos restore each layer
// once, on the next frame or as soon as the layer is needed (painted, another kind of step runs). history_busy() while
// some are pending.

typedef struct history_restore {
	u32 tl;   // Timeline uid
	i32 live; // Position the layer holds now, -1 unknown
	i32 pos;  // Position to restore
} history_restore_t;

static history_restore_t *history_restores          = NULL;
static i32                history_restore_count     = 0;
static i32                history_restore_cap       = 0;
static bool               history_restore_scheduled = false;
static i32                history_restore_runs      = 0; // Restores run so far, for tests

static history_restore_t *history_restore_find(u32 tl) {
	for (i32 i = 0; i < history_restore_count; ++i) {
		if (history_restores[i].tl == tl) {
			return &history_restores[i];
		}
	}
	return NULL;
}

static slot_layer_t *history_layer_of_timeline(u32 tl) {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (g_project->_->layers->buffer[i]->timeline == tl) {
			return g_project->_->layers->buffer[i];
		}
	}
	return NULL;
}

static void history_restore_run(history_restore_t *r) {
	timeline_t   *tl = timeline_find(r->tl);
	slot_layer_t *l  = history_layer_of_timeline(r->tl);
	if (tl == NULL || l == NULL || !timeline_layer_paints(l)) {
		return;
	}
	paint_target_t t;
	timeline_target_live(l, &t);
	hash128_t to   = timeline_state(tl, r->pos);
	hash128_t from = r->live >= 0 ? timeline_state(tl, r->live) : HASH128_NONE;
	bool      ok   = !hash128_is_none(to) ? timeline_tiles_restore_from(to, from, &t) : timeline_materialize(tl, r->pos, &t);
	if (!ok) {
		iron_log("Undo could not restore the pixels of %s", l->name);
	}
	history_restore_runs++;
	mask_stack_layer_changed(l);
	mask_stack_mark_dirty(l);
	g_context->layer_preview_dirty = true;
	g_context->ddirty              = 2;
	g_context->rtdirty             = 1;
}

// Pending restores run now: of l, every one when l is NULL
void history_flush_restores(slot_layer_t *l) {
	for (i32 i = 0; i < history_restore_count; ++i) {
		if (l != NULL && (l->timeline == 0 || history_restores[i].tl != l->timeline)) {
			continue;
		}
		history_restore_t r   = history_restores[i];
		history_restores[i--] = history_restores[--history_restore_count];
		history_restore_run(&r);
	}
}

static void history_restore_job(void *_) {
	history_restore_scheduled = false;
	history_flush_restores(NULL);
	if (g_config->touch_ui) {
		ui_menubar_menu_handle->redraws = 2; // Undo and redo buttons leave their busy state
	}
}

// Layers still to be restored after undo or redo
bool history_busy() {
	return history_restore_count > 0;
}

i32 history_test_restore_runs() {
	return history_restore_runs;
}

i32 history_test_undos() {
	return history_undos;
}

// The cursors step moved go back (undo) or forth, the layers follow at the next flush. full: what the layers hold now is
// no state of their timeline.
static void history_defer_moves(history_step_t *step, bool undo, bool full) {
	for (i32 i = 0; step->tl_moves != NULL && i < step->tl_moves->length; i += 3) {
		u32         uid = (u32)step->tl_moves->buffer[i];
		timeline_t *tl  = timeline_find(uid);
		if (tl == NULL) {
			continue;
		}
		history_restore_t *r = history_restore_find(uid);
		if (r == NULL) {
			if (history_restore_count == history_restore_cap) {
				history_restore_cap = history_restore_cap == 0 ? 8 : history_restore_cap * 2;
				history_restores    = realloc(history_restores, sizeof(history_restore_t) * history_restore_cap);
			}
			r       = &history_restores[history_restore_count++];
			r->tl   = uid;
			r->live = full ? -1 : tl->cursor;
		}
		else if (full) {
			r->live = -1;
		}
		r->pos = step->tl_moves->buffer[i + (undo ? 1 : 2)];
	}
	if (history_restore_count > 0 && !history_restore_scheduled) {
		history_restore_scheduled = true;
		sys_notify_on_next_frame(&history_restore_job, NULL);
	}
}

// Stroke, clear, invert: tiles swapped, or the layer restored from its timeline
static void history_v2_raster(history_step_t *step, bool undo) {
	slot_layer_t *l = g_project->_->layers->buffer[step->layer];
	if (step->action == HISTORY_ACTION_PAINT) {
		context_select_paint_object(g_project->_->paint_objects->buffer[step->object]);
	}
	context_set_layer(l);
	timeline_t *tl = timeline_of(l);
	if (step->undo_tiled && history_tiles_valid(step, l) && (tl == NULL || history_restore_find(tl->uid) == NULL)) {
		history_tiles_swap(step, l);
		mask_stack_mark_dirty(l);
		g_context->layer_preview_dirty = true;
	}
	else {
		history_tiles_release(step); // Its tiles no longer hold the other side of the layer
		history_defer_moves(step, undo, false);
	}
}

static void history_v2_refresh_fill(slot_layer_t *l) {
	if (slot_layer_alive(l)) {
		layers_refresh_fill_layer(l);
	}
}

// Apply mask undo: the layers it was applied to come back from their timelines, the mask from its own
static void history_v2_unapply_mask(history_step_t *step) {
	slot_layer_t_array_t *layers   = g_project->_->layers;
	i32                   mask_pos = step->layer;
	slot_layer_t         *current  = slot_layer_is_mask(layers->buffer[mask_pos]) ? layers->buffer[mask_pos]->parent : layers->buffer[mask_pos];
	timeline_t           *src      = NULL;
	i32                   src_pos  = 0;
	for (i32 i = 0; step->tl_moves != NULL && i < step->tl_moves->length && src == NULL; i += 3) {
		timeline_t *tl     = timeline_find((u32)step->tl_moves->buffer[i]);
		i32         before = step->tl_moves->buffer[i + 1];
		if (tl != NULL && before < tl->count && tl->entries[before].kind == TL_APPLY_MASK) {
			src     = timeline_find(tl->entries[before].src_tl);
			src_pos = tl->entries[before].src_pos;
		}
	}
	history_defer_moves(step, true, false);
	history_flush_restores(NULL);

	layers_new_mask(false, current, mask_pos);
	slot_layer_t *m = g_context->layer;
	history_restore_uid(m, step->layer_uid);
	paint_target_t t;
	timeline_target_live(m, &t);
	if (src == NULL || !timeline_materialize(src, src_pos, &t)) {
		draw_saved_t saved = draw_suspend();
		slot_layer_clear(m, 0xffffffff, NULL, 1.0, layers_default_rough, 0.0);
		draw_resume(saved);
	}
	if (src != NULL) {
		m->timeline = src->uid;
		src->cursor = src_pos;
	}
	history_restore_applied_mask(step, m);
	// Fill again: the mask, the layer it was applied to
	slot_material_t *fills[2] = {slot_material_find_uid(step->fill_uids[0]), slot_material_find_uid(step->fill_uids[1])};
	slot_layer_t    *owners[2] = {m, current};
	for (i32 i = 0; i < 2; ++i) {
		if (step->fill_uids[i] != 0 && fills[i] != NULL && !slot_layer_is_group(owners[i])) {
			owners[i]->fill_material = fills[i];
			sys_notify_on_next_frame(&history_v2_refresh_fill, owners[i]);
		}
	}
	g_context->layers_preview_dirty = true;
	context_set_layer(m);
}

static void history_v2_apply_mask(history_step_t *step) {
	slot_layer_t *m      = g_project->_->layers->buffer[step->layer];
	slot_layer_t *parent = m->parent;
	if (parent->fill_material != NULL && !slot_layer_is_group(parent)) {
		slot_layer_to_paint_layer(parent); // As apply mask did, its pixels come from its timeline
	}
	history_defer_moves(step, false, false);
	history_flush_restores(NULL);
	slot_layer_delete(m);
	context_set_layer(parent);
	g_context->layers_preview_dirty = true;
	make_material_parse_mesh_material();
}

// The steps history_v2 undoes and redoes otherwise than the ring, false for the rest (pending restores run first)
static bool history_v2_step(history_step_t *step, bool undo) {
	slot_layer_t *l = history_in(step->layer, g_project->_->layers->length) ? g_project->_->layers->buffer[step->layer] : NULL;
	switch (step->action) {
	case HISTORY_ACTION_PAINT:
	case HISTORY_ACTION_CLEAR_LAYER:
	case HISTORY_ACTION_INVERT_MASK:
		history_v2_raster(step, undo);
		return true;
	case HISTORY_ACTION_APPLY_MASK:
		history_flush_restores(NULL);
		undo ? history_v2_unapply_mask(step) : history_v2_apply_mask(step);
		return true;
	case HISTORY_ACTION_TO_FILL_LAYER:
	case HISTORY_ACTION_TO_FILL_MASK:
		history_flush_restores(NULL);
		context_set_layer(l);
		if (undo) {
			slot_layer_to_paint_layer(l);
			history_defer_moves(step, true, true); // The layer holds the fill
			history_flush_restores(l);
		}
		else {
			l->fill_material = g_project->_->materials->buffer[step->material];
			sys_notify_on_next_frame(&history_v2_refresh_fill, l);
		}
		mask_stack_mark_dirty(l);
		return true;
	case HISTORY_ACTION_TO_PAINT_LAYER:
	case HISTORY_ACTION_TO_PAINT_MASK:
		history_flush_restores(NULL);
		context_set_layer(l);
		if (undo) {
			l->fill_material = step->material >= 0 && step->material < g_project->_->materials->length ? g_project->_->materials->buffer[step->material] : NULL;
			sys_notify_on_next_frame(&history_v2_refresh_fill, l); // The fill renders again
		}
		else {
			slot_layer_to_paint_layer(l);
			history_defer_moves(step, false, true);
			history_flush_restores(l);
		}
		mask_stack_mark_dirty(l);
		return true;
	default:
		history_flush_restores(NULL);
		return false;
	}
}

void history_undo() {
	history_tiles_finalize();
	timeline_finalize();
	if (history_undos > 0) {
		stroke_refine_end_all(); // Strokes keep the tier they reached
		project_modified = true;
		mask_stack_layer_changed(NULL); // Anchors read the layers as they were
		render_path_base_redraw();
		i32             active = history_steps->length - 1 - history_redos;
		history_step_t *step   = history_steps->buffer[active];

		if (!history_step_valid(step, true)) {
			iron_log("Undo skipped, its layer is gone: %s", step->name);
			history_undo_i = ((history_undo_i - step->undo_ring) % history_ring_size() + history_ring_size()) % history_ring_size(); // Its copies
		}
		else if (history_v2_on && history_v2_step(step, true)) {
		}
		else if (step->action == HISTORY_ACTION_EDIT_NODES) {
			history_swap_canvas(step);
		}
		else if (step->action == HISTORY_ACTION_NEW_LAYER || step->action == HISTORY_ACTION_NEW_BLACK_MASK || step->action == HISTORY_ACTION_NEW_WHITE_MASK ||
		         step->action == HISTORY_ACTION_NEW_FILL_MASK) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			slot_layer_delete(g_context->layer);
			g_context->layer = g_project->_->layers->buffer[step->layer > 0 ? step->layer - 1 : 0];
		}
		else if (step->action == HISTORY_ACTION_SMART_MATERIAL) {
			// The inserted top layer, its masks and children go with it
			g_context->layer = g_project->_->layers->buffer[step->layer];
			slot_layer_delete(g_context->layer);
			i32 below        = (step->layer < g_project->_->layers->length ? step->layer : g_project->_->layers->length) - 1;
			g_context->layer = g_project->_->layers->buffer[below > 0 ? below : 0];
		}
		else if (step->action == HISTORY_ACTION_NEW_GROUP) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			// The layer below is the only layer in the group. Its layer masks are automatically unparented, too.
			g_project->_->layers->buffer[step->layer - 1]->parent = g_context->layer->parent;
			slot_layer_delete(g_context->layer);
			g_context->layer = g_project->_->layers->buffer[step->layer > 0 ? step->layer - 1 : 0];
		}
		else if (step->action == HISTORY_ACTION_DELETE_LAYER) {
			history_swap_merge(step, true);
		}
		else if (step->action == HISTORY_ACTION_CLEAR_LAYER) {
			context_set_layer(g_project->_->layers->buffer[step->layer]); // The cleared layer, not the selected one
			history_undo_i    = history_ring_prev();
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			slot_layer_swap(g_context->layer, lay);
			mask_stack_mark_dirty(g_context->layer);
			g_context->layer_preview_dirty = true;
		}
		else if (step->action == HISTORY_ACTION_DUPLICATE_LAYER) {
			i32 position     = step->layer + 1 + slot_layer_count_descendants(g_project->_->layers->buffer[step->layer]);
			g_context->layer = g_project->_->layers->buffer[position];
			slot_layer_delete(g_context->layer);
		}
		else if (step->action == HISTORY_ACTION_ORDER_LAYERS) {
			history_swap_order(step);
		}
		else if (step->action == HISTORY_ACTION_MERGE_LAYERS) {
			history_swap_merge(step, true);
		}
		else if (step->action == HISTORY_ACTION_APPLY_MASK) {
			// First restore the layer(s)
			i32           mask_pos      = step->layer;
			slot_layer_t *current_layer = NULL;
			// The layer at the old mask position is a mask, i.e. the layer had multiple masks before.
			if (slot_layer_is_mask(g_project->_->layers->buffer[mask_pos])) {
				current_layer = g_project->_->layers->buffer[mask_pos]->parent;
			}
			else if (slot_layer_is_layer(g_project->_->layers->buffer[mask_pos]) || slot_layer_is_group(g_project->_->layers->buffer[mask_pos])) {
				current_layer = g_project->_->layers->buffer[mask_pos];
			}

			slot_layer_t_array_t *layers_to_restore;
			if (slot_layer_is_group(current_layer)) {
				layers_to_restore = slot_layer_get_children(current_layer);
			}
			else {
				layers_to_restore = any_array_create_from_raw(
				    (void *[]){
				        current_layer,
				    },
				    1);
			}
			array_reverse(layers_to_restore);

			for (i32 i = 0; i < layers_to_restore->length; ++i) {
				slot_layer_t *layer = layers_to_restore->buffer[i];
				// Replace the current layer's content with the old one
				g_context->layer        = layer;
				history_undo_i          = history_ring_prev();
				slot_layer_t *old_layer = history_undo_slot(history_undo_i);
				slot_layer_swap(g_context->layer, old_layer);
			}

			// Now restore the applied mask
			history_undo_i     = history_ring_prev();
			slot_layer_t *mask = history_undo_slot(history_undo_i);
			layers_new_mask(false, current_layer, mask_pos);
			history_restore_uid(g_context->layer, step->layer_uid);
			slot_layer_swap(g_context->layer, mask);
			history_restore_applied_mask(step, g_context->layer);
			g_context->layers_preview_dirty = true;
			context_set_layer(g_context->layer);
		}
		else if (step->action == HISTORY_ACTION_INVERT_MASK) {
			sys_notify_on_next_frame(&history_undo_invert_mask, step);
		}
		else if (step->action == HISTORY_ACTION_TO_FILL_LAYER || step->action == HISTORY_ACTION_TO_FILL_MASK) {
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_to_paint_layer(g_context->layer);
			history_undo_i    = history_ring_prev();
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			slot_layer_swap(g_context->layer, lay);
			mask_stack_mark_dirty(g_context->layer);
		}
		else if (step->action == HISTORY_ACTION_TO_PAINT_LAYER || step->action == HISTORY_ACTION_TO_PAINT_MASK) {
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			history_undo_i    = history_ring_prev();
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			slot_layer_swap(g_context->layer, lay);
			mask_stack_mark_dirty(g_context->layer);
			g_context->layer->fill_material = step->material >= 0 && step->material < g_project->_->materials->length ? g_project->_->materials->buffer[step->material] : NULL;
		}
		else if (step->action == HISTORY_ACTION_LAYER_OPACITY || step->action == HISTORY_ACTION_LAYER_BLENDING ||
		         step->action == HISTORY_ACTION_LAYER_VISIBLE || step->action == HISTORY_ACTION_LAYER_NAME || step->action == HISTORY_ACTION_LAYER_OBJECT ||
		         step->action == HISTORY_ACTION_LAYER_SCALE || step->action == HISTORY_ACTION_LAYER_ANGLE || step->action == HISTORY_ACTION_LAYER_UV_TYPE ||
		         step->action == HISTORY_ACTION_LAYER_FILL_MATERIAL || step->action == HISTORY_ACTION_MASK_HEIGHT_BLEND ||
		         step->action == HISTORY_ACTION_MASK_STACK) {
			history_swap_layer_props(step);
		}
		else if (step->action == HISTORY_ACTION_RESOLUTION) {
			layers_resolution_apply(step->resolution[0], step->resolution[1]);
		}
		else if (step->action == HISTORY_ACTION_NEW_SWATCH || step->action == HISTORY_ACTION_DUPLICATE_SWATCH) {
			history_delete_swatch_slot(step);
		}
		else if (step->action == HISTORY_ACTION_DELETE_SWATCH) {
			history_new_swatch_slot(step);
		}
		else if (step->action == HISTORY_ACTION_REPLACE_SWATCHES) {
			history_swap_swatches(step);
		}
		else if (step->action == HISTORY_ACTION_EDIT_SWATCH) {
			history_swap_swatch(step);
		}
		else if (step->action == HISTORY_ACTION_DELETE_NODE_GROUP) {
			node_group_t *ng = ALLOC_INIT(node_group_t, {.canvas = NULL, .nodes = ui_nodes_create()});
			array_insert(history_groups(step), step->canvas_group, ng);
			history_swap_canvas(step);
		}
		else if (step->action == HISTORY_ACTION_NEW_MATERIAL) {
			g_context->material = g_project->_->materials->buffer[step->material];
			step->canvas        = g_context->material->canvas;
			slot_material_delete(g_context->material);
		}
		else if (step->action == HISTORY_ACTION_DELETE_MATERIAL) {
			g_context->material = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
			history_restore_material_uid(g_context->material, step->material_uid);
			array_insert(g_project->_->materials, step->material, g_context->material);
			g_context->material->canvas = step->canvas;
			ui_nodes_canvas_changed();
			ui_nodes_hwnd->redraws = 2;
		}
		else if (step->action == HISTORY_ACTION_DUPLICATE_MATERIAL) {
			g_context->material = g_project->_->materials->buffer[step->material];
			step->canvas        = g_context->material->canvas;
			slot_material_delete(g_context->material);
		}
		else if (step->undo_tiled) { // Paint operation, from its tiles
			context_select_paint_object(g_project->_->paint_objects->buffer[step->object]);
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			history_tiles_swap(step, g_context->layer);
			mask_stack_mark_dirty(g_context->layer);
			g_context->layer_preview_dirty = true;
		}
		else { // Paint operation
			history_undo_i    = history_ring_prev();
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			context_select_paint_object(g_project->_->paint_objects->buffer[step->object]);
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_swap(g_context->layer, lay);
			mask_stack_mark_dirty(g_context->layer);
			g_context->layer_preview_dirty = true;
		}

		if (history_step_valid_last) {
			timeline_step_apply(step, true);
		}
		history_undos--;
		history_redos++;
		if (step->with_prev) {
			history_undo();
		}
		g_context->rtdirty = 1;
		g_context->ddirty  = 2;

		ui_dock_redraw(TAB_AREA_SIDEBAR0);
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
		if (ui_view2d_show) {
			ui_view2d_hwnd->redraws = 2;
		}

		if (g_config->touch_ui) {
			// Refresh undo & redo buttons
			ui_menubar_menu_handle->redraws = 2;
		}
	}
	base_redraw_status();
}

static void history_redo_invert_mask(history_step_t *step) {
	if (!history_in(step->layer, g_project->_->layers->length)) {
		return;
	}
	g_context->layer = g_project->_->layers->buffer[step->layer];
	slot_layer_invert_mask(g_context->layer);
	mask_stack_mark_dirty(g_context->layer);
}

static void history_redo_apply_mask(void *_) {
	slot_layer_apply_mask(g_context->layer);
	context_set_layer(g_context->layer);
	g_context->layers_preview_dirty = true;
}

slot_layer_t *history_undo_slot(i32 i) {
	slot_layer_t *l = history_undo_layers->buffer[i];
	slot_layer_alloc_textures(l);
	return l;
}

void history_copy_to_undo(i32 from_id, i32 to_id, bool is_mask) {
	char *to_id_s   = i32_to_string(to_id);
	char *from_id_s = i32_to_string(from_id);
	if (history_undo_layers != NULL && to_id < history_undo_layers->length) {
		history_undo_slot(to_id);
	}
	if (is_mask) {
		render_path_set_target(string_tmp("texpaint_undo%s", to_id_s), NULL, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0);
		render_path_bind_target(string("texpaint%s", from_id_s), "tex");
		render_path_draw_shader("Scene/copy_pass/copy_pass");
	}
	else {
		string_array_t *additional = any_array_create_from_raw(
		    (void *[]){
		        string("texpaint_nor_undo%s", to_id_s),
		        string("texpaint_pack_undo%s", to_id_s),
		    },
		    2);
		render_path_set_target(string_tmp("texpaint_undo%s", to_id_s), additional, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0); // Overwritten whole
		render_path_bind_target(string("texpaint%s", from_id_s), "tex0");
		render_path_bind_target(string("texpaint_nor%s", from_id_s), "tex1");
		render_path_bind_target(string("texpaint_pack%s", from_id_s), "tex2");

		gpu_texture_format_t format = layers_format(base_bits);

		char *pipe = format == GPU_TEXTURE_FORMAT_RGBA32   ? "copy_mrt3_pass"
		             : format == GPU_TEXTURE_FORMAT_RGBA64 ? "copy_mrt3RGBA64_pass"
		                                                   : "copy_mrt3RGBA128_pass";
		render_path_draw_shader(string_tmp("Scene/copy_mrt3_pass/%s", pipe));
	}
	history_undo_i = history_ring_next();
	history_ring_pending++;
}

void history_copy_merging_layers2(slot_layer_t_array_t *layers) {
	for (i32 i = 0; i < layers->length; ++i) {
		slot_layer_t *layer = layers->buffer[i];
		history_copy_to_undo(layer->id, history_undo_i, slot_layer_is_mask(layer));
	}
}

static void history_redo_duplicate_layer(void *_) {
	layers_duplicate_layer(g_context->layer);
}

// A new layer or mask redone as it was created: cleared and the image drawn now, steps redone after it in the same frame
// paint on it
static void history_redo_new_content(history_step_t *step, slot_layer_t *l) {
	if (l->texpaint == NULL) {
		return; // Group
	}
	gpu_texture_t *image = NULL;
	for (i32 i = 0; step->image_file != NULL && i < g_project->_->assets->length; ++i) {
		if (string_equals(g_project->_->assets->buffer[i]->file, step->image_file)) {
			image = project_get_image(g_project->_->assets->buffer[i]);
			break;
		}
	}
	draw_saved_t saved = draw_suspend();
	slot_layer_clear(l, step->action == HISTORY_ACTION_NEW_WHITE_MASK ? 0xffffffff : 0x00000000, image, 1.0, layers_default_rough, 0.0);
	draw_resume(saved);
}

static void history_redo_new_fill_mask(slot_layer_t *l) {
	if (slot_layer_alive(l)) {
		slot_layer_to_fill_layer(l);
	}
}

void history_swap_active() {
	history_tiles_finalize(); // The tiles of the last stroke, before the layer changes
	slot_layer_t *undo_layer = history_undo_slot(history_undo_i);
	slot_layer_swap(undo_layer, g_context->layer);
	history_undo_i = history_ring_next();
	history_ring_pending++;
}

void history_redo() {
	history_tiles_finalize();
	timeline_finalize();
	if (history_redos > 0) {
		stroke_refine_end_all();
		project_modified = true;
		mask_stack_layer_changed(NULL);
		render_path_base_redraw();
		i32             active = history_steps->length - history_redos;
		history_step_t *step   = history_steps->buffer[active];

		if (!history_step_valid(step, false)) {
			iron_log("Redo skipped, its layer is gone: %s", step->name);
			history_undo_i = (history_undo_i + step->undo_ring) % history_ring_size();
		}
		else if (history_v2_on && history_v2_step(step, false)) {
		}
		else if (step->action == HISTORY_ACTION_EDIT_NODES) {
			history_swap_canvas(step);
		}
		else if (step->action == HISTORY_ACTION_NEW_LAYER || step->action == HISTORY_ACTION_NEW_BLACK_MASK || step->action == HISTORY_ACTION_NEW_WHITE_MASK ||
		         step->action == HISTORY_ACTION_NEW_FILL_MASK) {
			slot_layer_t *parent = step->layer_parent > 0 ? g_project->_->layers->buffer[step->layer_parent - 1] : NULL;
			slot_layer_t *l      = slot_layer_create("", step->layer_type, parent);
			history_restore_uid(l, step->layer_uid);
			array_insert(g_project->_->layers, step->layer, l);
			history_redo_new_content(step, l);
			if (step->action == HISTORY_ACTION_NEW_FILL_MASK) {
				g_context->material = g_project->_->materials->buffer[step->material];
				sys_notify_on_next_frame(&history_redo_new_fill_mask, l);
			}
			g_context->layer_preview_dirty = true;
			context_set_layer(l);
		}
		else if (step->action == HISTORY_ACTION_SMART_MATERIAL) {
			sys_notify_on_next_frame(&history_redo_smart_material, step->layer_name);
		}
		else if (step->action == HISTORY_ACTION_NEW_GROUP) {
			slot_layer_t *l     = g_project->_->layers->buffer[step->layer - 1];
			slot_layer_t *group = layers_new_group();
			history_restore_uid(group, step->layer_uid);
			array_remove(g_project->_->layers, group);
			array_insert(g_project->_->layers, step->layer, group);
			group->parent = l->parent;
			l->parent     = group;
			context_set_layer(group);
		}
		else if (step->action == HISTORY_ACTION_DELETE_LAYER) {
			history_swap_merge(step, false);
		}
		else if (step->action == HISTORY_ACTION_CLEAR_LAYER) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			history_swap_active();
			slot_layer_clear(g_context->layer, slot_layer_is_mask(g_context->layer) ? 0xffffffff : 0x00000000, NULL, 1.0, layers_default_rough, 0.0); // As the clear
			mask_stack_mark_dirty(g_context->layer);
			g_context->layer_preview_dirty = true;
		}
		else if (step->action == HISTORY_ACTION_DUPLICATE_LAYER) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			sys_notify_on_next_frame(&history_redo_duplicate_layer, NULL);
		}
		else if (step->action == HISTORY_ACTION_ORDER_LAYERS) {
			history_swap_order(step);
		}
		else if (step->action == HISTORY_ACTION_MERGE_LAYERS) {
			history_swap_merge(step, false);
		}
		else if (step->action == HISTORY_ACTION_APPLY_MASK) {
			g_context->layer = g_project->_->layers->buffer[step->layer];
			if (slot_layer_is_group_mask(g_context->layer)) {
				slot_layer_t         *group  = g_context->layer->parent;
				slot_layer_t_array_t *layers = slot_layer_get_children(group);
				array_insert(layers, 0, g_context->layer);
				history_copy_merging_layers2(layers);
			}
			else {
				slot_layer_t_array_t *layers = any_array_create_from_raw(
				    (void *[]){
				        g_context->layer,
				        g_context->layer->parent,
				    },
				    2);
				history_copy_merging_layers2(layers);
			}

			sys_notify_on_next_frame(&history_redo_apply_mask, NULL);
		}
		else if (step->action == HISTORY_ACTION_INVERT_MASK) {
			sys_notify_on_next_frame(&history_redo_invert_mask, step);
		}
		else if (step->action == HISTORY_ACTION_TO_FILL_LAYER || step->action == HISTORY_ACTION_TO_FILL_MASK) {
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			slot_layer_swap(g_context->layer, lay);
			mask_stack_mark_dirty(g_context->layer);
			g_context->layer->fill_material = g_project->_->materials->buffer[step->material];
			history_undo_i                  = history_ring_next();
		}
		else if (step->action == HISTORY_ACTION_TO_PAINT_LAYER || step->action == HISTORY_ACTION_TO_PAINT_MASK) {
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_to_paint_layer(g_context->layer);
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			slot_layer_swap(g_context->layer, lay);
			history_undo_i = history_ring_next();
		}
		else if (step->action == HISTORY_ACTION_LAYER_OPACITY || step->action == HISTORY_ACTION_LAYER_BLENDING ||
		         step->action == HISTORY_ACTION_LAYER_VISIBLE || step->action == HISTORY_ACTION_LAYER_NAME || step->action == HISTORY_ACTION_LAYER_OBJECT ||
		         step->action == HISTORY_ACTION_LAYER_SCALE || step->action == HISTORY_ACTION_LAYER_ANGLE || step->action == HISTORY_ACTION_LAYER_UV_TYPE ||
		         step->action == HISTORY_ACTION_LAYER_FILL_MATERIAL || step->action == HISTORY_ACTION_MASK_HEIGHT_BLEND ||
		         step->action == HISTORY_ACTION_MASK_STACK) {
			history_swap_layer_props(step);
		}
		else if (step->action == HISTORY_ACTION_RESOLUTION) {
			layers_resolution_apply(step->resolution[2], step->resolution[3]);
		}
		else if (step->action == HISTORY_ACTION_NEW_SWATCH || step->action == HISTORY_ACTION_DUPLICATE_SWATCH) {
			history_new_swatch_slot(step);
		}
		else if (step->action == HISTORY_ACTION_DELETE_SWATCH) {
			history_delete_swatch_slot(step);
		}
		else if (step->action == HISTORY_ACTION_REPLACE_SWATCHES) {
			history_swap_swatches(step);
		}
		else if (step->action == HISTORY_ACTION_EDIT_SWATCH) {
			history_swap_swatch(step);
		}
		else if (step->action == HISTORY_ACTION_DELETE_NODE_GROUP) {
			history_swap_canvas(step);
			array_remove(history_groups(step), history_groups(step)->buffer[step->canvas_group]);
		}
		else if (step->action == HISTORY_ACTION_NEW_MATERIAL) {
			g_context->material = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
			history_restore_material_uid(g_context->material, step->material_uid);
			array_insert(g_project->_->materials, step->material, g_context->material);
			g_context->material->canvas = step->canvas;
			ui_nodes_canvas_changed();
			ui_nodes_hwnd->redraws = 2;
		}
		else if (step->action == HISTORY_ACTION_DELETE_MATERIAL) {
			g_context->material = g_project->_->materials->buffer[step->material];
			step->canvas        = g_context->material->canvas;
			slot_material_delete(g_context->material);
		}
		else if (step->action == HISTORY_ACTION_DUPLICATE_MATERIAL) {
			g_context->material = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
			history_restore_material_uid(g_context->material, step->material_uid);
			array_insert(g_project->_->materials, step->material, g_context->material);
			g_context->material->canvas = step->canvas;
			ui_nodes_canvas_changed();
			ui_nodes_hwnd->redraws = 2;
		}
		else if (step->undo_tiled) { // Paint operation, from its tiles
			context_select_paint_object(g_project->_->paint_objects->buffer[step->object]);
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			history_tiles_swap(step, g_context->layer);
			mask_stack_mark_dirty(g_context->layer);
			g_context->layer_preview_dirty = true;
		}
		else { // Paint operation
			slot_layer_t *lay = history_undo_slot(history_undo_i);
			context_select_paint_object(g_project->_->paint_objects->buffer[step->object]);
			context_set_layer(g_project->_->layers->buffer[step->layer]);
			slot_layer_swap(g_context->layer, lay);
			mask_stack_mark_dirty(g_context->layer);
			g_context->layer_preview_dirty = true;
			history_undo_i                 = history_ring_next();
		}

		if (history_step_valid_last) {
			timeline_step_apply(step, false);
		}
		history_undos++;
		history_redos--;
		if (history_redos > 0 && history_steps->buffer[history_steps->length - history_redos]->with_prev) {
			history_redo();
		}
		g_context->rtdirty = 1;
		g_context->ddirty  = 2;

		ui_dock_redraw(TAB_AREA_SIDEBAR0);
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
		if (ui_view2d_show) {
			ui_view2d_hwnd->redraws = 2;
		}

		if (g_config->touch_ui) {
			// Refresh undo & redo buttons
			ui_menubar_menu_handle->redraws = 2;
		}
	}
	base_redraw_status();
}

// Low memory, not in a stroke: snapshots go. history_v2 keeps the tiles of the last 4 steps only, older ones undo from
// their timelines, and the blob store drops what it can without reading anything back.
void history_on_memory_warning() {
	history_tiles_trim();
	if (history_v2_on) {
		history_tiles_keep_newest(4);
		hstore_trim(true);
	}
}

void history_reset() {
	project_modified = false;
	layers_resolution_reset(); // The states resolution changes kept
	if (history_steps != NULL) {
		for (i32 i = 0; i < history_steps->length; ++i) {
			history_step_free(history_steps->buffer[i]);
		}
		array_free(history_steps);
		free(history_steps);
	}
	history_steps = any_array_create_from_raw(
	    (void *[]){
	        ALLOC_INIT(history_step_t,
	                   {.name = tr("New"), .layer = 0, .layer_type = LAYER_SLOT_TYPE_LAYER, .layer_parent = -1, .object = 0, .material = 0, .brush = 0}),
	    },
	    1);
	history_undos  = 0;
	history_redos  = 0;
	history_undo_i = 0;
	history_ring_pending = 0;
#ifdef IRON_METAL
	// Steps of both kinds never mix. Env HISTORY_V2=1 turns it on for test runs, the saved config stays as it is.
	char *env     = getenv("HISTORY_V2");
	history_v2_on = g_config->history_v2 || (env != NULL && atoi(env) != 0);
#else
	history_v2_on = false; // Tile transfers are Metal only
#endif
	history_restore_count     = 0;
	history_restore_scheduled = false; // A job still queued finds nothing
	history_tiles_reset();
	timeline_reset_all();
	stroke_refine_reset();
	material_cache_reset(); // A new project: every material cache goes
}

// The ring holds n whole layer copies from now on, the preference stays as it is. Steps keeping copies go: their slots
// moved. The redo branch goes first, then the oldest steps up to the last one keeping copies.
void history_ring_set_size(i32 n) {
	if (history_undo_layers == NULL || n < 1 || n == history_undo_layers->length) {
		return;
	}
	while (history_undo_layers->length < n) {
		any_array_push(history_undo_layers, slot_layer_create_undo(string("_undo%s", i32_to_string(history_undo_layers->length))));
	}
	while (history_undo_layers->length > n) {
		sys_notify_on_next_frame(&slot_layer_unload, array_pop(history_undo_layers));
	}
	if (history_steps != NULL && history_ring_used() > 0) {
		history_tiles_finalize();
		stroke_refine_reset(); // Records point at steps
		for (; history_redos > 0; --history_redos) {
			history_step_free(array_pop(history_steps));
		}
		while (history_ring_used() > 0) {
			history_drop_oldest();
		}
	}
	history_undo_i       = 0;
	history_ring_pending = 0;
}

history_step_t *history_push(history_action_t action) {
	if (history_v2_on) { // A stroke waits only for its own layer
		history_flush_restores(action == HISTORY_ACTION_PAINT ? g_context->layer : NULL);
	}
	history_tiles_finalize(); // The last stroke is over
	timeline_finalize();
	project_modified = true;
	render_path_base_redraw();
	char *name = history_action_to_string(action);
#ifdef IRON_MACOS
	char *filename = string_equals(g_project->_->filepath, "") ? ui_files_filename
	                                                           : substring(g_project->_->filepath, string_last_index_of(g_project->_->filepath, PATH_SEP) + 1,
	                                                                       string_length(g_project->_->filepath) - 4);
	sys_title_set(string("%s* - %s", filename, manifest_title));
#endif

	if (g_config->touch_ui) {
		// Refresh undo & redo buttons
		ui_menubar_menu_handle->redraws = 2;
	}

	history_undos++;
	if (history_redos > 0) {
		for (i32 i = 0; i < history_redos; ++i) {
			history_step_free(array_pop(history_steps));
		}
		history_redos = 0;
		timeline_truncate_all(); // Their entries
	}

	i32 opos = array_index_of(g_project->_->paint_objects, g_context->paint_object);
	i32 lpos = array_index_of(g_project->_->layers, g_context->layer);
	i32 mpos = array_index_of(g_project->_->materials, g_context->material);
	i32 bpos = array_index_of(g_project->_->brushes, g_context->brush);

	history_step_t *step =
	    ALLOC_INIT(history_step_t, {.name           = name,
	                                .action         = action,
	                                .layer          = lpos,
	                                .layer_type     = slot_layer_is_mask(g_context->layer)    ? LAYER_SLOT_TYPE_MASK
	                                                  : slot_layer_is_group(g_context->layer) ? LAYER_SLOT_TYPE_GROUP
	                                                                                          : LAYER_SLOT_TYPE_LAYER,
	                                .layer_parent   = g_context->layer->parent == NULL ? -1 : array_index_of(g_project->_->layers, g_context->layer->parent),
	                                .object         = opos,
	                                .material       = mpos,
	                                .brush          = bpos,
	                                .layer_opacity  = g_context->layer->mask_opacity,
	                                .layer_object   = g_context->layer->object_mask,
	                                .layer_blending = g_context->layer->blending});

	step->serial       = history_serial_next++;
	step->layer_uid    = g_context->layer->uid;
	step->parent_uid   = g_context->layer->parent != NULL ? g_context->layer->parent->uid : 0;
	step->material_uid = g_context->material != NULL ? g_context->material->uid : 0;

	step->undo_ring      = history_ring_pending;
	history_ring_pending = 0;
	any_array_push(history_steps, step);
	stroke_refine_history_pushed(action); // Layers changed otherwise than by strokes: their records end

	// Whole layer copies are a ring of undo_steps layers, a step whose copy would be overwritten goes first
	while (history_steps->length > history_max_steps() + 1 || (history_steps->length > 2 && history_ring_used() > history_ring_size())) {
		history_drop_oldest();
	}
	return history_steps->buffer[history_steps->length - 1];
}

static void history_redo_smart_material(char *name) {
	smart_materials_insert(name);
}
