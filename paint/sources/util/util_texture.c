
#include "../global.h"

// A packed asset name no packed asset of the project has yet: /packed/<basename><number>.png, the lowest number free
char *util_texture_packed_name(char *basename) {
	if (g_project->packed_assets == NULL) {
		g_project->packed_assets = any_array_create_from_raw((void *[]){}, 0);
	}
	for (i32 num = 0;; ++num) {
		char *abs   = string_tmp("/packed/%s%d.png", basename, num);
		bool  taken = false;
		for (i32 i = 0; i < g_project->packed_assets->length && !taken; ++i) {
			packed_asset_t *pa = g_project->packed_assets->buffer[i];
			taken              = string_equals(pa->name, abs);
		}
		if (!taken) {
			return string_copy(abs);
		}
	}
}

// dst becomes src through pipe (a copy pipeline of dst's format, or a filter such as invert)
void util_copy_texture(gpu_texture_t *dst, gpu_texture_t *src, gpu_pipeline_t *pipe) {
	draw_begin(dst, false, 0);
	draw_set_pipeline(pipe);
	draw_image(src, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();
}

void util_texture_capture_output(gpu_texture_t *img, char *basename) {
	if (img == NULL) {
		return;
	}

	char *abs = util_texture_packed_name(basename);

	buffer_t *buf = gpu_get_texture_pixels_rgba8(img);
#ifdef IRON_BGRA
	if (img->format == GPU_TEXTURE_FORMAT_RGBA32) {
		buffer_bgra_swap(buf); // iron_encode_png takes the order render targets read back in
	}
#endif

	u8_array_t     *bytes = iron_encode_png(buf, img->width, img->height, 0);
	packed_asset_t *pa    = ALLOC_INIT(packed_asset_t, {.name = abs, .bytes = bytes});
	any_array_push(g_project->packed_assets, pa);
	gpu_texture_t *copy = gpu_create_texture_from_encoded_bytes(bytes, ".png");
	any_map_set(data_cached_textures, abs, copy);
	import_texture_run(abs, true);
}

// Render targets sized on demand (tile and scratch targets), counted in the bytes below

static i64 util_texture_bytes      = 0;
static i64 util_texture_bytes_peak = 0;

static i64 util_texture_target_bytes(gpu_texture_t *t) {
	return (i64)t->width * t->height * gpu_texture_format_size(t->format);
}

// *t becomes a w x h render target of format, kept when it is one already. True when (re)allocated (its pixels are undefined).
bool util_texture_ensure_target(gpu_texture_t **t, i32 w, i32 h, gpu_texture_format_t format) {
	if (*t != NULL && (*t)->width == w && (*t)->height == h && (*t)->format == format) {
		return false;
	}
	util_texture_release_target(t);
	*t = gpu_create_render_target(w, h, format);
	util_texture_bytes += util_texture_target_bytes(*t);
	if (util_texture_bytes > util_texture_bytes_peak) {
		util_texture_bytes_peak = util_texture_bytes;
	}
	return true;
}

// Frees a target of util_texture_ensure_target, *t becomes NULL
void util_texture_release_target(gpu_texture_t **t) {
	if (*t == NULL) {
		return;
	}
	util_texture_bytes -= util_texture_target_bytes(*t);
	gpu_delete_texture(*t);
	*t = NULL;
}

// Bytes of the targets util_texture_ensure_target holds now
i64 util_texture_tracked_bytes() {
	return util_texture_bytes;
}

// The most they held since the last reset
i64 util_texture_tracked_peak() {
	return util_texture_bytes_peak;
}

void util_texture_tracked_peak_reset() {
	util_texture_bytes_peak = util_texture_bytes;
}

// Registered mesh maps of the asset, or "" if none
char *util_texture_mesh_maps_of(asset_t *asset) {
	char *s = "";
	for (i32 i = 0; i < MESH_MAP_COUNT; ++i) {
		if (mesh_maps_get_asset(i) == asset) {
			s = string("%s%s%s", s, s[0] != '\0' ? ", " : "", tr(mesh_map_name(i)));
		}
	}
	return s;
}

void util_texture_update_pointers(ui_node_t_array_t *nodes, i32 index) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, "TEX_IMAGE")) {
			if (n->buttons->buffer[0]->default_value->buffer[0] == index) {
				n->buttons->buffer[0]->default_value->buffer[0] = 9999; // Texture deleted, use pink now
			}
			else if (n->buttons->buffer[0]->default_value->buffer[0] > index) {
				n->buttons->buffer[0]->default_value->buffer[0]--; // Offset by deleted texture
			}
		}
	}
}

void util_texture_deleted_on_next_frame(void *_) {
	make_material_parse_paint_material(true);
	util_render_make_material_preview();
}

void util_texture_delete(asset_t *asset) {
	i32 index = array_index_of(g_project->_->assets, asset);
	if (g_project->_->assets->length > 1) {
		g_context->texture = g_project->_->assets->buffer[index == g_project->_->assets->length - 1 ? index - 1 : index + 1];
	}
	project_id_map_removed(asset);
	mesh_maps_asset_removed(asset);
	mask_stack_asset_removed(index);

	if (data_get_texture(asset->file) == scene_world->_->envmap) {
		project_set_default_envmap();
	}

	if (g_project->packed_assets != NULL) {
		for (i32 i = 0; i < g_project->packed_assets->length; ++i) {
			packed_asset_t *pa = g_project->packed_assets->buffer[i];
			if (string_equals(pa->name, asset->file)) {
				array_splice(g_project->packed_assets, i, 1);
				break;
			}
		}
	}

	data_delete_texture(asset->file);
	array_splice(g_project->_->assets, index, 1);
	sys_notify_on_next_frame(&util_texture_deleted_on_next_frame, NULL);

	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		util_texture_update_pointers(m->canvas->nodes, index);
	}

	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *g = g_project->_->material_groups->buffer[i];
		util_texture_update_pointers(g->canvas->nodes, index);
	}

	for (i32 i = 0; i < g_project->_->brushes->length; ++i) {
		slot_brush_t *b = g_project->_->brushes->buffer[i];
		util_texture_update_pointers(b->canvas->nodes, index);
	}
}

bool util_texture_nodes_use(ui_node_t_array_t *nodes, i32 index) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, "TEX_IMAGE") && n->buttons->buffer[0]->default_value->buffer[0] == index) {
			return true;
		}
	}
	return false;
}

bool util_texture_is_used(i32 index) {
	asset_t *asset = g_project->_->assets->buffer[index];
	if (g_project->id_map != NULL && string_equals(g_project->id_map, asset->name)) {
		return true;
	}
	if (!string_equals(util_texture_mesh_maps_of(asset), "")) {
		return true;
	}
	if (g_project->envmap != NULL && string_equals(g_project->envmap, asset->file)) {
		return true;
	}
	if (mask_stack_uses_asset(index)) {
		return true;
	}

	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		if (util_texture_nodes_use(m->canvas->nodes, index)) {
			return true;
		}
	}

	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *g = g_project->_->material_groups->buffer[i];
		if (util_texture_nodes_use(g->canvas->nodes, index)) {
			return true;
		}
	}

	for (i32 i = 0; i < g_project->_->brushes->length; ++i) {
		slot_brush_t *b = g_project->_->brushes->buffer[i];
		if (util_texture_nodes_use(b->canvas->nodes, index)) {
			return true;
		}
	}

	return false;
}

void util_texture_delete_unused() {
	asset_t_array_t *unused = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		if (!util_texture_is_used(i)) {
			any_array_push(unused, g_project->_->assets->buffer[i]);
		}
	}

	for (i32 i = 0; i < unused->length; ++i) {
		util_texture_delete(unused->buffer[i]);
	}

	array_delete(unused);
}
