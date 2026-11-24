#include "../global.h"

#include <iron_profile.h>

// Deleted textures are only freed at the end of the frame, loading a project would hold the old layers,
// the new ones and their upload copies at once
static void import_arm_free_deleted() {
	if (gpu_in_use || !gpu_cleanup_pending()) {
		return;
	}
	gpu_execute_and_wait();
	gpu_cleanup();
}

// Materials whose preview was given before their import compiles on the next frame (a library thumbnail, the preview
// saved in the material file): that compile does not render their preview
static slot_material_t_array_t *import_arm_previews_given = NULL;

void import_arm_set_preview_given(slot_material_t *m) {
	if (import_arm_previews_given == NULL) {
		import_arm_previews_given = any_array_create_from_raw((void *[]){}, 0);
	}
	any_array_push(import_arm_previews_given, m);
}

// Whether m's preview was given, forgetting it
bool import_arm_take_preview_given(slot_material_t *m) {
	if (import_arm_previews_given == NULL || array_index_of(import_arm_previews_given, m) < 0) {
		return false;
	}
	array_remove(import_arm_previews_given, m);
	return true;
}

// Materials of the opened project whose preview was not saved with it, rendered one per frame after the load
static slot_material_t_array_t *import_arm_preview_queue   = NULL;
static project_t               *import_arm_preview_project = NULL;
static bool                     import_arm_preview_pending = false;

static void import_arm_preview_queue_clear() {
	if (import_arm_preview_queue != NULL) {
		import_arm_preview_queue->length = 0;
	}
}

static void import_arm_preview_next(void *_) {
	import_arm_preview_pending = false;
	if (import_arm_preview_queue == NULL || import_arm_preview_queue->length == 0 || g_project != import_arm_preview_project) {
		import_arm_preview_queue_clear();
		return;
	}
	slot_material_t *m = import_arm_preview_queue->buffer[0];
	array_remove(import_arm_preview_queue, m);
	if (array_index_of(g_project->_->materials, m) >= 0 && !m->preview_ready) { // Not deleted, not rendered meanwhile
		slot_material_t *selected   = g_context->material;
		draw_saved_t     saved_draw = draw_suspend();
		g_context->material         = m;
		make_material_bake_node_previews();
		util_render_make_material_preview();
		g_context->material = selected;
		if (selected != m) {
			make_material_bake_node_previews(); // The selected material's node previews again (the bake replaced them)
		}
		draw_resume(saved_draw);
	}
	if (import_arm_preview_queue->length > 0) {
		import_arm_preview_pending = true;
		sys_notify_on_next_frame(&import_arm_preview_next, NULL);
	}
}

static void import_arm_preview_queue_add(slot_material_t *m) {
	if (import_arm_preview_queue == NULL) {
		import_arm_preview_queue = any_array_create_from_raw((void *[]){}, 0);
	}
	any_array_push(import_arm_preview_queue, m);
	import_arm_preview_project = g_project;
	if (!import_arm_preview_pending) {
		import_arm_preview_pending = true;
		sys_notify_on_next_frame(&import_arm_preview_next, NULL);
	}
}

static void import_arm_run_project_on_next_frame(void *_) {
	// Once envmap is imported
	scene_world->strength       = g_project->envmap_strength;
	g_context->envmap_angle     = g_project->envmap_angle;
	g_context->show_envmap_blur = g_project->envmap_blur;
	if (g_context->show_envmap_blur) {
		scene_world->_->envmap = scene_world->_->radiance_mipmaps->buffer[0];
	}
}

// Counts and indices of a project file are checked before anything reads them: missing lists become empty, indices out
// of range point nowhere (-1). False when the file can not be a project (no layer or no mesh).
static bool import_arm_validate(project_t *p, bool as_mesh) {
	if (p->assets == NULL) {
		p->assets = string_array_create(0);
	}
	if (p->mesh_datas == NULL || p->mesh_datas->length == 0) {
		return false;
	}
	if (as_mesh) {
		return true;
	}
	if (p->layer_datas == NULL || p->layer_datas->length == 0) {
		return false;
	}
	layer_data_t *l0 = p->layer_datas->buffer[0];
	if (l0->res_x < 1 || l0->res_x > 16384 || l0->res_y < 1 || l0->res_y > 16384 || (l0->bpp != 8 && l0->bpp != 16 && l0->bpp != 32)) {
		return false;
	}
	if (p->mesh_transforms == NULL || p->mesh_transforms->length < p->mesh_datas->length) {
		return false;
	}
	for (i32 i = 0; i < p->mesh_datas->length; ++i) {
		f32_array_t *t = p->mesh_transforms->buffer[i];
		if (t == NULL || t->length < 16) {
			return false;
		}
	}
	if (p->material_nodes == NULL) {
		p->material_nodes = any_array_create_from_raw((void *[]){}, 0);
	}
	if (p->material_datas == NULL || p->material_datas->length < p->material_nodes->length) {
		return false;
	}
	if (p->brush_nodes == NULL) {
		p->brush_nodes = any_array_create_from_raw((void *[]){}, 0);
	}
	if (p->camera_origin == NULL || p->camera_origin->length < 3 || (p->camera_world != NULL && p->camera_world->length < 16)) {
		p->camera_world = NULL; // The default camera
	}
	i32 layers    = p->layer_datas->length;
	i32 materials = p->material_nodes->length;
	for (i32 i = 0; i < layers; ++i) {
		layer_data_t *ld = p->layer_datas->buffer[i];
		if (ld->parent <= i || ld->parent >= layers) { // A parent sits above its masks and children
			ld->parent = -1;
		}
		if (ld->path_material >= materials) {
			ld->path_material = -1;
		}
	}
	if (p->mesh_parents != NULL) {
		for (i32 i = 0; i < p->mesh_parents->length; ++i) {
			if (p->mesh_parents->buffer[i] >= p->mesh_datas->length || p->mesh_parents->buffer[i] == i) {
				p->mesh_parents->buffer[i] = -1;
			}
		}
	}
	return true;
}

// Uids handed out after a load: above the saved counter and every uid of the project
static void import_arm_uid_next(u32 saved) {
	u32 next = saved > 0 ? saved : 1;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		u32 uid = g_project->_->layers->buffer[i]->uid;
		next    = uid >= next ? uid + 1 : next;
	}
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		u32 uid = g_project->_->materials->buffer[i]->uid;
		next    = uid >= next ? uid + 1 : next;
	}
	project_uid_next = next;
}

void import_arm_run_project(char *path) {
	iron_profile_mark("project load start");
	base_draw_progress(0.0);
	project_t *project;
	bool       import_as_mesh = false;
	buffer_t  *b              = NULL;
	if (project_container_is(path)) { // Version 31: the head, the index and the live layers are read
		export_tiled_abort_for_project_change(); // Before the open resets the store its states are in
		if (!project_leave()) { // Saved first, also when it is this file: what is read is what was just saved
			return;
		}
		project = project_container_open(path);
	}
	else if ((b = data_get_blob(path)) == NULL) {
		console_error(string("Could not open file %s.", path));
		return;
	}
	else if (!import_arm_has_version(b)) { // A mesh, opened in a new project
		scene_t *scene_raw = armpack_decode(b);
		if (scene_raw == NULL || scene_raw->mesh_datas == NULL) {
			console_error(string("Could not open file %s.", path));
			return;
		}
		export_tiled_abort_for_project_change();
		if (!project_leave()) {
			return;
		}
		import_as_mesh = true;
		project        = ALLOC_INIT(project_t, {.mesh_datas = scene_raw->mesh_datas});
	}
	else {
		project = import_arm_decode(b);
	}

	if (project == NULL) {
		i32 version = b != NULL ? import_arm_version(b) : -1;
		console_error(string(version > atoi(manifest_version_project) ? "Could not open file %s, it was saved by a newer version."
		                     : version >= 0 && version < manifest_version_oldest ? "Could not open file %s, it was saved by an older version."
		                                                                         : "Could not open file %s.",
		                     path));
		return;
	}

	if (project->version != NULL && project->layer_datas == NULL) {
		// Import as material
		if (project->material_nodes != NULL) {
			import_arm_run_material_from_project(project, path);
		}
		// Import as brush
		else if (project->brush_nodes != NULL) {
			import_arm_run_brush_from_project(project, path);
		}
		// Import as swatches
		else if (project->swatches != NULL) {
			import_arm_run_swatches_from_project(project, path, false);
		}
		return;
	}

	if (!import_arm_validate(project, import_as_mesh)) {
		console_error(string("Could not open file %s.", path));
		return;
	}

	g_context->layers_preview_dirty = true;
	g_context->layer_filter         = 0;
	base_draw_progress(0.1);

	i32 progress_assets = project->assets->length;
	i32 progress_loaded = 0;
	i32 progress_bytes  = 1;
	if (project->packed_assets != NULL) {
		for (i32 i = 0; i < project->packed_assets->length; ++i) {
			packed_asset_t *pa = project->packed_assets->buffer[i];
			progress_bytes += ((buffer_t *)pa->bytes)->length;
		}
	}
	import_arm_progress_unpacked = 0;

	project_new(import_as_mesh);
	project_container_attach(); // After project_new reset the store
	g_project->_->filepath = import_as_mesh ? "" : string_copy(path); // A mesh is not where the project saves
	ui_files_filename      = string_copy(substring(path, string_last_index_of(path, PATH_SEP) + 1, string_last_index_of(path, ".")));
#ifdef IRON_IOS
	sys_title_set(ui_files_filename);
#else
	sys_title_set(string("%s - %s", ui_files_filename, manifest_title));
#endif

	// Import as mesh instead
	if (import_as_mesh) {
		import_arm_run_mesh(project);
		return;
	}

// Save to recent
	char *recent_path = project_recent_entry(path);
	string_array_t *recent = g_config->recent_projects;
	string_array_remove(recent, recent_path);
	array_insert(recent, 0, recent_path);
	config_save();

	project->_ = g_project->_; // Carry over runtime arrays set up by project_new
	g_project  = project;
	project_uuid = g_project->uuid != NULL ? string_copy(g_project->uuid) : NULL; // A new one for older projects
	// Unsaved work of a session that did not end (a crash, iOS ended the app in the background) is laid over the file
	bool recovered = false;
	if (project_uuid != NULL && hstore_journal_pending(project_uuid)) {
		i32 blobs = hstore_recover();
		recovered = project_session_overlay(g_project);
		if (recovered) {
			iron_log("Unsaved work recovered (%d blobs)", blobs);
		}
	}
	layer_data_t *l0                     = g_project->layer_datas->buffer[0];
	i32           res_x                  = l0->res_x;
	i32           res_y                  = l0->res_y;
	base_res                             = res_x == res_y ? config_get_texture_res_pos(res_x) : TEXTURE_RES_CUSTOM;
	base_res_x                           = (f32)res_x;
	base_res_y                           = (f32)res_y;
	base_bits                            = l0->bpp == 8 ? TEXTURE_BITS_BITS8 : l0->bpp == 16 ? TEXTURE_BITS_BITS16 : TEXTURE_BITS_BITS32;

	char *base = path_base_dir(path);
	if (g_project->envmap != NULL) {
		g_project->envmap = import_arm_abs(g_project->envmap, base);
		g_project->envmap = string_copy(string_replace_all(g_project->envmap, "\\", "/"));
		g_project->envmap = path_normalize(g_project->envmap);
	}

	if (g_project->camera_world != NULL) {
		scene_camera->base->transform->local = mat4_from_f32_array(g_project->camera_world, 0);
		transform_decompose(scene_camera->base->transform);
		scene_camera->data->fov             = g_project->camera_fov;
		scene_camera->data->near_plane      = g_project->camera_near;
		scene_camera->data->far_plane       = g_project->camera_far;
		scene_camera->data->frustum_culling = g_project->camera_frustum_culling;
		camera_object_build_proj(scene_camera, -1.0);
		f32_array_t *origin = g_project->camera_origin;
		camera_origins[0]   = (vec4_t){origin->buffer[0], origin->buffer[1], origin->buffer[2], 1.0};
	}

	// Gather the encoded images first, so they can be decoded in parallel
	i32                 image_count = g_project->assets->length;
	import_arm_image_t *images      = calloc(image_count > 0 ? image_count : 1, sizeof(import_arm_image_t));
	for (i32 i = 0; i < image_count; ++i) {
		char *file = g_project->assets->buffer[i];
		file = string_copy(string_replace_all(file, "\\", "/"));
		// Convert image path from relative to absolute
		char *abs = import_arm_abs(file, base);
		if (g_project->packed_assets != NULL) {
			abs                = string_copy(path_normalize(abs));
			packed_asset_t *pa = import_arm_take_packed_asset(g_project, abs, file, false);
			if (pa != NULL) {
				images[i].bytes  = pa->bytes;
				images[i].format = pa->name;
				images[i].packed = true;
			}
		}
		if (images[i].bytes == NULL && any_map_get(data_cached_textures, abs) == NULL && import_arm_is_decodable(abs) && iron_file_exists(abs)) {
			images[i].from_disk = true;
			images[i].format    = abs;
		}
		images[i].abs = abs;
	}

	i32 decoded_end = 0;
	for (i32 i = 0; i < image_count; ++i) {
		if (i == decoded_end) {
			decoded_end = import_arm_decode_images(images, i, image_count);
		}
		import_arm_image_t *im  = &images[i];
		char               *abs = im->abs;
		if (im->decoded) {
			if (any_map_get(data_cached_textures, abs) == NULL) {
				any_map_set(data_cached_textures, abs, gpu_create_texture_from_pixels(&im->pixels));
			}
			else { // Listed twice
				free(im->pixels.data);
			}
		}
		if (im->packed) {
			import_arm_progress_unpacked += im->bytes->length;
		}
		else if (im->bytes != NULL) {
			iron_delete_blob(im->bytes);
		}
		if (any_map_get(data_cached_textures, abs) == NULL && !iron_file_exists(abs)) {
			import_arm_make_pink(abs);
		}
		bool hdr_as_envmap = path_has_ext(abs, "hdr") && g_project->envmap != NULL && string_equals(g_project->envmap, path_normalize(abs));
		import_texture_run(abs, hdr_as_envmap);
		progress_loaded++;
		base_draw_progress(0.1 + 0.7 * (0.8 * import_arm_progress_unpacked / (f32)progress_bytes + 0.2 * progress_loaded / (f32)progress_assets));
	}
	free(images);

	if (g_project->font_assets != NULL) {
		for (i32 i = 0; i < g_project->font_assets->length; ++i) {
			char *file = g_project->font_assets->buffer[i];
			file = string_copy(string_replace_all(file, "\\", "/"));
			// Convert font path from relative to absolute
			char *abs = import_arm_abs(file, base);
			if (iron_file_exists(abs)) {
				import_font_run(abs);
			}
		}
	}


	string_array_t      *mesh_names = string_array_create(0);
	mesh_data_t_array_t *mesh_datas = import_arm_get_mesh_datas(g_project, mesh_names);

	base_draw_progress(0.8);
	mesh_data_t *md = mesh_datas->buffer[0];

	mesh_object_set_data(g_context->paint_object, md);
	g_context->paint_object->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
	transform_build_matrix(g_context->paint_object->base->transform);
	g_context->paint_object->base->name = mesh_names->buffer[0];
	g_project->_->paint_objects         = any_array_create_from_raw(
        (void *[]){
            g_context->paint_object,
        },
        1);

	for (i32 i = 1; i < mesh_datas->length; ++i) {
		mesh_object_t *object = scene_add_mesh_object(mesh_datas->buffer[i], g_context->paint_object->material, g_context->paint_object->base);
		object->base->name    = mesh_names->buffer[i];
		any_array_push(g_project->_->paint_objects, object);
	}

	transform_set_matrix(g_context->paint_object->base->transform, mat4_from_f32_array(g_project->mesh_transforms->buffer[0], 0));
	for (i32 i = 1; i < g_project->mesh_datas->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		transform_set_matrix(o->base->transform, mat4_from_f32_array(g_project->mesh_transforms->buffer[i], 0));
	}

	if (g_project->mesh_assets != NULL && g_project->mesh_assets->length > 0) {
		char *file             = g_project->mesh_assets->buffer[0];
		char *abs              = import_arm_abs(file, base);
		g_project->mesh_assets = any_array_create_from_raw(
		    (void *[]){
		        abs,
		    },
		    1);
	}

	context_select_paint_object(context_main_object());

	gpu_texture_t *tex = g_project->_->layers->buffer[0]->texpaint;
	if (tex->width != config_get_texture_res_x() || tex->height != config_get_texture_res_y()) {
		layers_resize_targets();
	}

	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_unload(l);
	}
	import_arm_free_deleted(); // The old layers go before the new ones are allocated
	g_project->_->layers = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->layer_datas->length; ++i) {
		layer_data_t *ld       = g_project->layer_datas->buffer[i];
		bool          is_group = ld->type == LAYER_SLOT_TYPE_GROUP;
		bool          is_mask  = ld->type == LAYER_SLOT_TYPE_MASK;
		slot_layer_t *l        = slot_layer_create("", is_mask ? LAYER_SLOT_TYPE_MASK : is_group ? LAYER_SLOT_TYPE_GROUP : LAYER_SLOT_TYPE_LAYER, NULL);
		if (ld->name != NULL) {
			l->name = string_copy(ld->name);
		}
		if (ld->uid != 0) {
			l->uid = ld->uid;
		}
		l->timeline = ld->timeline;
		l->visible  = ld->visible;
		any_array_push(g_project->_->layers, l);
		if (!is_group) {
			// See-through meshes when the bottom layer is not fully opaque
			project_container_layer_restore(ld, l, i == 0 ? &g_project->_->translucency : NULL);
			project_container_layer_thumb_restore(ld, l);

			l->uv_map = ld->uv_map;
			if (ld->decal_mat != NULL) {
				l->decal_mat = mat4_from_f32_array(ld->decal_mat, 0);
			}
			l->mask_opacity = ld->opacity_mask;
			if (i == 0 && l->mask_opacity < 1.0) {
				g_project->_->translucency = true;
			}
			l->object_mask = ld->object_mask;
			l->blending    = ld->blending;

			l->paint_base         = ld->paint_base;
			l->paint_opac         = ld->paint_opac;
			l->paint_occ          = ld->paint_occ;
			l->paint_rough        = ld->paint_rough;
			l->paint_met          = ld->paint_met;
			l->paint_nor          = ld->paint_nor;
			l->paint_nor_blend    = ld->paint_nor_blend;
			l->paint_height       = ld->paint_height;
			l->paint_height_blend = ld->paint_height_blend;
			l->height_raise       = ld->height_raise;
			if (is_mask && ld->mask_effects != NULL && ld->mask_effects->length > 0) {
				l->mask_effects     = mask_stack_copy_effects(ld->mask_effects); // Owned, the decoded one lives in the project blob
				l->mask_stack_dirty = true;                                       // Evaluated before the first frame renders
			}
			l->paint_emis         = ld->paint_emis;
			l->paint_subs         = ld->paint_subs;
			l->path_points        = ld->path_points;
			l->path_points_world  = ld->path_points_world;
			l->path_points_camera = ld->path_points_camera;
			l->path_points_parent = ld->path_points_parent;
			l->path_tool          = ld->path_tool;
			l->path_curved        = ld->path_curved;
			l->path_text          = ld->path_text;

			import_arm_free_deleted(); // Upload textures of one layer at a time
		}
	}

	base_draw_progress(0.85);

	if (g_project->timelines != NULL) {
		timeline_io_decode((u8_array_t *)g_project->timelines); // The layers name their timeline, references at the end
	}
	if (g_project->history != NULL) {
		u8_array_t *h = (u8_array_t *)g_project->history;
		history_io_decode(h->buffer, h->length); // Steps name layers, materials and timelines by uid
	}

	// Assign parents to groups and masks
	for (i32 i = 0; i < g_project->layer_datas->length; ++i) {
		layer_data_t *ld = g_project->layer_datas->buffer[i];
		if (ld->parent >= 0) {
			g_project->_->layers->buffer[i]->parent = g_project->_->layers->buffer[ld->parent];
		}
		// Layer references saved the index of their layer, it has an id now
		mask_stack_effects_from_file(g_project->_->layers->buffer[i]->mask_effects, g_project->_->layers->buffer, g_project->_->layers->length);
	}

	// Layer
	g_context->layer        = g_project->_->layers->buffer[0];
	ui_view2d_hwnd->redraws = 2;
	if (slot_layer_get_object_mask(g_context->layer) > 0 || g_context->layer_filter > 0) {
		layers_set_object_mask();
	}
	make_material_parse_mesh_material();

	// Materials
	shader_data_t *m0       = data_get_shader("Scene", "Material");
	g_project->_->materials = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->material_nodes->length; ++i) {
		ui_node_canvas_t *n = g_project->material_nodes->buffer[i];
		import_arm_init_nodes(n->nodes);
		g_context->material = slot_material_create(m0, n);

		material_data2_t *md = g_project->material_datas->buffer[i];
		import_arm_material_data(g_context->material, md);
		g_context->material->uv_scale = md->uv_scale;
		if (md->uid != 0) {
			g_context->material->uid = md->uid;
		}

		any_array_push(g_project->_->materials, g_context->material);
	}

	import_arm_uid_next(g_project->next_uid);

	ui_nodes_hwnd->redraws        = 2;
	ui_nodes_group_stack          = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->material_groups = any_array_create_from_raw((void *[]){}, 0);
	if (g_project->material_groups != NULL) {
		for (i32 i = 0; i < g_project->material_groups->length; ++i) {
			ui_node_canvas_t *g = g_project->material_groups->buffer[i];
			import_arm_init_nodes(g->nodes);
			node_group_t *ng = ALLOC_INIT(node_group_t, {.canvas = g, .nodes = ui_nodes_create()});
			any_array_push(g_project->_->material_groups, ng);
		}
	}

	// Previews saved with the project are used as they are. The others render after the load, one per frame: rendering
	// a preview compiles the material and renders its Material Maker buffers, seconds for heavy ones on the iPad.
	buffer_t_array_t *icons = g_project->material_icons;
	import_arm_preview_queue_clear();
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		g_context->material = g_project->_->materials->buffer[i];
		if (!(icons != NULL && i < icons->length && util_render_load_material_icon(g_context->material, icons->buffer[i]))) {
			import_arm_preview_queue_add(g_context->material);
		}
	}
	make_material_parse_paint_material(true); // The selected material: compiled, its node previews baked

	g_project->_->brushes = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->brush_nodes->length; ++i) {
		ui_node_canvas_t *n = g_project->brush_nodes->buffer[i];
		import_arm_init_nodes(n->nodes);
		g_context->brush = slot_brush_create(n);
		any_array_push(g_project->_->brushes, g_context->brush);
		make_material_parse_brush();
		brush_output_node_parse_inputs();
		util_render_make_brush_preview();
	}

	// Fill layers and path layers materials
	for (i32 i = 0; i < g_project->layer_datas->length; ++i) {
		layer_data_t *ld       = g_project->layer_datas->buffer[i];
		slot_layer_t *l        = g_project->_->layers->buffer[i];
		if (ld->type != LAYER_SLOT_TYPE_GROUP) {
			l->fill          = fill_decode(ld->fill, true);
			l->fill_dirty    = l->fill != NULL; // Rasterized again for its readers
			l->path_material = ld->path_material > -1 ? g_project->_->materials->buffer[ld->path_material] : NULL;
		}
	}

	base_draw_progress(0.9);
	if (g_project->mesh_parents != NULL) {
		for (i32 i = 0; i < g_project->_->paint_objects->length && i < g_project->mesh_parents->length; ++i) {
			i32       parent_index = g_project->mesh_parents->buffer[i];
			object_t *parent       = parent_index >= 0 ? g_project->_->paint_objects->buffer[parent_index]->base : NULL;
			object_set_parent(g_project->_->paint_objects->buffer[i]->base, parent);
		}
		transform_build_matrix(_scene_root->transform);
	}

	base_draw_progress(0.95);
	util_mesh_sort_hierarchy();

	if (g_context->merged_object == NULL) {
		util_mesh_merge(NULL);
	}
	if (slot_layer_get_object_mask(g_context->layer) > 0 || g_context->layer_filter > 0) {
		layers_set_object_mask();
	}
	else {
		context_select_paint_object(context_main_object());
		if (g_context->merged_object != NULL) {
			g_context->merged_object->base->visible = true;
		}
	}

	base_draw_progress(1.0);
	sys_notify_on_next_frame(&import_arm_run_project_on_next_frame, NULL);

	base_update_tools();
	g_context->ddirty = 4;
	data_delete_blob(path);
	project_container_loaded();
	if (recovered) {
		project_modified = true; // Not in the file yet: saved like any change
	}
	iron_profile_mark("project load end");
}

// A file with a version: a project head, material, brush or swatches. Meshes have none.
bool import_arm_has_version(buffer_t *b) {
	// 0xdf map, i32 count, 0xdb string, i32 len, "version"
	return b != NULL && b->length > 10 && b->buffer[10] == 'v';
}

// The version a project, material, brush or swatches file was saved with, -1 for none or one that does not read
i32 import_arm_version(buffer_t *b) {
	// 0xdf map, i32 count, 0xdb string, i32 len, "version", 0xdb string, i32 len, version value
	if (!import_arm_has_version(b) || b->length < 22 || b->buffer[17] != 0xdb) {
		return -1;
	}
	u32 len;
	memcpy(&len, b->buffer + 18, 4); // Version value length
	if (len == 0 || len > 8 || len > b->length - 22) {
		return -1;
	}
	i32 v = 0;
	for (u32 i = 0; i < len; ++i) {
		u8 c = b->buffer[22 + i];
		if (c < '0' || c > '9') {
			return -1;
		}
		v = v * 10 + (c - '0');
	}
	return v;
}

// ---- Upgrades ----
// A field whose meaning changes bumps manifest_version_project and gets a step here: it turns what older versions
// saved into what this build reads. raw is the file by key (armpack_decode_to_map), with the fields this build no
// longer has. Fields only added, removed or moved need no step: the decode matches them by name.

static any_map_t *import_arm_raw_layer(any_map_t *raw, i32 i) {
	any_array_t *a = any_map_get(raw, "layer_datas");
	return a != NULL && i < (i32)a->length ? a->buffer[i] : NULL;
}

// 32: a fill layer or mask is a fill (fill_t, a source per channel), no longer fill_material with uv_scale, uv_rot and
// uv_type
static void import_arm_upgrade_32(project_t *p, any_map_t *raw) {
	i32 materials = p->material_datas != NULL ? p->material_datas->length : 0;
	for (i32 i = 0; p->layer_datas != NULL && i < p->layer_datas->length; ++i) {
		layer_data_t *ld = p->layer_datas->buffer[i];
		any_map_t    *rl = import_arm_raw_layer(raw, i);
		if (ld == NULL || rl == NULL || any_map_get(rl, "fill_material") == NULL) {
			continue;
		}
		i32 material = armpack_map_get_i32(rl, "fill_material");
		if (material < 0 || material >= materials) {
			continue;
		}
		i32     uv_type = armpack_map_get_i32(rl, "uv_type");
		fill_t *f       = fill_create(uv_type == UV_TYPE_TRIPLANAR ? FILL_PROJECTION_TRIPLANAR
		                              : uv_type == UV_TYPE_PROJECT ? FILL_PROJECTION_DECAL
		                                                           : FILL_PROJECTION_UV);
		for (i32 c = 0; c < FILL_CHANNEL_COUNT; ++c) {
			f->ch[c].kind     = FILL_SOURCE_MATERIAL;
			f->ch[c].material = (u32)material; // The index layer_data_t.fill holds (fill_encode by index)
		}
		f32 scale   = armpack_map_get_f32(rl, "uv_scale");
		f->scale[0] = f->scale[1] = scale > 0.0 ? scale : 1.0;
		f->angle                  = armpack_map_get_f32(rl, "uv_rot"); // Degrees then too
		ld->fill                  = fill_encode(f, false);              // Not by index: the material as set, no images (-1)
		free(f);
	}
}

// 33: a mask's height blend is a MASK_EFFECT_HEIGHT_BLEND effect on top of its stack, no longer the height_blend fields
static void import_arm_upgrade_33(project_t *p, any_map_t *raw) {
	for (i32 i = 0; p->layer_datas != NULL && i < p->layer_datas->length; ++i) {
		layer_data_t *ld = p->layer_datas->buffer[i];
		any_map_t    *rl = import_arm_raw_layer(raw, i);
		if (ld == NULL || rl == NULL || armpack_map_get_i32(rl, "height_blend") == 0) {
			continue;
		}
		mask_effect_t e             = mask_effect_default(MASK_EFFECT_HEIGHT_BLEND);
		e.params[MASK_HB_COMPARE]   = armpack_map_get_i32(rl, "height_blend_below") != 0 ? MASK_HB_COMPARE_BELOW : MASK_HB_COMPARE_FLAT;
		e.params[MASK_HB_INFLUENCE] = armpack_map_get_f32(rl, "height_blend_influence");
		e.params[MASK_HB_SHARPNESS] = armpack_map_get_f32(rl, "height_blend_sharpness");
		e.params[MASK_HB_OFFSET]    = armpack_map_get_f32(rl, "height_blend_offset");
		e.params[MASK_HB_REACH]     = armpack_map_get_f32(rl, "height_blend_reach") * 1024.0; // Was uv, texels of a 1024 mask now
		for (i32 k = 0; k < MASK_EFFECT_PARAMS; ++k) {
			e.params[k] = mask_effect_param_clamp(MASK_EFFECT_HEIGHT_BLEND, k, e.params[k]);
		}
		f32_array_t *old = ld->mask_effects;
		i32          n   = old != NULL ? old->length / MASK_EFFECT_STRIDE : 1; // NULL: the implicit paint effect
		f32_array_t *a   = f32_array_create((n + 1) * MASK_EFFECT_STRIDE);
		if (old != NULL) {
			memcpy(a->buffer, old->buffer, sizeof(f32) * n * MASK_EFFECT_STRIDE);
		}
		else {
			mask_stack_pack(a->buffer, mask_effect_default(MASK_EFFECT_PAINT));
		}
		mask_stack_pack(a->buffer + n * MASK_EFFECT_STRIDE, e);
		ld->mask_effects = a;
	}
}

static void import_arm_upgrade(project_t *p, buffer_t *b, i32 version) {
	if (p->layer_datas == NULL || p->layer_datas->length == 0) {
		return; // Materials, brushes and swatches: no step changes them
	}
	any_map_t *raw = armpack_decode_to_map(b);
	if (raw == NULL) {
		return;
	}
	if (version < 32) {
		import_arm_upgrade_32(p, raw);
	}
	if (version < 33) {
		import_arm_upgrade_33(p, raw);
	}
}

// A project head, material, brush or swatches file of this build's version or an older one down to
// manifest_version_oldest, read as this build's version. NULL for others, files of newer builds too.
project_t *import_arm_decode(buffer_t *b) {
	i32 version = import_arm_version(b);
	i32 current = atoi(manifest_version_project);
	if (version < manifest_version_oldest || version > current) {
		if (import_arm_has_version(b)) {
			iron_log(version > current ? "Saved by a newer version, not read" : "Saved by an older version, not read");
		}
		return NULL;
	}
	project_t *p = util_decode_project(b);
	if (p == NULL) {
		return NULL;
	}
	if (version < current) {
		import_arm_upgrade(p, b, version);
	}
	p->version = string_copy(manifest_version_project);
	return p;
}
