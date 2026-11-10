#pragma once

// File access for user folders that can live on network shares (material library).
// Everything here is safe to call from worker threads. On iOS reads and writes are
// coordinated with the file provider, which network (SMB) shares need.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct iron_dir_entry {
	char  *name;
	bool   is_dir;
	double mtime; // Seconds since 1970
} iron_dir_entry_t;

iron_dir_entry_t *iron_folder_list(const char *path, int *count); // NULL on error, free with iron_folder_list_free
void              iron_folder_list_free(iron_dir_entry_t *entries, int count);
double            iron_folder_mtime(const char *path);              // 0 when missing
uint8_t          *iron_folder_read(const char *path, size_t *size); // malloc'd, NULL on error
bool              iron_folder_write(const char *path, const void *data, size_t size);

// A folder picked by the user that stays accessible across launches (iOS keeps a security scoped bookmark)
void  iron_folder_pick(void (*done)(char *path)); // Called on the main thread, path is NULL when cancelled
char *iron_folder_restore(void);                  // Path of the folder picked last time, NULL if none (iOS only)
void  iron_folder_forget(void);
