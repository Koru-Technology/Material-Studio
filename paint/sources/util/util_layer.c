#include "../global.h"

#ifdef IRON_IOS
i32 layers_max_layers = 18;
#else
i32 layers_max_layers = 255;
#endif

uv_type_t _layers_uv_type;
mat4_t    _layers_decal_mat;
i32       _layers_position;
i32       _layers_base_color;
f32       _layers_occlusion;
f32       _layers_roughness;
f32       _layers_metallic;

// dst becomes src through pipe (a copy pipeline of dst's format, or a filter such as invert)
void util_copy_texture(gpu_texture_t *dst, gpu_texture_t *src, gpu_pipeline_t *pipe) {
	draw_begin(dst, false, 0);
	draw_set_pipeline(pipe);
	draw_image(src, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();
}

static void util_layer_draw_preview(slot_layer_t *l, gpu_texture_t *target) {
	paint_tex_t type = tab_layers_preview_tex;
	if (slot_layer_is_mask(l) || (type == PAINT_TEX_NORMAL && l->texpaint_nor == NULL) || (type >= PAINT_TEX_OCCLUSION && l->texpaint_pack == NULL)) {
		type = PAINT_TEX_BASE;
	}

	if (type == PAINT_TEX_BASE) {
		draw_begin(target, true, 0x00000000);
		draw_set_pipeline(pipes_copy); // texpaint_preview is always RGBA32 for now
		draw_scaled_image(slot_layer_is_mask(l) ? slot_layer_mask_texture(l) : l->texpaint, 0, 0, target->width, target->height);
		draw_set_pipeline(NULL);
		draw_end();
		return;
	}

	gpu_texture_t *source  = type == PAINT_TEX_OPACITY ? l->texpaint : type == PAINT_TEX_NORMAL ? l->texpaint_nor : l->texpaint_pack;
	i32            channel = type == PAINT_TEX_OCCLUSION   ? 1
	                         : type == PAINT_TEX_ROUGHNESS ? 2
	                         : type == PAINT_TEX_METALLIC  ? 3
	                         : type == PAINT_TEX_NORMAL    ? 5
	                                                       : 4; // Opacity, height
	draw_begin(target, true, 0xff000000);                    // view2d pipe keeps dst alpha
	draw_set_pipeline(ui_view2d_pipe);
	gpu_set_int(ui_view2d_channel_loc, channel);
	draw_scaled_image(source, 0, 0, target->width, target->height);
	draw_set_pipeline(NULL);
	draw_end();
}

void util_layer_update_preview() {
	if (layers_sync_nodes()) { // Nodes follow the layers they read
		layers_update_linked_layers();
		make_material_parse_mesh_material();
	}
	if (g_context->layers_preview_dirty || (g_context->layer != NULL && g_context->layer_preview_dirty)) {
		app_notify_previews();
	}
	if (g_context->layers_preview_dirty) {
		g_context->layers_preview_dirty = false;
		g_context->layer_preview_dirty  = false;
		g_context->mask_preview_last    = NULL;
		mask_stack_layer_changed(NULL); // Anchors
		// Update all layer previews
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (slot_layer_is_group(l)) {
				continue;
			}

			gpu_texture_t *target = l->texpaint_preview;
			if (target == NULL) {
				continue;
			}

			util_layer_draw_preview(l, target);
		}
		ui_dock_redraw(TAB_AREA_SIDEBAR0);
	}

	if (g_context->layer != NULL && g_context->layer_preview_dirty && !slot_layer_is_group(g_context->layer)) {
		g_context->layer_preview_dirty = false;
		g_context->mask_preview_last   = NULL;
		// Update layer preview
		slot_layer_t  *l      = g_context->layer;
		mask_stack_layer_changed(l); // Anchors
		gpu_texture_t *target = l->texpaint_preview;
		if (target != NULL) {
			util_layer_draw_preview(l, target);
			ui_dock_redraw(TAB_AREA_SIDEBAR0);
		}
	}
}

void layers_init() {
	slot_layer_clear(g_project->_->layers->buffer[0], color_from_floats(layers_default_base, layers_default_base, layers_default_base, 1.0), NULL, 1.0,
	                 layers_default_rough, 0.0);
}

void layers_resize() {
	if (config_get_texture_res_x() >= 8192 || config_get_texture_res_y() >= 8192) { // Save memory for >=8k
		g_config->undo_steps = config_get_texture_res_x() == 8192 ? 4 : 1;
		while (history_undo_layers->length > g_config->undo_steps) {
			slot_layer_t *l = array_pop(history_undo_layers);
			sys_notify_on_next_frame(&slot_layer_unload, l);
		}
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
	layers_resize_targets();

	render_path_raytrace_ready = false; // Rebuild baketex
	g_context->ddirty          = 2;
	layers_crop                = false;
}

// Undo layers, live layer and paint targets that follow the texture resolution
void layers_resize_targets() {
	stroke_refine_reset(); // Records of the old size go
	material_cache_reset(); // And material caches
	if (history_undo_layers != NULL) {
		for (i32 i = 0; i < history_undo_layers->length; ++i) {
			slot_layer_t *l = history_undo_layers->buffer[i];
			slot_layer_resize_and_set_bits(l);
		}
	}

	slot_layer_t_array_t *saved = history_merge_saved_layers();
	for (i32 i = 0; i < saved->length; ++i) {
		slot_layer_resize_and_set_bits(saved->buffer[i]);
	}

	any_map_t *rts = render_path_render_targets;

	render_target_t *blend0           = any_map_get(rts, "texpaint_blend0");
	gpu_texture_t   *_texpaint_blend0 = blend0->_image;
	gpu_delete_texture(_texpaint_blend0);
	blend0->width  = config_get_texture_res_x();
	blend0->height = config_get_texture_res_y();
	blend0->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R16);

	render_target_t *blend1           = any_map_get(rts, "texpaint_blend1");
	gpu_texture_t   *_texpaint_blend1 = blend1->_image;
	gpu_delete_texture(_texpaint_blend1);
	blend1->width  = config_get_texture_res_x();
	blend1->height = config_get_texture_res_y();
	blend1->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R16);

	g_context->brush_blend_dirty = true;

	render_target_t *blur = any_map_get(rts, "texpaint_blur");
	if (blur != NULL) {
		gpu_texture_t *_texpaint_blur = blur->_image;
		gpu_delete_texture(_texpaint_blur);
		f32 size_x   = math_floor(config_get_texture_res_x() * 0.95);
		f32 size_y   = math_floor(config_get_texture_res_y() * 0.95);
		blur->width  = size_x;
		blur->height = size_y;
		blur->_image = gpu_create_render_target(size_x, size_y, GPU_TEXTURE_FORMAT_RGBA32);
	}
	if (render_path_paint_live_layer != NULL) {
		slot_layer_resize_and_set_bits(render_path_paint_live_layer);
	}
}

void layers_set_bits() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
	for (i32 i = 0; i < history_undo_layers->length; ++i) {
		slot_layer_t *l = history_undo_layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
	slot_layer_t_array_t *saved = history_merge_saved_layers();
	for (i32 i = 0; i < saved->length; ++i) {
		slot_layer_resize_and_set_bits(saved->buffer[i]);
	}
}

void layers_make_temp_img() {
	slot_layer_t *l = g_project->_->layers->buffer[0];

	if (layers_temp_image != NULL && (layers_temp_image->width != l->texpaint->width || layers_temp_image->height != l->texpaint->height ||
	                                  layers_temp_image->format != l->texpaint->format)) {
		render_target_t *_temptex0 = any_map_get(render_path_render_targets, "temptex0");
		gpu_delete_texture(_temptex0->_image);
		map_delete(render_path_render_targets, "temptex0");
		layers_temp_image = NULL;
	}

	if (layers_temp_image == NULL) {
		char            *format = layers_format_name(base_bits);
		render_target_t *t      = render_target_create();
		t->name                 = "temptex0";
		t->width                = l->texpaint->width;
		t->height               = l->texpaint->height;
		t->format               = string_copy(format);
		render_target_t *rt     = render_path_create_render_target(t);
		layers_temp_image       = rt->_image;
	}
}

void layers_make_temp_mask_img() {
	if (pipes_temp_mask_image != NULL &&
	    (pipes_temp_mask_image->width != config_get_texture_res_x() || pipes_temp_mask_image->height != config_get_texture_res_y())) {
		gpu_texture_t *_temp_mask_image = pipes_temp_mask_image;
		gpu_delete_texture(_temp_mask_image);
		pipes_temp_mask_image = NULL;
	}

	if (pipes_temp_mask_image == NULL) {
		pipes_temp_mask_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_RGBA32);
	}
}

void layers_make_export_img() {
	slot_layer_t *l = g_project->_->layers->buffer[0];
	if (layers_expa != NULL &&
	    (layers_expa->width != l->texpaint->width || layers_expa->height != l->texpaint->height || layers_expa->format != l->texpaint->format)) {
		gpu_texture_t *_expa = layers_expa;
		gpu_texture_t *_expb = layers_expb;
		gpu_texture_t *_expc = layers_expc;
		gpu_delete_texture(_expa);
		gpu_delete_texture(_expb);
		gpu_delete_texture(_expc);
		layers_expa = NULL;
		layers_expb = NULL;
		layers_expc = NULL;
		map_delete(render_path_render_targets, "expa");
		map_delete(render_path_render_targets, "expb");
		map_delete(render_path_render_targets, "expc");
	}
	if (layers_expa == NULL) {
		char *format = layers_format_name(base_bits);
		{
			render_target_t *t  = render_target_create();
			t->name             = "expa";
			t->width            = l->texpaint->width;
			t->height           = l->texpaint->height;
			t->format           = string_copy(format);
			render_target_t *rt = render_path_create_render_target(t);
			layers_expa         = rt->_image;
		}
		{
			render_target_t *t  = render_target_create();
			t->name             = "expb";
			t->width            = l->texpaint->width;
			t->height           = l->texpaint->height;
			t->format           = string_copy(format);
			render_target_t *rt = render_path_create_render_target(t);
			layers_expb         = rt->_image;
		}
		{
			render_target_t *t  = render_target_create();
			t->name             = "expc";
			t->width            = l->texpaint->width;
			t->height           = l->texpaint->height;
			t->format           = string_copy(format);
			render_target_t *rt = render_path_create_render_target(t);
			layers_expc         = rt->_image;
		}
	}
}

void layers_commands_merge_pack(gpu_pipeline_t *pipe, gpu_texture_t *i0, gpu_texture_t *i1, gpu_texture_t *i1pack, f32 i1mask_opacity, gpu_texture_t *i1texmask,
                                i32 i1blending) {
	_gpu_begin(i0, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipe);
	gpu_set_texture(pipes_tex0, i1);
	gpu_set_texture(pipes_tex1, i1pack);
	gpu_set_texture(pipes_texmask, i1texmask);
	gpu_set_texture(pipes_texa, layers_temp_image);
	gpu_set_float(pipes_opac, i1mask_opacity);
	gpu_set_float(pipes_tex1w, i1pack->width);
	gpu_set_int(pipes_blending, i1blending);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
}

static void layers_enable_translucency_on_next_frame(void *_) {
	make_material_parse_mesh_material();
	make_material_parse_paint_material(false);
	util_render_make_material_preview();
}

// The bottom layer alpha is the surface opacity - once something can lower it, meshes turn see-through and stay so
void layers_enable_translucency() {
	if (g_project->_->translucency) {
		return;
	}
	g_project->_->translucency = true;
	sys_notify_on_next_frame(&layers_enable_translucency_on_next_frame, NULL);
}

void layers_set_object_mask() {
	string_array_t *ar = any_array_create_from_raw(
	    (void *[]){
	        tr("None"),
	    },
	    1);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		any_array_push(ar, p->base->name);
	}

	i32 mask = context_object_mask_used() ? slot_layer_get_object_mask(g_context->layer) : 0;
	if (context_layer_filter_used()) {
		mask = g_context->layer_filter;
	}
	if (mask > 0) {
		if (g_context->merged_object != NULL) {
			g_context->merged_object->base->visible = false;
		}
		mesh_object_t *o = g_project->_->paint_objects->buffer[0];
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *p         = g_project->_->paint_objects->buffer[i];
			char          *mask_name = ar->buffer[mask];
			if (string_equals(p->base->name, mask_name)) {
				o = p;
				break;
			}
		}
		context_select_paint_object(o);
	}
	else {
		i32 atlas_mask = g_context->layer_filter > g_project->_->paint_objects->length ? g_context->layer_filter : slot_layer_get_object_mask(g_context->layer);
		bool is_atlas  = atlas_mask > g_project->_->paint_objects->length;
		if (g_context->merged_object == NULL || is_atlas || g_context->merged_object_is_atlas) {
			mesh_object_t_array_t *visibles = is_atlas ? project_get_atlas_objects(atlas_mask) : NULL;
			util_mesh_merge(visibles);
		}
		context_select_paint_object(context_main_object());
		if (g_context->merged_object != NULL) {
			g_context->merged_object->base->visible = true;
		}
	}
	util_uv_dilatemap_cached = false;
}

void tab_layers_apply_filter(i32 filter) {
	g_context->layer_filter = filter;
	char *filter_name       = NULL;
	if (filter > 0 && filter <= g_project->_->paint_objects->length) {
		filter_name = g_project->_->paint_objects->buffer[filter - 1]->base->name;
	}
	else if (filter > g_project->_->paint_objects->length) {
		string_array_t *atlases = project_get_used_atlases();
		if (atlases != NULL) {
			filter_name = atlases->buffer[filter - g_project->_->paint_objects->length - 1];
		}
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		p->base->visible = filter == 0 || (filter_name != NULL && string_equals(p->base->name, filter_name)) || project_is_atlas_object(p);
	}
	if (filter == 0 && g_context->merged_object_is_atlas) {
		util_mesh_merge(NULL);
	}
	else if (filter > g_project->_->paint_objects->length) {
		util_mesh_merge(util_mesh_get_visible());
	}
	layers_set_object_mask();
	util_uv_uvmap_cached       = false;
	g_context->ddirty          = 2;
	render_path_raytrace_ready = false;
}

// Pushes a history step that undoes and redoes together with the one before it (one undo for several steps of one
// action). Nothing is linked when the push added no step.
void layers_history_with_prev(void (*push)(void)) {
	i32 before = history_steps->length;
	push();
	if (history_steps->length > before && history_steps->length > 1) {
		history_steps->buffer[history_steps->length - 1]->with_prev = true;
	}
}

void layers_on_resized_on_next_frame(void *_) {
	layers_resize();
	slot_layer_t    *_layer    = g_context->layer;
	slot_material_t *_material = g_context->material;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill_material != NULL) {
			g_context->layer    = l;
			g_context->material = l->fill_material;
			layers_set_object_mask();
			layers_update_fill_layer(true);
		}
	}
	g_context->layer    = _layer;
	g_context->material = _material;
	layers_set_object_mask();
	make_material_parse_paint_material(true);
}

void layers_on_resized() {
	sys_notify_on_next_frame(&layers_on_resized_on_next_frame, NULL);
	util_uv_uvmap              = NULL;
	util_uv_uvmap_cached       = false;
	util_uv_trianglemap        = NULL;
	util_uv_trianglemap_cached = false;
	util_uv_dilatemap_cached   = false;
	render_path_raytrace_ready = false;
}

// Layer, Layer Mask and Mesh Displace nodes hold a position in the layer stack. The stack as those positions were last
// valid is kept: when layers were added, removed or moved since (by any path, undo included), each node follows its
// layer, 9999 when it is gone.
static slot_layer_t_array_t *layers_nodes_stack    = NULL;
static slot_layer_t_array_t *layers_nodes_stack_of = NULL; // The project's layer array the stack is of

static bool layers_node_points_at_layer(ui_node_t *n) {
	return string_equals(n->type, "LAYER") || string_equals(n->type, "LAYER_MASK") || string_equals(n->type, "MESH_DISPLACE");
}

// True when a node changed
bool layers_sync_nodes() {
	slot_layer_t_array_t *layers = g_project->_->layers;
	slot_layer_t_array_t *old    = layers_nodes_stack;
	bool                  same   = old != NULL && layers_nodes_stack_of == layers && old->length == layers->length;
	for (i32 i = 0; same && i < layers->length; ++i) {
		same = old->buffer[i] == layers->buffer[i];
	}
	if (same) {
		return false;
	}
	bool changed = false;
	if (old != NULL && layers_nodes_stack_of == layers) { // Another project starts with its nodes as they are
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			ui_node_t_array_t *nodes = g_project->_->materials->buffer[i]->canvas->nodes;
			for (i32 j = 0; j < nodes->length; ++j) {
				ui_node_t *n = nodes->buffer[j];
				if (!layers_node_points_at_layer(n)) {
					continue;
				}
				f32_array_t *v = n->buttons->buffer[0]->default_value;
				i32          k = (i32)v->buffer[0];
				if (k < 0 || k >= old->length) {
					continue;
				}
				i32 now = array_index_of(layers, old->buffer[k]);
				if (now == -1) {
					now = 9999;
				}
				if (now != k) {
					v->buffer[0] = (f32)now;
					changed      = true;
				}
			}
		}
	}
	if (old != NULL) {
		array_free(old);
		free(old);
	}
	layers_nodes_stack    = array_slice(layers, 0, layers->length);
	layers_nodes_stack_of = layers;
	return changed;
}

// Before and after the layers change: kept for the call sites, layers_sync_nodes follows any change
slot_layer_t_array_t *tab_layers_init_layer_map() {
	layers_sync_nodes();
	return NULL;
}

void tab_layers_remap_all_layer_pointers(slot_layer_t_array_t *old_layers) {
	layers_sync_nodes();
}

void tab_layers_make_mask_preview_rgba32_on_next_frame(void *_) {
	slot_layer_t *l = tab_layers_l;
	draw_begin(g_context->mask_preview_rgba32, true, 0xff000000);
	draw_set_pipeline(ui_view2d_pipe);
	gpu_set_int(ui_view2d_channel_loc, 1);
	draw_image(l->texpaint_preview, 0, 0);
	draw_end();
	draw_set_pipeline(NULL);
}

void tab_layers_make_mask_preview_rgba32(slot_layer_t *l) {
	if (g_context->mask_preview_rgba32 == NULL) {
		g_context->mask_preview_rgba32 = gpu_create_render_target(util_render_layer_preview_size, util_render_layer_preview_size, GPU_TEXTURE_FORMAT_RGBA32);
	}
	// Convert from R8 to RGBA32 for tooltip display
	if (g_context->mask_preview_last != l) {
		g_context->mask_preview_last = l;
		tab_layers_l                 = l;
		sys_notify_on_next_frame(&tab_layers_make_mask_preview_rgba32_on_next_frame, NULL);
	}
}
