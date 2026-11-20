#include "../global.h"

// The layers under the one being edited, composited once into four uv space targets (make_mesh.c with make_mesh_bake):
// the viewport starts from them and composites only the edited layer and the ones above it each frame. The targets
// hold the running state of the composite after the cached layers: base color and translucency, tangent normal and
// material id, occlusion, roughness, metallic and height, the height a texel away each way (the normals of height).
//
// The bake is made again when anything it reads changes. Before each frame the bake shader is bound for every paint
// object without drawing, and what that bound is hashed: every texture it samples with the stamp of its last write
// (gpu_texture_written: painting, a fill's tiles sharpening, a mask an anchor evaluates again), its constants (opacity
// sliders, object transforms) and the shader itself (made again when the layers change shape).
//
// The targets are the size of the layers, so a fill layer in it is at that resolution instead of evaluated for each
// screen pixel as live: its finest grain is a little softer zoomed in (as exported at that resolution). Twice the size
// came closer at four times the memory and bake time.
//
// Layers that cannot be in it end the cached run below them: an object mask or another uv map (the targets are one uv
// space for all objects), udim tiles, and masks reading the edited layer (an anchor would make the bake again with each
// stroke).

#define LAYER_CACHE_TARGETS 4

static slot_layer_t_array_t *layer_cache_layers = NULL; // Cached, bottom up
static shader_context_t     *layer_cache_scon   = NULL;
static u64                   layer_cache_key    = 0;
static bool                  layer_cache_ok     = false; // Planned, its shader compiled
static i32                   layer_cache_bakes  = 0;
static i32                   layer_cache_w      = 0;
static i32                   layer_cache_h      = 0;
static char                 *layer_cache_names[LAYER_CACHE_TARGETS] = {"layer_cache0", "layer_cache1", "layer_cache2", "layer_cache3"};

bool layer_cache_active(void) {
	return layer_cache_ok;
}

// Bakes made since the start (tests: an unchanged scene makes none)
i32 layer_cache_bake_count(void) {
	return layer_cache_bakes;
}

bool layer_cache_has(slot_layer_t *l) {
	return layer_cache_ok && layer_cache_layers != NULL && array_index_of(layer_cache_layers, l) >= 0;
}

// The layer being edited: a mask's or filter's layer, a group as itself
static slot_layer_t *layer_cache_edited(void) {
	slot_layer_t *l = g_context->layer;
	if (l != NULL && (slot_layer_is_mask(l) || slot_layer_is_filter(l)) && l->parent != NULL) {
		l = l->parent;
	}
	return l;
}

// l is e or under it: its mask or filter, a layer of its group
static bool layer_cache_within(slot_layer_t *l, slot_layer_t *e) {
	for (i32 d = 0; l != NULL && d < 64; ++d, l = l->parent) {
		if (l == e) {
			return true;
		}
	}
	return false;
}

static bool layer_cache_can(slot_layer_t *l, slot_layer_t *edited) {
	if (slot_layer_get_object_mask(l) > 0 || l->uv_map == 1 || util_mesh_udim_layer(l)) {
		return false;
	}
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	for (i32 i = 0; masks != NULL && i < masks->length; ++i) {
		if (slot_layer_is_visible(masks->buffer[i]) && mask_stack_reads(masks->buffer[i], edited)) {
			return false;
		}
	}
	return true;
}

static void layer_cache_free_targets(void) {
	if (layer_cache_w == 0) {
		return;
	}
	for (i32 i = 0; i < LAYER_CACHE_TARGETS; ++i) {
		render_path_remove_target(layer_cache_names[i]);
	}
	render_path_remove_target("layer_cache_temp");
	layer_cache_w = 0;
	layer_cache_h = 0;
}

static void layer_cache_make_targets(void) {
	i32 w = config_get_texture_res_x();
	i32 h = config_get_texture_res_y();
	if (layer_cache_w == w && layer_cache_h == h) {
		return;
	}
	layer_cache_free_targets();
	for (i32 i = 0; i < LAYER_CACHE_TARGETS; ++i) {
		render_path_add_target(layer_cache_names[i], w, h, "RGBA64", 1.0);
	}
	render_path_add_target("layer_cache_temp", w, h, "RGBA64", 1.0); // Dilation reads a copy
	layer_cache_w = w;
	layer_cache_h = h;
}

// Which layers the cache takes, then its shader. Before the mesh shaders are made (make_material_parse_mesh_material):
// they leave out the cached layers and start from the cache. glsl: the shader is not loaded and its GLSL is kept there
// (malloc'd) to translate and compile on a thread (layer_cache_plan_take), NULL to load it now.
static void layer_cache_plan(char **glsl) {
	if (layer_cache_scon != NULL) {
		make_material_delete_context(layer_cache_scon);
		layer_cache_scon = NULL;
	}
	layer_cache_ok  = false;
	layer_cache_key = 0;
	if (layer_cache_layers == NULL) {
		layer_cache_layers = any_array_create_from_raw((void *[]){}, 0);
	}
	layer_cache_layers->length = 0;

	slot_layer_t *edited = layer_cache_edited();
	bool          usable = edited != NULL && g_project != NULL && g_project->_->paint_objects->length > 0 && g_context->tool != TOOL_TYPE_COLORID &&
	              g_context->viewport_mode != VIEWPORT_MODE_PATH_TRACE;
	i32  textures      = 0;
	bool fills_counted = false;
	for (i32 i = 0; usable && i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (layer_cache_within(l, edited)) {
			break;
		}
		if (!slot_layer_is_layer(l) || !slot_layer_is_visible(l) || slot_layer_has_visible_filter(l)) {
			continue; // A mask goes with its layer, the viewport draws none of these
		}
		if (!layer_cache_can(l, edited)) {
			break;
		}
		i32 count = 3; // As make_mesh_run counts them
		if (l->fill != NULL) {
			count         = make_fill_texture_count(l) + (fills_counted ? 0 : 1);
			fills_counted = true;
		}
		slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
		count += masks != NULL ? masks->length : 0;
		if (textures + count > GPU_MAX_TEXTURES - 8) {
			break; // The bake is one pass
		}
		textures += count;
		any_array_push(layer_cache_layers, l);
	}

	if (layer_cache_layers->length < 2) { // One layer is as cheap live
		layer_cache_layers->length = 0;
		if (glsl == NULL) {
			layer_cache_free_targets();
		}
		return;
	}

	layer_cache_ok              = true; // make_mesh_run reads the plan
	make_mesh_bake              = true;
	make_mesh_defer_translation = glsl != NULL;
	material_t            *mm   = ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
	node_shader_context_t *con  = make_mesh_run(mm, 0);
	make_mesh_bake              = false;
	make_mesh_defer_translation = false;
	layer_cache_scon            = con->data;
	node_shader_context_free(con);
	free(mm);
	if (glsl != NULL) {
		*glsl                   = make_mesh_deferred_glsl;
		make_mesh_deferred_glsl = NULL;
		return; // Checked once compiled (make_material.c)
	}
	shader_context_load(layer_cache_scon);

	if (layer_cache_scon == NULL || layer_cache_scon->_->pipe == NULL) {
		layer_cache_ok             = false;
		layer_cache_layers->length = 0;
		return;
	}
	layer_cache_make_targets();
}

void layer_cache_prepare(void) {
	layer_cache_plan(NULL);
}

// A plan made aside while the current one stays in use (make_material_parse_mesh_material_async): the cache's state
// moved out of it, put in place again with layer_cache_plan_put
struct layer_cache_plan {
	slot_layer_t_array_t *layers;
	shader_context_t     *scon;
	bool                  ok;
	u64                   key;
};

static layer_cache_plan_t *layer_cache_plan_out(void) {
	layer_cache_plan_t *p = calloc(1, sizeof(layer_cache_plan_t));
	p->layers             = layer_cache_layers;
	p->scon               = layer_cache_scon;
	p->ok                 = layer_cache_ok;
	p->key                = layer_cache_key;
	layer_cache_layers    = NULL;
	layer_cache_scon      = NULL;
	layer_cache_ok        = false;
	layer_cache_key       = 0;
	return p;
}

// The plan for the context as it is now, made while the current one is set aside, then the current one back in place.
// Returns the new plan with its shader not loaded (*scon, NULL when it caches nothing) and the GLSL to translate (*glsl,
// malloc'd). Whatever runs make_mesh_run with the new plan goes in made: called with the new plan in place.
layer_cache_plan_t *layer_cache_plan_take(shader_context_t **scon, char **glsl, void (*made)(void *), void *data) {
	layer_cache_plan_t *current = layer_cache_plan_out();
	*glsl                       = NULL;
	layer_cache_plan(glsl);
	*scon = layer_cache_ok ? layer_cache_scon : NULL;
	made(data);
	layer_cache_plan_t *next = layer_cache_plan_out();
	next->scon               = NULL; // Its compile owns it until layer_cache_plan_put
	layer_cache_plan_put(current, NULL);
	return next;
}

// Puts a plan in place, its shader compiled (NULL when it caches nothing, or when the plan holds it). The plan in use is
// freed.
void layer_cache_plan_put(layer_cache_plan_t *p, shader_context_t *scon) {
	if (layer_cache_scon != NULL) {
		make_material_delete_context(layer_cache_scon);
	}
	array_delete(layer_cache_layers);
	layer_cache_layers = p->layers;
	layer_cache_scon   = scon != NULL ? scon : p->scon;
	layer_cache_ok     = p->ok;
	layer_cache_key    = p->key; // 0 for a new plan: made on the next frame
	free(p);
	if (!layer_cache_ok) {
		layer_cache_free_targets();
	}
}

// A plan taken and not put in place
void layer_cache_plan_free(layer_cache_plan_t *p) {
	if (p == NULL) {
		return;
	}
	array_delete(p->layers);
	free(p);
}

// The viewport samples the cache: bound with the layers (render_path_paint_bind_layers)
void layer_cache_bind(void) {
	if (!layer_cache_ok) {
		return;
	}
	for (i32 i = 0; i < LAYER_CACHE_TARGETS; ++i) {
		render_path_bind_target(layer_cache_names[i], layer_cache_names[i]);
	}
}

static u64 layer_cache_hash(u64 h, const void *p, size_t n) {
	return hash_fnv1a(h, p, n);
}

// What the bake would read if drawn now: its shader, and per paint object the textures it would sample (each with its
// write stamp) and its constants
static u64 layer_cache_inputs_key(void) {
	u64 h   = HASH_FNV1A_BASIS;
	h       = layer_cache_hash(h, &layer_cache_scon, sizeof(layer_cache_scon));
	i32 r[] = {layer_cache_w, layer_cache_h, (i32)g_config->dilate_radius};
	h       = layer_cache_hash(h, r, sizeof(r));

	static gpu_texture_t *saved[GPU_MAX_TEXTURES];
	memcpy(saved, current_textures, sizeof(saved));
	_render_path_bind_params = NULL;
	render_path_paint_bind_layers();
	string_array_t *bind_params = _render_path_bind_params;
	uint8_t        *constants   = gpu_constant_data();
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		memset(current_textures, 0, sizeof(saved));
		memset(constants, 0, GPU_CONSTANT_BUFFER_SIZE); // The next draw sets what it reads
		uniforms_pos_unpack = o->data->scale_pos;
		uniforms_tex_unpack = o->data->scale_tex;
		transform_update(o->base->transform);
		uniforms_set_context_consts(layer_cache_scon, bind_params);
		uniforms_set_obj_consts(layer_cache_scon, o->base);
		for (i32 j = 0; j < GPU_MAX_TEXTURES; ++j) {
			gpu_texture_t *t = current_textures[j];
			if (t != NULL) {
				u64 v[2] = {(u64)(uintptr_t)t, t->written};
				h        = layer_cache_hash(h, v, sizeof(v));
			}
		}
		h = layer_cache_hash(h, constants, GPU_CONSTANT_BUFFER_SIZE);
	}
	_render_path_bind_params = NULL;
	render_path_paint_unbind_layers();
	memcpy(current_textures, saved, sizeof(saved));
	return h;
}

static void layer_cache_dilate(void) {
	if (g_config->dilate_radius <= 0 || g_context->paint2d) {
		return;
	}
	util_uv_cache_dilate_map();
	render_path_load_shader("Scene/copy_pass/copyRGBA64_pass");
	render_path_load_shader("Scene/dilate_pass/dilateRGBA64_pass");
	for (i32 i = 0; i < LAYER_CACHE_TARGETS; ++i) {
		render_path_set_target("layer_cache_temp", NULL, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0); // Overwritten whole
		render_path_bind_target(layer_cache_names[i], "tex");
		render_path_draw_shader("Scene/copy_pass/copyRGBA64_pass");
		render_path_set_target(layer_cache_names[i], NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target("layer_cache_temp", "tex");
		render_path_draw_shader("Scene/dilate_pass/dilateRGBA64_pass");
	}
}

static void layer_cache_bake(void) {
	static string_array_t additional = {0};
	additional.length                = 0;
	for (i32 i = 1; i < LAYER_CACHE_TARGETS; ++i) {
		string_array_push(&additional, layer_cache_names[i]);
	}
	render_path_set_target(layer_cache_names[0], &additional, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
	gpu_label_pass("layer cache");
	render_path_paint_bind_layers();
	_mesh_object_last_pipeline = NULL;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_draw(g_project->_->paint_objects->buffer[i], layer_cache_scon, _render_path_bind_params);
	}
	_render_path_bind_params = NULL;
	render_path_paint_unbind_layers();
	render_path_end();
	_mesh_object_last_pipeline = NULL;
	layer_cache_dilate();
}

// Before the viewport draws its layers: the cache made again if what it reads changed
void layer_cache_update(void) {
	if (!layer_cache_ok) {
		return;
	}
	layer_cache_make_targets();
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	if (layer_cache_scon->_->pipe->impl.pipeline == NULL) { // Still compiling: it would draw nothing
		layer_cache_key = 0;
		return;
	}
	u64 key = layer_cache_inputs_key();
	if (key == layer_cache_key) {
		return;
	}
	layer_cache_bake();
	layer_cache_key = key;
	layer_cache_bakes++;
}
