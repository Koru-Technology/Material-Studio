#include "../global.h"

// Fill layers and fill masks of the layer stack (util_fill.c evaluates them): made, filled from a material, painted
// through their mask, and path layers repainted along their points

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

// Materials that read layers (Layer nodes): their previews and fill tiles follow the layers
void layers_update_linked_layers() {
	render_path_base_redraw(); // The 3D view shows the change
	slot_material_t *_material = g_context->material;
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
		g_context->material = m;
		make_material_bake_node_previews_linked();
		fill_tile_invalidate(m);
	}
	g_context->material = _material;
}

// Paints the selected material over the whole of paint layer l (the fill tool on the object), an undoable paint
void layers_fill_with_material(slot_layer_t *l) {
	if (l == NULL || l->fill != NULL || slot_layer_is_group(l)) {
		return;
	}
	draw_saved_t saved_draw = draw_suspend();
	slot_layer_t *_layer     = g_context->layer;
	tool_type_t   _tool      = g_context->tool;
	i32           _fill_type = g_context->fill_type;
	i32           _pdirty    = g_context->pdirty;
	g_context->layer         = l;
	g_context->tool          = TOOL_TYPE_FILL;
	g_context->fill_type     = FILL_TYPE_OBJECT;
	g_context->pdirty        = 1;
	layers_set_object_mask();
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	make_material_parse_paint_material(false);
	render_path_paint_commands_paint(false);
	render_path_paint_dilate(true, true);
	g_context->layer     = _layer;
	g_context->tool      = _tool;
	g_context->fill_type = _fill_type;
	g_context->pdirty    = _pdirty;
	layers_set_object_mask();
	make_material_parse_paint_material(false);
	draw_resume(saved_draw);
}

static void layers_fill_paint_update(void *_) {
	fill_ensure_all();
}

// The fill layer of the current layer (the layer itself, or the layer of its mask), NULL when it is no fill layer
static slot_layer_t *layers_fill_paint_target() {
	slot_layer_t *l = g_context->layer;
	if (l != NULL && slot_layer_is_mask(l) && l->parent != NULL && slot_layer_is_layer(l->parent) && l->parent->fill != NULL) {
		return l->parent;
	}
	if (l == NULL || l->fill == NULL || !slot_layer_is_layer(l)) {
		return NULL;
	}
	return l;
}

static bool layers_fill_paint_to_mask_of(slot_layer_t *l, bool created);

// A stroke on a fill layer paints its mask, as in Painter: the first paintable mask of the layer, or a new black one.
// Returns false when the layer is no fill layer (nor the mask of one) or there is no room for a mask.
bool layers_fill_paint_to_mask() {
	slot_layer_t *l = layers_fill_paint_target();
	if (l == NULL) {
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
		if (m->parent == l && slot_layer_is_mask(m) && m->fill == NULL) {
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
	slot_layer_t_array_t *pointers = layers_init_layer_map();
	slot_layer_t         *m        = layers_new_mask(false, l, -1);
	if (m == NULL) {
		return false;
	}
	layers_remap_layer_pointers(pointers);
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
	return l != NULL && g_context->layer == l && layers_fill_paint_mask(l) == NULL;
}

static slot_layer_t *layers_fill_mask_layer = NULL; // The fill layer the box asks about
static i32           layers_fill_mask_type  = 0;    // layers_op_new_mask: 0 black, 1 white

static void layers_fill_paint_new_mask_on_next_frame(void *_) {
	slot_layer_t *l        = layers_fill_mask_layer;
	layers_fill_mask_layer = NULL;
	if (slot_layer_alive(l)) {
		layers_op_new_mask(l, layers_fill_mask_type); // Selected, the next stroke paints it
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

// The fill a new fill layer gets: the selected material in every channel (from_material), else plain colors, the
// current swatch's as its base color
static fill_t *layers_new_fill(i32 projection, bool from_material) {
	if (from_material && g_context->material != NULL) {
		return fill_from_material(g_context->material, projection);
	}
	fill_t *f = fill_create(projection);
	if (g_context->swatch != NULL) {
		u32 c = g_context->swatch->base;
		f->ch[FILL_CHANNEL_BASE].value[0] = color_get_rb(c) / 255.0;
		f->ch[FILL_CHANNEL_BASE].value[1] = color_get_gb(c) / 255.0;
		f->ch[FILL_CHANNEL_BASE].value[2] = color_get_bb(c) / 255.0;
	}
	return f;
}

// A new fill layer's fill when it is not from a material: plain colors
fill_t *layers_new_fill_layer_fill(void) {
	return layers_new_fill(FILL_PROJECTION_UV, false);
}

// A decal box around the paint object, as large as it: where a new decal or planar projection starts
mat4_t layers_default_decal_box(void) {
	mesh_object_t *o = g_context->paint_object;
	if (o == NULL) {
		return mat4_identity();
	}
	transform_t *t  = o->base->transform;
	f32          sz = math_max(t->dim.x * t->scale.x, math_max(t->dim.y * t->scale.y, t->dim.z * t->scale.z));
	sz              = sz > 0.0 ? sz : 1.0;
	return mat4_compose(t->loc, quat_from_euler(0.0, 0.0, 0.0), (vec4_t){sz, sz, sz, 1.0});
}

static i32  _layers_fill_projection   = FILL_PROJECTION_UV;
static bool _layers_fill_from_material = false;

void layers_create_fill_layer_on_next_frame(void *_) {
	i32           position = _layers_position == LAYERS_POSITION_TOP ? g_project->_->layers->length : _layers_position;
	slot_layer_t *l        = layers_new_layer(false, position, NULL);
	if (l == NULL) {
		return;
	}
	history_new_layer();
	if (!mat4_isnan(_layers_decal_mat)) {
		l->decal_mat = _layers_decal_mat;
	}
	else if (_layers_fill_projection == FILL_PROJECTION_DECAL || _layers_fill_projection == FILL_PROJECTION_PLANAR) {
		l->decal_mat = layers_default_decal_box();
	}
	l->object_mask = g_context->layer_filter;
	slot_layer_to_fill_layer(l, layers_new_fill(_layers_fill_projection, _layers_fill_from_material));
	layers_history_with_prev(history_to_fill_layer); // One undo removes the new fill layer
	// A white mask comes with it, its effects shape the fill. Undone with it, the fill layer stays selected
	i32 before = history_steps->length;
	if (layers_op_new_mask(l, 1) != NULL && history_steps->length > before) {
		history_steps->buffer[history_steps->length - 1]->with_prev = true;
	}
	context_set_layer(l);
}

// A new fill layer on the next frame: of the selected material (from_material) or plain colors
void layers_create_fill_layer(i32 projection, mat4_t decal_mat, i32 position, bool from_material) {
	_layers_fill_projection    = projection;
	_layers_fill_from_material = from_material;
	_layers_decal_mat          = decal_mat;
	_layers_position           = position;
	sys_notify_on_next_frame(&layers_create_fill_layer_on_next_frame, NULL);
}
