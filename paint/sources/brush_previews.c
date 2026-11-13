// Strokes of the brushes of the brush library in the brush list (brush_def_t image_row), kept on disk between launches.
// brush_previews/<key>.png in the app's save folder, the key hashes everything that shapes the stroke: the brush settings and
// imported tip, the pen settings it is painted with and BRUSH_PREVIEWS_VERSION. An edited brush gets a new key and renders again. A thread reads and decodes the files on startup, the
// strokes not found render on the main thread one per frame (the paint pipeline is not thread safe) and a thread writes them.
// The PNGs are encoded on the main thread, the deflate state is too large for a thread stack.

#include "global.h"
#include <iron_folder.h>
#include <iron_thread.h>

unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len); // iron_image.c

#define BRUSH_PREVIEWS_VERSION 2    // Bump when the stroke rendering changes, util_render_make_brush_preview() or the tips
#define BRUSH_PREVIEWS_MAX     1024 // BRUSH_LIBRARY_MAX
#define BRUSH_PREVIEWS_FILES   128  // Files kept, the oldest go beyond that, the ones of the current brushes stay

typedef enum {
	BRUSH_PREVIEW_UNKNOWN, // Not looked up on disk for its key yet
	BRUSH_PREVIEW_LOADING,
	BRUSH_PREVIEW_MISSING, // Not on disk, renders
	BRUSH_PREVIEW_READY,
} brush_preview_state_t;

// The pen settings that shape the strokes: they are painted as a Pencil stroke
typedef struct {
	f32 curve;
	f32 min;
	f32 sensitivity;
} brush_previews_pen_t;

static brush_previews_pen_t brush_previews_pen;
static u64                  brush_previews_base;
static bool                 brush_previews_started = false;
static i32                  brush_previews_pending = -1; // Id of the brush whose stroke renders on the next frame
static i32                  brush_previews_loaded  = 0;
static i32                  brush_previews_drawn   = 0;

// Load job: the paths and keys are set before the thread starts, the pixels are read once it finished
static iron_thread_t       brush_previews_load_thread;
static volatile i32        brush_previews_load_state = 0; // 0 - idle, 1 - running, 2 - finished
static i32                 brush_previews_load_count = 0;
static i32                 brush_previews_load_id[BRUSH_PREVIEWS_MAX];
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
	i32 n = brush_library_count();
	return n < BRUSH_PREVIEWS_MAX ? n : BRUSH_PREVIEWS_MAX;
}

static brush_previews_pen_t brush_previews_pen_now() {
	brush_previews_pen_t p;
	memset(&p, 0, sizeof(p));
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

static void brush_previews_make_base() {
	brush_previews_pen  = brush_previews_pen_now();
	u64 base            = 14695981039346656037ULL;
	i32 version         = BRUSH_PREVIEWS_VERSION;
	base                = brush_previews_hash(base, &version, sizeof(version));
	base                = brush_previews_hash(base, &brush_previews_pen, sizeof(brush_previews_pen));
	base                = brush_previews_hash(base, &util_render_material_preview_size, sizeof(util_render_material_preview_size));
	base                = brush_previews_hash(base, &util_render_brush_wave_strip, sizeof(util_render_brush_wave_strip));
	brush_previews_base = base;
}

// Hash of what shapes the stroke of brush d: its settings, and the file of its imported tip
static u64 brush_previews_key_of(brush_def_t *d) {
	brush_settings_t *s = &d->s;
	u64               h = brush_previews_base;
#define BRUSH_PREVIEWS_HASH_F32(key) h = brush_previews_hash(h, &s->key, sizeof(f32));
	BRUSH_LIBRARY_SETTINGS(BRUSH_PREVIEWS_HASH_F32)
#undef BRUSH_PREVIEWS_HASH_F32
	i32 v[] = {s->tip, s->rotation, s->tilt_shading ? 1 : 0};
	h       = brush_previews_hash(h, v, sizeof(v));
	if (s->tip == TIP_IMAGE) {
		if (d->tip_hash == 0) {
			d->tip_hash   = 1;
			size_t   size = 0;
			uint8_t *data = d->tip_file != NULL ? iron_folder_read(d->tip_file, &size) : NULL;
			if (data != NULL) {
				d->tip_hash = brush_previews_hash(14695981039346656037ULL, data, (i32)size) | 1;
				free(data);
			}
		}
		h = brush_previews_hash(h, &d->tip_hash, sizeof(d->tip_hash));
	}
	return h;
}

// A brush whose key changed is looked up again, its current stroke shows meanwhile
static void brush_previews_make_keys() {
	for (i32 i = 0; i < brush_previews_count(); ++i) {
		brush_def_t *d   = brush_library_at(i);
		u64          key = brush_previews_key_of(d);
		if (key != d->row_key) {
			d->row_key = key;
			if (d->row_state != BRUSH_PREVIEW_LOADING) {
				d->row_state = BRUSH_PREVIEW_UNKNOWN;
			}
		}
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
		brush_def_t *d = brush_library_at(i);
		if (d->row_state != BRUSH_PREVIEW_UNKNOWN) {
			continue;
		}
		i32 k                         = brush_previews_load_count++;
		brush_previews_load_id[k]     = d->id;
		brush_previews_load_key[k]    = d->row_key;
		brush_previews_load_path[k]   = strdup(brush_previews_path(d->row_key));
		brush_previews_load_pixels[k] = (iron_image_pixels_t){0};
		d->row_state                  = BRUSH_PREVIEW_LOADING;
	}
	if (brush_previews_load_count == 0) {
		return;
	}
	brush_previews_load_state = 1;
	iron_thread_init(&brush_previews_load_thread, brush_previews_load_run, NULL);
}

static void brush_previews_set(brush_def_t *d, gpu_texture_t *tex, bool rt) {
	if (d->image_row != NULL && d->image_row != tex) {
		gpu_delete_texture(d->image_row);
	}
	d->image_row = tex;
	d->row_rt    = rt;
}

static void brush_previews_load_finish() {
	iron_thread_wait_and_destroy(&brush_previews_load_thread);
	brush_previews_load_state = 0;
	for (i32 k = 0; k < brush_previews_load_count; ++k) {
		brush_def_t         *d  = brush_library_find(brush_previews_load_id[k]);
		iron_image_pixels_t *px = &brush_previews_load_pixels[k];
		free(brush_previews_load_path[k]);
		brush_previews_load_path[k] = NULL;
		if (d == NULL || d->id != brush_previews_load_id[k]) { // Deleted meanwhile
			free(px->data);
			px->data = NULL;
			continue;
		}
		bool current = brush_previews_load_key[k] == d->row_key;
		bool fits = px->data != NULL && px->width == util_render_material_preview_size &&
		            px->height == (i32)math_floor(util_render_material_preview_size * util_render_brush_wave_strip);
		if (current && fits) {
			brush_previews_set(d, gpu_create_texture_from_pixels(px), false); // Frees the pixels
			d->row_state = BRUSH_PREVIEW_READY;
			brush_previews_loaded++;
		}
		else {
			free(px->data);
			px->data                = NULL;
			d->row_state = current ? BRUSH_PREVIEW_MISSING : BRUSH_PREVIEW_UNKNOWN; // The key changed meanwhile, looked up again
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
		snprintf(brush_previews_job_keep[brush_previews_job_keep_count++], 24, "%s", brush_previews_file(brush_library_at(i)->row_key));
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
	return !base_startup_loading() && !render_path_paint_live_layer_locked && !mouse_down("left") && !pen_down("tip") && !ui_brush_creator_shown();
}

static void brush_previews_queue_write(u64 key, gpu_texture_t *rt) {
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
	    (brush_previews_write_t){.path = strdup(brush_previews_path(key)), .png = png, .size = len};
}

static void brush_previews_render(void *_) {
	brush_def_t *d         = brush_library_find(brush_previews_pending);
	brush_previews_pending = -1;
	if (d == NULL || d->row_state != BRUSH_PREVIEW_MISSING || !brush_previews_can_render()) {
		return;
	}
	if (!d->row_rt) { // A stroke read from disk is not a render target
		brush_previews_set(d, NULL, false);
	}
	tab_brushes_render_row(&d->s, brush_library_tip_of(d), &d->image_row);
	if (d->image_row != NULL) {
		d->row_rt = true;
		brush_previews_queue_write(d->row_key, d->image_row);
		d->row_state = BRUSH_PREVIEW_READY;
		brush_previews_drawn++;
	}
	ui_popovers_redraw(-1);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

// Looks the strokes up on disk, during the startup cover
void brush_previews_start() {
	if (brush_previews_started) {
		return;
	}
	brush_previews_started = true;
	brush_previews_make_base();
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
		brush_previews_make_base();
		g_context->brush->preview_ready = false; // The parameters panel stroke too
	}
	// Brushes added, edited, or of other pen settings
	brush_previews_make_keys();

	if (brush_previews_load_state == 2) {
		brush_previews_load_finish();
	}
	if (brush_previews_load_state == 0) {
		brush_previews_load_start();
	}

	// The group shown in the brush list first
	for (i32 pass = 0; pass < 2 && brush_previews_pending == -1 && brush_previews_can_render(); ++pass) {
		for (i32 i = 0; i < brush_previews_count(); ++i) {
			brush_def_t *d = brush_library_at(i);
			if (d->row_state == BRUSH_PREVIEW_MISSING && (pass == 1 || d->group == brush_previews_group)) {
				brush_previews_pending = d->id;
				sys_notify_on_next_frame(&brush_previews_render, NULL);
				break;
			}
		}
	}

	brush_previews_write_update();
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
			n += brush_library_at(i)->row_state != BRUSH_PREVIEW_READY;
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
		brush_def_t *d = brush_library_at(i);
		if (delete_files) {
			iron_delete_file(brush_previews_path(d->row_key));
		}
		d->row_state = BRUSH_PREVIEW_UNKNOWN;
	}
	brush_previews_loaded = 0;
	brush_previews_drawn  = 0;
}
