// app_api_export.h, #included by main.c after io/export_tiled.c

#include "app_api_export.h"

_Static_assert((i32)APP_EXPORT_LAYERS_PER_UDIM == (i32)EXPORT_MODE_PER_UDIM_TILE, "app_api_export.h mirrors enums.h");

static i32             app_export_size = 0;
static string_array_t *app_export_names = NULL; // Not upscaled at app_export_size

bool app_export_available(void) {
	return g_config->export_tiled;
}

static void app_export_presets() {
	if (box_export_files == NULL) {
		box_export_fetch_presets();
		i32 i                   = string_array_index_of(box_export_files, "generic");
		box_export_preset_index = i > 0 ? i : 0;
	}
}

void app_export_get(app_export_options_t *out) {
	app_export_presets();
	*out                = (app_export_options_t){0};
	out->size           = app_export_size;
	out->working_width  = config_get_texture_res_x();
	out->working_height = config_get_texture_res_y();
#ifdef IRON_IOS
	out->max_size = 8192;
#else
	out->max_size = 16384;
#endif
	out->preset       = box_export_preset_index;
	out->preset_count = box_export_files->length;
	out->layers       = g_context->layers_export;
	out->format       = base_bits == TEXTURE_BITS_BITS32 ? APP_EXPORT_FORMAT_EXR32
	                    : base_bits == TEXTURE_BITS_BITS16 ? APP_EXPORT_FORMAT_EXR16
	                    : g_context->format_type == TEXTURE_LDR_FORMAT_JPG ? APP_EXPORT_FORMAT_JPG
	                                                                       : APP_EXPORT_FORMAT_PNG;
	out->quality      = g_context->format_quality;
	out->padding      = g_context->export_padding;
	out->running      = export_tiled_running();
}

void app_export_set(const app_export_options_t *in) {
	app_export_presets();
	i32 max = 16384;
#ifdef IRON_IOS
	max = 8192;
#endif
	app_export_size = in->size > max ? max : in->size < 0 ? 0 : in->size;
	if (in->preset >= 0 && in->preset < box_export_files->length && in->preset != box_export_preset_index) {
		box_export_preset_index = in->preset;
		box_export_preset       = NULL;
	}
	g_context->layers_export  = in->layers >= 0 && in->layers <= APP_EXPORT_LAYERS_PER_UDIM ? in->layers : EXPORT_MODE_VISIBLE;
	g_context->format_quality = in->quality;
	g_context->export_padding = in->padding;
	i32 format                = in->format;
#ifdef IRON_IOS
	format = format > APP_EXPORT_FORMAT_JPG ? APP_EXPORT_FORMAT_PNG : format;
#endif
	g_context->format_type = format == APP_EXPORT_FORMAT_JPG ? TEXTURE_LDR_FORMAT_JPG : TEXTURE_LDR_FORMAT_PNG;
	i32 bits               = format == APP_EXPORT_FORMAT_EXR32 ? TEXTURE_BITS_BITS32 : format == APP_EXPORT_FORMAT_EXR16 ? TEXTURE_BITS_BITS16 : TEXTURE_BITS_BITS8;
	if (bits != base_bits) {
		base_bits = bits;
		layers_set_bits();
	}
}

const char *app_export_preset_name(int32_t i) {
	app_export_presets();
	return i >= 0 && i < box_export_files->length ? box_export_files->buffer[i] : "";
}

int32_t app_export_not_upscaled_count(void) {
	i32 n            = export_tiled_not_upscaled(app_export_size, app_export_size);
	app_export_names = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < n; ++i) {
		any_array_push(app_export_names, string_copy(export_tiled_not_upscaled_name(i)));
	}
	return n;
}

const char *app_export_not_upscaled_name(int32_t i) {
	return app_export_names != NULL && i >= 0 && i < app_export_names->length ? app_export_names->buffer[i] : "";
}

static void app_export_update(void *_) {
	iron_delay_idle_sleep();
	if (export_tiled_step(40.0)) {
		sys_remove_update(&app_export_update);
		export_tiled_set_resolution(0, 0);
	}
}

static void app_export_run(void *_) {
	export_tiled_set_resolution(app_export_size, app_export_size);
	if (export_tiled_start(g_context->texture_export_path, false)) {
		sys_notify_on_update(&app_export_update, NULL);
	}
	else {
		export_tiled_set_resolution(0, 0);
	}
}

static void app_export_path_picked(char *path) {
	g_context->texture_export_path = string_copy(path);
	sys_notify_on_next_frame(&app_export_run, NULL);
}

bool app_export_start(void) {
	if (!g_config->export_tiled || export_tiled_running()) {
		return false;
	}
	g_context->layers_destination = EXPORT_DESTINATION_DISK;
	char *filters                 = base_bits != TEXTURE_BITS_BITS8 ? "exr" : g_context->format_type == TEXTURE_LDR_FORMAT_PNG ? "png" : "jpg";
	ui_files_show(filters, true, false, &app_export_path_picked);
	return true;
}

float app_export_progress(void) {
	return export_tiled_progress();
}

void app_export_cancel(void) {
	if (export_tiled_running()) {
		export_tiled_cancel();
		sys_remove_update(&app_export_update);
		export_tiled_set_resolution(0, 0);
	}
}
