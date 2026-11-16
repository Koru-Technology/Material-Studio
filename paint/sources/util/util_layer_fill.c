#include "../global.h"

// Fill and path layers: their material rendered into the layer, filling with a stroke (to a new layer or a mask)

bool layers_is_fill_material() {
	slot_material_t *m = g_context->material;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill_material == m) {
			return true;
		}
	}
	return false;
}

void layers_update_fill_layers() {
	render_path_base_redraw(); // The 3D view shows the change
	if (bake_texture_node_baking)
		return;

	slot_layer_t  *_layer     = g_context->layer;
	tool_type_t    _tool      = g_context->tool;
	i32            _fill_type = g_context->fill_type;

	bool has_fill_layer = false;
	bool has_fill_mask  = false;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_layer(l) && l->fill_material == g_context->material) {
			has_fill_layer = true;
		}
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_mask(l) && l->fill_material == g_context->material) {
			has_fill_mask = true;
		}
	}

	if (has_fill_layer || has_fill_mask) {
		draw_saved_t saved_draw = draw_suspend();
		g_context->pdirty       = 1;
		g_context->tool         = TOOL_TYPE_FILL;
		g_context->fill_type    = FILL_TYPE_OBJECT;

		if (has_fill_layer) {
			bool first = true;
			for (i32 i = 0; i < g_project->_->layers->length; ++i) {
				slot_layer_t *l = g_project->_->layers->buffer[i];
				if (slot_layer_is_layer(l) && l->fill_material == g_context->material) {
					g_context->layer = l;
					if (first) {
						first = false;
						make_material_parse_paint_material(false);
					}
					layers_set_object_mask();
					slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
					render_path_paint_commands_paint(false);
					render_path_paint_dilate(true, true);
					stroke_refine_fill_track(l); // Painted again as better buffers land
				}
			}
		}
		if (has_fill_mask) {
			bool first = true;
			for (i32 i = 0; i < g_project->_->layers->length; ++i) {
				slot_layer_t *l = g_project->_->layers->buffer[i];
				if (slot_layer_is_mask(l) && l->fill_material == g_context->material) {
					g_context->layer = l;
					if (first) {
						first = false;
						make_material_parse_paint_material(false);
					}
					layers_set_object_mask();
					slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
					render_path_paint_commands_paint(false);
					render_path_paint_dilate(true, true);
					stroke_refine_fill_track(l);
				}
			}
		}
		g_context->pdirty               = 0;
		g_context->ddirty               = 2;
		g_context->rtdirty              = 1;
		g_context->layers_preview_dirty = true;
		draw_resume(saved_draw);
		g_context->layer = _layer;
		layers_set_object_mask();
		g_context->tool      = _tool;
		g_context->fill_type = _fill_type;
		make_material_parse_paint_material(false);
	}
}

bool layers_is_path_material() {
	slot_material_t *m = g_context->material;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_path(l) && l->path_material == m) {
			return true;
		}
	}
	return false;
}

void layers_update_path_layers() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_path(l) && l->path_material == g_context->material) {
			util_layer_repaint_path(l);
		}
	}
}

bool layers_tool_paints_fill_layer(tool_type_t tool) {
	return tool == TOOL_TYPE_BAKE || tool == TOOL_TYPE_PICKER || tool == TOOL_TYPE_COLORID;
}

void layers_update_fill_layer(bool parse_paint) {
	draw_saved_t saved_draw = draw_suspend();

	tool_type_t _tool      = g_context->tool;
	i32         _fill_type = g_context->fill_type;
	g_context->tool        = TOOL_TYPE_FILL;
	g_context->fill_type   = FILL_TYPE_OBJECT;
	g_context->pdirty      = 1;

	slot_layer_clear(g_context->layer, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	if (parse_paint) {
		make_material_parse_paint_material(false);
	}
	render_path_paint_commands_paint(false);
	render_path_paint_dilate(true, true);
	stroke_refine_fill_track(g_context->layer); // Painted again as better Material Maker buffers land

	g_context->tool      = _tool;
	g_context->fill_type = _fill_type;
	if (parse_paint && layers_tool_paints_fill_layer(_tool)) {
		make_material_parse_paint_material(false);
	}
	draw_resume(saved_draw);
}

// The render targets the paint pass reads by name, pointed at other textures for a while
typedef struct layers_fill_swap {
	char            *name;
	render_target_t *was;
	render_target_t  rt;
} layers_fill_swap_t;

static void layers_fill_swap(layers_fill_swap_t *s, char *name, gpu_texture_t *t) {
	s->name = string_copy(name);
	s->was  = any_map_get(render_path_render_targets, s->name);
	s->rt   = (render_target_t){.name = s->name, .width = t->width, .height = t->height, .format = gpu_texture_format_name(t->format), .scale = 1.0, ._image = t};
	any_map_set(render_path_render_targets, s->name, &s->rt);
}

static void layers_fill_unswap(layers_fill_swap_t *s) {
	if (s->was != NULL) {
		any_map_set(render_path_render_targets, s->name, s->was);
	}
	else {
		map_delete(render_path_render_targets, s->name);
	}
}

// Fill layer or fill mask l rendered into t (base, normal, pack; a mask: out[0]) for its uv rectangle at t->res_w x
// t->res_h, as layers_update_fill_layer does for the whole layer (without its dilation): the paint shader's uv tile
// transform (paint_exec_uv_tile) maps the rectangle to t. scratch, textures of t's size: [0..2] the layer before as the
// pass reads it (cleared here as slot_layer_clear does), [3..4] for a mask the normal and pack outputs of the pass (the
// formats of the masked layer's). The live layer, and the selected layer and material, stay as they are.
bool layers_render_fill_rect(slot_layer_t *l, paint_target_t *t, gpu_texture_t **scratch) {
	if (l == NULL || l->fill_material == NULL || t->w <= 0 || t->h <= 0) {
		return false;
	}
	bool          mask = slot_layer_is_mask(l);
	slot_layer_t *p    = l;
	if (mask) { // The paint pass writes the normal and pack of the masked layer too
		p = l->parent;
		if (p != NULL && slot_layer_is_group(p)) {
			slot_layer_t_array_t *children = slot_layer_get_children(p);
			p                              = children != NULL && children->length > 0 ? children->buffer[0] : NULL;
		}
		if (p == NULL || p->texpaint_nor == NULL) {
			return false;
		}
	}
	draw_saved_t saved_draw = draw_suspend();

	// The layer before: cleared as slot_layer_clear does, the pass blends over it
	gpu_texture_t *base   = t->out[0];
	gpu_texture_t *nor    = mask ? scratch[3] : t->out[1];
	gpu_texture_t *pack   = mask ? scratch[4] : t->out[2];
	u32            clr[3] = {0x00000000, color_from_floats(0.5, 0.5, 1.0, 0.0), color_from_floats(1.0, layers_default_rough, 0.0, 0.0)};
	gpu_texture_t *out[3] = {base, nor, pack};
	for (i32 k = 0; k < 3; ++k) {
		_gpu_begin(out[k], NULL, NULL, GPU_CLEAR_COLOR, clr[k], 0.0);
		gpu_end();
		_gpu_begin(scratch[k], NULL, NULL, GPU_CLEAR_COLOR, clr[k], 0.0);
		gpu_end();
	}

	layers_fill_swap_t swaps[3];
	layers_fill_swap(&swaps[0], string_tmp("texpaint%d", l->id), base);
	layers_fill_swap(&swaps[1], string_tmp("texpaint_nor%d", p->id), nor);
	layers_fill_swap(&swaps[2], string_tmp("texpaint_pack%d", p->id), pack);
	history_tiles_source_set(scratch[0], scratch[1], scratch[2]); // The fill blends over the cleared scratch

	slot_layer_t    *_layer     = g_context->layer;
	slot_material_t *_material  = g_context->material;
	tool_type_t      _tool      = g_context->tool;
	i32              _fill_type = g_context->fill_type;
	i32              _pdirty    = g_context->pdirty;
	vec4_t           _uv_tile   = paint_exec_uv_tile;
	vec2_t           _res0      = paint_exec_res0;
	g_context->layer            = l;
	g_context->material         = l->fill_material;
	g_context->tool             = TOOL_TYPE_FILL;
	g_context->fill_type        = FILL_TYPE_OBJECT;
	g_context->pdirty           = 1;
	layers_set_object_mask();
	paint_exec_uv_tile = (vec4_t){t->uv_rect[0], t->uv_rect[1], 1.0 / (t->uv_rect[2] - t->uv_rect[0]), 1.0 / (t->uv_rect[3] - t->uv_rect[1])};
	paint_exec_res0    = (vec2_t){(f32)t->res_w, (f32)t->res_h};
	i32 need           = mm_buffers_require(MM_BUFFERS_FULL); // Material Maker buffers full now, as for a bake
	// The stroke coverage targets are attachments of the pass too: at t's size, Metal clips the render area to the
	// smallest attachment (a tile larger than the working resolution would only get its top left part)
	render_target_t *blend   = any_map_get(render_path_render_targets, "texpaint_blend0");
	i32              blend_w = blend->width;
	i32              blend_h = blend->height;
	render_path_paint_scratch_resize(t->w, t->h);

	make_material_parse_paint_material(false);
	render_path_paint_commands_paint(false);
	if (_render_path_current_image != NULL) {
		render_path_end();
	}

	render_path_paint_scratch_resize(blend_w, blend_h);
	mm_buffers_require(need);
	paint_exec_uv_tile   = _uv_tile;
	paint_exec_res0      = _res0;
	g_context->layer     = _layer;
	g_context->material  = _material;
	g_context->tool      = _tool;
	g_context->fill_type = _fill_type;
	g_context->pdirty    = _pdirty;
	history_tiles_source_set(NULL, NULL, NULL);
	for (i32 i = 2; i >= 0; --i) {
		layers_fill_unswap(&swaps[i]);
	}
	layers_set_object_mask();
	make_material_parse_paint_material(false);
	draw_resume(saved_draw);
	return true;
}

// Repaints fill layer l with its own material, the selected layer and material stay
void layers_refresh_fill_layer(slot_layer_t *l) {
	if (l == NULL || l->fill_material == NULL || array_index_of(g_project->_->layers, l) == -1) {
		return;
	}
	slot_layer_t    *_layer    = g_context->layer;
	slot_material_t *_material = g_context->material;
	g_context->layer           = l;
	g_context->material        = l->fill_material;
	layers_set_object_mask();
	layers_update_fill_layer(true);
	g_context->layer    = _layer;
	g_context->material = _material;
	layers_set_object_mask();
	make_material_parse_paint_material(false);
	g_context->ddirty               = 2;
	g_context->rtdirty              = 1;
	g_context->layers_preview_dirty = true;
	render_path_base_redraw();
}

static slot_layer_t *layers_refresh_pending = NULL;

static void layers_refresh_fill_layer_run(void *_) {
	slot_layer_t *l        = layers_refresh_pending;
	layers_refresh_pending = NULL;
	layers_refresh_fill_layer(l);
}

// Once on the next frame, however often a slider moves in between
void layers_refresh_fill_layer_later(slot_layer_t *l) {
	if (layers_refresh_pending != NULL && layers_refresh_pending != l) {
		layers_refresh_fill_layer(layers_refresh_pending);
	}
	if (layers_refresh_pending == NULL) {
		sys_notify_on_next_frame(&layers_refresh_fill_layer_run, NULL);
	}
	layers_refresh_pending = l;
}

void layers_update_linked_layers() {
	render_path_base_redraw(); // The 3D view shows the change
	slot_material_t *_material  = g_context->material;
	bool             any_linked = false;
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m          = g_project->_->materials->buffer[i];
		bool             has_linked = false;
		for (i32 j = 0; j < m->canvas->nodes->length; ++j) {
			ui_node_t *node = m->canvas->nodes->buffer[j];
			if (string_equals(node->type, "LAYER") || string_equals(node->type, "LAYER_MASK")) {
				has_linked = true;
				break;
			}
		}
		if (!has_linked) {
			continue;
		}
		any_linked          = true;
		g_context->material = m;
		make_material_bake_node_previews_linked();
		layers_update_fill_layers();
	}
	g_context->material = _material;
	if (any_linked) {
		make_material_parse_paint_material(false);
	}
}

static void layers_fill_paint_update(void *_) {
	layers_update_fill_layers();
}

// Tools that put the selected material down (the others only shape a mask: eraser, blur)
static bool layers_tool_applies_material(i32 tool) {
	return tool == TOOL_TYPE_BRUSH || tool == TOOL_TYPE_FILL || tool == TOOL_TYPE_PARTICLE;
}

// A fill layer of the selected material above the current layer, for a stroke started on a fill layer of another
// material: the stroke paints the new layer's black mask, so the selected material shows where it is painted
static slot_layer_t *layers_new_fill_layer_for_stroke() {
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *l        = layers_new_layer(false, -1, NULL); // Above the current layer (its fill layer when on a mask)
	if (l == NULL) {
		return NULL;
	}
	tab_layers_remap_all_layer_pointers(pointers);
	history_new_layer();
	layers_history_with_prev(history_to_fill_layer);
	slot_layer_to_fill_layer(l); // Filled with the selected material
	return l;
}

// The fill layer of the current layer (the layer itself, or the layer of its mask), NULL when it is no fill layer
static slot_layer_t *layers_fill_paint_target() {
	slot_layer_t *l = g_context->layer;
	if (l != NULL && slot_layer_is_mask(l) && l->parent != NULL && slot_layer_is_layer(l->parent) && l->parent->fill_material != NULL) {
		return l->parent;
	}
	if (l == NULL || l->fill_material == NULL || !slot_layer_is_layer(l)) {
		return NULL;
	}
	return l;
}

// A stroke would put the selected material down on a fill layer of another material: one fill layer holds one
// material, so the stroke needs a new fill layer (asked with layers_fill_paint_confirm_new_layer)
bool layers_fill_paint_needs_new_layer() {
	slot_layer_t *l = layers_fill_paint_target();
	return l != NULL && l->fill_material != g_context->material && layers_tool_applies_material(g_context->tool);
}

static bool layers_fill_paint_to_mask_of(slot_layer_t *l, bool created);

// A fill layer of the selected material, on a new black mask that the next stroke paints. One undo removes both.
bool layers_fill_paint_to_new_layer() {
	if (layers_fill_paint_target() == NULL) {
		return false;
	}
	slot_layer_t *l = layers_new_fill_layer_for_stroke();
	return l != NULL && layers_fill_paint_to_mask_of(l, true);
}

static void layers_fill_paint_to_new_layer_on_next_frame(void *_) {
	layers_fill_paint_to_new_layer();
}

static void layers_fill_paint_confirm_box() {
	ui_text(tr("Multiple materials cannot be painted on the same fill layer."), UI_ALIGN_LEFT, 0x00000000);
	ui_row2();
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
	if (ui_icon_button(tr("Create new fill layer"), ICON_CHECK, UI_ALIGN_CENTER)) {
		ui_box_hide();
		sys_notify_on_next_frame(&layers_fill_paint_to_new_layer_on_next_frame, NULL); // Not in the middle of drawing the box
	}
}

// Asks to paint the selected material on a new fill layer, instead of the stroke on the current one
void layers_fill_paint_confirm_new_layer() {
	ui_box_show_custom(&layers_fill_paint_confirm_box, 560, 200, NULL, true, tr("Fill Layer"));
}

// A stroke on a fill layer paints its mask: the first paintable mask of the layer, or a new black one. A tool that
// puts another material down than the fill layer's has to ask for a new fill layer first
// (layers_fill_paint_needs_new_layer): nothing is changed for it here. Returns false when the layer is no fill layer
// (nor the mask of one), the stroke needs a new fill layer, or there is no room for a mask.
bool layers_fill_paint_to_mask() {
	slot_layer_t *l = layers_fill_paint_target();
	if (l == NULL || layers_fill_paint_needs_new_layer()) {
		return false;
	}
	if (g_context->layer != l) {
		return false; // The fill layer's own mask, painted as is
	}
	return layers_fill_paint_to_mask_of(l, false);
}

// The first paintable mask of fill layer l (not a fill mask), NULL when it has none
static slot_layer_t *layers_fill_paint_mask(slot_layer_t *l) {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *m = g_project->_->layers->buffer[i];
		if (m->parent == l && slot_layer_is_mask(m) && m->fill_material == NULL) {
			return m;
		}
	}
	return NULL;
}

// Selects the first paintable mask of fill layer l, or a new black one. created: l was just made, its mask's history
// step goes with it.
static bool layers_fill_paint_to_mask_of(slot_layer_t *l, bool created) {
	slot_layer_t *mask = layers_fill_paint_mask(l);
	if (mask != NULL) {
		context_set_layer(mask);
		return true;
	}
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *m        = layers_new_mask(false, l, -1);
	if (m == NULL) {
		return false;
	}
	tab_layers_remap_all_layer_pointers(pointers);
	slot_layer_clear(m, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	g_context->layer_preview_dirty = true;
	if (created) {
		layers_history_with_prev(history_new_black_mask);
	}
	else {
		history_new_black_mask();
	}
	sys_notify_on_next_frame(&layers_fill_paint_update, NULL);
	return true;
}

// A stroke on a fill layer that has no mask to paint: the user picks a black or a white mask first (asked with
// layers_fill_paint_confirm_mask), the stroke is dropped
bool layers_fill_paint_needs_mask() {
	slot_layer_t *l = layers_fill_paint_target();
	return l != NULL && g_context->layer == l && !layers_fill_paint_needs_new_layer() && layers_fill_paint_mask(l) == NULL;
}

static slot_layer_t *layers_fill_mask_layer = NULL; // The fill layer the box asks about
static i32           layers_fill_mask_type  = 0;    // layers_op_new_mask: 0 black, 1 white

static void layers_fill_paint_new_mask_on_next_frame(void *_) {
	slot_layer_t *l        = layers_fill_mask_layer;
	layers_fill_mask_layer = NULL;
	if (slot_layer_alive(l)) {
		layers_op_new_mask(l, layers_fill_mask_type); // Selected, the next stroke paints it
		ui_dock_redraw(-1);
	}
}

static void layers_fill_paint_new_mask(i32 type) {
	ui_box_hide();
	layers_fill_mask_type = type;
	sys_notify_on_next_frame(&layers_fill_paint_new_mask_on_next_frame, NULL); // Not in the middle of drawing the box
}

static void layers_fill_paint_mask_box() {
	ui_text(tr("A fill layer is painted through a mask."), UI_ALIGN_LEFT, 0x00000000);
	ui_text(tr("Black hides the fill until painted, white shows all of it."), UI_ALIGN_LEFT, 0x00000000);
	ui_row3();
	if (ui_icon_button(tr("Create Black Mask"), ICON_CHECK, UI_ALIGN_CENTER)) {
		layers_fill_paint_new_mask(0);
	}
	if (ui_icon_button(tr("Create White Mask"), ICON_CHECK, UI_ALIGN_CENTER)) {
		layers_fill_paint_new_mask(1);
	}
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		layers_fill_mask_layer = NULL;
		ui_box_hide();
	}
}

// Asks for a mask to paint the current fill layer through
void layers_fill_paint_confirm_mask() {
	layers_fill_mask_layer = layers_fill_paint_target();
	ui_box_show_custom(&layers_fill_paint_mask_box, 640, 230, NULL, true, tr("Fill Layer"));
}

// Tools that paint a fill layer through its mask
bool layers_fill_paint_tool(i32 tool) {
	return tool == TOOL_TYPE_BRUSH || tool == TOOL_TYPE_ERASER || tool == TOOL_TYPE_BLUR || tool == TOOL_TYPE_FILL || tool == TOOL_TYPE_PARTICLE;
}

void layers_create_fill_layer_on_next_frame(void *_) {
	slot_layer_t *l = layers_new_layer(false, _layers_position, NULL);
	history_new_layer();
	l->uv_type = _layers_uv_type;
	if (!mat4_isnan(_layers_decal_mat)) {
		l->decal_mat = _layers_decal_mat;
	}
	l->object_mask = g_context->layer_filter;

	layers_history_with_prev(history_to_fill_layer); // One undo removes the new fill layer
	slot_layer_to_fill_layer(l);
}

void layers_create_fill_layer(uv_type_t uv_type, mat4_t decal_mat, i32 position) {
	_layers_uv_type   = uv_type;
	_layers_decal_mat = decal_mat;
	_layers_position  = position;
	sys_notify_on_next_frame(&layers_create_fill_layer_on_next_frame, NULL);
}
