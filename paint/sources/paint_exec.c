#include "global.h"

// The paint executor: the GPU work of painting (draws, dilation, the stroke coverage clear, the clone source) as ops,
// executed in the order the stroke model emits them. With a paint shader reading resolved dabs (brush tools,
// make_paint_resolved), a draw keeps the brush state it was emitted with in its dabs and its op, the ops of a frame wait
// for the end of the outermost paint_exec_begin / paint_exec_end, one pass resolves every dab of them to world space
// (render/make_dab_resolve.c), then they draw. The brush uniforms (uniforms.c) read the op being executed.
// Otherwise every op runs at once, as painting did before.
//
// The stroke model emits into the open scope (render_path_paint_commands_paint_stroke and the other _stroke entries).
// Work outside it (a fill layer, a fill rendered for a replay or an export tile, bakes, paths, sharpening, replays) runs
// isolated (paint_exec_isolate_begin / end, which the public render_path_paint_commands_paint, _dilate and _clear_blend
// do): at once, against the targets and context its caller set, never part of a recorded stroke, whether a scope is
// open or not. Ops of the open scope waiting for its end run first when they paint the same targets with the same
// context, else after it (they are set aside: the caller swapped targets in, and swaps them back before the scope ends).

#define PAINT_EXEC_OPS_MAX 512
#define PAINT_EXEC_LEVELS  4 // Isolations nested at most

static paint_exec_op_t  paint_exec_ops0[PAINT_EXEC_OPS_MAX];
static f32              paint_exec_in0[PAINT_DAB_MAX * PAINT_DAB_TEXELS * 4];
static paint_exec_op_t *paint_exec_ops  = paint_exec_ops0;
static f32             *paint_exec_in   = paint_exec_in0;
static i32              paint_exec_nops = 0;
static i32              paint_exec_ndabs       = 0;
static i32              paint_exec_batch_first = -1; // Dabs of the batch being collected, [first, ndabs)
static i32              paint_exec_depth       = 0;
static paint_exec_op_t *paint_exec_op          = NULL;
static gpu_texture_t   *paint_exec_in_tex      = NULL;
static i32              paint_exec_resolved_n  = 0; // Dabs of paint_exec_in_tex resolved last
static bool             paint_exec_flushing    = false;
static i32              paint_exec_isolated    = 0;

// What the ops waiting paint into and with: an isolated draw with the same runs after them
typedef struct {
	void *layer;
	void *material;
	void *scon;
	void *object;
	void *rt[3];
	i32   tool;
} paint_exec_ctx_t;

static paint_exec_ctx_t paint_exec_queue_ctx;

static paint_exec_ctx_t paint_exec_ctx_now() {
	paint_exec_ctx_t c;
	memset(&c, 0, sizeof(c));
	slot_layer_t *l = g_context->layer;
	c.layer         = l;
	c.material      = g_context->material;
	c.scon          = make_material_paint_scon;
	c.object        = g_context->paint_object;
	c.tool          = g_context->tool;
	if (l != NULL) {
		c.rt[0] = any_map_get(render_path_render_targets, string_tmp("texpaint%d", l->id));
		c.rt[1] = any_map_get(render_path_render_targets, string_tmp("texpaint_nor%d", l->id));
		c.rt[2] = any_map_get(render_path_render_targets, string_tmp("texpaint_pack%d", l->id));
	}
	return c;
}

bool paint_exec_on() {
	return render_path_paint_resolved();
}

paint_exec_op_t *paint_exec_current() {
	return paint_exec_op;
}

f32 *paint_exec_dab(i32 k) {
	return &paint_exec_in[k * PAINT_DAB_TEXELS * 4];
}

gpu_texture_t *paint_exec_dabs_in() {
	return paint_exec_in_tex;
}

// Object space of the paint target: positions in its world_unpack space, where the mesh is in -1 to 1 (positions are
// packed as normalized shorts), grown by 1%
void paint_exec_space(mat4_t *obj, mat4_t *obj_inv, vec4_t *bmin, vec4_t *bsize) {
	mesh_object_t *o = render_path_paint_target();
	if (o == NULL) {
		o = g_context->paint_object;
	}
	*obj     = paint_exec_replay ? paint_replay_obj : o->base->transform->world_unpack; // Replaying: as recorded
	*obj_inv = paint_exec_replay ? paint_replay_obj_inv : mat4_inv(*obj);
	*bmin    = (vec4_t){-1.01, -1.01, -1.01, 0.0};
	*bsize   = (vec4_t){2.02, 2.02, 2.02, 0.0};
}

// IRON_PAINT_TIMING: the GPU time of each flush apart from the rest of the frame, logged as render_path_paint.c logs
// the draws of the old path
static i32 paint_exec_timing = -1;

typedef struct {
	f64 t;
	f64 cpu;
	i32 dabs;
} paint_exec_timing_t;

static void paint_exec_timing_done(void *data, f64 gpu_ms) {
	paint_exec_timing_t *p = data;
	fprintf(stderr, "[paint] t %.1f dabs %d cpu %.2f gpu %.2f ms\n", p->t, p->dabs, p->cpu, gpu_ms);
	free(p);
}

static void paint_exec_run(paint_exec_op_t *op, i32 rec_base) {
	bool rec = !paint_exec_replay && paint_exec_isolated == 0;
	if (rec) {
		paint_rec_op(op, rec_base);
	}
	f64 brush_time        = g_context->brush_time;
	g_context->brush_time = op->brush_time;
	if (op->kind == PAINT_OP_DRAW) {
		transform_t *t       = g_context->paint_object->base->transform;
		vec4_t       loc0    = t->loc;
		quat_t       rot0    = t->rot;
		vec4_t       scale0  = t->scale;
		bool         moved   = memcmp(&loc0, &op->loc, sizeof(vec4_t)) != 0 || memcmp(&rot0, &op->rot, sizeof(quat_t)) != 0 ||
		                 memcmp(&scale0, &op->scale, sizeof(vec4_t)) != 0;
		if (moved) { // A symmetry copy
			t->loc   = op->loc;
			t->rot   = op->rot;
			t->scale = op->scale;
			transform_build_matrix(t);
		}
		paint_exec_op               = op;
		render_path_paint_ref_taken = false;
		if (paint_exec_replay && op->ref_snapshot) {
			render_path_paint_ref_invalidate(); // Took a blur snapshot live: again
		}
		render_path_paint_commands_paint_now(op->dilation);
		if (rec) {
			paint_rec_op_done(op);
		}
		paint_exec_op = NULL;
		if (moved) {
			t->loc   = loc0;
			t->rot   = rot0;
			t->scale = scale0;
			transform_build_matrix(t);
		}
	}
	else if (op->kind == PAINT_OP_DILATE) {
		render_path_paint_dilate_now(op->base, op->nor_pack);
	}
	else if (op->kind == PAINT_OP_DILATE_AFTER) {
		render_path_paint_dilate_after_paint_now();
	}
	else if (op->kind == PAINT_OP_CLEAR_BLEND) {
		render_path_paint_clear_blend_now();
	}
	else if (op->kind == PAINT_OP_CLONE_FLATTEN) {
		render_path_paint_clone_snapshot();
	}
	g_context->brush_time = brush_time;
}

// Resolves the dabs of the ops so far and executes them. Dabs of a batch still being collected stay.
static void paint_exec_flush() {
	if (paint_exec_nops == 0) {
		return;
	}
	i32 ndabs = paint_exec_batch_first >= 0 ? paint_exec_batch_first : paint_exec_ndabs;
	if (paint_exec_timing < 0) {
		paint_exec_timing = getenv("IRON_PAINT_TIMING") != NULL ? 1 : 0;
	}
	paint_exec_timing_t *p = NULL;
	if (paint_exec_timing) {
		p       = malloc(sizeof(paint_exec_timing_t));
		p->t    = iron_glsl_now_ms();
		p->dabs = ndabs;
		if (_render_path_current_image != NULL) {
			render_path_end();
		}
		if (!gpu_in_use) {
			gpu_commit(NULL, NULL);
		}
	}

	if (ndabs > 0) {
		if (paint_exec_in_tex != NULL) {
			gpu_delete_texture(paint_exec_in_tex); // Freed after the frame
		}
		i32      rows = (ndabs + PAINT_DAB_ROW - 1) / PAINT_DAB_ROW;
		buffer_t b    = {.buffer = (u8 *)paint_exec_in, .length = rows * PAINT_DAB_ROW * PAINT_DAB_TEXELS * 4 * sizeof(f32)};
		paint_exec_in_tex = gpu_create_texture_from_bytes(&b, PAINT_DAB_ROW * PAINT_DAB_TEXELS, rows, GPU_TEXTURE_FORMAT_RGBA128);
		make_dab_resolve_run(ndabs);
		paint_exec_resolved_n = ndabs;
	}
	i32 rec_base = paint_exec_replay || paint_exec_isolated > 0 ? -1 : paint_rec_flush_dabs(ndabs); // The stroke's record reads them back

	i32              nops   = paint_exec_nops;
	paint_exec_op_t *ops    = paint_exec_ops; // An isolation while they execute has buffers of its own
	bool             nested = paint_exec_flushing;
	paint_exec_nops         = 0; // Ops emitted while these execute (none should) run at once
	paint_exec_flushing     = true;
	for (i32 i = 0; i < nops; ++i) {
		paint_exec_run(&ops[i], rec_base);
	}
	paint_exec_flushing = nested;

	// The batch being collected moves to the front
	if (paint_exec_batch_first >= 0) {
		i32 n = paint_exec_ndabs - paint_exec_batch_first;
		memmove(paint_exec_in, paint_exec_dab(paint_exec_batch_first), n * PAINT_DAB_TEXELS * 4 * sizeof(f32));
		paint_exec_batch_first = 0;
		paint_exec_ndabs       = n;
	}
	else {
		paint_exec_ndabs = 0;
	}

	if (p != NULL) {
		if (_render_path_current_image != NULL) {
			render_path_end();
		}
		p->cpu = iron_glsl_now_ms() - p->t;
		if (gpu_in_use) {
			p->cpu = -p->cpu;
			paint_exec_timing_done(p, -1.0);
		}
		else {
			gpu_commit(&paint_exec_timing_done, p);
		}
	}
}

void paint_exec_begin() {
	paint_exec_depth++;
}

void paint_exec_end() {
	if (paint_exec_depth > 0) {
		paint_exec_depth--;
	}
	if (paint_exec_depth == 0) {
		paint_exec_flush();
	}
}

static paint_exec_op_t *paint_exec_push(i32 kind) {
	if (paint_exec_nops == PAINT_EXEC_OPS_MAX) {
		paint_exec_flush();
	}
	if (paint_exec_nops == 0) {
		paint_exec_queue_ctx = paint_exec_ctx_now();
	}
	paint_exec_op_t *op = &paint_exec_ops[paint_exec_nops++];
	memset(op, 0, sizeof(paint_exec_op_t));
	op->kind       = kind;
	op->brush_time = g_context->brush_time;
	return op;
}

// Room for n more dabs
static void paint_exec_reserve(i32 n) {
	if (paint_exec_ndabs + n > PAINT_DAB_MAX) {
		paint_exec_flush();
	}
}

// A dab from the brush state, as the uniforms of a single draw see it
static i32 paint_exec_dab_add(f32 x, f32 y, f32 last_x, f32 last_y, f32 mode) {
	f32 *d = paint_exec_dab(paint_exec_ndabs);
	memset(d, 0, PAINT_DAB_TEXELS * 4 * sizeof(f32));
	vec2_t angle = uniforms_brush_angle();
	vec2_t dab   = uniforms_brush_dab_angle();
	vec2_t tilt  = util_brush_tilt_shape();
	d[0]         = x;
	d[1]         = y;
	d[2]         = last_x;
	d[3]         = last_y;
	d[4]         = mode;
	d[16]        = x; // t4, as the pass writes it: replaying, t0 holds the position
	d[17]        = y;
	d[18]        = last_x;
	d[19]        = last_y;
	d[12]        = uniforms_brush_radius();
	d[13]        = uniforms_brush_opacity();
	d[14]        = uniforms_brush_hardness();
	d[15]        = uniforms_brush_flow();
	d[24]        = angle.x * dab.x - angle.y * dab.y; // Both rotations of the mask
	d[25]        = angle.x * dab.y + angle.y * dab.x;
	d[26]        = tilt.x;
	d[27]        = tilt.y;
	d[28]        = angle.x;
	d[29]        = angle.y;
	d[30]        = dab.x;
	d[31]        = dab.y;
	return paint_exec_ndabs++;
}

// The direction of a directional brush once per dab, the symmetry copies of a dab share it
static vec4_t paint_exec_last_direction;
static f32    paint_exec_last_direction_key[4] = {-1.0, -1.0, -1.0, -1.0};

static vec4_t paint_exec_direction() {
	if (!g_context->brush_directional) {
		return (vec4_t){1.0, 0.0, 1.0, 1.0};
	}
	f32 key[4] = {g_context->paint_vec.x, g_context->paint_vec.y, g_context->last_paint_vec_x, g_context->last_paint_vec_y};
	if (memcmp(key, paint_exec_last_direction_key, sizeof(key)) == 0) {
		return paint_exec_last_direction;
	}
	memcpy(paint_exec_last_direction_key, key, sizeof(key));
	paint_exec_last_direction = uniforms_brush_direction_now();
	return paint_exec_last_direction;
}

// The state of a draw besides its dabs
static void paint_exec_draw_state(paint_exec_op_t *op) {
	transform_t *t    = g_context->paint_object->base->transform;
	op->loc           = t->loc;
	op->rot           = t->rot;
	op->scale         = t->scale;
	op->down          = uniforms_input_brush().z;
	op->direction     = paint_exec_direction();
	op->pixel_snap_uv = uniforms_pixel_snap_uv();
	op->decal_mask    = context_is_decal() ? uniforms_decal_mask() : (vec4_t){0.0, 0.0, 0.0, 0.0};
}

// The particle tool: a draw a particle that hit the mesh this frame, where it hit and its radius in the op, the brush
// state in its dab. Dilates after the last one.
static void paint_exec_particles(bool dilation) {
	if (g_context->pdirty <= 0) {
		return;
	}
	i32 last = -1;
	for (i32 i = 0; i < 32; ++i) {
		if (g_context->particles[i].timer != NULL && g_context->particles[i].hit) {
			last = i;
		}
	}
	for (i32 i = 0; i <= last; ++i) {
		if (g_context->particles[i].timer == NULL || !g_context->particles[i].hit) {
			continue;
		}
		paint_exec_reserve(1);
		paint_exec_op_t *op = paint_exec_push(PAINT_OP_DRAW);
		op->dilation        = dilation && i == last;
		op->dab_first       = paint_exec_dab_add(0.0, 0.0, 0.0, 0.0, PAINT_DAB_MODE_BRUSH);
		op->dab_count       = 1;
		op->particle_hit    = (vec4_t){g_context->particles[i].hit_x, g_context->particles[i].hit_y, g_context->particles[i].hit_z, util_particle_radius(i)};
		op->particle_last   = (vec4_t){g_context->particles[i].hit_last_x, g_context->particles[i].hit_last_y, g_context->particles[i].hit_last_z, 0.0};
		paint_exec_draw_state(op);
	}
	if (paint_exec_depth == 0) {
		paint_exec_flush();
	}
}

void paint_exec_draw(bool dilation) {
	if (g_context->tool == TOOL_TYPE_PARTICLE && paint_exec_op == NULL) {
		paint_exec_particles(dilation);
		return;
	}
	if (render_path_paint_fill_pick && paint_exec_op == NULL && !paint_exec_replay && render_path_paint_fill_picks()) {
		render_path_paint_fill_pick_now(); // What a fill paints, with the live camera: recorded in its state
	}
	if (paint_exec_op != NULL || !paint_exec_on()) { // The old path, or a draw of an op being executed
		if (paint_exec_op == NULL && g_context->pdirty > 0 && paint_exec_isolated == 0) {
			paint_rec_foreign(); // Not replayable yet
		}
		paint_exec_flush();
		render_path_paint_commands_paint_now(dilation);
		return;
	}
	if (g_context->pdirty <= 0) { // As render_path_paint_commands_paint_now
		if (paint_exec_batch_first >= 0) {
			paint_exec_ndabs       = paint_exec_batch_first;
			paint_exec_batch_first = -1;
		}
		return;
	}
	bool batched = render_path_paint_batched();
	if (!(batched && paint_exec_batch_first >= 0)) {
		paint_exec_reserve(2);
	}
	paint_exec_op_t *op = paint_exec_push(PAINT_OP_DRAW);
	op->dilation        = dilation;
	op->batched         = batched;
	if (batched && paint_exec_batch_first >= 0) {
		op->dab_first          = paint_exec_batch_first;
		op->dab_count          = paint_exec_ndabs - paint_exec_batch_first;
		paint_exec_batch_first = -1;
	}
	else {
		vec4_t inp    = uniforms_input_brush();
		vec4_t last   = uniforms_input_brush_last();
		op->dab_first = paint_exec_dab_add(inp.x, inp.y, last.x, last.y, PAINT_DAB_MODE_BRUSH);
		op->dab_count = 1;
		if (context_is_decal()) { // Where the decal is anchored: frozen for the mask pass and camera align, as make_texcoord_run read it
			bool   fixed  = context_is_decal_mask_paint_pass() || context_is_brush_camera_align();
			f32    ax     = fixed ? g_context->decal_x : inp.x;
			f32    ay     = fixed ? g_context->decal_y : inp.y;
			paint_exec_dab_add(ax, ay, ax, ay, PAINT_DAB_MODE_ANCHOR);
			op->dab_count = 2;
		}
	}
	paint_exec_draw_state(op);
	if (paint_exec_depth == 0) {
		paint_exec_flush();
	}
}

// Replay: an op of a record, its dabs written to the input already, ndabs of them so far
void paint_exec_replay_push(paint_exec_op_t *op, i32 ndabs) {
	paint_exec_op_t *o = paint_exec_push(op->kind);
	*o                 = *op;
	paint_exec_ndabs   = ndabs;
}

i32 paint_exec_pending_ops() {
	return paint_exec_nops;
}

void paint_exec_batch_add() {
	if (paint_exec_batch_first >= 0 && paint_exec_ndabs - paint_exec_batch_first == 64) { // As many as a draw of the old path
		paint_exec_draw(false);
	}
	if (paint_exec_batch_first < 0) {
		paint_exec_reserve(64);
		paint_exec_batch_first = paint_exec_ndabs;
	}
	vec4_t inp  = uniforms_input_brush();
	vec4_t last = uniforms_input_brush_last();
	paint_exec_dab_add(inp.x, inp.y, last.x, last.y, PAINT_DAB_MODE_BRUSH);
}

void paint_exec_batch_flush(bool dilation) {
	if (paint_exec_batch_first < 0 || paint_exec_ndabs == paint_exec_batch_first) {
		paint_exec_batch_first = -1;
		return;
	}
	paint_exec_draw(dilation);
}

// Ops other than draws wait behind the draws emitted before them
static bool paint_exec_pending() { // Or a stroke is recorded: they are ops of the record
	return paint_exec_op == NULL && (paint_exec_nops > 0 || (paint_exec_isolated == 0 && paint_rec_recording()));
}

void paint_exec_dilate(bool base, bool nor_pack) {
	if (!paint_exec_pending()) {
		render_path_paint_dilate_now(base, nor_pack);
		return;
	}
	paint_exec_op_t *op = paint_exec_push(PAINT_OP_DILATE);
	op->base            = base;
	op->nor_pack        = nor_pack;	if (paint_exec_depth == 0) {
		paint_exec_flush();
	}
}

void paint_exec_dilate_after_paint() {
	if (!paint_exec_pending()) {
		render_path_paint_dilate_after_paint_now();
		return;
	}
	paint_exec_push(PAINT_OP_DILATE_AFTER);
	if (paint_exec_depth == 0) {
		paint_exec_flush();
	}
}

void paint_exec_clear_blend() {
	if (!paint_exec_pending()) {
		render_path_paint_clear_blend_now();
		return;
	}
	paint_exec_push(PAINT_OP_CLEAR_BLEND);
	if (paint_exec_depth == 0) {
		paint_exec_flush();
	}
}

void paint_exec_clone_flatten() {
	if (!paint_exec_pending()) {
		render_path_paint_clone_snapshot();
		return;
	}
	paint_exec_push(PAINT_OP_CLONE_FLATTEN);
	if (paint_exec_depth == 0) {
		paint_exec_flush();
	}
}

// Isolated work (see the top): the ops of the open scope run first when they paint as it does, else wait, set aside
typedef struct {
	paint_exec_op_t *ops;
	f32             *in;
	i32              nops;
	i32              ndabs;
	i32              batch_first;
	i32              depth;
	paint_exec_op_t *op;
	gpu_texture_t   *in_tex;
	i32              resolved_n;
	paint_exec_ctx_t ctx;
} paint_exec_level_t;

static paint_exec_level_t paint_exec_levels[PAINT_EXEC_LEVELS];
static paint_exec_op_t   *paint_exec_level_ops[PAINT_EXEC_LEVELS];
static f32               *paint_exec_level_in[PAINT_EXEC_LEVELS];
static i32                paint_exec_overflow = 0; // Isolations past PAINT_EXEC_LEVELS, run as they are

void paint_exec_isolate_begin() {
	if (paint_exec_isolated == PAINT_EXEC_LEVELS - 1) {
		paint_exec_overflow++;
		return;
	}
	if (paint_exec_op == NULL && !paint_exec_flushing && paint_exec_nops > 0) {
		paint_exec_ctx_t now = paint_exec_ctx_now();
		if (memcmp(&now, &paint_exec_queue_ctx, sizeof(now)) == 0) {
			paint_exec_flush(); // The same targets: in the order emitted
		}
	}
	paint_exec_level_t *s = &paint_exec_levels[paint_exec_isolated];
	*s                    = (paint_exec_level_t){.ops         = paint_exec_ops,
	                                             .in          = paint_exec_in,
	                                             .nops        = paint_exec_nops,
	                                             .ndabs       = paint_exec_ndabs,
	                                             .batch_first = paint_exec_batch_first,
	                                             .depth       = paint_exec_depth,
	                                             .op          = paint_exec_op,
	                                             .in_tex      = paint_exec_in_tex,
	                                             .resolved_n  = paint_exec_resolved_n,
	                                             .ctx         = paint_exec_queue_ctx};
	paint_exec_isolated++;
	i32 k = paint_exec_isolated;
	if (paint_exec_level_ops[k] == NULL) {
		paint_exec_level_ops[k] = malloc(sizeof(paint_exec_op_t) * PAINT_EXEC_OPS_MAX);
		paint_exec_level_in[k]  = malloc(sizeof(f32) * PAINT_DAB_MAX * PAINT_DAB_TEXELS * 4);
	}
	paint_exec_ops         = paint_exec_level_ops[k];
	paint_exec_in          = paint_exec_level_in[k];
	paint_exec_nops        = 0;
	paint_exec_ndabs       = 0;
	paint_exec_batch_first = -1;
	paint_exec_depth       = 0;
	paint_exec_op          = NULL;
	paint_exec_in_tex      = NULL;
	paint_exec_resolved_n  = 0;
}

void paint_exec_isolate_end() {
	if (paint_exec_overflow > 0) {
		paint_exec_overflow--;
		return;
	}
	if (paint_exec_isolated == 0) {
		return;
	}
	paint_exec_depth = 0;
	paint_exec_flush();
	bool resolved = paint_exec_in_tex != NULL;
	if (paint_exec_in_tex != NULL) {
		gpu_delete_texture(paint_exec_in_tex); // Freed after the frame
	}
	paint_exec_isolated--;
	paint_exec_level_t *s  = &paint_exec_levels[paint_exec_isolated];
	paint_exec_ops         = s->ops;
	paint_exec_in          = s->in;
	paint_exec_nops        = s->nops;
	paint_exec_ndabs       = s->ndabs;
	paint_exec_batch_first = s->batch_first;
	paint_exec_depth       = s->depth;
	paint_exec_op          = s->op;
	paint_exec_in_tex      = s->in_tex;
	paint_exec_resolved_n  = s->resolved_n;
	paint_exec_queue_ctx   = s->ctx;
	if (resolved && paint_exec_flushing && paint_exec_in_tex != NULL && paint_exec_resolved_n > 0) {
		make_dab_resolve_run(paint_exec_resolved_n); // Ops of a flush still executing read their dabs resolved
	}
}

// Scripts: a scope of the stroke model held open (1) or closed (0), as a stroke's frame holds it
void paint_exec_test_scope(i32 open) {
	if (open) {
		paint_exec_begin();
	}
	else {
		paint_exec_end();
	}
}

// Scripts: fill layer l rendered by layers_render_fill_rect at w x w (a replay or an export tile), the hashes of its
// base, normal and pack; "" when it did not render
char *paint_exec_test_fill_rect(slot_layer_t *l, i32 w) {
	static gpu_texture_t *out[3]     = {NULL, NULL, NULL};
	static gpu_texture_t *scratch[5] = {NULL, NULL, NULL, NULL, NULL};
	if (l == NULL || l->texpaint == NULL || l->texpaint_nor == NULL) {
		return "";
	}
	gpu_texture_t *src[3] = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
	for (i32 k = 0; k < 3; ++k) {
		util_texture_ensure_target(&out[k], w, w, src[k]->format);
		util_texture_ensure_target(&scratch[k], w, w, src[k]->format);
	}
	util_texture_ensure_target(&scratch[3], w, w, src[1]->format);
	util_texture_ensure_target(&scratch[4], w, w, src[2]->format);
	paint_target_t t = {.w = w, .h = w, .uv_rect = {0.0, 0.0, 1.0, 1.0}, .res_w = w, .res_h = w, .channels = 3, .out = {out[0], out[1], out[2]}};
	if (!layers_render_fill_rect(l, &t, scratch)) {
		return "";
	}
	return string("%s %s %s", script_texture_hash(out[0]), script_texture_hash(out[1]), script_texture_hash(out[2]));
}
