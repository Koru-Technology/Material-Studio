
#include "../global.h"

// A packed asset name no packed asset of the project has yet: /packed/<basename><number>.png, the lowest number free
char *util_texture_packed_name(char *basename) {
	if (g_project->packed_assets == NULL) {
		g_project->packed_assets = any_array_create_from_raw((void *[]){}, 0);
	}
	for (i32 num = 0;; ++num) {
		char *abs   = string_tmp("/packed/%s%d.png", basename, num);
		bool  taken = false;
		for (i32 i = 0; i < g_project->packed_assets->length && !taken; ++i) {
			packed_asset_t *pa = g_project->packed_assets->buffer[i];
			taken              = string_equals(pa->name, abs);
		}
		if (!taken) {
			return string_copy(abs);
		}
	}
}

// dst becomes src through pipe (a copy pipeline of dst's format, or a filter such as invert)
void util_copy_texture(gpu_texture_t *dst, gpu_texture_t *src, gpu_pipeline_t *pipe) {
	draw_begin(dst, false, 0);
	draw_set_pipeline(pipe);
	draw_image(src, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();
}

void util_texture_capture_output(gpu_texture_t *img, char *basename) {
	if (img == NULL) {
		return;
	}

	char *abs = util_texture_packed_name(basename);

	buffer_t *buf = gpu_get_texture_pixels_rgba8(img);
#ifdef IRON_BGRA
	if (img->format == GPU_TEXTURE_FORMAT_RGBA32) {
		buffer_bgra_swap(buf); // iron_encode_png takes the order render targets read back in
	}
#endif

	u8_array_t     *bytes = iron_encode_png(buf, img->width, img->height, 0);
	packed_asset_t *pa    = ALLOC_INIT(packed_asset_t, {.name = abs, .bytes = bytes});
	any_array_push(g_project->packed_assets, pa);
	gpu_texture_t *copy = gpu_create_texture_from_encoded_bytes(bytes, ".png");
	any_map_set(data_cached_textures, abs, copy);
	import_texture_run(abs, true);
}
