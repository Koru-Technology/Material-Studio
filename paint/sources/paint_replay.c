#include "global.h"

// Replay of recorded strokes (stroke_rec.h): a TL_STROKE entry painted again into a paint target with the code that
// painted it live. The paint executor (paint_exec.c) runs the record's ops, its dabs go through the resolve pass in replay
// mode (positions from their quantized object space points), each draw with the paint state and camera it had: the paint
// shader is parsed again from the state and must hash as recorded, a geometry-only depth is drawn from the camera.
// The stroke starts from the target's own contents (S0, copied before), every scratch target has the target's size.

static i32 paint_replay_fallbacks = 0; // Replays that could not run (a shader that differs, a record this build does not read)
static i32 paint_replay_count     = 0;

gpu_texture_t *paint_replay_s0[3] = {NULL, NULL, NULL}; // The target before the stroke: what _texpaint_undo reads replaying
mat4_t         paint_replay_obj;                        // The paint target's object space when recorded
mat4_t         paint_replay_obj_inv;
paint_rec_cam_t paint_replay_cam; // The camera of the draws replayed now

// Paint shaders parsed for replays, by GLSL hash, kept for the next replays
#define PAINT_REPLAY_SCONS 8

static shader_context_t *paint_replay_scons[PAINT_REPLAY_SCONS];
static u64               paint_replay_scon_hashes[PAINT_REPLAY_SCONS];
static i32               paint_replay_scon_next = 0;

static void paint_replay_fail(char *why) {
	paint_replay_fallbacks++;
	iron_log("[replay] stroke not replayed: %s", why);
}

// The context a paint state was recorded in
static void paint_replay_state_apply(paint_state_t *s) {
	g_context->tool                        = s->tool;
	g_context->brush_blending              = s->blending;
	g_context->brush_paint                 = s->uv_type;
	g_context->picker_material_mask        = s->picker_material_mask;
	g_context->picker_color_mask           = s->picker_color_mask;
	g_context->materialid_picked           = s->materialid_picked;
	g_context->picker_color_mask_tolerance = s->picker_color_mask_tolerance;
	g_context->xray                        = s->xray;
	g_context->brush_directional           = s->directional;
	g_context->brush_rotation              = s->rotation;
	g_context->brush_camera_align          = s->camera_align;
	g_context->brush_tilt_shading          = s->tilt_shading;
	g_context->brush_roundness             = s->roundness;
	g_context->brush_angle_reject_dot      = s->angle_reject_dot;
	g_context->brush_angle                 = s->brush_angle;
	g_context->brush_nodes_angle           = s->nodes_angle;
	g_context->brush_jitter_angle          = s->jitter_angle;
	g_context->sym_x                       = (s->sym & 1) != 0;
	g_context->sym_y                       = (s->sym & 2) != 0;
	g_context->sym_z                       = (s->sym & 4) != 0;
	g_context->sym_radial                  = s->sym >> 8;
	g_context->sym_center                  = s->sym_center;
	g_context->sym_radial_axis             = s->sym_radial_axis;
	g_context->mask_value                  = s->mask_value;
	g_context->brush_scale                 = s->brush_scale;
	g_context->brush_nodes_scale           = s->nodes_scale;
	g_config->brush_depth_reject           = s->depth_reject;
	g_config->brush_angle_reject           = s->angle_reject;
	g_config->brush_alpha_discard          = s->alpha_discard;
	g_config->dilate_radius                = s->dilate_radius;
	g_project->linear_blending             = s->linear_blending;
	g_project->_->translucency             = s->translucency;
	make_paint_batching                    = s->batching;
	g_context->brush_mask_image            = paint_asset_texture(s->tip);
	g_context->brush_mask_image_is_alpha   = s->tip_alpha;
	g_context->brush_stencil_image         = paint_asset_texture(s->stencil);
	g_context->brush_stencil_image_is_alpha = s->stencil_alpha;
	g_context->brush_stencil_x             = s->stencil_transform[0];
	g_context->brush_stencil_y             = s->stencil_transform[1];
	g_context->brush_stencil_scale         = s->stencil_transform[2];
	g_context->brush_stencil_angle         = s->stencil_transform[3];
}

// A kept shader's images are still the textures loaded under their files: reloaded (an asset imported again), or false
// when one is gone (an asset deleted, a snapshot's image dropped with its project)
static bool paint_replay_scon_textures(shader_context_t *scon) {
	if (scon->bind_textures == NULL || scon->bind_textures->length == 0) {
		return true;
	}
	for (i32 i = 0; i < scon->bind_textures->length; ++i) {
		bind_tex_t *tex = scon->bind_textures->buffer[i];
		if (strcmp(tex->file, "") != 0 && (data_cached_textures == NULL || any_map_get(data_cached_textures, tex->file) == NULL)) {
			return false;
		}
	}
	shader_context_load_textures(scon);
	return true;
}

// The paint shader of a state, NULL when it does not hash as recorded. A stroke that read the material from its cache
// (addressed by texel of the live layer) replays at another size with the material evaluated: that shader is not the
// recorded one, the hash is not checked. Kept by GLSL and material snapshot: the same GLSL can read other images.
static shader_context_t *paint_replay_shader(paint_state_t *s) {
	bool verify = !(paint_replay_uncached && s->matcache);
	u64  key    = (verify ? s->glsl : s->glsl ^ 0x9e3779b97f4a7c15ull) ^ s->material.lo ^ (s->material.hi * 0xff51afd7ed558ccdull);
	for (i32 i = 0; i < PAINT_REPLAY_SCONS; ++i) {
		if (paint_replay_scons[i] != NULL && paint_replay_scon_hashes[i] == key) {
			if (paint_replay_scon_textures(paint_replay_scons[i])) {
				return paint_replay_scons[i];
			}
			make_material_free_paint_scon(paint_replay_scons[i]);
			paint_replay_scons[i] = NULL;
		}
	}
	bool uncached         = paint_replay_uncached;
	paint_replay_uncached = uncached || !s->matcache; // A material cache built since the stroke is not read
	paint_mat_snapshot_parse_begin(g_context->material);
	shader_context_t *scon = make_material_parse_paint_kept();
	paint_mat_snapshot_parse_end();
	paint_replay_uncached = uncached;
	if (scon == NULL || (verify && make_paint_glsl_hash(scon) != s->glsl)) {
		if (scon != NULL) {
			make_material_free_paint_scon(scon);
		}
		return NULL;
	}
	i32 i = paint_replay_scon_next;
	if (paint_replay_scons[i] != NULL) {
		make_material_free_paint_scon(paint_replay_scons[i]);
	}
	paint_replay_scons[i]       = scon;
	paint_replay_scon_hashes[i] = key;
	paint_replay_scon_next      = (i + 1) % PAINT_REPLAY_SCONS;
	return scon;
}

// The camera of a draw, and the geometry-only depth from it at the viewport's size then (depth reject reads it at the
// fragment's screen point): main_paint when it has that size, else a target of the replay's own
static void paint_replay_camera(paint_rec_cam_t *c) {
	scene_camera->v     = c->v;
	scene_camera->p     = c->p;
	scene_camera->vp    = mat4_mult_mat(c->v, c->p);
	scene_camera->frame = c->frame;
	paint_replay_cam    = *c;
	render_target_t *main = any_map_get(render_path_render_targets, "main_paint");
	if (main != NULL && main->_image != NULL && main->_image->width == c->w && main->_image->height == c->h) {
		paint_replay_depth = "main_paint";
		render_path_base_draw_paint_depth_into("gbuffer0", "main_paint");
		return;
	}
	render_path_ensure_target("replay_depth", c->w, c->h, "D32");
	render_path_ensure_target("replay_depth_color", c->w, c->h, "RGBA64"); // The depth pipeline's attachment
	paint_replay_depth = "replay_depth";
	render_path_base_draw_paint_depth_into("replay_depth_color", "replay_depth");
}

// UV islands of the layer for a target of w x h covering uv_rect (the dilate map of util_uv.c, at any size and rect)
static gpu_pipeline_t *paint_replay_dilate_pipe = NULL;

gpu_texture_t *paint_replay_dilate_map(i32 w, i32 h, const f32 *uv_rect) {
	static gpu_texture_t *map = NULL;
	util_texture_ensure_target(&map, w, h, GPU_TEXTURE_FORMAT_R8);
	if (paint_replay_dilate_pipe == NULL) {
		char *glsl = "#version 450\n\
layout(set = 0, binding = 0, std140) uniform Constants { layout(offset = 0) vec4 tile; } constants;\n\
layout(set = 0, binding = 1) uniform sampler sampler_linear;\n\
#ifdef VERT\n\
layout(location = 0) in vec4 in_pos;\n\
layout(location = 1) in vec2 in_nor;\n\
layout(location = 2) in vec2 in_tex;\n\
void main() {\n\
	vec2 t = (in_tex - constants.tile.xy) * constants.tile.zw;\n\
	gl_Position = vec4(t.x * 2.0 - 1.0, (1.0 - t.y) * 2.0 - 1.0, 0.0, 1.0);\n\
	float keep = in_pos.x + in_nor.x;\n\
}\n\
#endif\n\
#ifdef FRAG\n\
layout(location = 0) out float frag_color;\n\
void main() { frag_color = 1.0; }\n\
#endif\n";
		char *vs = NULL, *fs = NULL;
		i32   vs_size = 0, fs_size = 0;
		gpu_create_shaders_from_glsl(glsl, &vs, &fs, &vs_size, &fs_size);
		paint_replay_dilate_pipe                  = gpu_create_pipeline();
		paint_replay_dilate_pipe->vertex_shader   = gpu_create_shader_from_source(vs, vs_size, GPU_SHADER_TYPE_VERTEX);
		paint_replay_dilate_pipe->fragment_shader = gpu_create_shader_from_source(fs, fs_size, GPU_SHADER_TYPE_FRAGMENT);
		gpu_vertex_structure_t *vst               = ALLOC_INIT(gpu_vertex_structure_t, {0});
		gpu_vertex_structure_add(vst, "pos", GPU_VERTEX_DATA_I16_4X_NORM);
		gpu_vertex_structure_add(vst, "nor", GPU_VERTEX_DATA_I16_2X_NORM);
		gpu_vertex_structure_add(vst, "tex", GPU_VERTEX_DATA_I16_2X_NORM);
		paint_replay_dilate_pipe->input_layout        = vst;
		paint_replay_dilate_pipe->depth_write         = false;
		paint_replay_dilate_pipe->depth_mode          = GPU_COMPARE_MODE_ALWAYS;
		paint_replay_dilate_pipe->color_attachment[0] = GPU_TEXTURE_FORMAT_R8;
		gpu_pipeline_compile(paint_replay_dilate_pipe);
	}
	i32 mask = context_object_mask_used() ? slot_layer_get_object_mask(g_context->layer) : 0; // As util_uv_cache_dilate_map
	if (context_layer_filter_used()) {
		mask = g_context->layer_filter;
	}
	bool         use_merged = g_context->merged_object != NULL && (mask == 0 || util_mesh_udim_layer(g_context->layer));
	mesh_data_t *geom       = use_merged ? g_context->merged_object->data : g_context->paint_object->data;
	f32          du         = uv_rect[2] - uv_rect[0];
	f32          dv         = uv_rect[3] - uv_rect[1];
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	_gpu_begin(map, NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
	gpu_set_pipeline(paint_replay_dilate_pipe);
	gpu_set_float4(0, uv_rect[0], uv_rect[1], du > 0.0 ? 1.0 / du : 1.0, dv > 0.0 ? 1.0 / dv : 1.0);
	gpu_set_vertex_buffer(geom->_->vertex_buffer);
	gpu_set_index_buffer(geom->_->index_buffer);
	gpu_draw();
	gpu_end();
	return map;
}

// Render targets named as the layer's textures, pointing at t: the paint draws write t
typedef struct {
	char            *name;
	render_target_t *saved;
	render_target_t  rt;
} paint_replay_swap_t;

static void paint_replay_swap(paint_replay_swap_t *s, char *name, gpu_texture_t *tex) {
	s->name  = name;
	s->saved = any_map_get(render_path_render_targets, name);
	s->rt    = (render_target_t){.name = name, .width = tex->width, .height = tex->height, ._image = tex};
	any_map_set(render_path_render_targets, name, &s->rt);
}

static void paint_replay_s0_make(paint_target_t *t) {
	for (i32 i = 0; i < t->channels; ++i) {
		util_texture_ensure_target(&paint_replay_s0[i], t->w, t->h, t->out[i]->format);
	}
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	gpu_copy_regions_begin();
	for (i32 i = 0; i < t->channels; ++i) {
		gpu_copy_region(paint_replay_s0[i], 0, 0, t->out[i], 0, 0, t->w, t->h);
	}
	gpu_copy_regions_end();
}

// The record's ops in runs the executor flushes together: the same state and camera, room for their dabs
static bool paint_replay_ops(paint_rec_t *r) {
	i32 state = -1;
	i32 cam   = -1;
	i32 i     = 0;
	while (i < r->n_ops) {
		paint_rec_op_t *o = &r->ops[i];
		if (o->kind == PAINT_OP_DRAW && o->state != state) {
			paint_replay_state_apply(&r->states[o->state]);
			shader_context_t *scon = paint_replay_shader(&r->states[o->state]);
			if (scon == NULL) {
				paint_replay_fail("its paint shader is not the one recorded");
				return false;
			}
			make_material_paint_scon = scon;
			state                    = o->state;
		}
		if (o->kind == PAINT_OP_DRAW && o->cam != cam) {
			paint_replay_camera(&r->cams[o->cam]);
			cam = o->cam;
		}
		// The run: ops up to the next change of state or camera
		paint_exec_begin();
		i32 ndabs = 0;
		while (i < r->n_ops) {
			paint_rec_op_t *p = &r->ops[i];
			if (p->kind == PAINT_OP_DRAW && (p->state != state || p->cam != cam || ndabs + (i32)p->dab_count > PAINT_DAB_MAX)) {
				break;
			}
			if (paint_exec_pending_ops() >= 256) { // Room in the executor's ops
				break;
			}
			paint_exec_op_t op;
			memset(&op, 0, sizeof(op));
			op.kind       = p->kind;
			op.dilation   = (p->flags & 1) != 0;
			op.base       = (p->flags & 2) != 0;
			op.nor_pack   = (p->flags & 4) != 0;
			op.batched    = (p->flags & 8) != 0;
			op.brush_time = p->brush_time;
			if (p->kind == PAINT_OP_DRAW) {
				paint_rec_xform_t *x = &r->xforms[p->xform];
				op.dab_first         = ndabs;
				op.dab_count         = p->dab_count;
				op.down              = p->down;
				op.direction         = p->direction;
				op.pixel_snap_uv     = p->pixel_snap_uv;
				op.loc               = x->loc;
				op.rot               = x->rot;
				op.scale             = x->scale;
				memcpy(paint_exec_dab(ndabs), &r->dabs[(size_t)p->dab_first * PAINT_DAB_TEXELS * 4],
				       sizeof(f32) * PAINT_DAB_TEXELS * 4 * p->dab_count);
				ndabs += p->dab_count;
			}
			paint_exec_replay_push(&op, ndabs);
			i++;
		}
		paint_exec_end();
	}
	return true;
}

bool paint_replay_entry(timeline_t *tl, i32 index, paint_target_t *t, const paint_replay_opts_t *o) {
	if (tl == NULL || index < 0 || index >= tl->count || tl->entries[index].kind != TL_STROKE) {
		return false;
	}
	slot_layer_t *l = slot_layer_find_uid(tl->layer_uid);
	if (l == NULL) {
		paint_replay_fail("its layer is gone");
		return false;
	}
	u32 len  = 0;
	u8 *blob = hstore_get(tl->entries[index].payload, &len);
	paint_rec_t r;
	bool        ok = blob != NULL && paint_rec_decode(blob, len, &r);
	free(blob);
	if (!ok) {
		paint_replay_fail("its record is missing or of another version");
		return false;
	}
	if (r.n_states == 0 || hash128_is_none(r.states[0].material)) {
		paint_rec_free(&r);
		paint_replay_fail("no material");
		return false;
	}
	slot_material_t *m = paint_mat_snapshot_instantiate(r.states[0].material);
	if (m == NULL) {
		paint_rec_free(&r);
		paint_replay_fail("its material snapshot is missing");
		return false;
	}

	// Everything the replay changes, restored after
	context_t        saved_ctx    = *g_context;
	config_t         saved_cfg    = *g_config;
	bool             saved_linear = g_project->linear_blending;
	bool             saved_transl = g_project->_->translucency;
	shader_context_t *saved_scon  = make_material_paint_scon;
	bool             saved_flags[5] = {make_material_height_used, make_material_emis_used, make_material_subs_used, make_paint_batched, make_paint_resolved};
	bool             saved_batching = make_paint_batching;
	mat4_t           saved_v = scene_camera->v, saved_p = scene_camera->p, saved_vp = scene_camera->vp;
	i32              saved_frame = scene_camera->frame;
	vec4_t           saved_tile  = paint_exec_uv_tile;
	vec2_t           saved_res0  = paint_exec_res0;
	draw_saved_t     saved_draw  = draw_suspend();
	render_target_t *blend0      = any_map_get(render_path_render_targets, "texpaint_blend0");
	i32              live_w = blend0->width, live_h = blend0->height;

	paint_exec_isolate_begin(); // Apart from a scope open (a materialize from inside a stroke), its targets swapped in
	g_context->layer     = l;
	g_context->material  = m;
	g_context->pdirty    = 1;
	g_context->paint2d   = false;
	g_context->brush_mask_image    = NULL;
	g_context->brush_stencil_image = NULL;
	paint_replay_obj     = r.obj;
	paint_replay_obj_inv = mat4_inv(r.obj);
	paint_exec_res0      = (vec2_t){(f32)r.res_w, (f32)r.res_h};
	f32 du               = t->uv_rect[2] - t->uv_rect[0];
	f32 dv               = t->uv_rect[3] - t->uv_rect[1];
	paint_exec_uv_tile   = (vec4_t){t->uv_rect[0], t->uv_rect[1], du > 0.0 ? 1.0 / du : 1.0, dv > 0.0 ? 1.0 / dv : 1.0};

	paint_replay_swap_t swaps[4];
	i32                 nswaps = 0;
	// The map keeps its keys: the names render_path_paint.c keeps (render_path_paint_key), same translation unit
	paint_replay_swap(&swaps[nswaps++], render_path_paint_key("texpaint%d", l->id), t->out[0]);
	if (t->channels == 3) {
		paint_replay_swap(&swaps[nswaps++], render_path_paint_key("texpaint_nor%d", l->id), t->out[1]);
		paint_replay_swap(&swaps[nswaps++], render_path_paint_key("texpaint_pack%d", l->id), t->out[2]);
	}
	paint_replay_s0_make(t);
	render_path_paint_scratch_resize(t->w, t->h);
	bool exact = t->w == r.res_w && t->h == r.res_h && t->uv_rect[0] == 0.0 && t->uv_rect[1] == 0.0 && t->uv_rect[2] == 1.0 && t->uv_rect[3] == 1.0;
	paint_replay_uncached  = !exact;
	paint_replay_dilatemap = paint_replay_dilate_map(t->w, t->h, t->uv_rect);
	static gpu_texture_t *temp = NULL; // The dilation's copy, the target's size and format
	util_texture_ensure_target(&temp, t->w, t->h, t->out[0]->format);
	paint_replay_swap(&swaps[nswaps++], "temptex0", temp);
	if (o != NULL && o->mm_cap > 0) {
		mm_buffers_set_export_cap(o->mm_cap);
	}
	g_context->brush_blend_dirty = true;
	render_path_paint_clear_blend_now(); // The stroke's coverage starts empty

	paint_exec_replay = true;
	paint_mat_snapshot_replay_begin(m);
	i32 need          = mm_buffers_require(MM_BUFFERS_FULL);
	ok                = paint_replay_ops(&r);
	mm_buffers_require(need);
	paint_mat_snapshot_replay_end();
	paint_exec_replay = false;
	paint_exec_isolate_end();
	if (_render_path_current_image != NULL) {
		render_path_end();
	}

	for (i32 i = nswaps - 1; i >= 0; --i) {
		if (swaps[i].saved != NULL) {
			any_map_set(render_path_render_targets, swaps[i].name, swaps[i].saved);
		}
		else {
			any_map_set(render_path_render_targets, swaps[i].name, NULL); // Not there before
		}
	}
	if (o != NULL && o->mm_cap > 0) {
		mm_buffers_set_export_cap(0);
	}
	paint_replay_uncached  = false;
	paint_replay_depth     = NULL;
	paint_replay_dilatemap = NULL;
	render_path_paint_scratch_resize(live_w, live_h);
	*g_context                     = saved_ctx;
	*g_config                      = saved_cfg;
	g_project->linear_blending     = saved_linear;
	g_project->_->translucency     = saved_transl;
	make_material_paint_scon       = saved_scon;
	make_material_height_used      = saved_flags[0];
	make_material_emis_used        = saved_flags[1];
	make_material_subs_used        = saved_flags[2];
	make_paint_batched             = saved_flags[3];
	make_paint_resolved            = saved_flags[4];
	make_paint_batching            = saved_batching;
	scene_camera->v                = saved_v;
	scene_camera->p                = saved_p;
	scene_camera->vp               = saved_vp;
	scene_camera->frame            = saved_frame;
	paint_exec_uv_tile             = saved_tile;
	paint_exec_res0                = saved_res0;
	g_context->brush_blend_dirty   = true; // The live coverage was the replay's
	g_context->ddirty              = 2;
	draw_resume(saved_draw);
	paint_rec_free(&r);
	if (ok) {
		paint_replay_count++;
	}
	return ok;
}

i32 paint_replay_halo_px(const tl_entry_t *e, i32 res_w) {
	return (i32)ceilf(e->halo_uv * res_w);
}

// Scripts: replays that fell back, and that ran
i32 paint_replay_test_fallbacks() {
	return paint_replay_fallbacks;
}

i32 paint_replay_test_count() {
	return paint_replay_count;
}

// Scripts: replays entry index of the timeline of l into l itself, the state before it restored first
bool paint_replay_test_entry(slot_layer_t *l, i32 index) {
	timeline_t *tl = timeline_of(l);
	if (tl == NULL || !timeline_test_materialize(l, index)) {
		return false;
	}
	paint_target_t t;
	timeline_target_live(l, &t);
	return paint_replay_entry(tl, index, &t, NULL);
}

// Scripts: entry index of the timeline of src replayed into dst as it is (any size)
bool paint_replay_test_into(slot_layer_t *src, i32 index, slot_layer_t *dst) {
	paint_target_t t;
	timeline_target_live(dst, &t);
	return paint_replay_entry(timeline_of(src), index, &t, NULL);
}

// Scripts: the same into dst in n x n tiles, each replayed with halo texels around it, its middle copied into dst
bool paint_replay_test_tiles(slot_layer_t *src, i32 index, slot_layer_t *dst, i32 n, i32 halo) {
	paint_target_t live;
	timeline_target_live(dst, &live);
	i32             tw = live.w / n;
	i32             th = live.h / n;
	gpu_texture_t **tex = calloc(n * n * 3, sizeof(gpu_texture_t *));
	i32            *rect = calloc(n * n * 4, sizeof(i32));
	for (i32 k = 0; k < n * n; ++k) { // Every tile starts as dst is now
		i32 x0 = (k % n) * tw - halo, y0 = (k / n) * th - halo;
		i32 x1 = x0 + tw + halo * 2, y1 = y0 + th + halo * 2;
		x0 = x0 < 0 ? 0 : x0;
		y0 = y0 < 0 ? 0 : y0;
		x1 = x1 > live.w ? live.w : x1;
		y1 = y1 > live.h ? live.h : y1;
		i32 *r = &rect[k * 4];
		r[0] = x0, r[1] = y0, r[2] = x1 - x0, r[3] = y1 - y0;
		if (_render_path_current_image != NULL) {
			render_path_end();
		}
		gpu_copy_regions_begin();
		for (i32 c = 0; c < live.channels; ++c) {
			util_texture_ensure_target(&tex[k * 3 + c], r[2], r[3], live.out[c]->format);
			gpu_copy_region(tex[k * 3 + c], 0, 0, live.out[c], x0, y0, r[2], r[3]);
		}
		gpu_copy_regions_end();
	}
	bool ok = true;
	for (i32 k = 0; k < n * n && ok; ++k) {
		i32           *r = &rect[k * 4];
		paint_target_t t = live;
		t.w              = r[2];
		t.h              = r[3];
		t.uv_rect[0]     = r[0] / (f32)live.w;
		t.uv_rect[1]     = r[1] / (f32)live.h;
		t.uv_rect[2]     = (r[0] + r[2]) / (f32)live.w;
		t.uv_rect[3]     = (r[1] + r[3]) / (f32)live.h;
		for (i32 c = 0; c < live.channels; ++c) {
			t.out[c] = tex[k * 3 + c];
		}
		ok = paint_replay_entry(timeline_of(src), index, &t, NULL);
	}
	for (i32 k = 0; k < n * n && ok; ++k) { // The middles into dst
		i32 *r = &rect[k * 4];
		i32  x = (k % n) * tw, y = (k / n) * th;
		if (_render_path_current_image != NULL) {
			render_path_end();
		}
		gpu_copy_regions_begin();
		for (i32 c = 0; c < live.channels; ++c) {
			gpu_copy_region(live.out[c], x, y, tex[k * 3 + c], x - r[0], y - r[1], tw, th);
		}
		gpu_copy_regions_end();
	}
	for (i32 i = 0; i < n * n * 3; ++i) {
		util_texture_release_target(&tex[i]);
	}
	free(tex);
	free(rect);
	return ok;
}
