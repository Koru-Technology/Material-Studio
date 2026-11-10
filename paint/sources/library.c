// Material library: a user picked folder of texture folders (any nesting), each folder becomes a material.
// For every folder a <Name>.arm material file and a <Name>_icon.png preview are saved next to its textures.
// The Library tab only holds the previews, textures are loaded when a material is picked.
//
// Work is spread so the app stays usable:
// - Scan thread: walks the folder tree, finds material folders and which ones are already done.
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
static library_entry_t **library_scan_out   = NULL;
static i32               library_scan_count = 0;
static i32               library_scan_cap   = 0;
static char             *library_scan_root  = NULL;

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
static library_entry_t **library_queue      = NULL; // Folders left to import, main thread
static i32               library_queue_len  = 0;
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
	if (library_scan_count == library_scan_cap) {
		library_scan_cap = library_scan_cap == 0 ? 256 : library_scan_cap * 2;
		library_scan_out = realloc(library_scan_out, sizeof(library_entry_t *) * library_scan_cap);
	}
	library_scan_out[library_scan_count++] = e;
}

static void library_scan_dir(const char *dir, const char *rel, const char *name, i32 depth) {
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
		e->rel  = strdup(rel);
		e->name = strdup(name);
		e->done = has_icon && arm_mtime >= newest;
		library_scan_push(e);
	}
	else {
		free(e);
	}

	if (depth < LIBRARY_MAX_DEPTH) {
		for (i32 i = 0; i < count; ++i) {
			if (!entries[i].is_dir) {
				continue;
			}
			char *sub     = library_join(dir, entries[i].name);
			char *sub_rel = rel[0] == 0 ? strdup(entries[i].name) : library_join(rel, entries[i].name);
			library_scan_dir(sub, sub_rel, entries[i].name, depth + 1);
			free(sub);
			free(sub_rel);
		}
	}
	iron_folder_list_free(entries, count);
}

static void library_scan_run(void *_) {
	library_scan_dir(library_scan_root, "", "", 0);
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

static void library_save_index() {
	size_t cap = 1024;
	size_t len = 0;
	char  *s   = malloc(cap);
	for (i32 i = 0; i < library_count; ++i) {
		library_entry_t *e = library_entries[i];
		if (!e->done) {
			continue;
		}
		size_t need = strlen(e->rel) + strlen(e->name) + 3;
		while (len + need + 1 > cap) {
			cap *= 2;
			s = realloc(s, cap);
		}
		len += snprintf(s + len, cap - len, "%s\t%s\n", e->rel, e->name);
	}
	iron_folder_write(library_index_path(), s, len);
	free(s);
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
		char *tab = strchr(line, '\t');
		if (tab != NULL) {
			*tab                 = 0;
			library_entry_t *e   = calloc(1, sizeof(library_entry_t));
			e->rel               = strdup(line);
			e->name              = strdup(tab + 1);
			e->done              = true;
			library_entries      = realloc(library_entries, sizeof(library_entry_t *) * (library_count + 1));
			library_entries[library_count++] = e;
			library_done++;
		}
		if (end == NULL) {
			break;
		}
		line = end + 1;
	}
	free(text);
}

// ███╗   ███╗ █████╗ ██╗███╗   ██╗

static i32 library_compare(const void *a, const void *b) {
	return strcmp((*(library_entry_t **)a)->rel, (*(library_entry_t **)b)->rel);
}

// Takes over the finished scan, keeping the loaded previews
static void library_adopt_scan() {
	iron_thread_wait_and_destroy(&library_scan_thread);
	library_entry_t **entries = library_scan_out;
	i32               count   = library_scan_count;
	library_scan_out          = NULL;
	library_scan_count        = 0;
	library_scan_cap          = 0;
	library_scan_state        = 0;

	qsort(entries, count, sizeof(library_entry_t *), library_compare);
	for (i32 i = 0; i < count; ++i) {
		for (i32 j = 0; j < library_count; ++j) {
			library_entry_t *old = library_entries[j];
			if (old != NULL && old->icon != NULL && strcmp(old->rel, entries[i]->rel) == 0) {
				entries[i]->icon       = old->icon;
				entries[i]->icon_state = 2;
				old->icon              = NULL;
				break;
			}
		}
	}
	// The load thread may still hold an entry of the old list
	if (library_load_state != 0 || library_pending != NULL) {
		if (library_load_state != 0) {
			iron_thread_wait_and_destroy(&library_load_thread);
			if (library_job->written != NULL && library_job->write_ok) {
				library_job->written->done = true;
			}
			library_job_free(library_job);
			library_job        = NULL;
			library_load_state = 0;
		}
		if (library_pending != NULL) {
			library_job_free(library_pending);
			library_pending = NULL;
		}
	}
	if (library_icon_state != 0) {
		iron_thread_wait_and_destroy(&library_icon_thread);
		free(library_icon_pixels.data);
		library_icon_pixels.data = NULL;
		library_icon_state       = 0;
		library_icon_entry       = NULL;
	}
	library_icons = 0;
	for (i32 j = 0; j < library_count; ++j) {
		library_entry_free(library_entries[j]);
	}
	free(library_entries);
	library_entries = entries;
	library_count   = count;
	library_done    = 0;

	free(library_queue);
	library_queue     = malloc(sizeof(library_entry_t *) * (count > 0 ? count : 1));
	library_queue_len = 0;
	library_queue_pos = 0;
	for (i32 i = 0; i < count; ++i) {
		if (entries[i]->done) {
			library_done++;
		}
		else {
			library_queue[library_queue_len++] = entries[i];
		}
		if (entries[i]->icon != NULL) {
			library_icons++;
		}
	}
	library_save_index();
	iron_log("[library] %d materials, %d to import", count, library_queue_len);
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
		library_adopt_scan();
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
				job->written->done = true;
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

	if (library_load_state == 0 && library_scan_state == 0) {
		library_start_load();
	}
}

void library_rescan() {
	if (library_root == NULL || library_scan_state != 0) {
		return;
	}
	free(library_scan_root);
	library_scan_root  = strdup(library_root);
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
	if (library_scan_state != 0) {
		return string_tmp("%s (%d %s)", tr("Looking for new materials..."), library_done, tr("ready"));
	}
	i32 total = library_done + (library_queue_len - library_queue_pos) + (library_job != NULL && library_job->entry != NULL ? 1 : 0);
	if (library_done < total) {
		if (library_failed > 0) {
			return string_tmp("%s %d / %d (%d %s)", tr("Importing materials"), library_done, total + library_failed, library_failed, tr("failed"));
		}
		return string_tmp("%s %d / %d", tr("Importing materials"), library_done, total);
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
