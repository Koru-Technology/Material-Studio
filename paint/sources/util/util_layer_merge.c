#include "../global.h"

// Merging: masks applied, layers merged down, groups merged, the stack flattened for export

// Pipelines are made for one target format (Metal): base, or a copy of it made for a target of format (16 and 32 bit
// layers, RGBA32 masks merged through a layer sized temp)
gpu_pipeline_t *layers_pipe_for(gpu_pipeline_t *base, gpu_texture_format_t format) {
	typedef struct {
		gpu_pipeline_t      *base;
		gpu_texture_format_t format;
		gpu_pipeline_t      *pipe;
	} variant_t;
	static variant_t variants[64];
	static i32 count = 0;
	if (base->color_attachment[0] == format) {
		return base;
	}
	for (i32 i = 0; i < count; ++i) {
		if (variants[i].base == base && variants[i].format == format) {
			return variants[i].pipe;
		}
	}
	gpu_pipeline_t     *pipe = gpu_create_pipeline();
	gpu_pipeline_impl_t impl = pipe->impl;
	*pipe                    = *base;
	pipe->impl               = impl;
	pipe->color_attachment[0] = format;
	gpu_pipeline_compile(pipe);
	if (count < 64) {
		variants[count++] = (variant_t){base, format, pipe};
	}
	return pipe;
}

void layers_apply_masks(slot_layer_t *l) {
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, false);

	if (masks != NULL) {
		for (i32 i = 0; i < masks->length; ++i) {
			masks->buffer[i]->visible ? slot_layer_apply_mask(masks->buffer[i]) : slot_layer_delete(masks->buffer[i]);
		}
		g_context->layer_preview_dirty = true;
	}
}

static void layers_bake_opacity(slot_layer_t *l) {
	if (slot_layer_is_mask(l) && !mask_stack_is_trivial(l)) { // The evaluated stack becomes the paint
		gpu_texture_t *eval = slot_layer_mask_texture(l);
		util_copy_texture(l->texpaint, eval, layers_pipe_for(pipes_copy, l->texpaint->format));
		l->mask_effects = NULL;
		mask_stack_free_texture(l);
	}
	if (l->mask_opacity < 1.0 && slot_layer_is_mask(l)) {
		layers_make_temp_img();
		util_copy_texture(layers_temp_image, l->texpaint, layers_pipe_for(pipes_copy, layers_temp_image->format));
		_gpu_begin(l->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(layers_pipe_for(pipes_merge_mask, l->texpaint->format));
		gpu_set_texture(pipes_tex0_merge_mask, render_path_empty_white());
		gpu_set_texture(pipes_texa_merge_mask, layers_temp_image);
		gpu_set_float(pipes_opac_merge_mask, 1.0 - l->mask_opacity);
		gpu_set_int(pipes_blending_merge_mask, BLEND_TYPE_MIX);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
		l->mask_opacity = 1.0;
	}
	else if (l->mask_opacity < 1.0) {
		slot_layer_t *m = ALLOC_INIT(slot_layer_t, {.texpaint = render_path_empty_black(), .mask_opacity = 1.0 - l->mask_opacity});
		layers_apply_mask(l, m);
		free(m);
		l->mask_opacity = 1.0;
	}
}

void layers_merge_down() {
	slot_layer_t *l1 = g_context->layer;
	slot_layer_t *l0 = g_project->_->layers->buffer[array_index_of(g_project->_->layers, l1) - slot_layer_count_descendants(l1) - 1];

	if (!slot_layer_shows(l1) || !slot_layer_shows(l0)) {
		return; // Refused, layers_can_merge_down
	}

	if (slot_layer_is_group(l1)) {
		l1 = layers_merge_group(l1);
	}
	else if (slot_layer_has_masks(l1, false)) { // It is a layer
		layers_apply_masks(l1);
		context_set_layer(l1);
	}

	if (slot_layer_is_group(l0)) {
		l0 = layers_merge_group(l0);
	}
	else if (slot_layer_has_masks(l0, false)) { // It is a layer
		layers_apply_masks(l0);
		context_set_layer(l0);
	}

	layers_bake_opacity(l0);
	if (slot_layer_is_layer(l0)) {
		if (l0->object_mask != l1->object_mask) {
			l0->object_mask = 0;
		}
	}
	layers_merge_layer(l0, l1, false);
	slot_layer_delete(l1);
	context_set_layer(l0);
	g_context->layer_preview_dirty = true;
}

slot_layer_t *layers_merge_group(slot_layer_t *l) {
	if (!slot_layer_is_group(l)) {
		return NULL;
	}

	if (!slot_layer_shows(l)) {
		if (layers_can_delete(l)) {
			slot_layer_delete(l);
			for (slot_layer_t *p = l->parent; p != NULL && slot_layer_get_children(p) == NULL; p = p->parent) {
				slot_layer_delete(p);
			}
		}
		return NULL;
	}

	slot_layer_t_array_t *direct = slot_layer_get_direct_children(l);
	for (i32 i = 0; i < direct->length; ++i) {
		if (!slot_layer_shows(direct->buffer[i])) {
			slot_layer_delete(direct->buffer[i]);
		}
	}

	slot_layer_t_array_t *masks = slot_layer_get_masks(l, false);
	for (i32 i = 0; masks != NULL && i < masks->length; ++i) {
		masks->buffer[i]->visible ? slot_layer_apply_mask(masks->buffer[i]) : slot_layer_delete(masks->buffer[i]);
	}

	direct = slot_layer_get_direct_children(l);
	for (i32 i = 0; i < direct->length; ++i) {
		direct->buffer[i]->mask_opacity *= l->mask_opacity;
		if (slot_layer_is_group(direct->buffer[i])) {
			layers_merge_group(direct->buffer[i]);
		}
	}

	slot_layer_t_array_t *children = slot_layer_get_children(l);

	if (children->length == 1 && slot_layer_has_masks(children->buffer[0], false)) {
		layers_apply_masks(children->buffer[0]);
	}

	for (i32 i = 0; i < children->length - 1; ++i) {
		context_set_layer(children->buffer[children->length - 1 - i]);
		layers_merge_down();
	}

	children->buffer[0]->parent = l->parent;
	children->buffer[0]->name   = l->name;
	if (children->buffer[0]->fill_material != NULL) {
		slot_layer_to_paint_layer(children->buffer[0]);
	}
	slot_layer_delete(l);
	return children->buffer[0];
}

void layers_merge_layer(slot_layer_t *l0, slot_layer_t *l1, bool use_mask) {
	layers_make_temp_img();
	layers_merge_layer_ex(l0, l1, use_mask, layers_temp_image);
}

// layers_merge_layer on textures of any size (timeline replays), temp a scratch like l0's
void layers_merge_layer_ex(slot_layer_t *l0, slot_layer_t *l1, bool use_mask, gpu_texture_t *temp) {
	if (!l1->visible || slot_layer_is_group(l1)) {
		return;
	}

	util_copy_texture(temp, l0->texpaint, layers_pipe_for(pipes_copy, temp->format)); // Copy to temp

	gpu_texture_t        *empty    = render_path_empty_white();
	gpu_texture_t        *mask     = empty;
	slot_layer_t_array_t *l1masks  = use_mask ? slot_layer_get_masks(l1, true) : NULL;
	if (l1masks != NULL) {
		gpu_texture_t *m0 = slot_layer_mask_texture(l1masks->buffer[0]);
		layers_make_temp_mask_img();
		_gpu_begin(pipes_temp_mask_image, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(layers_pipe_for(pipes_merge_mask, pipes_temp_mask_image->format));
		gpu_set_texture(pipes_tex0_merge_mask, m0);
		gpu_set_texture(pipes_texa_merge_mask, empty);
		gpu_set_float(pipes_opac_merge_mask, l1masks->buffer[0]->mask_opacity);
		gpu_set_int(pipes_blending_merge_mask, BLEND_TYPE_MIX);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
		mask = pipes_temp_mask_image;
	}

	if (slot_layer_is_mask(l1)) {
		gpu_texture_t *m1 = slot_layer_mask_texture(l1); // Evaluated stack
		_gpu_begin(l0->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(layers_pipe_for(pipes_merge_mask, l0->texpaint->format));
		gpu_set_texture(pipes_tex0_merge_mask, m1);
		gpu_set_texture(pipes_texa_merge_mask, temp);
		gpu_set_float(pipes_opac_merge_mask, l1->mask_opacity);
		gpu_set_int(pipes_blending_merge_mask, BLEND_TYPE_MULTIPLY);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
	}

	if (slot_layer_is_layer(l1)) {
		gpu_texture_t *weight = gpu_create_render_target(l0->texpaint->width, l0->texpaint->height, l0->texpaint->format);
		_gpu_begin(weight, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(layers_pipe_for(pipes_merge, weight->format));
		gpu_set_texture(pipes_tex0, l1->texpaint);
		gpu_set_texture(pipes_tex1, empty);
		gpu_set_texture(pipes_texmask, mask);
		gpu_set_texture(pipes_texa, temp);
		gpu_set_float(pipes_opac, l1->mask_opacity);
		gpu_set_float(pipes_tex1w, empty->width);
		gpu_set_int(pipes_blending, 105);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();

		if (l1->paint_base) {
			_gpu_begin(l0->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(layers_pipe_for(pipes_merge, l0->texpaint->format));
			gpu_set_texture(pipes_tex0, l1->texpaint);
			gpu_set_texture(pipes_tex1, empty);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, temp);
			gpu_set_float(pipes_opac, l1->mask_opacity);
			gpu_set_float(pipes_tex1w, empty->width);
			gpu_set_int(pipes_blending, 200 + slot_layer_get_blending(l1) + (g_project->linear_blending ? 1000 : 0));
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
		}

		if (l0->texpaint_nor != NULL) {
			util_copy_texture(temp, l0->texpaint_nor, layers_pipe_for(pipes_copy, temp->format));

			if (l1->paint_nor) {
				_gpu_begin(l0->texpaint_nor, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
				gpu_set_pipeline(layers_pipe_for(pipes_merge, l0->texpaint_nor->format));
				gpu_set_texture(pipes_tex0, weight);
				gpu_set_texture(pipes_tex1, l1->texpaint_nor);
				gpu_set_texture(pipes_texmask, empty);
				gpu_set_texture(pipes_texa, temp);
				gpu_set_float(pipes_opac, 1.0);
				gpu_set_float(pipes_tex1w, l1->texpaint_nor->width);
				gpu_set_int(pipes_blending, l1->paint_nor_blend ? 106 : 101);
				gpu_set_vertex_buffer(const_data_screen_aligned_vb);
				gpu_set_index_buffer(const_data_screen_aligned_ib);
				gpu_draw();
				gpu_end();
			}
		}

		if (l0->texpaint_pack != NULL) {
			util_copy_texture(temp, l0->texpaint_pack, layers_pipe_for(pipes_copy, temp->format));

			if (l1->paint_occ || l1->paint_rough || l1->paint_met || l1->paint_height) {
				if (l1->paint_occ && l1->paint_rough && l1->paint_met && l1->paint_height) {
					layers_commands_merge_pack(layers_pipe_for(pipes_merge, l0->texpaint_pack->format), l0->texpaint_pack, weight, l1->texpaint_pack, 1.0, empty, l1->paint_height_blend ? 107 : 101, temp);
				}
				else {
					if (l1->paint_occ) {
						layers_commands_merge_pack(layers_pipe_for(pipes_get_merge_r(), l0->texpaint_pack->format), l0->texpaint_pack, weight, l1->texpaint_pack, 1.0, empty, 101, temp);
					}
					if (l1->paint_rough) {
						layers_commands_merge_pack(layers_pipe_for(pipes_get_merge_g(), l0->texpaint_pack->format), l0->texpaint_pack, weight, l1->texpaint_pack, 1.0, empty, 101, temp);
					}
					if (l1->paint_met) {
						layers_commands_merge_pack(layers_pipe_for(pipes_get_merge_b(), l0->texpaint_pack->format), l0->texpaint_pack, weight, l1->texpaint_pack, 1.0, empty, 101, temp);
					}
					if (l1->paint_height) {
						layers_commands_merge_pack(layers_pipe_for(pipes_get_merge_a(), l0->texpaint_pack->format), l0->texpaint_pack, weight, l1->texpaint_pack, 1.0, empty,
						                           l1->paint_height_blend ? 107 : 101, temp);
					}
				}
			}
		}

		gpu_delete_texture(weight);
	}
}

// Multiplies mask m into dst, the masks of layer combined so far. A height blended mask moves its edge with the height of the layer
// compared to below (composited height of the layers under it in .a, NULL for a flat level)
void layers_merge_mask(gpu_texture_t *dst, slot_layer_t *m, slot_layer_t *layer, gpu_texture_t *below) {
	if (!m->height_blend || layer == NULL || layer->texpaint_pack == NULL) {
		slot_layer_t *t = ALLOC_INIT(slot_layer_t, {.texpaint = dst});
		layers_merge_layer(t, m, false);
		return;
	}
	layers_make_temp_img();
	layers_merge_mask_ex(dst, layers_temp_image, m, slot_layer_mask_texture(m), layer, below, util_uv_dilatemap_cached ? util_uv_dilatemap : NULL);
}

// layers_merge_mask on textures of any size (export tiles): mask_tex the evaluated stack of m, temp a scratch like dst,
// coverage the uv islands (NULL for everywhere)
void layers_merge_mask_ex(gpu_texture_t *dst, gpu_texture_t *temp, slot_layer_t *m, gpu_texture_t *mask_tex, slot_layer_t *layer, gpu_texture_t *below,
                          gpu_texture_t *coverage) {
	if (!m->height_blend || layer == NULL || layer->texpaint_pack == NULL) {
		if (!m->visible) {
			return;
		}
		util_copy_texture(temp, dst, layers_pipe_for(pipes_copy, temp->format));
		_gpu_begin(dst, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(layers_pipe_for(pipes_merge_mask, dst->format));
		gpu_set_texture(pipes_tex0_merge_mask, mask_tex);
		gpu_set_texture(pipes_texa_merge_mask, temp);
		gpu_set_float(pipes_opac_merge_mask, m->mask_opacity);
		gpu_set_int(pipes_blending_merge_mask, BLEND_TYPE_MULTIPLY);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
		return;
	}
	util_copy_texture(temp, dst, layers_pipe_for(pipes_copy, temp->format)); // Copy to temp

	_gpu_begin(dst, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(layers_pipe_for(pipes_height_blend_mask, dst->format));
	gpu_set_texture(0, mask_tex);
	gpu_set_texture(1, temp);
	gpu_set_texture(2, layer->texpaint_pack);
	gpu_set_texture(3, below != NULL ? below : render_path_empty_white());
	gpu_set_texture(4, coverage != NULL ? coverage : render_path_empty_white()); // Uv island coverage
	gpu_set_float(pipes_opac_height_blend, m->mask_opacity);
	gpu_set_float(pipes_influence_height_blend, m->height_blend_influence);
	gpu_set_float(pipes_sharpness_height_blend, m->height_blend_sharpness);
	gpu_set_float(pipes_offset_height_blend, m->height_blend_offset);
	gpu_set_float(pipes_below_height_blend, below != NULL && m->height_blend_below ? 1.0 : 0.0);
	gpu_set_float(pipes_reach_height_blend, m->height_blend_reach);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
}

// Composited height of the layers under l (pack .a), NULL when nothing below paints height
gpu_texture_t *layers_height_below(slot_layer_t *l) {
	slot_layer_t_array_t *below = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *b = g_project->_->layers->buffer[i];
		if (b == l) {
			break;
		}
		if (slot_layer_is_layer(b) && b->paint_height) {
			any_array_push(below, b);
		}
	}
	if (below->length == 0) {
		return NULL;
	}
	slot_layer_t *flat = layers_flatten(false, below);
	return flat->texpaint_pack;
}

static gpu_texture_t *layers_udim_a    = NULL;
static gpu_texture_t *layers_udim_b    = NULL;
static gpu_texture_t *layers_udim_c    = NULL;
static gpu_texture_t *layers_udim_mask = NULL;

static gpu_texture_t *_layers_udim_target(gpu_texture_t **t, gpu_texture_t *like) {
	if (*t != NULL && ((*t)->width != like->width || (*t)->height != like->height || (*t)->format != like->format)) {
		gpu_delete_texture(*t);
		*t = NULL;
	}
	if (*t == NULL) {
		*t = gpu_create_render_target(like->width, like->height, like->format);
	}
	return *t;
}

static gpu_texture_t *_layers_udim_copy(gpu_texture_t **target, gpu_texture_t *src, i32 slot, bool to_tile) {
	gpu_texture_t *dst    = _layers_udim_target(target, src);
	i32            stride = util_mesh_atlas_stride();
	f32            cx     = (slot % stride) / (f32)stride;
	f32            cy     = (slot / stride) / (f32)stride;
	draw_begin(dst, true, 0x00000000);
	draw_set_pipeline(layers_pipe_for(pipes_copy, dst->format));
	if (to_tile) {
		draw_scaled_sub_image(src, cx * src->width, cy * src->height, src->width / (f32)stride, src->height / (f32)stride, 0, 0, dst->width, dst->height);
	}
	else {
		draw_scaled_image(src, cx * dst->width, cy * dst->height, dst->width / (f32)stride, dst->height / (f32)stride);
	}
	draw_set_pipeline(NULL);
	draw_end();
	return dst;
}

// Remap layer textures between the shared udim atlas and a single tile, mask is remapped in place when set
slot_layer_t *layers_udim_remap(slot_layer_t *l, gpu_texture_t **mask, i32 slot, bool to_tile) {
	static slot_layer_t remapped;
	remapped               = *l;
	remapped.texpaint      = _layers_udim_copy(&layers_udim_a, l->texpaint, slot, to_tile);
	remapped.texpaint_nor  = _layers_udim_copy(&layers_udim_b, l->texpaint_nor, slot, to_tile);
	remapped.texpaint_pack = _layers_udim_copy(&layers_udim_c, l->texpaint_pack, slot, to_tile);
	if (mask != NULL) {
		*mask = _layers_udim_copy(&layers_udim_mask, *mask, slot, to_tile);
	}
	return &remapped;
}

// Atlas slot of a layer assigned to a single udim tile object, -1 otherwise
i32 layers_udim_tile_slot(slot_layer_t *l) {
	i32 mask = slot_layer_get_object_mask(l);
	if (!util_mesh_udim_active() || l->uv_map == 1 || mask <= 0 || mask > g_project->_->paint_objects->length) {
		return -1;
	}
	i32 tile = util_mesh_udim_tile(g_project->_->paint_objects->buffer[mask - 1]->base->name);
	return tile < 0 ? -1 : util_mesh_udim_slot(tile);
}

static gpu_texture_t *layers_move_a    = NULL;
static gpu_texture_t *layers_move_b    = NULL;
static gpu_texture_t *layers_move_c    = NULL;
static gpu_texture_t *layers_move_mask = NULL;

static gpu_texture_t *_layers_move_copy(gpu_texture_t **target, gpu_texture_t *src, gpu_pipeline_t *pipe, i32 dx, i32 dy) {
	gpu_texture_t *dst = _layers_udim_target(target, src);
	draw_begin(dst, true, 0x00000000);
	draw_set_pipeline(pipe);
	draw_image(src, dx, dy);
	draw_set_pipeline(NULL);
	draw_end();
	return dst;
}

// Layer with the pending 2D view move applied, the layer itself when it is not being moved
static slot_layer_t *layers_move_preview(slot_layer_t *l) {
	i32 dx, dy;
	if (ui_view2d_move_layer(&dx, &dy) != l) {
		return l;
	}
	static slot_layer_t moved;
	moved = *l;
	if (slot_layer_is_mask(l)) {
		moved.texpaint     = _layers_move_copy(&layers_move_mask, slot_layer_mask_texture(l), pipes_copy8, dx, dy);
		moved.mask_effects = NULL; // The moved copy is the evaluated stack
		moved.texmask      = NULL;
	}
	else {
		gpu_pipeline_t *pipe = layers_copy_pipe(base_bits);
		moved.texpaint       = _layers_move_copy(&layers_move_a, l->texpaint, pipe, dx, dy);
		moved.texpaint_nor   = _layers_move_copy(&layers_move_b, l->texpaint_nor, pipe, dx, dy);
		moved.texpaint_pack  = _layers_move_copy(&layers_move_c, l->texpaint_pack, pipe, dx, dy);
	}
	return &moved;
}

slot_layer_t *layers_flatten(bool height_to_normal, slot_layer_t_array_t *layers) {
	if (layers == NULL) {
		layers = g_project->_->layers;
	}
	layers_make_temp_img();
	layers_make_export_img();
	gpu_texture_t *empty = render_path_empty_white();

	// Clear export layer
	_gpu_begin(layers_expa, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(0.0, 0.0, 0.0, 0.0), 0.0);
	gpu_end();
	_gpu_begin(layers_expb, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(0.5, 0.5, 1.0, 0.0), 0.0);
	gpu_end();
	_gpu_begin(layers_expc, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(1.0, 0.0, 0.0, 0.0), 0.0);
	gpu_end();

	// Flatten layers
	for (i32 i = 0; i < layers->length; ++i) {
		slot_layer_t *l1 = layers->buffer[i];
		if (!slot_layer_is_visible(l1)) {
			continue;
		}
		if (!slot_layer_is_layer(l1)) {
			continue;
		}
		if (slot_layer_has_visible_filter(l1)) {
			continue;
		}

		gpu_texture_t        *mask    = empty;
		slot_layer_t_array_t *l1masks = slot_layer_get_masks(l1, true);
		if (l1masks != NULL) {
			layers_make_temp_mask_img();
			draw_begin(pipes_temp_mask_image, GPU_CLEAR_COLOR, 0xffffffff);
			draw_end();
			for (i32 i = 0; i < l1masks->length; ++i) {
				layers_merge_mask(pipes_temp_mask_image, layers_move_preview(l1masks->buffer[i]), l1, layers_expc);
			}
			mask = pipes_temp_mask_image;
		}

		l1 = layers_move_preview(l1);

		// Flattened layers are in shared atlas space
		i32 tile_slot = layers_udim_tile_slot(l1);
		if (tile_slot >= 0) {
			l1 = layers_udim_remap(l1, l1masks != NULL ? &mask : NULL, tile_slot, false);
		}

		// Opacity still counts without the base color channel, the path tracer and export read the alpha as opacity
		if (l1->paint_base || l1->paint_opac) {
			util_copy_texture(layers_temp_image, layers_expa, layers_pipe_for(pipes_copy, layers_temp_image->format)); // Copy to temp

			_gpu_begin(layers_expa, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(layers_pipe_for(l1->paint_base ? pipes_merge : pipes_get_merge_a(), layers_expa->format));
			gpu_set_texture(pipes_tex0, l1->texpaint);
			gpu_set_texture(pipes_tex1, empty);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, layers_temp_image);
			gpu_set_float(pipes_opac, slot_layer_get_opacity(l1));
			gpu_set_float(pipes_tex1w, empty->width);
			gpu_set_int(pipes_blending, (layers->length > 1 ? slot_layer_get_blending(l1) : 0) + (g_project->linear_blending ? 1000 : 0));
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
		}

		if (l1->paint_nor) {
			util_copy_texture(layers_temp_image, layers_expb, layers_pipe_for(pipes_copy, layers_temp_image->format));

			_gpu_begin(layers_expb, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(layers_pipe_for(pipes_merge, layers_expb->format));
			gpu_set_texture(pipes_tex0, l1->texpaint);
			gpu_set_texture(pipes_tex1, l1->texpaint_nor);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, layers_temp_image);
			gpu_set_float(pipes_opac, slot_layer_get_opacity(l1));
			gpu_set_float(pipes_tex1w, l1->texpaint_nor->width);
			gpu_set_int(pipes_blending, l1->paint_nor_blend ? 102 : 101);
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
		}

		if (l1->paint_occ || l1->paint_rough || l1->paint_met || l1->paint_height) {
			util_copy_texture(layers_temp_image, layers_expc, layers_pipe_for(pipes_copy, layers_temp_image->format));

			// The layer blend mode also applies to occlusion, roughness and metallic, as in the viewport
			bool pack_blend = make_material_blend_mode_channels(slot_layer_get_blending(l1)) && i > 0;
			i32  pack_mode  = pack_blend ? 300 + slot_layer_get_blending(l1) : 101;
			if (l1->paint_occ && l1->paint_rough && l1->paint_met && l1->paint_height) {
				layers_commands_merge_pack(layers_pipe_for(pipes_merge, layers_expc->format), layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask,
				                           pack_blend ? (l1->paint_height_blend ? 400 : 300) + slot_layer_get_blending(l1) : l1->paint_height_blend ? 103 : 101, layers_temp_image);
			}
			else {
				if (l1->paint_occ) {
					layers_commands_merge_pack(layers_pipe_for(pipes_get_merge_r(), layers_expc->format), layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, pack_mode, layers_temp_image);
				}
				if (l1->paint_rough) {
					layers_commands_merge_pack(layers_pipe_for(pipes_get_merge_g(), layers_expc->format), layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, pack_mode, layers_temp_image);
				}
				if (l1->paint_met) {
					layers_commands_merge_pack(layers_pipe_for(pipes_get_merge_b(), layers_expc->format), layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask, pack_mode, layers_temp_image);
				}
				if (l1->paint_height) {
					layers_commands_merge_pack(layers_pipe_for(pipes_get_merge_a(), layers_expc->format), layers_expc, l1->texpaint, l1->texpaint_pack, slot_layer_get_opacity(l1), mask,
					                           l1->paint_height_blend ? 103 : 101, layers_temp_image);
				}
			}
		}
	}

	slot_layer_t *l0 = ALLOC_INIT(slot_layer_t, {.texpaint = layers_expa, .texpaint_nor = layers_expb, .texpaint_pack = layers_expc});

	// Merge height map into normal map
	if (height_to_normal && make_material_height_used) {
		util_copy_texture(layers_temp_image, l0->texpaint_nor, layers_pipe_for(pipes_copy, layers_temp_image->format));

		_gpu_begin(l0->texpaint_nor, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(layers_pipe_for(pipes_merge, l0->texpaint_nor->format));
		gpu_set_texture(pipes_tex0, layers_temp_image);
		gpu_set_texture(pipes_tex1, l0->texpaint_pack);
		gpu_set_texture(pipes_texmask, empty);
		gpu_set_texture(pipes_texa, empty);
		gpu_set_float(pipes_opac, 1.0);
		gpu_set_float(pipes_tex1w, l0->texpaint_pack->width);
		gpu_set_int(pipes_blending, 104);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
	}

	return l0;
}
