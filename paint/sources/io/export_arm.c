
#include "../global.h"

// Compressed layer pixels, without keeping the full size readback in memory
static buffer_t *export_arm_encode_layer_texture(gpu_texture_t *t) {
	buffer_t *b = lz4_encode(gpu_get_texture_pixels(t));
	gpu_release_texture_pixels(t);
	return b;
}

// A material preview (RGBA64) compressed, in RGBA order
static buffer_t *export_arm_encode_icon(gpu_texture_t *t) {
#ifdef IRON_BGRA
	return lz4_encode(buffer_bgra64_swap(gpu_get_texture_pixels(t)));
#else
	return lz4_encode(gpu_get_texture_pixels(t));
#endif
}

static mesh_data_t *export_arm_named_mesh_data(mesh_object_t *p) {
	// Mesh data shared by linked duplicates is named after the object holding it
	if (string_equals(p->data->name, p->base->name)) {
		return p->data;
	}
	mesh_data_t *renamed = calloc(1, sizeof(mesh_data_t));
	*renamed             = *p->data;
	renamed->name        = string_copy(p->base->name);
	return renamed;
}

static mesh_data_t *export_arm_linked_mesh_data(mesh_object_t *p, i32 source_index) {
	return ALLOC_INIT(mesh_data_t, {.name          = util_mesh_link_name(source_index, p->base->name),
	                                .scale_pos     = p->data->scale_pos,
	                                .scale_tex     = p->data->scale_tex,
	                                .vertex_arrays = any_array_create_from_raw((void *[]){}, 0),
	                                .index_array   = u32_array_create(0)});
}

void export_arm_run_mesh(char *path, mesh_object_t_array_t *paint_objects) {
	mesh_data_t_array_t *mesh_datas = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < paint_objects->length; ++i) {
		mesh_object_t *p = paint_objects->buffer[i];
		any_array_push(mesh_datas, export_arm_named_mesh_data(p));
	}

	scene_t  *raw = ALLOC_INIT(scene_t, {.mesh_datas = mesh_datas});
	buffer_t *b   = util_encode_scene(raw);

	if (!ends_with(path, ".arm")) {
		path = string("%s.arm", path);
	}
	if (!iron_file_save_bytes(path, b, 0)) {
		console_error(string("Could not save %s.", path));
	}
	array_free(b);
	free(b);
}

void export_arm_export_node(ui_node_t *n, asset_t_array_t *assets) {
	if (string_equals(n->type, "TEX_IMAGE")) {
		i32 index = n->buttons->buffer[0]->default_value->buffer[0];
		if (index < 0 || index >= g_project->_->assets->length) { // 9999 - Texture deleted
			n->buttons->buffer[0]->data = u8_array_create_from_string("");
			return;
		}
		n->buttons->buffer[0]->data = u8_array_create_from_string(base_combo_enum_texts(n->type)->buffer[index]);
		if (assets != NULL) {
			asset_t *asset = g_project->_->assets->buffer[index];
			if (array_index_of(assets, asset) == -1) {
				any_array_push(assets, asset);
			}
		}
	}
}

string_array_t *export_arm_assets_to_files(char *project_path, asset_t_array_t *assets) {
	string_array_t *texture_files = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < assets->length; ++i) {
		asset_t *a = assets->buffer[i];
#ifdef IRON_IOS
		bool same_drive = false;
#else
		bool same_drive = char_at(project_path, 0)[0] == char_at(a->file, 0)[0];
#endif
		// Convert image path from absolute to relative
		if (same_drive) {
			any_array_push(texture_files, path_to_relative(project_path, a->file));
		}
		else {
			any_array_push(texture_files, a->file);
		}
	}
	return texture_files;
}

string_array_t *export_arm_meshes_to_files(char *project_path) {
	string_array_t *mesh_files = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->mesh_assets->length; ++i) {
		char *file = g_project->mesh_assets->buffer[i];
#ifdef IRON_IOS
		bool same_drive = false;
#else
		bool same_drive = char_at(project_path, 0)[0] == char_at(file, 0)[0];
#endif
		// Convert mesh path from absolute to relative
		if (same_drive) {
			any_array_push(mesh_files, path_to_relative(project_path, file));
		}
		else {
			any_array_push(mesh_files, file);
		}
	}
	return mesh_files;
}

string_array_t *export_arm_fonts_to_files(char *project_path, slot_font_t_array_t *fonts) {
	string_array_t *font_files = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 1; i < fonts->length; ++i) {
		slot_font_t *f = fonts->buffer[i];
#ifdef IRON_IOS
		bool same_drive = false;
#else
		bool same_drive = char_at(project_path, 0)[0] == char_at(f->file, 0)[0];
#endif
		// Convert font path from absolute to relative
		if (same_drive) {
			any_array_push(font_files, path_to_relative(project_path, f->file));
		}
		else {
			any_array_push(font_files, f->file);
		}
	}
	return font_files;
}

static bool export_arm_show_progress = false;
static bool export_arm_v30           = false; // Save as v30: readable by builds from before 31, uids are not saved

// Version files are written in. 31 only with project_v31 on (or env PROJECT_V31=1 for test runs): builds from before
// it, installed on the same device, cannot open it
static char *export_arm_version() {
	char *e   = getenv("PROJECT_V31");
	bool  v31 = g_config->project_v31 || (e != NULL && atoi(e) != 0);
	return v31 && !export_arm_v30 ? manifest_version_project : "30";
}

static void export_arm_progress(f32 progress) {
	if (export_arm_show_progress) {
		ui_header_draw_progress(progress);
	}
}

static void export_arm_free_buffer(buffer_t *b) {
	if (b != NULL) {
		array_free(b);
		free(b);
	}
}

buffer_t *export_arm_encode_project(char *path) {
	// Path is where the project is imported from, asset paths are relative to it

	export_arm_progress(0.0);

	ui_node_canvas_t_array_t *mnodes = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t  *m = g_project->_->materials->buffer[i];
		ui_node_canvas_t *c = util_clone_canvas(m->canvas);
		for (i32 i = 0; i < c->nodes->length; ++i) {
			ui_node_t *n = c->nodes->buffer[i];
			export_arm_export_node(n, NULL);
		}
		any_array_push(mnodes, c);
	}

	ui_node_canvas_t_array_t *bnodes = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->brushes->length; ++i) {
		slot_brush_t     *b = g_project->_->brushes->buffer[i];
		ui_node_canvas_t *c = util_clone_canvas(b->canvas);
		for (i32 i = 0; i < c->nodes->length; ++i) {
			ui_node_t *n = c->nodes->buffer[i];
			export_arm_export_node(n, NULL);
		}
		any_array_push(bnodes, c);
	}

	ui_node_canvas_t_array_t *mgroups = NULL;
	if (g_project->_->material_groups->length > 0) {
		mgroups = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
			node_group_t     *g = g_project->_->material_groups->buffer[i];
			ui_node_canvas_t *c = util_clone_canvas(g->canvas);
			for (i32 i = 0; i < c->nodes->length; ++i) {
				ui_node_t *n = c->nodes->buffer[i];
				export_arm_export_node(n, NULL);
			}
			any_array_push(mgroups, c);
		}
	}

	material_data2_t_array_t *mdata2 = NULL;
	if (g_project->_->materials->length > 0) {
		mdata2 = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t  *m = g_project->_->materials->buffer[i];
			material_data2_t *d = ALLOC_INIT(material_data2_t, {
			                                                       .paint_base   = m->paint_base,
			                                                       .paint_opac   = m->paint_opac,
			                                                       .paint_occ    = m->paint_occ,
			                                                       .paint_rough  = m->paint_rough,
			                                                       .paint_met    = m->paint_met,
			                                                       .paint_nor    = m->paint_nor,
			                                                       .paint_height = m->paint_height,
			                                                       .paint_emis   = m->paint_emis,
			                                                       .paint_subs   = m->paint_subs,
			                                                       .uv_scale     = m->uv_scale,
			                                                       .uv_angle     = m->uv_angle,
			                                                       .uid          = m->uid,
			                                                   });
			any_array_push(mdata2, d);
		}
	}

	mesh_data_t_array_t *md = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p      = g_project->_->paint_objects->buffer[i];
		i32            source = util_mesh_data_owner(p->data);
		any_array_push(md, source >= 0 && source < i ? export_arm_linked_mesh_data(p, source) : export_arm_named_mesh_data(p));
	}

	char           *relative_to   = string_equals(g_project->_->filepath, "") ? path : g_project->_->filepath;
	string_array_t *texture_files = export_arm_assets_to_files(relative_to, g_project->_->assets);
	string_array_t *font_files    = export_arm_fonts_to_files(relative_to, g_project->_->fonts);
	string_array_t *mesh_files    = export_arm_meshes_to_files(relative_to);

	i32 bits_pos = base_bits;
	i32 bpp      = bits_pos == TEXTURE_BITS_BITS8 ? 8 : bits_pos == TEXTURE_BITS_BITS16 ? 16 : 32;

	layer_data_t_array_t *ld        = any_array_create_from_raw((void *[]){}, 0);
	bool                  container = project_container_writing(); // Pixels go in chunks (io/project_container.c)
	export_arm_progress(0.1);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		export_arm_progress(0.1 + 0.6 * i / (f32)g_project->_->layers->length);
		slot_layer_t *l = g_project->_->layers->buffer[i];
		layer_data_t *d =
		    ALLOC_INIT(layer_data_t, {.name               = l->name,
		                              .res                = l->texpaint != NULL ? l->texpaint->width : config_get_texture_res_x(),
		                              .bpp                = bpp,
		                              .texpaint           = l->texpaint != NULL && !container ? export_arm_encode_layer_texture(l->texpaint) : NULL,
		                              .uv_scale           = l->scale,
		                              .uv_rot             = l->angle,
		                              .uv_type            = l->uv_type,
		                              .uv_map             = l->uv_map,
		                              .decal_mat          = l->uv_type == UV_TYPE_PROJECT ? mat4_to_f32_array(l->decal_mat) : NULL,
		                              .opacity_mask       = l->mask_opacity,
		                              .fill_material      = l->fill_material != NULL ? array_index_of(g_project->_->materials, l->fill_material) : -1,
		                              .object_mask        = l->object_mask,
		                              .blending           = l->blending,
		                              .parent             = l->parent != NULL ? array_index_of(g_project->_->layers, l->parent) : -1,
		                              .visible            = l->visible,
		                              .texpaint_nor       = l->texpaint_nor != NULL && !container ? export_arm_encode_layer_texture(l->texpaint_nor) : NULL,
		                              .texpaint_pack      = l->texpaint_pack != NULL && !container ? export_arm_encode_layer_texture(l->texpaint_pack) : NULL,
		                              .texpaint_sculpt    = NULL, // Sculpting was removed, the field stays for file compatibility
		                              .paint_base         = l->paint_base,
		                              .paint_opac         = l->paint_opac,
		                              .paint_occ          = l->paint_occ,
		                              .paint_rough        = l->paint_rough,
		                              .paint_met          = l->paint_met,
		                              .paint_nor          = l->paint_nor,
		                              .paint_nor_blend    = l->paint_nor_blend,
		                              .paint_height       = l->paint_height,
		                              .paint_height_blend = l->paint_height_blend,
		                              .paint_emis         = l->paint_emis,
		                              .paint_subs         = l->paint_subs,
		                              .path_points        = l->path_points,
		                              .path_points_world  = l->path_points_world,
		                              .path_points_camera = l->path_points_camera,
		                              .path_points_parent = l->path_points_parent,
		                              .path_tool          = l->path_tool,
		                              .path_curved        = l->path_curved,
		                              .path_text          = l->path_text,
		                              .height_blend           = l->height_blend,
		                              .height_blend_below     = l->height_blend_below,
		                              .height_blend_influence = l->height_blend_influence,
		                              .height_blend_sharpness = l->height_blend_sharpness,
		                              .height_blend_offset    = l->height_blend_offset,
		                              .height_blend_reach     = l->height_blend_reach,
		                              .mask_effects           = mask_stack_anchors_to_index(l->mask_effects, g_project->_->layers->buffer, g_project->_->layers->length),
		                              .path_material      = l->path_material != NULL ? array_index_of(g_project->_->materials, l->path_material) : -1,
		                              .uid                = l->uid,
		                              .res_x              = l->texpaint != NULL ? l->texpaint->width : config_get_texture_res_x(),
		                              .res_y              = l->texpaint != NULL ? l->texpaint->height : config_get_texture_res_y(),
		                              .rasters            = container ? project_container_layer_rasters(l) : NULL});
		any_array_push(ld, d);
	}

	packed_asset_t_array_t *packed_assets = (g_project->packed_assets == NULL || g_project->packed_assets->length == 0) ? NULL : g_project->packed_assets;
#ifdef IRON_IOS
	bool same_drive = false;
#else
	bool same_drive = g_project->envmap != NULL ? char_at(relative_to, 0)[0] == char_at(g_project->envmap, 0)[0] : true;
#endif

	g_project->version                = string_copy(export_arm_version());
	g_project->uuid                   = project_uuid_get();
	g_project->next_uid               = project_uid_next;
	g_project->material_groups        = mgroups;
	g_project->material_datas         = mdata2;
	g_project->assets                 = texture_files;
	g_project->packed_assets          = packed_assets;
	g_project->swatches               = g_project->swatches;
	g_project->envmap                 = g_project->envmap != NULL ? (same_drive ? path_to_relative(relative_to, g_project->envmap) : g_project->envmap) : NULL;
	g_project->envmap_strength        = scene_world->strength;
	g_project->envmap_angle           = g_context->envmap_angle;
	g_project->envmap_blur            = g_context->show_envmap_blur;
	g_project->camera_world           = mat4_to_f32_array(scene_camera->base->transform->local);
	g_project->camera_origin          = vec3_to_f32_array(camera_origins[0]);
	g_project->camera_fov             = scene_camera->data->fov;
	g_project->camera_near            = scene_camera->data->near_plane;
	g_project->camera_far             = scene_camera->data->far_plane;
	g_project->camera_frustum_culling = scene_camera->data->frustum_culling;

	// g_project.mesh_datas = md; // TODO: fix GC ref
	if (g_project->mesh_datas == NULL) {
		g_project->mesh_datas = md;
	}
	else {
		g_project->mesh_datas->length = 0;
		for (i32 i = 0; i < md->length; ++i) {
			any_array_push(g_project->mesh_datas, md->buffer[i]);
		}
	}

	f32_array_t_array_t *mesh_transforms = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p  = g_project->_->paint_objects->buffer[i];
		f32_array_t   *ar = f32_array_create_from_raw(p->base->transform->local.m, 16);
		any_array_push(mesh_transforms, ar);
	}
	g_project->mesh_transforms = mesh_transforms;

	if (g_project->mesh_parents != NULL) {
		g_project->mesh_parents = i32_array_create(g_project->_->paint_objects->length);
		for (i32 i = 0; i < g_project->mesh_parents->length; ++i) {
			object_t *parent                   = g_project->_->paint_objects->buffer[i]->base->parent;
			g_project->mesh_parents->buffer[i] = -1; // No parent
			if (parent != NULL && parent != _scene_root) {
				for (i32 j = 0; j < g_project->mesh_parents->length; ++j) {
					if (g_project->_->paint_objects->buffer[j]->base == parent) {
						g_project->mesh_parents->buffer[i] = j;
						break;
					}
				}
			}
		}
	}

	// Material previews, so that opening the project does not render every material again (its buffers too). Empty for a
	// material whose preview is not rendered yet. Stored as the material export stores its icon.
	buffer_t_array_t *micons = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		buffer_t        *icon;
		if (m->preview_ready && m->image != NULL) {
			icon = export_arm_encode_icon(m->image);
			gpu_release_texture_pixels(m->image);
		}
		else {
			icon = buffer_create(0);
		}
		any_array_push(micons, icon);
	}

	g_project->material_nodes = mnodes;
	g_project->material_icons = micons;
	g_project->brush_nodes    = bnodes;
	g_project->layer_datas    = ld;
	g_project->font_assets    = font_files;
	g_project->mesh_assets    = mesh_files;

#ifdef IRON_BGRA
	g_project->is_bgra = true;
#else
	g_project->is_bgra = false;
#endif

	bool pack_assets = g_context->pack_assets_on_save;
	export_arm_progress(0.7);
	if (pack_assets) {
		export_arm_pack_assets(g_project, g_project->_->assets);
	}

	export_arm_progress(0.8);
	if (container) {
		project_container_pack_assets(g_project);
	}
	buffer_t *buffer = util_encode_project(g_project);
	if (container) {
		project_container_unpack_assets(g_project);
	}

	for (i32 i = 0; i < ld->length; ++i) {
		layer_data_t *d = ld->buffer[i];
		export_arm_free_buffer(d->texpaint);
		export_arm_free_buffer(d->texpaint_nor);
		export_arm_free_buffer(d->texpaint_pack);
		export_arm_free_buffer(d->texpaint_sculpt);
		export_arm_free_buffer(d->rasters);
		if (d->decal_mat != NULL) {
			array_free(d->decal_mat);
			free(d->decal_mat);
		}
		free(d);
	}
	array_free(ld);
	free(ld);
	g_project->layer_datas = NULL;
	for (i32 i = 0; i < micons->length; ++i) {
		export_arm_free_buffer(micons->buffer[i]);
	}
	array_free(micons);
	free(micons);
	g_project->material_icons = NULL;
	return buffer;
}

// A project file older builds open (version 30): no uids, layers saved square (their width)
void export_arm_run_project_v30(char *path) {
	export_arm_v30 = true;
	export_arm_run_project(path);
	export_arm_v30 = false;
}

void export_arm_run_project(char *path) {
	export_arm_show_progress = true;
	bool saved;
	if (string_equals(export_arm_version(), "31")) {
		saved = project_container_save(path); // Appends to the file when it can
	}
	else {
		buffer_t *buffer = export_arm_encode_project(path);
		export_arm_progress(0.9);
		saved = iron_file_save_bytes(path, buffer, 0);
		array_free(buffer);
		free(buffer);
	}
	if (!saved) {
		console_error(string("Could not save %s.", path)); // Still modified, saved again later
	}
	else if (string_equals(path, g_project->_->filepath)) {
		project_modified = false;
		hstore_journal_mark_saved(); // The history journal before here needs no recovery
	}
	export_arm_progress(1.0);
	export_arm_show_progress = false;
	base_redraw_ui();

#ifdef IRON_IOS
	{
		gpu_texture_t   *tex       = render_path_target_image("buf");
		gpu_texture_t   *mesh_icon = gpu_create_render_target(256, 256, GPU_TEXTURE_FORMAT_RGBA32);
		// The new target holds whatever was in memory (magenta on the iPad): clear it to the grey of the view,
		// it shows where the view is transparent
		f32 r  = base_window_aspect();
		f32 dw = r >= 1.0 ? 256 * r : 256;
		f32 dh = r >= 1.0 ? 256 : 256 / r;
		draw_begin(mesh_icon, true, 0xff1a1a1a); // Linear, the gamma below makes it 90 grey
		draw_scaled_image(tex, (256 - dw) / 2.0, (256 - dh) / 2.0, dw, dh); // Cover the square, also in portrait
		draw_end();

		buffer_t   *mesh_icon_pixels = gpu_get_texture_pixels(mesh_icon);
		u8_array_t *u8a              = mesh_icon_pixels;
		for (i32 i = 0; i < 256 * 256 * 4; ++i) {
			u8a->buffer[i] = math_floor(math_pow(u8a->buffer[i] / 255.0, 1.0 / 2.2) * 255);
		}
		iron_write_png(string("%s_icon.png", substring(path, 0, string_length(path) - 4)), mesh_icon_pixels, 256, 256, 0);
		gpu_delete_texture(mesh_icon);
	}
#endif

	if (!string_equals(path, g_project->_->filepath)) {
		return;
	}

	// Save to recent
#ifdef IRON_IOS
	char *recent_path = substring(path, string_last_index_of(path, "/") + 1, string_length(path));
#else
	char *recent_path = path;
#endif

	string_array_t *recent = g_config->recent_projects;
	string_array_remove(recent, recent_path);
	array_insert(recent, 0, recent_path);
	config_save();

	console_info(tr("Project saved"));
}

packed_asset_t_array_t *export_arm_get_packed_assets(char *project_path, string_array_t *texture_files) {
	packed_asset_t_array_t *packed_assets = NULL;
	if (g_project->packed_assets != NULL) {
		for (i32 i = 0; i < g_project->packed_assets->length; ++i) {
			packed_asset_t *pa = g_project->packed_assets->buffer[i];
#ifdef IRON_IOS
			bool same_drive = false;
#else
			bool same_drive = char_at(project_path, 0)[0] == char_at(pa->name, 0)[0];
#endif
			// Convert path from absolute to relative
			pa->name = same_drive ? path_to_relative(project_path, pa->name) : pa->name;
			for (i32 i = 0; i < texture_files->length; ++i) {
				char *tf = texture_files->buffer[i];
				if (string_equals(pa->name, tf)) {
					if (packed_assets == NULL) {
						packed_assets = any_array_create_from_raw((void *[]){}, 0);
					}
					any_array_push(packed_assets, pa);
					break;
				}
			}
		}
	}
	return packed_assets;
}

void export_arm_run_material(char *path) {
	if (!ends_with(path, ".arm")) {
		path = string("%s.arm", path);
	}
	ui_node_canvas_t_array_t *mnodes  = any_array_create_from_raw((void *[]){}, 0);
	ui_node_canvas_t_array_t *mgroups = NULL;
	slot_material_t          *m       = g_context->material;
	ui_node_canvas_t         *c       = util_clone_canvas(m->canvas);
	asset_t_array_t          *assets  = any_array_create_from_raw((void *[]){}, 0);
	if (ui_nodes_has_group(c)) {
		mgroups = any_array_create_from_raw((void *[]){}, 0);
		ui_nodes_traverse_group(mgroups, c);
		for (i32 i = 0; i < mgroups->length; ++i) {
			ui_node_canvas_t *gc = mgroups->buffer[i];
			for (i32 i = 0; i < gc->nodes->length; ++i) {
				ui_node_t *n = gc->nodes->buffer[i];
				export_arm_export_node(n, assets);
			}
		}
	}
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		export_arm_export_node(n, assets);
	}
	any_array_push(mnodes, c);

	string_array_t         *texture_files = export_arm_assets_to_files(path, assets);
	packed_asset_t_array_t *packed_assets = NULL;
	if (!g_context->pack_assets_on_export) {
		packed_assets = export_arm_get_packed_assets(path, texture_files);
	}

	buffer_t *buf = export_arm_encode_icon(m->image);
	buffer_t_array_t *micons = any_array_create_from_raw(
	    (void *[]){
	        buf,
	    },
	    1);

	material_data2_t_array_t *mdata2 = any_array_create_from_raw((void *[]){}, 0);
	material_data2_t         *md2    = ALLOC_INIT(material_data2_t, {
	                                                                    .paint_base   = m->paint_base,
	                                                                    .paint_opac   = m->paint_opac,
	                                                                    .paint_occ    = m->paint_occ,
	                                                                    .paint_rough  = m->paint_rough,
	                                                                    .paint_met    = m->paint_met,
	                                                                    .paint_nor    = m->paint_nor,
	                                                                    .paint_height = m->paint_height,
	                                                                    .paint_emis   = m->paint_emis,
	                                                                    .paint_subs   = m->paint_subs,
	                                                                    .uv_scale     = m->uv_scale,
	                                                                    .uv_angle     = m->uv_angle,
                                                         });
	any_array_push(mdata2, md2);

	project_t *raw = ALLOC_INIT(project_t, {.version         = export_arm_version(),
	                                        .material_nodes  = mnodes,
	                                        .material_groups = mgroups,
	                                        .material_icons  = micons,
	                                        .material_datas  = mdata2,
	                                        .assets          = texture_files,
	                                        .packed_assets   = packed_assets});

	if (g_context->write_icon_on_export) { // Separate icon files
		buffer_t *buf = buffer_half_to_u8(gpu_get_texture_pixels(m->image));
#ifdef IRON_BGRA
		buf = buffer_bgra_swap(buf);
#endif
		iron_write_png(string("%s_icon.png", substring(path, 0, string_length(path) - 4)), buf, m->image->width, m->image->height, 0);
		iron_write_jpg(string("%s_icon.jpg", substring(path, 0, string_length(path) - 4)), buf, m->image->width, m->image->height, 0, 50);
	}

	if (g_context->pack_assets_on_export) { // Pack textures
		export_arm_pack_assets(raw, assets);
	}

	buffer_t *buffer = util_encode_project(raw);
	if (!iron_file_save_bytes(path, buffer, 0)) {
		console_error(string("Could not save %s.", path));
	}
	array_free(buffer);
	free(buffer);
}

// Material file saved next to its textures (material library): textures are referenced by file name,
// nothing is packed, the preview is embedded
buffer_t *export_arm_encode_library_material(slot_material_t *m) {
	ui_node_canvas_t_array_t *mnodes = any_array_create_from_raw((void *[]){}, 0);
	ui_node_canvas_t         *c      = util_clone_canvas(m->canvas);
	asset_t_array_t          *assets = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < c->nodes->length; ++i) {
		export_arm_export_node(c->nodes->buffer[i], assets);
	}
	any_array_push(mnodes, c);

	string_array_t *texture_files = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < assets->length; ++i) {
		asset_t *a = assets->buffer[i];
		any_array_push(texture_files, string_copy(a->name)); // File name with extension
	}

	buffer_t *icon = export_arm_encode_icon(m->image);
	material_data2_t_array_t *mdata2 = any_array_create_from_raw((void *[]){}, 0);
	material_data2_t         *md2    = ALLOC_INIT(material_data2_t, {
	                                                                    .paint_base   = m->paint_base,
	                                                                    .paint_opac   = m->paint_opac,
	                                                                    .paint_occ    = m->paint_occ,
	                                                                    .paint_rough  = m->paint_rough,
	                                                                    .paint_met    = m->paint_met,
	                                                                    .paint_nor    = m->paint_nor,
	                                                                    .paint_height = m->paint_height,
	                                                                    .paint_emis   = m->paint_emis,
	                                                                    .paint_subs   = m->paint_subs,
	                                                                    .uv_scale     = m->uv_scale,
	                                                                    .uv_angle     = m->uv_angle,
                                                         });
	any_array_push(mdata2, md2);

	project_t *raw = ALLOC_INIT(project_t, {.version        = export_arm_version(),
	                                        .material_nodes = mnodes,
	                                        .material_icons = any_array_create_from_raw((void *[]){icon}, 1),
	                                        .material_datas = mdata2,
	                                        .assets         = texture_files});
	return util_encode_project(raw);
}

static buffer_t *export_arm_asset_bytes(char *file) {
	if (g_project->packed_assets != NULL) {
		for (i32 i = 0; i < g_project->packed_assets->length; ++i) {
			packed_asset_t *pa = g_project->packed_assets->buffer[i];
			if (string_equals(pa->name, file)) {
				return u8_array_create_from_array(pa->bytes);
			}
		}
	}
	return iron_load_blob(file);
}

void export_arm_pack_assets(project_t *raw, asset_t_array_t *assets) {
	if (raw->packed_assets == NULL) {
		raw->packed_assets = any_array_create_from_raw((void *[]){}, 0);
	}
	gpu_texture_t_array_t *temp_images = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < assets->length; ++i) {
		char *file = assets->buffer[i]->file;
		if (project_packed_asset_exists(raw->packed_assets, file)) {
			continue;
		}
		char     *ext = path_ext(file);
		buffer_t *bytes;
		if (any_map_get(import_texture_importers, ext) != NULL) { // Script importer format, store as png
			gpu_texture_t *image = project_get_image(assets->buffer[i]);
			gpu_texture_t *temp  = gpu_create_render_target(image->width, image->height, GPU_TEXTURE_FORMAT_RGBA32);
			draw_begin(temp, false, 0);
			draw_image(image, 0, 0);
			draw_end();
			any_array_push(temp_images, temp);
			bytes = iron_encode_png(gpu_get_texture_pixels(temp), temp->width, temp->height, 0);
		}
		else {
			bytes = export_arm_asset_bytes(file);
			if (bytes == NULL) {
				console_error(string("%s %s", strings_could_not_locate_texture(), file));
				continue;
			}
		}
		packed_asset_t *pa = ALLOC_INIT(packed_asset_t, {.name = file, .bytes = bytes});
		any_array_push(raw->packed_assets, pa);
	}

	for (i32 i = 0; i < temp_images->length; ++i) {
		gpu_texture_t *image = temp_images->buffer[i];
		gpu_delete_texture(image);
	}
}

void export_arm_run_swatches(char *path) {
	if (!ends_with(path, ".arm")) {
		path = string("%s.arm", path);
	}
	project_t *raw    = ALLOC_INIT(project_t, {.version = export_arm_version(), .swatches = g_project->swatches});
	buffer_t  *buffer = util_encode_project(raw);
	if (!iron_file_save_bytes(path, buffer, 0)) {
		console_error(string("Could not save %s.", path));
	}
	array_free(buffer);
	free(buffer);
}
