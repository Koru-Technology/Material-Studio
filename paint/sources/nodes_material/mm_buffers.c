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

#include "../global.h"
#include "../material_maker/mm.h"
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

// Hashing (FNV-1a 64)

#define MM_BUFFERS_SEED 0xcbf29ce484222325ULL

static u64 mm_buffers_hash(u64 h, const void *data, size_t n) {
	const uint8_t *p = data;
	for (size_t i = 0; i < n; ++i) {
		h ^= p[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

static u64 mm_buffers_hash_i32(u64 h, i32 v) {
	return mm_buffers_hash(h, &v, sizeof(v));
}

static u64 mm_buffers_hash_str(u64 h, const char *s) {
	if (s == NULL) {
		return mm_buffers_hash_i32(h, -1);
	}
	size_t n = strlen(s);
	return mm_buffers_hash_i32(mm_buffers_hash(h, s, n), (i32)n);
}

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

static i32 mm_buffers_project_res(void) {
	i32 x = config_get_texture_res_x();
	i32 y = config_get_texture_res_y();
	return x > y ? x : y;
}

static bool mm_buffers_float32(void) {
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
	u64 h = MM_BUFFERS_SEED;
	for (i32 t = 0; t < n; ++t) {
		i32  j    = order[t];
		i32 *deps = malloc(sizeof(i32) * mm_buffers_deps_max(&plan->passes[j], plan->count));
		i32  nd   = mm_buffers_deps(plan, j, deps);
		h         = mm_buffers_hash(h, &content[j], sizeof(u64));
		for (i32 d = 0; d < nd; ++d) {
			h = mm_buffers_hash_i32(h, deps[d] >= 0 && deps[d] < plan->count ? pos[deps[d]] : -1);
		}
		free(deps);
	}
	for (i32 t = 0; t < n; ++t) {
		pos[order[t]] = -1;
	}
	return h != 0 ? h : 1; // 0: no input
}

static u64 mm_buffers_content(mm_pass_t *p) {
	u64 g   = mm_buffers_hash_str(MM_BUFFERS_SEED, p->glsl);
	u64 l   = mm_buffers_hash_str(MM_BUFFERS_SEED, p->iterate ? p->glsl_loop : NULL);
	u64 h   = mm_buffers_hash(MM_BUFFERS_SEED, &g, sizeof(u64));
	h       = mm_buffers_hash(h, &l, sizeof(u64));
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
	h       = mm_buffers_hash(h, v, sizeof(v));
	if (p->texture_gen != NULL) { // What its pixels depend on: kind, parameters
		h = mm_buffers_hash_str(h, mm_texture_key(p->texture_gen));
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
		r->glsl_hash  = mm_buffers_hash_str(MM_BUFFERS_SEED, p->glsl);
		r->loop_hash  = mm_buffers_hash_str(MM_BUFFERS_SEED, r->glsl_loop);
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

// Pipelines, cached like textures

static void mm_buffers_pipe_shaders(mm_buffers_pipe_t *p, char *vs, char *fs, i32 vs_size, i32 fs_size) {
	p->vs = gpu_create_shader_from_source(vs, vs_size, GPU_SHADER_TYPE_VERTEX);
	p->fs = gpu_create_shader_from_source(fs, fs_size, GPU_SHADER_TYPE_FRAGMENT);
	gpu_pipeline_init(&p->pipe);
	p->pipe.input_layout           = &mm_buffers_structure;
	p->pipe.vertex_shader          = p->vs;
	p->pipe.fragment_shader        = p->fs;
	p->pipe.color_attachment_count = 1;
	p->pipe.color_attachment[0]    = p->format;
}

// The pipeline of a pass: found, or made new, nothing translated or compiled yet (mm_buffers_pipe_make, on any thread)
static mm_buffers_pipe_t *mm_buffers_pipe_get(char *glsl, u64 glsl_hash, gpu_texture_format_t format) {
	u64 key = mm_buffers_hash_i32(glsl_hash, (i32)format);
	for (i32 i = 0; i < mm_buffers_npipes; ++i) {
		if (mm_buffers_pipes[i]->key == key) {
			mm_buffers_pipes[i]->used = mm_buffers_gen;
			return mm_buffers_pipes[i];
		}
	}
	if (mm_buffers_structure.size == 0) {
		gpu_vertex_structure_add(&mm_buffers_structure, "pos", GPU_VERTEX_DATA_F32_2X);
	}
	mm_buffers_pipe_t *p = calloc(1, sizeof(mm_buffers_pipe_t));
	p->key               = key;
	p->used              = mm_buffers_gen;
	p->format            = format;
	atomic_init(&p->state, MM_PIPE_NEW);
#ifdef WITH_GLSL
	p->glsl = glsl != NULL ? string_copy(glsl) : NULL; // Translated by mm_buffers_pipe_make (thread safe)
#else
	if (glsl != NULL) { // The translator is not thread safe here: shaders made now, the pipeline compiles later
		char *vs      = NULL;
		char *fs      = NULL;
		i32   vs_size = 0;
		i32   fs_size = 0;
		gpu_create_shaders_from_glsl(glsl, &vs, &fs, &vs_size, &fs_size);
		if (vs != NULL && fs != NULL && (vs_size > 0 || vs[0] != '\0') && (fs_size > 0 || fs[0] != '\0')) {
			mm_buffers_pipe_shaders(p, vs, fs, vs_size, fs_size);
		}
	}
#endif
	mm_buffers_pipes                      = realloc(mm_buffers_pipes, sizeof(mm_buffers_pipe_t *) * (mm_buffers_npipes + 1));
	mm_buffers_pipes[mm_buffers_npipes++] = p;
	return p;
}

// Translates the GLSL, makes the shaders, compiles the pipeline: on a worker thread or this one
static void mm_buffers_pipe_make(mm_buffers_pipe_t *p) {
#ifdef WITH_GLSL
	if (p->glsl != NULL) {
		char *vs = NULL;
		char *fs = NULL;
		if (iron_glsl_translate(p->glsl, &vs, &fs)) {
			mm_buffers_pipe_shaders(p, vs, fs, 0, 0);
		}
		free(vs);
		free(fs);
		free(p->glsl);
		p->glsl = NULL;
	}
#endif
	if (p->vs != NULL && p->fs != NULL) {
		gpu_pipeline_compile(&p->pipe);
		p->ok = p->pipe.impl.pipeline != NULL;
	}
	atomic_store(&p->state, MM_PIPE_READY);
}

static bool mm_buffers_pipe_ready(mm_buffers_pipe_t *p) {
	return atomic_load(&p->state) == MM_PIPE_READY;
}

// Worker threads make the pipelines requested, they end when there are none left (mm_buffers_workers_start starts them
// again). A pipeline needed at once is made on this thread when no worker took it yet, waited for otherwise.
#define MM_BUFFERS_WORKERS 4

static iron_mutex_t        mm_buffers_jobs_mutex;
static bool                mm_buffers_jobs_init = false;
static mm_buffers_pipe_t **mm_buffers_jobs      = NULL;
static i32                 mm_buffers_njobs     = 0;
static i32                 mm_buffers_jobs_cap  = 0;
static iron_thread_t       mm_buffers_worker_threads[MM_BUFFERS_WORKERS];
static bool                mm_buffers_worker_alive[MM_BUFFERS_WORKERS];
static atomic_bool         mm_buffers_worker_done[MM_BUFFERS_WORKERS];

static void mm_buffers_jobs_lock(void) {
	if (!mm_buffers_jobs_init) {
		iron_mutex_init(&mm_buffers_jobs_mutex);
		mm_buffers_jobs_init = true;
	}
	iron_mutex_lock(&mm_buffers_jobs_mutex);
}

static mm_buffers_pipe_t *mm_buffers_jobs_pop(void) {
	mm_buffers_jobs_lock();
	mm_buffers_pipe_t *p = NULL;
	if (mm_buffers_njobs > 0) {
		p = mm_buffers_jobs[0];
		memmove(mm_buffers_jobs, mm_buffers_jobs + 1, sizeof(mm_buffers_pipe_t *) * (mm_buffers_njobs - 1));
		mm_buffers_njobs--;
	}
	iron_mutex_unlock(&mm_buffers_jobs_mutex);
	return p;
}

static void mm_buffers_worker(void *param) {
	i32 i = (i32)(size_t)param;
#ifdef __APPLE__
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0); // The material waits for them: performance cores
#endif
	while (true) {
		mm_buffers_pipe_t *p = mm_buffers_jobs_pop();
		if (p == NULL) {
			break;
		}
		mm_buffers_pipe_make(p);
	}
	atomic_store(&mm_buffers_worker_done[i], true);
}

static i32 mm_buffers_workers_max(void) {
	i32 n = iron_hardware_threads() - 1;
	n     = n > MM_BUFFERS_WORKERS ? MM_BUFFERS_WORKERS : n;
	return n < 1 ? 1 : n;
}

// Joins the workers that ended, starts workers for the pipelines waiting
static void mm_buffers_workers_start(void) {
	i32 alive = 0;
	for (i32 i = 0; i < MM_BUFFERS_WORKERS; ++i) {
		if (mm_buffers_worker_alive[i] && atomic_load(&mm_buffers_worker_done[i])) {
			iron_thread_wait_and_destroy(&mm_buffers_worker_threads[i]);
			mm_buffers_worker_alive[i] = false;
		}
		alive += mm_buffers_worker_alive[i];
	}
	mm_buffers_jobs_lock();
	i32 waiting = mm_buffers_njobs;
	iron_mutex_unlock(&mm_buffers_jobs_mutex);
	i32 max = mm_buffers_workers_max();
	for (i32 i = 0; i < MM_BUFFERS_WORKERS && alive < max && alive < waiting; ++i) {
		if (!mm_buffers_worker_alive[i]) {
			atomic_store(&mm_buffers_worker_done[i], false);
			mm_buffers_worker_alive[i] = true;
			iron_thread_init(&mm_buffers_worker_threads[i], mm_buffers_worker, (void *)(size_t)i);
			alive++;
		}
	}
}

// The pipeline is made on a worker
static void mm_buffers_pipe_request(mm_buffers_pipe_t *p) {
	if (atomic_load(&p->state) != MM_PIPE_NEW) {
		return;
	}
	atomic_store(&p->state, MM_PIPE_QUEUED);
	mm_buffers_jobs_lock();
	if (mm_buffers_njobs == mm_buffers_jobs_cap) {
		mm_buffers_jobs_cap = mm_buffers_jobs_cap * 2 + 16;
		mm_buffers_jobs     = realloc(mm_buffers_jobs, sizeof(mm_buffers_pipe_t *) * mm_buffers_jobs_cap);
	}
	mm_buffers_jobs[mm_buffers_njobs++] = p;
	iron_mutex_unlock(&mm_buffers_jobs_mutex);
}

// The pipeline, made now: here when no worker took it, waited for otherwise
static void mm_buffers_pipe_wait(mm_buffers_pipe_t *p) {
	if (mm_buffers_pipe_ready(p)) {
		return;
	}
	bool mine = atomic_load(&p->state) == MM_PIPE_NEW;
	if (!mine) { // Queued: taken back unless a worker has it
		mm_buffers_jobs_lock();
		for (i32 i = 0; i < mm_buffers_njobs; ++i) {
			if (mm_buffers_jobs[i] == p) {
				memmove(mm_buffers_jobs + i, mm_buffers_jobs + i + 1, sizeof(mm_buffers_pipe_t *) * (mm_buffers_njobs - i - 1));
				mm_buffers_njobs--;
				mine = true;
				break;
			}
		}
		iron_mutex_unlock(&mm_buffers_jobs_mutex);
	}
	if (mine) {
		mm_buffers_pipe_make(p);
		return;
	}
	while (!mm_buffers_pipe_ready(p)) {
#ifdef _WIN32
		Sleep(1);
#else
		usleep(500);
#endif
	}
}

// Every pipeline requested made, the workers joined
static void mm_buffers_workers_drain(void) {
	while (true) {
		mm_buffers_pipe_t *p = mm_buffers_jobs_pop();
		if (p == NULL) {
			break;
		}
		mm_buffers_pipe_make(p);
	}
	for (i32 i = 0; i < MM_BUFFERS_WORKERS; ++i) {
		if (mm_buffers_worker_alive[i]) {
			iron_thread_wait_and_destroy(&mm_buffers_worker_threads[i]);
			mm_buffers_worker_alive[i] = false;
		}
	}
}

static mm_buffers_pipe_t *mm_buffers_pipeline(char *glsl, u64 glsl_hash, gpu_texture_format_t format) {
	mm_buffers_pipe_t *p = mm_buffers_pipe_get(glsl, glsl_hash, format);
	mm_buffers_pipe_wait(p);
	return p;
}

// Compiling a material's pipelines one after another as its passes render made the main thread wait on Metal's compiler
// for each in turn (seconds for library materials the first time after an install, ~85 ms each on the iPad). The passes
// a render will draw get their pipelines compiled together on worker threads first.
static gpu_texture_format_t mm_buffers_format(mm_buffers_recipe_t *r);

// The recipe's pipelines (its pass, its loop): requested from the workers, true when all are ready
static bool mm_buffers_recipe_pipes(mm_buffers_recipe_t *r) {
	if (r->cpu) {
		return true;
	}
	bool ready = true;
	for (i32 k = 0; k < (r->iterate ? 2 : 1); ++k) {
		mm_buffers_pipe_t *p = mm_buffers_pipe_get(k == 0 ? r->glsl : r->glsl_loop, k == 0 ? r->glsl_hash : r->loop_hash, mm_buffers_format(r));
		mm_buffers_pipe_request(p);
		ready = ready && mm_buffers_pipe_ready(p);
	}
	return ready;
}

// A unit's pipelines: its own and its loop passes'
static bool mm_buffers_unit_pipes(mm_buffers_recipe_t *u) {
	bool ready = mm_buffers_recipe_pipes(u);
	for (i32 j = 0; j < u->nloop; ++j) {
		mm_buffers_recipe_t *l = mm_buffers_find(u->loop[j]);
		if (l != NULL) {
			ready = mm_buffers_recipe_pipes(l) && ready;
		}
	}
	return ready;
}

// The passes a synchronous render will draw get their pipelines made together first, on the workers and this thread
static void mm_buffers_precompile(mm_buffers_recipe_t **rs, i32 count) {
	for (i32 i = 0; i < count; ++i) {
		if (!mm_buffers_sat(rs[i], MM_BUFFERS_FULL)) {
			mm_buffers_recipe_pipes(rs[i]);
		}
	}
	mm_buffers_workers_start();
	mm_buffers_workers_drain(); // This thread makes its share
}

static mm_buffers_pipe_t *mm_buffers_pipe_of(mm_buffers_recipe_t *r, bool loop) {
	mm_buffers_pipe_t *p = mm_buffers_pipeline(loop ? r->glsl_loop : r->glsl, loop ? r->loop_hash : r->glsl_hash, mm_buffers_format(r));
	if (!p->ok) {
		r->failed = true;
	}
	return p;
}

// Scratch targets: a pass renders into one, iterate buffers ping-pong between them

static gpu_texture_t *mm_buffers_target(gpu_texture_format_t format, i32 size) {
	for (i32 i = 0; i < mm_buffers_nscratch; ++i) {
		mm_buffers_scratch_t *s = &mm_buffers_scratch[i];
		if (!s->busy && s->tex->format == format && (i32)s->tex->width == size) {
			s->busy = true;
			return s->tex;
		}
	}
	gpu_texture_t *t = calloc(1, sizeof(gpu_texture_t));
	gpu_render_target_init(t, size, size, format);
	mm_buffers_scratch = realloc(mm_buffers_scratch, sizeof(mm_buffers_scratch_t) * (mm_buffers_nscratch + 1));
	mm_buffers_scratch[mm_buffers_nscratch++] = (mm_buffers_scratch_t){.tex = t, .busy = true};
	return t;
}

static void mm_buffers_release(gpu_texture_t *t) {
	for (i32 i = 0; i < mm_buffers_nscratch; ++i) {
		if (mm_buffers_scratch[i].tex == t) {
			mm_buffers_scratch[i].busy = false;
		}
	}
}

// The texture becomes a recipe's
static void mm_buffers_detach(gpu_texture_t *t) {
	for (i32 i = 0; i < mm_buffers_nscratch; ++i) {
		if (mm_buffers_scratch[i].tex == t) {
			mm_buffers_scratch[i] = mm_buffers_scratch[--mm_buffers_nscratch];
			return;
		}
	}
}

// Frees the scratch targets not in use (busy ones: an iterate buffer's render in progress)
static void mm_buffers_scratch_free(void) {
	i32 n = 0;
	for (i32 i = 0; i < mm_buffers_nscratch; ++i) {
		if (mm_buffers_scratch[i].busy) {
			mm_buffers_scratch[n++] = mm_buffers_scratch[i];
		}
		else {
			mm_buffers_texture_free(mm_buffers_scratch[i].tex);
		}
	}
	mm_buffers_nscratch = n;
}

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
	gpu_texture_init_from_bytes(t, px->buffer, r->w, r->h, GPU_TEXTURE_FORMAT_RGBA32, false);
	if (px != r->pixels) {
		iron_delete_blob(px);
	}
	return t;
}

// GPU cost: ms per Mpx drawn, measured per unit (mm_buffers_learn), the average for units not measured yet
static double mm_buffers_cost_default = 25.0; // Material Maker passes measured 10 to 40 on the iPad, more for some

static void mm_buffers_commit(u64 key, double px, bool src);

static double mm_buffers_cost(mm_buffers_recipe_t *u) {
	return u->cost > 0 ? u->cost : mm_buffers_cost_default;
}

// An iterate buffer's loop source draws (its iterations) cost their own: its first draw is often far cheaper, one cost
// for both made the steps underestimate the iterations (hundreds of ms of GPU in one frame on the iPad)
static double mm_buffers_cost_src(mm_buffers_recipe_t *u) {
	return u->cost_src > 0 ? u->cost_src : mm_buffers_cost(u);
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
		double cost = src ? mm_buffers_cost_src(u) : mm_buffers_cost(r);
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
			mm_buffers_commit(r->key, (double)size * rows, src); // learn its cost (small ones go with the next)
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
} mm_buffers_slot_t;

static mm_buffers_slot_t mm_buffers_slots[MM_BUFFERS_SLOTS];

static void mm_buffers_measured(void *data, double ms) {
	mm_buffers_slot_t *s = data;
	s->ms                = ms;
	atomic_store(&s->state, 2);
}

// Submits the draws recorded so far, timed for the unit when key is not 0
static void mm_buffers_commit(u64 key, double px, bool src) {
#ifdef IRON_METAL
	for (i32 i = 0; key != 0 && px > 0 && i < MM_BUFFERS_SLOTS; ++i) {
		mm_buffers_slot_t *s = &mm_buffers_slots[i];
		if (atomic_load(&s->state) == 0) {
			s->key = key;
			s->px  = px;
			s->src = src;
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
			         s->src ? " (iteration)" : "", s->ms, s->px / 1e6, r != NULL ? (s->src ? mm_buffers_cost_src(r) : mm_buffers_cost(r)) : -1.0);
		}
		if (s->ms > 0 && s->px >= 0.05e6) { // Small draws measure their overhead more than their pixels
			double               c = s->ms / (s->px / 1e6);
			mm_buffers_recipe_t *r = mm_buffers_find(s->key);
			f32                 *k = r == NULL ? NULL : s->src ? &r->cost_src : &r->cost;
			if (k != NULL) {
				*k = *k > 0 ? (f32)(0.5 * *k + 0.5 * c) : (f32)c;
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
			mm_buffers_commit(0, 0, false); // The frame's work so far, apart from the timed draws
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
		ms += draws * (mm_buffers_cost(r) * (double)size * size / 1e6 + MM_BUFFERS_DRAW_MS);
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
#ifdef _WIN32
			Sleep(1);
#else
			usleep(500);
#endif
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
// (MM_BUFFERS_SYNC)
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

// Tests: renders a library material (its id in library.pack) or a .ptex file, reads back its maps

static float *mm_buffers_test_maps[MM_MAP_COUNT];
static i32    mm_buffers_test_sizes[MM_MAP_COUNT];
static char  *mm_buffers_test_links[MM_MAP_COUNT];
static bool   mm_buffers_test_grey[MM_MAP_COUNT];

static float mm_buffers_half(uint16_t h) {
	u32 sign = (u32)(h >> 15) << 31;
	i32 exp  = (h >> 10) & 0x1f;
	u32 man  = h & 0x3ff;
	u32 bits;
	if (exp == 0) {
		if (man == 0) {
			bits = sign;
		}
		else { // Subnormal
			exp = 127 - 15 + 1;
			while ((man & 0x400) == 0) {
				man <<= 1;
				exp--;
			}
			bits = sign | ((u32)exp << 23) | ((man & 0x3ff) << 13);
		}
	}
	else if (exp == 31) {
		bits = sign | 0x7f800000 | (man << 13);
	}
	else {
		bits = sign | ((u32)(exp - 15 + 127) << 23) | (man << 13);
	}
	float f;
	memcpy(&f, &bits, 4);
	return f;
}

// RGBA floats, greyscale as (r, r, r, 1) as the checker reads maps
static float *mm_buffers_read(gpu_texture_t *t, bool greyscale) {
	i32      n     = t->width * t->height;
	i32      fsize = gpu_texture_format_size(t->format);
	uint8_t *raw   = malloc((size_t)n * fsize);
	gpu_get_render_target_pixels(t, raw);
	float *out = malloc(sizeof(float) * 4 * n);
	for (i32 i = 0; i < n; ++i) {
		float v[4] = {0, 0, 0, 1};
		if (t->format == GPU_TEXTURE_FORMAT_R16) {
			v[0] = mm_buffers_half(((uint16_t *)raw)[i]);
		}
		else if (t->format == GPU_TEXTURE_FORMAT_R32) {
			v[0] = ((float *)raw)[i];
		}
		else if (t->format == GPU_TEXTURE_FORMAT_RGBA64) {
			for (i32 c = 0; c < 4; ++c) {
				v[c] = mm_buffers_half(((uint16_t *)raw)[i * 4 + c]);
			}
		}
		else if (t->format == GPU_TEXTURE_FORMAT_RGBA128) {
			for (i32 c = 0; c < 4; ++c) {
				v[c] = ((float *)raw)[i * 4 + c];
			}
		}
		else {
			for (i32 c = 0; c < 4; ++c) {
				v[c] = raw[i * fsize + c] / 255.0f;
			}
		}
		if (greyscale) {
			v[1] = v[2] = v[0];
			v[3]        = 1.0f;
		}
		memcpy(&out[i * 4], v, sizeof(v));
	}
	free(raw);
	return out;
}

static void mm_buffers_test_set_map(i32 k, gpu_texture_t *t) {
	free(mm_buffers_test_maps[k]);
	mm_buffers_test_maps[k]  = t != NULL ? mm_buffers_read(t, mm_buffers_test_grey[k]) : NULL;
	mm_buffers_test_sizes[k] = t != NULL ? t->width : 0;
}

static mm_pack_t *mm_buffers_library(void) {
	static mm_pack_t *library = NULL;
	if (library == NULL) {
		buffer_t *blob = data_get_blob("material_maker/library.pack"); // Kept: the pack points into it
		library        = blob != NULL ? mm_pack_open(blob->buffer, blob->length) : NULL;
	}
	return library;
}

// Returns a summary, "error: ..." on failure. size: log2 of the maps' resolution, 0 for the material's own. other: the
// last render's maps and links stay (a render that evicts them).
static char *mm_buffers_test_run(char *path_or_id, i32 size, bool other) {
	mm_nodes_init();
	mm_buffers_init();
	for (i32 m = 0; !other && m < MM_MAP_COUNT; ++m) {
		mm_buffers_test_grey[m] = false;
		mm_buffers_test_set_map(m, NULL);
		free(mm_buffers_test_links[m]);
		mm_buffers_test_links[m] = NULL;
	}
	char  *ptex = NULL;
	size_t len  = 0;
	if (ends_with(path_or_id, ".ptex")) {
		ptex = (char *)mm_buffers_read_file(path_or_id, &len);
	}
	else {
		mm_pack_t *library = mm_buffers_library();
		i32        id      = atoi(path_or_id);
		for (i32 i = 0; library != NULL && i < mm_pack_count(library); ++i) {
			if ((i32)mm_json_num(mm_json_get(mm_pack_entry(library, i), "id"), -1) == id) {
				ptex = mm_pack_read(library, i, &len);
				break;
			}
		}
	}
	if (ptex == NULL) {
		return string("error: %s not found", path_or_id);
	}
	mm_mark_t      mark = mm_arena_mark();
	mm_material_t *m    = mm_load(ptex, len);
	free(ptex);
	if (m == NULL || m->error != NULL) {
		char *e = string("error: %s", m != NULL ? m->error : "load");
		mm_arena_release(mark);
		return e;
	}
	mm_plan_t *plan = mm_plan(m, size);
	if (plan->error != NULL) {
		char *e = string("error: %s", plan->error);
		mm_arena_release(mark);
		return e;
	}
	f64 t0            = iron_time();
	mm_buffers_forced = true; // Rendered full now, as the checker's maps are
	gpu_texture_t **tex = mm_buffers_render(plan);
	mm_buffers_forced   = false;
	f64 ms              = (iron_time() - t0) * 1000.0;
	i32 need            = mm_buffers_require(MM_BUFFERS_FULL); // Links resolve to full textures
	// Every texture is found again by its link, through the app's texture link resolution
	i32 links = 0, bad_links = 0;
	for (i32 i = 0; i < plan->count; ++i) {
		char *link = mm_buffers_link(plan, i);
		if (link == NULL || tex[i] == NULL) {
			continue;
		}
		links++;
		bad_links += uniforms_ext_tex_link(NULL, NULL, link) != tex[i];
	}
	i32 maps = 0;
	for (i32 k = 0; !other && k < MM_MAP_COUNT; ++k) {
		i32 pi = plan->map_pass[k];
		if (pi < 0 || tex[pi] == NULL) {
			continue;
		}
		mm_buffers_test_grey[k]  = plan->passes[pi].greyscale;
		mm_buffers_test_links[k] = strdup(mm_buffers_link(plan, pi));
		mm_buffers_test_set_map(k, tex[pi]);
		maps++;
	}
	i64 kb = 0; // The plan's textures
	for (i32 i = 0; i < plan->count; ++i) {
		kb += tex[i] != NULL ? mm_buffers_bytes(tex[i]) / 1024 : 0;
	}
	char *s = string("passes %d rendered %d cached %d failed %d draws %d maps %d links %d bad_links %d kb %d ms %.1f", plan->count, mm_buffers_stats[0],
	                 mm_buffers_stats[1], mm_buffers_stats[2], mm_buffers_stats[3], maps, links, bad_links, (i32)kb, ms);
	if (getenv("MM_BUFFERS_VERBOSE") != NULL) {
		for (i32 i = 0; i < plan->count; ++i) {
			mm_pass_t *p = &plan->passes[i];
			iron_log("mm_buffers pass %d %s: size %d%s%s%s%s loop_of %d textures %d", i, p->name, p->size, p->greyscale ? " grey" : "", p->f32 ? " f32" : "",
			         p->iterate ? " iterate" : "", p->texture_gen != NULL ? " cpu" : "", p->loop_of, p->textures);
		}
	}
	mm_buffers_require(need);
	mm_arena_release(mark);
	return s;
}

char *mm_buffers_test_ptex(char *path_or_id, i32 size) {
	return mm_buffers_test_run(path_or_id, size, false);
}

char *mm_buffers_test_other(char *path_or_id, i32 size) {
	return mm_buffers_test_run(path_or_id, size, true);
}

// The last test render's map links: 1 when its texture is in memory, 0 when evicted (its recipe kept), -1 when gone
i32 mm_buffers_test_resident(i32 map) {
	if (map < 0 || map >= MM_MAP_COUNT || mm_buffers_test_links[map] == NULL) {
		return -1;
	}
	mm_buffers_recipe_t *r = mm_buffers_find(strtoull(mm_buffers_test_links[map] + strlen(MM_BUFFERS_LINK), NULL, 16));
	return r == NULL ? -1 : r->tex != NULL ? 1 : 0;
}

// Resolves the last test render's map links with uniforms_ext_tex_link (rendering evicted ones again) and reads them
// back. in_pass: resolved inside a render pass, as a material draw does; returns -1 when that pass lost its content.
// Returns the maps read.
i32 mm_buffers_test_resolve(i32 in_pass) {
	gpu_texture_t *outer = NULL;
	u32            color = 0xff336699;
	if (in_pass) {
		outer = calloc(1, sizeof(gpu_texture_t));
		gpu_render_target_init(outer, 8, 8, GPU_TEXTURE_FORMAT_RGBA64);
		gpu_texture_t *targets[1] = {outer};
		gpu_begin(targets, 1, NULL, GPU_CLEAR_COLOR, color, 0.0f);
	}
	gpu_texture_t *got[MM_MAP_COUNT] = {0};
	i32            need              = mm_buffers_require(MM_BUFFERS_FULL); // Rendered full now, as for a bake
	for (i32 k = 0; k < MM_MAP_COUNT; ++k) {
		got[k] = mm_buffers_test_links[k] != NULL ? uniforms_ext_tex_link(NULL, NULL, mm_buffers_test_links[k]) : NULL;
	}
	mm_buffers_require(need);
	i32 n = 0;
	if (in_pass) {
		gpu_end();
		float  r, g, b, a;
		float *px = mm_buffers_read(outer, false);
		iron_color_components(color, &r, &g, &b, &a);
		bool kept = fabsf(px[0] - r) < 0.01f && fabsf(px[1] - g) < 0.01f && fabsf(px[2] - b) < 0.01f && fabsf(px[3] - a) < 0.01f;
		free(px);
		mm_buffers_texture_free(outer);
		if (!kept) {
			return -1;
		}
	}
	for (i32 k = 0; k < MM_MAP_COUNT; ++k) {
		if (mm_buffers_test_links[k] != NULL) {
			mm_buffers_test_set_map(k, got[k]);
			n += got[k] != NULL;
		}
	}
	return n;
}

// The resolution of a map of the last test render, 0 when the material has none (mm_map_t order: albedo, orm,
// emission, normal, height, sss)
i32 mm_buffers_test_map_size(i32 map) {
	return map >= 0 && map < MM_MAP_COUNT ? mm_buffers_test_sizes[map] : 0;
}

f32 mm_buffers_test_texel(i32 map, i32 x, i32 y, i32 channel) {
	if (mm_buffers_test_map_size(map) == 0) {
		return -1.0f;
	}
	i32 s = mm_buffers_test_sizes[map];
	x     = x < 0 ? 0 : x >= s ? s - 1 : x;
	y     = y < 0 ? 0 : y >= s ? s - 1 : y;
	return mm_buffers_test_maps[map][(y * s + x) * 4 + (channel & 3)];
}

// Writes the maps as <prefix>_albedo.png, ... (8 bits, clamped, as the checker writes them)
void mm_buffers_test_write(char *prefix) {
	static const char *names[MM_MAP_COUNT] = {"albedo", "orm", "emission", "normal", "heightmap", "sss"};
	for (i32 k = 0; k < MM_MAP_COUNT; ++k) {
		i32 s = mm_buffers_test_sizes[k];
		if (s == 0) {
			continue;
		}
		buffer_t *b = buffer_create(s * s * 4);
		for (i32 i = 0; i < s * s * 4; ++i) {
			float v      = mm_buffers_test_maps[k][i];
			v            = v != v ? 0.0f : v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
			b->buffer[i] = (uint8_t)(v * 255.0f + 0.5f);
		}
#ifdef IRON_BGRA // iron_write_png swaps red and blue there
		for (i32 i = 0; i < s * s; ++i) {
			uint8_t c            = b->buffer[i * 4];
			b->buffer[i * 4]     = b->buffer[i * 4 + 2];
			b->buffer[i * 4 + 2] = c;
		}
#endif
		iron_write_png(string_tmp("%s_%s.png", prefix, names[k]), b, s, s, 0);
		iron_delete_blob(b);
	}
}
