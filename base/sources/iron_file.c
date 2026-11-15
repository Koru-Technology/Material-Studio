
#include "iron_file.h"
#include "iron_alloc.h"
#include "iron_json.h"
#include "iron_map.h"
#include "iron_net.h"
#include "iron_path.h"
#include "iron_string.h"
#include "iron_system.h"
#include "iron_dir.h"
#ifdef IRON_IOS
#include "backends/ios_file_dialog.h"
#endif
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef IRON_MACOS
#include <copyfile.h>
#include <spawn.h>
#include <sys/wait.h>
#endif


static char *fileslocation = NULL;
const char  *iron_get_resource_path();

void iron_internal_set_files_location(char *dir) {
	fileslocation = dir;
}

char *iron_internal_files_location(void) {
	static char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s/", iron_get_resource_path(), IRON_OUTDIR);
	return path;
}

static size_t iron_libc_file_reader_read(iron_file_reader_t *reader, void *data, size_t size) {
	return fread(data, 1, size, (FILE *)reader->data);
}

static bool iron_libc_file_reader_seek(iron_file_reader_t *reader, size_t pos) {
	fseek((FILE *)reader->data, pos, SEEK_SET);
	return true;
}

static bool iron_libc_file_reader_close(iron_file_reader_t *reader) {
	if (reader->data != NULL) {
		fclose((FILE *)reader->data);
		reader->data = NULL;
	}
	return true;
}

static size_t iron_libc_file_reader_pos(iron_file_reader_t *reader) {
	return ftell((FILE *)reader->data);
}

bool iron_internal_file_reader_open(iron_file_reader_t *reader, const char *filename, int type) {
	// Absolute, else relative to the files location, the save folder or the bundled data
	char filepath[PATH_MAX];
	int  length;
	if (filename[0] == '/') {
		length = snprintf(filepath, sizeof(filepath), "%s", filename);
	}
	else if (fileslocation != NULL && type != IRON_FILE_TYPE_SAVE) {
		length = snprintf(filepath, sizeof(filepath), "%s/%s", fileslocation, filename);
	}
	else if (type == IRON_FILE_TYPE_SAVE) {
		length = snprintf(filepath, sizeof(filepath), "%s%s", iron_internal_save_path(), filename);
	}
	else {
		length = snprintf(filepath, sizeof(filepath), "%s/%s/%s", iron_get_resource_path(), IRON_OUTDIR, filename);
	}
	if (length < 0 || length >= (int)sizeof(filepath)) {
		return false;
	}

	reader->data = fopen(filepath, "rb");
	if (reader->data == NULL) {
		return false;
	}

	fseek((FILE *)reader->data, 0, SEEK_END);
	long size    = ftell((FILE *)reader->data);
	reader->size = size > 0 ? (size_t)size : 0;
	fseek((FILE *)reader->data, 0, SEEK_SET);

	reader->read  = iron_libc_file_reader_read;
	reader->seek  = iron_libc_file_reader_seek;
	reader->close = iron_libc_file_reader_close;
	reader->pos   = iron_libc_file_reader_pos;

	return true;
}

bool iron_file_reader_open(iron_file_reader_t *reader, const char *filename, int type) {
	memset(reader, 0, sizeof(*reader));
	return iron_internal_file_reader_open(reader, filename, type);
}

size_t iron_file_reader_read(iron_file_reader_t *reader, void *data, size_t size) {
	return reader->read(reader, data, size);
}

bool iron_file_reader_seek(iron_file_reader_t *reader, size_t pos) {
	return reader->seek(reader, pos);
}

bool iron_file_reader_close(iron_file_reader_t *reader) {
	return reader->close(reader);
}

size_t iron_file_reader_pos(iron_file_reader_t *reader) {
	return reader->pos(reader);
}

size_t iron_file_reader_size(iron_file_reader_t *reader) {
	return reader->size;
}

int32_t iron_read_s32le(uint8_t *data) {
	return *(int32_t *)data;
}

bool iron_file_writer_open(iron_file_writer_t *writer, const char *filepath) {
	writer->file = NULL;

	char path[PATH_MAX];
	int  length = snprintf(path, sizeof(path), "%s%s", iron_internal_save_path(), filepath);
	if (length > 0 && length < (int)sizeof(path)) {
		writer->file = fopen(path, "wb");
	}
	if (writer->file == NULL) {
		iron_log("Could not open file %s.", filepath);
		return false;
	}
	return true;
}

void iron_file_writer_close(iron_file_writer_t *writer) {
	if (writer->file != NULL) {
		fclose((FILE *)writer->file);
		writer->file = NULL;
	}
}

void iron_file_writer_write(iron_file_writer_t *writer, void *data, int size) {
	fwrite(data, 1, size, (FILE *)writer->file);
}

// Names separated by \n, valid until the next call
char *iron_read_directory(char *path) {
	static char  *files    = NULL;
	static size_t capacity = 0;
	size_t        length   = 0;
	if (files == NULL) {
		capacity = 4096;
		files    = malloc(capacity);
	}
	files[0] = '\0';

	directory dir = open_dir(path);
	if (dir.handle == NULL) {
		return files;
	}

	while (true) {
		file f = read_next_file(&dir);
		if (!f.valid) {
			break;
		}
		size_t name_length = strlen(f.name);
		if (length + name_length + 2 > capacity) {
			while (length + name_length + 2 > capacity) {
				capacity *= 2;
			}
			files = realloc(files, capacity);
		}
		if (length > 0) {
			files[length++] = '\n';
		}
		memcpy(files + length, f.name, name_length + 1);
		length += name_length;
	}
	close_dir(&dir);
	return files;
}

// With the missing parent folders, as mkdir -p
void iron_create_directory(char *path) {
	char dir[PATH_MAX];
	int  length = snprintf(dir, sizeof(dir), "%s", path);
	if (length <= 0 || length >= (int)sizeof(dir)) {
		return;
	}
	for (char *p = dir + 1; *p != '\0'; ++p) {
		if (*p == '/') {
			*p = '\0';
			mkdir(dir, 0777);
			*p = '/';
		}
	}
	mkdir(dir, 0777);
}

bool iron_is_directory(char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool iron_file_exists(char *path) {
	iron_file_reader_t reader;
	if (iron_file_reader_open(&reader, path, IRON_FILE_TYPE_ASSET)) {
		iron_file_reader_close(&reader);
		return true;
	}
	return false;
}

void iron_delete_file(char *path) {
#ifdef IRON_IOS
	IOSDeleteFile(path);
#else
	unlink(path);
#endif
}

void iron_file_save_bytes(char *path, buffer_t *bytes, u64 length) {
	u64 byte_length = length > 0 ? length : (u64)bytes->length;
	if (byte_length > (u64)bytes->length) {
		byte_length = (u64)bytes->length;
	}

#ifdef IRON_WINDOWS
	MultiByteToWideChar(CP_UTF8, 0, path, -1, temp_wstring, 1024);
	FILE *file = _wfopen(temp_wstring, L"wb");
#else
	FILE *file = fopen(path, "wb");
#endif
	if (file == NULL) {
		return;
	}
	fwrite(bytes->buffer, 1, byte_length, file);
	fclose(file);
}

typedef struct _callback_data {
	int32_t size;
	char   *url;
	void (*func)(char *, buffer_t *);
} _callback_data_t;

static void _https_callback(int status, const char *body, size_t length, void *callback_data) {
	_callback_data_t *cbd    = (_callback_data_t *)callback_data;
	buffer_t         *buffer = NULL;
	if (body != NULL && status < 400) {
		if (cbd->size > 0 && (size_t)cbd->size < length) {
			length = cbd->size;
		}
		buffer           = malloc(sizeof(buffer_t));
		buffer->length   = (u32)length;
		buffer->capacity = (u32)length;
		buffer->buffer   = malloc(length > 0 ? length : 1);
		memcpy(buffer->buffer, body, length);
	}
	cbd->func(cbd->url, buffer);
	free(cbd->url);
	free(cbd);
}

void iron_file_download(char *url, void (*callback)(char *, buffer_t *), i32 size, char *dst_path) {
	_callback_data_t *cbd = malloc(sizeof(_callback_data_t));
	cbd->size             = size;
	cbd->url              = strdup(url);
	cbd->func             = callback;

	// https://host/path -> host, path
	char *host = strncmp(url, "https://", 8) == 0 ? url + 8 : url;
	char *path = strchr(host, '/');
	char *base = path != NULL ? strndup(host, path - host) : strdup(host);
	iron_net_request(base, path != NULL ? path + 1 : "", NULL, 443, IRON_HTTPS_GET, NULL, &_https_callback, cbd, dst_path);
	free(base);
}

char     *sys_buffer_to_string(buffer_t *b);
buffer_t *data_get_blob(char *file);

static any_map_t *_file_download_map = NULL;

any_array_t *file_read_directory(char *path) {

	any_array_t *files = string_split(iron_read_directory(path), "\n");
	if (files->length == 1 && string_equals(files->buffer[0], "")) {
		array_pop(files);
		return files;
	}
	array_sort(files, NULL);

	// Folders first
	i32 num = files->length;
	for (i32 i = 0; i < num; ++i) {
		char *f = files->buffer[i];
		if (!iron_is_directory(path_join(path, f))) {
			array_splice(files, i, 1);
			any_array_push(files, f);
			i--;
			num--;
		}
	}

	return files;
}

void file_copy(char *src_path, char *dst_path) {
#ifdef IRON_IOS
	// No shell on ios, copy the bytes directly (relative sources resolve to the app bundle)
	iron_file_reader_t reader;
	if (!iron_file_reader_open(&reader, src_path, IRON_FILE_TYPE_ASSET)) {
		return;
	}
	size_t size = iron_file_reader_size(&reader);
	void  *data = malloc(size > 0 ? size : 1);
	if (data == NULL) {
		iron_file_reader_close(&reader);
		return;
	}
	size_t read = iron_file_reader_read(&reader, data, size);
	iron_file_reader_close(&reader);
	FILE *f = fopen(dst_path, "wb");
	if (f != NULL) {
		fwrite(data, 1, read, f);
		fclose(f);
	}
	free(data);
#else
	if (copyfile(src_path, dst_path, NULL, COPYFILE_DATA | COPYFILE_STAT | COPYFILE_XATTR) != 0) {
		iron_log("Could not copy %s to %s: %s", src_path, dst_path, strerror(errno));
	}
#endif
}

// Opens a file or folder in the default app (Finder for a folder)
void file_start(char *path) {
#ifdef IRON_MACOS
	extern char **environ;
	char         *argv[] = {"/usr/bin/open", path, NULL};
	pid_t         pid;
	if (posix_spawn(&pid, argv[0], NULL, NULL, argv, environ) == 0) {
		waitpid(pid, NULL, 0);
	}
#endif
}

static void _file_download_to_callback(char *url, buffer_t *ab) {
	file_download_data_t *fdd = any_map_get(_file_download_map, url);
	fdd->done(url);
}

void file_download_to(char *url, char *dst_path, void (*done)(char *url), i32 size) {
	if (_file_download_map == NULL) {
		_file_download_map = any_map_create();
	}
	file_download_data_t *fdd = malloc(sizeof(file_download_data_t));
	fdd->done                 = done;
	any_map_set(_file_download_map, url, fdd);
	iron_file_download(url, _file_download_to_callback, size, dst_path);
}
