#pragma once

// app_api.h, texture export: what the native export sheet needs (io/export_tiled.c). Self-contained, imported by Swift.
// Only with the tiled export (the export_tiled preference): app_export_available() is false without it, the sheet then
// opens the engine's export box (APP_COMMAND_EXPORT_TEXTURES). The rest of app_api.h's rules apply.

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

bool        app_export_available(void);
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
