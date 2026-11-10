#include "iron_folder.h"
#include "iron_global.h"

#ifndef IRON_IOS // iOS: backends/ios_folder.m

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#ifdef WITH_NFD
#include <nfd.h>
#endif

static char *iron_folder_join(const char *a, const char *b) {
	size_t la = strlen(a);
	size_t lb = strlen(b);
	char  *s  = malloc(la + lb + 2);
	memcpy(s, a, la);
	s[la] = '/';
	memcpy(s + la + 1, b, lb + 1);
	return s;
}

double iron_folder_mtime(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) {
		return 0;
	}
	return (double)st.st_mtime;
}

iron_dir_entry_t *iron_folder_list(const char *path, int *count) {
	*count = 0;
#ifdef _WIN32
	char pattern[MAX_PATH];
	snprintf(pattern, sizeof(pattern), "%s\\*", path);
	WIN32_FIND_DATAA fd;
	HANDLE           h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) {
		return NULL;
	}
	int               cap     = 64;
	iron_dir_entry_t *entries = malloc(sizeof(iron_dir_entry_t) * cap);
	do {
		if (fd.cFileName[0] == '.') {
			continue;
		}
		if (*count == cap) {
			cap *= 2;
			entries = realloc(entries, sizeof(iron_dir_entry_t) * cap);
		}
		char *full                 = iron_folder_join(path, fd.cFileName);
		entries[*count].name   = strdup(fd.cFileName);
		entries[*count].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		entries[*count].mtime  = iron_folder_mtime(full);
		free(full);
		(*count)++;
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return entries;
#else
	DIR *dir = opendir(path);
	if (dir == NULL) {
		return NULL;
	}
	int               cap     = 64;
	iron_dir_entry_t *entries = malloc(sizeof(iron_dir_entry_t) * cap);
	struct dirent    *e;
	while ((e = readdir(dir)) != NULL) {
		if (e->d_name[0] == '.') { // Hidden files, "." and ".."
			continue;
		}
		if (*count == cap) {
			cap *= 2;
			entries = realloc(entries, sizeof(iron_dir_entry_t) * cap);
		}
		char       *full = iron_folder_join(path, e->d_name);
		struct stat st;
		bool        ok         = stat(full, &st) == 0;
		entries[*count].name   = strdup(e->d_name);
		entries[*count].is_dir = ok && S_ISDIR(st.st_mode);
		entries[*count].mtime  = ok ? (double)st.st_mtime : 0;
		free(full);
		(*count)++;
	}
	closedir(dir);
	return entries;
#endif
}

uint8_t *iron_folder_read(const char *path, size_t *size) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *data = malloc(len > 0 ? len : 1);
	size_t   n    = fread(data, 1, len, f);
	fclose(f);
	if ((long)n != len) {
		free(data);
		return NULL;
	}
	*size = n;
	return data;
}

bool iron_folder_write(const char *path, const void *data, size_t size) {
	FILE *f = fopen(path, "wb");
	if (f == NULL) {
		return false;
	}
	bool ok = fwrite(data, 1, size, f) == size;
	return fclose(f) == 0 && ok;
}

void iron_folder_pick(void (*done)(char *path)) {
#ifdef WITH_NFD
	char *out = NULL;
	if (NFD_PickFolder(NULL, &out) == NFD_OKAY) {
		done(out);
		return;
	}
#endif
	done(NULL);
}

char *iron_folder_restore(void) {
	return NULL; // Plain paths stay accessible on desktop
}

void iron_folder_forget(void) {}

#endif
