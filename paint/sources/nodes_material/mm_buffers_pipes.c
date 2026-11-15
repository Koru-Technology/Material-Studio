#include "../global.h"
#include "../material_maker/mm.h"
#include <iron_profile.h>

// Material Maker buffers (mm_buffers.c): the render pipelines of the passes, translated and compiled on worker threads,
// cached. Included after mm_buffers.c, whose types and statics it uses.

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
	u64 key = hash_fnv1a_i32(glsl_hash, (i32)format);
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

// Held while the paint shader of a material just picked compiles (make_material.c): strokes wait for that one, Metal's
// compiler gets to it first. A pipeline needed at once meanwhile is still made (mm_buffers_pipe_wait takes it back).
static atomic_bool mm_buffers_held = false;

void mm_buffers_hold_workers(bool hold) {
	atomic_store(&mm_buffers_held, hold);
}

static void mm_buffers_worker(void *param) {
	i32 i = (i32)(size_t)param;
#ifdef __APPLE__
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0); // The material waits for them: performance cores
#endif
	while (true) {
		while (atomic_load(&mm_buffers_held)) { // Ends with the jobs (mm_buffers_workers_drain takes them all and joins)
			mm_buffers_jobs_lock();
			i32 n = mm_buffers_njobs;
			iron_mutex_unlock(&mm_buffers_jobs_mutex);
			if (n == 0) {
				break;
			}
			usleep(1000);
		}
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

// At exit: the pipelines waiting are dropped, the workers joined. glslang's tables go with the process's static
// destructors, a translation still running then crashed (the app quit by a test script).
static void mm_buffers_workers_exit(void) {
	mm_buffers_jobs_lock();
	mm_buffers_njobs = 0;
	iron_mutex_unlock(&mm_buffers_jobs_mutex);
	for (i32 i = 0; i < MM_BUFFERS_WORKERS; ++i) {
		if (mm_buffers_worker_alive[i]) {
			iron_thread_wait_and_destroy(&mm_buffers_worker_threads[i]);
			mm_buffers_worker_alive[i] = false;
		}
	}
}

// Joins the workers that ended, starts workers for the pipelines waiting
static void mm_buffers_workers_start(void) {
	static bool at_exit = false;
	if (!at_exit) {
		at_exit = true;
		atexit(mm_buffers_workers_exit);
	}
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
		usleep(500);
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
