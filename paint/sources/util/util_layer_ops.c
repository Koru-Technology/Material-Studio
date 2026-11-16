#include "../global.h"

// Layer operations of the layers panel for the app API and the scripts (registered under their script_layer_* names,
// minic_api.c): checked arguments, one undo step each, NULL / false when the layer is gone or the operation does not
// apply

slot_layer_t *layers_op_get(char *name) {
	if (name == NULL || g_project == NULL || g_project->_ == NULL) {
		return NULL;
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (string_equals(g_project->_->layers->buffer[i]->name, name)) {
			return g_project->_->layers->buffer[i];
		}
	}
	return NULL;
}

void layers_op_set(slot_layer_t *l) {
	if (!slot_layer_alive(l)) {
		return;
	}
	context_set_layer(l);
	g_context->ddirty = 2;
}

static slot_layer_t *layers_op_new_cleared(i32 base_color, f32 occlusion, f32 roughness, f32 metallic) {
	if (g_context == NULL || g_context->layer == NULL) {
		return NULL;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	slot_layer_t *l = layers_new_layer(false, -1, NULL);
	if (l != NULL) {
		history_new_layer();
		slot_layer_clear(l, base_color, NULL, occlusion, roughness, metallic);
	}
	draw_resume(gpu);
	g_context->ddirty = 2;
	return l;
}

slot_layer_t *layers_op_new(void) {
	return layers_op_new_cleared(0x00000000, 1.0, layers_default_rough, 0.0);
}

slot_layer_t *layers_op_new_color(i32 base_color, f32 occlusion, f32 roughness, f32 metallic) {
	return layers_op_new_cleared(base_color, occlusion, roughness, metallic);
}

slot_layer_t *layers_op_new_fill(void) {
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL) {
		return NULL;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	slot_layer_t *l = layers_new_layer(false, -1, NULL);
	if (l != NULL) {
		history_new_layer();
		l->uv_type = UV_TYPE_UVMAP;
		layers_history_with_prev(history_to_fill_layer); // One undo, as the Fill Layer menu
		slot_layer_to_fill_layer(l);
	}
	draw_resume(gpu);
	g_context->rtdirty = 1;
	g_context->ddirty  = 2;
	return l;
}

void layers_op_delete(slot_layer_t *l) {
	if (!slot_layer_alive(l) || !tab_layers_can_delete(l)) {
		return;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	tab_layers_delete_layer(l);
	draw_resume(gpu);
}

i32 layers_op_count(void) {
	if (g_project == NULL || g_project->_ == NULL) {
		return 0;
	}
	return g_project->_->layers->length;
}

slot_layer_t *layers_op_at(i32 i) {
	if (i < 0 || i >= layers_op_count()) {
		return NULL;
	}
	return g_project->_->layers->buffer[i];
}

i32 layers_op_index(slot_layer_t *l) {
	if (l == NULL || g_project == NULL || g_project->_ == NULL) {
		return -1;
	}
	return array_index_of(g_project->_->layers, l);
}

slot_layer_t *layers_op_new_mask(slot_layer_t *l, i32 type) {
	// type: 0 black, 1 white, 2 fill with the current material
	if (!slot_layer_alive(l) || type < 0 || type > 2) {
		return NULL;
	}
	if (type == 2 && g_context->material == NULL) {
		return NULL;
	}
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		l = l->parent;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	context_set_layer(l);
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *m        = layers_new_mask(false, l, -1);
	if (m != NULL) {
		tab_layers_remap_all_layer_pointers(pointers);
		if (type == 0) {
			history_new_black_mask();
			slot_layer_clear(m, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		}
		else if (type == 1) {
			history_new_white_mask();
			slot_layer_clear(m, 0xffffffff, NULL, 1.0, layers_default_rough, 0.0);
		}
		else {
			history_new_fill_mask();
			slot_layer_clear(m, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
			slot_layer_to_fill_layer(m);
		}
		layers_update_fill_layers();
	}
	draw_resume(gpu);
	g_context->layers_preview_dirty = true;
	g_context->rtdirty              = 1;
	g_context->ddirty               = 2;
	return m;
}

slot_layer_t *layers_op_new_group(slot_layer_t *l) {
	if (!slot_layer_alive(l)) {
		return NULL;
	}
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		l = l->parent;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *group    = layers_new_group();
	if (group != NULL) {
		context_set_layer(l);
		array_remove(g_project->_->layers, group);
		array_insert(g_project->_->layers, array_index_of(g_project->_->layers, l) + 1, group);
		group->parent = l->parent;
		l->parent     = group;
		tab_layers_remap_all_layer_pointers(pointers);
		context_set_layer(group);
		history_new_group();
	}
	draw_resume(gpu);
	g_context->ddirty = 2;
	return group;
}

static void layers_op_refill(slot_layer_t *l) {
	slot_material_t *material = g_context->material;
	g_context->material       = l->fill_material;
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	layers_update_fill_layers();
	context_set_material(material);
}

void layers_op_set_name(slot_layer_t *l, char *name) {
	if (!slot_layer_alive(l) || name == NULL || name[0] == '\0') {
		return;
	}
	l->name = string_copy(slot_layer_unique_name(l, name));
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

void layers_op_update(slot_layer_t *l) {
	if (!slot_layer_alive(l)) {
		return;
	}
	draw_saved_t gpu = draw_suspend();
	if (l->fill_material != NULL) {
		layers_op_refill(l);
	}
	if (l == g_project->_->layers->buffer[0] && l->mask_opacity < 1.0) {
		layers_enable_translucency();
	}
	make_material_parse_mesh_material();
	draw_resume(gpu);
	g_context->layers_preview_dirty = true;
	g_context->rtdirty              = 1;
	g_context->ddirty               = 2;
}

bool layers_op_move(slot_layer_t *l, i32 to) {
	if (!slot_layer_alive(l) || to < 0 || to >= g_project->_->layers->length) {
		return false;
	}
	if (!slot_layer_can_move(l, to)) {
		return false;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	slot_layer_move(l, to);
	make_material_parse_mesh_material();
	draw_resume(gpu);
	g_context->ddirty = 2;
	return true;
}

bool layers_op_set_group(slot_layer_t *l, slot_layer_t *group) {
	if (!slot_layer_alive(l) || slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		return false;
	}
	if (group == NULL) {
		if (l->parent == NULL) {
			return true;
		}
		return layers_op_move(l, g_project->_->layers->length - 1);
	}
	if (!slot_layer_alive(group) || !slot_layer_is_group(group)) {
		return false;
	}
	if (l->parent == group) {
		return true;
	}
	// Place directly below the group and its masks, slot_layer_move then parents it to the group
	i32 anchor = array_index_of(g_project->_->layers, group);
	while (anchor > 0 && slot_layer_is_mask(g_project->_->layers->buffer[anchor - 1]) && g_project->_->layers->buffer[anchor - 1]->parent == group) {
		--anchor;
	}
	i32  from         = array_index_of(g_project->_->layers, l);
	i32  to           = from < anchor ? anchor - 1 : anchor;
	bool show_panel   = group->show_panel;
	group->show_panel = true; // A collapsed group would make the move skip past its children
	bool moved        = layers_op_move(l, to);
	group->show_panel = show_panel;
	return moved && l->parent == group;
}

void layers_op_set_object(slot_layer_t *l, object_t *object) {
	if (!slot_layer_alive(l)) {
		return;
	}
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		l = l->parent;
	}
	i32 mask = 0;
	if (object != NULL) {
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			if (g_project->_->paint_objects->buffer[i]->base == object) {
				mask = i + 1;
				break;
			}
		}
		if (mask == 0) {
			return;
		}
	}
	if (l->object_mask == mask) {
		return;
	}
	draw_saved_t gpu = draw_suspend();
	context_set_layer(l);
	history_layer_object();
	l->object_mask = mask;
	make_material_parse_mesh_material();
	if (l->fill_material != NULL) {
		layers_op_refill(l);
	}
	else {
		layers_set_object_mask();
	}
	draw_resume(gpu);
	g_context->rtdirty = 1;
	g_context->ddirty  = 2;
}

// Layer context menu operations

// draw_resume() after a layer operation, with the layer views updated
void layers_op_end(draw_saved_t g) {
	make_material_parse_mesh_material();
	draw_resume(g);
	g_context->layers_preview_dirty = true;
	g_context->rtdirty              = 1;
	g_context->ddirty               = 2;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

slot_layer_t *layers_op_duplicate(slot_layer_t *l) {
	if (!slot_layer_alive(l)) {
		return NULL;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	context_set_layer(l);
	history_duplicate_layer();
	layers_duplicate_layer(l);
	layers_op_end(gpu);
	return g_context->layer;
}

slot_layer_t *layers_op_merge_down(slot_layer_t *l) {
	if (!slot_layer_alive(l) || !tab_layers_can_merge_down(l)) {
		return NULL;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	context_set_layer(l);
	history_merge_layers();
	layers_merge_down();
	if (g_context->layer->fill_material != NULL) {
		slot_layer_to_paint_layer(g_context->layer);
	}
	layers_op_end(gpu);
	return g_context->layer;
}

slot_layer_t *layers_op_merge_group(slot_layer_t *group) {
	if (!slot_layer_alive(group) || !slot_layer_is_group(group) || slot_layer_get_children(group) == NULL) {
		return NULL;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	if (slot_layer_shows(group) || tab_layers_can_delete(group)) {
		history_merge_group(group);
	}
	slot_layer_t *l = layers_merge_group(group);
	if (l != NULL) {
		context_set_layer(l);
	}
	layers_op_end(gpu);
	return l;
}

void layers_op_clear(slot_layer_t *l) {
	if (!slot_layer_alive(l) || l->fill_material != NULL) {
		return;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	context_set_layer(l);
	if (slot_layer_is_group(l)) {
		slot_layer_t_array_t *children = slot_layer_get_children(l);
		for (i32 i = 0; children != NULL && i < children->length; ++i) {
			slot_layer_t *c  = children->buffer[i];
			g_context->layer = c;
			history_clear_layer();
			slot_layer_clear(c, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		}
		g_context->layer = l;
	}
	else {
		history_clear_layer();
		slot_layer_clear(l, slot_layer_is_mask(l) ? 0xffffffff : 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		util_layer_clear_path_points(l);
	}
	if (l == g_project->_->layers->buffer[0] || l == g_project->_->layers->buffer[0]->parent) {
		layers_enable_translucency();
	}
	layers_op_end(gpu);
}

bool layers_op_invert_mask(slot_layer_t *m) {
	if (!slot_layer_alive(m) || !slot_layer_is_mask(m) || m->fill_material != NULL) {
		return false;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	context_set_layer(m);
	history_invert_mask();
	slot_layer_invert_mask(m);
	layers_op_end(gpu);
	return true;
}

slot_layer_t *layers_op_apply_mask(slot_layer_t *m) {
	if (!slot_layer_alive(m) || !slot_layer_is_mask(m)) {
		return NULL;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	slot_layer_t *parent = m->parent;
	g_context->layer     = m;
	history_apply_mask();
	slot_layer_apply_mask(m);
	context_set_layer(parent);
	layers_op_end(gpu);
	return parent;
}

bool layers_op_to_fill(slot_layer_t *l) {
	// Fills with the current material, a fill layer takes it in place of its own
	if (slot_layer_alive(l) && slot_layer_is_layer(l) && l->fill_material != NULL && g_context->material != NULL &&
	    l->fill_material != g_context->material) {
		draw_saved_t gpu = draw_suspend();
		base_init_undo_layers();
		slot_layer_set_fill_material(l, g_context->material);
		layers_op_end(gpu);
		return true;
	}
	if (!slot_layer_alive(l) || slot_layer_is_group(l) || l->fill_material != NULL || l->path_material != NULL || g_context->material == NULL) {
		return false;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	context_set_layer(l);
	slot_layer_is_layer(l) ? history_to_fill_layer() : history_to_fill_mask();
	slot_layer_to_fill_layer(l);
	layers_op_end(gpu);
	return true;
}

bool layers_op_to_paint(slot_layer_t *l) {
	if (!slot_layer_alive(l) || slot_layer_is_group(l) || (l->fill_material == NULL && l->path_material == NULL)) {
		return false;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	if (slot_layer_is_filter(l)) {
		slot_layer_relocate(l, array_index_of(g_project->_->layers, l->parent));
		l->parent = l->parent->parent;
	}
	context_set_layer(l);
	slot_layer_is_layer(l) ? history_to_paint_layer() : history_to_paint_mask();
	slot_layer_to_paint_layer(l);
	layers_op_end(gpu);
	return true;
}

// Path, curve and text layers paint with the current material along the points added to them

slot_layer_t *layers_op_new_path(bool curved) {
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL) {
		return NULL;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	slot_layer_t *l = layers_new_path_layer(curved);
	if (l != NULL) {
		history_new_layer();
	}
	layers_op_end(gpu);
	return l;
}

slot_layer_t *layers_op_new_text(char *text) {
	// The layer name is the painted text
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL || text == NULL || text[0] == '\0') {
		return NULL;
	}
	draw_saved_t gpu = draw_suspend();
	base_init_undo_layers();
	slot_layer_t *l = layers_new_text_layer();
	if (l != NULL) {
		history_new_layer();
		layers_op_set_name(l, text);
	}
	layers_op_end(gpu);
	return l;
}

bool layers_op_add_path_point(slot_layer_t *l, f32 x, f32 y) {
	// x, y in 0..1 viewport space like script_paint(), a curve takes anchor, control, anchor, control, ...
	if (!slot_layer_alive(l) || !slot_layer_is_path(l)) {
		return false;
	}
	draw_saved_t gpu  = draw_suspend();
	tool_type_t tool = g_context->tool;
	if (l->path_tool != -1) {
		g_context->tool = l->path_tool;
	}
	else if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_BLUR && tool != TOOL_TYPE_PARTICLE) {
		g_context->tool = TOOL_TYPE_BRUSH; // The first point picks the tool the path paints with
	}
	i32 count              = l->path_points->length;
	path_layer_last_active = count / 2 - 1; // Continue from the last point
	util_layer_add_path_point(l, x, y);
	g_context->tool = tool;
	layers_op_end(gpu);
	return l->path_points->length > count;
}

void layers_op_repaint_path(slot_layer_t *l) {
	// Repaints with the current path material, after its nodes or the path points changed
	if (!slot_layer_alive(l) || !slot_layer_is_path(l)) {
		return;
	}
	draw_saved_t gpu = draw_suspend();
	util_layer_repaint_path(l);
	layers_op_end(gpu);
}
