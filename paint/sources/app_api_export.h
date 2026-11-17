#pragma once

// app_api.h, texture export: what the native export sheet needs (io/export_tiled.c). Self-contained, imported by Swift.
// The rest of app_api.h's rules apply.

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	APP_EXPORT_LAYERS_VISIBLE   = 0,
	APP_EXPORT_LAYERS_SELECTED  = 1,
	APP_EXPORT_LAYERS_PER_OBJECT = 2,
	APP_EXPORT_LAYERS_PER_UDIM  = 3,
} app_export_layers_t;

typedef enum {
	APP_EXPORT_FORMAT_PNG   = 0, // 8 bit
	APP_EXPORT_FORMAT_JPG   = 1, // 8 bit
	APP_EXPORT_FORMAT_EXR16 = 2, // Not on iOS
	APP_EXPORT_FORMAT_EXR32 = 3,
} app_export_format_t;

typedef struct {
	int32_t size;           // Export resolution (square), 0 the working one
	int32_t working_width;  // The project's
	int32_t working_height;
	int32_t max_size;       // 8192 on iOS, 16384 elsewhere
	int32_t preset;         // Index of app_export_preset_name
	int32_t preset_count;
	int32_t layers;         // app_export_layers_t
	int32_t format;         // app_export_format_t
	float   quality;        // Jpg, 0 to 100
	bool    padding;
	bool    running;        // An export is in progress (app_export_progress)
} app_export_options_t;

void        app_export_get(app_export_options_t *out);
void        app_export_set(const app_export_options_t *in); // size, preset, layers, format, quality, padding
const char *app_export_preset_name(int32_t i);

// Layers the export at the size set can not render at that size: scaled up from the working resolution (paint layers
// until their strokes replay). Read again after a change of size.
int32_t     app_export_not_upscaled_count(void);
const char *app_export_not_upscaled_name(int32_t i);

// Asks for a folder, then exports into it over the frames that follow. False when there is nothing to export.
bool  app_export_start(void);
float app_export_progress(void); // 0 to 1, 1 when none is in progress
void  app_export_cancel(void);

// Project resolution (util/util_layer_resolution.c). The change keeps painted detail, runs over frames (a layer each,
// app_resolution_get().progress) and is one undo step.
typedef struct {
	int32_t width; // The working resolution
	int32_t height;
	int32_t max_size;     // 8192 on iOS, 16384 elsewhere
	bool    running;
	float   progress; // 0 to 1, 1 when none runs
} app_resolution_t;

void app_resolution_get(app_resolution_t *out);
bool app_resolution_set(int32_t size); // Square, from 512 to max_size; false when it is the current one or one runs

// Project history (the undo history saved in the project, phase 5b)
typedef struct {
	int64_t ram_bytes;     // History blobs held in memory (compressed)
	int64_t disk_bytes;    // and in the session journal
	int32_t blobs;
} app_history_info_t;

void app_history_info(app_history_info_t *out);
void app_history_clear(void);     // Steps and checkpoints go, timelines stay (hi-res export keeps working)
bool app_history_save_copy(void); // A copy of the project without its history: asks where (iOS: a project folder of its own)
