#include "../global.h"

// Meshes from .arm files: a mesh only file, or meshes appended from a project

static void import_arm_run_mesh_on_next_frame(void *_) {
	layers_init();
}

mesh_data_t_array_t *import_arm_get_mesh_datas(project_t *project, string_array_t *mesh_names) {
	mesh_data_t_array_t *mesh_datas = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < project->mesh_datas->length; ++i) {
		mesh_data_t *raw = project->mesh_datas->buffer[i];
		i32          source_index;
		char        *object_name;
		if (i > 0 && util_mesh_link_parse(raw->name, &source_index, &object_name)) {
			any_array_push(mesh_datas, NULL);
			string_array_push(mesh_names, object_name);
		}
		else {
			mesh_data_t *md = mesh_data_create(raw);
			any_array_push(mesh_datas, md);
			string_array_push(mesh_names, md->name);
		}
	}
	for (i32 i = 1; i < mesh_datas->length; ++i) {
		if (mesh_datas->buffer[i] != NULL) {
			continue;
		}
		i32   source_index;
		char *object_name;
		util_mesh_link_parse(((mesh_data_t *)project->mesh_datas->buffer[i])->name, &source_index, &object_name);
		bool linked           = source_index >= 0 && source_index < mesh_datas->length && mesh_datas->buffer[source_index] != NULL;
		mesh_datas->buffer[i] = linked ? mesh_datas->buffer[source_index] : mesh_datas->buffer[0];
	}
	return mesh_datas;
}

void import_arm_run_mesh(project_t *raw) {
	g_project->_->paint_objects = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < raw->mesh_datas->length; ++i) {
		mesh_data_t   *md     = mesh_data_create(raw->mesh_datas->buffer[i]);
		mesh_object_t *object = NULL;
		if (i == 0) {
			mesh_object_set_data(g_context->paint_object, md);
			object = g_context->paint_object;
		}
		else {
			object             = scene_add_mesh_object(md, g_context->paint_object->material, g_context->paint_object->base);
			object->base->name = md->name;
		}
		object->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
		transform_build_matrix(object->base->transform);
		object->base->name = md->name;
		any_array_push(g_project->_->paint_objects, object);
		util_mesh_merge(NULL);
		viewport_scale_to_bounds(2.0);
	}
	sys_notify_on_next_frame(&import_arm_run_mesh_on_next_frame, NULL);
	mesh_maps_mesh_changed();
	history_reset();
}

project_t *import_arm_decode_project(buffer_t *b) {
	if (!import_arm_has_version(b)) { // Mesh only
		scene_t *scene = armpack_decode(b);
		return ALLOC_INIT(project_t, {.mesh_datas = scene->mesh_datas});
	}
	if (import_arm_is_old(b)) {
		return import_arm_from_old(b);
	}
	return armpack_decode(b);
}

bool import_arm_is_selected(i32_array_t *selected, i32 i) {
	return selected == NULL || (i < selected->length && selected->buffer[i] != 0);
}

static bool import_arm_has_selected(i32_array_t *selected) {
	if (selected == NULL) {
		return true;
	}
	for (i32 i = 0; i < selected->length; ++i) {
		if (selected->buffer[i] != 0) {
			return true;
		}
	}
	return false;
}

bool import_arm_object_name_exists(char *name) {
	if (name == NULL || g_project->_->paint_objects == NULL) {
		return false;
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		if (o->base->name != NULL && string_equals(o->base->name, name)) {
			return true;
		}
	}
	return false;
}

i32 import_arm_material_name_index(char *name) {
	if (name == NULL) {
		return -1;
	}
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		if (m->canvas->name != NULL && string_equals(m->canvas->name, name)) {
			return i;
		}
	}
	return -1;
}

static i32 import_arm_mesh_parent_index(project_t *project, i32 mesh_i) {
	if (project->mesh_parents != NULL && mesh_i < project->mesh_parents->length) {
		return project->mesh_parents->buffer[mesh_i];
	}
	return mesh_i > 0 ? 0 : -1;
}

static mat4_t import_arm_mesh_world_matrix(project_t *project, i32 mesh_i) {
	f32_array_t_array_t *transforms = project->mesh_transforms;
	mat4_t               m          = mat4_from_f32_array(transforms->buffer[mesh_i], 0);
	i32                  parent     = import_arm_mesh_parent_index(project, mesh_i);
	for (i32 guard = 0; guard < transforms->length && parent >= 0 && parent < transforms->length; ++guard) {
		m      = mat4_mult_mat3x4(m, mat4_from_f32_array(transforms->buffer[parent], 0));
		parent = import_arm_mesh_parent_index(project, parent);
	}
	return m;
}

static void import_arm_run_mesh_append_from_project(project_t *project, i32_array_t *selected) {
	if (project->mesh_datas == NULL || project->mesh_datas->length == 0) {
		return;
	}

	string_array_t      *mesh_names = string_array_create(0);
	mesh_data_t_array_t *mesh_datas = import_arm_get_mesh_datas(project, mesh_names);

	i32            appended = 0;
	mesh_object_t *first    = NULL;
	for (i32 i = 0; i < mesh_datas->length; ++i) {
		if (!import_arm_is_selected(selected, i)) {
			continue;
		}
		if (import_arm_object_name_exists(mesh_names->buffer[i])) {
			continue;
		}
		mesh_data_t   *md     = mesh_datas->buffer[i];
		mesh_object_t *object = scene_add_mesh_object(md, g_context->paint_object->material, NULL);
		object->base->name    = mesh_names->buffer[i];

		if (project->mesh_transforms != NULL && i < project->mesh_transforms->length) {
			transform_set_matrix(object->base->transform, import_arm_mesh_world_matrix(project, i));
		}
		else {
			object->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
			transform_build_matrix(object->base->transform);
		}

		any_array_push(g_project->_->paint_objects, object);
		appended++;
		if (first == NULL) {
			first = object;
		}

	}

	if (appended == 0) {
		return;
	}

	if (g_project->_->paint_objects->length > 1) {
		util_mesh_merge(NULL);
		if (g_context->merged_object != NULL) {
			g_context->merged_object->base->visible = true;
		}
	}
	context_select_paint_object(first);

	make_material_parse_paint_material(true);
	make_material_parse_mesh_material();
	base_update_tools();

	util_uv_uvmap_cached       = false;
	util_uv_trianglemap_cached = false;
	util_uv_seammap_cached     = false;
	util_uv_dilatemap_cached   = false;
	g_context->ddirty          = 4;
	mesh_maps_mesh_changed(); // The registered maps miss the appended meshes
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

void import_arm_append(project_t *project, char *path, i32_array_t *mesh_selected, i32_array_t *material_selected) {
	if (project->material_nodes != NULL && project->material_nodes->length > 0) {
		// Selected materials not already in the project (by name)
		i32_array_t *to_import = i32_array_create(project->material_nodes->length);
		for (i32 i = 0; i < to_import->length; ++i) {
			ui_node_canvas_t *c  = project->material_nodes->buffer[i];
			bool              in = import_arm_material_name_index(c != NULL ? c->name : NULL) >= 0;
			to_import->buffer[i] = !in && import_arm_is_selected(material_selected, i) ? 1 : 0;
		}
		if (import_arm_has_selected(to_import)) {
			import_arm_import_materials(project, path, to_import, false);
		}
	}

	if (project->mesh_datas != NULL && import_arm_has_selected(mesh_selected)) {
		import_arm_run_mesh_append_from_project(project, mesh_selected);
	}
	data_delete_blob(path);
}
