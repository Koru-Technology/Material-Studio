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

bool import_arm_is_selected(i32_array_t *selected, i32 i) {
	return selected == NULL || (i < selected->length && selected->buffer[i] != 0);
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

