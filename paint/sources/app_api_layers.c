// app_api_layers.h, #included by main.c after app_api.c (it reuses app_layer_by_id and the script_* helpers of minic_impl.c).
// The rules of what applies to which layer are those of the engine's layers tab (ui/tab_layers.c).

#include "app_api_layers.h"
#include "app_api_masks.h"

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

uint32_t app_layers_props_signature(void) {
	uint32_t h = APP_HASH_SEED;
	if (!app_ready()) {
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
		h               = app_hash_fq(h, l->scale);
		h               = app_hash_fq(h, l->angle);
		h               = app_hash_fq(h, l->height_blend_reach);
		h               = app_hash_fq(h, l->height_blend_influence);
		h               = app_hash_fq(h, l->height_blend_sharpness);
		h               = app_hash_fq(h, l->height_blend_offset);
		for (i32 j = 0; slot_layer_is_mask(l) && l->mask_effects != NULL && j < l->mask_effects->length; ++j) {
			h = app_hash_fq(h, l->mask_effects->buffer[j]);
		}
		h = app_hash(h, l->mask_effects != NULL ? l->mask_effects->length : 0);
	}
	h = app_hash(h, mesh_maps_get_asset(MESH_MAP_AO) != NULL); // app_mask_effect_t.needs_ao
	h = app_hash(h, mesh_maps_get_asset(MESH_MAP_THICKNESS) != NULL);
	h = app_hash(h, project_id_map_asset() != NULL);
	// The fill material picker lists the project materials, the UV map picker depends on the mesh
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		h = app_hash_str(h, g_project->_->materials->buffer[i]->canvas->name);
	}
	h = app_hash(h, (uint32_t)(uintptr_t)g_context->paint_object);
	h = app_hash(h, g_context->material != NULL); // app_layer_can_new
	h = app_hash(h, g_context->swatch != NULL);
	return h;
}

static char *app_blending_names[] = {
    "Mix",     "Darken",  "Multiply",     "Burn",       "Lighten",  "Screen", "Dodge",      "Add",   "Overlay",
    "Soft Light", "Linear Light", "Difference", "Subtract", "Divide", "Hue", "Saturation", "Color", "Value",
};

_Static_assert(ARRAY_LENGTH(app_blending_names) == BLEND_TYPE_VALUE + 1, "a name per blend_type_t");

int32_t app_layer_blending_count(void) {
	return (int32_t)ARRAY_LENGTH(app_blending_names);
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
	if (!app_ready() || g_context->layer == NULL) {
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
	case APP_LAYER_NEW_ID_MASK: { // Into the layer's mask, as Painter's mask with color selection
		slot_layer_t *target = app_layer_mask_target(g_context->layer);
		return target != NULL && !slot_layer_is_group(target);
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
		ok = app_layer_add_effect(app_layer_mask_target(l)->id, MASK_EFFECT_ID_SELECTION) >= 0; // Starts picking
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
	return app_ready() ? g_project->_->materials->length : 0;
}

const char *app_layer_material_name(int32_t index) {
	if (index < 0 || index >= app_layer_material_count()) {
		return "";
	}
	return app_str(g_project->_->materials->buffer[index]->canvas->name);
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
	app_drag_begin(APP_DRAG_LAYER_VALUE, id, value, 0);
}

bool app_layer_set_value(int32_t id, int32_t value, float v) {
	slot_layer_t *l   = app_layer_by_id(id);
	f32           min = 0.0;
	f32           max = 0.0;
	f32          *f   =l != NULL ? app_layer_value_field(l, value, &min, &max) : NULL;
	if (f == NULL) {
		return false;
	}
	if (!app_dragging(APP_DRAG_LAYER_VALUE, id, value, 0)) { // A change without a drag: its own undo step
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
