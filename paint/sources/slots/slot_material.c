
#include "../global.h"

buffer_t *slot_material_default_canvas = NULL;

static bool slot_material_is_unique_name(ui_node_canvas_t *self, char *name) {
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		ui_node_canvas_t *c = g_project->_->materials->buffer[i]->canvas;
		if (c != self && string_equals(c->name, name)) {
			return false;
		}
	}
	return true;
}

char *slot_material_unique_name(ui_node_canvas_t *self, char *name) {
	if (slot_material_is_unique_name(self, name)) {
		return name;
	}
	char *base;
	i32   i   = strings_split_number_ext(name, &base);
	char *res = string_tmp("%s%s", base, strings_number_ext(++i));
	while (!slot_material_is_unique_name(self, res)) {
		res = string_tmp("%s%s", base, strings_number_ext(++i));
	}
	return res;
}

slot_material_t *slot_material_create(shader_data_t *m, ui_node_canvas_t *c) {
	slot_material_t *raw = ALLOC_INIT(slot_material_t, {0});
	raw->nodes           = ui_nodes_create();
	raw->preview_ready   = false;
	raw->id              = 0;
	raw->paint_base      = true;
	raw->paint_opac      = true;
	raw->paint_occ       = true;
	raw->paint_rough     = true;
	raw->paint_met       = true;
	raw->paint_nor       = true;
	raw->paint_height    = true;
	raw->paint_emis      = true;
	raw->paint_subs      = true;
	raw->uv_scale        = 1.0;
	raw->uv_angle        = 0.0;
	raw->uid             = project_uid_take();

	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *mat = g_project->_->materials->buffer[i];
		if (mat->id >= raw->id) {
			raw->id = mat->id + 1;
		}
	}
	raw->data = m;

	i32 w           = util_render_material_preview_size;
	i32 w_icon      = ui_base_asset_icon_size();
	raw->image      = gpu_create_render_target(w, w, GPU_TEXTURE_FORMAT_RGBA64);
	raw->image_icon = gpu_create_render_target(w_icon, w_icon, GPU_TEXTURE_FORMAT_RGBA64);

	if (c == NULL) {
		if (slot_material_default_canvas == NULL) { // Synchronous
			buffer_t *b                  = data_get_blob("default_material.arm");
			slot_material_default_canvas = b;
		}
		ui_node_canvas_t *decoded = armpack_decode(slot_material_default_canvas);
		raw->canvas               = util_clone_canvas(decoded);
		free(decoded);
		i32 id            = (raw->id + 1);
		raw->canvas->name = string("Material %d", id);
	}
	else {
		raw->canvas = util_clone_canvas(c);
	}
	raw->canvas->name = string_copy(slot_material_unique_name(raw->canvas, raw->canvas->name));

	if (g_config->node_previews) {
		for (i32 i = 0; i < raw->canvas->nodes->length; ++i) {
			ui_node_t *n = raw->canvas->nodes->buffer[i];
			n->flags |= UI_NODE_FLAG_PREVIEW;
		}
	}

#ifdef IRON_IOS
	raw->nodes->pan_x -= 50; // Center initial position
#else
	raw->nodes->pan_x += 110;
#endif
	raw->nodes->pan_y += 120;

	return raw;
}

void slot_material_unload(slot_material_t *raw) {
	gpu_delete_texture(raw->image);
	gpu_delete_texture(raw->image_icon);
}

void slot_material_delete(slot_material_t *raw) {
	slot_material_unload(raw);
	make_material_delete_node_previews(raw, NULL);
	i32 mpos = array_index_of(g_project->_->materials, raw);
	array_remove(g_project->_->materials, raw);
	if (g_project->_->materials->length > 0) {
		context_set_material(g_project->_->materials->buffer[mpos > 0 ? mpos - 1 : 0]);
	}
}

slot_material_t *slot_material_find_uid(u32 uid) {
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		if (m->uid == uid) {
			return m;
		}
	}
	return NULL;
}

void slot_material_refresh() {
	ui_nodes_hwnd->redraws    = 2;
	ui_nodes_group_stack      = any_array_create_from_raw((void *[]){}, 0);
	make_material_parse_paint_material(true);
	util_render_make_material_preview();
	bool decal = context_is_decal();
	if (decal) {
		util_render_make_decal_preview();
	}
}

// A copy of material i, selected
void slot_material_duplicate(i32 i) {
	g_context->material = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	any_array_push(g_project->_->materials, g_context->material);
	ui_node_canvas_t *cloned    = util_clone_canvas(g_project->_->materials->buffer[i]->canvas);
	cloned->name                = string_copy(slot_material_unique_name(cloned, cloned->name));
	g_context->material->canvas = cloned;
	slot_material_t *src        = g_project->_->materials->buffer[i];
	g_context->material->uv_scale = src->uv_scale;
	g_context->material->uv_angle = src->uv_angle;
	slot_material_refresh();
	history_duplicate_material();
}

void slot_material_update_pointers(ui_node_t_array_t *nodes, i32 deleted) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, "MATERIAL")) {
			if (n->buttons->buffer[0]->default_value->buffer[0] == deleted) {
				n->buttons->buffer[0]->default_value->buffer[0] = 9999; // Material deleted
			}
			else if (n->buttons->buffer[0]->default_value->buffer[0] > deleted) {
				n->buttons->buffer[0]->default_value->buffer[0]--; // Offset by deleted material
			}
		}
	}
}

static void slot_material_on_removed(slot_material_t *m, i32 deleted) {
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_update_pointers(g_project->_->materials->buffer[i]->canvas->nodes, deleted);
	}
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		slot_material_update_pointers(g_project->_->material_groups->buffer[i]->canvas->nodes, deleted);
	}
	for (i32 i = 0; i < m->canvas->nodes->length; ++i) {
		ui_viewnodes_on_node_remove(m->canvas->nodes->buffer[i]);
	}
}

void slot_material_remove(slot_material_t *m) {
	i32 i = array_index_of(g_project->_->materials, m);
	if (i < 0) {
		return;
	}
	fill_material_removed(m);
	history_delete_material();
	context_select_material(i == g_project->_->materials->length - 1 ? i - 1 : i + 1);
	array_splice(g_project->_->materials, i, 1);
	slot_material_on_removed(m, i);
}

static bool slot_material_nodes_link(ui_node_t_array_t *nodes, i32 index) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, "MATERIAL") && n->buttons->buffer[0]->default_value->buffer[0] == index) {
			return true;
		}
	}
	return false;
}

static bool slot_material_is_used(i32 index) {
	slot_material_t *m = g_project->_->materials->buffer[index];
	// Fill or path layer
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (fill_uses_material(l->fill, m) || l->path_material == m) {
			return true;
		}
	}
	// Linked in another material via material node
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		if (i != index && slot_material_nodes_link(g_project->_->materials->buffer[i]->canvas->nodes, index)) {
			return true;
		}
	}
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		if (slot_material_nodes_link(g_project->_->material_groups->buffer[i]->canvas->nodes, index)) {
			return true;
		}
	}
	return false;
}

void slot_material_delete_unused() {
	i32   count = g_project->_->materials->length;
	bool *used  = calloc(count, sizeof(bool));
	for (i32 i = 0; i < count; ++i) {
		used[i] = slot_material_is_used(i);
	}

	slot_material_t *selected = g_context->material;
	bool             deleted  = false;
	// Go backwards so the indices of the remaining materials stay valid
	for (i32 i = count - 1; i >= 0 && g_project->_->materials->length > 1; --i) {
		if (used[i]) {
			continue;
		}
		slot_material_t *m  = g_project->_->materials->buffer[i];
		g_context->material = m; // History stores the selected material
		history_delete_material();
		array_splice(g_project->_->materials, i, 1);
		slot_material_on_removed(m, i);
		if (m == selected) {
			selected = NULL;
		}
		deleted = true;
	}
	free(used);

	if (!deleted) {
		return;
	}
	context_set_material(selected != NULL ? selected : g_project->_->materials->buffer[0]);
}

void slot_material_new_on_next_frame(void *_) {
	g_context->material = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	any_array_push(g_project->_->materials, g_context->material);
	slot_material_refresh();
	history_new_material();
}

// Copy picked values into the Material Output node when it is the only node in the selected material
// A color material: its output node alone, its values set directly
bool slot_material_is_plain(slot_material_t *m) {
	ui_node_canvas_t *canvas = m->canvas;
	return canvas->nodes->length == 1 && string_equals(canvas->nodes->buffer[0]->type, "OUTPUT_MATERIAL_PBR");
}

void slot_material_apply_picked_color(swatch_color_t *swatch) {
	if (!slot_material_is_plain(g_context->material)) {
		return;
	}
	ui_node_t *node = g_context->material->canvas->nodes->buffer[0];
	node->inputs->buffer[0]->default_value =
	    f32_array_create_xyzw(color_get_rb(swatch->base) / 255.0, color_get_gb(swatch->base) / 255.0, color_get_bb(swatch->base) / 255.0, 1.0);
	node->inputs->buffer[1]->default_value->buffer[0] = swatch->opacity;
	node->inputs->buffer[2]->default_value->buffer[0] = swatch->occlusion;
	node->inputs->buffer[3]->default_value->buffer[0] = swatch->roughness;
	node->inputs->buffer[4]->default_value->buffer[0] = swatch->metallic;
	node->inputs->buffer[5]->default_value =
	    f32_array_create_xyz(color_get_rb(swatch->normal) / 255.0, color_get_gb(swatch->normal) / 255.0, color_get_bb(swatch->normal) / 255.0);
	node->inputs->buffer[7]->default_value->buffer[0] = swatch->height;
	slot_material_refresh();
	if (layers_is_path_material()) {
		layers_update_path_layers();
	}
}

void slot_material_new_from_swatch(swatch_color_t *swatch) {
	g_context->material = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	for (i32 i = 0; i < g_context->material->canvas->nodes->length; ++i) {
		ui_node_t *node = g_context->material->canvas->nodes->buffer[i];
		if (string_equals(node->type, "OUTPUT_MATERIAL_PBR")) {
			node->inputs->buffer[0]->default_value            = f32_array_create_xyzw(color_get_rb(swatch->base) / 255.0, color_get_gb(swatch->base) / 255.0,
			                                                                          color_get_bb(swatch->base) / 255.0, color_get_ab(swatch->base) / 255.0);
			node->inputs->buffer[1]->default_value->buffer[0] = swatch->opacity;
			node->inputs->buffer[2]->default_value->buffer[0] = swatch->occlusion;
			node->inputs->buffer[3]->default_value->buffer[0] = swatch->roughness;
			node->inputs->buffer[4]->default_value->buffer[0] = swatch->metallic;
			node->inputs->buffer[7]->default_value->buffer[0] = swatch->height;
		}
	}
	any_array_push(g_project->_->materials, g_context->material);
	slot_material_refresh();
	history_new_material();
}
