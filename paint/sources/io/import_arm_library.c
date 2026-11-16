#include "../global.h"

// Materials, node groups, brushes and swatches from .arm files

extern string_array_t *import_arm_material_files; // import_arm_assets.c

void import_arm_run_material_from_project_on_next_frame(slot_material_t_array_t *imported) {
	for (i32 i = 0; i < imported->length; ++i) {
		slot_material_t *m  = imported->buffer[i];
		double           t0 = iron_time();
		context_set_material(m); // Compiles its paint material, node previews baked
		if (!import_arm_take_preview_given(m)) { // Given: a library thumbnail, the preview saved in the material file
			util_render_make_material_preview();
		}
		if (getenv("IRON_SHADER_TIMING") != NULL) {
			iron_log("[shader] imported material compiled and previewed: %.1f ms", (iron_time() - t0) * 1000.0);
		}
		iron_profile_mark("imported material ready");
	}
}

static bool import_arm_is_unique_group_name(char *name, ui_node_canvas_t_array_t *groups) {
	if (project_get_material_group_by_name(name) != NULL) {
		return false;
	}
	for (i32 i = 0; i < groups->length; ++i) {
		if (string_equals(groups->buffer[i]->name, name)) {
			return false;
		}
	}
	return true;
}

static char *import_arm_unique_group_name(char *name, ui_node_canvas_t_array_t *groups) {
	char *base;
	i32   i   = strings_split_number_ext(name, &base);
	char *res = string_tmp("%s%s", base, strings_number_ext(++i));
	while (!import_arm_is_unique_group_name(res, groups)) {
		res = string_tmp("%s%s", base, strings_number_ext(++i));
	}
	return res;
}

void import_arm_rename_group(char *name, char *new_name, slot_material_t_array_t *materials, ui_node_canvas_t_array_t *groups) {
	for (i32 i = 0; i < materials->length; ++i) {
		slot_material_t *m = materials->buffer[i];
		for (i32 i = 0; i < m->canvas->nodes->length; ++i) {
			ui_node_t *n = m->canvas->nodes->buffer[i];
			if (string_equals(n->type, "GROUP") && string_equals(n->name, name)) {
				n->name = string_copy(new_name);
			}
		}
	}
	for (i32 i = 0; i < groups->length; ++i) {
		ui_node_canvas_t *c = groups->buffer[i];
		for (i32 i = 0; i < c->nodes->length; ++i) {
			ui_node_t *n = c->nodes->buffer[i];
			if (string_equals(n->type, "GROUP") && string_equals(n->name, name)) {
				n->name = string_copy(new_name);
			}
		}
	}
}

// Channels and texture scale and rotation saved with material m
void import_arm_material_data(slot_material_t *m, material_data2_t *md) {
	m->paint_base   = md->paint_base;
	m->paint_opac   = md->paint_opac;
	m->paint_occ    = md->paint_occ;
	m->paint_rough  = md->paint_rough;
	m->paint_met    = md->paint_met;
	m->paint_nor    = md->paint_nor;
	m->paint_height = md->paint_height;
	m->paint_emis   = md->paint_emis;
	m->paint_subs   = md->paint_subs;
	m->uv_angle     = md->uv_angle;
}

void import_arm_import_materials(project_t *project, char *path, i32_array_t *selected, bool delete_blob) {
	if (project->material_nodes == NULL || project->material_nodes->length == 0) {
		if (delete_blob) {
			data_delete_blob(path);
		}
		return;
	}

	char *base                = path_base_dir(path);
	import_arm_material_files = any_array_create_from_raw((void *[]){}, 0);
	if (project->assets != NULL) {
		for (i32 i = 0; i < project->assets->length; ++i) {
			any_array_push(import_arm_material_files, import_arm_load_asset(project, project->assets->buffer[i], base));
		}
	}

	shader_data_t *m0 = data_get_shader("Scene", "Material");

	slot_material_t_array_t *imported = any_array_create_from_raw((void *[]){}, 0);

	for (i32 i = 0; i < project->material_nodes->length; ++i) {
		if (!import_arm_is_selected(selected, i)) {
			continue;
		}
		ui_node_canvas_t *c = util_clone_canvas(project->material_nodes->buffer[i]); // Project data is temporary
		import_arm_init_nodes(c->nodes);
		g_context->material = slot_material_create(m0, c);
		if (project->material_datas != NULL && i < project->material_datas->length) {
			// Channels and texture scale and rotation travel with the material file
			material_data2_t *md = project->material_datas->buffer[i];
			import_arm_material_data(g_context->material, md);
			g_context->material->uv_scale = md->uv_scale > 0.0 ? md->uv_scale : 1.0;
		}
		any_array_push(g_project->_->materials, g_context->material);
		any_array_push(imported, g_context->material);
		history_new_material();
		// The preview saved in the file (export_arm material_icons) instead of rendering it when the import compiles
		if (project->material_icons != NULL && i < project->material_icons->length &&
		    util_render_load_material_icon(g_context->material, project->material_icons->buffer[i])) {
			import_arm_set_preview_given(g_context->material);
		}
	}

	if (project->material_groups != NULL) {
		for (i32 i = 0; i < project->material_groups->length; ++i) {
			project->material_groups->buffer[i] = util_clone_canvas(project->material_groups->buffer[i]);
		}
		for (i32 i = 0; i < project->material_groups->length; ++i) {
			ui_node_canvas_t *c = project->material_groups->buffer[i];
			if (project_get_material_group_by_name(c->name) != NULL) { // Ensure unique group name
				char *old_name = c->name;
				c->name        = string_copy(import_arm_unique_group_name(old_name, project->material_groups));
				import_arm_rename_group(old_name, c->name, imported, project->material_groups);
			}
			import_arm_init_nodes(c->nodes);
			node_group_t *ng = ALLOC_INIT(node_group_t, {.canvas = c, .nodes = ui_nodes_create()});
			any_array_push(g_project->_->material_groups, ng);
		}
	}

	import_arm_material_files = NULL;
	sys_notify_on_next_frame(&import_arm_run_material_from_project_on_next_frame, imported);
	ui_nodes_group_stack = any_array_create_from_raw((void *[]){}, 0);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	if (delete_blob) {
		data_delete_blob(path);
	}
}

void import_arm_run_material_from_project(project_t *project, char *path) {
	import_arm_import_materials(project, path, NULL, true);
}

void import_arm_run_swatches_from_project(project_t *project, char *path, bool replace_existing) {
	history_replace_swatches(tr("Import Swatches"));

	if (replace_existing) {
		g_project->swatches = any_array_create_from_raw((void *[]){}, 0);

		if (project->swatches == NULL) { // No swatches contained
			any_array_push(g_project->swatches, project_make_swatch(0xffffffff));
		}
	}

	if (project->swatches != NULL) {
		for (i32 i = 0; i < project->swatches->length; ++i) {
			swatch_color_t *s = util_clone_swatch_color(project->swatches->buffer[i]);
			any_array_push(g_project->swatches, s);
		}
	}
	ui_dock_redraw(TAB_AREA_STATUS);
	data_delete_blob(path);
}

void import_arm_run_material(char *path) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		return;
	}
	project_t *project;
	if (import_arm_is_old(b)) {
		project = import_arm_from_old(b);
	}
	else {
		project = armpack_decode(b);
	}

	if (project == NULL || project->version == NULL) {
		data_delete_blob(path);
		return;
	}
	import_arm_run_material_from_project(project, path);
}

void import_arm_run_brush(char *path) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		return;
	}
	project_t *project = armpack_decode(b);
	if (project == NULL || project->version == NULL) {
		data_delete_blob(path);
		return;
	}
	import_arm_run_brush_from_project(project, path);
}

void import_arm_run_brush_from_project(project_t *project, char *path) {
	char *base = path_base_dir(path);
	for (i32 i = 0; project->assets != NULL && i < project->assets->length; ++i) {
		import_arm_load_asset(project, project->assets->buffer[i], base);
	}

	slot_brush_t_array_t *imported = any_array_create_from_raw((void *[]){}, 0);

	for (i32 i = 0; i < project->brush_nodes->length; ++i) {
		ui_node_canvas_t *c = util_clone_canvas(project->brush_nodes->buffer[i]);
		import_arm_init_nodes(c->nodes);
		g_context->brush = slot_brush_create(c);
		any_array_push(g_project->_->brushes, g_context->brush);
		any_array_push(imported, g_context->brush);
		make_material_parse_brush();
		brush_output_node_parse_inputs();
		util_render_make_brush_preview();
	}

	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	data_delete_blob(path);
}

void import_arm_run_swatches(char *path, bool replace_existing) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		return;
	}
	project_t *project = armpack_decode(b);
	if (project == NULL || project->version == NULL) {
		data_delete_blob(path);
		return;
	}
	import_arm_run_swatches_from_project(project, path, replace_existing);
}
