// app_api_layers.h, #included by main.c after app_api.c (it reuses app_layer_by_id and the script_* helpers of minic_impl.c).
// The rules of what applies to which layer are in util/util_layer_ops.c.

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
	bool fill   = l->fill != NULL || l->path_material != NULL;

	slot_layer_t *first_mask = app_layer_first_mask(l);
	slot_layer_t *target     = app_layer_mask_target(g_context->layer);

	*out = (app_layer_props_t){
	    .id                     = l->id,
	    .has_children           = layers_has_children(l) && !mask, // A mask's effects list in its settings, not as rows
	    .expanded               = l->show_panel,
	    .is_path                = slot_layer_is_path(l),
	    .is_text                = l->path_text,
	    .is_decal               = l->fill != NULL && (l->fill->projection == FILL_PROJECTION_DECAL || l->fill->projection == FILL_PROJECTION_PLANAR),
	    .is_fill                = l->fill != NULL,
	    .mask_id                = first_mask != NULL ? first_mask->id : -1,
	    .blending               = l->blending,
	    .uv_map                 = l->uv_map,
	    .has_uv1                = app_mesh_has_uv1(),
	    .channels               = app_layer_channels(l),
	    .height_raise           = l->height_raise,
	    .effect_count           = app_mask_effect_count(l->id),

	    .shows_blending            = !group && !mask && !filter,
	    .shows_channels            = !group,
	    .shows_raise               = layer && l->paint_height,
	    .shows_to_fill             = layer && !fill,
	    .shows_to_paint            = !group && fill,
	    .shows_clear               = l->fill == NULL,
	    .shows_fill_paint          = mask && l->fill == NULL,
	    .shows_invert              = mask,
	    .shows_apply               = mask,
	    .shows_add_mask            = slot_layer_takes_mask(l),
	    .shows_add_effect          = layer || group || mask,
	    .shows_merge_group         = group,
	    .shows_save_smart_material = group || (layer && l->fill != NULL),

	    .can_delete      = layers_can_delete(l),
	    .can_merge_down  = layers_can_merge_down(l),
	    .can_merge_group = group && slot_layer_get_children(l) != NULL && (slot_layer_shows(l) || layers_can_delete(l)),
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
		h               = app_hash(h, (uint32_t)l->uv_map);
		h               = app_hash(h, l->fill != NULL);
		if (l->fill != NULL) { // Every value of the fill: the panel shows them
			h = app_hash(h, (uint32_t)fill_shape(l));
			h = app_hash(h, (uint32_t)(fill_shape(l) >> 32));
			const f32 *v = (const f32 *)l->fill;
			for (i32 k = 0; k < (i32)(sizeof(fill_t) / sizeof(f32)); ++k) {
				h = app_hash_fq(h, v[k]);
			}
		}
		h               = app_hash(h, l->path_material != NULL);
		h               = app_hash(h, l->path_text);
		h               = app_hash(h, app_layer_channels(l));
		h               = app_hash_fq(h, l->height_raise);
		for (i32 j = 0; slot_layer_is_mask(l) && l->mask_effects != NULL && j < l->mask_effects->length; ++j) {
			h = app_hash_fq(h, l->mask_effects->buffer[j]);
		}
		h = app_hash(h, l->mask_effects != NULL ? l->mask_effects->length : 0);
	}
	h = app_hash(h, mesh_maps_get_asset(MESH_MAP_AO) != NULL); // app_mask_effect_t.needs_ao
	h = app_hash(h, mesh_maps_get_asset(MESH_MAP_THICKNESS) != NULL);
	h = app_hash(h, project_id_map_asset() != NULL);
	for (i32 i = 0; i < g_project->_->assets->length; ++i) { // The image pickers of fill effects and fill layers
		h = app_hash_str(h, g_project->_->assets->buffer[i]->name);
	}
	// The fill pickers list the project materials, the UV map picker depends on the mesh
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		h = app_hash_str(h, g_project->_->materials->buffer[i]->canvas->name);
	}
	h = app_hash(h, (uint32_t)(uintptr_t)g_context->paint_object);
	h = app_hash(h, g_context->material != NULL); // app_layer_can_new
	h = app_hash(h, g_context->swatch != NULL);
	return h;
}

int32_t app_layer_blending_count(void) {
	return BLEND_TYPE_COUNT;
}

const char *app_layer_blending_name(int32_t blending) {
	return blending >= 0 && blending < BLEND_TYPE_COUNT ? tr(blend_type_name(blending)) : "";
}

bool app_layer_pick(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return false;
	}
	layers_op_set(l);
	if (fill_main_material(l->fill) != NULL) {
		context_set_material(fill_main_material(l->fill)); // The material editor edits what fills it
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
		draw_saved_t gpu = draw_suspend();
		layers_repaint_text(l);
		layers_op_end(gpu);
	}
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
	case APP_LAYER_NEW_COLOR:
		return g_context->swatch != NULL;
	case APP_LAYER_NEW_BLACK_MASK:
	case APP_LAYER_NEW_WHITE_MASK:
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
		return layers_op_new_mask(l, 0);
	case APP_LAYER_NEW_WHITE_MASK:
		return layers_op_new_mask(l, 1);
	}
	return NULL;
}

bool app_layer_new(int32_t kind) {
	if (!app_layer_can_new(kind)) {
		return false;
	}
	slot_layer_t *l = g_context->layer;
	bool          ok = true;
	if (context_decal_mat != NULL && g_context->material == context_decal_mat) {
		g_context->material = context_user_material(); // New layers take the project's material, the Decal tool's comes back next frame
	}
	switch (kind) {
	case APP_LAYER_NEW_PAINT:
		ok = layers_op_new() != NULL;
		break;
	case APP_LAYER_NEW_FILL:
		layers_create_fill_layer(FILL_PROJECTION_UV, mat4_nan(), -1, false);
		break;
	case APP_LAYER_NEW_DECAL:
		layers_create_fill_layer(FILL_PROJECTION_DECAL, mat4_nan(), -1, false);
		break;
	case APP_LAYER_NEW_COLOR: {
		swatch_color_t *s = g_context->swatch;
		layers_create_color_layer(color_set_ab(s->base, s->opacity * 255), s->occlusion, s->roughness, s->metallic, -1);
		break;
	}
	case APP_LAYER_NEW_TEXT: {
		draw_saved_t gpu = draw_suspend();
		ok = layers_new_text_layer() != NULL;
		if (ok) {
			history_new_layer();
		}
		layers_op_end(gpu);
		break;
	}
	case APP_LAYER_NEW_PATH:
	case APP_LAYER_NEW_CURVE:
		ok = layers_op_new_path(kind == APP_LAYER_NEW_CURVE) != NULL;
		break;
	case APP_LAYER_NEW_GROUP:
		ok = layers_op_new_group(l) != NULL;
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
	iron_delay_idle_sleep();
	return ok;
}

// Layer menu

static bool app_layer_done(bool ok) {
	iron_delay_idle_sleep();
	return ok;
}

bool app_layer_duplicate(int32_t id) {
	return app_layer_done(layers_op_duplicate(app_layer_by_id(id)) != NULL);
}

bool app_layer_merge_down(int32_t id) {
	return app_layer_done(layers_op_merge_down(app_layer_by_id(id)) != NULL);
}

bool app_layer_merge_group(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || !slot_layer_is_group(l) || slot_layer_get_children(l) == NULL || !(slot_layer_shows(l) || layers_can_delete(l))) {
		return false;
	}
	return app_layer_done(layers_op_merge_group(l) != NULL);
}

bool app_layer_clear(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || l->fill != NULL) {
		return false;
	}
	layers_op_clear(l);
	return app_layer_done(true);
}

bool app_layer_fill_paint(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || !slot_layer_is_mask(l) || l->fill != NULL) {
		return false;
	}
	layers_op_fill_paint(l);
	return app_layer_done(true);
}

bool app_layer_to_fill(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || l->fill != NULL || l->path_material != NULL) { // Not a material swap, see layers_op_to_fill
		return false;
	}
	return app_layer_done(layers_op_to_fill(l));
}

bool app_layer_to_paint(int32_t id) {
	return app_layer_done(layers_op_to_paint(app_layer_by_id(id)));
}

bool app_layer_invert_mask(int32_t id) {
	return app_mask_add_effect(id, APP_MASK_EFFECT_INVERT) >= 0;
}

bool app_layer_apply_mask(int32_t id) {
	return app_layer_done(layers_op_apply_mask(app_layer_by_id(id)) != NULL);
}

bool app_layer_add_mask(int32_t id, int32_t kind) {
	slot_layer_t *l = app_layer_by_id(id);
	if (!slot_layer_takes_mask(l) || (kind != APP_LAYER_NEW_BLACK_MASK && kind != APP_LAYER_NEW_WHITE_MASK)) {
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
	layers_export_layer = l;
	if (slot_layer_is_mask(l)) {
		ui_files_show("png", true, false, &layers_export_on_file_picked);
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
	smart_materials_show_save();
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
	draw_saved_t gpu = draw_suspend();
	slot_layer_t_array_t *pointers   = layers_init_layer_map();
	slot_layer_t         *old_parent = l->parent;
	context_set_layer(l);
	history_order_layers(array_index_of(g_project->_->layers, l));
	l->parent = parent;
	if (old_parent != NULL && slot_layer_is_group(old_parent) && slot_layer_get_children(old_parent) == NULL) {
		history_delete_layer(old_parent)->with_prev = true;
	}
	context_set_layer(l);
	layers_remap_layer_pointers(pointers);
	layers_op_end(gpu);
	return true;
}

// The layers_op_move index that puts l directly below upper (NULL: the top), see slot_layer_move_target.
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
	return app_layer_done(in_place ? app_layer_reparent(l, app_layer_parent_below(upper)) : layers_op_move(l, to));
}

bool app_layer_move_below_flat(int32_t id, int32_t upper_id) {
	slot_layer_t *upper = upper_id >= 0 ? app_layer_by_id(upper_id) : NULL;
	if (upper == NULL) {
		return app_layer_move_below(id, -1);
	}
	slot_layer_t *root = upper;
	while (root->parent != NULL) {
		root = root->parent;
	}
	// Below the lowest of root and its masks, filters and children: under all of it
	slot_layer_t *lowest = g_project->_->layers->buffer[array_index_of(g_project->_->layers, root) - slot_layer_count_descendants(root)];
	slot_layer_t *l      = app_layer_by_id(id);
	if (l == NULL || l == root) {
		return false;
	}
	if (lowest == l) { // The lowest in its group already: out of it, where it is
		return app_layer_set_group(id, -1);
	}
	bool ok = app_layer_move_below(id, lowest->id);
	if (ok && l->parent != NULL && !slot_layer_is_mask(l) && !slot_layer_is_filter(l)) {
		app_layer_reparent(l, NULL); // Out of its group
		history_steps->buffer[history_steps->length - 1]->with_prev = true;
	}
	return ok;
}

// The steps an action pushed since history held `before` undo as one
static void app_layer_steps_as_one(i32 before) {
	for (i32 i = before + 1; i < history_steps->length; ++i) {
		history_steps->buffer[i]->with_prev = true;
	}
}

bool app_layer_drop(int32_t id, int32_t target_id, int32_t where) {
	slot_layer_t *l      = app_layer_by_id(id);
	slot_layer_t *target = app_layer_by_id(target_id);
	if (l == NULL || target == NULL || l == target || slot_layer_is_mask(l) || slot_layer_is_filter(l) || slot_layer_is_mask(target) ||
	    slot_layer_is_filter(target) || slot_layer_is_descendant(target, l) || where < APP_LAYER_DROP_ABOVE || where > APP_LAYER_DROP_INTO) {
		return false;
	}
	slot_layer_t *parent = where == APP_LAYER_DROP_INTO ? target : target->parent;
	if ((where == APP_LAYER_DROP_INTO && !slot_layer_is_group(target)) || (slot_layer_is_group(l) && parent != NULL)) {
		return false; // Into groups only, and groups do not nest
	}
	if (where == APP_LAYER_DROP_INTO) {
		return app_layer_set_group(id, target_id);
	}
	slot_layer_t_array_t *layers = g_project->_->layers;
	i32                   before = history_steps->length;
	bool                  ok;
	if (where == APP_LAYER_DROP_ABOVE) { // Below what is right above it in the stack
		i32 above = array_index_of(layers, target) + 1;
		ok        = app_layer_move_below(id, above < layers->length ? layers->buffer[above]->id : -1);
	}
	else { // Below the lowest of it and its masks, filters and children
		slot_layer_t *lowest = layers->buffer[array_index_of(layers, target) - slot_layer_count_descendants(target)];
		ok                   = lowest != l && app_layer_move_below(id, lowest->id);
	}
	if (ok && l->parent != parent) { // Beside target, in its group or none
		app_layer_reparent(l, parent);
		app_layer_steps_as_one(before);
	}
	return ok;
}

int32_t app_layer_group_with(int32_t id, int32_t other_id) {
	slot_layer_t *l     = app_layer_by_id(id);
	slot_layer_t *other = app_layer_by_id(other_id);
	if (l == NULL || other == NULL || l == other || !slot_layer_is_layer(l) || !slot_layer_is_layer(other) || other->parent != NULL) {
		return -1; // Two layers, the other one not in a group already
	}
	i32           before = history_steps->length;
	slot_layer_t *group  = layers_op_new_group(other);
	if (group == NULL) {
		return -1;
	}
	group->show_panel = true;
	if (!layers_op_set_group(l, group)) {
		history_undo(); // No group of one
		return -1;
	}
	app_layer_steps_as_one(before);
	context_set_layer(l);
	return app_layer_done(true) ? group->id : -1;
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
		return app_layer_done(layers_op_set_group(l, group));
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
	bool ok           = layers_op_move(l, gi - 1 != from ? gi - 1 : gstart);
	group->show_panel = show_panel;
	return app_layer_done(ok && l->parent != group);
}

// Properties

bool app_layer_set_blending(int32_t id, int32_t blending) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || blending < 0 || blending >= app_layer_blending_count() || (i32)l->blending == blending) {
		return false;
	}
	draw_saved_t gpu = draw_suspend();
	context_set_layer(l);
	history_layer_blending();
	l->blending = blending;
	layers_op_end(gpu);
	return app_layer_done(true);
}

bool app_layer_set_channel(int32_t id, int32_t channel, bool on) {
	slot_layer_t *l = app_layer_by_id(id);
	bool         *f = l != NULL && !slot_layer_is_group(l) ? app_layer_channel_field(l, channel) : NULL;
	if (f == NULL || *f == on) {
		return false;
	}
	*f = on;
	draw_saved_t gpu = draw_suspend();
	layers_op_end(gpu);
	return app_layer_done(true);
}

bool app_layer_set_uv_map(int32_t id, int32_t uv_map) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL || slot_layer_is_group(l) || uv_map < 0 || uv_map > (app_mesh_has_uv1() ? 1 : 0) || l->uv_map == uv_map) {
		return false;
	}
	l->uv_map = uv_map;
	draw_saved_t gpu = draw_suspend();
	make_material_parse_paint_material(true);
	layers_op_end(gpu);
	return app_layer_done(true);
}

// Fill layers

static slot_layer_t *app_fill_layer(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && l->fill != NULL ? l : NULL;
}

static fill_source_t *app_fill_source_of(slot_layer_t *l, int32_t channel) {
	return l != NULL && l->fill != NULL && channel >= 0 && channel < FILL_CHANNEL_COUNT ? &l->fill->ch[channel] : NULL;
}

bool app_layer_fill(int32_t id, app_fill_t *out) {
	slot_layer_t *l = app_fill_layer(id);
	if (l == NULL) {
		return false;
	}
	fill_t *f = l->fill;
	*out      = (app_fill_t){.projection = f->projection,
	                         .scale_x    = f->scale[0],
	                         .scale_y    = f->scale[1],
	                         .offset_x   = f->offset[0],
	                         .offset_y   = f->offset[1],
	                         .angle      = f->angle,
	                         .hardness   = f->hardness,
	                         .is_mask    = slot_layer_is_mask(l)};
	return true;
}

bool app_layer_fill_source(int32_t id, int32_t channel, app_fill_source_t *out) {
	fill_source_t *s = app_fill_source_of(app_fill_layer(id), channel);
	if (s == NULL) {
		return false;
	}
	asset_t         *a = fill_source_asset(s);
	slot_material_t *m = slot_material_find_uid(s->material);
	*out               = (app_fill_source_t){.kind          = s->kind,
	                                         .image         = a != NULL ? array_index_of(g_project->_->assets, a) : -1,
	                                         .image_channel = s->image_channel,
	                                         .pattern       = s->pattern,
	                                         .material      = m != NULL ? array_index_of(g_project->_->materials, m) : -1};
	memcpy(out->color, s->value, sizeof(out->color));
	memcpy(out->color2, s->value2, sizeof(out->color2));
	memcpy(out->params, s->params, sizeof(out->params));
	return true;
}

// An undoable edit of l's fill: shape (a new viewport shader) or values only
static void app_fill_edit_begin(slot_layer_t *l) {
	app_drag_end();
	history_layer_fill(l);
}

static bool app_fill_edit_end(slot_layer_t *l, bool shape) {
	draw_saved_t gpu = draw_suspend();
	shape ? fill_reshaped(l) : fill_changed(l);
	draw_resume(gpu);
	iron_delay_idle_sleep();
	return app_layer_done(true);
}

bool app_layer_set_fill_projection(int32_t id, int32_t projection) {
	slot_layer_t *l = app_fill_layer(id);
	if (l == NULL || projection < 0 || projection >= FILL_PROJECTION_COUNT || l->fill->projection == projection) {
		return false;
	}
	if (l->fill->projection == FILL_PROJECTION_DECAL || projection == FILL_PROJECTION_DECAL) { // Decal layers stay decals
		return false;
	}
	app_fill_edit_begin(l);
	bool box = projection == FILL_PROJECTION_DECAL || projection == FILL_PROJECTION_PLANAR;
	if (box && l->fill->projection != FILL_PROJECTION_DECAL && l->fill->projection != FILL_PROJECTION_PLANAR && mat4_get_scale(l->decal_mat).x <= 1.0) {
		l->decal_mat = layers_default_decal_box(); // The gizmo moves it
	}
	l->fill->projection = projection;
	context_set_layer(l); // The gizmo shows for a box
	return app_fill_edit_end(l, true);
}

bool app_layer_set_fill_kind(int32_t id, int32_t channel, int32_t kind) {
	slot_layer_t  *l = app_fill_layer(id);
	fill_source_t *s = app_fill_source_of(l, channel);
	if (s == NULL || kind < 0 || kind >= FILL_SOURCE_COUNT || s->kind == kind) {
		return false;
	}
	app_fill_edit_begin(l);
	fill_source_set_kind(s, channel, kind);
	if (kind == FILL_SOURCE_MATERIAL && fill_source_material(s) == NULL && context_user_material() != NULL) {
		s->material = context_user_material()->uid;
	}
	if (kind == FILL_SOURCE_TEXTURE && fill_source_asset(s) == NULL && g_project->_->assets->length > 0) {
		s->image = g_project->_->assets->buffer[0]->id;
	}
	return app_fill_edit_end(l, true);
}

bool app_layer_set_fill_image(int32_t id, int32_t channel, int32_t image) {
	slot_layer_t  *l = app_fill_layer(id);
	fill_source_t *s = app_fill_source_of(l, channel);
	if (s == NULL || image < 0 || image >= g_project->_->assets->length) {
		return false;
	}
	i32 asset_id = g_project->_->assets->buffer[image]->id;
	if (s->kind == FILL_SOURCE_TEXTURE && s->image == asset_id) {
		return false;
	}
	app_fill_edit_begin(l);
	fill_source_set_kind(s, channel, FILL_SOURCE_TEXTURE);
	s->image = asset_id;
	return app_fill_edit_end(l, true);
}

bool app_layer_set_fill_image_channel(int32_t id, int32_t channel, int32_t image_channel) {
	slot_layer_t  *l = app_fill_layer(id);
	fill_source_t *s = app_fill_source_of(l, channel);
	if (s == NULL || image_channel < 0 || image_channel > 4 || s->image_channel == image_channel) {
		return false;
	}
	app_fill_edit_begin(l);
	s->image_channel = image_channel;
	return app_fill_edit_end(l, true);
}

bool app_layer_set_fill_pattern(int32_t id, int32_t channel, int32_t pattern) {
	slot_layer_t  *l = app_fill_layer(id);
	fill_source_t *s = app_fill_source_of(l, channel);
	if (s == NULL || pattern < 0 || pattern >= pattern_node_count() || (s->kind == FILL_SOURCE_PROCEDURAL && s->pattern == pattern)) {
		return false;
	}
	app_fill_edit_begin(l);
	fill_source_set_kind(s, channel, FILL_SOURCE_PROCEDURAL);
	f32 seed   = s->params[4];
	s->pattern = pattern;
	fill_pattern_params_reset(s);
	s->params[4] = seed;
	return app_fill_edit_end(l, true);
}

bool app_layer_set_fill_material(int32_t id, int32_t channel, int32_t material) {
	slot_layer_t *l = app_fill_layer(id);
	if (l == NULL || material < 0 || material >= app_layer_material_count()) {
		return false;
	}
	slot_material_t *m = g_project->_->materials->buffer[material];
	if (channel < 0) { // Every channel, as dropping the material on the layer
		layers_op_fill_material(l, m);
		return app_layer_done(true);
	}
	fill_source_t *s = app_fill_source_of(l, channel);
	if (s == NULL || (s->kind == FILL_SOURCE_MATERIAL && s->material == m->uid)) {
		return false;
	}
	app_fill_edit_begin(l);
	fill_source_set_kind(s, channel, FILL_SOURCE_MATERIAL);
	s->material = m->uid;
	return app_fill_edit_end(l, true);
}

#define APP_FILL_COLOR_KEY 30 // After a channel's values: its colors in app_layer_set_fill_color's drags

bool app_layer_set_fill_color(int32_t id, int32_t channel, int32_t which, float r, float g, float b) {
	slot_layer_t  *l = app_fill_layer(id);
	fill_source_t *s = app_fill_source_of(l, channel);
	if (s == NULL || which < 0 || which > 1) {
		return false;
	}
	f32 *v   = which == 0 ? s->value : s->value2;
	i32  key = APP_LAYER_VALUE_FILL_SOURCE + channel * 32 + APP_FILL_COLOR_KEY + which;
	if (v[0] == r && v[1] == g && v[2] == b) {
		return false;
	}
	if (!app_dragging(APP_DRAG_LAYER_VALUE, id, key, 0)) { // A picker's changes in a row: one undo step
		history_layer_fill(l);
		app_drag_begin(APP_DRAG_LAYER_VALUE, id, key, 0);
	}
	v[0] = math_max(0.0, math_min(1.0, r));
	v[1] = math_max(0.0, math_min(1.0, g));
	v[2] = math_max(0.0, math_min(1.0, b));
	fill_changed(l);
	iron_delay_idle_sleep();
	return true;
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

int32_t app_fill_pattern_count(void) {
	return pattern_node_count();
}

const char *app_fill_pattern_name(int32_t pattern) {
	return pattern >= 0 && pattern < pattern_node_count() ? app_str(pattern_node_name(pattern)) : "";
}

static char *app_fill_common_params[7] = {"Scale", "Stretch", "Angle", "Contrast", "Seed", "Distort", "Distort Scale"};
static f32   app_fill_common_min[7]    = {1.0, 1.0, 0.0, 0.2, 0.0, 0.0, 1.0};
static f32   app_fill_common_max[7]    = {100.0, 50.0, 180.0, 4.0, 1000.0, 1.0, 32.0};

int32_t app_fill_param_count(int32_t pattern) {
	return pattern >= 0 && pattern < pattern_node_count() ? 7 + pattern_def_extras(pattern) : 0;
}

const char *app_fill_param_name(int32_t pattern, int32_t i) {
	if (i < 0 || i >= app_fill_param_count(pattern)) {
		return "";
	}
	return app_str(i < 7 ? tr(app_fill_common_params[i]) : pattern_def_extra_name(pattern, i - 7));
}

float app_fill_param_min(int32_t pattern, int32_t i) {
	return i >= 0 && i < 7 ? app_fill_common_min[i] : 0.0;
}

float app_fill_param_max(int32_t pattern, int32_t i) {
	if (i < 0 || i >= app_fill_param_count(pattern)) {
		return 1.0;
	}
	return i < 7 ? app_fill_common_max[i] : pattern_def_extra_max(pattern, i - 7);
}

// Slider drags

static f32 *app_layer_value_field(slot_layer_t *l, i32 value, f32 *min, f32 *max) {
	fill_t *f = l->fill;
	if (value >= APP_LAYER_VALUE_FILL_SOURCE) { // A source's value or pattern setting
		i32            c = (value - APP_LAYER_VALUE_FILL_SOURCE) / 32;
		i32            k = (value - APP_LAYER_VALUE_FILL_SOURCE) % 32;
		fill_source_t *s = app_fill_source_of(l, c);
		if (s == NULL) {
			return NULL;
		}
		if (k == APP_FILL_VALUE || k == APP_FILL_VALUE2) {
			*min = 0.0;
			*max = c == FILL_CHANNEL_NORMAL ? 4.0 : 1.0; // A bump's strength
			return k == APP_FILL_VALUE ? &s->value[0] : &s->value2[0];
		}
		i32 p = k - APP_FILL_VALUE_PARAM;
		if (p < 0 || p >= app_fill_param_count(s->pattern)) {
			return NULL;
		}
		*min = app_fill_param_min(s->pattern, p);
		*max = app_fill_param_max(s->pattern, p);
		return &s->params[p];
	}
	switch (value) {
	case APP_LAYER_VALUE_FILL_SCALE:
	case APP_LAYER_VALUE_FILL_SCALE_X:
	case APP_LAYER_VALUE_FILL_SCALE_Y:
		*min = 0.01;
		*max = 64.0;
		return f != NULL ? &f->scale[value == APP_LAYER_VALUE_FILL_SCALE_Y ? 1 : 0] : NULL;
	case APP_LAYER_VALUE_FILL_OFFSET:
	case APP_LAYER_VALUE_FILL_OFFSET_X:
	case APP_LAYER_VALUE_FILL_OFFSET_Y:
		*min = -1.0;
		*max = 1.0;
		return f != NULL ? &f->offset[value == APP_LAYER_VALUE_FILL_OFFSET_Y ? 1 : 0] : NULL;
	case APP_LAYER_VALUE_FILL_ANGLE:
		*min = 0.0;
		*max = 360.0;
		return f != NULL ? &f->angle : NULL;
	case APP_LAYER_VALUE_FILL_HARDNESS:
		*min = 0.0;
		*max = 1.0;
		return f != NULL ? &f->hardness : NULL;
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
	history_layer_fill(l);
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
	if (value == APP_LAYER_VALUE_FILL_SCALE || value == APP_LAYER_VALUE_FILL_OFFSET) { // Linked: Y follows X
		f[1] = f[0];
	}
	fill_changed(l); // Live: the parameters texture, no new shader
	iron_delay_idle_sleep();
	return true;
}

float app_layer_value(int32_t id, int32_t value) {
	slot_layer_t *l   = app_layer_by_id(id);
	f32           min = 0.0;
	f32           max = 0.0;
	f32          *f   = l != NULL ? app_layer_value_field(l, value, &min, &max) : NULL;
	return f != NULL ? *f : 0.0;
}

bool app_layer_value_range(int32_t id, int32_t value, float *min, float *max) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && app_layer_value_field(l, value, min, max) != NULL;
}
