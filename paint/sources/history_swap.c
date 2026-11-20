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

// Layer records: a layer out of the project for a step (deleted, merged) keeps its props in its struct and
// its pixels as an HSTORE_LAYER_RECORD: u32 uid, u32 timeline, hash128 tile map (NONE for fill layers, they render
// again, and groups), u32 layer_slot_type_t (the kind of a layer is told by its textures, they go); then u32 length and
// the layer's props as an armpack map (history_record_layer makes the layer again from them: a saved history loaded).

#define HISTORY_RECORD_LEN 28

// hstore children of a record: its tile map
void history_record_children(const u8 *data, u32 len, hstore_visit_fn visit, void *ud) {
	if (len >= HISTORY_RECORD_LEN) {
		hash128_t h;
		memcpy(&h.lo, data + 8, 8);
		memcpy(&h.hi, data + 16, 8);
		visit(h, ud);
	}
}

// Props of a layer record (every layer_data_t field but the pixels, and uid, timeline, id), bools as i32
#define HISTORY_RECORD_PROPS 33

static void hrec_i32(char *k, i32 v) {
	armpack_encode_string(k);
	armpack_encode_i32(v);
}

static void hrec_f32(char *k, f32 v) {
	armpack_encode_string(k);
	armpack_encode_f32(v);
}

static void hrec_f32s(char *k, f32_array_t *a) {
	armpack_encode_string(k);
	if (a != NULL) {
		armpack_encode_array_f32(a);
	}
	else {
		armpack_encode_null();
	}
}

static u32 history_record_props_size(slot_layer_t *l) {
	f32_array_t *a[5] = {l->path_points, l->path_points_world, l->path_points_camera, l->mask_effects, NULL};
	u32          n    = 4096 + FILL_ENCODE_LENGTH * 4 + (l->name != NULL ? string_length(l->name) : 0) + (l->path_points_parent != NULL ? l->path_points_parent->length * 4 : 0);
	for (i32 i = 0; i < 4; ++i) {
		n += a[i] != NULL ? a[i]->length * 4 : 0;
	}
	return n;
}

static u32 history_record_props(slot_layer_t *l, u8 *out) {
	armpack_encode_start(out);
	armpack_encode_map(HISTORY_RECORD_PROPS);
	armpack_encode_string("name");
	armpack_encode_string(l->name != NULL ? l->name : "");
	hrec_i32("uid", (i32)l->uid);
	hrec_i32("timeline", (i32)l->timeline);
	hrec_i32("id", l->id);
	hrec_i32("visible", l->visible);
	hrec_f32("opacity_mask", l->mask_opacity);
	hrec_f32("height_raise", l->height_raise);
	hrec_i32("show_panel", l->show_panel);
	hrec_i32("blending", l->blending);
	hrec_i32("object_mask", l->object_mask);
	hrec_i32("uv_map", l->uv_map);
	bool flags[11] = {l->paint_base, l->paint_opac,   l->paint_occ,          l->paint_rough, l->paint_met, l->paint_nor,
	                  l->paint_nor_blend, l->paint_height, l->paint_height_blend, l->paint_emis,  l->paint_subs};
	char *names[11] = {"paint_base", "paint_opac", "paint_occ", "paint_rough", "paint_met", "paint_nor", "paint_nor_blend", "paint_height", "paint_height_blend", "paint_emis", "paint_subs"};
	for (i32 i = 0; i < 11; ++i) {
		hrec_i32(names[i], flags[i]);
	}
	f32_array_t decal = {.buffer = (f32 *)&l->decal_mat, .length = 16, .capacity = 16};
	hrec_f32s("decal_mat", &decal);
	hrec_f32s("path_points", l->path_points);
	hrec_f32s("path_points_world", l->path_points_world);
	hrec_f32s("path_points_camera", l->path_points_camera);
	armpack_encode_string("path_points_parent");
	if (l->path_points_parent != NULL) {
		armpack_encode_array_i32(l->path_points_parent);
	}
	else {
		armpack_encode_null();
	}
	hrec_i32("path_tool", l->path_tool);
	hrec_i32("path_curved", l->path_curved);
	hrec_i32("path_text", l->path_text);
	hrec_i32("path_material", l->path_material != NULL ? (i32)l->path_material->uid : 0);
	hrec_f32s("mask_effects", l->mask_effects);
	f32_array_t *fill = fill_encode(l->fill, false);
	hrec_f32s("fill", fill);
	array_delete(fill);
	return armpack_encode_end();
}

static f32_array_t *hrec_get_f32s(any_map_t *m, char *k) {
	f32_array_t *a = any_map_get(m, k);
	if (a == NULL) {
		return NULL;
	}
	f32_array_t *c = f32_array_create(a->length);
	if (a->length > 0) {
		memcpy(c->buffer, a->buffer, sizeof(f32) * a->length);
	}
	return c;
}

// A layer out of the project made again from its record: props, no textures (the record restore gives them back).
// NULL when the record is missing.
slot_layer_t *history_record_layer(hash128_t record) {
	u32 len = 0;
	u8 *b   = hash128_is_none(record) ? NULL : hstore_get(record, &len);
	u32 plen = 0;
	if (b == NULL || len < HISTORY_RECORD_LEN + 4) {
		free(b);
		return NULL;
	}
	memcpy(&plen, b + HISTORY_RECORD_LEN, 4);
	buffer_t   pb = {.buffer = b + HISTORY_RECORD_LEN + 4, .length = plen};
	any_map_t *m  = HISTORY_RECORD_LEN + 4 + plen <= len ? armpack_decode_to_map(&pb) : NULL;
	if (m == NULL) {
		free(b);
		return NULL;
	}
	slot_layer_t *l = ALLOC_INIT(slot_layer_t, {0});
	char         *name = any_map_get(m, "name");
	l->name            = string_copy(name != NULL ? name : "");
	l->uid             = (u32)armpack_map_get_i32(m, "uid");
	l->timeline        = (u32)armpack_map_get_i32(m, "timeline");
	l->id              = armpack_map_get_i32(m, "id");
	l->visible         = armpack_map_get_i32(m, "visible") != 0;
	l->mask_opacity    = armpack_map_get_f32(m, "opacity_mask");
	l->height_raise    = armpack_map_get_f32(m, "height_raise");
	l->show_panel      = armpack_map_get_i32(m, "show_panel") != 0;
	l->blending        = armpack_map_get_i32(m, "blending");
	l->object_mask     = armpack_map_get_i32(m, "object_mask");
	l->uv_map          = armpack_map_get_i32(m, "uv_map");
	bool *flags[11]    = {&l->paint_base, &l->paint_opac,   &l->paint_occ,          &l->paint_rough, &l->paint_met, &l->paint_nor,
	                      &l->paint_nor_blend, &l->paint_height, &l->paint_height_blend, &l->paint_emis,  &l->paint_subs};
	char *names[11]    = {"paint_base", "paint_opac", "paint_occ", "paint_rough", "paint_met", "paint_nor", "paint_nor_blend", "paint_height", "paint_height_blend", "paint_emis", "paint_subs"};
	for (i32 i = 0; i < 11; ++i) {
		*flags[i] = armpack_map_get_i32(m, names[i]) != 0;
	}
	f32_array_t *decal = any_map_get(m, "decal_mat");
	l->decal_mat       = mat4_identity();
	if (decal != NULL && decal->length == 16) {
		memcpy(&l->decal_mat, decal->buffer, sizeof(f32) * 16);
	}
	l->path_points        = hrec_get_f32s(m, "path_points");
	l->path_points_world  = hrec_get_f32s(m, "path_points_world");
	l->path_points_camera = hrec_get_f32s(m, "path_points_camera");
	i32_array_t *pp       = any_map_get(m, "path_points_parent");
	if (pp != NULL) {
		l->path_points_parent = i32_array_create(pp->length);
		if (pp->length > 0) {
			memcpy(l->path_points_parent->buffer, pp->buffer, sizeof(i32) * pp->length);
		}
	}
	l->path_tool              = armpack_map_get_i32(m, "path_tool");
	l->path_curved            = armpack_map_get_i32(m, "path_curved") != 0;
	l->path_text              = armpack_map_get_i32(m, "path_text") != 0;
	l->path_material          = slot_material_find_uid((u32)armpack_map_get_i32(m, "path_material"));
	l->mask_effects           = hrec_get_f32s(m, "mask_effects");
	l->fill                   = fill_decode(any_map_get(m, "fill"), false);
	l->fill_dirty             = l->fill != NULL;
	free(b);
	l->ext = string("%d", l->id); // history_io_decode gives it a free id
	return l;
}

// The record of l as it is now, the layer keeps its textures
static hash128_t history_record_make(slot_layer_t *l) {
	hash128_t   map = HASH128_NONE;
	timeline_t *tl  = l->texpaint != NULL ? timeline_of(l) : NULL; // Groups: props only
	if (tl != NULL) {
		map = timeline_state(tl, tl->cursor);
		hstore_ref(map);
	}
	if (hash128_is_none(map) && l->texpaint != NULL && timeline_layer_paints(l)) {
		map = timeline_live_map(l);
	}
	u32 kind = l->texpaint == NULL ? LAYER_SLOT_TYPE_GROUP : slot_layer_is_mask(l) ? LAYER_SLOT_TYPE_MASK : LAYER_SLOT_TYPE_LAYER;
	u8 *b    = malloc(HISTORY_RECORD_LEN + 4 + history_record_props_size(l));
	memcpy(b, &l->uid, 4);
	memcpy(b + 4, &l->timeline, 4);
	memcpy(b + 8, &map.lo, 8);
	memcpy(b + 16, &map.hi, 8);
	memcpy(b + 24, &kind, 4);
	u32 plen = history_record_props(l, b + HISTORY_RECORD_LEN + 4);
	memcpy(b + HISTORY_RECORD_LEN, &plen, 4);
	hash128_t h = hstore_put(HSTORE_LAYER_RECORD, b, HISTORY_RECORD_LEN + 4 + plen); // Takes its own reference on the map
	free(b);
	hstore_unref(map);
	return h;
}

// The layer leaves the project as its record, its textures go
static hash128_t history_record_out(slot_layer_t *l) {
	hash128_t h = history_record_make(l);
	if (l->texpaint == NULL) {
		return h; // A group: nothing to unload
	}
	slot_layer_unload(l);
	l->texpaint         = NULL;
	l->texpaint_nor     = NULL;
	l->texpaint_pack    = NULL;
	l->texpaint_preview = NULL;
	return h;
}

// The layer comes back: textures again, its pixels from the record
static void history_record_in(slot_layer_t *l, hash128_t record) {
	u32       len = 0;
	u8       *b   = hash128_is_none(record) ? NULL : hstore_get(record, &len);
	hash128_t map = HASH128_NONE;
	u32       kind = LAYER_SLOT_TYPE_GROUP;
	if (b != NULL && len >= HISTORY_RECORD_LEN) {
		memcpy(&map.lo, b + 8, 8);
		memcpy(&map.hi, b + 16, 8);
		memcpy(&kind, b + 24, 4);
	}
	free(b);
	if (l->texpaint != NULL || kind == LAYER_SLOT_TYPE_GROUP) {
		hstore_unref(record);
		return;
	}
	if (kind == LAYER_SLOT_TYPE_MASK) {
		l->texpaint = render_path_add_target(string_tmp("texpaint%s", l->ext), config_get_texture_res_x(), config_get_texture_res_y(), "RGBA32", 1.0)->_image;
	}
	else {
		slot_layer_alloc_textures(l);
	}
	l->texpaint_preview = gpu_create_render_target(util_render_layer_preview_size, util_render_layer_preview_size, GPU_TEXTURE_FORMAT_RGBA32);
	paint_target_t t;
	timeline_target_live(l, &t);
	if (hash128_is_none(map) || !timeline_state_resample(map, &t)) { // Scaled when the project's resolution changed since
		draw_saved_t saved = draw_suspend();
		slot_layer_clear(l, slot_layer_is_mask(l) ? 0xffffffff : 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		draw_resume(saved);
	}
	if (l->fill != NULL) {
		l->fill_dirty = true; // Rasterized again, the viewport shows it live meanwhile
	}
	hstore_unref(record);
	mask_stack_mark_dirty(l);
}

// The layers of a delete or merge step leave the project as records
void history_records_take(history_step_t *step) {
	if (step->merge_layers == NULL) {
		return;
	}
	free(step->records);
	step->records = calloc(step->merge_layers->length > 0 ? step->merge_layers->length : 1, sizeof(hash128_t));
	for (i32 i = 0; i < step->merge_layers->length; ++i) {
		step->records[i] = history_record_out(step->merge_layers->buffer[i]);
	}
}

// A merge step keeps the layers it merges as records of them made now, and copies made from the records (props, no
// textures). The layers stay in the project for the merge.
void history_records_copy(history_step_t *step, slot_layer_t_array_t *layers, i32 start, i32 end) {
	step->merge_layers  = any_array_create_from_raw((void *[]){}, 0);
	step->merge_parents = i32_array_create(0);
	free(step->records);
	step->records = calloc(end - start + 1 > 0 ? end - start + 1 : 1, sizeof(hash128_t));
	for (i32 i = start; i <= end; ++i) {
		slot_layer_t *l = layers->buffer[i];
		hash128_t     h = history_record_make(l);
		slot_layer_t *c = history_record_layer(h);
		step->records[step->merge_layers->length] = h;
		any_array_push(step->merge_layers, c);
		i32_array_push(step->merge_parents, l->parent != NULL ? array_index_of(layers, l->parent) : -1);
	}
}

void history_records_free(history_step_t *step) {
	for (i32 i = 0; step->records != NULL && step->merge_layers != NULL && i < step->merge_layers->length; ++i) {
		hstore_unref(step->records[i]);
	}
	free(step->records);
	step->records = NULL;
}

void history_swap_merge(history_step_t *step, bool undo) {
	slot_layer_t_array_t *layers   = g_project->_->layers;
	slot_layer_t_array_t *saved    = step->merge_layers;
	i32_array_t          *parents  = step->merge_parents;
	i32                   count    = layers->length - (step->merge_length - saved->length);
	slot_layer_t_array_t *pointers = layers_init_layer_map();

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
	hash128_t *records = step->records;
	step->records      = NULL;
	history_records_take(step); // The layers leaving
	for (i32 i = 0; i < saved->length; ++i) {
		if (records != NULL) {
			history_record_in(saved->buffer[i], records[i]);
		}
		array_insert(layers, step->merge_start + i, saved->buffer[i]);
	}
	free(records);
	for (i32 i = 0; i < saved->length; ++i) {
		saved->buffer[i]->parent = parents->buffer[i] >= 0 ? layers->buffer[parents->buffer[i]] : NULL;
	}

	if (step->action == HISTORY_ACTION_DELETE_LAYER) {
		layers_remap_layer_pointers(pointers);
	}

	i32 top = step->merge_start + saved->length - 1;
	context_set_layer(layers->buffer[undo ? step->layer : top >= 0 ? top : 0]);
	g_context->layers_preview_dirty = true;
}

static f32_array_t *history_f32s_copy(f32_array_t *a) {
	if (a == NULL) {
		return NULL;
	}
	f32_array_t *c = f32_array_create(a->length);
	if (a->length > 0) {
		memcpy(c->buffer, a->buffer, sizeof(f32) * a->length);
	}
	return c;
}

// The path props of l (points, curve, text, tool, material) in a layer struct of their own
slot_layer_t *history_path_copy(slot_layer_t *l) {
	slot_layer_t *c       = ALLOC_INIT(slot_layer_t, {0});
	c->path_points        = history_f32s_copy(l->path_points);
	c->path_points_world  = history_f32s_copy(l->path_points_world);
	c->path_points_camera = history_f32s_copy(l->path_points_camera);
	if (l->path_points_parent != NULL) {
		c->path_points_parent = i32_array_create(l->path_points_parent->length);
		if (l->path_points_parent->length > 0) {
			memcpy(c->path_points_parent->buffer, l->path_points_parent->buffer, sizeof(i32) * l->path_points_parent->length);
		}
	}
	c->path_curved   = l->path_curved;
	c->path_text     = l->path_text;
	c->path_tool     = l->path_tool;
	c->path_material = l->path_material;
	return c;
}

void history_path_free(slot_layer_t *p) {
	if (p == NULL) {
		return;
	}
	array_delete(p->path_points);
	array_delete(p->path_points_world);
	array_delete(p->path_points_camera);
	array_delete(p->path_points_parent);
	free(p);
}

// The path props of l and of the step trade places
void history_swap_path(history_step_t *step, slot_layer_t *l) {
	slot_layer_t t        = *l;
	slot_layer_t *p       = step->path;
	l->path_points        = p->path_points;
	l->path_points_world  = p->path_points_world;
	l->path_points_camera = p->path_points_camera;
	l->path_points_parent = p->path_points_parent;
	l->path_curved        = p->path_curved;
	l->path_text          = p->path_text;
	l->path_tool          = p->path_tool;
	l->path_material      = p->path_material;
	p->path_points        = t.path_points;
	p->path_points_world  = t.path_points_world;
	p->path_points_camera = t.path_points_camera;
	p->path_points_parent = t.path_points_parent;
	p->path_curved        = t.path_curved;
	p->path_text          = t.path_text;
	p->path_tool          = t.path_tool;
	p->path_material      = t.path_material;
	path_layer_last_active = l->path_points != NULL ? l->path_points->length / 2 - 1 : -1;
}

void history_swap_order(history_step_t *step) {
	slot_layer_t_array_t *pointers = layers_init_layer_map();
	slot_layer_t         *l        = g_project->_->layers->buffer[step->prev_order];
	i32                   parent   = l->parent != NULL ? array_index_of(g_project->_->layers, l->parent) : -1;
	slot_layer_relocate(l, step->layer);
	l->parent          = step->layer_parent >= 0 ? g_project->_->layers->buffer[step->layer_parent] : NULL;
	step->layer_parent = parent;
	i32 prev_order     = step->prev_order;
	step->prev_order   = step->layer;
	step->layer        = prev_order;
	layers_remap_layer_pointers(pointers);
	context_set_layer(l);
	make_material_parse_mesh_material();
}

// The mask recreated by an apply mask undo gets the settings it had, the step keeps its copy for the next undo
void history_restore_applied_mask(history_step_t *step, slot_layer_t *m) {
	m->mask_opacity = step->layer_opacity;
	m->blending     = step->layer_blending;
	m->mask_effects = mask_stack_copy_effects(step->mask_effects);
	mask_stack_mark_dirty(m);
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
	else if (step->action == HISTORY_ACTION_LAYER_RAISE) {
		context_set_layer(l);
		f32 t             = l->height_raise;
		l->height_raise   = step->layer_raise;
		step->layer_raise = t;
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
		fill_changed(l);
		layers_set_object_mask();
	}
	else if (step->action == HISTORY_ACTION_LAYER_FILL) {
		f32_array_t *t = fill_encode(l->fill, false);
		for (i32 i = 0; i < 16; ++i) { // The decal box after the fill
			f32_array_push(t, ((f32 *)&l->decal_mat)[i]);
		}
		fill_t *f = fill_decode(step->fill, false);
		if (f != NULL) {
			free(l->fill);
			l->fill = f;
		}
		if (step->fill != NULL && step->fill->length >= FILL_ENCODE_LENGTH + 16) {
			memcpy(&l->decal_mat, step->fill->buffer + FILL_ENCODE_LENGTH, sizeof(f32) * 16);
		}
		array_delete(step->fill);
		step->fill = t;
		context_set_layer(l);
		fill_reshaped(l);
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
}

void history_new_swatch_slot(history_step_t *step) {
	array_insert(g_project->swatches, step->swatch, step->swatch_color);
	g_context->swatch = step->swatch_color;
}

void history_delete_swatch_slot(history_step_t *step) {
	step->swatch_color = g_project->swatches->buffer[step->swatch];
	if (g_project->swatches->length > 1) {
		g_context->swatch = g_project->swatches->buffer[step->swatch == g_project->swatches->length - 1 ? step->swatch - 1 : step->swatch + 1];
	}
	array_splice(g_project->swatches, step->swatch, 1);
}

void history_swap_swatches(history_step_t *step) {
	swatch_color_t_array_t *t = g_project->swatches;
	g_project->swatches       = step->swatch_colors;
	step->swatch_colors       = t;
	if (g_project->swatches->length > 0) {
		g_context->swatch = g_project->swatches->buffer[0];
	}
}

void history_swap_swatch(history_step_t *step) {
	swatch_color_t *s       = g_project->swatches->buffer[step->swatch];
	swatch_color_t  t       = *s;
	*s                      = *step->swatch_color;
	*step->swatch_color     = t;
	g_context->swatch       = s;
	g_context->picked_color = util_clone_swatch_color(s);
}

node_group_t_array_t *history_groups(history_step_t *step) {
	return g_project->_->material_groups;
}

static ui_node_canvas_t *history_get_canvas(history_step_t *step) {
	if (step->canvas_group == -1) {
		return g_project->_->materials->buffer[step->material]->canvas;
	}
	else {
		return history_groups(step)->buffer[step->canvas_group]->canvas;
	}
}

static void history_set_canvas(history_step_t *step, ui_node_canvas_t *canvas) {
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
		slot_material_color_to_picked(g_context->material);
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
