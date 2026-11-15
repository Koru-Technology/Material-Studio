#include "../global.h"
#include "../material_maker/mm.h"
#include <iron_profile.h>

// Material Maker buffers (mm_buffers.c): drawing the passes, the deferred queue stepped each frame, rendering a plan and
// resolving the texture links of material shaders. Included after mm_buffers_pipes.c.

// Drawing

static gpu_texture_t *mm_buffers_empty_texture(void) {
	if (mm_buffers_empty == NULL) {
		uint16_t zero[4 * 4 * 4] = {0};
		mm_buffers_empty         = calloc(1, sizeof(gpu_texture_t));
		gpu_texture_init_from_bytes(mm_buffers_empty, zero, 4, 4, GPU_TEXTURE_FORMAT_RGBA64, false);
	}
	return mm_buffers_empty;
}

// IRON_SHADER_TIMING: pixels drawn and the largest target of the render in progress
static double mm_buffers_pixels   = 0;
static i32    mm_buffers_max_size = 0;
static char  *mm_buffers_drawing  = NULL; // Name of the pass being drawn

// Rows y0 to y1 of the target: cleared with the first band, the others keep what is there
static void mm_buffers_draw(mm_buffers_pipe_t *pipe, gpu_texture_t *target, gpu_texture_t **inputs, i32 n, i32 size, i32 iteration, i32 y0, i32 y1) {
	gpu_texture_t *targets[1] = {target};
	gpu_begin(targets, 1, NULL, y0 == 0 ? GPU_CLEAR_COLOR : GPU_CLEAR_NONE, 0x00000000, 0.0f);
	if (y0 > 0 || y1 < size) {
		gpu_scissor(0, y0, size, y1 - y0);
	}
	if (pipe != NULL && pipe->ok && n <= GPU_MAX_TEXTURES) { // Did not compile: the texture stays cleared, as in Material Maker
		gpu_set_pipeline(&pipe->pipe);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		for (i32 k = 0; k < n; ++k) {
			gpu_set_texture(k, inputs[k]);
		}
		gpu_set_float4(0, (f32)size, (f32)iteration, 1.0f, 0.0f); // w: a run time 0.0 (mm_uniform_runtime_declaration)
		gpu_draw();
	}
	gpu_end();
	mm_buffers_stats[3]++;
	mm_buffers_pixels += (double)size * (y1 - y0);
	if (size > mm_buffers_max_size) {
		mm_buffers_max_size = size;
	}
	static int profile = -1; // Env IRON_BUFFERS_PROFILE: the GPU time of each draw (waits for each, slow)
	if (profile < 0) {
		profile = getenv("IRON_BUFFERS_PROFILE") != NULL;
	}
	if (profile) {
		double t0 = iron_time();
		gpu_execute_and_wait();
		iron_log("[buffers] pass %s: size %d, iteration %d, %d inputs, %.1f ms", mm_buffers_drawing != NULL ? mm_buffers_drawing : "?", size, iteration, n,
		         (iron_time() - t0) * 1000.0);
		return;
	}
	if (++mm_buffers_draws % MM_BUFFERS_FLUSH == 0 && !mm_buffers_stepping) { // A step's draws go with the frame
		gpu_execute_and_wait();
	}
}

static gpu_texture_t *mm_buffers_ensure(mm_buffers_recipe_t *r, i32 level);

static bool mm_buffers_owner_rendering(mm_buffers_recipe_t *r) {
	mm_buffers_recipe_t *o = r->in_loop ? mm_buffers_find(r->owner) : NULL;
	return o != NULL && o->rendering;
}

// An input's texture: a loop pass's or an iterate buffer's current one while it renders, others rendered when missing
// or below the level. NULL (an empty texture) when none: missing recipe, a loop pass not rendered yet in its loop.
static gpu_texture_t *mm_buffers_input(u64 key, i32 level) {
	mm_buffers_recipe_t *r = key != 0 ? mm_buffers_find(key) : NULL;
	if (r == NULL) {
		return NULL;
	}
	if (r->cur != NULL) {
		return r->cur;
	}
	if (r->rendering || mm_buffers_owner_rendering(r)) {
		return NULL;
	}
	return mm_buffers_ensure(r, level);
}

static i32 mm_buffers_inputs(i32 n, u64 *keys, gpu_texture_t **out, i32 level) {
	n = n > GPU_MAX_TEXTURES ? GPU_MAX_TEXTURES : n;
	for (i32 k = 0; k < n; ++k) {
		out[k] = mm_buffers_input(keys[k], level);
	}
	for (i32 k = 0; k < n; ++k) { // After: rendering an input may need the empty texture made
		out[k] = out[k] != NULL ? out[k] : mm_buffers_empty_texture();
	}
	return n;
}

// The keys a unit (a pass, or an iterate buffer with its loop passes) reads from outside it: its inputs, its loop
// inputs, its loop passes' inputs. Returns the count, the keys in *out (malloc'd).
static i32 mm_buffers_unit_deps(mm_buffers_recipe_t *u, u64 **out) {
	i32 max = u->textures + u->loop_textures;
	for (i32 j = 0; j < u->nloop; ++j) {
		mm_buffers_recipe_t *l = mm_buffers_find(u->loop[j]);
		max += l != NULL ? l->textures : 0;
	}
	u64 *d = malloc(sizeof(u64) * (max + 1));
	i32  n = 0;
	for (i32 s = -1; s <= u->nloop; ++s) {
		mm_buffers_recipe_t *r    = s < 0 ? u : s < u->nloop ? mm_buffers_find(u->loop[s]) : NULL;
		i32                  m    = s == u->nloop ? u->loop_textures : r != NULL ? r->textures : 0;
		u64                 *keys = s == u->nloop ? u->loop_inputs : r != NULL ? r->inputs : NULL;
		for (i32 k = 0; k < m && n < max; ++k) {
			mm_buffers_recipe_t *dr = keys[k] != 0 ? mm_buffers_find(keys[k]) : NULL;
			if (dr == NULL || dr == u || (dr->in_loop && dr->owner == u->key)) {
				continue;
			}
			d[n++] = keys[k];
		}
	}
	*out = d;
	return n;
}

// Everything the unit reads is full
static bool mm_buffers_deps_full(mm_buffers_recipe_t *u) {
	u64 *d    = NULL;
	i32  n    = mm_buffers_unit_deps(u, &d);
	bool full = true;
	for (i32 i = 0; i < n && full; ++i) {
		mm_buffers_recipe_t *r = mm_buffers_find(d[i]);
		full                   = r == NULL || mm_buffers_sat(r, MM_BUFFERS_FULL);
	}
	free(d);
	return full;
}

static i32  mm_buffers_landed = 0;     // Textures rendered by steps (redraws)
static bool mm_buffers_stale  = false; // A link resolved to a texture not full (or none): redraw when better ones land

// The texture becomes the recipe's, replacing the one it had
static void mm_buffers_set_tex(mm_buffers_recipe_t *r, gpu_texture_t *t, i32 size, i32 level) {
	mm_buffers_detach(t);
	if (r->tex != t) {
		mm_buffers_texture_free(r->tex);
	}
	r->tex           = t;
	r->tex_size      = size;
	r->tex_level     = level;
	r->used          = mm_buffers_gen;
	r->rendered      = mm_buffers_gen;
	r->rendered_call = mm_buffers_calls;
	mm_buffers_landed++;
}

// Image or text node: its pixels as RGBA8 unorm
static gpu_texture_t *mm_buffers_cpu_texture(mm_buffers_recipe_t *r) {
	gpu_texture_t *t   = calloc(1, sizeof(gpu_texture_t));
	u32            len = r->w * r->h * 4;
	buffer_t      *px  = r->raw ? r->pixels : lz4_decode(r->pixels, len);
	if (px == NULL) { // Corrupt: a cleared texture, as for a pass that does not compile
		u8 *zero = calloc(len, 1);
		gpu_texture_init_from_bytes(t, zero, r->w, r->h, GPU_TEXTURE_FORMAT_RGBA32, false);
		free(zero);
		return t;
	}
	gpu_texture_init_from_bytes(t, px->buffer, r->w, r->h, GPU_TEXTURE_FORMAT_RGBA32, false);
	if (px != r->pixels) {
		iron_delete_blob(px);
	}
	return t;
}

// GPU cost: ms per Mpx drawn, measured per unit (mm_buffers_learn), the average for units not measured yet
static double mm_buffers_cost_default = 25.0; // Material Maker passes measured 10 to 40 on the iPad, more for some

static void mm_buffers_commit(u64 key, double px, bool src, i32 size);

// A cost measured at a smaller size, drawn larger: as the square of the sizes, until a draw at that size measures it.
// Kernels in texels grow with the size (blurs, a 2D one as its square): measured at 128 and 512, a pass's 2048 draw cost
// 40 times its estimate on the iPad (425 ms of GPU in one frame). Its first band is small, its time corrects the rest.
static double mm_buffers_cost_scaled(double cost, i32 at, i32 size) {
	if (at > 0 && size > at) {
		double k = (double)size / at;
		cost *= k * k;
	}
	return cost;
}

static double mm_buffers_cost_at(mm_buffers_recipe_t *u, i32 size) {
	return u->cost > 0 ? mm_buffers_cost_scaled(u->cost, u->cost_at, size) : mm_buffers_cost_default;
}

// An iterate buffer's loop source draws (its iterations) cost their own: its first draw is often far cheaper, one cost
// for both made the steps underestimate the iterations (hundreds of ms of GPU in one frame on the iPad)
static double mm_buffers_cost_src_at(mm_buffers_recipe_t *u, i32 size) {
	return u->cost_src > 0 ? mm_buffers_cost_scaled(u->cost_src, u->cost_src_at, size) : mm_buffers_cost_at(u, size);
}

static void mm_buffers_job_cancel(mm_buffers_recipe_t *u) {
	if (!u->job) {
		return;
	}
	mm_buffers_release(u->job_t);
	mm_buffers_release(u->job_draw);
	u->job_t    = NULL;
	u->job_draw = NULL;
	for (i32 j = 0; j < u->nloop; ++j) {
		mm_buffers_recipe_t *l = mm_buffers_find(u->loop[j]);
		if (l != NULL && l->job_cur != NULL) {
			mm_buffers_release(l->job_cur);
			l->job_cur = NULL;
		}
	}
	u->job = false;
}

// A unit's render, at the level: its draws run by mm_buffers_job_run
static void mm_buffers_job_start(mm_buffers_recipe_t *u, i32 level) {
	mm_buffers_job_cancel(u);
	i32  size  = mm_buffers_level_size(u, level);
	bool exact = level >= MM_BUFFERS_FULL || (size == mm_buffers_full_size(u) && mm_buffers_deps_full(u));
	for (i32 j = 0; j < u->nloop && exact && level < MM_BUFFERS_FULL; ++j) {
		mm_buffers_recipe_t *l = mm_buffers_find(u->loop[j]);
		exact                  = l == NULL || mm_buffers_level_size(l, level) == mm_buffers_full_size(l);
	}
	u->job       = true;
	u->job_level = level;
	u->job_exact = exact;
	u->job_k     = 0;
	u->job_phase = 0;
	u->job_band  = 0;
	u->job_draw  = NULL;
	u->job_t     = NULL;
}

#define MM_BUFFERS_MIN_BAND 32 // Rows: a draw split over frames goes in bands of at least this

// Runs a unit's draws until all are done, or (budget_ms > 0) until their estimated GPU time (added to *spent_ms) reaches
// the budget: a draw that does not fit goes in bands of rows. An iterate buffer's loop passes and loop source read the
// previous iteration. True when done: the last draws' textures become the unit's (an iterate buffer's loop passes').
static bool mm_buffers_job_run(mm_buffers_recipe_t *u, double budget_ms, double *spent_ms) {
	i32                   level = u->job_level;
	mm_buffers_recipe_t **loop  = calloc(u->nloop + 1, sizeof(mm_buffers_recipe_t *));
	for (i32 j = 0; j < u->nloop; ++j) {
		loop[j] = mm_buffers_find(u->loop[j]);
		if (loop[j] != NULL) {
			loop[j]->cur = loop[j]->job_cur;
		}
	}
	bool done = false;
	gpu_texture_t *in[GPU_MAX_TEXTURES];
	u->rendering = true;
	while (true) {
		// The draw: the first one, a loop pass, or the loop source
		mm_buffers_recipe_t *r    = u;
		bool                 src  = false;
		i32                  size = mm_buffers_level_size(u, level);
		if (u->job_k > 0 && u->job_phase < u->nloop) {
			r = loop[u->job_phase];
			if (r == NULL) { // Gone: skipped
				u->job_phase++;
				continue;
			}
			size = mm_buffers_level_size(r, level);
		}
		else if (u->job_k > 0) {
			src = true;
			if (u->shrink) {
				size = u->job_k < 31 ? size >> u->job_k : 0;
				size = size < 4 ? 4 : size;
			}
		}
		i32    rows = size - u->job_band;
		double cost = src ? mm_buffers_cost_src_at(u, size) : mm_buffers_cost_at(r, size);
		if (budget_ms > 0) {
			double row_ms = cost * size / 1e6;
			double fit    = (budget_ms - *spent_ms) / row_ms;
			if (fit < rows) {
				i32 min = size < MM_BUFFERS_MIN_BAND ? size : MM_BUFFERS_MIN_BAND;
				if (fit < min && *spent_ms > 0) {
					break; // Next frame
				}
				rows = fit < min ? min : (i32)fit;
				rows = rows > size - u->job_band ? size - u->job_band : rows;
			}
		}
		if (u->job_draw == NULL) {
			u->job_draw = mm_buffers_target(mm_buffers_format(r), size);
		}
		u->cur         = u->job_k > 0 ? u->job_t : NULL;
		bool rendering = r->rendering;
		r->rendering   = true;
		i32 n = src ? mm_buffers_inputs(u->loop_textures, u->loop_inputs, in, level) : mm_buffers_inputs(r->textures, r->inputs, in, level);
		r->rendering   = rendering;
		if (mm_buffers_aborted) { // An input's render was cut short: this draw waits, the job continues later
			break;
		}
		mm_buffers_drawing = r->name;
		mm_buffers_draw(mm_buffers_pipe_of(r, src), u->job_draw, in, n, size, src ? u->job_k : 0, u->job_band, u->job_band + rows);
		*spent_ms += cost * size * rows / 1e6;
		if (mm_buffers_deadline > 0) { // On the spot, timed: the GPU caught up with every few draws
			static i32 timed = 0;
			if (++timed % 4 == 0) {
				gpu_execute_and_wait();
			}
			mm_buffers_aborted = iron_time() > mm_buffers_deadline;
		}
		if (budget_ms > 0 && (double)size * rows >= 0.05e6) { // A step: each draw in a command buffer of its own, timed, to
			mm_buffers_commit(r->key, (double)size * rows, src, size); // learn its cost (small ones go with the next)
		}
		u->job_band += rows;
		if (u->job_band < size) {
			continue;
		}
		// The draw is done
		gpu_texture_t *t = u->job_draw;
		u->job_draw      = NULL;
		u->job_band      = 0;
		if (u->job_k == 0) {
			u->job_t = t;
			u->job_k = 1;
			done     = !u->iterate || u->iterations < 1;
		}
		else if (!src) {
			if (r->cur != NULL) {
				mm_buffers_release(r->cur);
			}
			r->cur = t;
			u->job_phase++;
		}
		else {
			mm_buffers_release(u->job_t);
			u->job_t     = t;
			u->job_phase = 0;
			u->job_k++;
			done = u->job_k > u->iterations;
			if (!done && budget_ms > 0 && u->cost_src <= 0) { // Its cost not measured yet: one iteration a frame until it is
				*spent_ms = budget_ms;
				break;
			}
		}
		if (done || (budget_ms > 0 && *spent_ms >= budget_ms) || mm_buffers_aborted) {
			break;
		}
	}
	u->cur       = NULL;
	u->rendering = false;
	for (i32 j = 0; j < u->nloop; ++j) {
		if (loop[j] != NULL) {
			loop[j]->job_cur = loop[j]->cur;
			loop[j]->cur     = NULL;
		}
	}
	if (done) {
		i32 got = u->job_exact ? MM_BUFFERS_FULL : level < MM_BUFFERS_LOW ? MM_BUFFERS_TINY : MM_BUFFERS_LOW;
		for (i32 j = 0; j < u->nloop; ++j) {
			if (loop[j] != NULL && loop[j]->job_cur != NULL) {
				mm_buffers_set_tex(loop[j], loop[j]->job_cur, mm_buffers_level_size(loop[j], level), got);
				loop[j]->job_cur = NULL;
			}
		}
		mm_buffers_set_tex(u, u->job_t, mm_buffers_level_size(u, level), got);
		u->job   = false;
		u->job_t = NULL;
	}
	free(loop);
	return done;
}

// Renders a unit's texture (an iterate buffer's: its loop passes' too) at the level, replacing what it had
static i32 mm_buffers_again = 0; // IRON_SHADER_TIMING: recipes of the render in progress rendered before (evicted since)

static void mm_buffers_produce(mm_buffers_recipe_t *u, i32 level) {
	if (u->rendered != 0) {
		mm_buffers_again++;
	}
	if (u->cpu) {
		mm_buffers_set_tex(u, mm_buffers_cpu_texture(u), mm_buffers_full_size(u), MM_BUFFERS_FULL);
		return;
	}
	if (!u->job || u->job_level < level) {
		mm_buffers_job_start(u, level);
	}
	double spent = 0;
	mm_buffers_job_run(u, 0, &spent);
}

// The recipe's texture at the level, rendered now (and the inputs it needs) when missing or below. A loop pass's
// comes from rendering its iterate buffer.
static gpu_texture_t *mm_buffers_ensure(mm_buffers_recipe_t *r, i32 level) {
	r->used = mm_buffers_gen;
	if (mm_buffers_sat(r, level)) {
		return r->tex;
	}
	mm_buffers_recipe_t *u = r->in_loop ? mm_buffers_find(r->owner) : r;
	if (u == NULL || u->rendering) {
		return r->tex;
	}
	mm_buffers_produce(u, level);
	return r->tex;
}

// Saves the 2D drawing in progress, the passes need their own render passes
static bool           mm_buffers_saved_in_use = false;
static gpu_texture_t *mm_buffers_saved_target = NULL;
static u32            mm_buffers_saved_color  = 0;

static void mm_buffers_gpu_begin(void) {
	mm_buffers_saved_in_use = gpu_in_use;
	if (mm_buffers_saved_in_use) {
		mm_buffers_saved_target = _draw_current;
		mm_buffers_saved_color  = draw_get_color();
		draw_end();
	}
	if (const_data_screen_aligned_vb == NULL) {
		const_data_create_screen_aligned_data();
	}
	gpu_use_linear_sampling(true);
}

static i32 mm_buffers_nq[MM_BUFFERS_STAGES] = {0, 0, 0}; // Queued recipes, stage 0, 1 and 2

static i32 mm_buffers_queued(void) {
	return mm_buffers_nq[0] + mm_buffers_nq[1] + mm_buffers_nq[2];
}

static void mm_buffers_gpu_end(void) {
	if (mm_buffers_queued() == 0) { // Kept while steps render, they need them again next frame
		mm_buffers_scratch_free();
	}
	if (mm_buffers_saved_in_use) {
		draw_begin(mm_buffers_saved_target, false, 0);
		draw_set_color(mm_buffers_saved_color);
	}
}

// Renders the plan's passes. Returns the texture of each pass, valid until a later render evicts it (bound ones stay):
// NULL for passes whose shader failed (they render cleared, their link resolves to that). A loop pass's is its last
// iteration. The array is valid until the next call.
// One material compile renders its buffers in several plans (one per material output), sharing most passes. Each render
// used to be a generation of its own: past the budget, a plan's render evicted the passes of the plan before it, which
// the next one rendered again (dozens of passes for heavy materials on the iPad). In a batch (a material parse:
// parser_material_init to its finalize) the renders share one generation and eviction waits for the end.
static bool mm_buffers_batch         = false;
static bool mm_buffers_batch_started = false; // The batch's generation is taken

void mm_buffers_batch_end(void) {
	if (!mm_buffers_batch) {
		return;
	}
	mm_buffers_batch = false;
	if (mm_buffers_batch_started) {
		mm_buffers_evict(false);
	}
}

void mm_buffers_batch_begin(void) {
	mm_buffers_batch_end(); // A parse that did not finalize
	mm_buffers_batch         = true;
	mm_buffers_batch_started = false;
}

// The queues: stage 0 the recipes with no texture (rendered tiny), stage 1 those below low, stage 2 those not full.
// Keys, in the order queued.
static u64   *mm_buffers_q[MM_BUFFERS_STAGES]    = {NULL, NULL, NULL};
static i32    mm_buffers_qcap[MM_BUFFERS_STAGES] = {0, 0, 0};
static bool   mm_buffers_step_on = false; // mm_buffers_step registered
static double mm_buffers_wave_t0 = 0;     // The queue's work since it was last empty: start, stats
static i32    mm_buffers_wave_frames = 0;
static i32    mm_buffers_wave_passes = 0;
static i32    mm_buffers_wave_max_passes = 0;
static double mm_buffers_wave_max_ms = 0;
static double mm_buffers_wave_px = 0;
static double mm_buffers_wave_gpu = 0; // Measured GPU ms
static bool   mm_buffers_wave_usable = false;
static bool   mm_buffers_wave_tiny   = false;

static void mm_buffers_step(void *_);

static void mm_buffers_enqueue(mm_buffers_recipe_t *r, i32 stage) {
	if (r->queued[stage]) {
		return;
	}
	if (mm_buffers_queued() == 0) {
		mm_buffers_wave_t0         = iron_time();
		mm_buffers_wave_frames     = 0;
		mm_buffers_wave_passes     = 0;
		mm_buffers_wave_max_passes = 0;
		mm_buffers_wave_max_ms     = 0;
		mm_buffers_wave_px         = 0;
		mm_buffers_wave_gpu        = 0;
		mm_buffers_wave_usable     = false;
		mm_buffers_wave_tiny       = false;
	}
	if (mm_buffers_nq[stage] == mm_buffers_qcap[stage]) {
		mm_buffers_qcap[stage] = mm_buffers_qcap[stage] * 2 + 64;
		mm_buffers_q[stage]    = realloc(mm_buffers_q[stage], sizeof(u64) * mm_buffers_qcap[stage]);
	}
	mm_buffers_q[stage][mm_buffers_nq[stage]++] = r->key;
	r->queued[stage]                            = true;
	if (!mm_buffers_step_on) {
		mm_buffers_step_on = true;
		sys_notify_on_update(mm_buffers_step, NULL);
	}
}

// The queue of the textures not yet good for the level: tiny for any, low below low, full below full
static void mm_buffers_enqueue_missing(mm_buffers_recipe_t *r) {
	if (r->tex == NULL) {
		mm_buffers_enqueue(r, 0);
	}
	if (!mm_buffers_sat(r, MM_BUFFERS_LOW)) {
		mm_buffers_enqueue(r, 1);
	}
	mm_buffers_enqueue(r, 2);
}

static void mm_buffers_queue_clear(void) {
	for (i32 s = 0; s < MM_BUFFERS_STAGES; ++s) {
		for (i32 i = 0; i < mm_buffers_nq[s]; ++i) {
			mm_buffers_recipe_t *r = mm_buffers_find(mm_buffers_q[s][i]);
			if (r != NULL) {
				r->queued[s] = false;
			}
		}
		mm_buffers_nq[s] = 0;
	}
}

static bool mm_buffers_timing(void) {
	static int timing = -1;
	if (timing < 0) {
		timing = getenv("IRON_SHADER_TIMING") != NULL;
	}
	return timing == 1;
}

// Stage 0 is done: every queued pass has a texture
static void mm_buffers_tiny_done(void) {
	if (mm_buffers_wave_tiny) {
		return;
	}
	mm_buffers_wave_tiny = true;
	iron_profile_mark("mm buffers tiny");
	if (mm_buffers_timing()) {
		iron_log("[buffers] tiny after %.1f ms: %d frames, %d passes", (iron_time() - mm_buffers_wave_t0) * 1000.0, mm_buffers_wave_frames, mm_buffers_wave_passes);
	}
}

// Stage 1 is done: every queued pass is low or better
static void mm_buffers_usable(void) {
	mm_buffers_tiny_done();
	if (mm_buffers_wave_usable) {
		return;
	}
	mm_buffers_wave_usable = true;
	iron_profile_mark("mm buffers usable");
	if (mm_buffers_timing()) {
		iron_log("[buffers] usable (low) after %.1f ms: %d frames, %d passes", (iron_time() - mm_buffers_wave_t0) * 1000.0, mm_buffers_wave_frames, mm_buffers_wave_passes);
	}
}

// The queue is empty: what used textures not full draws again, eviction runs (unless in a batch)
static void mm_buffers_drained(void) {
	mm_buffers_usable();
	iron_profile_mark("mm buffers full");
	if (mm_buffers_timing()) {
		iron_log("[buffers] full after %.1f ms: %d frames, %d passes, at most %d passes and %.1f ms (main thread) a frame, %.1f Mpx, %.1f ms of GPU measured, "
		         "%.1f ms/Mpx average", (iron_time() - mm_buffers_wave_t0) * 1000.0, mm_buffers_wave_frames, mm_buffers_wave_passes, mm_buffers_wave_max_passes,
		         mm_buffers_wave_max_ms, mm_buffers_wave_px / 1e6, mm_buffers_wave_gpu, mm_buffers_cost_default);
	}
	if (mm_buffers_stale) {
		mm_buffers_stale  = false;
		g_context->ddirty = 2;
	}
	mm_buffers_scratch_free();
	if (!mm_buffers_batch) {
		mm_buffers_evict(false);
	}
}

static i32 mm_buffers_search = 0; // mm_buffers_work's stamp

// The next pass to render for the unit at the level, in dependency order: the unit itself once what it reads is good
// for the level and its pipelines are ready, else the first such pass among what it reads. NULL when everything left
// waits for pipelines.
static mm_buffers_recipe_t *mm_buffers_work(mm_buffers_recipe_t *u, i32 level) {
	if (u->visit == mm_buffers_search) { // Seen in this search: waiting, or in the path to it
		return NULL;
	}
	u->visit = mm_buffers_search;
	if (u->job) { // Continued at its level
		return u;
	}
	u64 *d       = NULL;
	i32  n       = mm_buffers_unit_deps(u, &d);
	bool waiting = false;
	for (i32 i = 0; i < n; ++i) {
		mm_buffers_recipe_t *dr = mm_buffers_find(d[i]);
		if (dr == NULL || mm_buffers_sat(dr, level)) {
			continue;
		}
		mm_buffers_recipe_t *du = dr->in_loop ? mm_buffers_find(dr->owner) : dr;
		if (du == NULL || du == u) {
			continue;
		}
		mm_buffers_recipe_t *w = mm_buffers_work(du, level);
		if (w != NULL) {
			free(d);
			return w;
		}
		waiting = true;
	}
	free(d);
	if (waiting) {
		return NULL;
	}
	return mm_buffers_unit_pipes(u) ? u : NULL;
}

// GPU time measurements: a step's draws for a unit go in a command buffer of their own, its GPU time comes back when
// it completes (on a Metal thread) and teaches the unit's cost
#define MM_BUFFERS_SLOTS 256

typedef struct {
	atomic_int state; // 0 free, 1 waiting for the GPU, 2 measured
	u64        key;
	double     px;
	double     ms;
	bool       src; // An iterate buffer's loop source draw (its cost_src)
	i32        size;
} mm_buffers_slot_t;

static mm_buffers_slot_t mm_buffers_slots[MM_BUFFERS_SLOTS];

static void mm_buffers_measured(void *data, double ms) {
	mm_buffers_slot_t *s = data;
	s->ms                = ms;
	atomic_store(&s->state, 2);
}

// Submits the draws recorded so far, timed for the unit when key is not 0
static void mm_buffers_commit(u64 key, double px, bool src, i32 size) {
#ifdef IRON_METAL
	for (i32 i = 0; key != 0 && px > 0 && i < MM_BUFFERS_SLOTS; ++i) {
		mm_buffers_slot_t *s = &mm_buffers_slots[i];
		if (atomic_load(&s->state) == 0) {
			s->key = key;
			s->px  = px;
			s->src  = src;
			s->size = size;
			atomic_store(&s->state, 1);
			gpu_commit(mm_buffers_measured, s);
			return;
		}
	}
	gpu_commit(NULL, NULL);
#endif
}

static void mm_buffers_learn(void) {
	for (i32 i = 0; i < MM_BUFFERS_SLOTS; ++i) {
		mm_buffers_slot_t *s = &mm_buffers_slots[i];
		if (atomic_load(&s->state) != 2) {
			continue;
		}
		mm_buffers_wave_gpu += s->ms > 0 ? s->ms : 0;
		if (s->ms > 50.0 && mm_buffers_timing()) { // A step's draw that took far more than its share of a frame
			mm_buffers_recipe_t *r = mm_buffers_find(s->key);
			iron_log("[buffers] a step's draw of %s%s took %.1f ms of GPU for %.2f Mpx (%.1f ms/Mpx known before)", r != NULL && r->name != NULL ? r->name : "?",
			         s->src ? " (iteration)" : "", s->ms, s->px / 1e6, r != NULL ? (s->src ? mm_buffers_cost_src_at(r, s->size) : mm_buffers_cost_at(r, s->size)) : -1.0);
		}
		if (s->ms > 0 && s->px >= 0.05e6) { // Small draws measure their overhead more than their pixels
			double               c = s->ms / (s->px / 1e6);
			mm_buffers_recipe_t *r = mm_buffers_find(s->key);
			f32                 *k  = r == NULL ? NULL : s->src ? &r->cost_src : &r->cost;
			i32                 *at = r == NULL ? NULL : s->src ? &r->cost_src_at : &r->cost_at;
			if (k != NULL) { // Averaged with what was measured at the same size, replaced by another size's
				*k  = *k > 0 && *at == s->size ? (f32)(0.5 * *k + 0.5 * c) : (f32)c;
				*at = s->size;
			}
			mm_buffers_cost_default = 0.8 * mm_buffers_cost_default + 0.2 * c;
		}
		atomic_store(&s->state, 0);
	}
}

// Per frame budget: estimated GPU time of the draws, main thread time
#define MM_BUFFERS_STEP_GPU_MS 10.0 // On the iPad: 685 full in 1.4 s, frames as without (6 ms: 3.5 s, no better frames)
#define MM_BUFFERS_STEP_MS     6.0

// Renders queued passes, a frame's share (sys_notify_on_update)
static void mm_buffers_step(void *_) {
	mm_buffers_learn();
	if (mm_buffers_queued() == 0) {
		return;
	}
	double t0 = iron_time();
	static double gpu_ms = -1, step_ms = -1; // Env MM_BUFFERS_STEP_GPU_MS, MM_BUFFERS_STEP_MS: the budgets
	if (gpu_ms < 0) {
		gpu_ms  = getenv("MM_BUFFERS_STEP_GPU_MS") != NULL ? atof(getenv("MM_BUFFERS_STEP_GPU_MS")) : MM_BUFFERS_STEP_GPU_MS;
		step_ms = getenv("MM_BUFFERS_STEP_MS") != NULL ? atof(getenv("MM_BUFFERS_STEP_MS")) : MM_BUFFERS_STEP_MS;
	}
	mm_buffers_workers_start();
	static double pipes_t0 = -1; // IRON_SHADER_TIMING: when the wave's pipelines were all made
	if (mm_buffers_timing() && pipes_t0 != mm_buffers_wave_t0) {
		bool busy = false;
		for (i32 i = 0; i < MM_BUFFERS_WORKERS; ++i) {
			busy = busy || (mm_buffers_worker_alive[i] && !atomic_load(&mm_buffers_worker_done[i]));
		}
		if (!busy) {
			pipes_t0 = mm_buffers_wave_t0;
			iron_log("[buffers] pipelines made after %.1f ms", (t0 - mm_buffers_wave_t0) * 1000.0);
		}
	}
	double px0    = mm_buffers_pixels;
	double spent  = 0;
	i32    passes = 0;
	bool   began  = false;
	while (true) {
		mm_buffers_recipe_t *w     = NULL;
		i32                  level = 1;
		mm_buffers_search++;
		for (i32 s = 0; s < MM_BUFFERS_STAGES && w == NULL; ++s) {
			level = s + 1; // MM_BUFFERS_TINY, MM_BUFFERS_LOW, MM_BUFFERS_FULL
			i32 n = 0;
			for (i32 i = 0; i < mm_buffers_nq[s]; ++i) { // Done ones leave the queue
				u64                  key = mm_buffers_q[s][i];
				mm_buffers_recipe_t *r   = mm_buffers_find(key);
				mm_buffers_recipe_t *u   = r != NULL && r->in_loop ? mm_buffers_find(r->owner) : r;
				if (r == NULL || u == NULL || mm_buffers_sat(r, level)) {
					if (r != NULL) {
						r->queued[s] = false;
					}
					continue;
				}
				mm_buffers_q[s][n++] = key;
				if (w == NULL) {
					w = mm_buffers_work(u, level);
				}
			}
			mm_buffers_nq[s] = n;
			if (s == 0 && n == 0) {
				mm_buffers_tiny_done();
			}
			if (s == 1 && n == 0) {
				mm_buffers_usable();
			}
			if (s < MM_BUFFERS_STAGES - 1 && n > 0) { // Better textures wait for every pass to have the tier below
				break;
			}
		}
		if (w == NULL) {
			break;
		}
		if (!began) {
			mm_buffers_gpu_begin();
			mm_buffers_commit(0, 0, false, 0); // The frame's work so far, apart from the timed draws
			mm_buffers_stepping = true;
			began               = true;
		}
		w->used    = mm_buffers_gen;
		double px1 = mm_buffers_pixels;
		if (w->cpu) {
			mm_buffers_produce(w, level);
		}
		else {
			if (!w->job) {
				mm_buffers_job_start(w, level);
			}
			mm_buffers_job_run(w, gpu_ms, &spent);
			if (mm_buffers_pixels == px1) { // Nothing more fits this frame
				break;
			}
		}
		passes++;
		if (spent >= gpu_ms || (iron_time() - t0) * 1000.0 >= step_ms) {
			break;
		}
	}
	if (began) {
		mm_buffers_stepping = false;
		mm_buffers_gpu_end();
		double ms = (iron_time() - t0) * 1000.0;
		mm_buffers_wave_frames++;
		mm_buffers_wave_passes += passes;
		mm_buffers_wave_px += mm_buffers_pixels - px0;
		mm_buffers_wave_max_passes = passes > mm_buffers_wave_max_passes ? passes : mm_buffers_wave_max_passes;
		mm_buffers_wave_max_ms     = ms > mm_buffers_wave_max_ms ? ms : mm_buffers_wave_max_ms;
		if (mm_buffers_stale) {
			g_context->ddirty = 2; // What showed textures not full draws again with the better ones
		}
	}
	if (mm_buffers_queued() == 0) {
		mm_buffers_drained();
	}
}

// The estimated time (ms) of rendering the recipes with no texture yet at the tiny size s: each draw's pixels at its
// measured cost per Mpx (the average when not measured yet) plus a fixed overhead per draw
static double mm_buffers_tiny_estimate(mm_buffers_recipe_t **rs, i32 count, i32 s) {
	double ms = 0;
	for (i32 i = 0; i < count; ++i) {
		mm_buffers_recipe_t *r = rs[i];
		if (r->tex != NULL || r->cpu) {
			continue;
		}
		bool dup = false;
		for (i32 j = 0; j < i && !dup; ++j) {
			dup = rs[j] == r;
		}
		if (dup) {
			continue;
		}
		mm_buffers_recipe_t *o     = r->in_loop ? mm_buffers_find(r->owner) : NULL;
		i32                  draws = r->in_loop ? (o != NULL ? o->iterations : 1) : r->iterate ? 1 + r->iterations : 1;
		i32                  size  = mm_buffers_full_size(r);
		size                       = size > s ? s : size;
		ms += draws * (mm_buffers_cost_at(r, size) * (double)size * size / 1e6 + MM_BUFFERS_DRAW_MS);
	}
	return ms;
}

// A plan's tiny size: MM_BUFFERS_TINY_MAX, halved while its passes with no texture would take more than
// MM_BUFFERS_TINY_MS to render on the spot, down to MM_BUFFERS_TINY_MIN
static void mm_buffers_tiny_size(mm_buffers_recipe_t **rs, i32 count) {
	static i32 forced = -1; // Env MM_BUFFERS_TINY_SIZE: the tiny size, whatever the estimate
	if (forced < 0) {
		forced = getenv("MM_BUFFERS_TINY_SIZE") != NULL ? atoi(getenv("MM_BUFFERS_TINY_SIZE")) : 0;
	}
	i32 s = MM_BUFFERS_TINY_MAX;
	while (forced <= 0 && s > MM_BUFFERS_TINY_MIN && mm_buffers_tiny_estimate(rs, count, s) > MM_BUFFERS_TINY_MS) {
		s /= 2;
	}
	s = forced > 0 ? forced : s;
	bool any = false;
	for (i32 i = 0; i < count; ++i) {
		if (rs[i]->tex == NULL && !rs[i]->job) { // A job in progress keeps its size
			rs[i]->tiny = s;
			any         = true;
		}
	}
	if (any && mm_buffers_timing()) {
		iron_log("[buffers] tiny size %d, estimated %.1f ms on the spot", s, mm_buffers_tiny_estimate(rs, count, s));
	}
}

gpu_texture_t **mm_buffers_render(mm_plan_t *plan) {
	if (!mm_buffers_batch || !mm_buffers_batch_started) {
		mm_buffers_gen++;
		mm_buffers_batch_started = mm_buffers_batch;
	}
	mm_buffers_calls++;
	memset(mm_buffers_stats, 0, sizeof(mm_buffers_stats));
	double t_render     = iron_time();
	mm_buffers_pixels   = 0;
	mm_buffers_max_size = 0;
	mm_buffers_again    = 0;
	i32 count             = plan != NULL ? plan->count : 0;
	mm_buffers_last_plan  = plan;
	mm_buffers_last_count = count;
	mm_buffers_last_keys  = realloc(mm_buffers_last_keys, sizeof(u64) * (count + 1));
	mm_buffers_out        = realloc(mm_buffers_out, sizeof(gpu_texture_t *) * (count + 1));
	memset(mm_buffers_out, 0, sizeof(gpu_texture_t *) * (count + 1));
	if (count == 0) {
		if (!mm_buffers_batch) {
			mm_buffers_evict(false);
		}
		return mm_buffers_out;
	}

	u64 *content = malloc(sizeof(u64) * count);
	i32 *pos     = malloc(sizeof(i32) * count);
	i32 *order   = malloc(sizeof(i32) * count);
	for (i32 i = 0; i < count; ++i) {
		content[i] = mm_buffers_content(&plan->passes[i]);
		pos[i]     = -1;
	}
	for (i32 i = 0; i < count; ++i) {
		mm_buffers_last_keys[i] = mm_buffers_key(plan, content, i, pos, order);
	}
	free(content);
	free(pos);
	free(order);

	// Recipes first, the passes' inputs are found by their keys (a CPU texture's pixels are made here, once)
	mm_buffers_recipe_t **rs = malloc(sizeof(mm_buffers_recipe_t *) * count);
	for (i32 i = 0; i < count; ++i) {
		rs[i] = mm_buffers_find(mm_buffers_last_keys[i]);
		if (rs[i] == NULL) {
			rs[i] = mm_buffers_recipe_new(plan, i, mm_buffers_last_keys);
		}
		rs[i]->recipe_used = mm_buffers_gen;
	}
	bool began  = false;
	bool sync   = mm_buffers_sync() || mm_buffers_forced;
	i32  queued = 0;
	if (!sync) {
		mm_buffers_tiny_size(rs, count);
	}
	if (sync) { // Every pass full, now
		mm_buffers_precompile(rs, count);
		for (i32 i = 0; i < count; ++i) {
			mm_buffers_recipe_t *r = rs[i];
			if (mm_buffers_sat(r, MM_BUFFERS_FULL)) {
				r->used = mm_buffers_gen;
				continue;
			}
			if (!began) {
				mm_buffers_gpu_begin();
				began = true;
			}
			mm_buffers_ensure(r, MM_BUFFERS_FULL);
		}
	}
	else if (mm_buffers_needed() > 0) { // A preview or a thumbnail: its draw renders what it needs, the pipelines start now
		for (i32 i = 0; i < count; ++i) {
			rs[i]->used = mm_buffers_gen;
			if (!mm_buffers_sat(rs[i], mm_buffers_needed())) {
				mm_buffers_recipe_pipes(rs[i]);
			}
		}
		mm_buffers_workers_start();
	}
	else { // Queued: tiny textures first, then low, then full ones, mm_buffers_step renders them over the next frames
		for (i32 i = 0; i < count; ++i) {
			mm_buffers_recipe_t *r = rs[i];
			r->used                = mm_buffers_gen;
			if (mm_buffers_sat(r, MM_BUFFERS_FULL)) {
				continue;
			}
			queued += !r->queued[2];
			mm_buffers_enqueue_missing(r);
			mm_buffers_recipe_pipes(r); // Made by the workers meanwhile
		}
		// The workers start with the next step: the material's own shaders compile first, Metal's compiler not shared
	}
	for (i32 i = 0; i < count; ++i) {
		bool dup = false; // A pass twice in the plan (same key) counts once
		for (i32 j = 0; j < i && !dup; ++j) {
			dup = rs[j] == rs[i];
		}
		if (!dup) {
			mm_buffers_stats[0] += rs[i]->rendered_call == mm_buffers_calls;
			mm_buffers_stats[1] += rs[i]->tex != NULL && rs[i]->rendered_call != mm_buffers_calls;
			mm_buffers_stats[2] += rs[i]->failed;
		}
		mm_buffers_out[i] = rs[i]->failed ? NULL : rs[i]->tex;
	}
	if (getenv("IRON_SHADER_TIMING") != NULL) {
		i32 iterations = 0, iterating = 0, biggest = 0;
		for (i32 i = 0; i < count; ++i) {
			if (rs[i]->iterate) {
				iterating++;
				iterations += rs[i]->iterations;
			}
			i32 s   = mm_buffers_full_size(rs[i]);
			biggest = s > biggest ? s : biggest;
		}
		if (began) {
			gpu_execute_and_wait(); // The GPU time of this render lands here, not in the next one
		}
		iron_log("[buffers] %d passes (%d rendered, %d of them again after eviction, %d cached, %d failed, %d queued), %d draws, %.1f Mpx drawn, largest target %d "
		         "(plan %d), %d iterate buffers with %d iterations, budget %d MB, %.1f ms",
		         count, mm_buffers_stats[0], mm_buffers_again, mm_buffers_stats[1], mm_buffers_stats[2], queued, mm_buffers_stats[3], mm_buffers_pixels / 1e6,
		         mm_buffers_max_size, biggest, iterating, iterations, (i32)(mm_buffers_budget() / (1024 * 1024)), (iron_time() - t_render) * 1000.0);
	}
	free(rs);
	if (began) {
		mm_buffers_gpu_end();
	}
	if (!mm_buffers_batch) { // A batch evicts when it ends
		mm_buffers_evict(false);
	}
	return mm_buffers_out;
}

// The texture link of a pass of the plan last rendered: "_mmbuf" + 16 hex digits. NULL for other plans.
char *mm_buffers_link(mm_plan_t *plan, i32 pass) {
	if (plan == NULL || plan != mm_buffers_last_plan || pass < 0 || pass >= mm_buffers_last_count) {
		return NULL;
	}
	return string("%s%016llx", MM_BUFFERS_LINK, (unsigned long long)mm_buffers_last_keys[pass]);
}

// The pipelines not ready yet that rendering r at the level now uses: those of every unit below the level it reads
static void mm_buffers_pipes_collect(mm_buffers_recipe_t *r, i32 level, mm_buffers_pipe_t ***out, i32 *n, i32 *cap) {
	mm_buffers_recipe_t *u = r->in_loop ? mm_buffers_find(r->owner) : r;
	if (u == NULL || u->visit == mm_buffers_search || mm_buffers_sat(r, level)) {
		return;
	}
	u->visit = mm_buffers_search;
	for (i32 s = -1; s < u->nloop; ++s) {
		mm_buffers_recipe_t *rr = s < 0 ? u : mm_buffers_find(u->loop[s]);
		if (rr == NULL || rr->cpu) {
			continue;
		}
		for (i32 k = 0; k < (rr->iterate ? 2 : 1); ++k) {
			mm_buffers_pipe_t *p = mm_buffers_pipe_get(k == 0 ? rr->glsl : rr->glsl_loop, k == 0 ? rr->glsl_hash : rr->loop_hash, mm_buffers_format(rr));
			if (mm_buffers_pipe_ready(p)) {
				continue;
			}
			bool dup = false;
			for (i32 i = 0; i < *n && !dup; ++i) {
				dup = (*out)[i] == p;
			}
			if (dup) {
				continue;
			}
			if (*n == *cap) {
				*cap = *cap * 2 + 32;
				*out = realloc(*out, sizeof(mm_buffers_pipe_t *) * *cap);
			}
			(*out)[(*n)++] = p;
		}
	}
	u64 *d  = NULL;
	i32  nd = mm_buffers_unit_deps(u, &d);
	for (i32 i = 0; i < nd; ++i) {
		mm_buffers_recipe_t *dr = mm_buffers_find(d[i]);
		if (dr != NULL && !mm_buffers_sat(dr, level)) {
			mm_buffers_pipes_collect(dr, level, out, n, cap);
		}
	}
	free(d);
}

static bool mm_buffers_in_list(mm_buffers_pipe_t *p, mm_buffers_pipe_t **list, i32 n) {
	for (i32 i = 0; i < n; ++i) {
		if (list[i] == p) {
			return true;
		}
	}
	return false;
}

// Before an on-the-spot render: the pipelines it needs go first in the workers' queue, the workers start and this
// thread makes its share. Made one after another as the passes rendered, they took seconds for a library material
// picked a moment before (the workers had not started yet). Past the deadline (mm_buffers_deadline) it stops waiting.
// Returns whether all are ready, the ms waited in *ms.
static bool mm_buffers_pipes_first(mm_buffers_recipe_t *r, i32 level, double *ms) {
	mm_buffers_pipe_t **list = NULL;
	i32                 n = 0, cap = 0;
	*ms                      = 0;
	mm_buffers_search++;
	mm_buffers_pipes_collect(r, level, &list, &n, &cap);
	if (n == 0) {
		free(list);
		return true;
	}
	double t0 = iron_time();
	mm_buffers_jobs_lock();
	for (i32 i = n - 1; i >= 0; --i) { // The list's order at the front
		mm_buffers_pipe_t *p = list[i];
		bool               queued = false;
		for (i32 j = 0; j < mm_buffers_njobs; ++j) {
			if (mm_buffers_jobs[j] == p) {
				memmove(mm_buffers_jobs + j, mm_buffers_jobs + j + 1, sizeof(mm_buffers_pipe_t *) * (mm_buffers_njobs - j - 1));
				mm_buffers_njobs--;
				queued = true;
				break;
			}
		}
		if (!queued && atomic_load(&p->state) != MM_PIPE_NEW) {
			continue; // A worker makes it
		}
		atomic_store(&p->state, MM_PIPE_QUEUED);
		if (mm_buffers_njobs == mm_buffers_jobs_cap) {
			mm_buffers_jobs_cap = mm_buffers_jobs_cap * 2 + 16;
			mm_buffers_jobs     = realloc(mm_buffers_jobs, sizeof(mm_buffers_pipe_t *) * mm_buffers_jobs_cap);
		}
		memmove(mm_buffers_jobs + 1, mm_buffers_jobs, sizeof(mm_buffers_pipe_t *) * mm_buffers_njobs);
		mm_buffers_jobs[0] = p;
		mm_buffers_njobs++;
	}
	iron_mutex_unlock(&mm_buffers_jobs_mutex);
	mm_buffers_workers_start();
	bool all = false;
	while (true) {
		all = true;
		for (i32 i = 0; i < n && all; ++i) {
			all = mm_buffers_pipe_ready(list[i]);
		}
		if (all || (mm_buffers_deadline > 0 && iron_time() > mm_buffers_deadline)) {
			break;
		}
		mm_buffers_pipe_t *p = NULL;
		mm_buffers_jobs_lock();
		if (mm_buffers_njobs > 0 && mm_buffers_in_list(mm_buffers_jobs[0], list, n)) {
			p = mm_buffers_jobs[0];
			memmove(mm_buffers_jobs, mm_buffers_jobs + 1, sizeof(mm_buffers_pipe_t *) * (mm_buffers_njobs - 1));
			mm_buffers_njobs--;
		}
		iron_mutex_unlock(&mm_buffers_jobs_mutex);
		if (p != NULL) {
			mm_buffers_pipe_make(p);
		}
		else {
			usleep(500);
		}
	}
	free(list);
	*ms = (iron_time() - t0) * 1000.0;
	return all;
}

// A texture at the level, rendered now: inside a material's render pass on Metal, which is suspended meanwhile
static gpu_texture_t *mm_buffers_rerender(mm_buffers_recipe_t *r, i32 level) {
	bool suspended = false;
	if (gpu_in_use) {
#ifdef IRON_METAL
		gpu_suspend(); // Between gpu_begin and gpu_draw of a material
		suspended = true;
#else // No gpu_suspend: the best texture there is, the rest renders over the next frames
		mm_buffers_enqueue_missing(r);
		mm_buffers_stale = true;
		return r->tex;
#endif
	}
	bool   had     = r->rendered != 0;
	double t0      = iron_time();
	double pipe_ms = 0;
	mm_buffers_aborted = false;
	if (mm_buffers_pipes_first(r, level, &pipe_ms)) {
		mm_buffers_gpu_begin();
		mm_buffers_ensure(r, level);
		mm_buffers_gpu_end();
	}
	else {
		mm_buffers_aborted = true; // Its pipelines were not ready by the deadline
	}
	if (suspended) {
#ifdef IRON_METAL
		gpu_resume();
#endif
	}
	mm_buffers_rerenders += had;
	bool cut = mm_buffers_aborted;
	mm_buffers_aborted = false;
	mm_buffers_skipped += cut;
	if (!mm_buffers_sat(r, MM_BUFFERS_FULL)) { // Better ones over the next frames (a render cut short continues there)
		mm_buffers_enqueue_missing(r);
		mm_buffers_stale = true;
	}
	if (mm_buffers_timing()) {
		iron_log("[buffers] %s %s on the spot at tier %d (size %d): %.1f ms, %.1f of them waiting for pipelines", r->name != NULL ? r->name : "?",
		         cut ? "cut short (deadline)" : "rendered", level, r->tex_size, (iron_time() - t0) * 1000.0, pipe_ms);
	}
	return r->tex;
}

// Paint draws (tiny, no preview size cap): their on-the-spot renders share an allowance of MM_BUFFERS_RUSH_MS of wall
// time, renewed once none was asked for MM_BUFFERS_RUSH_GAP seconds. Past it, a link resolves to what there is (an empty
// texture when nothing yet), the rest renders over the next frames, util_stroke_refine.c paints the strokes again.
static bool mm_buffers_rushed(i32 level) {
	return level == MM_BUFFERS_TINY && mm_buffers_size_cap == 0 && !mm_buffers_sync() && !mm_buffers_forced;
}

i32 mm_buffers_rush_skipped(void) {
	return mm_buffers_skipped;
}

// The texture of a link: the best one rendered, NULL (an empty texture) when none yet; what is missing is queued. When
// links need usable or full textures (mm_buffers_require), they render now. NULL when it is not a link or its recipe
// is gone. Marks it used: bound textures stay.
gpu_texture_t *mm_buffers_texture(char *link) {
	if (link == NULL || !starts_with(link, MM_BUFFERS_LINK)) {
		return NULL;
	}
	u64                  key = strtoull(link + strlen(MM_BUFFERS_LINK), NULL, 16);
	mm_buffers_recipe_t *r   = mm_buffers_find(key);
	if (r == NULL) {
		return NULL;
	}
	mm_buffers_touch(r);
	r->used   = mm_buffers_gen;
	i32 level = mm_buffers_needed();
	if (level > 0 && !mm_buffers_sat(r, level)) {
		if (!mm_buffers_rushed(level)) {
			return mm_buffers_rerender(r, level);
		}
		double now = iron_time();
		if (mm_buffers_rush_last < 0 || now - mm_buffers_rush_last > MM_BUFFERS_RUSH_GAP) {
			mm_buffers_rush_t0 = now;
		}
		mm_buffers_rush_last = now;
		mm_buffers_deadline  = mm_buffers_rush_t0 + MM_BUFFERS_RUSH_MS / 1000.0;
		gpu_texture_t *t     = r->tex;
		if (now < mm_buffers_deadline) {
			t = mm_buffers_rerender(r, level);
		}
		else {
			mm_buffers_skipped++;
		}
		mm_buffers_deadline = 0;
		if (!mm_buffers_sat(r, MM_BUFFERS_FULL)) {
			mm_buffers_enqueue_missing(r);
			mm_buffers_stale = true;
		}
		return t;
	}
	if (!mm_buffers_sat(r, MM_BUFFERS_FULL)) { // The best there is, the rest over the next frames
		mm_buffers_enqueue_missing(r);
		mm_buffers_stale = true;
	}
	return r->tex;
}

// The buffer keys a shader context samples (its "_mmbuf" texture links), at most max; returns the count
i32 mm_buffers_paint_keys(shader_context_t *scon, u64 *out, i32 max) {
	i32 n = 0;
	for (i32 i = 0; scon != NULL && scon->texture_units != NULL && i < scon->texture_units->length && n < max; ++i) {
		tex_unit_t *tu = scon->texture_units->buffer[i];
		if (tu->link != NULL && starts_with(tu->link, MM_BUFFERS_LINK)) {
			out[n++] = strtoull(tu->link + strlen(MM_BUFFERS_LINK), NULL, 16);
		}
	}
	return n;
}

// The lowest tier among the recipes of the keys: 0 when one has no texture, MM_BUFFERS_FULL when all are full, -1 when
// a recipe is gone (evicted, cleared). A texture's tier includes its inputs'.
i32 mm_buffers_links_tier(u64 *keys, i32 n) {
	i32 tier = MM_BUFFERS_FULL;
	for (i32 i = 0; i < n; ++i) {
		mm_buffers_recipe_t *r = mm_buffers_find(keys[i]);
		if (r == NULL) {
			return -1;
		}
		i32 t = mm_buffers_tier(r);
		tier  = t < tier ? t : tier;
	}
	return tier;
}

i32 mm_buffers_landed_count(void) {
	return mm_buffers_landed;
}

// Every queued pass rendered full now
void mm_buffers_finish(void) {
	if (mm_buffers_queued() == 0) {
		return;
	}
	mm_buffers_gpu_begin();
	for (i32 s = 0; s < MM_BUFFERS_STAGES; ++s) {
		for (i32 i = 0; i < mm_buffers_nq[s]; ++i) {
			mm_buffers_recipe_t *r = mm_buffers_find(mm_buffers_q[s][i]);
			if (r != NULL) {
				mm_buffers_ensure(r, MM_BUFFERS_FULL);
			}
		}
	}
	mm_buffers_queue_clear();
	mm_buffers_gpu_end();
	mm_buffers_drained();
}

// Frees every texture, recipe and pipeline
void mm_buffers_clear(void) {
	for (i32 b = 0; b < MM_BUFFERS_BUCKETS; ++b) {
		for (mm_buffers_recipe_t *r = mm_buffers_table[b]; r != NULL; r = r->next) {
			mm_buffers_job_cancel(r);
		}
	}
	mm_buffers_queue_clear();
	mm_buffers_scratch_free();
	mm_buffers_evict(true);
	mm_buffers_last_plan  = NULL;
	mm_buffers_last_count = 0;
}

// 0: passes rendered by the last render, 1: found in the cache, 2: failed shaders, 3: draws, 4: cached textures,
// 5: cached pipelines, 6: cached texture memory in KB, 7: the texture budget in KB, 8: textures rendered again on
// demand (since start), 9: recipes, 10: recipe memory in KB, 11: queued passes, 12: 1 in the synchronous mode
// (MM_BUFFERS_SYNC), 13: 1 when 32-bit float buffers stay 32-bit (filterable)
i32 mm_buffers_stat(i32 what) {
	if (what >= 0 && what < 4) {
		return mm_buffers_stats[what];
	}
	if (what == 5) {
		return mm_buffers_npipes;
	}
	if (what == 7) {
		return (i32)(mm_buffers_budget() / 1024);
	}
	if (what == 8) {
		return mm_buffers_rerenders;
	}
	if (what == 9) {
		return mm_buffers_nrecipes;
	}
	if (what == 11) {
		return mm_buffers_queued();
	}
	if (what == 12) {
		return mm_buffers_sync() ? 1 : 0;
	}
	if (what == 13) {
		return mm_buffers_float32() ? 1 : 0;
	}
	i64 textures = 0, bytes = 0, recipes = 0;
	for (i32 b = 0; b < MM_BUFFERS_BUCKETS; ++b) {
		for (mm_buffers_recipe_t *r = mm_buffers_table[b]; r != NULL; r = r->next) {
			textures += r->tex != NULL;
			bytes += r->tex != NULL ? mm_buffers_bytes(r->tex) : 0;
			recipes += r->bytes;
		}
	}
	return what == 4 ? (i32)textures : what == 6 ? (i32)(bytes / 1024) : what == 10 ? (i32)(recipes / 1024) : 0;
}

// Image and text nodes: files relative to data/material_maker, the shipped font

static uint8_t *mm_buffers_read_file(const char *path, size_t *size) {
	char     *file = path[0] == '/' ? (char *)path : data_resolve_path(string_tmp("material_maker/%s", path));
	buffer_t *b    = iron_load_blob(file);
	if (b == NULL) {
		return NULL;
	}
	uint8_t *data = malloc(b->length > 0 ? b->length : 1);
	memcpy(data, b->buffer, b->length);
	*size = b->length;
	iron_delete_blob(b);
	return data;
}

void mm_buffers_init(void) {
	static bool done = false;
	if (done) {
		return;
	}
	done = true;
	mm_set_file_reader(mm_buffers_read_file);
	buffer_t *font = data_get_blob("material_maker/OpenSans-SemiBold.ttf"); // Kept, mm_add_font does not copy
	if (font != NULL) {
		mm_add_font(font->buffer, font->length);
	}
}
