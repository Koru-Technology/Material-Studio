
#include "../global.h"

i16_array_t *util_mesh_va0;

static mesh_object_t_array_t *util_mesh_merged_objects = NULL;

void util_mesh_remove_merged() {
	if (util_mesh_merged_objects != NULL) {
		util_mesh_merged_objects->length = 0;
	}
	if (g_context->merged_object != NULL) {
		mesh_data_delete(g_context->merged_object->data);
		mesh_object_remove(g_context->merged_object);
		g_context->merged_object = NULL;
	}
}

mesh_object_t_array_t *util_mesh_get_visible() {
	mesh_object_t_array_t *ar = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p->base->visible) {
			any_array_push(ar, p);
		}
	}
	return ar;
}

mesh_object_t_array_t *util_mesh_dedup_data(mesh_object_t_array_t *objects) {
	mesh_object_t_array_t *ar = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *p    = objects->buffer[i];
		bool           seen = false;
		for (i32 j = 0; j < ar->length && !seen; ++j) {
			seen = ar->buffer[j]->data == p->data;
		}
		if (!seen) {
			any_array_push(ar, p);
		}
	}
	return ar;
}

mesh_object_t_array_t *util_mesh_get_unique_data() {
	return util_mesh_dedup_data(g_project->_->paint_objects);
}

mesh_object_t_array_t *util_mesh_get_hierarchy(mesh_object_t *o) {
	// The object and all its descendants, one object per mesh data
	mesh_object_t_array_t *ar = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		object_t      *q = p->base;
		while (q != NULL && q != o->base) {
			q = q->parent;
		}
		bool seen = q == NULL;
		for (i32 j = 0; j < ar->length && !seen; ++j) {
			seen = ar->buffer[j]->data == p->data;
		}
		if (!seen) {
			any_array_push(ar, p);
		}
	}
	return ar;
}

i32 util_mesh_data_owner(mesh_data_t *data) {
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (g_project->_->paint_objects->buffer[i]->data == data) {
			return i;
		}
	}
	return -1;
}

bool util_mesh_data_is_shared(mesh_data_t *data) {
	i32 users = 0;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (g_project->_->paint_objects->buffer[i]->data == data) {
			users++;
		}
	}
	return users > 1;
}

void util_mesh_sync_scale_world(mesh_data_t *data) {
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p->data == data && p->base->transform->scale_world != data->scale_pos) {
			p->base->transform->scale_world = data->scale_pos;
			transform_build_matrix(p->base->transform);
		}
	}
}

char *util_mesh_link_name(i32 source_index, char *object_name) {
	return string("%s@%d", object_name, source_index);
}

bool util_mesh_link_parse(char *name, i32 *source_index, char **object_name) {
	if (name == NULL) {
		return false;
	}
	i32 sep = string_last_index_of(name, "@");
	if (sep < 0) {
		return false;
	}
	i32 len = string_length(name);
	if (sep + 1 == len) {
		return false;
	}
	for (i32 i = sep + 1; i < len; ++i) {
		if (name[i] < '0' || name[i] > '9') {
			return false;
		}
	}
	*source_index = parse_int(substring(name, sep + 1, len));
	*object_name  = substring(name, 0, sep);
	return true;
}

#define ATLAS_MAX_STRIDE 8
#define ATLAS_MAX_SLOTS  (ATLAS_MAX_STRIDE * ATLAS_MAX_STRIDE)

static i32 _util_mesh_atlas_stride_for(i32 count) {
	i32 stride = 1;
	while (stride * stride < count && stride < ATLAS_MAX_STRIDE) {
		stride++;
	}
	return stride;
}

static i32  util_mesh_atlas_stride_merged = 1;
static i32  util_mesh_udim_tiles[ATLAS_MAX_SLOTS]; // Sorted tile ids, one atlas slot per tile
static i32  util_mesh_udim_tile_count  = 0;
static bool util_mesh_udim_tiles_spent = false;

// Tile id from the ".1001" name suffix given by udim import, -1 if none
i32 util_mesh_udim_tile(char *name) {
	if (name == NULL) {
		return -1;
	}
	i32 len = string_length(name);
	if (len < 5 || name[len - 5] != '.' || name[len - 4] != '1') {
		return -1;
	}
	i32 id = 0;
	for (i32 i = len - 4; i < len; ++i) {
		if (name[i] < '0' || name[i] > '9') {
			return -1;
		}
		id = id * 10 + (name[i] - '0');
	}
	return id > 1000 ? id : -1;
}

static void _util_mesh_udim_build_tiles() {
	mesh_object_t_array_t *paint_objects = g_project->_->paint_objects;
	util_mesh_udim_tile_count            = 0;
	util_mesh_udim_tiles_spent           = false;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		i32 id = util_mesh_udim_tile(paint_objects->buffer[i]->base->name);
		if (id < 0) {
			continue;
		}
		i32 pos = 0;
		while (pos < util_mesh_udim_tile_count && util_mesh_udim_tiles[pos] < id) {
			pos++;
		}
		if (pos < util_mesh_udim_tile_count && util_mesh_udim_tiles[pos] == id) {
			continue;
		}
		if (util_mesh_udim_tile_count == ATLAS_MAX_SLOTS) {
			util_mesh_udim_tiles_spent = true;
			continue;
		}
		for (i32 j = util_mesh_udim_tile_count; j > pos; --j) {
			util_mesh_udim_tiles[j] = util_mesh_udim_tiles[j - 1];
		}
		util_mesh_udim_tiles[pos] = id;
		util_mesh_udim_tile_count++;
	}
}

// Shared layers of a mesh split by udim tile keep every tile in its own atlas slot
bool util_mesh_udim_active() {
	return util_mesh_udim_tile_count > 1;
}

i32 util_mesh_udim_slot(i32 tile) {
	for (i32 i = 0; i < util_mesh_udim_tile_count; ++i) {
		if (util_mesh_udim_tiles[i] == tile) {
			return i;
		}
	}
	return util_mesh_udim_tiles_spent ? ATLAS_MAX_SLOTS - 1 : 0;
}

bool util_mesh_udim_layer(slot_layer_t *l) {
	if (!util_mesh_udim_active() || l == NULL || l->uv_map == 1) {
		return false;
	}
	i32 mask = slot_layer_get_object_mask(l);
	return mask == 0 || mask > g_project->_->paint_objects->length;
}

static i32 _util_mesh_atlas_slot_for_object(mesh_object_t *o) {
	if (util_mesh_udim_active()) {
		return util_mesh_udim_slot(util_mesh_udim_tile(o->base->name));
	}
	return 0;
}

static void _util_mesh_atlas_build_slots() {
	_util_mesh_udim_build_tiles();
	util_mesh_atlas_stride_merged = util_mesh_udim_active() ? _util_mesh_atlas_stride_for(util_mesh_udim_tile_count) : 1;
}

i32 util_mesh_atlas_stride() {
	return util_mesh_atlas_stride_merged;
}

i32 util_mesh_atlas_slot(object_t *object) {
	mesh_object_t_array_t *paint_objects = g_project->_->paint_objects;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		if (paint_objects->buffer[i]->base == object) {
			return _util_mesh_atlas_slot_for_object(paint_objects->buffer[i]);
		}
	}
	return -1; // Merged object or 2d plane, uvs already in atlas space
}

void util_mesh_delete_data_uncache(void *data) {
	mesh_data_t *md = (mesh_data_t *)data;
	if (md->_->handle != NULL) {
		mesh_data_t *cached = any_map_get(data_cached_meshes, md->_->handle);
		if (cached == md) {
			map_delete(data_cached_meshes, md->_->handle);
		}
	}
	mesh_data_delete(md);
}

void util_mesh_remove_objects(mesh_object_t_array_t *objects, mesh_object_t *keep) {
	any_array_t *datas = any_array_create(0);
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *p = objects->buffer[i];
		if (p == keep) {
			continue;
		}
		if ((keep == NULL || p->data != keep->data) && array_index_of(datas, p->data) < 0) {
			any_array_push(datas, p->data);
		}
		mesh_object_remove(p);
	}
	for (i32 i = 0; i < datas->length; ++i) {
		util_mesh_delete_data_uncache(datas->buffer[i]);
	}
	array_free(datas);
	free(datas);
}

mesh_data_t *util_mesh_data_duplicate(mesh_data_t *source) {
	mesh_data_t *raw = calloc(1, sizeof(mesh_data_t));
	raw->name        = string_copy(source->name);
	raw->scale_pos   = source->scale_pos;
	raw->scale_tex   = source->scale_tex;

	raw->vertex_arrays = (vertex_array_t_array_t *)any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < source->vertex_arrays->length; ++i) {
		vertex_array_t *src = source->vertex_arrays->buffer[i];
		vertex_array_t *va =
		    ALLOC_INIT(vertex_array_t,
		               {.attrib = string_copy(src->attrib), .data = string_copy(src->data), .values = i16_array_create_from_array(src->values)});
		any_array_push(raw->vertex_arrays, va);
	}

	raw->index_array = u32_array_create_from_array(source->index_array);

	mesh_data_t *md    = mesh_data_create(raw);
	md->_->owns_arrays = true;
	return md;
}

static f32 util_mesh_pack_merged_positions(mesh_object_t_array_t *paint_objects, object_t *parent, i16_array_t *va0, i16_array_t *va1) {
	bool   bake       = parent != NULL;
	mat4_t inv_parent = bake ? mat4_inv(parent->transform->world) : mat4_identity();
	f32    max_scale  = 0.0;

	for (i32 i = 0; i < paint_objects->length; ++i) {
		mesh_object_t *o = paint_objects->buffer[i];
		if (!bake) {
			max_scale = math_max(max_scale, o->data->scale_pos);
			continue;
		}
		mat4_t       m     = mat4_mult_mat(o->base->transform->world_unpack, inv_parent);
		i16_array_t *pos   = o->data->vertex_arrays->buffer[0]->values;
		i32          count = math_floor(pos->length / 4.0);
		for (i32 j = 0; j < count; ++j) {
			vec4_t p  = (vec4_t){pos->buffer[j * 4] / 32767.0, pos->buffer[j * 4 + 1] / 32767.0, pos->buffer[j * 4 + 2] / 32767.0, 1.0};
			p         = vec4_apply_mat4(p, m);
			max_scale = math_max(max_scale, math_max(math_abs(p.x), math_max(math_abs(p.y), math_abs(p.z))));
		}
	}
	if (max_scale <= 0.0) {
		max_scale = 1.0;
	}

	i32 voff = 0;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		mesh_object_t *o     = paint_objects->buffer[i];
		i16_array_t   *pos   = o->data->vertex_arrays->buffer[0]->values;
		i16_array_t   *nor   = o->data->vertex_arrays->buffer[1]->values;
		i32            count = math_floor(pos->length / 4.0);

		if (bake) {
			mat4_t m  = mat4_mult_mat(o->base->transform->world_unpack, inv_parent);
			mat4_t nm = mat4_transpose3(mat4_inv(m));
			for (i32 j = 0; j < count; ++j) {
				i32    k = (voff + j) * 4;
				vec4_t p = (vec4_t){pos->buffer[j * 4] / 32767.0, pos->buffer[j * 4 + 1] / 32767.0, pos->buffer[j * 4 + 2] / 32767.0, 1.0};
				p        = vec4_apply_mat4(p, m);
				vec4_t n = (vec4_t){nor->buffer[j * 2] / 32767.0, nor->buffer[j * 2 + 1] / 32767.0, pos->buffer[j * 4 + 3] / 32767.0, 0.0};
				n        = vec4_norm(vec4_apply_mat4(n, nm));

				va0->buffer[k]                  = math_floor(p.x / max_scale * 32767);
				va0->buffer[k + 1]              = math_floor(p.y / max_scale * 32767);
				va0->buffer[k + 2]              = math_floor(p.z / max_scale * 32767);
				va0->buffer[k + 3]              = math_floor(n.z * 32767);
				va1->buffer[(voff + j) * 2]     = math_floor(n.x * 32767);
				va1->buffer[(voff + j) * 2 + 1] = math_floor(n.y * 32767);
			}
		}
		else {
			f32 scale = o->data->scale_pos;
			for (i32 j = 0; j < count; ++j) {
				i32 k              = (voff + j) * 4;
				va0->buffer[k]     = math_floor((pos->buffer[j * 4] * scale) / (float)max_scale);
				va0->buffer[k + 1] = math_floor((pos->buffer[j * 4 + 1] * scale) / (float)max_scale);
				va0->buffer[k + 2] = math_floor((pos->buffer[j * 4 + 2] * scale) / (float)max_scale);
				va0->buffer[k + 3] = pos->buffer[j * 4 + 3];
			}
			for (i32 j = 0; j < nor->length; ++j) {
				va1->buffer[j + voff * 2] = nor->buffer[j];
			}
		}

		voff += count;
	}
	return max_scale;
}

static mesh_data_t *util_mesh_build_merged_data(mesh_object_t_array_t *paint_objects, char *name, object_t *parent) {
	i32 vlen = 0;
	i32 ilen = 0;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		vlen += paint_objects->buffer[i]->data->vertex_arrays->buffer[0]->values->length;
		ilen += paint_objects->buffer[i]->data->index_array->length;
	}
	vlen                = math_floor(vlen / 4.0);
	i16_array_t *va0    = i16_array_create(vlen * 4);
	i16_array_t *va1    = i16_array_create(vlen * 2);
	i16_array_t *va2    = i16_array_create(vlen * 2);
	i16_array_t *vatex1 = mesh_data_get_vertex_array(paint_objects->buffer[0]->data, "tex1") != NULL ? i16_array_create(vlen * 2) : NULL;
	i16_array_t *vacol  = mesh_data_get_vertex_array(paint_objects->buffer[0]->data, "col") != NULL ? i16_array_create(vlen * 4) : NULL;
	i32          tex1i  = 3;
	i32          coli   = vatex1 != NULL ? 4 : 3;
	u32_array_t *ia     = u32_array_create(ilen);

	_util_mesh_atlas_build_slots();
	i32 atlas_stride = util_mesh_atlas_stride_merged;

	// Pos, nor
	f32 max_scale = util_mesh_pack_merged_positions(paint_objects, parent, va0, va1);

	i32 voff = 0;
	i32 ioff = 0;
	for (i32 i = 0; i < paint_objects->length; ++i) {
		vertex_array_t_array_t *vas = paint_objects->buffer[i]->data->vertex_arrays;
		u32_array_t            *ias = paint_objects->buffer[i]->data->index_array;

		// Tex
		i32 slot      = _util_mesh_atlas_slot_for_object(paint_objects->buffer[i]);
		f32 tile_step = 32767.0f / atlas_stride;
		f32 tile_x    = atlas_stride > 1 ? (slot % atlas_stride) * tile_step : 0.0f;
		f32 tile_y    = atlas_stride > 1 ? (slot / atlas_stride) * tile_step : 0.0f;
		for (i32 j = 0; j < vas->buffer[2]->values->length / 2; ++j) {
			va2->buffer[j * 2 + voff * 2]     = vas->buffer[2]->values->buffer[j * 2] / (float)atlas_stride + tile_x;
			va2->buffer[j * 2 + 1 + voff * 2] = vas->buffer[2]->values->buffer[j * 2 + 1] / (float)atlas_stride + tile_y;
		}
		// Tex1
		if (vatex1 != NULL) {
			for (i32 j = 0; j < vas->buffer[tex1i]->values->length; ++j) {
				vatex1->buffer[j + voff * 2] = vas->buffer[tex1i]->values->buffer[j];
			}
		}
		// Col
		if (vacol != NULL) {
			for (i32 j = 0; j < vas->buffer[coli]->values->length; ++j) {
				vacol->buffer[j + voff * 4] = vas->buffer[coli]->values->buffer[j];
			}
		}
		// Indices
		for (i32 j = 0; j < ias->length; ++j) {
			ia->buffer[j + ioff] = ias->buffer[j] + voff;
		}

		voff += math_floor(vas->buffer[0]->values->length / 4.0);
		ioff += math_floor(ias->length);
	}
	mesh_data_t *raw = ALLOC_INIT(mesh_data_t, {.name          = name,
	                                            .vertex_arrays = any_array_create_from_raw(
	                                                (void *[]){
	                                                    ALLOC_INIT(vertex_array_t, {.values = va0, .attrib = "pos", .data = "short4norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = va1, .attrib = "nor", .data = "short2norm"}),
	                                                    ALLOC_INIT(vertex_array_t, {.values = va2, .attrib = "tex", .data = "short2norm"}),
	                                                },
	                                                3),
	                                            .index_array = ia,
	                                            .scale_pos   = max_scale,
	                                            .scale_tex   = 1.0});
	if (vatex1 != NULL) {
		vertex_array_t *va = ALLOC_INIT(vertex_array_t, {.values = vatex1, .attrib = "tex1", .data = "short2norm"});
		any_array_push(raw->vertex_arrays, va);
	}
	if (vacol != NULL) {
		vertex_array_t *va = ALLOC_INIT(vertex_array_t, {.values = vacol, .attrib = "col", .data = "short4norm"});
		any_array_push(raw->vertex_arrays, va);
	}
	return raw;
}

static mesh_object_t_array_t *util_mesh_paintable(mesh_object_t_array_t *objects) {
	mesh_object_t_array_t *ar = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < objects->length; ++i) {
		any_array_push(ar, objects->buffer[i]);
	}
	return ar;
}

static void util_mesh_merged_toggled() {
	if (g_context->layer != NULL) {
		context_select_paint_object(g_context->paint_object);
	}
	render_path_raytrace_ready = false;
}

static void _util_mesh_merge(mesh_object_t_array_t *paint_objects, bool paintable_only) {
	if (paint_objects == NULL) {
		paint_objects = g_project->_->paint_objects;
	}
	if (paint_objects->length == 0) {
		return;
	}
	material_cache_mesh_changed(); // Material caches of the old mesh no longer match
	g_context->merged_object_is_atlas = paint_objects->length < g_project->_->paint_objects->length;

	// Meshes rendered with an override material are not painted
	mesh_object_t_array_t *paintable = paintable_only ? util_mesh_paintable(paint_objects) : NULL;
	if (paintable != NULL) {
		paint_objects = paintable;
	}
	if (paint_objects->length == 0) {
		if (g_context->merged_object != NULL) {
			util_mesh_remove_merged();
			util_mesh_merged_toggled();
		}
		util_mesh_merged_stale = false;
		array_delete(paintable);
		return;
	}

	object_t    *parent         = context_main_object()->base;
	bool         was_merged     = g_context->merged_object != NULL;
	bool         merged_visible = !was_merged || g_context->merged_object->base->visible;
	mesh_data_t *raw            = util_mesh_build_merged_data(paint_objects, g_context->paint_object->base->name, parent);

	util_mesh_remove_merged();
	util_mesh_merged_stale = false;
	if (util_mesh_merged_objects == NULL) {
		util_mesh_merged_objects = any_array_create(0);
	}
	for (i32 i = 0; i < paint_objects->length; ++i) {
		any_array_push(util_mesh_merged_objects, paint_objects->buffer[i]);
	}

	mesh_data_t *md                         = mesh_data_create(raw);
	md->_->owns_arrays                      = true;
	shader_data_t *paint_material           = g_project->_->materials->buffer[0]->data;
	g_context->merged_object                = mesh_object_create(md, paint_material);
	g_context->merged_object->base->name    = string("%s_merged", g_context->paint_object->base->name);
	g_context->merged_object->base->visible = merged_visible; // Painted instead of the objects when visible
	array_remove(scene_meshes, g_context->merged_object);     // Drawn by the paint passes only
	object_set_parent(g_context->merged_object->base, parent);
	transform_build_matrix(g_context->merged_object->base->transform);
	render_path_raytrace_ready = false;
	array_delete(paintable);
	if (!was_merged) {
		util_mesh_merged_toggled();
	}
}

bool util_mesh_merged_partial() {
	if (!g_context->merged_object_is_atlas || util_mesh_merged_objects == NULL) {
		return false;
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *po = g_project->_->paint_objects->buffer[i];
		if (po->base->visible && array_index_of(util_mesh_merged_objects, po) < 0) {
			return true;
		}
	}
	return false;
}

void util_mesh_merge(mesh_object_t_array_t *paint_objects) {
	_util_mesh_merge(paint_objects, true);
}

void util_mesh_merge_all() {
	if (g_context->merged_object == NULL || util_mesh_merged_objects == NULL || util_mesh_merged_objects->length != g_project->_->paint_objects->length) {
		_util_mesh_merge(NULL, false);
	}
}

bool util_mesh_merge_refresh() {
	mesh_object_t_array_t *objects = util_mesh_merged_objects;
	if (g_context->merged_object == NULL || objects == NULL || objects->length == 0) {
		return false;
	}
	mesh_data_t *md = g_context->merged_object->data;
	if (md->vertex_arrays->length < 2) {
		return false;
	}
	i16_array_t *va0  = md->vertex_arrays->buffer[0]->values;
	i16_array_t *va1  = md->vertex_arrays->buffer[1]->values;
	i32          vlen = 0;
	for (i32 i = 0; i < objects->length; ++i) {
		if (array_index_of(g_project->_->paint_objects, objects->buffer[i]) < 0) {
			return false; // Object was removed
		}
		vlen += objects->buffer[i]->data->vertex_arrays->buffer[0]->values->length;
	}
	vlen = math_floor(vlen / 4.0);
	if (vlen * 4 != va0->length || vlen * 2 != va1->length) {
		return false; // Vertex count changed
	}

	md->scale_pos = util_mesh_pack_merged_positions(objects, g_context->merged_object->base->parent, va0, va1);
	mesh_data_build_vertices(md->_->vertex_buffer, md->vertex_arrays);

	transform_t *t = g_context->merged_object->base->transform;
	t->scale_world = md->scale_pos;
	transform_build_matrix(t);
	return true;
}

void util_mesh_transform_changed() {
	if (g_context->merged_object != NULL && !util_mesh_merge_refresh()) {
		mesh_object_t_array_t *objects = any_array_create(0);
		if (util_mesh_merged_objects != NULL) {
			for (i32 i = 0; i < util_mesh_merged_objects->length; ++i) {
				if (array_index_of(g_project->_->paint_objects, util_mesh_merged_objects->buffer[i]) >= 0) {
					any_array_push(objects, util_mesh_merged_objects->buffer[i]);
				}
			}
		}
		util_mesh_merge(objects->length > 0 ? objects : NULL);
		array_delete(objects);
	}
	render_path_raytrace_ready = false;
}

void util_mesh_visibility_changed() {
	if (base_player) {
		util_mesh_merged_stale     = true;
		render_path_raytrace_ready = false;
		g_context->ddirty          = 2;
		return;
	}
	mesh_object_t_array_t *visibles = util_mesh_get_visible();
	util_mesh_merge(visibles);
	array_delete(visibles);
	util_uv_uvmap_cached       = false;
	util_uv_trianglemap_cached = false;
	util_uv_seammap_cached     = false;
	util_uv_dilatemap_cached   = false;
	g_context->ddirty          = 2;
}

void util_mesh_uv_unwrap(mesh_object_t_array_t *objects) {
	if (objects == NULL) {
		objects = g_project->_->paint_objects;
	}
	_util_mesh_merge(objects, false);
	if (g_context->merged_object == NULL) {
		return;
	}
	if (g_context->merged_object->data->index_array->length == 0) {
		util_mesh_merge(NULL);
		return;
	}

	mesh_data_t *mmd         = g_context->merged_object->data;
	i16_array_t *merged_posa = mmd->vertex_arrays->buffer[0]->values;
	i16_array_t *merged_nora = mmd->vertex_arrays->buffer[1]->values;
	u32_array_t *merged_inda = mmd->index_array;

	i16_array_t *posa      = i16_array_create(merged_posa->length);
	i16_array_t *nora      = i16_array_create(merged_nora->length);
	f32          max_scale = util_mesh_pack_merged_positions(util_mesh_merged_objects, NULL, posa, nora);

	u32_array_t *inda = malloc(sizeof(u32_array_t));
	inda->length = inda->capacity = merged_inda->length;
	inda->buffer                  = malloc(merged_inda->length * sizeof(u32));
	memcpy(inda->buffer, merged_inda->buffer, merged_inda->length * sizeof(u32));

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {.posa = posa, .nora = nora, .texa = NULL, .inda = inda});
	util_uv_unwrap_run(mesh);

	i32 ioff = 0;
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_data_t *md      = objects->buffer[i]->data;
		i32          ilen    = md->index_array->length;
		f32          rescale = max_scale / md->scale_pos;

		i16_array_t *new_posa = i16_array_create(ilen * 4);
		i16_array_t *new_nora = i16_array_create(ilen * 2);
		i16_array_t *new_texa = i16_array_create(ilen * 2);
		u32_array_t *new_inda = u32_array_create(ilen);

		for (i32 j = 0; j < ilen; ++j) {
			i32 src                     = (ioff + j) * 4;
			new_posa->buffer[j * 4]     = (i16)math_floor(mesh->posa->buffer[src] * rescale);
			new_posa->buffer[j * 4 + 1] = (i16)math_floor(mesh->posa->buffer[src + 1] * rescale);
			new_posa->buffer[j * 4 + 2] = (i16)math_floor(mesh->posa->buffer[src + 2] * rescale);
			new_posa->buffer[j * 4 + 3] = mesh->posa->buffer[src + 3];
		}
		for (i32 j = 0; j < ilen * 2; ++j) {
			new_nora->buffer[j] = mesh->nora->buffer[ioff * 2 + j];
		}
		for (i32 j = 0; j < ilen * 2; ++j) {
			new_texa->buffer[j] = mesh->texa->buffer[ioff * 2 + j];
		}
		for (i32 j = 0; j < ilen; ++j) {
			new_inda->buffer[j] = (u32)j;
		}

		md->vertex_arrays->buffer[0]->values = new_posa;
		md->vertex_arrays->buffer[1]->values = new_nora;
		md->vertex_arrays->buffer[2]->values = new_texa;
		md->index_array                      = new_inda;

		mesh_data_build(md);
		ioff += ilen;
	}

	free(mesh->posa->buffer);
	free(mesh->posa);
	free(mesh->nora->buffer);
	free(mesh->nora);
	free(mesh->texa->buffer);
	free(mesh->texa);
	free(mesh->inda->buffer);
	free(mesh->inda);

	util_mesh_merge(NULL);
	util_uv_uvmap_cached = false;
}

static i32 *_cc_he_vlo;
static i32 *_cc_he_vhi;

i32 util_mesh_calc_normals_sort(i32 *pa, i32 *pb) {
	i32 a    = *(pa);
	i32 b    = *(pb);
	i32 diff = util_mesh_va0->buffer[a * 4] - util_mesh_va0->buffer[b * 4];
	if (diff != 0)
		return diff;
	diff = util_mesh_va0->buffer[a * 4 + 1] - util_mesh_va0->buffer[b * 4 + 1];
	if (diff != 0)
		return diff;
	return util_mesh_va0->buffer[a * 4 + 2] - util_mesh_va0->buffer[b * 4 + 2];
}

static i32 _util_mesh_bevel_find(i32 *uf, i32 i) {
	while (uf[i] != i) {
		uf[i] = uf[uf[i]];
		i     = uf[i];
	}
	return i;
}

static void _util_mesh_bevel_union(i32 *uf, i32 a, i32 b) {
	a = _util_mesh_bevel_find(uf, a);
	b = _util_mesh_bevel_find(uf, b);
	if (a != b) {
		uf[a < b ? b : a] = a < b ? a : b;
	}
}

// Children follow their parents in the mesh list, layer object masks follow their meshes
static void util_mesh_collect_children(mesh_object_t_array_t *objects, mesh_object_t **out, i32 *count, object_t *parent) {
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *o = objects->buffer[i];
		object_t      *p = o->base->parent == _scene_root ? NULL : o->base->parent;
		if (p != parent) {
			continue;
		}
		out[(*count)++] = o;
		if (o->base->children->length > 0) {
			util_mesh_collect_children(objects, out, count, o->base);
		}
	}
}

static i32 util_mesh_remapped_mask(mesh_object_t **old_order, i32 length, i32 mask) {
	if (mask < 1 || mask > length) {
		return mask;
	}
	i32 index = array_index_of(g_project->_->paint_objects, old_order[mask - 1]);
	return index >= 0 ? index + 1 : mask;
}

static void util_mesh_sort_hierarchy_from(mesh_object_t **old_order) {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	i32                    length  = objects->length;
	mesh_object_t        **sorted  = calloc(length, sizeof(mesh_object_t *));
	i32                    count   = 0;
	util_mesh_collect_children(objects, sorted, &count, NULL);

	// Meshes parented outside of the list
	for (i32 i = 0; i < length && count < length; ++i) {
		bool collected = false;
		for (i32 j = 0; j < count; ++j) {
			if (sorted[j] == objects->buffer[i]) {
				collected = true;
				break;
			}
		}
		if (!collected) {
			sorted[count++] = objects->buffer[i];
		}
	}

	memcpy(objects->buffer, sorted, length * sizeof(mesh_object_t *));
	free(sorted);

	bool changed = false;
	for (i32 i = 0; i < length; ++i) {
		if (old_order[i] != objects->buffer[i]) {
			changed = true;
			break;
		}
	}
	if (!changed) {
		return;
	}

	if (g_project->_->layers != NULL) {
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			l->object_mask  = util_mesh_remapped_mask(old_order, length, l->object_mask);
		}
	}
	g_context->layer_filter = util_mesh_remapped_mask(old_order, length, g_context->layer_filter);
}

void util_mesh_sort_hierarchy() {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	if (objects == NULL || objects->length < 2) {
		return;
	}
	mesh_object_t **old_order = malloc(objects->length * sizeof(mesh_object_t *));
	memcpy(old_order, objects->buffer, objects->length * sizeof(mesh_object_t *));
	util_mesh_sort_hierarchy_from(old_order);
	free(old_order);
}

// Union-find over the vertices of g, joined by triangles and by equal positions
static i32 *_util_mesh_connected(mesh_data_t *g) {
	i16_array_t *va0       = g->vertex_arrays->buffer[0]->values;
	u32_array_t *inda      = g->index_array;
	i32          num_verts = math_floor(va0->length / 4.0);
	i32          num_inds  = math_floor(inda->length / 3.0) * 3;

	// Position-weld, vertices split by uv seams stay connected
	i32_array_t *weld_sort = i32_array_create(num_verts);
	i32         *uf        = malloc(num_verts * sizeof(i32));
	for (i32 i = 0; i < num_verts; ++i) {
		weld_sort->buffer[i] = i;
		uf[i]                = i;
	}
	util_mesh_va0 = va0;
	i32_array_sort(weld_sort, &util_mesh_calc_normals_sort);
	for (i32 i = 1; i < num_verts; ++i) {
		i32 curr = weld_sort->buffer[i];
		i32 prev = weld_sort->buffer[i - 1];
		if (va0->buffer[curr * 4] == va0->buffer[prev * 4] && va0->buffer[curr * 4 + 1] == va0->buffer[prev * 4 + 1] &&
		    va0->buffer[curr * 4 + 2] == va0->buffer[prev * 4 + 2]) {
			_util_mesh_bevel_union(uf, curr, prev);
		}
	}
	array_delete(weld_sort);

	for (i32 i = 0; i < num_inds; i += 3) {
		_util_mesh_bevel_union(uf, inda->buffer[i], inda->buffer[i + 1]);
		_util_mesh_bevel_union(uf, inda->buffer[i], inda->buffer[i + 2]);
	}
	return uf;
}

// Triangles connected to triangle tri, the same surface util_mesh_split separates
u32_array_t *util_mesh_connected_indices(mesh_data_t *g, i32 tri) {
	u32_array_t *inda     = g->index_array;
	i32          num_inds = math_floor(inda->length / 3.0) * 3;
	if (tri < 0 || tri * 3 >= num_inds) {
		return NULL;
	}
	i32         *uf   = _util_mesh_connected(g);
	i32          root = _util_mesh_bevel_find(uf, inda->buffer[tri * 3]);
	u32_array_t *out  = u32_array_create(0);
	for (i32 i = 0; i < num_inds; i += 3) {
		if (_util_mesh_bevel_find(uf, inda->buffer[i]) == root) {
			u32_array_push(out, inda->buffer[i]);
			u32_array_push(out, inda->buffer[i + 1]);
			u32_array_push(out, inda->buffer[i + 2]);
		}
	}
	free(uf);
	return out;
}

// Triangle of o hit by the ray, closest to its origin, -1 on a miss
i32 util_mesh_raycast_triangle(mesh_object_t *o, ray_t *ray) {
	i16_array_t *va0     = o->data->vertex_arrays->buffer[0]->values;
	u32_array_t *inda    = o->data->index_array;
	mat4_t       m       = o->base->transform->world_unpack;
	i32          closest = -1;
	f32          min_d   = 0.0;
	for (i32 i = 0; i + 2 < inda->length; i += 3) {
		vec4_t p[3];
		for (i32 k = 0; k < 3; ++k) {
			i32 v = inda->buffer[i + k] * 4;
			p[k]  = vec4_apply_mat4((vec4_t){va0->buffer[v] / 32767.0, va0->buffer[v + 1] / 32767.0, va0->buffer[v + 2] / 32767.0, 1.0}, m);
		}
		vec4_t hit = ray_intersect_triangle(ray, p[0], p[1], p[2], false);
		if (isnan(hit.x)) {
			continue;
		}
		f32 d = vec4_dist(hit, ray->origin);
		if (closest < 0 || d < min_d) {
			closest = i / 3;
			min_d   = d;
		}
	}
	return closest;
}

static bool util_mesh_is_unique_name(mesh_object_t *self, char *name) {
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p != self && string_equals(p->base->name, name)) {
			return false;
		}
	}
	return true;
}

char *util_mesh_unique_name(mesh_object_t *self, char *name) {
	if (util_mesh_is_unique_name(self, name)) {
		return name;
	}
	char *base;
	i32   i   = strings_split_number_ext(name, &base);
	char *res = string_tmp("%s%s", base, strings_number_ext(++i));
	while (!util_mesh_is_unique_name(self, res)) {
		res = string_tmp("%s%s", base, strings_number_ext(++i));
	}
	return res;
}

void util_mesh_rename(mesh_object_t *o, char *name) {
	name = string_copy(name);
	o->base->name = name;
	// Mesh data shared by linked duplicates is named after the object holding it
	if (o->data != NULL && util_mesh_data_owner(o->data) == array_index_of(g_project->_->paint_objects, o)) {
		o->data->name = string_copy(name);
	}
}
