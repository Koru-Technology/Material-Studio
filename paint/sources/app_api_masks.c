// app_api_masks.h, #included by main.c after app_api_layers.c (it reuses its layer helpers)

#include "app_api_masks.h"

// Mask effects. The stack is listed bottom first in slot_layer_t.mask_effects, the API counts from the top.

_Static_assert((i32)APP_MASK_EFFECT_DIRECTIONAL == (i32)MASK_EFFECT_DIRECTIONAL && (i32)APP_MASK_EFFECT_ANCHOR == (i32)MASK_EFFECT_ANCHOR &&
                   APP_MASK_EFFECT_ANCHOR + 1 == MASK_EFFECT_COUNT && APP_MASK_EFFECT_PARAMS == MASK_EFFECT_PARAMS &&
                   APP_MASK_NOISE_FIRST == MASK_EFFECT_NOISE_SCALE && APP_MASK_ID_SELECTION_MAX == MASK_ID_SELECTION_MAX &&
                   (i32)APP_MASK_PARAM_LAYER == (i32)MASK_PARAM_LAYER,
               "app_api_layers.h mirrors enums.h");

static slot_layer_t *app_mask_by_id(int32_t id) {
	slot_layer_t *m = app_layer_by_id(id);
	return m != NULL && slot_layer_is_mask(m) ? m : NULL;
}

// The stack index of effect index of m, -1 when out of range
static i32 app_mask_stack_index(slot_layer_t *m, int32_t index) {
	i32 n = m != NULL ? mask_stack_count(m) : 0;
	return index >= 0 && index < n ? n - 1 - index : -1;
}

// Before an edit of m: selects it (history steps are of the current layer) and keeps its effects for undo
static void app_mask_history(slot_layer_t *m) {
	context_set_layer(m);
	history_mask_stack();
}

static bool app_mask_done(bool ok) {
	if (ok) {
		ui_dock_redraw(TAB_AREA_SIDEBAR0);
		iron_delay_idle_sleep();
	}
	return ok;
}

int32_t app_mask_effect_count(int32_t id) {
	slot_layer_t *m = app_mask_by_id(id);
	return m != NULL ? mask_stack_count(m) : 0;
}

bool app_mask_effect(int32_t id, int32_t index, app_mask_effect_t *out) {
	slot_layer_t *m = app_mask_by_id(id);
	i32           i = app_mask_stack_index(m, index);
	if (i < 0) {
		return false;
	}
	mask_effect_t e = mask_stack_get(m, i);
	*out            = (app_mask_effect_t){
	    .type            = e.type,
	    .visible         = e.visible,
	    .opacity         = e.opacity,
	    .blending        = e.blending,
	    .is_paint        = e.type == MASK_EFFECT_PAINT,
	    .is_filter       = mask_effect_is_filter(e.type),
	    .is_generator    = mask_effect_is_generator(e.type),
	    .needs_ao        = e.type == MASK_EFFECT_DIRT && mesh_maps_get_asset(MESH_MAP_AO) == NULL,
	    .needs_thickness = e.type == MASK_EFFECT_THICKNESS && mesh_maps_get_asset(MESH_MAP_THICKNESS) == NULL,
	    .needs_id_map    = e.type == MASK_EFFECT_ID_SELECTION && project_id_map_asset() == NULL,
	};
	for (i32 j = 0; j < MASK_EFFECT_PARAMS; ++j) {
		out->params[j] = e.params[j];
	}
	return true;
}

int32_t app_mask_effect_type_count(void) {
	return MASK_EFFECT_COUNT;
}

const char *app_mask_effect_type_name(int32_t type) {
	return type >= 0 && type < MASK_EFFECT_COUNT ? tr(mask_effect_name(type)) : NULL;
}

bool app_mask_effect_type_is_filter(int32_t type) {
	return type >= 0 && type < MASK_EFFECT_COUNT && mask_effect_is_filter(type);
}

bool app_mask_effect_type_is_generator(int32_t type) {
	return type >= 0 && type < MASK_EFFECT_COUNT && mask_effect_is_generator(type);
}

int32_t app_mask_effect_param_count(int32_t type) {
	i32 count = 0;
	mask_effect_params(type, &count);
	return count;
}

bool app_mask_effect_param(int32_t type, int32_t p, app_mask_param_t *out) {
	i32                  count = 0;
	mask_effect_param_t *ps    = mask_effect_params(type, &count);
	if (p < 0 || p >= count) {
		return false;
	}
	*out = (app_mask_param_t){
	    .name    = app_str(tr(ps[p].name)),
	    .index   = ps[p].index,
	    .kind    = ps[p].kind,
	    .min     = ps[p].min,
	    .max     = ps[p].max,
	    .def     = ps[p].def,
	    .options = ps[p].options != NULL ? ps[p].options : "",
	};
	return true;
}

int32_t app_mask_blending_count(void) {
	return MASK_BLEND_COUNT;
}

const char *app_mask_blending_name(int32_t blending) {
	return blending >= 0 && blending < MASK_BLEND_COUNT ? tr(mask_blend_name(blending)) : NULL;
}

int32_t app_mask_add_effect(int32_t id, int32_t type) {
	slot_layer_t *m = app_mask_by_id(id);
	if (m == NULL || type < 0 || type >= MASK_EFFECT_COUNT || (type == MASK_EFFECT_PAINT && mask_stack_paint_index(m) >= 0)) {
		return -1;
	}
	app_mask_history(m);
	i32 at = mask_stack_add(m, type, -1); // On top: the native list shows no selected effect
	if (at < 0) {
		return -1;
	}
	g_context->mask_effect = at;
	m->show_panel          = true; // The engine's layers tab lists the effects under the mask
	if (type == MASK_EFFECT_ID_SELECTION) {
		ui_id_mask_begin_effect(m, at); // Its parts are picked right away
	}
	app_mask_done(true);
	return mask_stack_count(m) - 1 - at;
}

bool app_mask_remove_effect(int32_t id, int32_t index) {
	slot_layer_t *m = app_mask_by_id(id);
	i32           i = app_mask_stack_index(m, index);
	if (i < 0 || mask_stack_get(m, i).type == MASK_EFFECT_PAINT) {
		return false;
	}
	app_mask_history(m);
	mask_stack_remove(m, i);
	g_context->mask_effect = -1;
	return app_mask_done(true);
}

bool app_mask_move_effect(int32_t id, int32_t from, int32_t to) {
	slot_layer_t *m = app_mask_by_id(id);
	i32           f = app_mask_stack_index(m, from);
	i32           t = app_mask_stack_index(m, to);
	if (f < 0 || t < 0 || f == t) {
		return false;
	}
	app_mask_history(m);
	mask_stack_move(m, f, t);
	g_context->mask_effect = t;
	return app_mask_done(true);
}

// Edits effect index of m through edit, one undo step. Refused when edit leaves it as it was.
static bool app_mask_edit(int32_t id, int32_t index, bool (*edit)(mask_effect_t *e, i32 a), i32 a) {
	slot_layer_t *m = app_mask_by_id(id);
	i32           i = app_mask_stack_index(m, index);
	if (i < 0) {
		return false;
	}
	mask_effect_t e = mask_stack_get(m, i);
	if (!edit(&e, a)) {
		return false;
	}
	app_mask_history(m);
	mask_stack_set(m, i, e);
	return app_mask_done(true);
}

static bool app_mask_edit_visible(mask_effect_t *e, i32 on) {
	if (e->visible == (bool)on) {
		return false;
	}
	e->visible = on;
	return true;
}

static bool app_mask_edit_blending(mask_effect_t *e, i32 blending) {
	if (blending < 0 || blending >= MASK_BLEND_COUNT || e->blending == blending || mask_effect_is_filter(e->type)) {
		return false;
	}
	e->blending = blending;
	return true;
}

static bool app_mask_edit_reset(mask_effect_t *e, i32 _) {
	mask_effect_t d       = mask_effect_default(e->type);
	bool          changed = false;
	for (i32 j = 0; j < MASK_EFFECT_PARAMS; ++j) {
		changed      = changed || e->params[j] != d.params[j];
		e->params[j] = d.params[j];
	}
	return changed;
}

bool app_mask_set_effect_visible(int32_t id, int32_t index, bool on) {
	return app_mask_edit(id, index, app_mask_edit_visible, on);
}

bool app_mask_set_effect_blending(int32_t id, int32_t index, int32_t blending) {
	return app_mask_edit(id, index, app_mask_edit_blending, blending);
}

bool app_mask_reset_effect(int32_t id, int32_t index) {
	return app_mask_edit(id, index, app_mask_edit_reset, 0);
}

bool app_mask_effect_pick_ids(int32_t id, int32_t index) {
	slot_layer_t *m = app_mask_by_id(id);
	i32           i = app_mask_stack_index(m, index);
	if (i < 0 || mask_stack_get(m, i).type != MASK_EFFECT_ID_SELECTION) {
		return false;
	}
	ui_id_mask_begin_effect(m, i);
	iron_delay_idle_sleep();
	return true;
}

// Anchors: an Anchor effect reads another paint or fill layer, not the layer the mask is of
static bool app_mask_anchors(slot_layer_t *m, slot_layer_t *l) {
	return l != m->parent && slot_layer_is_layer(l) && !slot_layer_is_filter(l);
}

// Layer i of the anchor list of mask id, top first as the layers panel
static slot_layer_t *app_mask_anchor_layer(int32_t id, int32_t i) {
	slot_layer_t *m = app_mask_by_id(id);
	if (m == NULL || i < 0) {
		return NULL;
	}
	for (i32 k = g_project->_->layers->length - 1; k >= 0; --k) {
		slot_layer_t *l = g_project->_->layers->buffer[k];
		if (app_mask_anchors(m, l) && i-- == 0) {
			return l;
		}
	}
	return NULL;
}

int32_t app_mask_anchor_layer_count(int32_t id) {
	slot_layer_t *m = app_mask_by_id(id);
	i32           n = 0;
	for (i32 k = 0; m != NULL && k < g_project->_->layers->length; ++k) {
		n += app_mask_anchors(m, g_project->_->layers->buffer[k]);
	}
	return n;
}

int32_t app_mask_anchor_layer_id(int32_t id, int32_t i) {
	slot_layer_t *l = app_mask_anchor_layer(id, i);
	return l != NULL ? l->id : -1;
}

const char *app_mask_anchor_layer_name(int32_t id, int32_t i) {
	slot_layer_t *l = app_mask_anchor_layer(id, i);
	return app_str(l != NULL ? l->name : NULL);
}

// Slider drags

// Field -1 is the opacity, the others are settings of the effect's type
static bool app_mask_valid_field(i32 type, i32 field) {
	return field == -1 || (field >= 0 && field < MASK_EFFECT_PARAMS && mask_effect_param_at(type, field) != NULL);
}

void app_mask_effect_value_begin(int32_t id, int32_t index, int32_t field) {
	slot_layer_t *m = app_mask_by_id(id);
	i32           i = app_mask_stack_index(m, index);
	if (i < 0 || !app_mask_valid_field(mask_stack_get(m, i).type, field)) {
		return;
	}
	app_mask_history(m);
	app_drag_begin(APP_DRAG_MASK_VALUE, id, i, field);
}

bool app_mask_set_effect_value(int32_t id, int32_t index, int32_t field, float v) {
	slot_layer_t *m = app_mask_by_id(id);
	i32           i = app_mask_stack_index(m, index);
	if (i < 0) {
		return false;
	}
	mask_effect_t e = mask_stack_get(m, i);
	if (!app_mask_valid_field(e.type, field)) {
		return false;
	}
	f32 *f    = field < 0 ? &e.opacity : &e.params[field];
	f32  c    = field < 0 ? math_max(0.0, math_min(1.0, v)) : mask_effect_param_clamp(e.type, field, v);
	if (field >= 0 && mask_effect_param_at(e.type, field)->kind == MASK_PARAM_LAYER && c >= 0.0) {
		slot_layer_t *l = app_layer_by_id((i32)c);
		if (l == NULL || !app_mask_anchors(m, l)) {
			return false; // Not a layer it can reference
		}
	}
	if (!app_dragging(APP_DRAG_MASK_VALUE, id, i, field)) { // A change without a drag: its own undo step
		if (*f == c) {
			return false;
		}
		app_mask_history(m);
	}
	*f = c;
	mask_stack_set(m, i, e);
	iron_delay_idle_sleep();
	return true;
}

// An effect from a layer's menu: into its mask, a black mask made first when it has none (one undo step)
int32_t app_layer_add_effect(int32_t id, int32_t type) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || slot_layer_is_filter(l) || type <= MASK_EFFECT_PAINT || type >= MASK_EFFECT_COUNT) {
		return -1;
	}
	slot_layer_t *m       = slot_layer_is_mask(l) ? l : app_layer_first_mask(l);
	bool          created = false;
	if (m == NULL) {
		m = app_layer_new_mask(l, APP_LAYER_NEW_BLACK_MASK);
		if (m == NULL) {
			return -1;
		}
		created = true;
	}
	if (app_mask_add_effect(m->id, type) < 0) {
		return -1;
	}
	if (created) {
		history_steps->buffer[history_steps->length - 1]->with_prev = true; // Undone with the new mask
	}
	context_set_layer(m);
	return m->id;
}
