#include "../global.h"
#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <zstd.h>

// Results that are slow to make and the same each time, kept across sessions in Library/Caches/<sub>/<key>.bin (the
// system may purge it: a missing entry is made again). Blobs keyed by u64, zstd compressed; texels (rows of w texels of
// bpt bytes) through the predictor of the blob store (hstore_texels_split). Files are read and written on threads, a
// write goes to a temporary file moved over the entry. Past DISK_CACHE_BUDGET the least recently used go (a read
// touches its file).
//
// File: "MSDC" u32 version, u32 raw length, u32 w, u32 bpt (0 0: not texels), then the zstd frame.

#define DISK_CACHE_VERSION 1
#define DISK_CACHE_HEADER  20
#define DISK_CACHE_BUDGET  ((i64)1024 * 1024 * 1024)

void hstore_texels_split(const u8 *raw, u8 *out, u32 len, u32 w, u32 bpt); // history_store.c
void hstore_texels_join(const u8 *planes, u8 *raw, u32 len, u32 w, u32 bpt);

struct disk_cache_job {
	char       *path;
	u8         *data;
	u32         len;
	atomic_bool done;
};

// Paths are malloc'd with snprintf: threads build them too (string() is not for threads)
static char *disk_cache_printf(const char *fmt, const char *a, const char *b) {
	size_t n = strlen(a) + strlen(b) + 32;
	char  *p = malloc(n);
	snprintf(p, n, fmt, a, b);
	return p;
}

static char *disk_cache_dir(const char *sub) {
	char *dir = disk_cache_printf("%s%s", iron_internal_cache_path(), sub);
	mkdir(dir, 0755);
	return dir;
}

static char *disk_cache_path(const char *sub, u64 key) {
	char *dir = disk_cache_dir(sub);
	char  name[24];
	snprintf(name, sizeof(name), "%016llx", (unsigned long long)key);
	char *path = disk_cache_printf("%s/%s.bin", dir, name);
	free(dir);
	return path;
}

static void *disk_cache_read_thread(void *param) {
	disk_cache_job_t *j = param;
	FILE             *f = fopen(j->path, "rb");
	if (f != NULL) {
		u8 head[DISK_CACHE_HEADER];
		u32 v[4];
		if (fread(head, 1, DISK_CACHE_HEADER, f) == DISK_CACHE_HEADER && memcmp(head, "MSDC", 4) == 0) {
			memcpy(v, head + 4, 16);
			fseek(f, 0, SEEK_END);
			long   size = ftell(f) - DISK_CACHE_HEADER;
			u8    *comp = size > 0 ? malloc(size) : NULL;
			fseek(f, DISK_CACHE_HEADER, SEEK_SET);
			u32    raw_len = v[1], w = v[2], bpt = v[3];
			if (v[0] == DISK_CACHE_VERSION && comp != NULL && fread(comp, 1, size, f) == (size_t)size) {
				u8    *out = malloc(raw_len > 0 ? raw_len : 1);
				size_t n   = ZSTD_decompress(out, raw_len, comp, size);
				if (!ZSTD_isError(n) && n == raw_len) {
					if (w > 0 && bpt > 0 && raw_len % (w * bpt) == 0) {
						u8 *texels = malloc(raw_len > 0 ? raw_len : 1);
						hstore_texels_join(out, texels, raw_len, w, bpt);
						free(out);
						out = texels;
					}
					j->data = out;
					j->len  = raw_len;
				}
				else {
					free(out);
				}
			}
			free(comp);
		}
		fclose(f);
		if (j->data != NULL) {
			utimes(j->path, NULL); // Recently used
		}
		else {
			unlink(j->path); // Unreadable: made again
		}
	}
	atomic_store(&j->done, true);
	return NULL;
}

// The entry of key read on a thread, NULL when there is none
disk_cache_job_t *disk_cache_read(const char *sub, u64 key) {
	char       *path = disk_cache_path(sub, key);
	struct stat st;
	if (stat(path, &st) != 0) {
		free(path);
		return NULL;
	}
	disk_cache_job_t *j = calloc(1, sizeof(disk_cache_job_t));
	j->path             = path;
	atomic_init(&j->done, false);
	pthread_t t;
	if (pthread_create(&t, NULL, disk_cache_read_thread, j) != 0) {
		free(j->path);
		free(j);
		return NULL;
	}
	pthread_detach(t);
	return j;
}

bool disk_cache_job_done(disk_cache_job_t *j) {
	return atomic_load(&j->done);
}

// A finished read's bytes (malloc'd, the caller's), NULL when it failed. The job is freed.
u8 *disk_cache_job_take(disk_cache_job_t *j, u32 *len) {
	u8 *data = j->data;
	*len     = j->len;
	free(j->path);
	free(j);
	return data;
}

// A job not waited for any more: freed once it ends
static void *disk_cache_drop_thread(void *param) {
	disk_cache_job_t *j = param;
	while (!atomic_load(&j->done)) {
		usleep(1000);
	}
	free(j->data);
	free(j->path);
	free(j);
	return NULL;
}

void disk_cache_job_drop(disk_cache_job_t *j) {
	if (j == NULL) {
		return;
	}
	pthread_t t;
	if (pthread_create(&t, NULL, disk_cache_drop_thread, j) == 0) {
		pthread_detach(t);
	}
}

typedef struct {
	char *dir;
	char *path;
	u8   *data;
	u32   len;
	u32   w;
	u32   bpt;
} disk_cache_write_t;

typedef struct {
	char  name[64];
	i64   size;
	f64   mtime;
} disk_cache_file_t;

static int disk_cache_cmp_mtime(const void *a, const void *b) {
	f64 x = ((const disk_cache_file_t *)a)->mtime, y = ((const disk_cache_file_t *)b)->mtime;
	return x < y ? -1 : x > y ? 1 : 0;
}

// The least recently used entries of dir go while it is past the budget
static void disk_cache_trim(const char *dir) {
	DIR *d = opendir(dir);
	if (d == NULL) {
		return;
	}
	disk_cache_file_t *files = NULL;
	i32                n = 0, cap = 0;
	i64                total = 0;
	struct dirent     *e;
	while ((e = readdir(d)) != NULL) {
		size_t l = strlen(e->d_name);
		if (l < 5 || l >= 64 || strcmp(e->d_name + l - 4, ".bin") != 0) {
			continue;
		}
		char       *p = disk_cache_printf("%s/%s", dir, e->d_name);
		struct stat st;
		if (stat(p, &st) == 0) {
			if (n == cap) {
				cap   = cap == 0 ? 64 : cap * 2;
				files = realloc(files, sizeof(disk_cache_file_t) * cap);
			}
			snprintf(files[n].name, sizeof(files[n].name), "%s", e->d_name);
			files[n].size  = st.st_size;
			files[n].mtime = st.st_mtimespec.tv_sec + st.st_mtimespec.tv_nsec * 1e-9;
			total += st.st_size;
			n++;
		}
		free(p);
	}
	closedir(d);
	if (total > DISK_CACHE_BUDGET) {
		qsort(files, n, sizeof(disk_cache_file_t), disk_cache_cmp_mtime);
		for (i32 i = 0; i < n && total > DISK_CACHE_BUDGET; ++i) {
			char *p = disk_cache_printf("%s/%s", dir, files[i].name);
			unlink(p);
			free(p);
			total -= files[i].size;
		}
	}
	free(files);
}

static void *disk_cache_write_thread(void *param) {
	disk_cache_write_t *w    = param;
	u8                 *src  = w->data;
	u8                 *planes = NULL;
	if (w->w > 0) {
		planes = malloc(w->len > 0 ? w->len : 1);
		hstore_texels_split(w->data, planes, w->len, w->w, w->bpt);
		src = planes;
	}
	size_t bound = ZSTD_compressBound(w->len);
	u8    *out   = malloc(DISK_CACHE_HEADER + bound);
	size_t n     = ZSTD_compress(out + DISK_CACHE_HEADER, bound, src, w->len, 1);
	if (!ZSTD_isError(n)) {
		u32 v[4] = {DISK_CACHE_VERSION, w->len, w->w, w->bpt};
		memcpy(out, "MSDC", 4);
		memcpy(out + 4, v, 16);
		char *tmp = disk_cache_printf("%s%s", w->path, ".tmp");
		FILE *f   = fopen(tmp, "wb");
		bool  ok  = f != NULL && fwrite(out, 1, DISK_CACHE_HEADER + n, f) == DISK_CACHE_HEADER + n;
		if (f != NULL) {
			ok = fclose(f) == 0 && ok;
		}
		if (!ok || rename(tmp, w->path) != 0) {
			unlink(tmp);
		}
		free(tmp);
		disk_cache_trim(w->dir);
	}
	free(out);
	free(planes);
	free(w->data);
	free(w->path);
	free(w->dir);
	free(w);
	return NULL;
}

// data (taken) kept as the entry of key, written on a thread. Texels: rows of w texels of bpt bytes (w 0: other bytes).
void disk_cache_write(const char *sub, u64 key, u8 *data, u32 len, u32 w, u32 bpt) {
	disk_cache_write_t *job = calloc(1, sizeof(disk_cache_write_t));
	job->dir                = disk_cache_dir(sub);
	job->path               = disk_cache_path(sub, key);
	job->data               = data;
	job->len                = len;
	job->w                  = w > 0 && bpt > 0 && len % (w * bpt) == 0 ? w : 0;
	job->bpt                = job->w > 0 ? bpt : 0;
	pthread_t t;
	if (pthread_create(&t, NULL, disk_cache_write_thread, job) == 0) {
		pthread_detach(t);
	}
	else {
		free(job->data);
		free(job->path);
		free(job->dir);
		free(job);
	}
}

// Whether key has an entry (tests)
bool disk_cache_has(const char *sub, u64 key) {
	char       *path = disk_cache_path(sub, key);
	struct stat st;
	bool        has = stat(path, &st) == 0;
	free(path);
	return has;
}
