
#include "../global.h"

// Material maps in the order of the PBR output sockets they connect to
static i32 import_folder_sockets[IMPORT_FOLDER_MAPS] = {0, 1, 2, 3, 4, 5, 7}; // Base, opacity, occlusion, roughness, metallic, normal, height

void import_folder_place_image_node(ui_nodes_t *nodes, ui_node_canvas_t *canvas, char *file, i32 ny, i32 to_id, i32 to_socket) {
	// Added to the given canvas, not the one shown in the node editor (the library builds materials in the background)
	ui_node_t *n = ui_nodes_make_node(nodes_material_get_node_t("TEX_IMAGE"), nodes, canvas);
	any_array_push(canvas->nodes, n);
	n->buttons->buffer[0]->default_value = f32_array_create_x(base_get_asset_index_by_file(file)); // Full path, file names repeat across folders
	n->x                                 = 72;
	n->y                                 = ny;
	ui_node_link_t *l =
	    ALLOC_INIT(ui_node_link_t, {.id = ui_next_link_id(canvas->links), .from_id = n->id, .from_socket = 0, .to_id = to_id, .to_socket = to_socket});
	any_array_push(canvas->links, l);
}

// Picks the maps of a material out of a folder listing, returns false when there are none
bool import_folder_find_maps(string_array_t *files, char **maps) {
	bool found = false;
	for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
		maps[i] = NULL;
	}
	for (i32 i = 0; i < files->length; ++i) {
		char *f = files->buffer[i];
		if (!path_is_texture(f)) {
			continue;
		}
		char *base = to_lower_case(substring(f, 0, string_last_index_of(f, ".")));
		bool  is[IMPORT_FOLDER_MAPS] = {
            path_is_base_color_tex(base), path_is_opacity_tex(base),  path_is_occlusion_tex(base),    path_is_roughness_tex(base),
            path_is_metallic_tex(base),   path_is_normal_map_tex(base), path_is_displacement_tex(base),
        };
		for (i32 m = 0; m < IMPORT_FOLDER_MAPS; ++m) {
			if (maps[m] == NULL && is[m]) {
				maps[m] = string_copy(f);
				found   = true;
			}
		}
	}
	return found;
}

// Material wired to the textures of a folder, the textures have to be imported already.
// The material is not added to the project.
slot_material_t *import_folder_make_material(char *name, char *dir, char **maps) {
	slot_material_t  *m      = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	ui_nodes_t       *nodes  = m->nodes;
	ui_node_canvas_t *canvas = m->canvas;
	canvas->name             = string_copy(slot_material_unique_name(canvas, name));
	ui_node_t *nout          = NULL;
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
			nout = n;
			break;
		}
	}
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->name, "Color")) {
			ui_remove_node(n, canvas);
			break;
		}
	}

	i32 pos     = 0;
	i32 start_y = 100;
	i32 node_h  = 180;
	for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
		if (maps[i] != NULL) {
			char *file = string("%s%s%s", dir, PATH_SEP, maps[i]);
			import_folder_place_image_node(nodes, canvas, file, start_y + node_h * pos, nout->id, import_folder_sockets[i]);
			pos++;
		}
	}
	return m;
}

void import_folder_run(char *path) {
	string_array_t *files = file_read_directory(path);
	char           *maps[IMPORT_FOLDER_MAPS];
	if (!import_folder_find_maps(files, maps)) {
		console_info(tr("Folder does not contain textures"));
		return;
	}
	for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
		if (maps[i] != NULL) {
			import_texture_run(string("%s%s%s", path, PATH_SEP, maps[i]), false);
		}
	}

	string_array_t *dirs = string_split(path, PATH_SEP);
	g_context->material  = import_folder_make_material(dirs->buffer[dirs->length - 1], path, maps);
	any_array_push(g_project->_->materials, g_context->material);

	make_material_parse_paint_material(true);
	util_render_make_material_preview();
	ui_dock_redraw(1);
	history_new_material();
}
