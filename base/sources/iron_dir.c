// Directory listing

#include "iron_dir.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

directory open_dir(const char *dirname) {
	directory dir;
	dir.handle = opendir(dirname);
	return dir;
}

file read_next_file(directory *dir) {
	struct dirent *entry = readdir(dir->handle);

	while (entry != NULL && (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)) {
		entry = readdir(dir->handle);
	}

	file f;
	f.valid = entry != NULL;

	if (f.valid) {
		strcpy(f.name, entry->d_name);
	}

	return f;
}

void close_dir(directory *dir) {
	if (dir->handle != NULL) {
		closedir(dir->handle);
		dir->handle = NULL;
	}
}

