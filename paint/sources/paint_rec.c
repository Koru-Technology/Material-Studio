#include "global.h"

// Stroke recording (stroke_rec.h): what the paint executor (paint_exec.c) ran for a stroke, as it ran it: its ops, the
// paint state and camera of each draw, the symmetry transforms and the resolved dabs (read back without waiting). When
// the stroke ends its record is stored (HSTORE_STROKE) and its timeline entry becomes a TL_STROKE that paint_replay.c
// replays. Strokes it cannot replay yet stay TL_RASTER (their tile state).
//
// Blob, sections 4-byte aligned, structs of arrays (the store compresses it):
//   paint_rec_head_t, states (paint_state_t), cams (paint_rec_cam_t), xforms (paint_rec_xform_t),
//   ops: kind u8, flags u8, mm_tier u8 (padded), state u16, cam u16, xform u16 (padded), dab_first u32, dab_count u32,
//        brush_time f32, down f32, direction 4 f32, pixel_snap_uv 4 f32, decal_mask 4 f32, each an array of n_ops,
//   dabs: PAINT_DAB_TEXELS * 4 arrays of n_dabs f32, the resolved texels component by component.

#define PAINT_REC_MAGIC   0x31435250 // "PRC1"
#define PAINT_REC_VERSION 6

typedef struct paint_rec_head {
	u32 magic;
	u32 version;
	i32 res_w;
	i32 res_h;
	i32 channels;
	u32 n_states;
	u32 n_cams;
	u32 n_xforms;
	u32 n_ops;
	u32 n_dabs;
	u32 mm_tier;
	u32 dab_texels; // PAINT_DAB_TEXELS
	f32 obj[16];    // world_unpack of the paint target when recorded
} paint_rec_head_t;

static paint_rec_t paint_rec = {0};
static i32         paint_rec_stored = 0; // Strokes stored as TL_STROKE, for tests
static u64         paint_rec_keys[32];   // Material Maker buffers sampled below full by the stroke (paint_refine.c)
static i32         paint_rec_nkeys = 0;

#define PAINT_REC_GROW(arr, n, cap, type)                       \
	if ((n) == (cap)) {                                         \
		(cap) = (cap) == 0 ? 16 : (cap) * 2;                    \
		(arr) = realloc((arr), sizeof(type) * (cap));           \
	}

static void paint_rec_clear(paint_rec_t *r) {
	free(r->states);
	free(r->cams);
	free(r->xforms);
	free(r->ops);
	free(r->dabs);
	memset(r, 0, sizeof(paint_rec_t));
}

// A record being made for the layer painted now: the executor's ops are the stroke's
bool paint_rec_recording() {
	paint_rec_t *r = &paint_rec;
	return r->active && !paint_exec_replay && g_context->layer != NULL && g_context->layer->uid == r->layer_uid && !render_path_paint_live_layer_locked;
}

// A stroke is being recorded
bool paint_rec_active() {
	return paint_rec.active;
}

// The stroke paints something the replay does not do yet: it stays a raster entry
void paint_rec_foreign() {
	if (paint_rec_recording()) {
		paint_rec.replayable = false;
	}
}

static bool paint_rec_tool_replayable() {
	tool_type_t tool = g_context->tool;
	slot_layer_t *l  = g_context->layer;
	if (tool == TOOL_TYPE_CLONE && g_context->clone_layers == CLONE_LAYERS_VISIBLE) {
		return false; // Reads the other layers: a keyframe
	}
	return (tool == TOOL_TYPE_BRUSH || tool == TOOL_TYPE_ERASER || tool == TOOL_TYPE_FILL || tool == TOOL_TYPE_DECAL || tool == TOOL_TYPE_TEXT ||
	        tool == TOOL_TYPE_BLUR || tool == TOOL_TYPE_CLONE) &&
	       l->fill_material == NULL && !slot_layer_is_path(l) && !g_context->paint2d &&
	       !context_is_pixel_snap() && !g_context->pick_pos_nor_tex;
}

void paint_state_capture(paint_state_t *s, hash128_t material) {
	memset(s, 0, sizeof(paint_state_t));
	s->glsl                        = make_paint_glsl_hash(make_material_paint_scon);
	s->material                    = material;
	s->tool                        = g_context->tool;
	s->blending                    = g_context->brush_blending;
	s->uv_type                     = g_context->brush_paint;
	s->uv_map                      = g_context->layer->uv_map;
	s->mask                        = slot_layer_is_mask(g_context->layer);
	s->picker_material_mask        = g_context->picker_material_mask;
	s->picker_color_mask           = g_context->picker_color_mask;
	s->materialid_picked           = g_context->materialid_picked;
	s->picker_color_mask_tolerance = g_context->picker_color_mask_tolerance;
	s->xray                        = g_context->xray;
	s->directional                 = g_context->brush_directional;
	s->rotation                    = g_context->brush_rotation;
	s->camera_align                = g_context->brush_camera_align;
	s->tilt_shading                = g_context->brush_tilt_shading;
	s->roundness                   = g_context->brush_roundness;
	s->angle_reject_dot            = g_context->brush_angle_reject_dot;
	s->brush_angle                 = g_context->brush_angle;
	s->nodes_angle                 = g_context->brush_nodes_angle;
	s->jitter_angle                = g_context->brush_jitter_angle;
	s->sym             = (g_context->sym_x ? 1 : 0) | (g_context->sym_y ? 2 : 0) | (g_context->sym_z ? 4 : 0) | (g_context->sym_radial << 8);
	s->sym_center      = g_context->sym_center;
	s->sym_radial_axis = g_context->sym_radial_axis;
	s->mask_value      = g_context->mask_value;
	s->brush_scale     = g_context->brush_scale;
	s->nodes_scale     = g_context->brush_nodes_scale;
	s->depth_reject    = g_config->brush_depth_reject;
	s->angle_reject    = g_config->brush_angle_reject;
	s->alpha_discard   = g_config->brush_alpha_discard;
	s->linear_blending = g_project->linear_blending;
	s->translucency    = g_project->_->translucency;
	s->batching        = make_paint_batching;
	s->dilate_radius   = g_config->dilate_radius;
	s->matcache        = make_material_paint_scon != NULL && make_paint_scon_uses_matcache(make_material_paint_scon);
	s->tip             = paint_asset_intern(g_context->brush_mask_image);
	s->tip_alpha       = g_context->brush_mask_image_is_alpha;
	s->stencil         = paint_asset_intern(g_context->brush_stencil_image);
	s->stencil_alpha   = g_context->brush_stencil_image_is_alpha;
	s->stencil_transform[0] = g_context->brush_stencil_x;
	s->stencil_transform[1] = g_context->brush_stencil_y;
	s->stencil_transform[2] = g_context->brush_stencil_scale;
	s->stencil_transform[3] = g_context->brush_stencil_angle;
	bool picks         = render_path_paint_fill_picks();
	s->fill_type       = g_context->fill_type;
	s->fill_object     = picks ? array_index_of(g_project->_->paint_objects, render_path_paint_fill_object) : -1;
	s->fill_tri        = picks ? render_path_paint_fill_tri : -1;
	s->brush_scale_x   = g_context->brush_scale_x;
	s->blur_type       = g_context->blur_type;
	s->clone_delta[0]  = g_context->clone_delta_x;
	s->clone_delta[1]  = g_context->clone_delta_y;
	s->text            = g_context->tool == TOOL_TYPE_TEXT ? paint_asset_intern(g_context->text_tool_image) : HASH128_NONE;
}

static u16 paint_rec_state_index(paint_rec_t *r) {
	if (r->material_of != g_context->material) { // Once a stroke, unless its material changes
		hstore_unref(r->material);
		r->material    = paint_mat_snapshot_intern(g_context->material);
		r->material_of = g_context->material;
	}
	paint_state_t s;
	paint_state_capture(&s, r->material);
	if (s.glsl == 0) {
		r->replayable = false;
	}
	for (i32 i = r->n_states - 1; i >= 0; --i) {
		if (memcmp(&r->states[i], &s, sizeof(s)) == 0) {
			return i;
		}
	}
	PAINT_REC_GROW(r->states, r->n_states, r->c_states, paint_state_t);
	r->states[r->n_states] = s;
	return r->n_states++;
}

static u16 paint_rec_cam_index(paint_rec_t *r) {
	paint_rec_cam_t c;
	memset(&c, 0, sizeof(c));
	c.v     = scene_camera->v;
	c.p     = scene_camera->p;
	gpu_texture_t *depth = render_path_target_image("main_paint"); // The depth the draw reads (not the target drawn into)
	c.w                  = depth != NULL ? depth->width : render_path_current_w;
	c.h                  = depth != NULL ? depth->height : render_path_current_h;
	c.frame  = scene_camera->frame;
	c.aspect = sys_aspect();
	c.right = camera_object_right_world(scene_camera);
	c.up    = camera_object_up_world(scene_camera);
	if (r->n_cams > 0 && memcmp(&r->cams[r->n_cams - 1], &c, sizeof(c)) == 0) {
		return r->n_cams - 1;
	}
	PAINT_REC_GROW(r->cams, r->n_cams, r->c_cams, paint_rec_cam_t);
	r->cams[r->n_cams] = c;
	return r->n_cams++;
}

static u16 paint_rec_xform_index(paint_rec_t *r, paint_exec_op_t *op) {
	paint_rec_xform_t x = {.loc = op->loc, .rot = op->rot, .scale = op->scale};
	for (i32 i = r->n_xforms - 1; i >= 0; --i) {
		if (memcmp(&r->xforms[i], &x, sizeof(x)) == 0) {
			return i;
		}
	}
	PAINT_REC_GROW(r->xforms, r->n_xforms, r->c_xforms, paint_rec_xform_t);
	r->xforms[r->n_xforms] = x;
	return r->n_xforms++;
}

// history_paint(): the stroke starting is recorded, its entry is the last of the layer's timeline
void paint_rec_stroke_begin(history_step_t *step, slot_layer_t *l) {
	paint_rec_finish();
	timeline_t *tl = timeline_of(l);
	if (tl == NULL || tl->count == 0 || !paint_rec_tool_replayable()) {
		return;
	}
	paint_asset_stroke_begin();
	paint_rec_t *r = &paint_rec;
	r->active      = true;
	r->replayable  = true;
	r->layer_uid   = l->uid;
	r->tl_uid      = tl->uid;
	r->index       = tl->count - 1;
	r->serial      = tl->entries[r->index].step_serial;
	r->res_w       = l->texpaint->width;
	r->res_h       = l->texpaint->height;
	r->channels    = slot_layer_is_mask(l) ? 1 : 3;
	r->mm_tier     = MM_BUFFERS_FULL;
	paint_rec_nkeys = 0;
	vec4_t bmin, bsize;
	paint_exec_space(&r->obj, &r->obj_inv, &bmin, &bsize);
}

// Readback of the resolved rows of a flush
typedef struct {
	gpu_transfer_t *t;
	i32             base;  // First dab of the record
	i32             count;
	u32             token; // The record it belongs to
	bool            busy;
} paint_rec_readback_t;

#define PAINT_REC_READBACKS GPU_TRANSFER_INFLIGHT

static paint_rec_readback_t paint_rec_readbacks[PAINT_REC_READBACKS];
static u32                  paint_rec_token = 1;

static void paint_rec_readback_done(gpu_transfer_t *t, void *ud) {
	paint_rec_readback_t *rb = ud;
	paint_rec_t          *r  = &paint_rec;
	if (r->active && r->token == rb->token) {
		f32 *src = gpu_transfer_map(t, 0);
		memcpy(&r->dabs[rb->base * PAINT_DAB_TEXELS * 4], src, (size_t)rb->count * PAINT_DAB_TEXELS * 4 * sizeof(f32));
		r->pending--;
	}
	rb->busy = false;
}

// A flush of the executor resolved count dabs: reads them back for the record, the first dab's index in it (-1 not recorded)
i32 paint_rec_flush_dabs(i32 count) {
	if (!paint_rec_recording() || count == 0) {
		return -1;
	}
	paint_rec_t *r = &paint_rec;
	if (r->token == 0) {
		r->token = paint_rec_token++;
	}
	paint_rec_readback_t *rb = NULL;
	while (rb == NULL) {
		for (i32 i = 0; i < PAINT_REC_READBACKS; ++i) {
			if (!paint_rec_readbacks[i].busy) {
				rb = &paint_rec_readbacks[i];
				break;
			}
		}
		if (rb == NULL) {
			gpu_transfer_wait();
		}
	}
	while (r->n_dabs + count > r->c_dabs) {
		r->c_dabs = r->c_dabs == 0 ? 256 : r->c_dabs * 2;
		r->dabs   = realloc(r->dabs, sizeof(f32) * PAINT_DAB_TEXELS * 4 * r->c_dabs);
	}
	i32 base = r->n_dabs;
	r->n_dabs += count;
	if (rb->t == NULL) {
		rb->t = gpu_transfer_create(0);
	}
	i32 rows  = (count + PAINT_DAB_ROW - 1) / PAINT_DAB_ROW;
	i32 width = PAINT_DAB_ROW * PAINT_DAB_TEXELS;
	gpu_transfer_map(rb->t, (size_t)rows * width * 16);
	if (_render_path_current_image != NULL) {
		render_path_end();
	}
	gpu_copy_regions_begin();
	gpu_copy_region_to_transfer(rb->t, render_path_target_image("texpaint_dabs_resolved"), 0, 0, width, rows, 0);
	gpu_copy_regions_end();
	rb->base  = base;
	rb->count = count;
	rb->token = r->token;
	rb->busy  = true;
	r->pending++;
	gpu_transfer_commit(rb->t, paint_rec_readback_done, rb);
	return base;
}

// An op the executor runs, its dabs from base on in the record
void paint_rec_op(paint_exec_op_t *op, i32 base) {
	if (!paint_rec_recording()) {
		return;
	}
	paint_rec_t *r = &paint_rec;
	if (op->kind == PAINT_OP_DRAW && base < 0) { // Dabs not read back
		r->replayable = false;
		return;
	}
	PAINT_REC_GROW(r->ops, r->n_ops, r->c_ops, paint_rec_op_t);
	paint_rec_op_t *o = &r->ops[r->n_ops++];
	memset(o, 0, sizeof(paint_rec_op_t));
	o->kind       = op->kind;
	o->flags      = (op->dilation ? 1 : 0) | (op->base ? 2 : 0) | (op->nor_pack ? 4 : 0) | (op->batched ? 8 : 0);
	o->brush_time = op->brush_time;
	o->mm_tier    = MM_BUFFERS_FULL;
	if (op->kind == PAINT_OP_DRAW) {
		o->state         = paint_rec_state_index(r);
		o->cam           = paint_rec_cam_index(r);
		o->xform         = paint_rec_xform_index(r, op);
		o->dab_first     = base + op->dab_first;
		o->dab_count     = op->dab_count;
		o->down          = op->down;
		o->direction     = op->direction;
		o->pixel_snap_uv = op->pixel_snap_uv;
		o->decal_mask    = op->decal_mask;
	}
}

// After a draw ran: the lowest Material Maker tier it sampled, whether it took a blur snapshot
void paint_rec_op_done(paint_exec_op_t *op) {
	if (!paint_rec_recording() || op->kind != PAINT_OP_DRAW || paint_rec.n_ops == 0) {
		return;
	}
	if (render_path_paint_ref_taken) {
		paint_rec.ops[paint_rec.n_ops - 1].flags |= 16;
		render_path_paint_ref_taken = false;
	}
	u64 keys[64];
	i32 n    = mm_buffers_paint_keys(make_material_paint_scon, keys, 64);
	i32 tier = n > 0 ? mm_buffers_links_tier(keys, n) : MM_BUFFERS_FULL;
	tier     = tier < 0 ? 0 : tier;
	paint_rec_t *r                = &paint_rec;
	r->ops[r->n_ops - 1].mm_tier = tier;
	if (tier < r->mm_tier) {
		r->mm_tier = tier;
	}
	for (i32 i = 0; tier < MM_BUFFERS_FULL && i < n; ++i) {
		bool seen = false;
		for (i32 k = 0; k < paint_rec_nkeys && !seen; ++k) {
			seen = paint_rec_keys[k] == keys[i];
		}
		if (!seen && paint_rec_nkeys < 32) {
			paint_rec_keys[paint_rec_nkeys++] = keys[i];
		}
	}
}

// Encoded record, malloc'd
static u8 *paint_rec_encode(paint_rec_t *r, u32 *len) {
	u32 n  = r->n_ops;
	u32 nd = r->n_dabs;
	u32 a  = (n + 3) & ~3u;     // u8 arrays padded
	u32 a2 = (n * 2 + 3) & ~3u; // u16 arrays padded
	u32 size = sizeof(paint_rec_head_t) + r->n_states * sizeof(paint_state_t) + r->n_cams * sizeof(paint_rec_cam_t) +
	           r->n_xforms * sizeof(paint_rec_xform_t) + a * 3 + a2 * 3 + n * 4 * 4 + n * 4 * 12 + nd * PAINT_DAB_TEXELS * 4 * 4;
	u8 *b  = calloc(1, size);
	u8 *p  = b;
	paint_rec_head_t h = {.magic      = PAINT_REC_MAGIC,
	                      .version    = PAINT_REC_VERSION,
	                      .res_w      = r->res_w,
	                      .res_h      = r->res_h,
	                      .channels   = r->channels,
	                      .n_states   = r->n_states,
	                      .n_cams     = r->n_cams,
	                      .n_xforms   = r->n_xforms,
	                      .n_ops      = n,
	                      .n_dabs     = nd,
	                      .mm_tier    = r->mm_tier,
	                      .dab_texels = PAINT_DAB_TEXELS};
	memcpy(h.obj, &r->obj, sizeof(h.obj));
	memcpy(p, &h, sizeof(h));
	p += sizeof(h);
	memcpy(p, r->states, r->n_states * sizeof(paint_state_t));
	p += r->n_states * sizeof(paint_state_t);
	memcpy(p, r->cams, r->n_cams * sizeof(paint_rec_cam_t));
	p += r->n_cams * sizeof(paint_rec_cam_t);
	memcpy(p, r->xforms, r->n_xforms * sizeof(paint_rec_xform_t));
	p += r->n_xforms * sizeof(paint_rec_xform_t);
	for (u32 i = 0; i < n; ++i) {
		p[i]         = r->ops[i].kind;
		p[a + i]     = r->ops[i].flags;
		p[a * 2 + i] = r->ops[i].mm_tier;
	}
	p += a * 3;
	u16 *s = (u16 *)p;
	for (u32 i = 0; i < n; ++i) {
		s[i]                = r->ops[i].state;
		s[a2 / 2 + i]       = r->ops[i].cam;
		s[a2 / 2 * 2 + i]   = r->ops[i].xform;
	}
	p += a2 * 3;
	u32 *u = (u32 *)p;
	for (u32 i = 0; i < n; ++i) {
		u[i]     = r->ops[i].dab_first;
		u[n + i] = r->ops[i].dab_count;
	}
	p += n * 4 * 2;
	f32 *f = (f32 *)p;
	for (u32 i = 0; i < n; ++i) {
		f[i]     = r->ops[i].brush_time;
		f[n + i] = r->ops[i].down;
		f32 *d   = &r->ops[i].direction.x;
		f32 *ps  = &r->ops[i].pixel_snap_uv.x;
		f32 *dm  = &r->ops[i].decal_mask.x;
		for (i32 c = 0; c < 4; ++c) {
			f[n * (2 + c) + i]  = d[c];
			f[n * (6 + c) + i]  = ps[c];
			f[n * (10 + c) + i] = dm[c];
		}
	}
	p += n * 4 * 14;
	f = (f32 *)p;
	i32 comps = PAINT_DAB_TEXELS * 4;
	for (u32 k = 0; k < nd; ++k) {
		for (i32 c = 0; c < comps; ++c) {
			f[(size_t)c * nd + k] = r->dabs[(size_t)k * comps + c];
		}
	}
	p += (size_t)nd * comps * 4;
	*len = (u32)(p - b);
	return b;
}

// A record from its blob, false when it is not one this build reads
bool paint_rec_decode(const u8 *b, u32 len, paint_rec_t *r) {
	memset(r, 0, sizeof(paint_rec_t));
	paint_rec_head_t h;
	if (len < sizeof(h)) {
		return false;
	}
	memcpy(&h, b, sizeof(h));
	if (h.magic != PAINT_REC_MAGIC || h.version != PAINT_REC_VERSION || h.dab_texels != PAINT_DAB_TEXELS) {
		return false;
	}
	u32 n = h.n_ops, nd = h.n_dabs;
	u32 a = (n + 3) & ~3u, a2 = (n * 2 + 3) & ~3u;
	u64 need = sizeof(h) + (u64)h.n_states * sizeof(paint_state_t) + (u64)h.n_cams * sizeof(paint_rec_cam_t) + (u64)h.n_xforms * sizeof(paint_rec_xform_t) +
	           a * 3 + a2 * 3 + (u64)n * 4 * 16 + (u64)nd * PAINT_DAB_TEXELS * 4 * 4;
	if (need > len) {
		return false;
	}
	r->res_w    = h.res_w;
	r->res_h    = h.res_h;
	r->channels = h.channels;
	r->mm_tier  = h.mm_tier;
	memcpy(&r->obj, h.obj, sizeof(h.obj));
	const u8 *p = b + sizeof(h);
	r->n_states = r->c_states = h.n_states;
	r->states                 = malloc(sizeof(paint_state_t) * (h.n_states + 1));
	memcpy(r->states, p, h.n_states * sizeof(paint_state_t));
	p += h.n_states * sizeof(paint_state_t);
	r->n_cams = r->c_cams = h.n_cams;
	r->cams               = malloc(sizeof(paint_rec_cam_t) * (h.n_cams + 1));
	memcpy(r->cams, p, h.n_cams * sizeof(paint_rec_cam_t));
	p += h.n_cams * sizeof(paint_rec_cam_t);
	r->n_xforms = r->c_xforms = h.n_xforms;
	r->xforms                 = malloc(sizeof(paint_rec_xform_t) * (h.n_xforms + 1));
	memcpy(r->xforms, p, h.n_xforms * sizeof(paint_rec_xform_t));
	p += h.n_xforms * sizeof(paint_rec_xform_t);
	r->n_ops = r->c_ops = n;
	r->ops              = calloc(n + 1, sizeof(paint_rec_op_t));
	for (u32 i = 0; i < n; ++i) {
		r->ops[i].kind    = p[i];
		r->ops[i].flags   = p[a + i];
		r->ops[i].mm_tier = p[a * 2 + i];
	}
	p += a * 3;
	const u16 *s = (const u16 *)p;
	for (u32 i = 0; i < n; ++i) {
		r->ops[i].state = s[i];
		r->ops[i].cam   = s[a2 / 2 + i];
		r->ops[i].xform = s[a2 / 2 * 2 + i];
	}
	p += a2 * 3;
	const u32 *u = (const u32 *)p;
	for (u32 i = 0; i < n; ++i) {
		r->ops[i].dab_first = u[i];
		r->ops[i].dab_count = u[n + i];
	}
	p += n * 4 * 2;
	const f32 *f = (const f32 *)p;
	for (u32 i = 0; i < n; ++i) {
		r->ops[i].brush_time = f[i];
		r->ops[i].down       = f[n + i];
		f32 *d               = &r->ops[i].direction.x;
		f32 *ps              = &r->ops[i].pixel_snap_uv.x;
		f32 *dm              = &r->ops[i].decal_mask.x;
		for (i32 c = 0; c < 4; ++c) {
			d[c]  = f[n * (2 + c) + i];
			ps[c] = f[n * (6 + c) + i];
			dm[c] = f[n * (10 + c) + i];
		}
	}
	p += n * 4 * 14;
	f         = (const f32 *)p;
	i32 comps = PAINT_DAB_TEXELS * 4;
	r->n_dabs = r->c_dabs = nd;
	r->dabs               = malloc(sizeof(f32) * comps * (nd + 1));
	for (u32 k = 0; k < nd; ++k) {
		for (i32 c = 0; c < comps; ++c) {
			r->dabs[(size_t)k * comps + c] = f[(size_t)c * nd + k];
		}
	}
	for (u32 i = 0; i < n; ++i) { // Indices in range
		paint_rec_op_t *o = &r->ops[i];
		if (o->kind == PAINT_OP_DRAW &&
		    (o->state >= h.n_states || o->cam >= h.n_cams || o->xform >= h.n_xforms || (u64)o->dab_first + o->dab_count > nd)) {
			paint_rec_clear(r);
			return false;
		}
	}
	return true;
}

void paint_rec_free(paint_rec_t *r) {
	paint_rec_clear(r);
}

// Children of a stored record (hstore_set_children): the material snapshots of its states
static void paint_rec_children(const u8 *data, u32 len, hstore_visit_fn visit, void *ud) {
	paint_rec_head_t h;
	if (len < sizeof(h)) {
		return;
	}
	memcpy(&h, data, sizeof(h));
	if (h.magic != PAINT_REC_MAGIC || sizeof(h) + (u64)h.n_states * sizeof(paint_state_t) > len) {
		return;
	}
	for (u32 i = 0; i < h.n_states; ++i) {
		paint_state_t s;
		memcpy(&s, data + sizeof(h) + i * sizeof(paint_state_t), sizeof(s));
		if (!hash128_is_none(s.material)) {
			visit(s.material, ud);
		}
		if (!hash128_is_none(s.tip)) {
			visit(s.tip, ud);
		}
		if (!hash128_is_none(s.stencil)) {
			visit(s.stencil, ud);
		}
		if (!hash128_is_none(s.text)) {
			visit(s.text, ud);
		}
	}
}

// Strokes stored whose uv bounds wait for the state after them to be captured (timeline_finalize)
typedef struct {
	u32 tl;
	i32 index;
	u32 serial;
} paint_rec_bounds_t;

static paint_rec_bounds_t paint_rec_bounds[64];
static i32                paint_rec_bounds_count = 0;

// The uv box of the tiles a stored stroke changed, once both states are kept
static void paint_rec_bounds_update() {
	i32 kept = 0;
	for (i32 i = 0; i < paint_rec_bounds_count; ++i) {
		paint_rec_bounds_t *b  = &paint_rec_bounds[i];
		timeline_t         *tl = timeline_find(b->tl);
		if (tl == NULL || b->index >= tl->count || tl->entries[b->index].step_serial != b->serial || tl->entries[b->index].kind != TL_STROKE) {
			continue; // Gone
		}
		hash128_t before = timeline_state(tl, b->index);
		hash128_t after  = timeline_state(tl, b->index + 1);
		if (hash128_is_none(before) || hash128_is_none(after)) {
			paint_rec_bounds[kept++] = *b; // Not captured yet
			continue;
		}
		f32 box[4];
		if (timeline_tiles_changed_bounds(before, after, box)) {
			memcpy(tl->entries[b->index].uv_bounds, box, sizeof(box));
		}
	}
	paint_rec_bounds_count = kept;
}

// Scripts: the uv bounds of entry index of the timeline of l, component c
f32 paint_rec_test_bounds(slot_layer_t *l, i32 index, i32 c) {
	paint_rec_bounds_update();
	timeline_t *tl = timeline_of(l);
	return tl != NULL && index >= 0 && index < tl->count && c >= 0 && c < 4 ? tl->entries[index].uv_bounds[c] : -1.0;
}

// At startup: stored records own their material snapshots, snapshots their files (before a project's blobs are counted at its opening)
void paint_rec_init() {
	hstore_set_children(HSTORE_STROKE, paint_rec_children);
	paint_mat_snapshot_init();
	paint_refine_init();
}

// The stroke ended: stored, its entry a TL_STROKE when it replays
void paint_rec_finish() {
	paint_rec_t *r = &paint_rec;
	if (!r->active) {
		return;
	}
	if (r->pending > 0) {
		gpu_transfer_wait();
	}
	timeline_t *tl = timeline_find(r->tl_uid);
	bool        ok = r->replayable && r->pending == 0 && r->n_ops > 0 && tl != NULL && r->index < tl->count &&
	          tl->entries[r->index].step_serial == r->serial && tl->entries[r->index].kind == TL_RASTER;
	if (ok) {
		u32       len  = 0;
		u8       *blob = paint_rec_encode(r, &len);
		hash128_t h    = hstore_put(HSTORE_STROKE, blob, len); // Its states' snapshots are referenced by the blob
		free(blob);
		timeline_entry_upgrade(tl, r->index, TL_STROKE, h);
		tl_entry_t *e = &tl->entries[r->index];
		bool nonlocal = false; // Blur, smudge and clone read the layer anywhere
		for (u32 k = 0; k < r->n_states; ++k) {
			nonlocal = nonlocal || r->states[k].tool == TOOL_TYPE_BLUR || r->states[k].tool == TOOL_TYPE_CLONE;
		}
		e->flags = (nonlocal ? TL_FLAG_NONLOCAL : TL_FLAG_LOCAL) | TL_FLAG_UPSCALABLE;
		e->mm_tier    = r->mm_tier;
		if (r->mm_tier < MM_BUFFERS_FULL) {
			paint_refine_note(tl, r->index, paint_rec_keys, paint_rec_nkeys); // Sharpens by replay once they are full
		}
		// Reads around what it writes: dilation's neighbours (blur's kernel, once replayed)
		i32 reach  = r->n_states > 0 ? r->states[0].dilate_radius : 0;
		e->halo_uv = (f32)(reach + 1) / (f32)r->res_w;
		paint_rec_stored++;
		if (paint_rec_bounds_count == 64) {
			paint_rec_bounds_update();
		}
		if (paint_rec_bounds_count < 64) {
			paint_rec_bounds[paint_rec_bounds_count++] = (paint_rec_bounds_t){.tl = r->tl_uid, .index = r->index, .serial = r->serial};
		}
	}
	hstore_unref(r->material); // The blob holds it now
	paint_rec_clear(r);
}

// Before undo, save, export: the readbacks of the stroke land, a stroke that ended is stored
void paint_rec_flush() {
	paint_rec_bounds_update();
	if (paint_rec.active && paint_rec.pending > 0) {
		gpu_transfer_wait();
	}
	if (paint_rec.active && g_context->brush_time == 0.0 && !render_path_paint_tail_pending) {
		paint_rec_finish();
	}
}

// Scripts: strokes stored as TL_STROKE so far
i32 paint_rec_test_stored() {
	return paint_rec_stored;
}
