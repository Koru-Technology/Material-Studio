#pragma once

// GLSL shaders: GLSL 450 (Vulkan flavour) -> SPIR-V (glslang) -> MSL (SPIRV-Cross).
// One source holds both stages: code for one stage goes in #ifdef VERT / #ifdef FRAG. VARYING is out in the vertex
// stage and in in the fragment stage, VARYING_NAME is vout / vin, for one varyings block declared for both stages.
// Bindings follow the Metal backend: set 0, binding 0 the constants block, binding 1 the sampler, binding 2 + i
// texture i, all in an argument buffer at [[buffer(1)]]. Every shader declares the constants block and the sampler
// (unused ones too): the argument buffer layout has no gaps, as the backend encodes it.

#include <stdbool.h>

typedef enum {
	IRON_GLSL_VERTEX   = 0,
	IRON_GLSL_FRAGMENT = 1,
} iron_glsl_stage_t;

// MSL for one stage, the entry function named vert / frag. Returns NULL on error, *error then holds the message.
// The returned strings stay valid until the next call.
char *iron_glsl_to_msl(const char *glsl, iron_glsl_stage_t stage, const char *defines, bool ios, char **error);

// Monotonic wall time in milliseconds, for the IRON_SHADER_TIMING logs
double iron_glsl_now_ms(void);

// Both stages: *vs = "//>vert\n" + msl, *fs = "//>frag\n" + msl, or "" on error
void gpu_create_shaders_from_glsl(char *glsl, char **vs, char **fs, int *vs_size, int *fs_size);

// The same, thread safe (translations run in parallel): malloc'd strings the caller frees, false on error
bool iron_glsl_translate(const char *glsl, char **vs, char **fs);
