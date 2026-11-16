#include "../global.h"

// Textures of .arm files: paths relative to the file, packed assets, decoded in parallel, layer data, pink placeholders

// Absolute path of a file referenced by a project. iOS moves the app container between installs, a path inside the old container is
// moved into the current one
char *import_arm_abs(char *file, char *base) {
	char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
#ifdef IRON_IOS
	char *marker = "/Containers/Data/Application/";
	char *home   = getenv("HOME");
	i32   at     = string_index_of(abs, marker);
	if (home != NULL && at >= 0 && !iron_file_exists(abs)) {
		char *rest  = abs + at + string_length(marker);
		char *slash = strchr(rest, '/'); // After the container id
		if (slash != NULL) {
			char *moved = string("%s%s", home, slash);
			if (iron_file_exists(moved)) {
				return moved;
			}
		}
	}
#endif
	return abs;
}

void import_arm_make_pink(char *abs) {
	console_error(string("%s %s", strings_could_not_locate_texture(), abs));
	u8_array_t *b       = u8_array_create(4);
	b->buffer[0]        = 255;
	b->buffer[1]        = 0;
	b->buffer[2]        = 255;
	b->buffer[3]        = 255;
	gpu_texture_t *pink = gpu_create_texture_from_bytes(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA32);
	array_free(b);
	free(b);
	any_map_set(data_cached_textures, abs, pink);
}

static bool import_arm_has_alpha(buffer_t *pixels, i32 format) {
	if (format == GPU_TEXTURE_FORMAT_RGBA64) {
		u16 *p = (u16 *)pixels->buffer;
		for (u32 i = 3; i < pixels->length / 2; i += 4) {
			if (p[i] < 0x3c00) { // Half float 1.0
				return true;
			}
		}
	}
	else if (format == GPU_TEXTURE_FORMAT_RGBA128) {
		f32 *p = (f32 *)pixels->buffer;
		for (u32 i = 3; i < pixels->length / 4; i += 4) {
			if (p[i] < 1.0) {
				return true;
			}
		}
	}
	else {
		for (u32 i = 3; i < pixels->length; i += 4) {
			if (pixels->buffer[i] < 255) {
				return true;
			}
		}
	}
	return false;
}

// A layer texture of res x res texels of bytes_per_texel, empty when its data is missing or corrupt
static gpu_texture_t *import_arm_texture_from_lz4(buffer_t *b, i32 res, i32 bytes_per_texel, i32 format, bool *has_alpha) {
	u64       size   = (u64)res * (u64)res * (u64)bytes_per_texel; // 16K layers pass 2^31 bytes
	buffer_t *pixels = res > 0 && size <= UINT32_MAX ? lz4_decode(b, (u32)size) : NULL;
	if (pixels == NULL) {
		iron_log("Layer data could not be read, the layer starts empty");
		if (res <= 0 || size > UINT32_MAX) {
			res  = 1;
			size = bytes_per_texel;
		}
		pixels = buffer_create((u32)size);
		memset(pixels->buffer, 0, pixels->length);
	}
	if (has_alpha != NULL) {
		*has_alpha = import_arm_has_alpha(pixels, format);
	}
	gpu_texture_t *texture = gpu_create_texture_from_bytes_raw(pixels, res, res, format);
	array_free(pixels);
	free(pixels);
	return texture;
}

// dst of a loaded layer becomes its saved texture
void import_arm_layer_texture(gpu_texture_t *dst, buffer_t *b, i32 res, i32 bytes_per_texel, i32 format, bool *has_alpha) {
	gpu_texture_t *t = import_arm_texture_from_lz4(b, res, bytes_per_texel, format, has_alpha);
	util_copy_texture(dst, t, g_project->is_bgra ? pipes_copy_bgra : pipes_copy);
	gpu_delete_texture(t);
}

// Image nodes are saved by asset name, the saved index only picks between assets sharing a name
static i32 import_arm_image_asset_index(ui_node_button_t *but) {
	char *name  = u8_array_to_string(but->data);
	i32   saved = but->default_value->buffer[0];
	if (import_arm_material_files != NULL) {
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			asset_t *a = g_project->_->assets->buffer[i];
			if (string_equals(a->name, name) && string_array_index_of(import_arm_material_files, a->file) != -1) {
				return i;
			}
		}
	}
	if (saved >= 0 && saved < g_project->_->assets->length && string_equals(g_project->_->assets->buffer[saved]->name, name)) {
		return saved;
	}
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		if (string_equals(g_project->_->assets->buffer[i]->name, name)) {
			return i;
		}
	}
	return 9999; // Texture deleted, use pink now
}

void import_arm_init_nodes(ui_node_t_array_t *nodes) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *node = nodes->buffer[i];
		if (string_equals(node->type, "TEX_IMAGE")) {
			node->buttons->buffer[0]->default_value = f32_array_create_x(import_arm_image_asset_index(node->buttons->buffer[0]));
			node->buttons->buffer[0]->data          = u8_array_create_from_string("");
		}
	}
}

packed_asset_t *import_arm_take_packed_asset(project_t *project, char *abs, char *file, bool copy) {
	if (g_project->packed_assets == NULL) {
		g_project->packed_assets = any_array_create_from_raw((void *[]){}, 0);
	}
	for (i32 i = 0; i < project->packed_assets->length; ++i) {
		packed_asset_t *pa = project->packed_assets->buffer[i];
		pa->name = string_copy(string_replace_all(pa->name, "\\", "/"));
		pa->name     = string_copy(path_normalize(pa->name));
		bool matches = string_equals(pa->name, file) || string_equals(pa->name, abs);
		if (!matches) {
			char *pa_name   = substring(pa->name, string_last_index_of(pa->name, PATH_SEP) + 1, string_length(pa->name));
			char *file_name = substring(file, string_last_index_of(file, PATH_SEP) + 1, string_length(file));
			matches         = string_equals(pa_name, file_name);
		}
		if (matches) {
			pa->name = string_copy(abs);
			if (!project_packed_asset_exists(g_project->packed_assets, pa->name)) {

				if (copy) {
					packed_asset_t *pa_copy = ALLOC_INIT(packed_asset_t, {.name  = string_copy(pa->name),
					                                                      .bytes = u8_array_create_from_array(pa->bytes)}); // Project data is temporary
					pa                      = pa_copy;
				}

				any_array_push(g_project->packed_assets, pa);
			}
			return pa;
		}
	}
	return NULL;
}

static void import_arm_unpack_asset(project_t *project, char *abs, char *file, bool copy) {
	packed_asset_t *pa = import_arm_take_packed_asset(project, abs, file, copy);
	if (pa == NULL) {
		return;
	}
	import_arm_progress_unpacked += ((buffer_t *)pa->bytes)->length;
	gpu_texture_t *image = gpu_create_texture_from_encoded_bytes(pa->bytes, pa->name);
	any_map_set(data_cached_textures, abs, image);
}

typedef struct import_arm_image {
	char               *abs;
	buffer_t           *bytes; // Encoded
	char               *format;
	bool                packed;
	bool                from_disk; // Read when its batch is decoded
	bool                decoded;
	iron_image_pixels_t pixels;
} import_arm_image_t;

typedef struct import_arm_decode {
	import_arm_image_t *images;
	i32                 end;
	volatile int32_t    next;
} import_arm_decode_t;

#define IMPORT_ARM_DECODE_BUDGET  ((i64)256 * 1024 * 1024)
#define IMPORT_ARM_DECODE_THREADS 16

bool import_arm_is_decodable(char *abs) {
	char *ext = path_ext(abs);
	if (any_map_get(import_texture_importers, ext) != NULL) {
		return false;
	}
	return string_equals(ext, "jpg") || string_equals(ext, "jpeg") || string_equals(ext, "png") || string_equals(ext, "hdr");
}

static void import_arm_decode_thread(void *arg) {
	import_arm_decode_t *d = arg;
	for (;;) {
		i32 i = iron_atomic_increment(&d->next);
		if (i >= d->end) {
			break;
		}
		import_arm_image_t *im = &d->images[i];
		if (im->bytes != NULL) {
			im->decoded = iron_decode_image(im->bytes, im->format, &im->pixels);
		}
	}
}

i32 import_arm_decode_images(import_arm_image_t *images, i32 start, i32 count) {
	i64 pixel_bytes = 0;
	i32 end         = start;
	while (end < count) {
		import_arm_image_t *im = &images[end];
		if (im->from_disk && im->bytes == NULL) {
			im->bytes = iron_load_blob(data_resolve_path(im->abs));
		}
		buffer_t *b    = im->bytes;
		i64       size = 0;
		i32       w;
		i32       h;
		i32       comp;
		if (b != NULL && stbi_info_from_memory(b->buffer, b->length, &w, &h, &comp)) {
			size = (i64)w * h * (stbi_is_hdr_from_memory(b->buffer, b->length) ? 16 : 4);
		}
		if (end > start && pixel_bytes + size > IMPORT_ARM_DECODE_BUDGET) {
			break;
		}
		pixel_bytes += size;
		end++;
	}

	import_arm_decode_t d            = {.images = images, .end = end, .next = start};
	i32                 thread_count = iron_hardware_threads();
	if (thread_count > IMPORT_ARM_DECODE_THREADS) {
		thread_count = IMPORT_ARM_DECODE_THREADS;
	}
	if (thread_count > end - start) {
		thread_count = end - start;
	}
	if (thread_count <= 1) {
		import_arm_decode_thread(&d);
		return end;
	}
	iron_thread_t threads[IMPORT_ARM_DECODE_THREADS];
	for (i32 i = 0; i < thread_count; ++i) {
		iron_thread_init(&threads[i], import_arm_decode_thread, &d);
	}
	for (i32 i = 0; i < thread_count; ++i) {
		iron_thread_wait_and_destroy(&threads[i]);
	}
	return end;
}

// Texture file of a material or brush file, relative to its folder (base) or packed into it: imported, pink when missing.
// Its absolute path.
char *import_arm_load_asset(project_t *project, char *file, char *base) {
	file      = string_copy(string_replace_all(file, "\\", "/"));
	char *abs = import_arm_abs(file, base);
	if (project->packed_assets != NULL) {
		abs = string_copy(path_normalize(abs));
		import_arm_unpack_asset(project, abs, file, true);
	}
	if (any_map_get(data_cached_textures, abs) == NULL && !iron_file_exists(abs)) {
		import_arm_make_pink(abs);
	}
	import_texture_run(abs, true);
	return abs;
}
