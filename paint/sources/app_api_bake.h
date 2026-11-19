#pragma once

// app_api.h, Bake Mesh Maps: what the native bake sheet needs. The ray tracer is plugins/bake_maps/bake_maps.c.
// Self-contained, imported by Swift. The rest of app_api.h's rules apply.
// Maps bake from the paint meshes, or from a high poly file read straight into the ray tracer (never into the scene).
// Each is saved as <mesh>_<suffix>.png in the project's Mesh Maps folder, added to the project textures and, for the
// mesh map kinds, registered as the project's mesh map.

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	APP_BAKE_MAP_NORMAL       = 0, // Tangent space, from the high poly (flat without one)
	APP_BAKE_MAP_HEIGHT       = 1, // From the high poly, 0.5 the low poly surface, the max distance either way
	APP_BAKE_MAP_AO           = 2,
	APP_BAKE_MAP_CURVATURE    = 3,
	APP_BAKE_MAP_CAVITY       = 4,
	APP_BAKE_MAP_THICKNESS    = 5,
	APP_BAKE_MAP_POSITION     = 6,
	APP_BAKE_MAP_WORLD_NORMAL = 7,
	APP_BAKE_MAP_COUNT,
} app_bake_map_t;

typedef struct {
	uint32_t maps; // Bit per app_bake_map_t, the maps a bake makes
	int32_t  size; // 1024, 2048, 4096 or 8192
	int32_t  rays; // Per texel for occlusion and thickness, 4 to 512
	float    ao_strength; // 0 to 2
	float    ao_radius;   // 0 to 2
	float    ao_offset;   // 0 to 2
	float    curvature_strength; // 0 to 2
	float    curvature_radius;   // 0 to 2
	float    curvature_offset;   // -2 to 2
	int32_t  curvature_smooth;   // Blur passes, 0 to 5
	float    cavity_size;        // Texels, 1 to 16
	float    cavity_contrast;    // 0.5 to 6
	float    max_distance;       // High poly search distance, relative to the low poly size, 0.001 to 0.2
	bool     average_normals;    // Rays along normals averaged across hard edges
} app_bake_options_t;

void        app_bake_get(app_bake_options_t *out);
void        app_bake_set(const app_bake_options_t *in);
const char *app_bake_map_name(int32_t map);

typedef struct {
	int32_t high_triangles; // The high poly's, 0 when none is loaded
	float   high_fit;       // Its size relative to the low poly, 0 when they do not overlap
	float   missed;         // Fraction of rays missing the high poly, 0 to 1
	bool    running;
	float   progress;  // Of the whole bake, 0 to 1
	int32_t map;       // app_bake_map_t baking now
	bool    has_baked; // A normal, occlusion or height map of this mesh is in the project (app_bake_add_layer)
	bool    stale;     // The paint meshes changed since the mesh maps were baked
} app_bake_state_t;

void app_bake_state(app_bake_state_t *out);

// Reads the high poly file into the ray tracer, its triangle count (0: failed). "" removes it.
int32_t     app_bake_set_high(const char *path);
const char *app_bake_high_name(void); // File name, "" when none

// Software render of the low poly (missed rays in red) or the high poly, for the sheet. Writes RGBA8 rows top to bottom
// into out and returns the byte count (Pixels.swift), 0 when there is nothing to show. fast: a cheap estimate while
// the view turns.
int32_t app_bake_preview(float yaw, float pitch, float zoom, bool high, bool fast, int32_t width, int32_t height,
                         uint8_t *out, int32_t capacity, int32_t *w, int32_t *h);

bool app_bake_start(void); // False when no map is selected or one runs
void app_bake_cancel(void);
void app_bake_close(void);     // The sheet went away: the ray tracer's data (gigabytes with a high poly) is released
bool app_bake_add_layer(void); // A fill layer with the baked normal, occlusion and height

// The project's mesh maps (mask generators read them): a project texture per kind
int32_t     app_mesh_map_count(void);
const char *app_mesh_map_name(int32_t type);
int32_t     app_mesh_map_texture(int32_t type); // Index of app_texture_name, -1 for none
bool        app_mesh_map_set(int32_t type, int32_t texture); // -1 clears it
int32_t     app_texture_count(void);
const char *app_texture_name(int32_t i);
