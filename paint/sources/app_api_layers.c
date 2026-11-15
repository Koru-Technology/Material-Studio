// app_api_layers.h, #included by main.c after app_api.c (it reuses app_layer_by_id and the script_* helpers of minic_impl.c).
// The rules of what applies to which layer are those of the engine's layers tab (ui/tab_layers.c).

#include "app_api_layers.h"

// The first mask of l in the list: masks sit below their layer in the stack, the highest one shows first
static slot_layer_t *app_layer_first_mask(slot_layer_t *l) {
	if (slot_layer_is_mask(l)) {
		return NULL;
	}
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, false);
	return masks != NULL ? masks->buffer[masks->length - 1] : NULL;
}

static uint32_t app_layer_channels(slot_layer_t *l) {
	bool ch[APP_LAYER_CHANNEL_COUNT] = {
	    l->paint_base,  l->paint_opac,         l->paint_nor,  l->paint_nor_blend, l->paint_rough, l->paint_met,
	    l->paint_height, l->paint_height_blend, l->paint_emis, l->paint_subs,      l->paint_occ,
	};
	uint32_t bits = 0;
	for (i32 i = 0; i < APP_LAYER_CHANNEL_COUNT; ++i) {
		if (ch[i]) {
			bits |= 1u << i;
		}
	}
	return bits;
}

static bool *app_layer_channel_field(slot_layer_t *l, i32 channel) {
	switch (channel) {
	case APP_LAYER_CHANNEL_BASE:
		return &l->paint_base;
	case APP_LAYER_CHANNEL_OPACITY:
		return &l->paint_opac;
	case APP_LAYER_CHANNEL_NORMAL:
		return &l->paint_nor;
	case APP_LAYER_CHANNEL_NORMAL_BLEND:
		return &l->paint_nor_blend;
	case APP_LAYER_CHANNEL_ROUGHNESS:
		return &l->paint_rough;
	case APP_LAYER_CHANNEL_METALLIC:
		return &l->paint_met;
	case APP_LAYER_CHANNEL_HEIGHT:
		return &l->paint_height;
	case APP_LAYER_CHANNEL_HEIGHT_BLEND:
		return &l->paint_height_blend;
	case APP_LAYER_CHANNEL_EMISSION:
		return &l->paint_emis;
	case APP_LAYER_CHANNEL_SUBSURFACE:
		return &l->paint_subs;
	case APP_LAYER_CHANNEL_OCCLUSION:
		return &l->paint_occ;
	}
	return NULL;
}

static bool app_mesh_has_uv1(void) {
	return g_context->paint_object != NULL && g_context->paint_object->data != NULL &&
	       mesh_data_get_vertex_array(g_context->paint_object->data, "tex1") != NULL;
}

// The layer the "+" menu adds masks, filters and ID masks to: a mask's or filter's layer
static slot_layer_t *app_layer_mask_target(slot_layer_t *l) {
	return l != NULL && (slot_layer_is_mask(l) || slot_layer_is_filter(l)) ? l->parent : l;
}

bool app_layer_props(int32_t id, app_layer_props_t *out) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return false;
	}
	bool group  = slot_layer_is_group(l);
	bool mask   = slot_layer_is_mask(l);
	bool filter = slot_layer_is_filter(l);
	bool layer  = slot_layer_is_layer(l);
	bool fill   = l->fill_material != NULL || l->path_material != NULL;

	slot_layer_t *first_mask = app_layer_first_mask(l);
	slot_layer_t *target     = app_layer_mask_target(g_context->layer);

	*out = (app_layer_props_t){
	    .id                     = l->id,
	    .has_children           = tab_layers_has_children(l) && !mask, // A mask's effects list in its settings, not as rows
	    .expanded               = l->show_panel,
	    .is_path                = slot_layer_is_path(l),
	    .is_text                = l->path_text,
	    .is_decal               = l->fill_material != NULL && l->uv_type == UV_TYPE_PROJECT,
	    .mask_id                = first_mask != NULL ? first_mask->id : -1,
	    .blending               = l->blending,
	    .uv_type                = l->uv_type,
	    .uv_map                 = l->uv_map,
	    .has_uv1                = app_mesh_has_uv1(),
	    .scale                  = l->scale,
	    .angle                  = l->angle,
	    .fill_material          = l->fill_material != NULL ? array_index_of(g_project->_->materials, l->fill_material) : -1,
	    .channels               = app_layer_channels(l),
	    .height_blend           = l->height_blend,
	    .height_blend_below     = l->height_blend_below,
	    .height_blend_reach     = l->height_blend_reach,
	    .height_blend_influence = l->height_blend_influence,
	    .height_blend_sharpness = l->height_blend_sharpness,
	    .height_blend_offset    = l->height_blend_offset,
	    .effect_count           = app_mask_effect_count(l->id),

	    .shows_blending            = !group && !mask && !filter,
	    .shows_channels            = !group,
	    .shows_to_fill             = !group && !fill,
	    .shows_to_paint            = !group && fill,
	    .shows_clear               = l->fill_material == NULL,
	    .shows_invert              = mask,
	    .shows_apply               = mask,
	    .shows_add_mask            = slot_layer_takes_mask(l),
	    .shows_add_effect          = layer || group || mask,
	    .shows_merge_group         = group,
	    .shows_save_smart_material = group || (layer && l->fill_material != NULL),

	    .can_delete      = tab_layers_can_delete(l),
	    .can_merge_down  = tab_layers_can_merge_down(l),
	    .can_merge_group = group && slot_layer_get_children(l) != NULL && (slot_layer_shows(l) || tab_layers_can_delete(l)),
	    .can_id_mask     = target != NULL && !slot_layer_is_group(target),
	};
	return true;
}

static uint32_t app_hash_f(uint32_t h, float v) {
	return app_hash(h, (uint32_t)(int32_t)(v * 10000.0f));
}

uint32_t app_layers_props_signature(void) {
	uint32_t h = 2166136261u;
	if (!app_engine_ready || g_project == NULL || g_project->_ == NULL) {
		return h;
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		h               = app_hash(h, (uint32_t)l->id);
		h               = app_hash(h, l->show_panel);
		h               = app_hash(h, (uint32_t)l->blending);
		h               = app_hash(h, (uint32_t)l->uv_type);
		h               = app_hash(h, (uint32_t)l->uv_map);
		h               = app_hash(h, (uint32_t)array_index_of(g_project->_->materials, l->fill_material));
		h               = app_hash(h, l->path_material != NULL);
		h               = app_hash(h, l->path_text);
		h               = app_hash(h, app_layer_channels(l));
		h               = app_hash(h, l->height_blend | (l->height_blend_below << 1));
		h               = app_hash_f(h, l->scale);
		h               = app_hash_f(h, l->angle);
		h               = app_hash_f(h, l->height_blend_reach);
		h               = app_hash_f(h, l->height_blend_influence);
		h               = app_hash_f(h, l->height_blend_sharpness);
		h               = app_hash_f(h, l->height_blend_offset);
		for (i32 j = 0; slot_layer_is_mask(l) && l->mask_effects != NULL && j < l->mask_effects->length; ++j) {
			h = app_hash_f(h, l->mask_effects->buffer[j]);
		}
		h = app_hash(h, l->mask_effects != NULL ? l->mask_effects->length : 0);
	}
	h = app_hash(h, mesh_maps_get_asset(MESH_MAP_AO) != NULL); // app_mask_effect_t.needs_ao
	// The fill material picker lists the project materials, the UV map picker depends on the mesh
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		h = app_hash_str(h, g_project->_->materials->buffer[i]->canvas->name);
	}
	h = app_hash(h, (uint32_t)(uintptr_t)g_context->paint_object);
	h = app_hash(h, g_context->material != NULL);
	return h;
}

static char *app_blending_names[] = {
    "Mix",     "Darken",  "Multiply",     "Burn",       "Lighten",  "Screen", "Dodge",      "Add",   "Overlay",
    "Soft Light", "Linear Light", "Difference", "Subtract", "Divide", "Hue", "Saturation", "Color", "Value",
};

int32_t app_layer_blending_count(void) {
	return sizeof(app_blending_names) / sizeof(app_blending_names[0]);
}

const char *app_layer_blending_name(int32_t blending) {
	if (blending < 0 || blending >= app_layer_blending_count()) {
		return NULL;
	}
	return tr(app_blending_names[blending]);
}

bool app_layer_pick(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return false;
	}
	script_layer_set(l);
	if (l->fill_material != NULL) {
		context_set_material(l->fill_material);
	}
	else if (l->path_material != NULL) {
		context_set_material(l->path_material);
	}
	iron_delay_idle_sleep();
	return true;
}

bool app_layer_set_expanded(int32_t id, bool expanded) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return false;
	}
	l->show_panel = expanded;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	return true;
}

bool app_layer_set_name(int32_t id, const char *name) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || name == NULL || name[0] == '\0' || string_equals(l->name, (char *)name)) {
		return false;
	}
	char *prev = string_copy(l->name);
	l->name    = string_copy(slot_layer_unique_name(l, (char *)name));
	history_layer_name(l, prev);
	if (l->path_text && slot_layer_is_path(l)) { // The name is the painted text
		gpu_texture_t *current;
		bool           in_use;
		script_gpu_begin(&current, &in_use);
		tab_layers_repaint_text_layer(l);
		script_layer_op_end(current, in_use);
	}
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	iron_delay_idle_sleep();
	return true;
}

// New layers

bool app_layer_can_new(int32_t kind) {
	if (!app_engine_ready || g_context == NULL || g_context->layer == NULL) {
		return false;
	}
	switch (kind) {
	case APP_LAYER_NEW_FILL:
	case APP_LAYER_NEW_DECAL:
	case APP_LAYER_NEW_TEXT:
	case APP_LAYER_NEW_PATH:
	case APP_LAYER_NEW_CURVE:
		return g_context->material != NULL;
	case APP_LAYER_NEW_FILL_MASK:
		return g_context->material != NULL && slot_layer_takes_mask(app_layer_mask_target(g_context->layer));
	case APP_LAYER_NEW_COLOR:
		return g_context->swatch != NULL;
	case APP_LAYER_NEW_BLACK_MASK:
	case APP_LAYER_NEW_WHITE_MASK:
	case APP_LAYER_NEW_HEIGHT_MASK:
		return slot_layer_takes_mask(app_layer_mask_target(g_context->layer));
	case APP_LAYER_NEW_ID_MASK: {
		slot_layer_t *target = app_layer_mask_target(g_context->layer);
		return slot_layer_takes_mask(target) && !slot_layer_is_group(target);
	}
	}
	return kind >= APP_LAYER_NEW_PAINT && kind <= APP_LAYER_NEW_ID_MASK;
}

static slot_layer_t *app_layer_new_mask(slot_layer_t *l, i32 kind) {
	switch (kind) {
	case APP_LAYER_NEW_BLACK_MASK:
		return script_layer_new_mask(l, 0);
	case APP_LAYER_NEW_WHITE_MASK:
		return script_layer_new_mask(l, 1);
	case APP_LAYER_NEW_FILL_MASK:
		return script_layer_new_mask(l, 2);
	case APP_LAYER_NEW_HEIGHT_MASK: { // Paint black where the layer should go, tall details reach past the edge
		slot_layer_t *m = script_layer_new_mask(l, 1);
		if (m != NULL) {
			m->height_blend           = true;
			make_material_height_used = true;
			make_material_parse_mesh_material();
		}
		return m;
	}
	}
	return NULL;
}

bool app_layer_new(int32_t kind) {
	if (!app_layer_can_new(kind)) {
		return false;
	}
	slot_layer_t *l = g_context->layer;
	bool          ok = true;
	switch (kind) {
	case APP_LAYER_NEW_PAINT:
		ok = script_layer_new() != NULL;
		break;
	case APP_LAYER_NEW_FILL:
		layers_create_fill_layer(UV_TYPE_UVMAP, mat4_nan(), -1);
		break;
	case APP_LAYER_NEW_DECAL:
		layers_create_fill_layer(UV_TYPE_PROJECT, mat4_nan(), -1);
		break;
	case APP_LAYER_NEW_COLOR: {
		swatch_color_t *s = g_context->swatch;
		layers_create_color_layer(color_set_ab(s->base, s->opacity * 255), s->occlusion, s->roughness, s->metallic, -1);
		break;
	}
	case APP_LAYER_NEW_TEXT: {
		gpu_texture_t *current;
		bool           in_use;
		script_gpu_begin(&current, &in_use);
		base_init_undo_layers();
		ok = layers_new_text_layer() != NULL;
		if (ok) {
			history_new_layer();
		}
		script_layer_op_end(current, in_use);
		break;
	}
	case APP_LAYER_NEW_PATH:
	case APP_LAYER_NEW_CURVE:
		ok = script_layer_new_path(kind == APP_LAYER_NEW_CURVE) != NULL;
		break;
	case APP_LAYER_NEW_GROUP:
		ok = script_layer_new_group(l) != NULL;
		break;
	case APP_LAYER_NEW_FILTER:
		context_set_layer(app_layer_mask_target(l));
		layers_create_filter();
		break;
	case APP_LAYER_NEW_ID_MASK:
		ui_id_mask_begin();
		break;
	default:
		ok = app_layer_new_mask(l, kind) != NULL;
		break;
	}
	g_context->ddirty = 2;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	iron_delay_idle_sleep();
	return ok;
}

// Layer menu

static bool app_layer_done(bool ok) {
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	iron_delay_idle_sleep();
	return ok;
}

bool app_layer_duplicate(int32_t id) {
	return app_layer_done(script_layer_duplicate(app_layer_by_id(id)) != NULL);
}

bool app_layer_merge_down(int32_t id) {
	return app_layer_done(script_layer_merge_down(app_layer_by_id(id)) != NULL);
}

bool app_layer_merge_group(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || !slot_layer_is_group(l) || slot_layer_get_children(l) == NULL || !(slot_layer_shows(l) || tab_layers_can_delete(l))) {
		return false;
	}
	return app_layer_done(script_layer_merge_group(l) != NULL);
}

bool app_layer_clear(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || l->fill_material != NULL) {
		return false;
	}
	script_layer_clear(l);
	return app_layer_done(true);
}

bool app_layer_to_fill(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || l->fill_material != NULL || l->path_material != NULL) { // Not a material swap, see script_layer_to_fill
		return false;
	}
	return app_layer_done(script_layer_to_fill(l));
}

bool app_layer_to_paint(int32_t id) {
	return app_layer_done(script_layer_to_paint(app_layer_by_id(id)));
}

bool app_layer_invert_mask(int32_t id) {
	return app_mask_add_effect(id, APP_MASK_EFFECT_INVERT) >= 0;
}

bool app_layer_apply_mask(int32_t id) {
	return app_layer_done(script_layer_apply_mask(app_layer_by_id(id)) != NULL);
}

bool app_layer_add_mask(int32_t id, int32_t kind) {
	slot_layer_t *l = app_layer_by_id(id);
	if (!slot_layer_takes_mask(l) || kind < APP_LAYER_NEW_BLACK_MASK || kind > APP_LAYER_NEW_FILL_MASK) {
		return false;
	}
	if (kind == APP_LAYER_NEW_FILL_MASK && g_context->material == NULL) {
		return false;
	}
	return app_layer_done(app_layer_new_mask(l, kind) != NULL);
}

void app_layer_export(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return;
	}
	context_set_layer(l);
	tab_layers_l = l;
	if (slot_layer_is_mask(l)) {
		ui_files_show("png", true, false, &tab_layers_draw_layer_context_menu_export_on_file_picked);
	}
	else {
		g_context->layers_export = EXPORT_MODE_SELECTED;
		box_export_tab           = 0;
		box_export_show_textures();
	}
	iron_delay_idle_sleep();
}

void app_layer_save_smart_material(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return;
	}
	context_set_layer(l);
	tab_smart_materials_show_save();
	iron_delay_idle_sleep();
}

// Order

// The parent a layer directly below upper gets, as slot_layer_move_target decides
static slot_layer_t *app_layer_parent_below(slot_layer_t *upper) {
	if (upper == NULL) {
		return NULL;
	}
	slot_layer_t *collapsed = NULL;
	for (slot_layer_t *p = upper; p != NULL; p = p->parent) {
		if (!p->show_panel && slot_layer_count_descendants(p) > 0) {
			collapsed = p;
		}
	}
	if (collapsed != NULL) {
		return collapsed->parent;
	}
	if (slot_layer_is_group(upper)) {
		return upper;
	}
	if (slot_layer_is_layer_mask(upper) || slot_layer_is_filter(upper)) {
		return upper->parent->parent;
	}
	return upper->parent;
}

// A layer that stays where it is in the stack and changes group: below the last child of a group, into it,
// or the last child out of it. One undo step, as slot_layer_move.
static bool app_layer_can_reparent(slot_layer_t *l, slot_layer_t *parent) {
	return !slot_layer_is_mask(l) && !slot_layer_is_filter(l) && parent != l->parent && (parent == NULL || slot_layer_is_group(parent)) &&
	       (parent == NULL || !slot_layer_is_descendant(parent, l));
}

static bool app_layer_reparent(slot_layer_t *l, slot_layer_t *parent) {
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_t_array_t *pointers   = tab_layers_init_layer_map();
	slot_layer_t         *old_parent = l->parent;
	context_set_layer(l);
	history_order_layers(array_index_of(g_project->_->layers, l));
	l->parent = parent;
	if (old_parent != NULL && slot_layer_is_group(old_parent) && slot_layer_get_children(old_parent) == NULL) {
		history_delete_layer(old_parent)->with_prev = true;
	}
	context_set_layer(l);
	tab_layers_remap_all_layer_pointers(pointers);
	script_layer_op_end(current, in_use);
	return true;
}

// The script_layer_move index that puts l directly below upper (NULL: the top), see slot_layer_move_target.
// *in_place: l is there already, only its group changes.
static bool app_layer_move_index(slot_layer_t *l, slot_layer_t *upper, i32 *to, bool *in_place) {
	i32 n     = g_project->_->layers->length;
	i32 from  = array_index_of(g_project->_->layers, l);
	*in_place = false;
	if (upper == l || (upper != NULL && slot_layer_is_descendant(upper, l))) {
		return false;
	}
	if (upper == NULL) {
		*to = n - 1;
	}
	else {
		i32 u = array_index_of(g_project->_->layers, upper);
		*to   = u > from ? u - 1 : u;
	}
	if (*to == from) {
		*in_place = true;
		return app_layer_can_reparent(l, app_layer_parent_below(upper));
	}
	return slot_layer_can_move(l, *to);
}

bool app_layer_can_move_below(int32_t id, int32_t upper_id) {
	slot_layer_t *l     = app_layer_by_id(id);
	slot_layer_t *upper = upper_id >= 0 ? app_layer_by_id(upper_id) : NULL;
	i32           to;
	bool          in_place;
	return l != NULL && (upper_id < 0 || upper != NULL) && app_layer_move_index(l, upper, &to, &in_place);
}

bool app_layer_move_below(int32_t id, int32_t upper_id) {
	slot_layer_t *l     = app_layer_by_id(id);
	slot_layer_t *upper = upper_id >= 0 ? app_layer_by_id(upper_id) : NULL;
	i32           to;
	bool          in_place;
	if (l == NULL || (upper_id >= 0 && upper == NULL) || !app_layer_move_index(l, upper, &to, &in_place)) {
		return false;
	}
	return app_layer_done(in_place ? app_layer_reparent(l, app_layer_parent_below(upper)) : script_layer_move(l, to));
}

bool app_layer_set_group(int32_t id, int32_t group_id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		return false;
	}
	if (group_id >= 0) {
		slot_layer_t *group = app_layer_by_id(group_id);
		if (group == NULL || !slot_layer_is_group(group) || l->parent == group || l == group || slot_layer_is_descendant(group, l)) {
			return false;
		}
		return app_layer_done(script_layer_set_group(l, group));
	}
	// Out of its group, below it
	slot_layer_t *group = l->parent;
	if (group == NULL || !slot_layer_is_group(group)) {
		return false;
	}
	slot_layer_t_array_t *layers = g_project->_->layers;
	i32                   from   = array_index_of(layers, l);
	i32                   gi     = array_index_of(layers, group);
	i32                   gstart = gi - slot_layer_count_descendants(group);
	if (from - slot_layer_count_descendants(l) == gstart) { // The lowest in the group already
		return app_layer_done(app_layer_reparent(l, group->parent));
	}
	// As if dropped below the group closed
	bool show_panel   = group->show_panel;
	group->show_panel = false;
	bool ok           = script_layer_move(l, gi - 1 != from ? gi - 1 : gstart);
	group->show_panel = show_panel;
	return app_layer_done(ok && l->parent != group);
}

// Properties

bool app_layer_set_blending(int32_t id, int32_t blending) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || blending < 0 || blending >= app_layer_blending_count() || (i32)l->blending == blending) {
		return false;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	context_set_layer(l);
	history_layer_blending();
	l->blending = blending;
	script_layer_op_end(current, in_use);
	return app_layer_done(true);
}

bool app_layer_set_channel(int32_t id, int32_t channel, bool on) {
	slot_layer_t *l = app_layer_by_id(id);
	bool         *f = l != NULL && !slot_layer_is_group(l) ? app_layer_channel_field(l, channel) : NULL;
	if (f == NULL || *f == on) {
		return false;
	}
	*f = on;
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	script_layer_op_end(current, in_use);
	return app_layer_done(true);
}

bool app_layer_set_uv_map(int32_t id, int32_t uv_map) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || slot_layer_is_group(l) || uv_map < 0 || uv_map > (app_mesh_has_uv1() ? 1 : 0) || l->uv_map == uv_map) {
		return false;
	}
	l->uv_map = uv_map;
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	make_material_parse_paint_material(true);
	script_layer_op_end(current, in_use);
	return app_layer_done(true);
}

// Fill layer edits take history for l and leave the selection alone, as the layers tab
static void app_layer_history_for(slot_layer_t *l, void (*history)(void)) {
	slot_layer_t *_layer = g_context->layer;
	g_context->layer     = l;
	history();
	g_context->layer = _layer;
}

bool app_layer_set_uv_type(int32_t id, int32_t uv_type) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || l->fill_material == NULL || uv_type < UV_TYPE_UVMAP || uv_type > UV_TYPE_PROJECT || (i32)l->uv_type == uv_type) {
		return false;
	}
	app_layer_history_for(l, history_layer_uv_type);
	l->uv_type = uv_type;
	layers_refresh_fill_layer_later(l);
	return app_layer_done(true);
}

int32_t app_layer_material_count(void) {
	return g_project != NULL && g_project->_ != NULL ? g_project->_->materials->length : 0;
}

const char *app_layer_material_name(int32_t index) {
	if (index < 0 || index >= app_layer_material_count()) {
		return "";
	}
	return g_project->_->materials->buffer[index]->canvas->name;
}

bool app_layer_set_fill_material(int32_t id, int32_t material_index) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || l->fill_material == NULL || material_index < 0 || material_index >= app_layer_material_count()) {
		return false;
	}
	slot_material_t *m = g_project->_->materials->buffer[material_index];
	if (m == l->fill_material) {
		return false;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_set_fill_material(l, m);
	script_layer_op_end(current, in_use);
	return app_layer_done(true);
}

static void app_layer_height_blend_changed(slot_layer_t *m) {
	if (m->height_blend) {
		make_material_height_used = true; // Track the height of the layers below
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	script_layer_op_end(current, in_use);
	if (ui_view2d_show) {
		ui_view2d_hwnd->redraws = 2;
	}
}

static bool app_layer_set_mask_flag(int32_t id, bool on, bool below) {
	slot_layer_t *m = app_layer_by_id(id);
	if (m == NULL || !slot_layer_is_mask(m)) {
		return false;
	}
	bool *f = below ? &m->height_blend_below : &m->height_blend;
	if (*f == on) {
		return false;
	}
	context_set_layer(m);
	history_mask_height_blend();
	*f = on;
	app_layer_height_blend_changed(m);
	return app_layer_done(true);
}

bool app_layer_set_height_blend(int32_t id, bool on) {
	return app_layer_set_mask_flag(id, on, false);
}

bool app_layer_set_height_blend_below(int32_t id, bool on) {
	return app_layer_set_mask_flag(id, on, true);
}

// Slider drags

static slot_layer_t *app_value_layer = NULL; // Layer and value of the drag in progress
static i32           app_value_kind  = -1;

static f32 *app_layer_value_field(slot_layer_t *l, i32 value, f32 *min, f32 *max) {
	bool mask = slot_layer_is_mask(l);
	switch (value) {
	case APP_LAYER_VALUE_SCALE:
		*min = 0.0;
		*max = 5.0;
		return l->fill_material != NULL ? &l->scale : NULL;
	case APP_LAYER_VALUE_ANGLE:
		*min = 0.0;
		*max = 360.0;
		return l->fill_material != NULL ? &l->angle : NULL;
	case APP_LAYER_VALUE_HEIGHT_BLEND_REACH:
		*min = 0.0;
		*max = 0.1;
		return mask ? &l->height_blend_reach : NULL;
	case APP_LAYER_VALUE_HEIGHT_BLEND_INFLUENCE:
		*min = 0.0;
		*max = 4.0;
		return mask ? &l->height_blend_influence : NULL;
	case APP_LAYER_VALUE_HEIGHT_BLEND_SHARPNESS:
		*min = 1.0;
		*max = 50.0;
		return mask ? &l->height_blend_sharpness : NULL;
	case APP_LAYER_VALUE_HEIGHT_BLEND_OFFSET:
		*min = -1.0;
		*max = 1.0;
		return mask ? &l->height_blend_offset : NULL;
	}
	return NULL;
}

void app_layer_value_begin(int32_t id, int32_t value) {
	slot_layer_t *l   = app_layer_by_id(id);
	f32           min = 0.0;
	f32           max = 0.0;
	if (l == NULL || app_layer_value_field(l, value, &min, &max) == NULL) {
		return;
	}
	if (value == APP_LAYER_VALUE_SCALE) {
		app_layer_history_for(l, history_layer_scale);
	}
	else if (value == APP_LAYER_VALUE_ANGLE) {
		app_layer_history_for(l, history_layer_angle);
	}
	else {
		context_set_layer(l);
		history_mask_height_blend();
	}
	app_value_layer = l;
	app_value_kind  = value;
}

bool app_layer_set_value(int32_t id, int32_t value, float v) {
	slot_layer_t *l   = app_layer_by_id(id);
	f32           min = 0.0;
	f32           max = 0.0;
	f32          *f   =l != NULL ? app_layer_value_field(l, value, &min, &max) : NULL;
	if (f == NULL) {
		return false;
	}
	if (l != app_value_layer || value != app_value_kind) { // A change without a drag: its own undo step
		app_layer_value_begin(id, value);
	}
	*f = math_max(min, math_min(max, v));
	if (value == APP_LAYER_VALUE_SCALE || value == APP_LAYER_VALUE_ANGLE) {
		layers_refresh_fill_layer_later(l);
	}
	else {
		app_layer_height_blend_changed(l);
	}
	iron_delay_idle_sleep();
	return true;
}

void app_layer_value_end(void) {
	app_value_layer = NULL;
	app_value_kind  = -1;
}

bool app_layer_expanded(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && l->show_panel;
}

int32_t app_layer_uv_type(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL ? (int32_t)l->uv_type : -1;
}

// Mask effects. The stack is listed bottom first in slot_layer_t.mask_effects, the API counts from the top.

_Static_assert((i32)APP_MASK_EFFECT_DIRECTIONAL == (i32)MASK_EFFECT_DIRECTIONAL && APP_MASK_EFFECT_PARAMS == MASK_EFFECT_PARAMS &&
                   APP_MASK_NOISE_FIRST == MASK_EFFECT_NOISE_SCALE && (i32)APP_MASK_PARAM_SEED == (i32)MASK_PARAM_SEED,
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
	    .type         = e.type,
	    .visible      = e.visible,
	    .opacity      = e.opacity,
	    .blending     = e.blending,
	    .is_paint     = e.type == MASK_EFFECT_PAINT,
	    .is_filter    = mask_effect_is_filter(e.type),
	    .is_generator = mask_effect_is_generator(e.type),
	    .needs_ao     = e.type == MASK_EFFECT_DIRT && mesh_maps_get_asset(MESH_MAP_AO) == NULL,
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
	    .name    = tr(ps[p].name),
	    .index   = ps[p].index,
	    .kind    = ps[p].kind,
	    .min     = ps[p].min,
	    .max     = ps[p].max,
	    .def     = ps[p].def,
	    .options = ps[p].options != NULL ? ps[p].options : "",
	};
	return true;
}

static char *app_mask_blending_names[] = {"Normal", "Multiply", "Add", "Subtract", "Max", "Min", "Overlay"};

int32_t app_mask_blending_count(void) {
	return MASK_BLEND_COUNT;
}

const char *app_mask_blending_name(int32_t blending) {
	return blending >= 0 && blending < MASK_BLEND_COUNT ? tr(app_mask_blending_names[blending]) : NULL;
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

// Slider drags

static slot_layer_t *app_mask_value_mask  = NULL; // Effect and field of the drag in progress
static i32           app_mask_value_index = -1;
static i32           app_mask_value_field = -2;

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
	app_mask_value_mask  = m;
	app_mask_value_index = i;
	app_mask_value_field = field;
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
	bool drag = m == app_mask_value_mask && i == app_mask_value_index && field == app_mask_value_field;
	if (!drag) { // A change without a drag: its own undo step
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

void app_mask_effect_value_end(void) {
	app_mask_value_mask  = NULL;
	app_mask_value_index = -1;
	app_mask_value_field = -2;
}

int32_t app_mask_effect_type(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) ? e.type : -1;
}

bool app_mask_effect_visible(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) && e.visible;
}

int32_t app_mask_effect_blending(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) ? e.blending : -1;
}

float app_mask_effect_value(int32_t id, int32_t index, int32_t field) {
	app_mask_effect_t e;
	if (!app_mask_effect(id, index, &e) || field < -1 || field >= APP_MASK_EFFECT_PARAMS) {
		return 0.0;
	}
	return field < 0 ? e.opacity : e.params[field];
}

bool app_mask_effect_needs_ao(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) && e.needs_ao;
}

uint32_t app_layer_previews_version(void) {
	return app_previews_counter;
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
