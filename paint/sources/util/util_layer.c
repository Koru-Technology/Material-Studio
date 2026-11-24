#include "../global.h"
#ifdef IRON_IOS
#include <os/proc.h>
#endif

#ifdef IRON_IOS
i32 layers_max_layers = 32;
#else
i32 layers_max_layers = 255;
#endif

// Room for n more layers, masks and groups. Undo and redo bring back what they took out at any count: a project from the
// Mac can hold more than the iPad makes.
bool layers_room(i32 n) {
	return history_applying > 0 || g_project->_->layers->length + n <= layers_max_layers;
}

// Tests: the layer limit at n (the iPad's on the Mac), the one before returned
i32 layers_test_set_max(i32 n) {
	i32 old           = layers_max_layers;
	layers_max_layers = n;
	return old;
}

// ---- Memory for layers ----
// iOS ends an app that takes more memory than it may. Layers are the largest allocations: a full resolution target a
// channel, 768 MB for an 8K 8 bit layer. What the user adds (layers, masks, duplicates, smart materials, a higher
// resolution or more bits, opening a project) is refused with a toast when it would leave less than the reserve: room
// for the scratch targets painting, compositing and saving take (a 16 bit target of the working resolution) and the
// rest of the app. Caches that render again are dropped first. Undo and redo bring back what existed, never refused
// (history_applying, layers_alloc_unchecked). The Mac has swap: never refused.

#define LAYERS_MEMORY_RESERVE_MB 512

i32        layers_alloc_unchecked = 0; // > 0: allocations that replace what was just freed (a mesh import), not checked
static i32 layers_alloc_refused   = 0;

// Bytes of the full resolution targets of a layer (3 at bits) or a mask (1, RGBA32) of w x h, 0 for a group
i64 layers_slot_bytes_at(layer_slot_type_t type, i32 w, i32 h, texture_bits_t bits) {
	i64 texels = (i64)w * h;
	return type == LAYER_SLOT_TYPE_LAYER  ? 3 * texels * gpu_texture_format_size(layers_format(bits))
	       : type == LAYER_SLOT_TYPE_MASK ? texels * 4
	                                      : 0;
}

// A new layer, mask or group of the working resolution and bits
i64 layers_slot_bytes(layer_slot_type_t type) {
	return layers_slot_bytes_at(type, config_get_texture_res_x(), config_get_texture_res_y(), base_bits);
}

static layer_slot_type_t layers_slot_type(slot_layer_t *l) {
	return slot_layer_is_mask(l) ? LAYER_SLOT_TYPE_MASK : slot_layer_is_group(l) ? LAYER_SLOT_TYPE_GROUP : LAYER_SLOT_TYPE_LAYER;
}

// The project's layers and masks at w x h and bits
i64 layers_project_bytes(i32 w, i32 h, texture_bits_t bits) {
	i64 bytes = 0;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		bytes += layers_slot_bytes_at(layers_slot_type(g_project->_->layers->buffer[i]), w, h, bits);
	}
	return bytes;
}

// l with its masks and children, as a duplicate copies them
i64 layers_tree_bytes(slot_layer_t *l) {
	i64 bytes = 0;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *c = g_project->_->layers->buffer[i];
		for (slot_layer_t *p = c; p != NULL; p = p->parent) {
			if (p == l) {
				bytes += layers_slot_bytes(layers_slot_type(c));
				break;
			}
		}
	}
	return bytes;
}

// The memory iOS still lets the app take, -1 when unknown (elsewhere)
i64 layers_memory_available() {
#ifdef IRON_IOS
	if (__builtin_available(iOS 13.0, *)) {
		i64 avail = (i64)os_proc_available_memory();
		return avail > 0 ? avail : -1;
	}
#endif
	return -1;
}

static bool layers_memory_fits(i64 bytes) {
	i64 avail = layers_memory_available();
	if (avail < 0) {
		return true;
	}
	i64 reserve = (i64)LAYERS_MEMORY_RESERVE_MB * 1024 * 1024 + (i64)config_get_texture_res_x() * config_get_texture_res_y() * 8;
	return bytes + reserve <= avail;
}

// True when bytes of layer targets can be allocated now. False after a toast: the caller allocates nothing.
bool layers_can_allocate(i64 bytes) {
	if (bytes <= 0 || history_applying > 0 || layers_alloc_unchecked > 0 || layers_memory_fits(bytes)) {
		return true;
	}
	base_on_memory_warning(); // What renders again goes, then once more
	if (layers_memory_fits(bytes)) {
		return true;
	}
	layers_alloc_refused++;
	iron_log("Not enough memory for %d MB of layers, %d MB available", (i32)(bytes >> 20), (i32)(layers_memory_available() >> 20));
	console_toast(tr("Not enough memory"));
	return false;
}

// Tests: allocations refused so far, the memory available in MB (-1 unknown)
i32 layers_alloc_refusals() {
	return layers_alloc_refused;
}

i32 layers_memory_available_mb() {
	i64 avail = layers_memory_available();
	return avail < 0 ? -1 : (i32)(avail >> 20);
}

mat4_t    _layers_decal_mat;
i32       _layers_position;
i32       _layers_base_color;
f32       _layers_occlusion;
f32       _layers_roughness;
f32       _layers_metallic;

// Layer textures at bits per channel (base_bits): texture format and copy pipeline, gpu_texture_format_name() for render targets
gpu_texture_format_t layers_format(texture_bits_t bits) {
	return bits == TEXTURE_BITS_BITS8 ? GPU_TEXTURE_FORMAT_RGBA32 : bits == TEXTURE_BITS_BITS16 ? GPU_TEXTURE_FORMAT_RGBA64 : GPU_TEXTURE_FORMAT_RGBA128;
}

// The flattened and exported occlusion, roughness, metallic and height: at least 16 bit float, raised layers take the
// height below 0 and above 1
gpu_texture_format_t layers_pack_format(texture_bits_t bits) {
	return bits == TEXTURE_BITS_BITS8 ? GPU_TEXTURE_FORMAT_RGBA64 : layers_format(bits);
}

gpu_pipeline_t *layers_copy_pipe(texture_bits_t bits) {
	return bits == TEXTURE_BITS_BITS8 ? pipes_copy : bits == TEXTURE_BITS_BITS16 ? pipes_copy64 : pipes_copy128;
}

static void util_layer_draw_preview(slot_layer_t *l, gpu_texture_t *target) {
	draw_begin(target, true, 0x00000000);
	draw_set_pipeline(pipes_copy); // texpaint_preview is always RGBA32 for now
	draw_scaled_image(slot_layer_is_mask(l) ? slot_layer_mask_texture(l) : l->texpaint, 0, 0, target->width, target->height);
	draw_set_pipeline(NULL);
	draw_end();
	if (target == l->texpaint_preview) {
		layers_thumb_queue(l); // The list's thumbnail read back (util_layer_thumbs.c)
	}
}

void util_layer_update_preview() {
	if (layers_sync_nodes()) { // Nodes follow the layers they read
		layers_update_linked_layers();
		make_material_parse_mesh_material();
	}
	if (export_tiled_frozen()) { // The layers are placeholders: the previews wait for them
		return;
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
		}
	}
	layers_thumbs_flush();
}

void layers_init() {
	slot_layer_clear(g_project->_->layers->buffer[0], color_from_floats(layers_default_base, layers_default_base, layers_default_base, 1.0), NULL, 1.0,
	                 layers_default_rough, 0.0);
}

// The layers of a new project to the working resolution (project_new). Changes of an open project's resolution keep
// detail and are undoable: layers_resolution_begin (util/util_layer_resolution.c).
void layers_resize() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
	layers_resize_targets();

	render_path_raytrace_ready = false; // Rebuild baketex
	g_context->ddirty          = 2;
	layers_crop                = false;
}

// Live layer and paint targets that follow the texture resolution (layers out of the project for undo are records,
// without textures)
void layers_resize_targets() {
	material_cache_reset(); // And material caches

	any_map_t *rts = render_path_render_targets;

	render_path_paint_scratch_resize(config_get_texture_res_x(), config_get_texture_res_y());

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

// The layers to base_bits. Every layer and mask is made again, the old ones held until the frame ends: refused when that
// memory is not there (false, base_bits back to the bits of the layers).
bool layers_set_bits() {
	export_tiled_abort_for_project_change();
	texture_bits_t now = base_bits;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_layer(l) && l->texpaint != NULL) {
			gpu_texture_format_t f = l->texpaint->format;
			now = f == GPU_TEXTURE_FORMAT_RGBA32 ? TEXTURE_BITS_BITS8 : f == GPU_TEXTURE_FORMAT_RGBA64 ? TEXTURE_BITS_BITS16 : TEXTURE_BITS_BITS32;
			break;
		}
	}
	if (now != base_bits && !layers_can_allocate(layers_project_bytes(config_get_texture_res_x(), config_get_texture_res_y(), base_bits))) {
		base_bits = now;
		return false;
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
	return true;
}

// Tests: the layers at bits 8, 16 or 32 a channel
void layers_test_set_bits(i32 bits) {
	texture_bits_t b = bits == 32 ? TEXTURE_BITS_BITS32 : bits == 16 ? TEXTURE_BITS_BITS16 : TEXTURE_BITS_BITS8;
	if (b != base_bits) {
		base_bits = b;
		layers_set_bits();
	}
}

void layers_make_temp_img() {
	slot_layer_t *l = g_project->_->layers->buffer[0];

	if (layers_temp_image != NULL && (layers_temp_image->width != l->texpaint->width || layers_temp_image->height != l->texpaint->height ||
	                                  layers_temp_image->format != l->texpaint->format)) {
		render_path_remove_target("temptex0");
		layers_temp_image = NULL;
	}

	if (layers_temp_image == NULL) {
		layers_temp_image = render_path_add_target("temptex0", l->texpaint->width, l->texpaint->height, gpu_texture_format_name(layers_format(base_bits)), 1.0)->_image;
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
		render_path_remove_target("expa");
		render_path_remove_target("expb");
		render_path_remove_target("expc");
		layers_expa = NULL;
		layers_expb = NULL;
		layers_expc = NULL;
	}
	if (layers_expa == NULL) {
		char *format = gpu_texture_format_name(layers_format(base_bits));
		i32   w      = l->texpaint->width;
		i32   h      = l->texpaint->height;
		layers_expa  = render_path_add_target("expa", w, h, format, 1.0)->_image;
		layers_expb  = render_path_add_target("expb", w, h, format, 1.0)->_image;
		layers_expc  = render_path_add_target("expc", w, h, gpu_texture_format_name(layers_pack_format(base_bits)), 1.0)->_image;
	}
}

void layers_commands_merge_pack(gpu_pipeline_t *pipe, gpu_texture_t *i0, gpu_texture_t *i1, gpu_texture_t *i1pack, f32 i1mask_opacity, gpu_texture_t *i1texmask,
                                i32 i1blending, gpu_texture_t *temp) {
	_gpu_begin(i0, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipe);
	gpu_set_texture(pipes_tex0, i1);
	gpu_set_texture(pipes_tex1, i1pack);
	gpu_set_texture(pipes_texmask, i1texmask);
	gpu_set_texture(pipes_texa, temp); // The copy of i0
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

// Pushes a history step that undoes and redoes together with the one before it (one undo for several steps of one
// action). Nothing is linked when the push added no step.
void layers_history_with_prev(void (*push)(void)) {
	i32 before = history_steps->length;
	push();
	if (history_steps->length > before && history_steps->length > 1) {
		history_steps->buffer[history_steps->length - 1]->with_prev = true;
	}
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
slot_layer_t_array_t *layers_init_layer_map() {
	layers_sync_nodes();
	return NULL;
}

void layers_remap_layer_pointers(slot_layer_t_array_t *old_layers) {
	layers_sync_nodes();
}

