#ifdef WITH_GLSL

#include "iron_glsl.h"
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <pthread.h>
#include <spirv_cross_c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int gpu_shader_failure_count;
void       iron_log(const char *format, ...);

#define IRON_GLSL_MAX_TEXTURES 32

// Wall time, the translation runs on the main thread while pipelines compile on workers (clock() would sum their cpu time)
double iron_glsl_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static char *glsl_error = NULL; // iron_glsl_to_msl's

// Translations run on any thread at once: glslang compiles in parallel once its process is initialized (its pool
// allocator is per thread), each translation has its own spirv-cross context and error string. The lock guards the
// process init and iron_glsl_to_msl's static results.
static pthread_mutex_t glsl_lock = PTHREAD_MUTEX_INITIALIZER;

static void glsl_init(void) {
	static bool ready = false;
	pthread_mutex_lock(&glsl_lock);
	if (!ready) {
		glslang_initialize_process();
		ready = true;
	}
	pthread_mutex_unlock(&glsl_lock);
}

static void glsl_set_error(char **error, const char *what, const char *msg) {
	free(*error);
	size_t n = strlen(what) + strlen(msg) + 3;
	*error   = malloc(n);
	snprintf(*error, n, "%s: %s", what, msg);
}

static unsigned *glsl_to_spirv(const char *glsl, iron_glsl_stage_t stage, const char *defines, size_t *words, char **error) {
	glslang_stage_t gstage = stage == IRON_GLSL_VERTEX ? GLSLANG_STAGE_VERTEX : GLSLANG_STAGE_FRAGMENT;
	glslang_input_t input  = {
	     .language                          = GLSLANG_SOURCE_GLSL,
	     .stage                             = gstage,
	     .client                            = GLSLANG_CLIENT_VULKAN,
	     .client_version                    = GLSLANG_TARGET_VULKAN_1_1,
	     .target_language                   = GLSLANG_TARGET_SPV,
	     .target_language_version           = GLSLANG_TARGET_SPV_1_3,
	     .code                              = glsl,
	     .default_version                   = 450,
	     .default_profile                   = GLSLANG_NO_PROFILE,
	     .force_default_version_and_profile = false,
	     .forward_compatible                = false,
	     .messages                          = GLSLANG_MSG_DEFAULT_BIT,
	     .resource                          = glslang_default_resource(),
    };

	const char *stage_defines = stage == IRON_GLSL_VERTEX ? "#define VERT\n#define VARYING out\n#define VARYING_NAME vout\n"
	                                                      : "#define FRAG\n#define VARYING in\n#define VARYING_NAME vin\n";
	size_t      n             = strlen(stage_defines) + (defines != NULL ? strlen(defines) : 0) + 1;
	char       *preamble      = malloc(n);
	snprintf(preamble, n, "%s%s", stage_defines, defines != NULL ? defines : "");

	unsigned          *out     = NULL;
	glslang_shader_t  *shader  = glslang_shader_create(&input);
	glslang_program_t *program = NULL;
	glslang_shader_set_preamble(shader, preamble);
	if (!glslang_shader_preprocess(shader, &input) || !glslang_shader_parse(shader, &input)) {
		glsl_set_error(error, stage == IRON_GLSL_VERTEX ? "vert" : "frag", glslang_shader_get_info_log(shader));
		goto done;
	}
	program = glslang_program_create();
	glslang_program_add_shader(program, shader);
	if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
		glsl_set_error(error, "link", glslang_program_get_info_log(program));
		goto done;
	}
	glslang_program_SPIRV_generate(program, gstage);
	*words = glslang_program_SPIRV_get_size(program);
	out    = malloc(*words * sizeof(unsigned));
	glslang_program_SPIRV_get(program, out);

done:
	if (program != NULL) {
		glslang_program_delete(program);
	}
	glslang_shader_delete(shader);
	free(preamble);
	return out;
}

static void glsl_bind(spvc_compiler c, SpvExecutionModel model, unsigned binding, unsigned index) {
	spvc_msl_resource_binding_2 b;
	spvc_msl_resource_binding_init_2(&b);
	b.stage       = model;
	b.desc_set    = 0;
	b.binding     = binding;
	b.count       = 1;
	b.msl_buffer  = index;
	b.msl_texture = index;
	b.msl_sampler = index;
	spvc_compiler_msl_add_resource_binding_2(c, &b);
}

// Malloc'd msl, NULL on error (*error set)
static char *glsl_spirv_to_msl(unsigned *words, size_t count, iron_glsl_stage_t stage, bool ios, char **error) {
	spvc_context ctx = NULL;
	if (spvc_context_create(&ctx) != SPVC_SUCCESS) {
		glsl_set_error(error, "spirv-cross", "no context");
		return NULL;
	}
	char *msl = NULL;

	spvc_parsed_ir        ir;
	spvc_compiler         c;
	spvc_compiler_options o;
	if (spvc_context_parse_spirv(ctx, words, count, &ir) != SPVC_SUCCESS ||
	    spvc_context_create_compiler(ctx, SPVC_BACKEND_MSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &c) != SPVC_SUCCESS) {
		glsl_set_error(error, "spirv-cross", spvc_context_get_last_error_string(ctx));
		spvc_context_destroy(ctx);
		return NULL;
	}
	spvc_compiler_create_compiler_options(c, &o);
	spvc_compiler_options_set_uint(o, SPVC_COMPILER_OPTION_MSL_VERSION, SPVC_MAKE_MSL_VERSION(2, 3, 0));
	spvc_compiler_options_set_uint(o, SPVC_COMPILER_OPTION_MSL_PLATFORM, ios ? SPVC_MSL_PLATFORM_IOS : SPVC_MSL_PLATFORM_MACOS);
	spvc_compiler_options_set_bool(o, SPVC_COMPILER_OPTION_MSL_ARGUMENT_BUFFERS, SPVC_TRUE);
	spvc_compiler_options_set_uint(o, SPVC_COMPILER_OPTION_MSL_ARGUMENT_BUFFERS_TIER, 0); // Tier 1, as the backend encodes it
	// The backend encodes one layout (constants, sampler, textures) for both stages: every declared resource stays in the
	// argument buffer, a stage using only some of them would otherwise get a struct with the members at other offsets
	spvc_compiler_options_set_bool(o, SPVC_COMPILER_OPTION_MSL_FORCE_ACTIVE_ARGUMENT_BUFFER_RESOURCES, SPVC_TRUE);
	spvc_compiler_install_compiler_options(c, o);

	SpvExecutionModel model = stage == IRON_GLSL_VERTEX ? SpvExecutionModelVertex : SpvExecutionModelFragment;
	spvc_compiler_rename_entry_point(c, "main", stage == IRON_GLSL_VERTEX ? "vert" : "frag", model);
	glsl_bind(c, model, SPVC_MSL_ARGUMENT_BUFFER_BINDING, 1); // The argument buffer of set 0 at [[buffer(1)]], buffer 0 holds the vertices
	for (unsigned i = 0; i < 2 + IRON_GLSL_MAX_TEXTURES; ++i) {
		glsl_bind(c, model, i, i); // Binding i at [[id(i)]]: 0 constants, 1 sampler, 2 + i textures
	}

	const char *result = NULL;
	if (spvc_compiler_compile(c, &result) != SPVC_SUCCESS) {
		glsl_set_error(error, "spirv-cross", spvc_context_get_last_error_string(ctx));
	}
	else {
		msl = strdup(result);
	}
	spvc_context_destroy(ctx);
	return msl;
}

// One stage, thread safe: malloc'd msl, NULL on error (*error malloc'd)
static char *glsl_translate_stage(const char *glsl, iron_glsl_stage_t stage, const char *defines, bool ios, char **error) {
	glsl_init();
	static int timing = -1; // Env IRON_SHADER_TIMING (or IRON_GLSL_TIMING) logs the time of each step
	if (timing < 0) {
		timing = getenv("IRON_SHADER_TIMING") != NULL || getenv("IRON_GLSL_TIMING") != NULL;
	}
	double    t0    = iron_glsl_now_ms();
	size_t    words = 0;
	unsigned *spirv = glsl_to_spirv(glsl, stage, defines, &words, error);
	double    t1    = iron_glsl_now_ms();
	char     *msl   = NULL;
	if (spirv != NULL) {
		msl = glsl_spirv_to_msl(spirv, words, stage, ios, error);
		free(spirv);
	}
	if (timing) {
		fprintf(stderr, "[shader] translate %s: %d chars, glslang %.1f ms, spirv-cross %.1f ms\n", stage == IRON_GLSL_VERTEX ? "vert" : "frag",
		        (int)strlen(glsl), t1 - t0, iron_glsl_now_ms() - t1);
	}
	return msl;
}

char *iron_glsl_to_msl(const char *glsl, iron_glsl_stage_t stage, const char *defines, bool ios, char **error) {
	static char *result = NULL;
	char        *err    = NULL;
	char        *msl    = glsl_translate_stage(glsl, stage, defines, ios, &err);
	pthread_mutex_lock(&glsl_lock);
	free(result);
	result = msl;
	free(glsl_error);
	glsl_error = err;
	pthread_mutex_unlock(&glsl_lock);
	if (error != NULL) {
		*error = msl == NULL ? glsl_error : NULL;
	}
	return msl;
}

#ifndef AMAKE

// Numbered source lines around the error, the messages refer to lines as 0:<line>
static void glsl_log_source(const char *glsl, const char *error) {
	const char *at   = strstr(error, "0:");
	int         line = at != NULL ? atoi(at + 2) : 0;
	int         n    = 1;
	const char *s    = glsl;
	while (*s) {
		const char *e = strchr(s, '\n');
		int         l = e != NULL ? (int)(e - s) : (int)strlen(s);
		if (line == 0 || (n >= line - 3 && n <= line + 3)) {
			iron_log("%4d%s %.*s", n, n == line ? ">" : " ", l, s);
		}
		if (e == NULL) {
			break;
		}
		s = e + 1;
		n += 1;
	}
}

// Both stages, malloc'd ("//>vert\n" + msl, "//>frag\n" + msl), false on error. Thread safe.
bool iron_glsl_translate(const char *glsl, char **vs, char **fs) {
#ifdef IRON_IOS
	bool ios = true;
#else
	bool ios = false;
#endif
	*vs              = NULL;
	*fs              = NULL;
	const char *dump = getenv("IRON_GLSL_DUMP"); // A folder for every compiled source, to debug generated shaders
	if (dump != NULL) {
		pthread_mutex_lock(&glsl_lock);
		static int count = 0;
		char       path[512];
		snprintf(path, sizeof(path), "%s/shader_%04d.glsl", dump, count++);
		FILE *f = fopen(path, "wb");
		if (f != NULL) {
			fputs(glsl, f);
			fclose(f);
		}
		pthread_mutex_unlock(&glsl_lock);
	}
	for (int stage = 0; stage < 2; ++stage) {
		char *error = NULL;
		char *msl   = glsl_translate_stage(glsl, stage, NULL, ios, &error);
		if (msl == NULL) {
			pthread_mutex_lock(&glsl_lock); // The log lines of one failure together
			iron_log("Warning: Shader compilation failed\n%s", error != NULL ? error : "");
			glsl_log_source(glsl, error != NULL ? error : "");
			gpu_shader_failure_count += 1;
			pthread_mutex_unlock(&glsl_lock);
			free(error);
			free(*vs);
			*vs = NULL;
			return false;
		}
		const char *head = stage == 0 ? "//>vert\n" : "//>frag\n";
		size_t      n    = strlen(head) + strlen(msl) + 1;
		char       *out  = malloc(n);
		snprintf(out, n, "%s%s", head, msl);
		free(msl);
		*(stage == 0 ? vs : fs) = out;
	}
	return true;
}

void gpu_create_shaders_from_glsl(char *glsl, char **vs, char **fs, int *vs_size, int *fs_size) {
	static char *vs_out = NULL;
	static char *fs_out = NULL;
	*vs = "";
	*fs = "";
	char *v = NULL;
	char *f = NULL;
	if (iron_glsl_translate(glsl, &v, &f)) {
		free(vs_out);
		free(fs_out);
		vs_out = v;
		fs_out = f;
		*vs    = vs_out;
		*fs    = fs_out;
	}
	// Text output, sizes 0 as callers expect for text (make_paint_copy_shader copies size bytes of binary shaders)
	if (vs_size != NULL) {
		*vs_size = 0;
	}
	if (fs_size != NULL) {
		*fs_size = 0;
	}
}

#endif

#endif
