#include "../global.h"
#include <unistd.h>

// Pixel comparisons and hashes for test scripts (paint/tests), in C: per-pixel loops in minic are too slow at layer sizes.
// Diffs count the channel values that differ by more than tol, in 1/255 units whatever the format; script_diff_max and
// script_diff_mean read the last diff's largest and mean difference (same units). script_diff_mask limits diffs to the
// texels where a mask is set (or not set).

static f32 util_test_diff_max  = 0.0;
static f32 util_test_diff_mean = 0.0;
static u8 *util_test_mask      = NULL; // 1 where a texel is compared
static i32 util_test_mask_w    = 0;
static i32 util_test_mask_h    = 0;

static i32 util_test_channels(gpu_texture_format_t f) {
	return f == GPU_TEXTURE_FORMAT_R8 || f == GPU_TEXTURE_FORMAT_R16 || f == GPU_TEXTURE_FORMAT_R32 ? 1 : 4;
}

static f32 util_test_value(u8 *p, gpu_texture_format_t f, i32 i) {
	if (f == GPU_TEXTURE_FORMAT_RGBA64 || f == GPU_TEXTURE_FORMAT_R16) {
		return half_to_float(((u16 *)p)[i]);
	}
	if (f == GPU_TEXTURE_FORMAT_RGBA128 || f == GPU_TEXTURE_FORMAT_R32) {
		return ((f32 *)p)[i];
	}
	return p[i] / 255.0;
}

static i32 util_test_diff(u8 *a, u8 *b, i32 w, i32 h, gpu_texture_format_t f, f32 tol) {
	i32 ch = util_test_channels(f);
	if (util_test_mask != NULL && (util_test_mask_w != w || util_test_mask_h != h)) {
		iron_log("script_diff: mask is %dx%d, images %dx%d\n", util_test_mask_w, util_test_mask_h, w, h);
		return -1;
	}
	bool bytes = f != GPU_TEXTURE_FORMAT_RGBA64 && f != GPU_TEXTURE_FORMAT_R16 && f != GPU_TEXTURE_FORMAT_RGBA128 && f != GPU_TEXTURE_FORMAT_R32;
	i32  over  = 0;
	f64  sum   = 0.0;
	i64  count = 0;
	f32  max   = 0.0;
	for (i32 t = 0; t < w * h; ++t) {
		if (util_test_mask != NULL && !util_test_mask[t]) {
			continue;
		}
		for (i32 c = 0; c < ch; ++c) {
			i32 i = t * ch + c; // Bytes: their difference exactly (in floats a step of one came out 1.000001 at some values)
			f32 d = bytes ? (f32)abs((i32)a[i] - (i32)b[i]) : fabsf(util_test_value(a, f, i) - util_test_value(b, f, i)) * 255.0;
			if (d != d) { // NaN on one side only
				d = 255.0;
			}
			if (d > tol) {
				over++;
			}
			max = d > max ? d : max;
			sum += d;
			count++;
		}
	}
	util_test_diff_max  = max;
	util_test_diff_mean = count > 0 ? (f32)(sum / count) : 0.0;
	return over;
}

static u8 *util_test_load(char *path, i32 *w, i32 *h) {
	buffer_t *blob = iron_load_blob(path);
	if (blob == NULL) {
		return NULL;
	}
	i32 comp;
	u8 *p = stbi_load_from_memory(blob->buffer, blob->length, w, h, &comp, 4);
	iron_delete_blob(blob);
	return p;
}

// Channel values of two image files (png, jpg) more than tol apart, -1 when one does not load or their sizes differ
i32 script_diff_images(char *path_a, char *path_b, f32 tol) {
	i32 wa, ha, wb, hb;
	u8 *a = util_test_load(path_a, &wa, &ha);
	u8 *b = util_test_load(path_b, &wb, &hb);
	i32 r = -1;
	if (a == NULL || b == NULL) {
		iron_log("script_diff_images: can not load %s\n", a == NULL ? path_a : path_b);
	}
	else if (wa != wb || ha != hb) {
		iron_log("script_diff_images: %dx%d and %dx%d\n", wa, ha, wb, hb);
	}
	else {
		r = util_test_diff(a, b, wa, ha, GPU_TEXTURE_FORMAT_RGBA32, tol);
	}
	free(a);
	free(b);
	return r;
}

static u8 *util_test_read(gpu_texture_t *t) {
	draw_saved_t gpu = draw_suspend();
	buffer_t    *b   = gpu_get_texture_pixels(t);
	draw_resume(gpu);
	u8 *copy = malloc(b->length);
	memcpy(copy, b->buffer, b->length);
	gpu_release_texture_pixels(t);
	return copy;
}

// Channel values of two textures (read back) more than tol apart, -1 when their sizes or formats differ
i32 script_diff_textures(gpu_texture_t *a, gpu_texture_t *b, f32 tol) {
	if (a == NULL || b == NULL || a->width != b->width || a->height != b->height || a->format != b->format) {
		iron_log("script_diff_textures: textures differ in size or format\n");
		return -1;
	}
	u8 *pa = util_test_read(a);
	u8 *pb = util_test_read(b);
	i32 r  = util_test_diff(pa, pb, a->width, a->height, a->format, tol);
	free(pa);
	free(pb);
	return r;
}

f32 script_diff_max() {
	return util_test_diff_max;
}

f32 script_diff_mean() {
	return util_test_diff_mean;
}

// Diffs compare only the texels where the first channel of mask is set (mode 1) or not set (mode 2), mode 0: all
void script_diff_mask(gpu_texture_t *mask, i32 mode) {
	free(util_test_mask);
	util_test_mask = NULL;
	if (mask == NULL || mode == 0) {
		return;
	}
	u8 *p             = util_test_read(mask);
	i32 ch            = util_test_channels(mask->format);
	util_test_mask_w  = mask->width;
	util_test_mask_h  = mask->height;
	util_test_mask    = malloc((size_t)mask->width * mask->height);
	for (i32 t = 0; t < mask->width * mask->height; ++t) {
		bool set          = util_test_value(p, mask->format, t * ch) > 0.0;
		util_test_mask[t] = mode == 1 ? set : !set;
	}
	free(p);
}

// 16 hex digits of the texture's pixels (read back), equal pixels hash equal
char *script_texture_hash(gpu_texture_t *t) {
	if (t == NULL) {
		return "";
	}
	u8 *p = util_test_read(t);
	u64 h = hash_fnv1a(HASH_FNV1A_BASIS, p, (size_t)t->width * t->height * gpu_texture_format_size(t->format));
	free(p);
	return string("%016llx", (unsigned long long)h);
}

// Bytes that differ between two files, -1 when one does not load or their sizes differ
i32 script_diff_files(char *path_a, char *path_b) {
	buffer_t *a = iron_load_blob(path_a);
	buffer_t *b = iron_load_blob(path_b);
	i32       r = -1;
	if (a != NULL && b != NULL && a->length == b->length) {
		r = 0;
		for (u32 i = 0; i < a->length; ++i) {
			r += a->buffer[i] != b->buffer[i];
		}
	}
	iron_delete_blob(a);
	iron_delete_blob(b);
	return r;
}

// A temporary directory of this run, /tmp/<name>_<pid>: suites of several worktrees run at the same time and must not
// share their files
char *script_tmp_dir(char *name) {
#ifdef IRON_IOS
	return string("%s%s_%d", iron_internal_save_path(), name, (i32)getpid()); // /tmp is outside the sandbox
#else
	return string("/tmp/%s_%d", name, (i32)getpid());
#endif
}

// Texels for comparisons across formats: script_texture_read or script_image_read takes a snapshot (RGBA floats, RGBA
// order whatever the texture's), script_texel reads it
static f32 *util_test_snap   = NULL;
static i32  util_test_snap_w = 0;
static i32  util_test_snap_h = 0;

static void util_test_snap_alloc(i32 w, i32 h) {
	free(util_test_snap);
	util_test_snap   = calloc((size_t)w * h * 4, sizeof(f32));
	util_test_snap_w = w;
	util_test_snap_h = h;
}

void script_texture_read(gpu_texture_t *t) {
	u8 *p  = util_test_read(t);
	i32 ch = util_test_channels(t->format);
	util_test_snap_alloc(t->width, t->height);
	for (i32 i = 0; i < t->width * t->height; ++i) {
		for (i32 c = 0; c < 4; ++c) {
			i32 k = c;
#ifdef IRON_BGRA
			if (t->format == GPU_TEXTURE_FORMAT_RGBA32) {
				k = c == 0 ? 2 : c == 2 ? 0 : c; // Render targets hold BGRA
			}
#endif
			util_test_snap[i * 4 + c] = ch == 4 ? util_test_value(p, t->format, i * 4 + k) : c == 3 ? 1.0 : util_test_value(p, t->format, i);
		}
	}
	free(p);
}

// png, jpg, or an exr as export_exr.c writes them (uncompressed scanlines, B G R of half or float). False when it does not load.
bool script_image_read(char *path) {
	buffer_t *blob = iron_load_blob(path);
	if (blob == NULL) {
		return false;
	}
	bool ok = false;
	if (ends_with(path, ".exr")) {
		u8 *b   = blob->buffer;
		i32 len = blob->length;
		i32 w = 0, h = 0, type = 1, end = -1;
		for (i32 i = 0; i + 20 < len && end < 0; ++i) {
			if (memcmp(b + i, "dataWindow\0box2i\0", 17) == 0) {
				i32 *box = (i32 *)(b + i + 17 + 4);
				w        = box[2] - box[0] + 1;
				h        = box[3] - box[1] + 1;
			}
			if (memcmp(b + i, "chlist\0", 7) == 0) {
				type = *(i32 *)(b + i + 7 + 4 + 2); // Size, then "B\0", then the pixel type
			}
			if (memcmp(b + i, "screenWindowWidth\0float\0", 24) == 0) {
				end = i + 24 + 4 + 4 + 1; // Size, 1.0, end of header
			}
		}
		i32 bs = type == 1 ? 2 : 4;
		if (w > 0 && h > 0 && end > 0 && end + 8 * h + (i64)h * (8 + w * 3 * bs) <= len) {
			util_test_snap_alloc(w, h);
			u8 *rows = b + end + 8 * h;
			for (i32 y = 0; y < h; ++y) {
				u8 *row = rows + (size_t)y * (8 + w * 3 * bs) + 8;
				for (i32 x = 0; x < w; ++x) {
					for (i32 c = 0; c < 3; ++c) { // B G R planes
						u8 *v                                       = row + ((size_t)c * w + x) * bs;
						f32 f                                       = bs == 2 ? half_to_float(*(u16 *)v) : *(f32 *)v;
						util_test_snap[((size_t)y * w + x) * 4 + 2 - c] = f;
					}
					util_test_snap[((size_t)y * w + x) * 4 + 3] = 1.0;
				}
			}
			ok = true;
		}
	}
	else {
		i32 w, h, comp;
		u8 *p = stbi_load_from_memory(blob->buffer, blob->length, &w, &h, &comp, 4);
		if (p != NULL) {
			util_test_snap_alloc(w, h);
			for (i32 i = 0; i < w * h * 4; ++i) {
				util_test_snap[i] = p[i] / 255.0;
			}
			free(p);
			ok = true;
		}
	}
	iron_delete_blob(blob);
	return ok;
}

// Channel c (0 r .. 3 a) of the snapshot at u, v (0 to 1, any size)
f32 script_texel(f32 u, f32 v, i32 c) {
	if (util_test_snap == NULL) {
		return -1.0;
	}
	i32 x = (i32)(u * util_test_snap_w);
	i32 y = (i32)(v * util_test_snap_h);
	x     = x < 0 ? 0 : x >= util_test_snap_w ? util_test_snap_w - 1 : x;
	y     = y < 0 ? 0 : y >= util_test_snap_h ? util_test_snap_h - 1 : y;
	return util_test_snap[((size_t)y * util_test_snap_w + x) * 4 + (c & 3)];
}
