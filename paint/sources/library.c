// Material library: a user picked folder of texture folders (any nesting), each folder becomes a material.
// For every folder a <Name>.arm material file and a <Name>_icon.png preview are saved next to its textures.
// The Library tab only holds the previews, textures are loaded when a material is picked.
//
// Work is spread so the app stays usable:
// - Scan thread: walks the folder tree, finds material folders and which ones are already done. Folders not
//   modified since the last scan are taken from the index without being read, found folders are handed
//   over while the scan goes on so importing starts right away.
// - Load thread: reads and decodes the textures of the next folder, scaled down for the preview,
//   then writes the files of the previous folder.
// - Main thread: uploads the small textures, renders the preview sphere (one folder at a time,
//   paused while the user is touching the screen).
// - Icon thread: reads the previews of the visible library items.
// A folder is done once its .arm file is newer than all its textures, so the import resumes
// where it left off after a restart.

#include "global.h"
#include <iron_folder.h>
#include <iron_thread.h>

#define LIBRARY_TEX_RES 512   // Textures are scaled down to this for the preview render
#define LIBRARY_ICON_RES 128  // Size of the previews held in memory
#define LIBRARY_MAX_ICONS 400 // Loaded previews, the ones not seen for the longest are unloaded
#define LIBRARY_MAX_DEPTH 16

struct library_entry { // library_entry_t, types.h
	char          *rel;  // Folder path relative to the library root
	char          *name; // Folder name
	char          *maps[IMPORT_FOLDER_MAPS];
	bool           done;
	double         mtime;       // Folder modification time when it was last read, 0 when unknown
	bool           has_subdirs; // Always read, the folders inside may have changed
	bool           imported;    // Done in this session, a scan started before the files were written does not undo it
	bool           queued;
	i32            seen; // Scan that found it last
	gpu_texture_t *icon;
	i32            icon_state; // 0 - not loaded, 1 - loading, 2 - loaded, 3 - failed
	i32            last_seen;  // Frame the icon was last drawn
};

static char             *library_root    = NULL; // Accessible folder path, NULL when not set
static library_entry_t **library_entries = NULL; // Main thread
static i32               library_count   = 0;
static i32               library_done    = 0;
static i32               library_icons   = 0;

// Scan
static iron_thread_t     library_scan_thread;
static volatile i32      library_scan_state = 0; // 0 - idle, 1 - running, 2 - finished
static iron_mutex_t      library_scan_mutex;      // Guards the found folders
static library_entry_t **library_scan_out   = NULL;
static i32               library_scan_count = 0;
static i32               library_scan_cap   = 0;
static char             *library_scan_root  = NULL;
static library_entry_t **library_scan_cache = NULL; // Copies of the known folders sorted by rel, read by the scan thread
static i32               library_scan_cache_count = 0;
static i32               library_scan_gen         = 0;

// Import
typedef struct library_job {
	library_entry_t    *entry; // Folder to load, NULL when there is only writing to do
	char               *dir;
	char               *maps[IMPORT_FOLDER_MAPS];
	iron_image_pixels_t pixels[IMPORT_FOLDER_MAPS];
	// Files of the previous folder, written by the thread first
	library_entry_t *written;
	char            *icon_path;
	uint8_t         *icon_data;
	size_t           icon_size;
	char            *arm_path;
	uint8_t         *arm_data;
	size_t           arm_size;
	bool             write_ok;
} library_job_t;

static iron_thread_t     library_load_thread;
static volatile i32      library_load_state = 0; // 0 - idle, 1 - running, 2 - finished
static library_job_t    *library_job        = NULL;
static library_entry_t **library_queue      = NULL; // Folders left to import, main thread, NULL for removed ones
static i32               library_queue_len  = 0;
static i32               library_queue_cap  = 0;
static i32               library_queue_pos  = 0;
static library_job_t    *library_pending    = NULL; // Files waiting for the next load thread
static i32               library_last_frame = 0;
static i32               library_failed     = 0;

// Icons
static iron_thread_t     library_icon_thread;
static volatile i32      library_icon_state  = 0;
static library_entry_t  *library_icon_entry  = NULL;
static char             *library_icon_path   = NULL;
static iron_image_pixels_t library_icon_pixels = {0};

static char *library_join(const char *a, const char *b) {
	size_t la = strlen(a);
	size_t lb = strlen(b);
	char  *s  = malloc(la + lb + 2);
	memcpy(s, a, la);
	s[la] = '/';
	memcpy(s + la + 1, b, lb + 1);
	return s;
}

static char *library_dir(library_entry_t *e) {
	return string("%s/%s", library_root, e->rel);
}

// ███████╗ ██████╗ █████╗ ███╗   ██╗
// Thread safe: plain C only, no string_tmp or engine caches

static bool library_ends_with(const char *s, const char *end) {
	size_t ls = strlen(s);
	size_t le = strlen(end);
	return ls >= le && strcmp(s + ls - le, end) == 0;
}

static bool library_in(const char *tag, const char **list) {
	for (i32 i = 0; list[i] != NULL; ++i) {
		if (strcmp(tag, list[i]) == 0) {
			return true;
		}
	}
	return false;
}

// Which material map a file is, -1 when none. Same tags as the folder import (iron_path.c)
static i32 library_map_type(const char *file) {
	const char *dot = strrchr(file, '.');
	if (dot == NULL) {
		return -1;
	}
	char ext[8];
	i32  n = 0;
	for (const char *c = dot + 1; *c != 0 && n < 7; ++c) {
		ext[n++] = (char)tolower(*c);
	}
	ext[n]                    = 0;
	static const char *exts[] = {"jpg", "jpeg", "png", "tga", "bmp", "psd", "gif", NULL};
	if (!library_in(ext, exts)) {
		return -1;
	}
	char base[256];
	n = 0;
	for (const char *c = file; c < dot && n < 255; ++c) {
		base[n++] = (char)tolower(*c);
	}
	base[n] = 0;
	if (library_ends_with(base, "_icon") || library_ends_with(base, "_preview")) {
		return -1;
	}
	const char *sep = NULL;
	for (const char *c = base; *c != 0; ++c) {
		if (*c == '_' || *c == '-') {
			sep = c;
		}
	}
	if (sep == NULL) {
		return -1;
	}
	const char        *tag              = sep + 1;
	static const char *base_color[]     = {"albedo", "alb", "basecol", "basecolor", "diffuse", "diff", "base", "bc", "d", "color", "col", NULL};
	static const char *opacity[]        = {"opac", "opacity", "alpha", NULL};
	static const char *occlusion[]      = {"ao", "occlusion", "ambientocclusion", "o", "occ", NULL};
	static const char *roughness[]      = {"roughness", "rough", "r", "rgh", NULL};
	static const char *metallic[]       = {"metallic", "metal", "metalness", "m", "met", NULL};
	static const char *normal[]         = {"normal", "normals", "nor", "n", "nrm", "normalgl", NULL};
	static const char *height[]         = {"displacement", "height", "h", "disp", NULL};
	static const char **lists[]         = {base_color, opacity, occlusion, roughness, metallic, normal, height};
	for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
		if (library_in(tag, lists[i])) {
			return i;
		}
	}
	return -1;
}

static void library_scan_push(library_entry_t *e) {
	iron_mutex_lock(&library_scan_mutex);
	if (library_scan_count == library_scan_cap) {
		library_scan_cap = library_scan_cap == 0 ? 256 : library_scan_cap * 2;
		library_scan_out = realloc(library_scan_out, sizeof(library_entry_t *) * library_scan_cap);
	}
	library_scan_out[library_scan_count++] = e;
	iron_mutex_unlock(&library_scan_mutex);
}

static i32 library_compare_rel(const void *a, const void *b) {
	return strcmp((*(library_entry_t **)a)->rel, (*(library_entry_t **)b)->rel);
}

static library_entry_t *library_find(library_entry_t **entries, i32 count, const char *rel) {
	library_entry_t   key = {.rel = (char *)rel};
	library_entry_t  *k   = &key;
	library_entry_t **r   = bsearch(&k, entries, count, sizeof(library_entry_t *), library_compare_rel);
	return r != NULL ? *r : NULL;
}

static library_entry_t *library_entry_copy(library_entry_t *src) {
	library_entry_t *e = calloc(1, sizeof(library_entry_t));
	e->rel             = strdup(src->rel);
	e->name            = strdup(src->name);
	for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
		e->maps[i] = src->maps[i] != NULL ? strdup(src->maps[i]) : NULL;
	}
	e->done        = src->done;
	e->mtime       = src->mtime;
	e->has_subdirs = src->has_subdirs;
	return e;
}

static void library_scan_dir(const char *dir, const char *rel, const char *name, double mtime, i32 depth) {
	int               count   = 0;
	iron_dir_entry_t *entries = iron_folder_list(dir, &count);
	if (entries == NULL) {
		return;
	}

	library_entry_t *e      = calloc(1, sizeof(library_entry_t));
	double           newest = 0;
	bool             found  = false;
	for (i32 i = 0; i < count; ++i) {
		if (entries[i].is_dir) {
			e->has_subdirs = true;
			continue;
		}
		i32 t = library_map_type(entries[i].name);
		if (t >= 0 && e->maps[t] == NULL) {
			e->maps[t] = strdup(entries[i].name);
			found      = true;
			if (entries[i].mtime > newest) {
				newest = entries[i].mtime;
			}
		}
	}

	if (found) {
		// Done when both files exist and the material file is newer than the textures
		size_t ln       = strlen(name);
		char  *arm_name = malloc(ln + 5);
		char  *ico_name = malloc(ln + 10);
		snprintf(arm_name, ln + 5, "%s.arm", name);
		snprintf(ico_name, ln + 10, "%s_icon.png", name);
		double arm_mtime = -1;
		bool   has_icon  = false;
		for (i32 i = 0; i < count; ++i) {
			if (strcmp(entries[i].name, arm_name) == 0) {
				arm_mtime = entries[i].mtime;
			}
			if (strcmp(entries[i].name, ico_name) == 0) {
				has_icon = true;
			}
		}
		free(arm_name);
		free(ico_name);
		e->rel   = strdup(rel);
		e->name  = strdup(name);
		e->done  = has_icon && arm_mtime >= newest;
		e->mtime = mtime;
		library_scan_push(e);
	}
	else {
		for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
			free(e->maps[i]);
		}
		free(e);
	}

	if (depth < LIBRARY_MAX_DEPTH) {
		for (i32 i = 0; i < count; ++i) {
			if (!entries[i].is_dir) {
				continue;
			}
			char *sub_rel = rel[0] == 0 ? strdup(entries[i].name) : library_join(rel, entries[i].name);
			// Unchanged since the last scan: no need to read it over the network
			library_entry_t *known = library_find(library_scan_cache, library_scan_cache_count, sub_rel);
			if (known != NULL && !known->has_subdirs && known->mtime > 0 && fabs(known->mtime - entries[i].mtime) < 0.001) {
				library_scan_push(library_entry_copy(known));
			}
			else {
				char *sub = library_join(dir, entries[i].name);
				library_scan_dir(sub, sub_rel, entries[i].name, entries[i].mtime, depth + 1);
				free(sub);
			}
			free(sub_rel);
		}
	}
	iron_folder_list_free(entries, count);
}

static void library_scan_run(void *_) {
	library_scan_dir(library_scan_root, "", "", 0, 0);
	library_scan_state = 2;
}

static void library_entry_free(library_entry_t *e) {
	free(e->rel);
	free(e->name);
	for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
		free(e->maps[i]);
	}
	if (e->icon != NULL) {
		gpu_delete_texture(e->icon);
	}
	free(e);
}

// ██╗      ██████╗  █████╗ ██████╗

// Box filter down to at most LIBRARY_TEX_RES
static void library_downscale(iron_image_pixels_t *p, i32 max) {
	i32 f = 1;
	while (p->width / f > max || p->height / f > max) {
		f *= 2;
	}
	if (f == 1) {
		return;
	}
	i32            w   = p->width / f;
	i32            h   = p->height / f;
	unsigned char *out = malloc(w * h * 4);
	for (i32 y = 0; y < h; ++y) {
		for (i32 x = 0; x < w; ++x) {
			u32 sum[4] = {0};
			for (i32 sy = 0; sy < f; ++sy) {
				unsigned char *row = p->data + ((y * f + sy) * p->width + x * f) * 4;
				for (i32 sx = 0; sx < f; ++sx) {
					sum[0] += row[sx * 4];
					sum[1] += row[sx * 4 + 1];
					sum[2] += row[sx * 4 + 2];
					sum[3] += row[sx * 4 + 3];
				}
			}
			for (i32 c = 0; c < 4; ++c) {
				out[(y * w + x) * 4 + c] = (unsigned char)(sum[c] / (f * f));
			}
		}
	}
	stbi_image_free(p->data);
	p->data   = out;
	p->width  = w;
	p->height = h;
}

static bool library_decode(const char *path, iron_image_pixels_t *out, i32 max) {
	size_t   size = 0;
	uint8_t *data = iron_folder_read(path, &size);
	if (data == NULL) {
		return false;
	}
	int            w, h, comp;
	unsigned char *pixels = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
	free(data);
	if (pixels == NULL) {
		return false;
	}
	out->data   = pixels;
	out->width  = w;
	out->height = h;
	out->format = GPU_TEXTURE_FORMAT_RGBA32;
	library_downscale(out, max);
	if (out->data != pixels) {
		return true; // Already copied by the downscale
	}
	// Keep malloc ownership consistent for free()
	unsigned char *copy = malloc(w * h * 4);
	memcpy(copy, pixels, w * h * 4);
	stbi_image_free(pixels);
	out->data = copy;
	return true;
}

static void library_load_run(void *_) {
	library_job_t *job = library_job;
	if (job->written != NULL) {
		job->write_ok = iron_folder_write(job->icon_path, job->icon_data, job->icon_size) && iron_folder_write(job->arm_path, job->arm_data, job->arm_size);
	}
	if (job->entry != NULL) {
		for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
			if (job->maps[i] != NULL) {
				char *path = library_join(job->dir, job->maps[i]);
				if (!library_decode(path, &job->pixels[i], LIBRARY_TEX_RES)) {
					job->pixels[i].data = NULL;
				}
				free(path);
			}
		}
	}
	library_load_state = 2;
}

static void library_job_free(library_job_t *job) {
	free(job->dir);
	for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
		free(job->maps[i]);
		free(job->pixels[i].data);
	}
	free(job->icon_path);
	free(job->icon_data);
	free(job->arm_path);
	free(job->arm_data);
	free(job);
}

// Starts the load thread with the next folder and the files of the previous one
static void library_start_load() {
	library_entry_t *next = NULL;
	while (library_queue_pos < library_queue_len && next == NULL) {
		library_entry_t *e = library_queue[library_queue_pos++];
		if (e == NULL) {
			continue;
		}
		e->queued = false;
		if (!e->done) {
			next = e;
		}
	}
	if (next == NULL && library_pending == NULL) {
		return;
	}
	library_job_t *job = library_pending != NULL ? library_pending : calloc(1, sizeof(library_job_t));
	library_pending    = NULL;
	if (next != NULL) {
		job->entry = next;
		job->dir   = strdup(library_dir(next));
		for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
			job->maps[i] = next->maps[i] != NULL ? strdup(next->maps[i]) : NULL;
		}
	}
	library_job        = job;
	library_load_state = 1;
	iron_thread_init(&library_load_thread, library_load_run, NULL);
}

// Uploads the textures, renders the preview and encodes the files, all on the main thread
static void library_render(library_job_t *job) {
	library_entry_t *e   = job->entry;
	char            *dir = string_copy(job->dir);

	slot_material_t *saved_material = g_context->material;
	asset_t         *saved_texture  = g_context->texture;
	i32              assets_before  = g_project->_->assets->length;
	bool             cached[IMPORT_FOLDER_MAPS] = {0};
	bool             any                        = false;

	for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
		if (job->maps[i] == NULL || job->pixels[i].data == NULL) {
			continue;
		}
		any        = true;
		char *path = string("%s%s%s", dir, PATH_SEP, job->maps[i]);
		if (any_map_get(data_cached_textures, path) == NULL) {
			gpu_texture_t *tex = gpu_create_texture_from_pixels(&job->pixels[i]); // Frees the pixels
			any_map_set(data_cached_textures, path, tex);
			cached[i] = true;
		}
		bool exists = false;
		for (i32 j = 0; j < g_project->_->assets->length; ++j) {
			if (string_equals(g_project->_->assets->buffer[j]->file, path)) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			asset_t *a = ALLOC_INIT(asset_t, {.name = string_copy(job->maps[i]), .file = path, .id = g_project->_->next_asset_id++});
			a->image   = any_map_get(data_cached_textures, path);
			any_array_push(g_project->_->assets, a);
		}
	}
	if (!any) {
		iron_log("[library] no readable textures in %s", dir);
		library_failed++;
		return;
	}

	// Maps that could be read
	char *maps[IMPORT_FOLDER_MAPS];
	for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
		bool loaded = job->maps[i] != NULL && any_map_get(data_cached_textures, string("%s%s%s", dir, PATH_SEP, job->maps[i])) != NULL;
		maps[i]     = loaded ? job->maps[i] : NULL;
	}
	slot_material_t *m  = import_folder_make_material(e->name, dir, maps);
	g_context->material = m;
	util_render_make_material_preview();

	// Files of the folder, written by the next load thread
	buffer_t *pixels = buffer_half_to_u8(gpu_get_texture_pixels(m->image));
#ifdef IRON_BGRA
	pixels = buffer_bgra_swap(pixels);
#endif
	buffer_t      *png = iron_encode_png(pixels, m->image->width, m->image->height, 0);
	buffer_t      *arm = export_arm_encode_library_material(m);
	library_job_t *out = calloc(1, sizeof(library_job_t));
	out->written       = e;
	out->icon_path     = strdup(string("%s/%s_icon.png", dir, e->name));
	out->icon_size     = png->length;
	out->icon_data     = malloc(png->length);
	memcpy(out->icon_data, png->buffer, png->length);
	out->arm_path = strdup(string("%s/%s.arm", dir, e->name));
	out->arm_size = arm->length;
	out->arm_data = malloc(arm->length);
	memcpy(out->arm_data, arm->buffer, arm->length);
	library_pending = out;

	// Preview icon right away, no need to read it back
	if (e->icon == NULL) {
		e->icon       = gpu_create_render_target(LIBRARY_ICON_RES, LIBRARY_ICON_RES, GPU_TEXTURE_FORMAT_RGBA32);
		e->icon_state = 2;
		draw_begin(e->icon, true, 0x00000000);
		draw_scaled_image(m->image, 0, 0, LIBRARY_ICON_RES, LIBRARY_ICON_RES);
		draw_end();
		library_icons++;
	}

	// Restore
	g_context->material = saved_material;
	g_context->texture  = saved_texture;
	for (i32 i = assets_before; i < g_project->_->assets->length; ++i) {
		free(g_project->_->assets->buffer[i]);
	}
	g_project->_->assets->length = assets_before;
	for (i32 i = 0; i < IMPORT_FOLDER_MAPS; ++i) {
		if (cached[i]) {
			data_delete_texture(string("%s%s%s", dir, PATH_SEP, job->maps[i]));
		}
	}
	slot_material_unload(m);
	g_context->ddirty = 2;
}

// ██╗ ██████╗ ██████╗ ███╗   ██╗███████╗

static void library_icon_run(void *_) {
	if (!library_decode(library_icon_path, &library_icon_pixels, LIBRARY_ICON_RES)) {
		library_icon_pixels.data = NULL;
	}
	library_icon_state = 2;
}

// Asks for the preview of an item that is drawn, returns it once loaded
gpu_texture_t *library_entry_icon(library_entry_t *e) {
	e->last_seen = g_context->frame;
	if (e->icon_state == 0 && library_icon_state == 0) {
		e->icon_state      = 1;
		library_icon_entry = e;
		free(library_icon_path);
		char *dir         = library_dir(e);
		library_icon_path = strdup(string("%s/%s_icon.png", dir, e->name));
		library_icon_state = 1;
		iron_thread_init(&library_icon_thread, library_icon_run, NULL);
	}
	return e->icon_state == 2 ? e->icon : NULL;
}

static void library_update_icons() {
	if (library_icon_state == 2) {
		iron_thread_wait_and_destroy(&library_icon_thread);
		library_entry_t *e = library_icon_entry;
		if (library_icon_pixels.data != NULL && e != NULL) {
			e->icon       = gpu_create_texture_from_pixels(&library_icon_pixels);
			e->icon_state = 2;
			library_icons++;
		}
		else if (e != NULL) {
			e->icon_state = 3;
		}
		library_icon_entry = NULL;
		library_icon_state = 0;
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
	}

	// Unload the previews not drawn for the longest
	while (library_icons > LIBRARY_MAX_ICONS) {
		library_entry_t *oldest = NULL;
		for (i32 i = 0; i < library_count; ++i) {
			library_entry_t *e = library_entries[i];
			if (e->icon != NULL && (oldest == NULL || e->last_seen < oldest->last_seen)) {
				oldest = e;
			}
		}
		if (oldest == NULL) {
			break;
		}
		gpu_delete_texture(oldest->icon);
		oldest->icon       = NULL;
		oldest->icon_state = 0;
		library_icons--;
	}
}

// ██╗███╗   ██╗██████╗ ███████╗██╗  ██╗
// Done folders are remembered so the Library tab fills right away on the next launch

static char *library_index_path() {
	return string("%slibrary_index.txt", iron_internal_save_path());
}

// One line per material folder: rel, name, modification time, flags (1 - done, 2 - has folders inside), the 7 maps
static void library_save_index() {
	size_t cap = 4096;
	size_t len = 0;
	char  *s   = malloc(cap);
	len += snprintf(s, cap, "v2\n");
	for (i32 i = 0; i < library_count; ++i) {
		library_entry_t *e    = library_entries[i];
		size_t           need = strlen(e->rel) + strlen(e->name) + 64;
		for (i32 k = 0; k < IMPORT_FOLDER_MAPS; ++k) {
			need += e->maps[k] != NULL ? strlen(e->maps[k]) + 1 : 1;
		}
		while (len + need + 1 > cap) {
			cap *= 2;
			s = realloc(s, cap);
		}
		len += snprintf(s + len, cap - len, "%s\t%s\t%.6f\t%d", e->rel, e->name, e->mtime, (e->done ? 1 : 0) | (e->has_subdirs ? 2 : 0));
		for (i32 k = 0; k < IMPORT_FOLDER_MAPS; ++k) {
			len += snprintf(s + len, cap - len, "\t%s", e->maps[k] != NULL ? e->maps[k] : "");
		}
		len += snprintf(s + len, cap - len, "\n");
	}
	iron_folder_write(library_index_path(), s, len);
	free(s);
}

static void library_queue_push(library_entry_t *e) {
	if (e->queued || e->done) {
		return;
	}
	if (library_queue_len == library_queue_cap) {
		library_queue_cap = library_queue_cap == 0 ? 256 : library_queue_cap * 2;
		library_queue     = realloc(library_queue, sizeof(library_entry_t *) * library_queue_cap);
	}
	library_queue[library_queue_len++] = e;
	e->queued                          = true;
}

static void library_count_done() {
	library_done = 0;
	for (i32 i = 0; i < library_count; ++i) {
		if (library_entries[i]->done) {
			library_done++;
		}
	}
}

static void library_load_index() {
	size_t size = 0;
	char  *data = (char *)iron_folder_read(library_index_path(), &size);
	if (data == NULL) {
		return;
	}
	char *text = malloc(size + 1);
	memcpy(text, data, size);
	text[size] = 0;
	free(data);
	char *line = text;
	while (*line != 0) {
		char *end = strchr(line, '\n');
		if (end != NULL) {
			*end = 0;
		}
		// Fields split on tabs, the first index version only had rel and name of the done folders
		char *f[4 + IMPORT_FOLDER_MAPS] = {0};
		i32   n                         = 0;
		char *c                         = line;
		while (n < 4 + IMPORT_FOLDER_MAPS) {
			f[n++]  = c;
			char *t = strchr(c, '\t');
			if (t == NULL) {
				break;
			}
			*t = 0;
			c  = t + 1;
		}
		if (n >= 2) {
			library_entry_t *e = calloc(1, sizeof(library_entry_t));
			e->rel             = strdup(f[0]);
			e->name            = strdup(f[1]);
			e->done            = true;
			if (n >= 4) {
				i32 flags      = atoi(f[3]);
				e->mtime       = atof(f[2]);
				e->done        = (flags & 1) != 0;
				e->has_subdirs = (flags & 2) != 0;
				for (i32 k = 0; k < IMPORT_FOLDER_MAPS && 4 + k < n; ++k) {
					e->maps[k] = f[4 + k][0] != 0 ? strdup(f[4 + k]) : NULL;
				}
			}
			library_entries                  = realloc(library_entries, sizeof(library_entry_t *) * (library_count + 1));
			library_entries[library_count++] = e;
		}
		if (end == NULL) {
			break;
		}
		line = end + 1;
	}
	free(text);
	qsort(library_entries, library_count, sizeof(library_entry_t *), library_compare_rel);
	// Folders left from the last launch are imported right away, the scan catches up with changes
	for (i32 i = 0; i < library_count; ++i) {
		library_entry_t *e = library_entries[i];
		for (i32 k = 0; k < IMPORT_FOLDER_MAPS; ++k) {
			if (e->maps[k] != NULL) {
				library_queue_push(e);
				break;
			}
		}
	}
	library_count_done();
}

// ███╗   ███╗ █████╗ ██╗███╗   ██╗

// Still used by a thread or waiting to be written
static bool library_entry_in_use(library_entry_t *e) {
	return (library_job != NULL && (library_job->entry == e || library_job->written == e)) || (library_pending != NULL && library_pending->written == e) ||
	       library_icon_entry == e;
}

// Takes the folders found by the scan so far: updates the known ones, adds the new ones and queues what needs importing
static void library_merge_scan() {
	iron_mutex_lock(&library_scan_mutex);
	library_entry_t **found = library_scan_out;
	i32               count = library_scan_count;
	library_scan_out        = NULL;
	library_scan_count      = 0;
	library_scan_cap        = 0;
	iron_mutex_unlock(&library_scan_mutex);
	if (count == 0) {
		return;
	}

	library_entry_t **added       = malloc(sizeof(library_entry_t *) * count);
	i32               added_count = 0;
	for (i32 i = 0; i < count; ++i) {
		library_entry_t *s = found[i];
		library_entry_t *e = library_find(library_entries, library_count, s->rel);
		if (e == NULL) {
			s->seen              = library_scan_gen;
			added[added_count++] = s;
			continue;
		}
		e->seen        = library_scan_gen;
		e->mtime       = s->mtime;
		e->has_subdirs = s->has_subdirs;
		if (!library_entry_in_use(e)) {
			for (i32 k = 0; k < IMPORT_FOLDER_MAPS; ++k) {
				free(e->maps[k]);
				e->maps[k] = s->maps[k];
				s->maps[k] = NULL;
			}
			e->done = s->done || e->imported;
		}
		if (!library_entry_in_use(e)) {
			library_queue_push(e);
		}
		library_entry_free(s);
	}
	free(found);

	if (added_count > 0) {
		library_entries = realloc(library_entries, sizeof(library_entry_t *) * (library_count + added_count));
		for (i32 i = 0; i < added_count; ++i) {
			library_entries[library_count++] = added[i];
			library_queue_push(added[i]);
		}
		qsort(library_entries, library_count, sizeof(library_entry_t *), library_compare_rel);
	}
	free(added);
	library_count_done();
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

// Scan finished: drops the folders that are gone and remembers the rest
static void library_finish_scan() {
	iron_thread_wait_and_destroy(&library_scan_thread);
	library_merge_scan();
	library_scan_state = 0;

	i32 kept = 0;
	for (i32 i = 0; i < library_count; ++i) {
		library_entry_t *e = library_entries[i];
		if (e->seen == library_scan_gen || library_entry_in_use(e)) {
			library_entries[kept++] = e;
			continue;
		}
		for (i32 q = library_queue_pos; q < library_queue_len; ++q) {
			if (library_queue[q] == e) {
				library_queue[q] = NULL;
			}
		}
		if (e->icon != NULL) {
			library_icons--;
		}
		library_entry_free(e);
	}
	library_count = kept;

	for (i32 i = 0; i < library_scan_cache_count; ++i) {
		library_entry_free(library_scan_cache[i]);
	}
	free(library_scan_cache);
	library_scan_cache       = NULL;
	library_scan_cache_count = 0;

	library_count_done();
	library_save_index();
	iron_log("[library] %d materials, %d done", library_count, library_done);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

// Imports one folder at a time while the user is not interacting
static bool library_busy_user() {
	return mouse_down("left") || ui_box_show || ui_menu_show || g_context->frame < 10;
}

static void library_update(void *_) {
	if (library_root == NULL) {
		return;
	}
	if (library_scan_state == 2) {
		library_finish_scan();
	}
	else if (library_scan_state == 1) {
		library_merge_scan();
	}
	library_update_icons();

	if (library_load_state == 2) {
		if (library_busy_user() || g_context->frame - library_last_frame < 15) {
			return; // Keep the decoded textures until the user pauses
		}
		iron_thread_wait_and_destroy(&library_load_thread);
		library_job_t *job = library_job;
		library_job        = NULL;
		library_load_state = 0;
		if (job->written != NULL) {
			if (job->write_ok) {
				job->written->done     = true;
				job->written->imported = true;
				library_done++;
				library_save_index();
			}
			else {
				library_failed++;
				iron_log("[library] could not save %s", job->arm_path);
			}
		}
		if (job->entry != NULL) {
			library_render(job);
			library_last_frame = g_context->frame;
		}
		library_job_free(job);
		ui_dock_redraw(TAB_AREA_SIDEBAR1);
	}

	if (library_load_state == 0) {
		library_start_load(); // Also while scanning, the folders found so far are queued
	}
}

void library_rescan() {
	if (library_root == NULL || library_scan_state != 0) {
		return;
	}
	static bool mutex_ready = false;
	if (!mutex_ready) {
		mutex_ready = true;
		iron_mutex_init(&library_scan_mutex);
	}
	free(library_scan_root);
	library_scan_root = strdup(library_root);
	library_scan_gen++;
	// Copies of the known folders, unchanged ones are not read again
	library_scan_cache       = malloc(sizeof(library_entry_t *) * (library_count > 0 ? library_count : 1));
	library_scan_cache_count = library_count;
	for (i32 i = 0; i < library_count; ++i) {
		library_scan_cache[i] = library_entry_copy(library_entries[i]);
	}
	library_scan_state = 1;
	iron_thread_init(&library_scan_thread, library_scan_run, NULL);
}

static void library_set_root(char *path) {
	library_root = path != NULL ? string_copy(path) : NULL;
	static bool update_added = false;
	if (!update_added) {
		update_added = true;
		sys_notify_on_update(library_update, NULL);
	}
}

static void library_folder_picked(char *path) {
	if (path == NULL) {
		return;
	}
	g_config->materials_folder = string_copy(path);
	config_save();
	library_set_root(path);
	library_failed = 0;
	// Maybe another folder: only what the scan finds in it gets imported
	for (i32 q = library_queue_pos; q < library_queue_len; ++q) {
		if (library_queue[q] != NULL) {
			library_queue[q]->queued = false;
		}
	}
	library_queue_pos = 0;
	library_queue_len = 0;
	library_rescan();
}

void library_pick_folder() {
	iron_folder_pick(&library_folder_picked);
}

void library_forget() {
	iron_folder_forget();
	g_config->materials_folder = "";
	library_root               = NULL;
	config_save();
}

// Restores access to the folder picked last time and refreshes it
void library_init() {
	if (string_equals(g_config->materials_folder, "")) {
		return;
	}
	char *restored = iron_folder_restore();
	library_set_root(restored != NULL ? restored : g_config->materials_folder);
	iron_log("[library] root %s", library_root);
	library_load_index();
	library_rescan();
}

// ██╗   ██╗██╗

bool library_has_folder() {
	return library_root != NULL;
}

char *library_status() {
	if (library_root == NULL) {
		return tr("No materials folder, choose one in Preferences > Library");
	}
	i32  total     = library_count;
	i32  importing = total - library_done - library_failed;
	bool scanning  = library_scan_state != 0;
	if (importing > 0) {
		char *progress = string_tmp("%s %d / %d", tr("Importing materials"), library_done, total);
		if (library_failed > 0) {
			progress = string_tmp("%s (%d %s)", progress, library_failed, tr("failed"));
		}
		return scanning ? string_tmp("%s, %s", progress, tr("looking for more...")) : progress;
	}
	if (scanning) {
		return string_tmp("%s (%d %s)", tr("Looking for new materials..."), library_done, tr("ready"));
	}
	if (library_failed > 0) {
		return string_tmp("%d %s, %d %s", library_done, tr("materials"), library_failed, tr("failed"));
	}
	return string_tmp("%d %s", library_done, tr("materials"));
}

i32 library_entry_count() {
	return library_count;
}

library_entry_t *library_entry_get(i32 i) {
	return library_entries[i];
}

bool library_entry_ready(library_entry_t *e) {
	return e->done;
}

// Remembered from the last launch but its preview is gone
bool library_entry_broken(library_entry_t *e) {
	return e->icon_state == 3;
}

char *library_entry_name(library_entry_t *e) {
	return e->name;
}

char *library_entry_rel(library_entry_t *e) {
	return e->rel;
}

// Adds the material to the project (loads its textures), or selects it when it is there already
void library_use(library_entry_t *e) {
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		if (string_equals(g_project->_->materials->buffer[i]->canvas->name, e->name)) {
			context_select_material(i);
			return;
		}
	}
	char *path = string("%s/%s.arm", library_dir(e), e->name);
	console_toast(tr("Loading material"));
	import_arm_run_material(path);
}
