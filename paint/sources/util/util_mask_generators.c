#include "../global.h"
#include "util_hash.h"

// Mask generators: effects of a mask stack (util_mask_stack.c) whose source is made from the mesh shape (Edge Wear, Dirt,
// Position Gradient, Directional, 3D Noise, Light, Thickness, Mask Builder, Ambient Occlusion), and fills of an image or
// grunge projected on it. The paint mesh is drawn flat in uv space (mask_generator.glsl) into the stack's scratch, giving
// each texel its object position and normal and the mesh maps, then dilated (mask_generator_dilate.glsl) into an R8
// texture cached on the mask. Inside the stack a generator then reads like the paint effect.
//
// The cache is keyed by a signature of everything the output depends on: the effect's params, the mask size, uv set and
// object mask, the drawn object and its transform, the mesh maps, a fill's image and the layer a Mask Builder takes its
// material reads too (its revision). Painting on the mask reuses it, an edit renders only the generator it changed.

static gpu_pipeline_t        *mask_gen_pipes[4]; // One a mesh vertex layout
static gpu_vertex_structure_t mask_gen_layouts[4];
static i32                    mask_gen_pipe_count = 0;
static gpu_pipeline_t        *mask_gen_pipe_dilate = NULL;
static i32                    mask_gen_n;
static i32                    mask_gen_params0;
static i32                    mask_gen_params1;
static i32                    mask_gen_params2;
static i32                    mask_gen_params3;
static i32                    mask_gen_pos_min;
static i32                    mask_gen_pos_inv;
static i32                    mask_gen_type;
static i32                    mask_gen_uv_map;
static i32                    mask_gen_noise_inv;
static i32                    mask_gen_uv_rect;
static i32                    mask_gen_maps;
static i32                    mask_gen_map_scale;
static i32                    mask_gen_radius_loc;
static i32                    mask_gen_size_loc;
static i32                    mask_gen_render_count = 0; // Generator passes so far (tests)
static u64                    mask_gen_mesh_gen     = 0; // Bumped when the painted meshes move or are merged again
static i32                    mask_gen_timing       = -1;

#define MASK_GEN_MAX_RADIUS 8 // Export dilates layers 256 texels, a generator does not need it

// Mesh bounds of the last drawn mesh, read again only when it changes
static mesh_data_t *mask_gen_bounds_md  = NULL;
static u64          mask_gen_bounds_gen = 0;
static vec4_t       mask_gen_bounds_min;
static vec4_t       mask_gen_bounds_max;

static u64 mask_gen_hash_string(u64 h, char *s) {
	return s != NULL ? hash_fnv1a(h, s, strlen(s)) : h;
}

void mask_gen_mesh_changed() {
	mask_gen_mesh_gen++;
}

i32 mask_gen_renders() {
	return mask_gen_render_count;
}

static i32 mask_gen_radius() {
	return math_min(math_max(g_config->dilate_radius, 1), MASK_GEN_MAX_RADIUS);
}

// The layer whose uvs and object mask the mask follows
static slot_layer_t *mask_gen_owner(slot_layer_t *m) {
	return m->parent != NULL ? m->parent : m;
}

// Merged object, or the paint object, or the object mask's object alone
static mesh_object_t *mask_gen_object(slot_layer_t *m) {
	i32 mask = slot_layer_get_object_mask(m);
	if (mask > 0 && mask <= g_project->_->paint_objects->length) {
		return g_project->_->paint_objects->buffer[mask - 1];
	}
	return g_context->merged_object != NULL ? g_context->merged_object : g_context->paint_object;
}

// The mesh maps generators read, bound in this order (mask_generator.glsl texcurv to texnor)
static const i32 mask_gen_maps_read[] = {MESH_MAP_CURVATURE, MESH_MAP_AO, MESH_MAP_THICKNESS, MESH_MAP_POSITION, MESH_MAP_WORLD_NORMAL, MESH_MAP_NORMAL};

// What every generator depends on: the meshes and the mesh maps
static u64 mask_gen_epoch() {
	u64 h    = 1469598103934665603ull;
	u64 v[2] = {mesh_maps_generation(), mask_gen_mesh_gen};
	h        = hash_fnv1a(h, v, sizeof(v));
	for (i32 i = 0; i < (i32)ARRAY_LENGTH(mask_gen_maps_read); ++i) {
		u64 t = (u64)(uintptr_t)mesh_maps_texture_model(mask_gen_maps_read[i]);
		h     = hash_fnv1a(h, &t, sizeof(t));
	}
	string_array_t *maps = g_project->mesh_maps;
	for (i32 i = 0; maps != NULL && i < maps->length; ++i) {
		h = mask_gen_hash_string(h, maps->buffer[i]);
	}
	return h;
}

// The image a fill reads, NULL for none
static gpu_texture_t *mask_gen_image(mask_effect_t *e) {
	return project_get_image(mask_effect_image(e));
}

static u64 mask_gen_signature(slot_layer_t *m, mask_effect_t *e, u64 epoch) {
	slot_layer_t  *owner = mask_gen_owner(m);
	mesh_object_t *o     = mask_gen_object(m);
	i32            v[7]  = {e->type,
	                        (i32)m->texpaint->width,
	                        (i32)m->texpaint->height,
	                        owner->uv_map,
	                        slot_layer_get_object_mask(m),
	                        util_mesh_udim_layer(owner) ? 1 : 0,
	                        mask_gen_radius()};
	slot_layer_t  *layer = mask_effect_builder_layer(e);
	u64            p[5]  = {(u64)(uintptr_t)o, (u64)(uintptr_t)o->data, (u64)(uintptr_t)mask_gen_image(e), // A reimported image is another
	                        (u64)(uintptr_t)layer, layer != NULL ? layer->revision : 0};
	u64            h     = hash_fnv1a(epoch, v, sizeof(v));
	h        = hash_fnv1a(h, e->params, sizeof(e->params));
	h        = hash_fnv1a(h, p, sizeof(p));
	h        = hash_fnv1a(h, &o->base->transform->world, sizeof(mat4_t)); // Directional
	return h;
}

static bool mask_gen_has_generator(slot_layer_t *m) {
	if (m->mask_effects == NULL) {
		return false;
	}
	i32 n = mask_stack_count(m);
	for (i32 i = 0; i < n; ++i) {
		mask_effect_t e = mask_stack_get(m, i);
		mask_effect_t g;
		if (mask_effect_rendered_as(&e, &g)) {
			return true;
		}
	}
	return false;
}

// Signatures of the visible generators in stack order
static u64 mask_gen_key(slot_layer_t *m, u64 epoch) {
	u64 key = 1469598103934665603ull;
	i32 n   = mask_stack_count(m);
	for (i32 i = 0; i < n; ++i) {
		mask_effect_t e = mask_stack_get(m, i);
		if (e.visible && mask_effect_rendered_as(&e, &e)) { // Its own output, or what it reads (an Edge Breakup's noise)
			u64 s = mask_gen_signature(m, &e, epoch);
			key   = hash_fnv1a(key, &s, sizeof(s));
		}
	}
	return key;
}

// The generators of m no longer match what texmask was evaluated with (mesh, transform, maps, object mask changed)
bool mask_gen_stale(slot_layer_t *m) {
	if (!mask_gen_has_generator(m)) {
		return false;
	}
	return mask_gen_key(m, mask_gen_epoch()) != m->mask_gen_key;
}

// Pipelines

// A layout of the mesh's own vertex arrays (Metal takes the stride from it), tex and tex1 as one 4 short attribute
static gpu_pipeline_t *mask_gen_pipeline(mesh_data_t *md) {
	gpu_vertex_structure_t  vs = {0};
	gpu_vertex_structure_t *s  = &md->_->structure;
	for (i32 i = 0; i < (i32)s->size; ++i) {
		if (string_equals((char *)s->elements[i].name, "tex") && i + 1 < (i32)s->size && string_equals((char *)s->elements[i + 1].name, "tex1")) {
			gpu_vertex_structure_add(&vs, "tex", GPU_VERTEX_DATA_I16_4X_NORM);
			i++;
		}
		else {
			gpu_vertex_structure_add(&vs, s->elements[i].name, s->elements[i].data);
		}
	}
	for (i32 i = 0; i < mask_gen_pipe_count; ++i) {
		gpu_vertex_structure_t *l    = &mask_gen_layouts[i];
		bool                    same = l->size == vs.size;
		for (i32 j = 0; same && j < (i32)vs.size; ++j) {
			same = l->elements[j].data == vs.elements[j].data;
		}
		if (same) {
			return mask_gen_pipes[i];
		}
	}
	i32 k = mask_gen_pipe_count < 4 ? mask_gen_pipe_count++ : 3; // Never more than pos, nor, tex, tex1, col combinations
	if (mask_gen_pipes[k] != NULL) {
		gpu_delete_pipeline(mask_gen_pipes[k]);
	}
	mask_gen_layouts[k]          = vs;
	gpu_pipeline_t *pipe         = gpu_create_pipeline();
	pipe->vertex_shader          = sys_get_shader("mask_generator.vert");
	pipe->fragment_shader        = sys_get_shader("mask_generator.frag");
	pipe->input_layout           = &mask_gen_layouts[k];
	pipe->color_attachment_count = 1;
	pipe->color_attachment[0]    = GPU_TEXTURE_FORMAT_RGBA32;
	gpu_pipeline_compile(pipe);
	mask_gen_pipes[k] = pipe;

	pipes_offset       = 0;
	mask_gen_n         = pipes_get_constant_location("float3x3");
	mask_gen_params0   = pipes_get_constant_location("float4");
	mask_gen_params1   = pipes_get_constant_location("float4");
	mask_gen_params2   = pipes_get_constant_location("float4");
	mask_gen_params3   = pipes_get_constant_location("float4");
	mask_gen_pos_min   = pipes_get_constant_location("float3");
	mask_gen_pos_inv   = pipes_get_constant_location("float3");
	mask_gen_type      = pipes_get_constant_location("int");
	mask_gen_uv_map    = pipes_get_constant_location("int");
	mask_gen_noise_inv = pipes_get_constant_location("float");
	mask_gen_uv_rect   = pipes_get_constant_location("float4");
	mask_gen_maps      = pipes_get_constant_location("int");
	mask_gen_map_scale = pipes_get_constant_location("float");
	return pipe;
}

static void mask_gen_init_dilate() {
	if (mask_gen_pipe_dilate != NULL) {
		return;
	}
	mask_gen_pipe_dilate                  = gpu_create_pipeline();
	mask_gen_pipe_dilate->vertex_shader   = sys_get_shader("mask_generator_dilate.vert");
	mask_gen_pipe_dilate->fragment_shader = sys_get_shader("mask_generator_dilate.frag");
	gpu_vertex_structure_t *vs            = ALLOC_INIT(gpu_vertex_structure_t, {0});
	gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_F32_2X);
	mask_gen_pipe_dilate->input_layout           = vs;
	mask_gen_pipe_dilate->color_attachment_count = 1;
	mask_gen_pipe_dilate->color_attachment[0]    = GPU_TEXTURE_FORMAT_R8;
	gpu_pipeline_compile(mask_gen_pipe_dilate);
	pipes_offset        = 0;
	mask_gen_radius_loc = pipes_get_constant_location("float");
	mask_gen_size_loc   = pipes_get_constant_location("float2");
}

// Rendering

typedef struct {
	char *name;
	i32   w;
	i32   h;
	f64   cpu;
} mask_gen_timing_t;

static void mask_gen_timing_done(void *data, f64 gpu_ms) {
	mask_gen_timing_t *p = data;
	fprintf(stderr, "[mask_gen] %s %dx%d cpu %.2f gpu %.2f ms\n", p->name, p->w, p->h, p->cpu, gpu_ms);
	free(p);
}

// Nothing to read: no thickness or AO map (their neutral white would read thick or open), a fill without an image
static bool mask_gen_no_input(mask_effect_t *e) {
	if (e->type == MASK_EFFECT_THICKNESS) {
		return project_get_image(mesh_maps_get_asset(MESH_MAP_THICKNESS)) == NULL;
	}
	if (e->type == MASK_EFFECT_AO) {
		return project_get_image(mesh_maps_get_asset(MESH_MAP_AO)) == NULL;
	}
	return mask_effect_fill_source(e) == MASK_FILL_IMAGE && mask_gen_image(e) == NULL;
}

// Generator e of m into dst: the mesh into scratch (R value, A coverage), dilated into dst. rect: the part of the mask
// dst shows (u0, v0, du, dv), NULL the whole. No pass may be open.
static void mask_gen_render(slot_layer_t *m, mask_effect_t *e, gpu_texture_t *scratch, gpu_texture_t *dst, f32 *rect) {
	slot_layer_t *owner = mask_gen_owner(m);
	if (util_mesh_udim_layer(owner) || mask_gen_no_input(e)) { // Not supported yet, or nothing to read: black
		_gpu_begin(dst, NULL, NULL, GPU_CLEAR_COLOR, 0xff000000, 0.0);
		gpu_end();
		return;
	}
	if (mask_gen_timing < 0) {
		mask_gen_timing = getenv("IRON_PAINT_TIMING") != NULL ? 1 : 0;
	}
	f64 t0 = 0.0;
	if (mask_gen_timing) {
		gpu_commit(NULL, NULL); // The work before goes apart
		t0 = iron_glsl_now_ms();
	}

	mesh_object_t *o  = mask_gen_object(m);
	mesh_data_t   *md = o->data;
	if (md != mask_gen_bounds_md || mask_gen_bounds_gen != (mask_gen_mesh_gen ^ mesh_maps_generation())) {
		mesh_data_calculate_aabb_min_max(md, &mask_gen_bounds_min, &mask_gen_bounds_max);
		mask_gen_bounds_md  = md;
		mask_gen_bounds_gen = mask_gen_mesh_gen ^ mesh_maps_generation();
	}
	// Bounds in the packed units the shader reads (pos / scale_pos)
	f32    f    = md->scale_pos > 0.0 ? 1.0 / md->scale_pos : 1.0;
	vec4_t lo   = (vec4_t){mask_gen_bounds_min.x * f, mask_gen_bounds_min.y * f, mask_gen_bounds_min.z * f, 0.0};
	f32    ex   = math_max((mask_gen_bounds_max.x - mask_gen_bounds_min.x) * f, 0.000001);
	f32    ey   = math_max((mask_gen_bounds_max.y - mask_gen_bounds_min.y) * f, 0.000001);
	f32    ez   = math_max((mask_gen_bounds_max.z - mask_gen_bounds_min.z) * f, 0.000001);
	f32    emax = math_max(ex, math_max(ey, ez));
	bool   tex1 = owner->uv_map == 1 && mesh_data_get_vertex_array(md, "tex1") != NULL;

	mat4_t n4 = mat4_transpose3(mat4_inv(o->base->transform->world));

	gpu_pipeline_t *pipe = mask_gen_pipeline(md);
	_gpu_begin(scratch, NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
	gpu_set_pipeline(pipe);
	gpu_set_mat3(mask_gen_n, mat3_set_from4(n4));
	gpu_set_float4(mask_gen_params0, e->params[0], e->params[1], e->params[2], e->params[3]);
	gpu_set_float4(mask_gen_params1, e->params[4], e->params[5], e->params[6], e->params[7]);
	gpu_set_float4(mask_gen_params2, e->params[8], e->params[9], e->params[10], e->params[11]);
	gpu_set_float4(mask_gen_params3, e->params[12], e->params[13], e->params[14], e->params[15]);
	gpu_set_float3(mask_gen_pos_min, lo.x, lo.y, lo.z);
	gpu_set_float3(mask_gen_pos_inv, 1.0 / ex, 1.0 / ey, 1.0 / ez);
	gpu_set_int(mask_gen_type, e->type);
	gpu_set_int(mask_gen_uv_map, tex1 ? 1 : 0);
	gpu_set_float(mask_gen_noise_inv, 1.0 / emax);
	if (rect != NULL) {
		gpu_set_float4(mask_gen_uv_rect, rect[0], rect[1], rect[2], rect[3]);
	}
	else {
		gpu_set_float4(mask_gen_uv_rect, 0.0, 0.0, 1.0, 1.0);
	}
	// Position, world normal and normal maps where registered (bits 1, 2, 4), the mesh's own otherwise
	i32 maps = 0;
	for (i32 i = 0; i < (i32)ARRAY_LENGTH(mask_gen_maps_read); ++i) {
		i32 type = mask_gen_maps_read[i];
		gpu_set_texture(i, mesh_maps_texture_model(type));
		if (i >= 3 && project_get_image(mesh_maps_get_asset(type)) != NULL) {
			maps |= 1 << (i - 3);
		}
	}
	i32            k     = (i32)ARRAY_LENGTH(mask_gen_maps_read);
	gpu_texture_t *image = mask_gen_image(e);
	gpu_set_texture(k, image != NULL ? image : render_path_empty_black());
	slot_layer_t *layer = mask_effect_builder_layer(e); // A Mask Builder's layer: its normal, occlusion and height (bit 8)
	gpu_set_texture(k + 1, layer != NULL ? layer->texpaint_nor : render_path_empty_black());
	gpu_set_texture(k + 2, layer != NULL ? layer->texpaint_pack : render_path_empty_black());
	maps |= layer != NULL ? 8 : 0;
	gpu_set_int(mask_gen_maps, maps);
	gpu_set_float(mask_gen_map_scale, f);
	gpu_set_vertex_buffer(md->_->vertex_buffer);
	gpu_set_index_buffer(md->_->index_buffer);
	gpu_draw();
	gpu_end();

	mask_gen_init_dilate();
	if (const_data_screen_aligned_vb == NULL) {
		const_data_create_screen_aligned_data();
	}
	_gpu_begin(dst, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(mask_gen_pipe_dilate);
	gpu_set_texture(0, scratch);
	gpu_set_float(mask_gen_radius_loc, (f32)mask_gen_radius());
	gpu_set_float2(mask_gen_size_loc, (f32)dst->width, (f32)dst->height);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
	mask_gen_render_count++;

	if (mask_gen_timing) {
		mask_gen_timing_t *p = malloc(sizeof(mask_gen_timing_t));
		p->name              = mask_effect_name(e->type);
		p->w                 = dst->width;
		p->h                 = dst->height;
		p->cpu               = iron_glsl_now_ms() - t0;
		gpu_commit(&mask_gen_timing_done, p);
	}
}

// Cache

static u64 mask_gen_epoch_current = 0; // Of the last mask_gen_update

// Renders the visible generators of m missing from its cache, frees the cached outputs no generator uses any more.
// They render through the stack's scratch (mask_stack_get_scratch). No pass may be open.
void mask_gen_update(slot_layer_t *m) {
	if (m->mask_gen_count == 0 && !mask_gen_has_generator(m)) {
		return;
	}
	u64 epoch              = mask_gen_epoch();
	mask_gen_epoch_current = epoch;
	i32             n      = mask_stack_count(m);
	gpu_texture_t **tex    = calloc(n + 1, sizeof(gpu_texture_t *));
	u64            *sig    = calloc(n + 1, sizeof(u64));
	mask_effect_t  *effs   = calloc(n + 1, sizeof(mask_effect_t));
	i32             count  = 0;
	u64             key    = 1469598103934665603ull;

	// Outputs still matching are kept
	for (i32 i = 0; i < n; ++i) {
		mask_effect_t e = mask_stack_get(m, i);
		if (!e.visible || !mask_effect_rendered_as(&e, &e)) {
			continue;
		}
		u64 s = mask_gen_signature(m, &e, epoch);
		key   = hash_fnv1a(key, &s, sizeof(s));
		bool dup = false;
		for (i32 k = 0; k < count; ++k) {
			dup = dup || sig[k] == s;
		}
		if (dup) { // Same settings twice, one output
			continue;
		}
		tex[count]  = NULL;
		sig[count]  = s;
		effs[count] = e;
		for (i32 k = 0; k < m->mask_gen_count; ++k) {
			if (m->mask_gen_textures[k] != NULL && m->mask_gen_signatures[k] == s) {
				tex[count]               = m->mask_gen_textures[k];
				m->mask_gen_textures[k] = NULL;
				break;
			}
		}
		count++;
	}

	// The others render, into an unused output of the same size when there is one
	i32 w = m->texpaint->width;
	i32 h = m->texpaint->height;
	for (i32 i = 0; i < count; ++i) {
		if (tex[i] != NULL) {
			continue;
		}
		for (i32 k = 0; k < m->mask_gen_count; ++k) {
			gpu_texture_t *t = m->mask_gen_textures[k];
			if (t != NULL && (i32)t->width == w && (i32)t->height == h) {
				tex[i]                   = t;
				m->mask_gen_textures[k] = NULL;
				break;
			}
		}
		if (tex[i] == NULL) {
			tex[i] = gpu_create_render_target(w, h, GPU_TEXTURE_FORMAT_R8);
		}
		mask_gen_render(m, &effs[i], mask_stack_get_scratch(w, h), tex[i], NULL);
	}

	mask_gen_free(m);
	m->mask_gen_textures   = tex;
	m->mask_gen_signatures = sig;
	m->mask_gen_count      = count;
	m->mask_gen_key        = key;
	free(effs);
}

// Cached output of generator e of m (after mask_gen_update), black when there is none
gpu_texture_t *mask_gen_texture(slot_layer_t *m, mask_effect_t e) {
	u64 s = mask_gen_signature(m, &e, mask_gen_epoch_current);
	for (i32 k = 0; k < m->mask_gen_count; ++k) {
		if (m->mask_gen_signatures[k] == s && m->mask_gen_textures[k] != NULL) {
			return m->mask_gen_textures[k];
		}
	}
	return render_path_empty_black();
}

// Export (io/export_tiled.c): generator e of m rendered for a part of the mask (rect: u0, v0, du, dv) into dst (R8),
// scratch an RGBA32 target of dst's size. Not cached. No pass may be open.
void mask_gen_render_rect(slot_layer_t *m, mask_effect_t e, gpu_texture_t *scratch, gpu_texture_t *dst, f32 *rect) {
	mask_gen_render(m, &e, scratch, dst, rect);
}

// Frees the cached outputs of m, they render again on its next evaluation
void mask_gen_free(slot_layer_t *m) {
	for (i32 k = 0; k < m->mask_gen_count; ++k) {
		if (m->mask_gen_textures[k] != NULL) {
			gpu_delete_texture(m->mask_gen_textures[k]);
		}
	}
	free(m->mask_gen_textures);
	free(m->mask_gen_signatures);
	m->mask_gen_textures   = NULL;
	m->mask_gen_signatures = NULL;
	m->mask_gen_count      = 0;
}

// iOS memory warning: every cache but the selected mask's goes, texmask keeps the result
void mask_gen_trim() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l != g_context->layer && l->mask_gen_count > 0) {
			mask_gen_free(l);
		}
	}
}
