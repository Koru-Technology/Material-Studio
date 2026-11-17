#include "../global.h"

// New layers, masks, groups, path, text, fill, filter, image and color layers, duplicates

void layers_new_layer_clear(slot_layer_t *l) {
	if (array_index_of(g_project->_->layers, l) < 0) {
		return;
	}
	if (slot_layer_is_path(l) && l->path_points->length > 0) {
		return;
	}
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
}

slot_layer_t *layers_new_layer(bool clear, i32 position, slot_layer_t *parent) {
	if (g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}

	slot_layer_t *l = slot_layer_create("", LAYER_SLOT_TYPE_LAYER, parent);
	l->object_mask  = g_context->layer_filter;

	if (position == -1 && slot_layer_is_filter(l)) {
		position = array_index_of(g_project->_->layers, parent);
	}

	if (position == -1) {
		if (slot_layer_is_mask(g_context->layer) || slot_layer_is_filter(g_context->layer))
			context_set_layer(g_context->layer->parent);
		array_insert(g_project->_->layers, array_index_of(g_project->_->layers, g_context->layer) + 1, l);
	}
	else {
		array_insert(g_project->_->layers, position, l);
	}

	context_set_layer(l);

	if (parent == NULL) {
		i32 li = array_index_of(g_project->_->layers, g_context->layer);
		if (li > 0) {
			slot_layer_t *below = g_project->_->layers->buffer[li - 1];
			if (slot_layer_is_layer(below) || slot_layer_is_group(below)) {
				g_context->layer->parent = below->parent;
			}
		}
	}

	if (clear) {
		sys_notify_on_next_frame(&layers_new_layer_clear, l);
	}


	g_context->layer_preview_dirty = true;
	return l;
}

void layers_new_mask_clear(slot_layer_t *l) {
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
}

slot_layer_t *layers_new_mask(bool clear, slot_layer_t *parent, i32 position) {
	if (g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}

	slot_layer_t *l = slot_layer_create("", LAYER_SLOT_TYPE_MASK, parent);
	if (position == -1) {
		position = array_index_of(g_project->_->layers, parent);
	}
	array_insert(g_project->_->layers, position, l);
	context_set_layer(l);
	if (clear) {
		sys_notify_on_next_frame(&layers_new_mask_clear, l);
	}
	g_context->layer_preview_dirty = true;
	return l;
}

slot_layer_t *layers_new_group() {
	if (g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}

	slot_layer_t *l = slot_layer_create("", LAYER_SLOT_TYPE_GROUP, NULL);
	any_array_push(g_project->_->layers, l);
	context_set_layer(l);
	return l;
}

slot_layer_t *layers_new_path_layer(bool curved) {
	slot_layer_t *l = layers_new_layer(true, -1, NULL);
	if (l == NULL) {
		return NULL;
	}
	l->path_points        = f32_array_create(0);
	l->path_points_world  = f32_array_create(0);
	l->path_points_camera = f32_array_create(0);
	l->path_points_parent = i32_array_create(0);
	l->path_tool          = -1;
	l->path_curved        = curved;
	l->path_material      = g_context->material;
	char *name            = string_copy(slot_layer_unique_name(l, string_tmp(curved ? "Curve %d" : "Path %d", l->id + 1)));
	l->name = name;

	return l;
}

slot_layer_t *layers_new_text_layer() {
	slot_layer_t *l = layers_new_path_layer(true);
	if (l == NULL) {
		return NULL;
	}
	l->path_text = true;
	l->path_tool = TOOL_TYPE_TEXT;
	char *name   = string_copy(slot_layer_unique_name(l, string_tmp("Text %d", l->id + 1)));
	l->name = name;
	return l;
}

void layers_create_filter_on_next_frame(void *_) {
	if (slot_layer_get_filters(g_context->layer, true) != NULL) {
		return;
	}

	slot_material_new_on_next_frame(NULL);

	slot_layer_t *l = layers_new_layer(false, -1, g_context->layer);
	layers_history_with_prev(history_new_layer); // With the filter's new material: one undo removes the filter
	layers_history_with_prev(history_to_fill_layer);
	slot_layer_to_fill_layer(l);

	// Filter material
	g_context->material->canvas->name = string_copy(g_context->layer->name);

	ui_node_canvas_t *canvas = g_context->material->canvas;
	ui_node_t        *nout   = ui_canvas_find_type(canvas, "OUTPUT_MATERIAL_PBR");
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->name, "Color")) {
			ui_remove_node(n, canvas);
			break;
		}
	}

	ui_node_t *n                         = nodes_material_create_node("LAYER", NULL);
	n->buttons->buffer[0]->default_value = f32_array_create_x(array_index_of(g_project->_->layers, l->parent));
	n->x                                 = -50;
	n->y                                 = 100;

	for (int i = 0; i < 9; ++i) {
		ui_node_link_t *l =
		    ALLOC_INIT(ui_node_link_t, {.id = ui_next_link_id(canvas->links), .from_id = n->id, .from_socket = i, .to_id = nout->id, .to_socket = i});
		any_array_push(canvas->links, l);
	}

	layers_update_fill_layer(true);
	util_render_make_material_preview();
}

void layers_create_filter() {
	sys_notify_on_next_frame(&layers_create_filter_on_next_frame, NULL);
}

void layers_create_image_mask(asset_t *asset) {
	slot_layer_t *l = g_context->layer;
	if (slot_layer_is_mask(l) || slot_layer_is_group(l)) {
		return;
	}

	slot_layer_t *m = layers_new_mask(false, l, -1);
	if (m == NULL) {
		return;
	}
	history_new_image_layer(asset); // The step points at the mask, redo draws the image again
	slot_layer_clear(m, 0x00000000, project_get_image(asset), 1.0, layers_default_rough, 0.0);
	g_context->layer_preview_dirty = true;
}

void layers_create_image_layer(asset_t *asset) {
	slot_layer_t *m = layers_new_layer(false, -1, NULL);
	if (m == NULL) {
		return;
	}
	history_new_image_layer(asset);
	slot_layer_clear(m, 0x00000000, project_get_image(asset), 1.0, layers_default_rough, 0.0);
	g_context->layer_preview_dirty = true;
}

// Scripts: an image layer, or a mask of the selected layer, from asset i of the project
void layers_create_image_at(i32 i, bool mask) {
	if (i < 0 || i >= g_project->_->assets->length) {
		return;
	}
	draw_saved_t gpu = draw_suspend();
	if (mask) {
		layers_create_image_mask(g_project->_->assets->buffer[i]);
	}
	else {
		layers_create_image_layer(g_project->_->assets->buffer[i]);
	}
	draw_resume(gpu);
}

void layers_create_color_layer_on_next_frame(void *_) {
	slot_layer_t *l = layers_new_layer(false, _layers_position, NULL);
	history_new_layer();
	l->uv_type     = UV_TYPE_UVMAP;
	l->object_mask = g_context->layer_filter;
	slot_layer_clear(l, _layers_base_color, NULL, _layers_occlusion, _layers_roughness, _layers_metallic);
}

void layers_create_color_layer(i32 base_color, f32 occlusion, f32 roughness, f32 metallic, i32 position) {
	_layers_base_color = base_color;
	_layers_occlusion  = occlusion;
	_layers_roughness  = roughness;
	_layers_metallic   = metallic;
	_layers_position   = position;

	sys_notify_on_next_frame(&layers_create_color_layer_on_next_frame, NULL);
}

static void layers_duplicate_children(slot_layer_t *l, slot_layer_t *new_parent) {
	slot_layer_t_array_t *children = slot_layer_get_direct_children(l);
	for (i32 i = 0; i < children->length; ++i) {
		slot_layer_t *c     = children->buffer[i];
		slot_layer_t *new_c = slot_layer_is_group(c) ? layers_new_group() : slot_layer_duplicate(c);
		new_c->parent       = new_parent;
		timeline_fork(c, new_c);
		array_remove(g_project->_->layers, new_c);
		array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_parent), new_c);
		layers_duplicate_children(c, new_c);
	}
}

void layers_duplicate_layer(slot_layer_t *l) {
	slot_layer_t *new_layer = slot_layer_is_group(l) ? layers_new_group() : slot_layer_duplicate(l);
	new_layer->parent       = l->parent;
	timeline_fork(l, new_layer); // Its pixels share the history of l
	array_remove(g_project->_->layers, new_layer);
	array_insert(g_project->_->layers, array_index_of(g_project->_->layers, l) + 1, new_layer);
	layers_duplicate_children(l, new_layer);
	context_set_layer(new_layer);
}
