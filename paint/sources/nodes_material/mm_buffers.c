// Material Maker buffers: renders the passes of an mm_plan_t (material_maker/mm.h) with the app's GPU layer, as
// paint/tools/material_maker/mm_render.m does (the checker's Metal renderer, whose output matches Material Maker's
// exports): a fullscreen triangle per pass into a float target (R16F / R32F / RGBA16F / RGBA32F), constants vec4 mm =
// (target size, iteration, 1.0, 0) at offset 0, the linear repeat sampler, texture i at binding 2 + i. A pass whose
// shader does not compile renders a cleared texture, as in Material Maker.
//
// Material shaders sample a rendered pass through a texture link: "_mmbuf" + the pass's key as 16 lowercase hex digits
// (mm_buffers_link), resolved by uniforms_ext_tex_link with mm_buffers_texture.
//
// Keys: a 64-bit hash of everything a pass's texture depends on: its GLSL (and loop GLSL), its size in the graph (not the
// size it renders at, see Levels), format, iterations and shrink, a CPU texture's mm_texture_key, and the same for every
// pass it reads, followed through iterate loops (a canonical hash of the dependency graph from the pass).
//
// Recipes: each pass gets a recipe, malloc'd, outliving the mm arena: its GLSL, flags, the keys of its inputs, the loop
// passes of an iterate buffer, a CPU texture's pixels (lz4). A recipe renders its texture again by itself, rendering
// the inputs it needs from their recipes: a material shader compiled once keeps resolving its links after its textures
// were evicted. A texture needed at once (mm_buffers_require) renders when the link resolves: between gpu_begin and
// gpu_draw on Metal, where the pass in progress is suspended (gpu_suspend); elsewhere it is queued.
//
// Eviction: textures stay while they fit MM_BUFFERS_BUDGET_MB; past it, the least recently rendered, found or bound go
// first, never the last render's (a generation counter: mm_buffers_render advances it, finding or binding marks used),
// nor queued ones. A recipe has one texture: its low one is freed when the full one lands.
// Pipelines stay up to MM_BUFFERS_MAX_PIPES, the least recently used go first. Recipes
// stay while they fit MM_BUFFERS_RECIPE_BUDGET_MB, the least recently used go first; binding a link keeps its recipe and
// those of every pass it reads.
//
// Passes inside an iterate buffer's loop (loop_of >= 0) render into scratch targets for each iteration, recycled; their
// last iteration's texture is kept as theirs (Material Maker renders them again whenever the loop texture updates),
// with a link like any pass. Their key includes the iterate buffer's; rendering one again renders the iterate buffer.
// An iterate buffer and its loop passes render together: a unit.
//
// Levels (tiers): a pass's texture renders at three resolutions. Full: the pass's own size, at most the project's
// texture resolution and MM_BUFFERS_MAX_SIZE. Low: at most MM_BUFFERS_LOW_SIZE, from low or better inputs. Tiny: a
// size chosen per material (mm_buffers_tiny_size, 128 down to 32) so that rendering all its passes on the spot takes
// about MM_BUFFERS_TINY_MS: a stroke started before the buffers are done paints at once with them. A texture is full
// once rendered at the full size from full inputs. A recipe holds its best texture; the key and link do not depend on
// the size, the material shader samples whichever texture is there without compiling again.
//
// Deferred rendering: a material compile (mm_buffers_render) makes the recipes and queues their passes instead of
// rendering them: the stage 0 queue for passes with no texture (rendered tiny), the stage 1 queue for passes below
// low, the stage 2 queue for passes not full; a stage starts once the one before it is empty.
// mm_buffers_step, each frame, renders queued passes in dependency order within a budget, stage 0 first, without
// waiting for the GPU: the estimated GPU time of its draws (MM_BUFFERS_STEP_GPU_MS, each pass's cost in ms per Mpx
// measured from the GPU times of earlier draws, gpu_commit) and main thread time. A unit's render continues across
// frames (the recipe's job): draw after draw (an iterate buffer's iterations), a draw too long for a frame in bands of
// rows (scissor). Pipelines translate (GLSL to MSL) and compile on worker threads (mm_buffers_pipe_request), a pass
// renders once its pipelines are ready.
//
// mm_buffers_texture returns the best texture of a link (NULL, an empty texture, when there is none yet) and queues
// what is missing, unless the caller needs more (mm_buffers_require): MM_BUFFERS_TINY, any texture rendered at once
// (paint draws and fill layers: tiny, within an allowance of MM_BUFFERS_RUSH_MS, pipelines included, past which they
// take what there is; previews: at most the size cap; util_stroke_refine.c paints strokes and fill layers again as
// better tiers land), MM_BUFFERS_LOW (node previews) or MM_BUFFERS_FULL (bakes, thumbnails: full textures at once).
// Env MM_BUFFERS_SYNC=1: the old synchronous behaviour, every pass rendered full by mm_buffers_render, no low stage.
// Env MM_BUFFERS_STEP_GPU_MS, MM_BUFFERS_STEP_MS: a step's budgets. IRON_SHADER_TIMING logs when the queue's pipelines
// are made, when its passes are usable and full (also profile marks "mm buffers usable" / "mm buffers full").
//
// Files: mm_buffers.c keys, recipes and eviction, mm_buffers_pipes.c pipelines and their worker threads,
// mm_buffers_render.c drawing, the deferred queue and the API, mm_buffers_test.c the hooks of tests/mm_buffers.c.

#include "../global.h"
#include "../material_maker/mm.h"
#include "../util/util_hash.h"
#include <iron_profile.h>
#include <stdatomic.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MM_BUFFERS_MAX_PIPES 256 // Compiled pass pipelines kept, the least recently used go first past it
#define MM_BUFFERS_FLUSH 64 // Draws between submits, short command buffers (the ios gpu watchdog, iterate buffers draw thousands)
#define MM_BUFFERS_LOW_SIZE 512 // The low level, after tiny
#define MM_BUFFERS_TINY_MAX 128 // Tiny sizes: the largest, halved down to the smallest while the estimate exceeds the budget
#define MM_BUFFERS_TINY_MIN 32
#define MM_BUFFERS_TINY_MS  300.0 // Estimated time of a material's tiny passes rendered on the spot
#define MM_BUFFERS_DRAW_MS  0.05  // Estimated overhead of a draw, whatever its size
#define MM_BUFFERS_STAGES   3     // Queues: tiny, low, full
#define MM_BUFFERS_RUSH_MS  250.0 // Paint draws: their on-the-spot tiny renders take at most this together (wall time,
#define MM_BUFFERS_RUSH_GAP 0.5   // pipelines included), until none was asked for this many seconds
#ifdef IRON_IOS
#define MM_BUFFERS_MAX_SIZE         2048 // Full pass sizes are clamped to it (and to the project's texture resolution)
#define MM_BUFFERS_MAX_CPU_SIZE     1024 // CPU textures (text: 2048²) are halved down to it
#define MM_BUFFERS_BUDGET_MB        256  // When the device's memory is unknown (see mm_buffers_budget)
#define MM_BUFFERS_RAM_SHARE        10   // Otherwise the budget is this fraction of physical memory,
#define MM_BUFFERS_BUDGET_MIN_MB    256  // clamped to this range,
#define MM_BUFFERS_BUDGET_MAX_MB    1536 // and to a third of what iOS still lets the app use (os_proc_available_memory)
#define MM_BUFFERS_RECIPE_BUDGET_MB 32
#else
#define MM_BUFFERS_MAX_SIZE         4096
#define MM_BUFFERS_MAX_CPU_SIZE     4096
#define MM_BUFFERS_BUDGET_MB        512
#define MM_BUFFERS_RAM_SHARE        8
#define MM_BUFFERS_BUDGET_MIN_MB    512
#define MM_BUFFERS_BUDGET_MAX_MB    4096
#define MM_BUFFERS_RECIPE_BUDGET_MB 64
#endif
#define MM_BUFFERS_LINK    "_mmbuf"

#ifdef __APPLE__
#include <pthread.h>
#include <sys/sysctl.h>
#ifdef IRON_IOS
#include <os/proc.h>
#endif
#endif
#define MM_BUFFERS_BUCKETS 1024

typedef struct mm_buffers_recipe {
	u64                       key;
	char                     *name; // The pass's debug name, for IRON_BUFFERS_PROFILE
	struct mm_buffers_recipe *next; // Hash bucket
	char                     *glsl;
	char                     *glsl_loop;
	u64                       glsl_hash;
	u64                       loop_hash;
	i32                       own; // The pass's size in its graph, before clamping (mm_buffers_full_size)
	bool                      greyscale;
	bool                      f32;
	bool                      exact; // 32-bit floats wherever (read with texelFetch)
	bool                      iterate;
	bool                      shrink;
	bool                      in_loop;
	i32                       iterations;
	i32                       textures;
	u64                      *inputs; // Keys, 0: none
	i32                       loop_textures;
	u64                      *loop_inputs; // The recipe's own key: the previous iteration
	i32                       nloop;
	u64                      *loop; // The loop's passes, in render order
	u64                       owner; // A loop pass: its iterate buffer
	// CPU textures: RGBA8 pixels, lz4 (raw when tiny)
	bool                      cpu;
	buffer_t                 *pixels;
	bool                      raw;
	i32                       w;
	i32                       h;
	// State
	gpu_texture_t            *tex; // NULL until rendered, and once evicted: the best texture rendered
	i32                       tex_size;  // The size it rendered at
	i32                       tex_level; // MM_BUFFERS_TINY, MM_BUFFERS_LOW (from low inputs or better), MM_BUFFERS_FULL
	i32                       tiny;      // The size its tiny level renders at (mm_buffers_tiny_size), 0 MM_BUFFERS_TINY_MAX
	bool                      queued[MM_BUFFERS_STAGES]; // In the stage 0 / 1 / 2 queue
	i32                       visit;     // mm_buffers_work's search stamp
	// A unit's render in progress, continued over frames: draw after draw (an iterate buffer's first draw, then for each
	// iteration its loop passes and its loop source), a draw in bands of rows when it does not fit a frame
	bool                      job;
	i32                       job_level;
	i32                       job_k;     // Iteration: 0 the first draw
	i32                       job_phase; // In an iteration: loop pass, nloop for the loop source
	i32                       job_band;  // Rows of the draw in progress done
	bool                      job_exact; // Full sizes from full inputs
	gpu_texture_t            *job_draw;  // The target of the draw in progress
	gpu_texture_t            *job_t;     // The last iteration's texture
	gpu_texture_t            *job_cur;   // A loop pass: its texture of the last iteration
	f32                       cost;      // Measured GPU ms per Mpx drawn, 0 unknown
	f32                       cost_src;  // An iterate buffer's loop source draws', 0 unknown
	i32                       cost_at;     // The size cost was measured at
	i32                       cost_src_at; // The size cost_src was measured at
	bool                      failed;
	i32                       used;        // Generation the texture was last rendered, found or bound
	i32                       recipe_used; // Generation the recipe was last needed
	i64                       bytes;       // Of the recipe
	gpu_texture_t            *cur;         // While rendering: a loop pass's current texture, an iterate buffer's previous iteration
	bool                      rendering;
	i32                       rendered;      // Generation it was last rendered
	i32                       rendered_call; // mm_buffers_render call it was last rendered by (the stats)
} mm_buffers_recipe_t;

// A pipeline's state: new (nothing made), queued for a worker or being made by one, ready (compiled, or failed: !ok)
#define MM_PIPE_NEW    0
#define MM_PIPE_QUEUED 1
#define MM_PIPE_READY  2

typedef struct {
	u64                  key; // GLSL hash and format
	gpu_pipeline_t       pipe;
	gpu_shader_t        *vs;
	gpu_shader_t        *fs;
	bool                 ok;
	atomic_int           state;
	char                *glsl; // Until made
	gpu_texture_format_t format;
	i32                  used;
} mm_buffers_pipe_t;

typedef struct {
	gpu_texture_t *tex;
	bool           busy;
} mm_buffers_scratch_t;

static mm_buffers_recipe_t   *mm_buffers_table[MM_BUFFERS_BUCKETS];
static i32                    mm_buffers_nrecipes = 0;
static mm_buffers_pipe_t    **mm_buffers_pipes    = NULL;
static i32                    mm_buffers_npipes   = 0;
static mm_buffers_scratch_t  *mm_buffers_scratch  = NULL;
static i32                    mm_buffers_nscratch = 0;
static i32                    mm_buffers_gen      = 0;
static i32                    mm_buffers_calls    = 0; // mm_buffers_render calls (a batch shares one generation)
static i32                    mm_buffers_draws    = 0;
static i32                    mm_buffers_rerenders = 0;
static gpu_texture_t         *mm_buffers_empty    = NULL;
static gpu_vertex_structure_t mm_buffers_structure;
static i32                    mm_buffers_need     = 0;     // mm_buffers_require
static bool                   mm_buffers_forced   = false; // mm_buffers_render renders synchronously (tests)
static bool                   mm_buffers_stepping = false; // In mm_buffers_step: draws are not flushed
static double                 mm_buffers_deadline = 0;     // An on-the-spot render stops past it (iron_time), 0: none
static bool                   mm_buffers_aborted  = false; // It did, or its pipelines were not ready by then
static i32                    mm_buffers_skipped  = 0;     // On-the-spot renders skipped or cut short by the deadline
static double                 mm_buffers_rush_t0   = -1;   // The paint draws' allowance (MM_BUFFERS_RUSH_MS) started
static double                 mm_buffers_rush_last = -1;   // An on-the-spot render was last asked for

// The last render: its returned textures and keys, for mm_buffers_link
static mm_plan_t      *mm_buffers_last_plan  = NULL;
static i32             mm_buffers_last_count = 0;
static u64            *mm_buffers_last_keys  = NULL;
static gpu_texture_t **mm_buffers_out        = NULL;
static i32             mm_buffers_stats[4]; // Rendered, found in the cache, failed (shaders), draws

// Pass properties

// Material previews (a 256 px sphere) need usable textures only, rendered at most this size when missing: the
// material's own sizes (2048 and more) cost up to seconds per preview on the iPad. 0: no cap. Painting still gets full
// textures (a recipe's texture is full only when rendered at the full size).
static i32 mm_buffers_size_cap = 0;

void mm_buffers_set_size_cap(i32 size) {
	mm_buffers_size_cap = size;
}

static bool mm_buffers_sync(void) { // Env MM_BUFFERS_SYNC: every pass rendered full by mm_buffers_render, as before
	static int sync = -1;
	if (sync < 0) {
		sync = getenv("MM_BUFFERS_SYNC") != NULL && getenv("MM_BUFFERS_SYNC")[0] != '0';
	}
	return sync == 1;
}

// What links need now: 0 the best texture there is (or none), else at least that tier, rendered now
i32 mm_buffers_require(i32 level) {
	i32 old         = mm_buffers_need;
	mm_buffers_need = level;
	return old;
}

static i32 mm_buffers_needed(void) {
	if (mm_buffers_sync()) {
		return MM_BUFFERS_FULL;
	}
	return mm_buffers_need > 0 ? mm_buffers_need : mm_buffers_size_cap > 0 ? MM_BUFFERS_TINY : 0;
}

static i32 mm_buffers_own(mm_pass_t *p) {
	return p->size < 1 ? 1 : p->size;
}

static i32 mm_buffers_res_cap = 0; // Tests: full sizes at most this (0: the project's resolution)

void mm_buffers_test_res(i32 res) {
	mm_buffers_res_cap = res;
}

static i32 mm_buffers_project_res(void) {
	if (mm_buffers_res_cap > 0) {
		return mm_buffers_res_cap;
	}
	i32 x = config_get_texture_res_x();
	i32 y = config_get_texture_res_y();
	return x > y ? x : y;
}

static bool mm_buffers_float32(void) {
	static int force = -2; // Env MM_FLOAT32=0/1 overrides (comparisons)
	if (force == -2) {
		char *e = getenv("MM_FLOAT32");
		force   = e == NULL ? -1 : e[0] == '1' ? 1 : 0;
	}
	if (force >= 0) {
		return force == 1;
	}
#ifdef IRON_METAL
	return gpu_float32_filtering_supported();
#else
	return true;
#endif
}

static gpu_texture_format_t mm_buffers_format(mm_buffers_recipe_t *r) {
	bool f32 = r->f32 && (mm_buffers_float32() || r->exact); // Unfilterable 32-bit floats: 16-bit, unless read with texelFetch
	if (r->greyscale) {
		return f32 ? GPU_TEXTURE_FORMAT_R32 : GPU_TEXTURE_FORMAT_R16;
	}
	return f32 ? GPU_TEXTURE_FORMAT_RGBA128 : GPU_TEXTURE_FORMAT_RGBA64;
}

// The full size of a pass: its own, at most the project's texture resolution and MM_BUFFERS_MAX_SIZE
static i32 mm_buffers_full_size(mm_buffers_recipe_t *r) {
	i32 s   = r->own > MM_BUFFERS_MAX_SIZE ? MM_BUFFERS_MAX_SIZE : r->own;
	i32 res = mm_buffers_project_res();
	s       = res > 0 && s > res ? res : s;
	return s < 1 ? 1 : s;
}

// The size a level renders at: full, low (at most MM_BUFFERS_LOW_SIZE and the size cap), tiny (the recipe's tiny size;
// a preview's size cap instead while one is set)
static i32 mm_buffers_level_size(mm_buffers_recipe_t *r, i32 level) {
	i32 s = mm_buffers_full_size(r);
	if (level >= MM_BUFFERS_FULL) {
		return s;
	}
	s = s > MM_BUFFERS_LOW_SIZE ? MM_BUFFERS_LOW_SIZE : s;
	if (mm_buffers_size_cap > 0) {
		return s > mm_buffers_size_cap ? mm_buffers_size_cap : s;
	}
	if (level <= MM_BUFFERS_TINY) {
		i32 t = r->tiny > 0 ? r->tiny : MM_BUFFERS_TINY_MAX;
		s     = s > t ? t : s;
	}
	return s;
}

// The recipe has a texture good for the level: any for tiny, low or better for low, a full one for full
static bool mm_buffers_sat(mm_buffers_recipe_t *r, i32 level) {
	if (r->tex == NULL) {
		return false;
	}
	if (level <= MM_BUFFERS_TINY || r->cpu) {
		return true;
	}
	if (level == MM_BUFFERS_LOW) {
		return r->tex_level >= MM_BUFFERS_LOW;
	}
	return r->tex_level >= MM_BUFFERS_FULL && r->tex_size == mm_buffers_full_size(r);
}

// The tier its texture is good for, 0 without one
static i32 mm_buffers_tier(mm_buffers_recipe_t *r) {
	return mm_buffers_sat(r, MM_BUFFERS_FULL) ? MM_BUFFERS_FULL : mm_buffers_sat(r, MM_BUFFERS_LOW) ? MM_BUFFERS_LOW : r->tex != NULL ? MM_BUFFERS_TINY : 0;
}

static bool mm_buffers_in_loop(mm_pass_t *p) {
	return p->loop_of >= 0 && p->texture_gen == NULL;
}

// What a pass's texture depends on: its textures, its loop textures (-1: itself), the iterate buffer it is in, the
// passes of its loop. Returns the count, -1 entries for none.
static i32 mm_buffers_deps(mm_plan_t *plan, i32 i, i32 *deps) {
	mm_pass_t *p = &plan->passes[i];
	i32        n = 0;
	for (i32 k = 0; k < p->textures; ++k) {
		deps[n++] = p->texture_pass[k];
	}
	for (i32 k = 0; p->iterate && k < p->loop_textures; ++k) {
		deps[n++] = p->loop_texture_pass[k] < 0 ? i : p->loop_texture_pass[k];
	}
	deps[n++] = p->loop_of;
	for (i32 j = 0; p->iterate && j < plan->count; ++j) {
		if (plan->passes[j].loop_of == i) {
			deps[n++] = j;
		}
	}
	return n;
}

static i32 mm_buffers_deps_max(mm_pass_t *p, i32 count) {
	return p->textures + p->loop_textures + 1 + count;
}

static void mm_buffers_visit(mm_plan_t *plan, i32 i, i32 *pos, i32 *order, i32 *n) {
	pos[i]        = *n;
	order[(*n)++] = i;
	i32 *deps     = malloc(sizeof(i32) * mm_buffers_deps_max(&plan->passes[i], plan->count));
	i32  nd       = mm_buffers_deps(plan, i, deps);
	for (i32 d = 0; d < nd; ++d) {
		if (deps[d] >= 0 && deps[d] < plan->count && pos[deps[d]] < 0) {
			mm_buffers_visit(plan, deps[d], pos, order, n);
		}
	}
	free(deps);
}

// The passes reachable from i in visiting order, each one's content and its dependencies by visiting position
static u64 mm_buffers_key(mm_plan_t *plan, u64 *content, i32 i, i32 *pos, i32 *order) {
	i32 n = 0;
	mm_buffers_visit(plan, i, pos, order, &n);
	u64 h = HASH_FNV1A_BASIS;
	for (i32 t = 0; t < n; ++t) {
		i32  j    = order[t];
		i32 *deps = malloc(sizeof(i32) * mm_buffers_deps_max(&plan->passes[j], plan->count));
		i32  nd   = mm_buffers_deps(plan, j, deps);
		h         = hash_fnv1a(h, &content[j], sizeof(u64));
		for (i32 d = 0; d < nd; ++d) {
			h = hash_fnv1a_i32(h, deps[d] >= 0 && deps[d] < plan->count ? pos[deps[d]] : -1);
		}
		free(deps);
	}
	for (i32 t = 0; t < n; ++t) {
		pos[order[t]] = -1;
	}
	return h != 0 ? h : 1; // 0: no input
}

static u64 mm_buffers_content(mm_pass_t *p) {
	u64 g   = hash_fnv1a_str(HASH_FNV1A_BASIS, p->glsl);
	u64 l   = hash_fnv1a_str(HASH_FNV1A_BASIS, p->iterate ? p->glsl_loop : NULL);
	u64 h   = hash_fnv1a(HASH_FNV1A_BASIS, &g, sizeof(u64));
	h       = hash_fnv1a(h, &l, sizeof(u64));
	i32 v[] = {p->texture_gen != NULL ? 0 : mm_buffers_own(p),
	           p->greyscale,
	           p->f32,
	           p->iterate,
	           p->iterate ? p->iterations : 0,
	           p->iterate && p->shrink,
	           p->textures,
	           p->iterate ? p->loop_textures : 0,
	           p->loop_of >= 0,
	           p->texture_gen != NULL,
	           p->exact};
	h       = hash_fnv1a(h, v, sizeof(v));
	if (p->texture_gen != NULL) { // What its pixels depend on: kind, parameters
		h = hash_fnv1a_str(h, mm_texture_key(p->texture_gen));
	}
	return h;
}

// Recipes

static mm_buffers_recipe_t *mm_buffers_find(u64 key) {
	for (mm_buffers_recipe_t *r = mm_buffers_table[key % MM_BUFFERS_BUCKETS]; r != NULL; r = r->next) {
		if (r->key == key) {
			return r;
		}
	}
	return NULL;
}

static i64 mm_buffers_bytes(gpu_texture_t *t) {
	return (i64)t->width * t->height * gpu_texture_format_size(t->format);
}

static void mm_buffers_texture_free(gpu_texture_t *t) {
	if (t != NULL) {
		gpu_texture_destroy(t); // Deferred until the gpu is done with it
		free(t);
	}
}

static u64 *mm_buffers_keys_copy(i32 n, i32 *passes, u64 *keys, i32 count, u64 self) {
	u64 *out = calloc(n + 1, sizeof(u64));
	for (i32 k = 0; k < n; ++k) {
		i32 j  = passes[k];
		out[k] = j < 0 ? self : j < count ? keys[j] : 0;
	}
	return out;
}

static void mm_buffers_downscale(uint8_t **rgba, i32 *w, i32 *h) {
	while (*w > MM_BUFFERS_MAX_CPU_SIZE || *h > MM_BUFFERS_MAX_CPU_SIZE) {
		i32      nw  = *w > 1 ? *w / 2 : 1;
		i32      nh  = *h > 1 ? *h / 2 : 1;
		uint8_t *src = *rgba;
		uint8_t *dst = malloc((size_t)nw * nh * 4);
		for (i32 y = 0; y < nh; ++y) {
			for (i32 x = 0; x < nw; ++x) {
				for (i32 c = 0; c < 4; ++c) {
					i32 x0 = x * 2 < *w ? x * 2 : *w - 1, x1 = x * 2 + 1 < *w ? x * 2 + 1 : *w - 1;
					i32 y0 = y * 2 < *h ? y * 2 : *h - 1, y1 = y * 2 + 1 < *h ? y * 2 + 1 : *h - 1;
					i32 s  = src[((size_t)y0 * *w + x0) * 4 + c] + src[((size_t)y0 * *w + x1) * 4 + c] + src[((size_t)y1 * *w + x0) * 4 + c] +
					        src[((size_t)y1 * *w + x1) * 4 + c];
					dst[((size_t)y * nw + x) * 4 + c] = (uint8_t)((s + 2) / 4);
				}
			}
		}
		free(src);
		*rgba = dst;
		*w    = nw;
		*h    = nh;
	}
}

// The pass's recipe, from the plan (the mm arena), with copies of all it needs
static mm_buffers_recipe_t *mm_buffers_recipe_new(mm_plan_t *plan, i32 i, u64 *keys) {
	mm_pass_t           *p = &plan->passes[i];
	mm_buffers_recipe_t *r = calloc(1, sizeof(mm_buffers_recipe_t));
	r->key                 = keys[i];
	r->name                = p->name != NULL ? strdup(p->name) : NULL;
	r->cpu                 = p->texture_gen != NULL;
	r->greyscale           = p->greyscale;
	r->f32                 = p->f32;
	r->exact               = p->exact;
	r->own                 = mm_buffers_own(p);
	r->in_loop             = mm_buffers_in_loop(p);
	r->bytes               = sizeof(mm_buffers_recipe_t);
	if (r->cpu) {
		uint8_t *rgba = NULL;
		i32      w = 1, h = 1;
		mm_texture_pixels(p->texture_gen, &w, &h, &rgba);
		if (rgba == NULL) {
			rgba = calloc(1, 4);
			w = h    = 1;
			rgba[3] = 255;
		}
		mm_buffers_downscale(&rgba, &w, &h);
		r->w           = w;
		r->h           = h;
		i32      len   = w * h * 4;
		buffer_t raw   = {.buffer = rgba, .length = len, .capacity = len};
		r->raw         = len < 1024;
		r->pixels      = r->raw ? buffer_create(len) : lz4_encode(&raw);
		if (r->raw || r->pixels == NULL) {
			r->raw    = true;
			r->pixels = buffer_create(len);
			memcpy(r->pixels->buffer, rgba, len);
		}
		r->bytes += r->pixels->length;
		free(rgba);
	}
	else {
		r->glsl       = p->glsl != NULL ? string_copy(p->glsl) : NULL;
		r->glsl_loop  = p->iterate && p->glsl_loop != NULL ? string_copy(p->glsl_loop) : NULL;
		r->glsl_hash  = hash_fnv1a_str(HASH_FNV1A_BASIS, p->glsl);
		r->loop_hash  = hash_fnv1a_str(HASH_FNV1A_BASIS, r->glsl_loop);
		r->iterate    = p->iterate;
		r->iterations = p->iterate ? p->iterations : 0;
		r->shrink     = p->iterate && p->shrink;
		r->textures   = p->textures;
		r->inputs     = mm_buffers_keys_copy(p->textures, p->texture_pass, keys, plan->count, 0);
		r->owner      = r->in_loop && p->loop_of < plan->count ? keys[p->loop_of] : 0;
		r->bytes += (r->glsl != NULL ? strlen(r->glsl) : 0) + (r->glsl_loop != NULL ? strlen(r->glsl_loop) : 0) + sizeof(u64) * p->textures;
		if (p->iterate) {
			r->loop_textures = p->loop_textures;
			r->loop_inputs   = mm_buffers_keys_copy(p->loop_textures, p->loop_texture_pass, keys, plan->count, keys[i]);
			r->loop          = calloc(plan->count + 1, sizeof(u64));
			for (i32 j = 0; j < plan->count; ++j) {
				if (plan->passes[j].loop_of == i && plan->passes[j].texture_gen == NULL) {
					r->loop[r->nloop++] = keys[j];
				}
			}
			r->bytes += sizeof(u64) * (p->loop_textures + r->nloop);
		}
	}
	u64 b                  = r->key % MM_BUFFERS_BUCKETS;
	r->next                = mm_buffers_table[b];
	mm_buffers_table[b]    = r;
	mm_buffers_nrecipes++;
	return r;
}

static void mm_buffers_recipe_free(mm_buffers_recipe_t *r) {
	mm_buffers_texture_free(r->tex);
	free(r->name);
	free(r->glsl);
	free(r->glsl_loop);
	free(r->inputs);
	free(r->loop_inputs);
	free(r->loop);
	if (r->pixels != NULL) {
		iron_delete_blob(r->pixels);
	}
	free(r);
}

// The recipe and those of every pass it reads are needed
static void mm_buffers_touch(mm_buffers_recipe_t *r) {
	if (r == NULL || r->recipe_used == mm_buffers_gen) {
		return;
	}
	r->recipe_used = mm_buffers_gen;
	for (i32 k = 0; k < r->textures; ++k) {
		mm_buffers_touch(mm_buffers_find(r->inputs[k]));
	}
	for (i32 k = 0; k < r->loop_textures; ++k) {
		mm_buffers_touch(mm_buffers_find(r->loop_inputs[k]));
	}
	for (i32 k = 0; k < r->nloop; ++k) {
		mm_buffers_touch(mm_buffers_find(r->loop[k]));
	}
	if (r->owner != 0) {
		mm_buffers_touch(mm_buffers_find(r->owner));
	}
}

// Eviction

static i64 mm_buffers_budget_kb = 0; // mm_buffers_test_budget: 0 for MM_BUFFERS_BUDGET_MB

// The texture budget: a share of the device's physical memory (a 4 GB iPad keeps less than a 16 GB one), on iOS also at
// most a third of what the system still lets the app use, checked each time (it shrinks as layers and other memory grow)
static i64 mm_buffers_budget(void) {
	if (mm_buffers_budget_kb > 0) {
		return mm_buffers_budget_kb * 1024;
	}
	const i64  mb   = 1024 * 1024;
	static i64 base = 0;
	if (base == 0) {
		uint64_t ram = 0;
#ifdef __APPLE__
		size_t len = sizeof(ram);
		if (sysctlbyname("hw.memsize", &ram, &len, NULL, 0) != 0) {
			ram = 0;
		}
#endif
		base = ram > 0 ? (i64)(ram / MM_BUFFERS_RAM_SHARE) : (i64)MM_BUFFERS_BUDGET_MB * mb;
		base = base < MM_BUFFERS_BUDGET_MIN_MB * mb ? MM_BUFFERS_BUDGET_MIN_MB * mb : base;
		base = base > MM_BUFFERS_BUDGET_MAX_MB * mb ? MM_BUFFERS_BUDGET_MAX_MB * mb : base;
	}
	i64 budget = base;
#ifdef IRON_IOS
	if (__builtin_available(iOS 13.0, *)) {
		i64 avail = (i64)os_proc_available_memory(); // 0 when unknown
		if (avail > 0 && budget > avail / 3) {
			budget = avail / 3 > 64 * mb ? avail / 3 : 64 * mb;
		}
	}
#endif
	return budget;
}

static void mm_buffers_scratch_free(void);

// iOS memory warning (base_on_memory_warning): frees every texture the last render did not use. Recipes stay, a texture
// needed again renders again from its recipe.
void mm_buffers_trim(void) {
	for (i32 b = 0; b < MM_BUFFERS_BUCKETS; ++b) {
		for (mm_buffers_recipe_t *r = mm_buffers_table[b]; r != NULL; r = r->next) {
			if (r->tex != NULL && r->used < mm_buffers_gen && !r->rendering && !r->job && !r->queued[0] && !r->queued[1] && !r->queued[2]) {
				mm_buffers_texture_free(r->tex);
				r->tex = NULL;
			}
		}
	}
	mm_buffers_scratch_free();
}

// Queued and in progress recipes stay, and their textures: rendering them again would not end
static void mm_buffers_keep_pending(void) {
	for (i32 b = 0; b < MM_BUFFERS_BUCKETS; ++b) {
		for (mm_buffers_recipe_t *r = mm_buffers_table[b]; r != NULL; r = r->next) {
			if (r->job || r->job_cur != NULL || r->queued[0] || r->queued[1] || r->queued[2] || r->rendering) {
				r->used = mm_buffers_gen;
				mm_buffers_touch(r); // Its inputs' recipes too
			}
		}
	}
}

// Tests: a texture budget in KB (eviction without rendering hundreds of MB), 0 restores the default
void mm_buffers_test_budget(i32 kb) {
	mm_buffers_budget_kb = kb;
}

static int mm_buffers_cmp_i32(const void *a, const void *b) {
	i32 x = *(const i32 *)a;
	i32 y = *(const i32 *)b;
	return x < y ? -1 : x > y;
}

static void mm_buffers_workers_drain(void);

static void mm_buffers_evict(bool all) {
	// Textures stay while they fit the budget: a material compiles into several plans (paint, preview, mesh) and other
	// materials render in between, freeing whatever a render did not use made the same passes render again and again.
	// Over budget, the least recently used go first, never the last render's.
	if (!all) {
		mm_buffers_keep_pending();
	}
	i64 total = 0, recipes = 0;
	i32 oldest_kept = INT32_MAX;
	for (i32 b = 0; b < MM_BUFFERS_BUCKETS; ++b) {
		for (mm_buffers_recipe_t *r = mm_buffers_table[b]; r != NULL; r = r->next) {
			if (r->tex != NULL) {
				total += mm_buffers_bytes(r->tex);
				oldest_kept = r->used < oldest_kept ? r->used : oldest_kept;
			}
			recipes += r->bytes;
		}
	}
	while (total > mm_buffers_budget() && oldest_kept < mm_buffers_gen) {
		i32 next = mm_buffers_gen; // The next older generation still holding textures
		for (i32 b = 0; b < MM_BUFFERS_BUCKETS; ++b) {
			for (mm_buffers_recipe_t *r = mm_buffers_table[b]; r != NULL; r = r->next) {
				if (r->tex == NULL) {
					continue;
				}
				if (r->used == oldest_kept) {
					total -= mm_buffers_bytes(r->tex);
				}
				else if (r->used > oldest_kept && r->used < next) {
					next = r->used;
				}
			}
		}
		oldest_kept = next;
	}
	// Recipes over budget: the least recently needed go
	i32 oldest_recipe = INT32_MIN;
	while (recipes > (i64)MM_BUFFERS_RECIPE_BUDGET_MB * 1024 * 1024) {
		i32 min = INT32_MAX;
		for (i32 b = 0; b < MM_BUFFERS_BUCKETS; ++b) {
			for (mm_buffers_recipe_t *r = mm_buffers_table[b]; r != NULL; r = r->next) {
				if (r->recipe_used >= oldest_recipe && r->recipe_used < min) {
					min = r->recipe_used;
				}
			}
		}
		if (min >= mm_buffers_gen) {
			break;
		}
		for (i32 b = 0; b < MM_BUFFERS_BUCKETS; ++b) {
			for (mm_buffers_recipe_t *r = mm_buffers_table[b]; r != NULL; r = r->next) {
				if (r->recipe_used == min) {
					recipes -= r->bytes;
				}
			}
		}
		oldest_recipe = min + 1;
	}
	for (i32 b = 0; b < MM_BUFFERS_BUCKETS; ++b) {
		mm_buffers_recipe_t **link = &mm_buffers_table[b];
		while (*link != NULL) {
			mm_buffers_recipe_t *r = *link;
			if (all || r->recipe_used < oldest_recipe) {
				*link = r->next;
				mm_buffers_recipe_free(r);
				mm_buffers_nrecipes--;
				continue;
			}
			if (r->tex != NULL && r->used < oldest_kept) {
				mm_buffers_texture_free(r->tex);
				r->tex = NULL;
			}
			link = &r->next;
		}
	}
	// Pipelines past MM_BUFFERS_MAX_PIPES: the least recently used go (compiling one again translates its GLSL and builds
	// the pipeline, tens of ms on the iPad)
	i32 oldest_pipe = INT32_MIN;
	if (!all && mm_buffers_npipes > MM_BUFFERS_MAX_PIPES) {
		i32 *used = malloc(sizeof(i32) * mm_buffers_npipes);
		for (i32 i = 0; i < mm_buffers_npipes; ++i) {
			used[i] = mm_buffers_pipes[i]->used;
		}
		qsort(used, mm_buffers_npipes, sizeof(i32), mm_buffers_cmp_i32);
		oldest_pipe = used[mm_buffers_npipes - MM_BUFFERS_MAX_PIPES]; // Kept: used at least this recently
		oldest_pipe = oldest_pipe > mm_buffers_gen ? mm_buffers_gen : oldest_pipe;
		free(used);
	}
	if (all) {
		mm_buffers_workers_drain();
	}
	i32 n = 0;
	for (i32 i = 0; i < mm_buffers_npipes; ++i) {
		mm_buffers_pipe_t *p = mm_buffers_pipes[i];
		if ((all || p->used < oldest_pipe) && atomic_load(&p->state) != MM_PIPE_QUEUED) {
			free(p->glsl);
			if (p->ok) {
				gpu_pipeline_destroy(&p->pipe);
			}
			if (p->vs != NULL) {
				gpu_delete_shader(p->vs);
			}
			if (p->fs != NULL) {
				gpu_delete_shader(p->fs);
			}
			free(p);
		}
		else {
			mm_buffers_pipes[n++] = p;
		}
	}
	mm_buffers_npipes = n;
}
