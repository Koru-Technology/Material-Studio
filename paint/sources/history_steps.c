#include "global.h"

// The steps pushed before an action, with what its undo needs

void history_edit_nodes(ui_node_canvas_t *canvas, i32 canvas_type, i32 canvas_group) {
	history_step_t *step = history_push(HISTORY_ACTION_EDIT_NODES);
	step->canvas_group   = canvas_group;
	step->canvas_type    = canvas_type;
	step->canvas         = util_clone_canvas(canvas);
}

void history_paint() {
	history_push_undo    = false;
	history_step_t *step = history_push(HISTORY_ACTION_PAINT);
	step->name           = tr(history_tool_names->buffer[g_context->tool]);
	history_tiles_begin(step, g_context->layer); // The snapshot the stroke blends over, its tiles for undo
	timeline_stroke_begin(step, g_context->layer);
	paint_rec_stroke_begin(step, g_context->layer); // The stroke recorded for replay (paint_rec.c)

	// Erasing the bottom layer makes the mesh see-through
	if (g_context->tool == TOOL_TYPE_ERASER && g_context->layer == g_project->_->layers->buffer[0]) {
		layers_enable_translucency();
	}
}

void history_new_layer() {
	history_push(HISTORY_ACTION_NEW_LAYER);
}

// After the layer or mask asset is drawn into was created and selected
void history_new_image_layer(asset_t *asset) {
	history_step_t *step = history_push(HISTORY_ACTION_NEW_LAYER);
	step->image_file     = string_copy(asset->file);
	timeline_record_image(g_context->layer, asset);
}

void history_new_black_mask() {
	history_push(HISTORY_ACTION_NEW_BLACK_MASK);
}

void history_new_white_mask() {
	history_push(HISTORY_ACTION_NEW_WHITE_MASK);
}

// After the new fill mask got its fill: redo makes it again with it
void history_new_group() {
	history_push(HISTORY_ACTION_NEW_GROUP);
}

void history_duplicate_layer() {
	history_push(HISTORY_ACTION_DUPLICATE_LAYER);
}

static slot_layer_t *history_emptied_root(slot_layer_t *l) {
	slot_layer_t *top = l;
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		return top;
	}
	while (top->parent != NULL) {
		slot_layer_t_array_t *children = slot_layer_get_children(top->parent);
		for (i32 i = 0; children != NULL && i < children->length; ++i) {
			slot_layer_t *c = children->buffer[i];
			if (c != top && !slot_layer_is_descendant(c, top)) {
				return top;
			}
		}
		top = top->parent;
	}
	return top;
}

history_step_t *history_delete_layer(slot_layer_t *l) {
	slot_layer_t_array_t *layers = g_project->_->layers;
	slot_layer_t         *top    = history_emptied_root(l);
	i32                   end    = array_index_of(layers, top);
	i32                   start  = end - slot_layer_count_descendants(top);
	history_step_t       *step   = history_push(HISTORY_ACTION_DELETE_LAYER);
	step->merge_layers           = any_array_create_from_raw((void *[]){}, 0);
	step->merge_parents          = i32_array_create(0);
	step->merge_start            = start;
	step->merge_length           = layers->length;
	for (i32 i = start; i <= end; ++i) {
		slot_layer_t *c = layers->buffer[i];
		any_array_push(step->merge_layers, c);
		i32_array_push(step->merge_parents, c->parent != NULL ? array_index_of(layers, c->parent) : -1);
	}
	for (i32 i = 0; i < step->merge_layers->length; ++i) {
		array_remove(layers, step->merge_layers->buffer[i]);
	}
	history_records_take(step);
	context_set_layer(layers->buffer[start > 0 ? start - 1 : 0]);
	return step;
}

void history_clear_layer(history_action_t action, u32 color) {
	history_step_t *step = history_push(action);
	step->path           = slot_layer_is_path(g_context->layer) ? history_path_copy(g_context->layer) : NULL; // Clear drops the points too
	history_tiles_begin(step, g_context->layer); // As a stroke, the tiles it changes. Before the entry: it finalizes what is pending
	timeline_record_clear(g_context->layer, color);
}

void history_order_layers(i32 prev_order) {
	history_step_t *step = history_push(HISTORY_ACTION_ORDER_LAYERS);
	step->prev_order     = prev_order;
}

static void history_merge_capture(slot_layer_t *top, i32 start) {
	slot_layer_t_array_t *layers = g_project->_->layers;
	history_step_t       *step   = history_push(HISTORY_ACTION_MERGE_LAYERS);
	step->merge_start            = start;
	step->merge_length           = layers->length;
	history_records_copy(step, layers, start, array_index_of(layers, top)); // Back in place of the merged layers on undo
}

void history_merge_layers() {
	slot_layer_t_array_t *layers = g_project->_->layers;
	slot_layer_t         *l1     = g_context->layer;
	slot_layer_t         *l0     = layers->buffer[array_index_of(layers, l1) - slot_layer_count_descendants(l1) - 1];
	history_merge_capture(l1, array_index_of(layers, l0) - slot_layer_count_descendants(l0));
	timeline_record_merge(l0, l1); // l1 goes into l0 (a group merged first is a new layer, its timeline starts with its next change)
}

void history_merge_group(slot_layer_t *l) {
	slot_layer_t *top = slot_layer_shows(l) ? l : history_emptied_root(l);
	history_merge_capture(top, array_index_of(g_project->_->layers, top) - slot_layer_count_descendants(top));
}

void history_apply_mask() {
	// The mask's settings, undo brings it back with them (opacity and blending are kept by history_push)
	slot_layer_t   *m    = g_context->layer;
	history_step_t *step = history_push(HISTORY_ACTION_APPLY_MASK);
	step->mask_effects = mask_stack_copy_effects(m->mask_effects);

	// The layers the mask is applied to keep its timeline. A fill mask or fill layer starts one from its render (undo
	// brings them back from it, fill again).
	step->fills[0] = fill_encode(m->fill, false);
	step->fills[1] = fill_encode(m->parent->fill, false);
	timeline_allow_fill(true);
	timeline_t           *src   = timeline_ensure(m);
	slot_layer_t_array_t *dests = slot_layer_is_group(m->parent) ? slot_layer_get_children(m->parent) : NULL;
	for (i32 i = 0; i < (dests != NULL ? dests->length : 1); ++i) {
		timeline_record_apply_mask(dests != NULL ? dests->buffer[i] : m->parent, m, src);
	}
	timeline_allow_fill(false);
}

void history_invert_mask() {
	history_step_t *step = history_push(HISTORY_ACTION_INVERT_MASK);
	history_tiles_begin(step, g_context->layer);
	timeline_record_op(g_context->layer, TL_INVERT, NULL, 0, NULL, TL_FLAG_UPSCALABLE | TL_FLAG_INVOLUTION);
}

// After the layer turned into a fill (slot_layer_to_fill_layer), the step keeps the fill for redo. Its timeline ends
// with the conversion and waits for a conversion back.
static void history_to_fill(history_action_t action) {
	history_step_t *step = history_push(action);
	step->fill           = fill_encode(g_context->layer->fill, false);
	timeline_record_fill(g_context->layer);
}

void history_to_fill_layer() {
	history_to_fill(HISTORY_ACTION_TO_FILL_LAYER);
}

// Before the layer turns back to paint: undo gives its fill back
static void history_to_paint(history_action_t action) {
	history_step_t *step = history_push(action);
	step->fill           = fill_encode(g_context->layer->fill, false);
}

void history_to_paint_layer() {
	history_to_paint(HISTORY_ACTION_TO_PAINT_LAYER);
}

void history_to_paint_mask() {
	history_to_paint(HISTORY_ACTION_TO_PAINT_MASK);
}

void history_layer_opacity() {
	history_push(HISTORY_ACTION_LAYER_OPACITY);
}

void history_layer_blending() {
	history_push(HISTORY_ACTION_LAYER_BLENDING);
}

void history_layer_visible(slot_layer_t *l) {
	slot_layer_t *prev = g_context->layer;
	g_context->layer   = l;
	history_push(HISTORY_ACTION_LAYER_VISIBLE);
	g_context->layer = prev;
}

void history_layer_name(slot_layer_t *l, char *prev_name) {
	slot_layer_t *prev   = g_context->layer;
	g_context->layer     = l;
	history_step_t *step = history_push(HISTORY_ACTION_LAYER_NAME);
	step->layer_name     = prev_name;
	g_context->layer     = prev;
}

void history_layer_object() {
	history_push(HISTORY_ACTION_LAYER_OBJECT);
}

// After smart material name was put on the layer stack, its top layer selected
void history_smart_material(char *name) {
	history_step_t *step = history_push(HISTORY_ACTION_SMART_MATERIAL);
	step->layer_name     = string_copy(name);
}

// Before the fill of l changes (a source, the projection), its fill and decal box as they are
void history_layer_fill(slot_layer_t *l) {
	slot_layer_t *_layer = g_context->layer;
	g_context->layer     = l;
	history_step_t *step = history_push(HISTORY_ACTION_LAYER_FILL);
	step->fill           = fill_encode(l->fill, false);
	for (i32 i = 0; i < 16; ++i) { // The decal box after the fill
		f32_array_push(step->fill, ((f32 *)&l->decal_mat)[i]);
	}
	g_context->layer = _layer;
}

void history_new_swatch() {
	history_step_t *step = history_push(HISTORY_ACTION_NEW_SWATCH);
	step->swatch         = array_index_of(g_project->swatches, g_context->swatch);
	step->swatch_color   = g_context->swatch;
}

void history_delete_swatch(swatch_color_t *swatch) {
	history_step_t *step = history_push(HISTORY_ACTION_DELETE_SWATCH);
	step->swatch         = array_index_of(g_project->swatches, swatch);
	step->swatch_color   = swatch;
}

void history_replace_swatches(char *name) {
	history_step_t *step = history_push(HISTORY_ACTION_REPLACE_SWATCHES);
	step->name           = name;
	step->swatch_colors  = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->swatches->length; ++i) {
		any_array_push(step->swatch_colors, g_project->swatches->buffer[i]);
	}
}

void history_new_material() {
	history_step_t *step = history_push(HISTORY_ACTION_NEW_MATERIAL);
	step->canvas_type    = 0;
	step->canvas         = util_clone_canvas(g_context->material->canvas);
}

void history_delete_material() {
	history_step_t *step = history_push(HISTORY_ACTION_DELETE_MATERIAL);
	step->canvas_type    = 0;
	step->canvas         = util_clone_canvas(g_context->material->canvas);
}

void history_duplicate_material() {
	history_step_t *step = history_push(HISTORY_ACTION_DUPLICATE_MATERIAL);
	step->canvas_type    = 0;
	step->canvas         = util_clone_canvas(g_context->material->canvas);
}

void history_delete_material_group(node_group_t *group) {
	history_step_t *step = history_push(HISTORY_ACTION_DELETE_NODE_GROUP);
	step->canvas_type    = CANVAS_TYPE_MATERIAL;
	step->canvas_group   = array_index_of(history_groups(step), group);
	step->canvas         = util_clone_canvas(group->canvas);
}

// Before a mask stack edit of the current layer: keeps its effects for undo
void history_mask_stack() {
	history_step_t *step = history_push(HISTORY_ACTION_MASK_STACK);
	step->mask_effects   = mask_stack_copy_effects(g_context->layer->mask_effects);
}

// Before a path edit of l (points added, removed or moved): undo gives its points and pixels back
void history_path(slot_layer_t *l) {
	slot_layer_t *prev   = g_context->layer;
	g_context->layer     = l;
	history_step_t *step = history_push(HISTORY_ACTION_PATH);
	step->path           = history_path_copy(l);
	g_context->layer     = prev;
}

// Working resolution w0 x h0 to w1 x h1 (util/util_layer_resolution.c)
void history_resolution(i32 w0, i32 h0, i32 w1, i32 h1) {
	history_step_t *step = history_push(HISTORY_ACTION_RESOLUTION);
	step->resolution[0]  = w0;
	step->resolution[1]  = h0;
	step->resolution[2]  = w1;
	step->resolution[3]  = h1;
}
