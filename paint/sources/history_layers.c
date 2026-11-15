#include "global.h"

// What undo and redo swap: layers taken out and put back, layer order, properties, swatches and node canvases

slot_layer_t_array_t *history_merge_saved_layers() {
	i32 count = 0;
	for (i32 i = 0; history_steps != NULL && i < history_steps->length; ++i) {
		slot_layer_t_array_t *layers = history_steps->buffer[i]->merge_layers;
		count += layers != NULL ? layers->length : 0;
	}
	slot_layer_t_array_t *saved = any_array_create_from_raw_tmp(NULL, count);
	i32                   pos   = 0;
	for (i32 i = 0; history_steps != NULL && i < history_steps->length; ++i) {
		slot_layer_t_array_t *layers = history_steps->buffer[i]->merge_layers;
		for (i32 j = 0; layers != NULL && j < layers->length; ++j) {
			saved->buffer[pos++] = layers->buffer[j];
		}
	}
	return saved;
}

void history_swap_merge(history_step_t *step, bool undo) {
	slot_layer_t_array_t *layers   = g_project->_->layers;
	slot_layer_t_array_t *saved    = step->merge_layers;
	i32_array_t          *parents  = step->merge_parents;
	i32                   count    = layers->length - (step->merge_length - saved->length);
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();

	step->merge_layers  = any_array_create_from_raw((void *[]){}, 0);
	step->merge_parents = i32_array_create(0);
	step->merge_length  = layers->length;
	for (i32 i = 0; i < count; ++i) {
		slot_layer_t *l = layers->buffer[step->merge_start + i];
		any_array_push(step->merge_layers, l);
		i32_array_push(step->merge_parents, l->parent != NULL ? array_index_of(layers, l->parent) : -1);
	}
	for (i32 i = 0; i < count; ++i) {
		array_remove(layers, step->merge_layers->buffer[i]);
	}
	for (i32 i = 0; i < saved->length; ++i) {
		array_insert(layers, step->merge_start + i, saved->buffer[i]);
	}
	for (i32 i = 0; i < saved->length; ++i) {
		saved->buffer[i]->parent = parents->buffer[i] >= 0 ? layers->buffer[parents->buffer[i]] : NULL;
	}

	if (step->action == HISTORY_ACTION_DELETE_LAYER) {
		tab_layers_remap_all_layer_pointers(pointers);
	}

	i32 top = step->merge_start + saved->length - 1;
	context_set_layer(layers->buffer[undo ? step->layer : top >= 0 ? top : 0]);
	g_context->layers_preview_dirty = true;
}

void history_swap_order(history_step_t *step) {
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *l        = g_project->_->layers->buffer[step->prev_order];
	i32                   parent   = l->parent != NULL ? array_index_of(g_project->_->layers, l->parent) : -1;
	slot_layer_relocate(l, step->layer);
	l->parent          = step->layer_parent >= 0 ? g_project->_->layers->buffer[step->layer_parent] : NULL;
	step->layer_parent = parent;
	i32 prev_order     = step->prev_order;
	step->prev_order   = step->layer;
	step->layer        = prev_order;
	tab_layers_remap_all_layer_pointers(pointers);
	context_set_layer(l);
	make_material_parse_mesh_material();
}

// Deferred to the next frame: a later step undone in the same frame may have removed the layer, its textures are gone
bool history_layer_alive(slot_layer_t *l) {
	return array_index_of(g_project->_->layers, l) >= 0;
}

void history_update_fill_layer(slot_layer_t *l) {
	if (history_layer_alive(l)) {
		layers_refresh_fill_layer(l);
	}
}

void history_layer_object_clear(slot_layer_t *l) {
	if (!history_layer_alive(l)) {
		return;
	}
	g_context->material = l->fill_material;
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	layers_update_fill_layers();
}

void history_get_height_blend(slot_layer_t *l, f32 *out) {
	out[0] = l->height_blend ? 1.0 : 0.0;
	out[1] = l->height_blend_below ? 1.0 : 0.0;
	out[2] = l->height_blend_influence;
	out[3] = l->height_blend_sharpness;
	out[4] = l->height_blend_offset;
	out[5] = l->height_blend_reach;
}

static void history_set_height_blend(slot_layer_t *l, f32 *in) {
	l->height_blend           = in[0] > 0.5;
	l->height_blend_below     = in[1] > 0.5;
	l->height_blend_influence = in[2];
	l->height_blend_sharpness = in[3];
	l->height_blend_offset    = in[4];
	l->height_blend_reach     = in[5];
}

// The mask recreated by an apply mask undo gets the settings it had, the step keeps its copy for the next undo
void history_restore_applied_mask(history_step_t *step, slot_layer_t *m) {
	m->mask_opacity = step->layer_opacity;
	m->blending     = step->layer_blending;
	history_set_height_blend(m, step->layer_height_blend);
	m->mask_effects = mask_stack_copy_effects(step->mask_effects);
	mask_stack_mark_dirty(m);
	if (m->height_blend) {
		make_material_height_used = true;
	}
	make_material_parse_mesh_material();
}

void history_swap_layer_props(history_step_t *step) {
	slot_layer_t *l = g_project->_->layers->buffer[step->layer];

	if (step->action == HISTORY_ACTION_LAYER_OPACITY) {
		context_set_layer(l);
		f32 t               = l->mask_opacity;
		l->mask_opacity     = step->layer_opacity;
		step->layer_opacity = t;
		make_material_parse_mesh_material();
	}
	else if (step->action == HISTORY_ACTION_LAYER_BLENDING) {
		context_set_layer(l);
		blend_type_t t       = l->blending;
		l->blending          = step->layer_blending;
		step->layer_blending = t;
		make_material_parse_mesh_material();
	}
	else if (step->action == HISTORY_ACTION_LAYER_VISIBLE) {
		context_set_layer(l);
		l->visible = !l->visible;
		make_material_parse_mesh_material();
		if (ui_view2d_show) {
			ui_view2d_hwnd->redraws = 2;
		}
	}
	else if (step->action == HISTORY_ACTION_LAYER_NAME) {
		char *t = l->name;
		l->name          = step->layer_name;
		step->layer_name = t;
		context_set_layer(l);
	}
	else if (step->action == HISTORY_ACTION_LAYER_OBJECT) {
		i32 t              = l->object_mask;
		l->object_mask     = step->layer_object;
		step->layer_object = t;
		context_set_layer(l);
		make_material_parse_mesh_material();
		if (l->fill_material != NULL) {
			sys_notify_on_next_frame(&history_layer_object_clear, l);
		}
		else {
			layers_set_object_mask();
		}
	}
	else if (step->action == HISTORY_ACTION_LAYER_SCALE) {
		f32 t             = l->scale;
		l->scale          = step->layer_scale;
		step->layer_scale = t;
		sys_notify_on_next_frame(&history_update_fill_layer, l);
	}
	else if (step->action == HISTORY_ACTION_LAYER_ANGLE) {
		f32 t             = l->angle;
		l->angle          = step->layer_angle;
		step->layer_angle = t;
		sys_notify_on_next_frame(&history_update_fill_layer, l);
	}
	else if (step->action == HISTORY_ACTION_LAYER_FILL_MATERIAL) {
		slot_material_t_array_t *materials = g_project->_->materials;
		i32                      t         = array_index_of(materials, l->fill_material);
		if (step->material >= 0 && step->material < materials->length) {
			l->fill_material = materials->buffer[step->material];
		}
		step->material = t;
		sys_notify_on_next_frame(&history_update_fill_layer, l);
	}
	else if (step->action == HISTORY_ACTION_MASK_HEIGHT_BLEND) {
		f32 t[6];
		history_get_height_blend(l, t);
		history_set_height_blend(l, step->layer_height_blend);
		memcpy(step->layer_height_blend, t, sizeof(t));
		context_set_layer(l);
		make_material_parse_mesh_material();
	}
	else if (step->action == HISTORY_ACTION_MASK_STACK) {
		f32_array_t *t     = l->mask_effects;
		l->mask_effects    = step->mask_effects;
		step->mask_effects = t;
		context_set_layer(l);
		if (g_context->mask_effect >= mask_stack_count(l)) {
			g_context->mask_effect = -1; // The selected effect went with the edit
		}
		mask_stack_mark_dirty(l);
	}
	else if (step->action == HISTORY_ACTION_LAYER_UV_TYPE) {
		i32 t               = l->uv_type;
		l->uv_type          = step->layer_uv_type;
		step->layer_uv_type = t;
		context_set_material(l->fill_material);
		context_set_layer(l);
		make_material_parse_paint_material(true);
		sys_notify_on_next_frame(&history_update_fill_layer, l);
	}
}

void history_new_swatch_slot(history_step_t *step) {
	array_insert(g_project->swatches, step->swatch, step->swatch_color);
	g_context->swatch = step->swatch_color;
	ui_dock_redraw(TAB_AREA_STATUS);
}

void history_delete_swatch_slot(history_step_t *step) {
	step->swatch_color = g_project->swatches->buffer[step->swatch];
	if (g_project->swatches->length > 1) {
		g_context->swatch = g_project->swatches->buffer[step->swatch == g_project->swatches->length - 1 ? step->swatch - 1 : step->swatch + 1];
	}
	array_splice(g_project->swatches, step->swatch, 1);
	ui_dock_redraw(TAB_AREA_STATUS);
}

void history_swap_swatches(history_step_t *step) {
	swatch_color_t_array_t *t = g_project->swatches;
	g_project->swatches       = step->swatch_colors;
	step->swatch_colors       = t;
	if (g_project->swatches->length > 0) {
		g_context->swatch = g_project->swatches->buffer[0];
	}
	ui_dock_redraw(TAB_AREA_STATUS);
}

void history_swap_swatch(history_step_t *step) {
	swatch_color_t *s       = g_project->swatches->buffer[step->swatch];
	swatch_color_t  t       = *s;
	*s                      = *step->swatch_color;
	*step->swatch_color     = t;
	g_context->swatch       = s;
	g_context->picked_color = util_clone_swatch_color(s);
	ui_dock_redraw(TAB_AREA_STATUS);
	ui_header_handle->redraws = 2;
}

node_group_t_array_t *history_groups(history_step_t *step) {
	return g_project->_->material_groups;
}

ui_node_canvas_t *history_get_canvas(history_step_t *step) {
	if (step->canvas_group == -1) {
		return g_project->_->materials->buffer[step->material]->canvas;
	}
	else {
		return history_groups(step)->buffer[step->canvas_group]->canvas;
	}
}

void history_set_canvas(history_step_t *step, ui_node_canvas_t *canvas) {
	if (step->canvas_group == -1) {
		g_project->_->materials->buffer[step->material]->canvas = canvas;
	}
	else {
		history_groups(step)->buffer[step->canvas_group]->canvas = canvas;
	}
}

void history_swap_canvas(history_step_t *step) {
	if (step->canvas_type == 0) {
		ui_node_canvas_t *_canvas = history_get_canvas(step);
		history_set_canvas(step, step->canvas);
		step->canvas        = _canvas;
		g_context->material = g_project->_->materials->buffer[step->material];
	}
	else {
		ui_node_canvas_t *_canvas                          = g_project->_->brushes->buffer[step->brush]->canvas;
		g_project->_->brushes->buffer[step->brush]->canvas = step->canvas;
		step->canvas                                       = _canvas;
		g_context->brush                                   = g_project->_->brushes->buffer[step->brush];
	}

	ui_nodes_t *nodes                = ui_nodes_get_nodes();
	nodes->nodes_selected_id->length = 0;

	ui_nodes_canvas_changed();
	ui_nodes_hwnd->redraws = 2;
}
