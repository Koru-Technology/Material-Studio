// Material Maker buffers: renders the passes of an mm_plan_t (material_maker/mm.h) with the app's GPU layer, as
// paint/tools/material_maker/mm_render.m does (the checker's Metal renderer, whose output matches Material Maker's
// exports): a fullscreen triangle per pass into a float target (R16F / R32F / RGBA16F / RGBA32F), constants vec4 mm =
// (target size, iteration, 1.0, 0) at offset 0, the linear repeat sampler, texture i at binding 2 + i. A pass whose
// shader does not compile renders a cleared texture, as in Material Maker.
//
// Material shaders sample a rendered pass through a texture link: "_mmbuf" + the pass's key as 16 lowercase hex digits
// (mm_buffers_link), resolved by uniforms_ext_tex_link with mm_buffers_texture.
//
// Keys: a 64-bit hash of everything a pass's texture depends on: its GLSL (and loop GLSL), size, format, iterations and
// shrink, a CPU texture's mm_texture_key, and the same for every pass it reads, followed through iterate loops (a
// canonical hash of the dependency graph from the pass).
//
// Recipes: each pass gets a recipe, malloc'd, outliving the mm arena: its GLSL, flags, the keys of its inputs, the loop
// passes of an iterate buffer, a CPU texture's pixels (lz4). A recipe renders its texture again by itself, rendering
// the inputs it needs from their recipes: a material shader compiled once keeps resolving its links after its textures
// were evicted. The texture is made when the link resolves: between gpu_begin and gpu_draw on Metal, where the pass in
// progress is suspended (gpu_suspend); elsewhere the link reads black for a frame and the texture renders on the next.
//
// Eviction: textures stay while they fit MM_BUFFERS_BUDGET_MB; past it, the least recently rendered, found or bound go
// first, never the last render's (a generation counter: mm_buffers_render advances it, finding or binding marks used).
// Pipelines stay up to MM_BUFFERS_MAX_PIPES, the least recently used go first. Recipes
// stay while they fit MM_BUFFERS_RECIPE_BUDGET_MB, the least recently used go first; binding a link keeps its recipe and
// those of every pass it reads.
//
// Passes inside an iterate buffer's loop (loop_of >= 0) render into scratch targets for each iteration, recycled; their
// last iteration's texture is kept as theirs (Material Maker renders them again whenever the loop texture updates),
// with a link like any pass. Their key includes the iterate buffer's; rendering one again renders the iterate buffer.

#include "../global.h"
#include "../material_maker/mm.h"

#define MM_BUFFERS_MAX_PIPES 256 // Compiled pass pipelines kept, the least recently used go first past it
#define MM_BUFFERS_FLUSH 64 // Draws between submits, short command buffers (the ios gpu watchdog, iterate buffers draw thousands)
#ifdef IRON_IOS
#define MM_BUFFERS_MAX_SIZE         2048 // Pass sizes are clamped to it (shaders get the real size): 2048² RGBA32F is 64 MB
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
	i32                       size;
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
	gpu_texture_t            *tex; // NULL until rendered, and once evicted
	bool                      failed;
	i32                       used;        // Generation the texture was last rendered, found or bound
	i32                       recipe_used; // Generation the recipe was last needed
	i64                       bytes;       // Of the recipe
	gpu_texture_t            *cur;         // While rendering: a loop pass's current texture, an iterate buffer's previous iteration
	bool                      rendering;
	i32                       rendered;      // Generation it was last rendered
	i32                       rendered_call; // mm_buffers_render call it was last rendered by (the stats)
} mm_buffers_recipe_t;

typedef struct {
	u64            key; // GLSL hash and format
	gpu_pipeline_t pipe;
	gpu_shader_t  *vs;
	gpu_shader_t  *fs;
	bool           ok;
	bool           compiled; // false: shaders made, pipeline still to compile (mm_buffers_precompile)
	i32            used;
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

// Material previews (a 256 px sphere) render their passes at most this size: the material's own sizes (2048 and more)
// cost up to seconds per preview on the iPad. 0: no cap. The size is part of a pass's key, the capped textures never
// stand in for the full size ones painting uses.
static i32 mm_buffers_size_cap = 0;

void mm_buffers_set_size_cap(i32 size) {
	mm_buffers_size_cap = size;
}

static i32 mm_buffers_size(mm_pass_t *p) {
	i32 s = p->size < 1 ? 1 : p->size;
	s     = s > MM_BUFFERS_MAX_SIZE ? MM_BUFFERS_MAX_SIZE : s;
	return mm_buffers_size_cap > 0 && s > mm_buffers_size_cap ? mm_buffers_size_cap : s;
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
	i32 v[] = {p->texture_gen != NULL ? 0 : mm_buffers_size(p),
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
	r->size                = mm_buffers_size(p);
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
			if (r->tex != NULL && r->used < mm_buffers_gen && !r->rendering) {
				mm_buffers_texture_free(r->tex);
				r->tex = NULL;
			}
		}
	}
	mm_buffers_scratch_free();
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

static void mm_buffers_evict(bool all) {
	// Textures stay while they fit the budget: a material compiles into several plans (paint, preview, mesh) and other
	// materials render in between, freeing whatever a render did not use made the same passes render again and again.
	// Over budget, the least recently used go first, never the last render's.
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
	i32 n = 0;
	for (i32 i = 0; i < mm_buffers_npipes; ++i) {
		mm_buffers_pipe_t *p = mm_buffers_pipes[i];
		if (all || p->used < oldest_pipe) {
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

// The pipeline of a pass, its shaders made (GLSL translated, on this thread: the translator is not thread safe) but not
// compiled yet: mm_buffers_pipe_compile does, on any thread
static mm_buffers_pipe_t *mm_buffers_pipe_prepare(char *glsl, u64 glsl_hash, gpu_texture_format_t format) {
	u64 key = mm_buffers_hash_i32(glsl_hash, (i32)format);
	for (i32 i = 0; i < mm_buffers_npipes; ++i) {
		if (mm_buffers_pipes[i]->key == key) {
			mm_buffers_pipes[i]->used = mm_buffers_gen;
			return mm_buffers_pipes[i];
		}
	}
	mm_buffers_pipe_t *p = calloc(1, sizeof(mm_buffers_pipe_t));
	p->key               = key;
	p->used              = mm_buffers_gen;
	p->compiled          = true; // Unless shaders are made below
	if (glsl != NULL) {
		char *vs      = NULL;
		char *fs      = NULL;
		i32   vs_size = 0;
		i32   fs_size = 0;
		gpu_create_shaders_from_glsl(glsl, &vs, &fs, &vs_size, &fs_size);
		if (vs != NULL && fs != NULL && (vs_size > 0 || vs[0] != '\0') && (fs_size > 0 || fs[0] != '\0')) {
			if (mm_buffers_structure.size == 0) {
				gpu_vertex_structure_add(&mm_buffers_structure, "pos", GPU_VERTEX_DATA_F32_2X);
			}
			p->vs = gpu_create_shader_from_source(vs, vs_size, GPU_SHADER_TYPE_VERTEX);
			p->fs = gpu_create_shader_from_source(fs, fs_size, GPU_SHADER_TYPE_FRAGMENT);
			gpu_pipeline_init(&p->pipe);
			p->pipe.input_layout           = &mm_buffers_structure;
			p->pipe.vertex_shader          = p->vs;
			p->pipe.fragment_shader        = p->fs;
			p->pipe.color_attachment_count = 1;
			p->pipe.color_attachment[0]    = format;
			p->compiled                    = false;
		}
	}
	mm_buffers_pipes                      = realloc(mm_buffers_pipes, sizeof(mm_buffers_pipe_t *) * (mm_buffers_npipes + 1));
	mm_buffers_pipes[mm_buffers_npipes++] = p;
	return p;
}

static void mm_buffers_pipe_compile(mm_buffers_pipe_t *p) {
	if (p->compiled) {
		return;
	}
	gpu_pipeline_compile(&p->pipe);
	p->ok       = p->pipe.impl.pipeline != NULL;
	p->compiled = true;
}

static mm_buffers_pipe_t *mm_buffers_pipeline(char *glsl, u64 glsl_hash, gpu_texture_format_t format) {
	mm_buffers_pipe_t *p = mm_buffers_pipe_prepare(glsl, glsl_hash, format);
	mm_buffers_pipe_compile(p);
	return p;
}

// Compiling a material's pipelines one after another as its passes render made the main thread wait on Metal's compiler
// for each in turn (seconds for library materials the first time after an install, ~85 ms each on the iPad). The passes
// a render will draw get their pipelines compiled together on worker threads first.
#define MM_BUFFERS_COMPILE_THREADS 8

typedef struct {
	mm_buffers_pipe_t **items;
	i32                 count;
	i32                 next;
	iron_mutex_t        mutex;
} mm_buffers_compile_jobs_t;

static void mm_buffers_compile_worker(void *param) {
	mm_buffers_compile_jobs_t *j = param;
	while (true) {
		iron_mutex_lock(&j->mutex);
		i32 i = j->next++;
		iron_mutex_unlock(&j->mutex);
		if (i >= j->count) {
			return;
		}
		mm_buffers_pipe_compile(j->items[i]);
	}
}

static gpu_texture_format_t mm_buffers_format(mm_buffers_recipe_t *r);

static void mm_buffers_precompile(mm_buffers_recipe_t **rs, i32 count) {
	mm_buffers_pipe_t **todo = malloc(sizeof(mm_buffers_pipe_t *) * (count * 2 + 1));
	i32                 n    = 0;
	for (i32 i = 0; i < count; ++i) {
		mm_buffers_recipe_t *r = rs[i];
		if (r->cpu || r->tex != NULL) { // Nothing to draw: a CPU texture, or cached
			continue;
		}
		for (i32 k = 0; k < (r->iterate ? 2 : 1); ++k) {
			mm_buffers_pipe_t *p = mm_buffers_pipe_prepare(k == 0 ? r->glsl : r->glsl_loop, k == 0 ? r->glsl_hash : r->loop_hash, mm_buffers_format(r));
			bool               listed = p->compiled;
			for (i32 m = 0; m < n && !listed; ++m) {
				listed = todo[m] == p;
			}
			if (!listed) {
				todo[n++] = p;
			}
		}
	}
	i32 threads = iron_hardware_threads();
	threads     = threads > n ? n : threads;
	threads     = threads > MM_BUFFERS_COMPILE_THREADS ? MM_BUFFERS_COMPILE_THREADS : threads;
	if (threads > 1) {
		mm_buffers_compile_jobs_t j = {.items = todo, .count = n};
		iron_mutex_init(&j.mutex);
		iron_thread_t workers[MM_BUFFERS_COMPILE_THREADS];
		for (i32 i = 1; i < threads; ++i) {
			iron_thread_init(&workers[i], mm_buffers_compile_worker, &j);
		}
		mm_buffers_compile_worker(&j);
		for (i32 i = 1; i < threads; ++i) {
			iron_thread_wait_and_destroy(&workers[i]);
		}
		iron_mutex_destroy(&j.mutex);
	}
	else {
		for (i32 i = 0; i < n; ++i) {
			mm_buffers_pipe_compile(todo[i]);
		}
	}
	free(todo);
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

static void mm_buffers_scratch_free(void) {
	for (i32 i = 0; i < mm_buffers_nscratch; ++i) {
		mm_buffers_texture_free(mm_buffers_scratch[i].tex);
	}
	mm_buffers_nscratch = 0;
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

static void mm_buffers_draw(mm_buffers_pipe_t *pipe, gpu_texture_t *target, gpu_texture_t **inputs, i32 n, i32 size, i32 iteration) {
	gpu_texture_t *targets[1] = {target};
	gpu_begin(targets, 1, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0f);
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
	mm_buffers_pixels += (double)size * size;
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
	if (++mm_buffers_draws % MM_BUFFERS_FLUSH == 0) {
		gpu_execute_and_wait();
	}
}

static gpu_texture_t *mm_buffers_ensure(mm_buffers_recipe_t *r);

static bool mm_buffers_owner_rendering(mm_buffers_recipe_t *r) {
	mm_buffers_recipe_t *o = r->in_loop ? mm_buffers_find(r->owner) : NULL;
	return o != NULL && o->rendering;
}

// An input's texture: a loop pass's or an iterate buffer's current one while it renders, others rendered when missing.
// NULL (an empty texture) when none: missing recipe, a loop pass not rendered yet in its loop.
static gpu_texture_t *mm_buffers_input(u64 key) {
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
	return mm_buffers_ensure(r);
}

static i32 mm_buffers_inputs(i32 n, u64 *keys, gpu_texture_t **out) {
	n = n > GPU_MAX_TEXTURES ? GPU_MAX_TEXTURES : n;
	for (i32 k = 0; k < n; ++k) {
		out[k] = mm_buffers_input(keys[k]);
	}
	for (i32 k = 0; k < n; ++k) { // After: rendering an input may need the empty texture made
		out[k] = out[k] != NULL ? out[k] : mm_buffers_empty_texture();
	}
	return n;
}

// A scratch target, or a new texture for CPU textures
static gpu_texture_t *mm_buffers_render_recipe(mm_buffers_recipe_t *r) {
	if (r->cpu) { // Image or text node: its pixels as RGBA8 unorm
		gpu_texture_t *t    = calloc(1, sizeof(gpu_texture_t));
		u32            len  = r->w * r->h * 4;
		buffer_t      *px   = r->raw ? r->pixels : lz4_decode(r->pixels, len);
		gpu_texture_init_from_bytes(t, px->buffer, r->w, r->h, GPU_TEXTURE_FORMAT_RGBA32, false);
		if (px != r->pixels) {
			iron_delete_blob(px);
		}
		return t;
	}
	r->rendering                = true;
	gpu_texture_format_t format = mm_buffers_format(r);
	gpu_texture_t       *in[GPU_MAX_TEXTURES];
	i32                  n    = mm_buffers_inputs(r->textures, r->inputs, in);
	mm_buffers_pipe_t   *pipe = mm_buffers_pipe_of(r, false);
	gpu_texture_t       *t    = mm_buffers_target(format, r->size);
	mm_buffers_drawing        = r->name;
	mm_buffers_draw(pipe, t, in, n, r->size, 0);
	if (r->iterate) {
		mm_buffers_recipe_t **loop = calloc(r->nloop + 1, sizeof(mm_buffers_recipe_t *));
		for (i32 j = 0; j < r->nloop; ++j) {
			loop[j] = mm_buffers_find(r->loop[j]);
		}
		for (i32 k = 1; k <= r->iterations; ++k) {
			r->cur = t; // The loop's buffers and loop source read the previous iteration
			for (i32 j = 0; j < r->nloop; ++j) {
				if (loop[j] != NULL) {
					gpu_texture_t *old = loop[j]->cur;
					gpu_texture_t *now = mm_buffers_render_recipe(loop[j]);
					if (old != NULL) {
						mm_buffers_release(old);
					}
					loop[j]->cur = now;
				}
			}
			i32 s = r->size;
			if (r->shrink) {
				s = k < 31 ? s >> k : 0;
				s = s < 4 ? 4 : s;
			}
			n                       = mm_buffers_inputs(r->loop_textures, r->loop_inputs, in);
			mm_buffers_pipe_t *lp   = mm_buffers_pipe_of(r, true);
			gpu_texture_t     *next = mm_buffers_target(format, s);
			mm_buffers_drawing      = r->name;
			mm_buffers_draw(lp, next, in, n, s, k);
			mm_buffers_release(t);
			t = next;
		}
		r->cur = NULL;
		for (i32 j = 0; j < r->nloop; ++j) { // The last iteration's textures become the loop passes'
			if (loop[j] != NULL && loop[j]->cur != NULL) {
				mm_buffers_detach(loop[j]->cur);
				mm_buffers_texture_free(loop[j]->tex);
				loop[j]->tex      = loop[j]->cur;
				loop[j]->cur      = NULL;
				loop[j]->used     = mm_buffers_gen;
				loop[j]->rendered      = mm_buffers_gen;
				loop[j]->rendered_call = mm_buffers_calls;
			}
		}
		free(loop);
	}
	r->rendering = false;
	return t;
}

// Renders the recipe's texture again, replacing it
static i32 mm_buffers_again = 0; // IRON_SHADER_TIMING: recipes of the render in progress rendered before (evicted since)

static void mm_buffers_produce(mm_buffers_recipe_t *r) {
	if (r->rendered != 0) {
		mm_buffers_again++;
	}
	gpu_texture_t *t = mm_buffers_render_recipe(r);
	mm_buffers_detach(t);
	mm_buffers_texture_free(r->tex);
	r->tex      = t;
	r->used     = mm_buffers_gen;
	r->rendered      = mm_buffers_gen;
	r->rendered_call = mm_buffers_calls;
}

// The recipe's texture, rendered (and the inputs it needs) when missing. A loop pass's comes from rendering its
// iterate buffer again.
static gpu_texture_t *mm_buffers_ensure(mm_buffers_recipe_t *r) {
	r->used = mm_buffers_gen;
	if (r->tex != NULL) {
		return r->tex;
	}
	if (r->in_loop) {
		mm_buffers_recipe_t *o = mm_buffers_find(r->owner);
		if (o != NULL && !o->rendering) {
			mm_buffers_produce(o);
		}
		return r->tex;
	}
	mm_buffers_produce(r);
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

static void mm_buffers_gpu_end(void) {
	mm_buffers_scratch_free();
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
	mm_buffers_precompile(rs, count);
	bool began = false;
	for (i32 i = 0; i < count; ++i) {
		mm_buffers_recipe_t *r = rs[i];
		if (r->tex != NULL) {
			r->used = mm_buffers_gen;
			continue;
		}
		if (!began) {
			mm_buffers_gpu_begin();
			began = true;
		}
		mm_buffers_ensure(r);
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
			biggest = rs[i]->size > biggest ? rs[i]->size : biggest;
		}
		if (began) {
			gpu_execute_and_wait(); // The GPU time of this render lands here, not in the next one
		}
		iron_log("[buffers] %d passes (%d rendered, %d of them again after eviction, %d cached, %d failed), %d draws, %.1f Mpx drawn, largest target %d (plan %d), "
		         "%d iterate buffers with %d iterations, budget %d MB, %.1f ms",
		         count, mm_buffers_stats[0], mm_buffers_again, mm_buffers_stats[1], mm_buffers_stats[2], mm_buffers_stats[3], mm_buffers_pixels / 1e6, mm_buffers_max_size,
		         biggest, iterating, iterations, (i32)(mm_buffers_budget() / (1024 * 1024)), (iron_time() - t_render) * 1000.0);
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

#ifndef IRON_METAL
// No gpu_suspend: links resolved during a pass render on the next frame
static u64 *mm_buffers_queue  = NULL;
static i32  mm_buffers_queued = 0;

static void mm_buffers_render_queued(void *_) {
	mm_buffers_gpu_begin();
	for (i32 i = 0; i < mm_buffers_queued; ++i) {
		mm_buffers_recipe_t *r = mm_buffers_find(mm_buffers_queue[i]);
		if (r != NULL) {
			mm_buffers_ensure(r);
			mm_buffers_rerenders++;
		}
	}
	mm_buffers_queued = 0;
	mm_buffers_gpu_end();
	g_context->ddirty = 2;
}
#endif

// An evicted texture, rendered again from the recipes
static gpu_texture_t *mm_buffers_rerender(mm_buffers_recipe_t *r) {
	bool suspended = false;
	if (gpu_in_use) {
#ifdef IRON_METAL
		gpu_suspend(); // Between gpu_begin and gpu_draw of a material
		suspended = true;
#else
		if (mm_buffers_queued == 0) {
			sys_notify_on_next_frame(&mm_buffers_render_queued, NULL);
		}
		mm_buffers_queue                      = realloc(mm_buffers_queue, sizeof(u64) * (mm_buffers_queued + 1));
		mm_buffers_queue[mm_buffers_queued++] = r->key;
		return NULL;
#endif
	}
	mm_buffers_gpu_begin();
	mm_buffers_ensure(r);
	mm_buffers_gpu_end();
	if (suspended) {
#ifdef IRON_METAL
		gpu_resume();
#endif
	}
	mm_buffers_rerenders++;
	return r->tex;
}

// The texture of a link, rendered again when it was evicted; NULL when it is not one or its recipe is gone. Marks it
// used: bound textures stay.
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
	r->used = mm_buffers_gen;
	return r->tex != NULL ? r->tex : mm_buffers_rerender(r);
}

// Frees every texture, recipe and pipeline
void mm_buffers_clear(void) {
	mm_buffers_evict(true);
	mm_buffers_last_plan  = NULL;
	mm_buffers_last_count = 0;
}

// 0: passes rendered by the last render, 1: found in the cache, 2: failed shaders, 3: draws, 4: cached textures,
// 5: cached pipelines, 6: cached texture memory in KB, 7: the texture budget in KB, 8: textures rendered again on
// demand (since start), 9: recipes, 10: recipe memory in KB
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
	f64             t0  = iron_time();
	gpu_texture_t **tex = mm_buffers_render(plan);
	f64             ms  = (iron_time() - t0) * 1000.0;
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
	for (i32 k = 0; k < MM_MAP_COUNT; ++k) {
		got[k] = mm_buffers_test_links[k] != NULL ? uniforms_ext_tex_link(NULL, NULL, mm_buffers_test_links[k]) : NULL;
	}
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
