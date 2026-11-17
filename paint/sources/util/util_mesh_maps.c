
#include "../global.h"

// Mesh maps: the baked maps of the paint meshes registered with the project, one per mesh_map_t, by asset name
// (g_project->mesh_maps, reimporting a texture replaces its asset_t). Mask generators read them through mesh_maps_texture()
// or the _texmeshmap<type> shader link. Without a registered curvature map the curvature is baked from the paint meshes.

char *mesh_map_name(i32 type) {
	switch (type) {
	case MESH_MAP_AO:
		return "Ambient Occlusion";
	case MESH_MAP_CURVATURE:
		return "Curvature";
	case MESH_MAP_CAVITY:
		return "Cavity";
	case MESH_MAP_THICKNESS:
		return "Thickness";
	case MESH_MAP_POSITION:
		return "Position";
	case MESH_MAP_WORLD_NORMAL:
		return "World Space Normal";
	case MESH_MAP_NORMAL:
		return "Normal";
	}
	return "";
}

// File name suffix the bake dialog writes, also matched on texture import
char *mesh_map_suffix(i32 type) {
	switch (type) {
	case MESH_MAP_AO:
		return "ao";
	case MESH_MAP_CURVATURE:
		return "curvature";
	case MESH_MAP_CAVITY:
		return "cavity";
	case MESH_MAP_THICKNESS:
		return "thickness";
	case MESH_MAP_POSITION:
		return "position";
	case MESH_MAP_WORLD_NORMAL:
		return "world_normal";
	case MESH_MAP_NORMAL:
		return "normal";
	}
	return "";
}

// Map type of a file named <name>_<suffix>.<ext>, -1 for none
i32 mesh_map_from_file(char *path) {
	string_array_t *parts = string_split(path, PATH_SEP);
	char           *stem  = parts->buffer[parts->length - 1];
	i32             dot   = string_last_index_of(stem, ".");
	if (dot > 0) {
		stem = substring(stem, 0, dot);
	}
	if (ends_with(stem, "_bent_normal")) {
		return -1;
	}
	i32 best = -1;
	for (i32 i = 0; i < MESH_MAP_COUNT; ++i) {
		char *suffix = string_tmp("_%s", mesh_map_suffix(i));
		if (ends_with(stem, suffix) && (best < 0 || string_length(mesh_map_suffix(i)) > string_length(mesh_map_suffix(best)))) {
			best = i; // The longest wins, _world_normal over _normal
		}
	}
	return best;
}

asset_t *mesh_maps_get_asset(i32 type) {
	string_array_t *maps = g_project->mesh_maps;
	if (maps == NULL || type < 0 || type >= maps->length || maps->buffer[type] == NULL || string_equals(maps->buffer[type], "")) {
		return NULL;
	}
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		if (string_equals(a->name, maps->buffer[type])) {
			return a;
		}
	}
	return NULL;
}

static void mesh_maps_reparse(void *_) {
	make_material_parse_paint_material(true);
	make_material_parse_mesh_material();
	fill_tile_invalidate(NULL); // Materials reading the maps
	util_render_make_material_preview();
}

// Curvature Mask nodes sample a registered curvature map instead of their own bake
static void mesh_maps_check_curvature(bool had_curvature) {
	if ((mesh_maps_get_asset(MESH_MAP_CURVATURE) != NULL) != had_curvature) {
		sys_notify_on_next_frame(&mesh_maps_reparse, NULL);
	}
}

// NULL clears the map
void mesh_maps_set(i32 type, asset_t *asset) {
	if (type < 0 || type >= MESH_MAP_COUNT) {
		return;
	}
	bool had_curvature = mesh_maps_get_asset(MESH_MAP_CURVATURE) != NULL;
	if (g_project->mesh_maps == NULL) {
		g_project->mesh_maps = any_array_create_from_raw((void *[]){}, 0);
	}
	while (g_project->mesh_maps->length < MESH_MAP_COUNT) { // Older projects have fewer types
		any_array_push(g_project->mesh_maps, "");
	}
	g_project->mesh_maps->buffer[type] = asset != NULL ? string_copy(asset->name) : "";
	g_context->ddirty                  = 2;
	mesh_maps_check_curvature(had_curvature);
}

// The asset is deleted
void mesh_maps_asset_removed(asset_t *asset) {
	bool            had_curvature = mesh_maps_get_asset(MESH_MAP_CURVATURE) != NULL;
	string_array_t *maps          = g_project->mesh_maps;
	for (i32 i = 0; maps != NULL && i < maps->length; ++i) {
		if (maps->buffer[i] != NULL && string_equals(maps->buffer[i], asset->name)) {
			maps->buffer[i] = "";
		}
	}
	mesh_maps_check_curvature(had_curvature);
}

static project_t *mesh_maps_project = NULL; // Project seen by the last update

// Baked mesh maps are named <name>_<map>: the first of a kind is used as the project's map.
// A project being opened keeps the maps it was saved with.
void mesh_maps_texture_imported(asset_t *asset) {
	i32 type = mesh_map_from_file(asset->file);
	if (type < 0 || g_project != mesh_maps_project) {
		return;
	}
	string_array_t *maps = g_project->mesh_maps;
	if (maps == NULL || type >= maps->length || maps->buffer[type] == NULL || string_equals(maps->buffer[type], "")) {
		mesh_maps_set(type, asset);
	}
}

#define MESH_MAPS_CURVATURE_ID  "meshmap_curvature" // Not a material preview id "m<id>_", kept by make_material_delete_node_previews
#define MESH_MAPS_CURVATURE_RES 1024

static gpu_texture_t *mesh_maps_gray      = NULL;
static gpu_texture_t *mesh_maps_flat      = NULL;
static bool           mesh_maps_neutral   = false; // Material icons render a sphere, the model's maps do not fit it
static u64            mesh_maps_baked_key = 0;     // Meshes the auto curvature was baked from
static u64            mesh_maps_wait_key  = 0;
static i32            mesh_maps_wait      = 0;
static u32            mesh_maps_version   = 0; // Bumped when the meshes change in place

static gpu_texture_t *mesh_maps_constant(f32 r, f32 g, f32 b) {
	buffer_t *buf = buffer_create(16);
	buffer_set_f32(buf, 0, r);
	buffer_set_f32(buf, 4, g);
	buffer_set_f32(buf, 8, b);
	buffer_set_f32(buf, 12, 1.0);
	gpu_texture_t *t = gpu_create_texture_from_bytes(buf, 1, 1, GPU_TEXTURE_FORMAT_RGBA128);
	array_free(buf);
	free(buf);
	return t;
}

static gpu_texture_t *mesh_maps_neutral_texture(i32 type) {
	if (type == MESH_MAP_CURVATURE) {
		if (mesh_maps_gray == NULL) {
			mesh_maps_gray = mesh_maps_constant(0.5, 0.5, 0.5);
		}
		return mesh_maps_gray;
	}
	if (type == MESH_MAP_NORMAL || type == MESH_MAP_WORLD_NORMAL) {
		if (mesh_maps_flat == NULL) {
			mesh_maps_flat = mesh_maps_constant(0.5, 0.5, 1.0);
		}
		return mesh_maps_flat;
	}
	return type == MESH_MAP_POSITION ? render_path_empty_black() : render_path_empty_white();
}

// Curvature baked from the paint meshes while none is registered, NULL until baked
gpu_texture_t *mesh_maps_auto_curvature() {
	return g_context->node_previews != NULL ? any_map_get(g_context->node_previews, MESH_MAPS_CURVATURE_ID) : NULL;
}

// Registered map, or the auto baked curvature, or a neutral texture (white AO, gray curvature, ...). Never NULL.
// Curvature is signed: 0.5 flat, brighter ridges, darker grooves.
gpu_texture_t *mesh_maps_texture(i32 type) {
	if (!mesh_maps_neutral) {
		gpu_texture_t *image = project_get_image(mesh_maps_get_asset(type));
		if (image != NULL) {
			return image;
		}
		if (type == MESH_MAP_CURVATURE && mesh_maps_auto_curvature() != NULL) {
			return mesh_maps_auto_curvature();
		}
	}
	return mesh_maps_neutral_texture(type);
}

// Neutral maps while a material icon renders
void mesh_maps_preview_neutral(bool on) {
	mesh_maps_neutral = on;
}

// The paint meshes changed (import, unwrap): the registered maps no longer fit them
void mesh_maps_mesh_changed() {
	for (i32 i = 0; i < MESH_MAP_COUNT; ++i) {
		if (mesh_maps_get_asset(i) != NULL) {
			mesh_maps_stale = true;
			break;
		}
	}
	mesh_maps_version++;
	make_curvature_mesh_changed();
}

static u64 mesh_maps_meshes_key() {
	mesh_object_t_array_t *objs = g_project->_->paint_objects;
	u64                    key  = 1469598103934665603ull ^ mesh_maps_version;
	for (i32 i = 0; i < objs->length; ++i) {
		mesh_object_t *o = objs->buffer[i];
		u64            v = (u64)(uintptr_t)o->data ^ ((u64)(o->data != NULL && o->data->index_array != NULL ? o->data->index_array->length : 0) << 40);
		key              = (key ^ v) * 1099511628211ull;
	}
	return key;
}

// Every update: bakes the curvature again once the paint meshes changed and settled, while no curvature map is registered
void mesh_maps_update() {
	if (g_project != mesh_maps_project) { // New or loaded project
		mesh_maps_project = g_project;
		mesh_maps_stale   = false;
	}
	if (g_project->_->paint_objects->length == 0 || mesh_maps_get_asset(MESH_MAP_CURVATURE) != NULL) {
		return;
	}
	u64 key = mesh_maps_meshes_key();
	if (key == mesh_maps_baked_key && mesh_maps_auto_curvature() != NULL) {
		return;
	}
	// Meshes come in over a few frames while importing
	if (key != mesh_maps_wait_key) {
		mesh_maps_wait_key = key;
		mesh_maps_wait     = 0;
		return;
	}
	if (++mesh_maps_wait < 2) {
		return;
	}
	i32 res = math_min(MESH_MAPS_CURVATURE_RES, config_get_texture_res_x());
	make_curvature_bake_mesh(MESH_MAPS_CURVATURE_ID, g_project->_->paint_objects, res, res, 1.0, 1.0, 0.0, 3, false, true, 2.0);
	mesh_maps_baked_key = key;
	g_context->ddirty   = 2;
}

// Changes when the meshes change in place or the auto curvature is baked again (mask generator signatures)
u64 mesh_maps_generation() {
	return ((u64)mesh_maps_version << 32) ^ mesh_maps_baked_key;
}

// mesh_maps_texture of the model, also while a material icon renders (mask generators render into the layers)
gpu_texture_t *mesh_maps_texture_model(i32 type) {
	bool neutral      = mesh_maps_neutral;
	mesh_maps_neutral = false;
	gpu_texture_t *t  = mesh_maps_texture(type);
	mesh_maps_neutral = neutral;
	return t;
}
