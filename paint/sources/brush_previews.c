// Strokes of the built-in brushes in the touch brush list, kept on disk between launches.
// brush_previews/<key>.png in the app's save folder, the key hashes everything that shapes the stroke: the brush settings,
// the pen settings it is painted with and BRUSH_PREVIEWS_VERSION. A thread reads and decodes the files on startup, the
// strokes not found render on the main thread one per frame (the paint pipeline is not thread safe) and a thread writes them.
// The PNGs are encoded on the main thread, the deflate state is too large for a thread stack.

#include "global.h"
#include <iron_folder.h>
#include <iron_thread.h>

unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len); // iron_image.c

#define BRUSH_PREVIEWS_VERSION 1 // Bump when the stroke rendering changes, util_render_make_brush_preview() or the tips
#define BRUSH_PREVIEWS_MAX     64
#define BRUSH_PREVIEWS_FILES   64 // Files kept, the oldest go beyond that

typedef enum {
	BRUSH_PREVIEW_UNKNOWN, // Not looked up on disk for its key yet
	BRUSH_PREVIEW_LOADING,
	BRUSH_PREVIEW_MISSING, // Not on disk, renders
	BRUSH_PREVIEW_READY,
} brush_preview_state_t;

// The pen settings that shape the strokes: they are painted as a Pencil stroke
typedef struct {
	i32 dyn[5];
	f32 curve;
	f32 min;
	f32 sensitivity;
} brush_previews_pen_t;

static brush_previews_pen_t  brush_previews_pen;
static bool                  brush_previews_started = false;
static u64                   brush_previews_key[BRUSH_PREVIEWS_MAX];
static brush_preview_state_t brush_previews_state[BRUSH_PREVIEWS_MAX];
static gpu_texture_t        *brush_previews_tex[BRUSH_PREVIEWS_MAX];
static bool                  brush_previews_rt[BRUSH_PREVIEWS_MAX]; // The texture is a render target, rendered here, not read from disk
static i32                   brush_previews_pending = -1;           // Row that renders on the next frame
static slot_brush_t         *brush_previews_brush   = NULL;         // Default brush the rows paint with, the project brush nodes do not show
static i32                   brush_previews_loaded  = 0;
static i32                   brush_previews_drawn   = 0;

// Load job: the paths and keys are set before the thread starts, the pixels are read once it finished
static iron_thread_t       brush_previews_load_thread;
static volatile i32        brush_previews_load_state = 0; // 0 - idle, 1 - running, 2 - finished
static i32                 brush_previews_load_count = 0;
static i32                 brush_previews_load_row[BRUSH_PREVIEWS_MAX];
static u64                 brush_previews_load_key[BRUSH_PREVIEWS_MAX];
static char               *brush_previews_load_path[BRUSH_PREVIEWS_MAX];
static iron_image_pixels_t brush_previews_load_pixels[BRUSH_PREVIEWS_MAX];

// Write job: rendered rows wait in the queue while a write runs
typedef struct {
	char *path;
	u8   *png;
	i32   size;
} brush_previews_write_t;

static brush_previews_write_t brush_previews_queue[BRUSH_PREVIEWS_MAX];
static i32                    brush_previews_queue_count = 0;
static brush_previews_write_t brush_previews_job[BRUSH_PREVIEWS_MAX];
static i32                    brush_previews_job_count = 0;
static char                  *brush_previews_job_dir   = NULL;
static char                   brush_previews_job_keep[BRUSH_PREVIEWS_MAX][24]; // Files of the current keys, never removed
static i32                    brush_previews_job_keep_count = 0;
static iron_thread_t          brush_previews_write_thread;
static volatile i32           brush_previews_write_state = 0; // 0 - idle, 1 - running, 2 - finished

static i32 brush_previews_count() {
	i32 n = builtin_brush_count();
	return n < BRUSH_PREVIEWS_MAX ? n : BRUSH_PREVIEWS_MAX;
}

static brush_previews_pen_t brush_previews_pen_now() {
	brush_previews_pen_t p;
	memset(&p, 0, sizeof(p));
	p.dyn[0]      = g_config->dyn_size;
	p.dyn[1]      = g_config->dyn_opacity;
	p.dyn[2]      = g_config->dyn_flow;
	p.dyn[3]      = g_config->dyn_hardness;
	p.dyn[4]      = g_config->dyn_angle;
	p.curve       = g_config->pressure_curve;
	p.min         = g_config->pressure_min;
	p.sensitivity = g_config->pressure_sensitivity;
	return p;
}

u64 brush_previews_hash(u64 h, void *data, i32 size) {
	u8 *b = data;
	for (i32 i = 0; i < size; ++i) {
		h ^= b[i];
		h *= 1099511628211ULL; // FNV-1a
	}
	return h;
}

static void brush_previews_make_keys() {
	brush_previews_pen = brush_previews_pen_now();
	u64 base           = 14695981039346656037ULL;
	i32 version        = BRUSH_PREVIEWS_VERSION;
	base               = brush_previews_hash(base, &version, sizeof(version));
	base               = brush_previews_hash(base, &brush_previews_pen, sizeof(brush_previews_pen));
	base               = brush_previews_hash(base, &util_render_material_preview_size, sizeof(util_render_material_preview_size));
	base               = brush_previews_hash(base, &util_render_brush_wave_strip, sizeof(util_render_brush_wave_strip));
	for (i32 i = 0; i < brush_previews_count(); ++i) {
		brush_previews_key[i] = builtin_brush_hash(i, base);
	}
}

static char *brush_previews_dir() {
	return string("%sbrush_previews", iron_internal_save_path());
}

static char *brush_previews_file(u64 key) {
	return string("%016llx.png", (unsigned long long)key);
}

static char *brush_previews_path(u64 key) {
	return string("%s%s%s", brush_previews_dir(), PATH_SEP, brush_previews_file(key));
}

// ██╗      ██████╗  █████╗ ██████╗
// Off the main thread: malloc, iron_folder and stb only

static void brush_previews_load_run(void *_) {
	for (i32 k = 0; k < brush_previews_load_count; ++k) {
		iron_image_pixels_t *out = &brush_previews_load_pixels[k];
		out->data                = NULL;
		size_t   size            = 0;
		uint8_t *data            = iron_folder_read(brush_previews_load_path[k], &size);
		if (data == NULL) {
			continue;
		}
		int            w, h, comp;
		unsigned char *pixels = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
		free(data);
		if (pixels == NULL) {
			continue;
		}
		out->data   = pixels;
		out->width  = w;
		out->height = h;
		out->format = GPU_TEXTURE_FORMAT_RGBA32;
	}
	brush_previews_load_state = 2;
}

static void brush_previews_load_start() {
	brush_previews_load_count = 0;
	for (i32 i = 0; i < brush_previews_count(); ++i) {
		if (brush_previews_state[i] != BRUSH_PREVIEW_UNKNOWN) {
			continue;
		}
		i32 k                           = brush_previews_load_count++;
		brush_previews_load_row[k]      = i;
		brush_previews_load_key[k]      = brush_previews_key[i];
		brush_previews_load_path[k]     = strdup(brush_previews_path(brush_previews_key[i]));
		brush_previews_load_pixels[k]   = (iron_image_pixels_t){0};
		brush_previews_state[i]         = BRUSH_PREVIEW_LOADING;
	}
	if (brush_previews_load_count == 0) {
		return;
	}
	brush_previews_load_state = 1;
	iron_thread_init(&brush_previews_load_thread, brush_previews_load_run, NULL);
}

static void brush_previews_set(i32 i, gpu_texture_t *tex, bool rt) {
	if (brush_previews_tex[i] != NULL && brush_previews_tex[i] != tex) {
		gpu_delete_texture(brush_previews_tex[i]);
	}
	brush_previews_tex[i] = tex;
	brush_previews_rt[i]  = rt;
}

static void brush_previews_load_finish() {
	iron_thread_wait_and_destroy(&brush_previews_load_thread);
	brush_previews_load_state = 0;
	for (i32 k = 0; k < brush_previews_load_count; ++k) {
		i32                  i  = brush_previews_load_row[k];
		iron_image_pixels_t *px = &brush_previews_load_pixels[k];
		free(brush_previews_load_path[k]);
		brush_previews_load_path[k] = NULL;
		bool current                = brush_previews_load_key[k] == brush_previews_key[i];
		bool fits = px->data != NULL && px->width == util_render_material_preview_size &&
		            px->height == (i32)math_floor(util_render_material_preview_size * util_render_brush_wave_strip);
		if (current && fits) {
			brush_previews_set(i, gpu_create_texture_from_pixels(px), false); // Frees the pixels
			brush_previews_state[i] = BRUSH_PREVIEW_READY;
			brush_previews_loaded++;
		}
		else {
			free(px->data);
			px->data                = NULL;
			brush_previews_state[i] = current ? BRUSH_PREVIEW_MISSING : BRUSH_PREVIEW_UNKNOWN; // The key changed meanwhile, looked up again
		}
	}
	brush_previews_load_count = 0;
	ui_popovers_redraw(-1);
}

// ██╗    ██╗██████╗ ██╗████████╗███████╗
// Off the main thread: malloc, iron_folder and stb only

static bool brush_previews_kept(char *name) {
	for (i32 k = 0; k < brush_previews_job_keep_count; ++k) {
		if (strcmp(name, brush_previews_job_keep[k]) == 0) {
			return true;
		}
	}
	return false;
}

// The oldest files beyond BRUSH_PREVIEWS_FILES go, the ones of the current keys stay
static void brush_previews_prune() {
	i32               count   = 0;
	iron_dir_entry_t *entries = iron_folder_list(brush_previews_job_dir, &count);
	if (entries == NULL) {
		return;
	}
	i32 pngs = 0;
	for (i32 i = 0; i < count; ++i) {
		size_t len = strlen(entries[i].name);
		if (!entries[i].is_dir && len > 4 && strcmp(entries[i].name + len - 4, ".png") == 0) {
			entries[pngs++] = entries[i];
		}
		else {
			free(entries[i].name);
			entries[i].name = NULL;
		}
	}
	// Oldest first
	for (i32 i = 1; i < pngs; ++i) {
		for (i32 j = i; j > 0 && entries[j - 1].mtime > entries[j].mtime; --j) {
			iron_dir_entry_t t = entries[j];
			entries[j]         = entries[j - 1];
			entries[j - 1]     = t;
		}
	}
	for (i32 i = 0; i < pngs && pngs - i > BRUSH_PREVIEWS_FILES; ++i) {
		if (brush_previews_kept(entries[i].name)) {
			continue;
		}
		size_t size = strlen(brush_previews_job_dir) + strlen(entries[i].name) + 8;
		char  *path = malloc(size);
		snprintf(path, size, "%s%s%s", brush_previews_job_dir, PATH_SEP, entries[i].name);
		remove(path);
		free(path);
	}
	iron_folder_list_free(entries, pngs); // The other names were freed above
}

static void brush_previews_write_run(void *_) {
	for (i32 k = 0; k < brush_previews_job_count; ++k) {
		brush_previews_write_t *j = &brush_previews_job[k];
		// A file is complete or not there, written aside then moved in place
		size_t size = strlen(j->path) + 8;
		char  *tmp  = malloc(size);
		snprintf(tmp, size, "%s.tmp", j->path);
		if (iron_folder_write(tmp, j->png, j->size)) {
			remove(j->path);
			rename(tmp, j->path);
		}
		else {
			remove(tmp);
		}
		free(tmp);
		free(j->png);
		free(j->path);
	}
	brush_previews_prune();
	brush_previews_write_state = 2;
}

static void brush_previews_write_update() {
	if (brush_previews_write_state == 2) {
		iron_thread_wait_and_destroy(&brush_previews_write_thread);
		brush_previews_write_state = 0;
		brush_previews_job_count   = 0;
	}
	if (brush_previews_write_state != 0 || brush_previews_queue_count == 0) {
		return;
	}
	free(brush_previews_job_dir);
	brush_previews_job_dir = strdup(brush_previews_dir());
	iron_create_directory(brush_previews_job_dir);
	brush_previews_job_keep_count = 0;
	for (i32 i = 0; i < brush_previews_count(); ++i) {
		snprintf(brush_previews_job_keep[brush_previews_job_keep_count++], 24, "%s", brush_previews_file(brush_previews_key[i]));
	}
	for (i32 k = 0; k < brush_previews_queue_count; ++k) {
		brush_previews_job[k] = brush_previews_queue[k];
	}
	brush_previews_job_count   = brush_previews_queue_count;
	brush_previews_queue_count = 0;
	brush_previews_write_state = 1;
	iron_thread_init(&brush_previews_write_thread, brush_previews_write_run, NULL);
}

// ██████╗ ███████╗███╗   ██╗██████╗ ███████╗██████╗
// Main thread

// Painting the stroke borrows the live layer, the camera and the paint shader: not while the user paints or a project loads
static bool brush_previews_can_render() {
	return !base_startup_loading() && !render_path_paint_live_layer_locked && !mouse_down("left") && !pen_down("tip");
}

static void brush_previews_queue_write(i32 i, gpu_texture_t *rt) {
	if (brush_previews_queue_count >= BRUSH_PREVIEWS_MAX) {
		return;
	}
	buffer_t *b = gpu_get_texture_pixels(rt);
	i32       n = rt->width * rt->height * 4;
	if (b == NULL || b->buffer == NULL || b->length < n) {
		return;
	}
	u8 *pixels = malloc(n);
	memcpy(pixels, b->buffer, n);
	gpu_release_texture_pixels(rt);
#ifdef IRON_BGRA
	for (i32 p = 0; p < n; p += 4) {
		u8 c          = pixels[p];
		pixels[p]     = pixels[p + 2];
		pixels[p + 2] = c;
	}
#endif
	int            len = 0;
	unsigned char *png = stbi_write_png_to_mem(pixels, rt->width * 4, rt->width, rt->height, 4, &len);
	free(pixels);
	if (png == NULL) {
		return;
	}
	brush_previews_queue[brush_previews_queue_count++] =
	    (brush_previews_write_t){.path = strdup(brush_previews_path(brush_previews_key[i])), .png = png, .size = len};
}

static void brush_previews_render(void *_) {
	i32 i                  = brush_previews_pending;
	brush_previews_pending = -1;
	if (i < 0 || i >= brush_previews_count() || brush_previews_state[i] != BRUSH_PREVIEW_MISSING || !brush_previews_can_render()) {
		return;
	}

	slot_brush_t *_brush          = g_context->brush;
	i32           _builtin        = g_context->brush_builtin;
	f32           _hardness       = g_context->brush_hardness;
	f32           _spacing        = g_context->brush_spacing;
	f32           _scatter        = g_context->brush_scatter;
	f32           _jitter_size    = g_context->brush_jitter_size;
	f32           _jitter_angle   = g_context->brush_jitter_angle;
	f32           _jitter_opacity = g_context->brush_jitter_opacity;
	f32           _flow           = g_context->brush_flow;
	if (brush_previews_brush == NULL) {
		brush_previews_brush = slot_brush_create(NULL);
	}
	g_context->brush = brush_previews_brush;
	builtin_brush_settings(i);
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	util_render_make_brush_preview();

	gpu_texture_t *src = g_context->brush->image_wave;
	if (src != NULL) {
		if (!brush_previews_rt[i] || brush_previews_tex[i] == NULL) {
			brush_previews_set(i, gpu_create_render_target(src->width, src->height, GPU_TEXTURE_FORMAT_RGBA32), true);
		}
		gpu_texture_t *dst     = brush_previews_tex[i];
		gpu_texture_t *current = _draw_current;
		bool           in_use  = gpu_in_use;
		if (in_use)
			draw_end();
		draw_begin(dst, true, 0x00000000);
		draw_set_color(0xffffffff);
		draw_image(src, 0, 0);
		draw_end();
		brush_previews_queue_write(i, dst);
		if (in_use)
			draw_begin(current, false, 0);
		brush_previews_state[i] = BRUSH_PREVIEW_READY;
		brush_previews_drawn++;
	}

	g_context->brush                = _brush;
	g_context->brush_builtin        = _builtin;
	g_context->brush_hardness       = _hardness;
	g_context->brush_spacing        = _spacing;
	g_context->brush_scatter        = _scatter;
	g_context->brush_jitter_size    = _jitter_size;
	g_context->brush_jitter_angle   = _jitter_angle;
	g_context->brush_jitter_opacity = _jitter_opacity;
	g_context->brush_flow           = _flow;
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	ui_popovers_redraw(-1);
}

// Looks the strokes up on disk, during the startup cover
void brush_previews_start() {
	if (brush_previews_started) {
		return;
	}
	brush_previews_started = true;
	brush_previews_make_keys();
	brush_previews_load_start();
}

// Every frame: takes what the threads did, renders a missing stroke
void brush_previews_update() {
	if (!brush_previews_started) {
		return;
	}

	// Pen settings changed: the strokes of the new settings are looked up, the current ones show meanwhile
	brush_previews_pen_t pen = brush_previews_pen_now();
	if (memcmp(&pen, &brush_previews_pen, sizeof(pen)) != 0) {
		brush_previews_make_keys();
		for (i32 i = 0; i < brush_previews_count(); ++i) {
			if (brush_previews_state[i] != BRUSH_PREVIEW_LOADING) {
				brush_previews_state[i] = BRUSH_PREVIEW_UNKNOWN;
			}
		}
		g_context->brush->preview_ready = false; // The parameters panel stroke too
	}

	if (brush_previews_load_state == 2) {
		brush_previews_load_finish();
	}
	if (brush_previews_load_state == 0) {
		brush_previews_load_start();
	}

	if (brush_previews_pending == -1 && brush_previews_can_render()) {
		for (i32 i = 0; i < brush_previews_count(); ++i) {
			if (brush_previews_state[i] == BRUSH_PREVIEW_MISSING) {
				brush_previews_pending = i;
				sys_notify_on_next_frame(&brush_previews_render, NULL);
				break;
			}
		}
	}

	brush_previews_write_update();
}

// Stroke of built-in brush i, NULL until loaded or rendered
gpu_texture_t *brush_previews_row(i32 i) {
	return i >= 0 && i < brush_previews_count() ? brush_previews_tex[i] : NULL;
}

// For tests: 0 - rows loaded from disk, 1 - rows rendered, 2 - rows not ready, 3 - rows waiting to be written, 4 - rows
i32 brush_previews_stat(i32 what) {
	if (what == 4) {
		return brush_previews_count();
	}
	if (what == 0) {
		return brush_previews_loaded;
	}
	if (what == 1) {
		return brush_previews_drawn;
	}
	if (what == 2) {
		i32 n = 0;
		for (i32 i = 0; i < brush_previews_count(); ++i) {
			n += brush_previews_state[i] != BRUSH_PREVIEW_READY;
		}
		return n;
	}
	return brush_previews_queue_count + brush_previews_job_count;
}

// For tests: the rows are looked up again, with delete_files they render again too
void brush_previews_forget(bool delete_files) {
	if (brush_previews_load_state != 0 || brush_previews_write_state == 1 || brush_previews_queue_count > 0) {
		return; // Busy, asked again later
	}
	for (i32 i = 0; i < brush_previews_count(); ++i) {
		if (delete_files) {
			iron_delete_file(brush_previews_path(brush_previews_key[i]));
		}
		brush_previews_state[i] = BRUSH_PREVIEW_UNKNOWN;
	}
	brush_previews_loaded = 0;
	brush_previews_drawn  = 0;
}
