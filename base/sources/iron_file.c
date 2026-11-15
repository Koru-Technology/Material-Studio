
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

i32         iron_sys_command(char *cmd);
extern char temp_string[1024 * 128];

static char *fileslocation = NULL;
#if defined(IRON_MACOS) || defined(IRON_IOS)
const char *iron_get_resource_path();
#endif

void iron_internal_set_files_location(char *dir) {
	fileslocation = dir;
}

char *iron_internal_files_location(void) {
#ifdef IRON_MACOS
	static char path[1024];
	strcpy(path, iron_get_resource_path());
	strcat(path, "/");
	strcat(path, IRON_OUTDIR);
	strcat(path, "/");
	return path;
#elif defined(IRON_IOS)
	static char path[1024];
	strcpy(path, iron_get_resource_path());
	strcat(path, "/");
	strcat(path, IRON_OUTDIR);
	strcat(path, "/");
	return path;
#else
	return fileslocation;
#endif
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
	char filepath[1001];
#ifdef IRON_IOS
	strcpy(filepath, type == IRON_FILE_TYPE_SAVE ? iron_internal_save_path() : iron_get_resource_path());
	if (type != IRON_FILE_TYPE_SAVE) {
		strcat(filepath, "/");
		strcat(filepath, IRON_OUTDIR);
		strcat(filepath, "/");
	}

	strcat(filepath, filename);
#endif
#ifdef IRON_MACOS
	strcpy(filepath, type == IRON_FILE_TYPE_SAVE ? iron_internal_save_path() : iron_get_resource_path());
	if (type != IRON_FILE_TYPE_SAVE) {
		strcat(filepath, "/");
		strcat(filepath, IRON_OUTDIR);
		strcat(filepath, "/");
	}
	strcat(filepath, filename);
#endif

	bool is_abs = filename[0] == '/';

	if (is_abs) {
		strcpy(filepath, filename);
	}
	else if (fileslocation != NULL && type != IRON_FILE_TYPE_SAVE) {
		strcpy(filepath, fileslocation);
		strcat(filepath, "/");
		strcat(filepath, filename);
	}

	reader->data = fopen(filepath, "rb");
	if (reader->data == NULL) {
		return false;
	}

	fseek((FILE *)reader->data, 0, SEEK_END);
	reader->size = ftell((FILE *)reader->data);
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

float iron_read_f32le(uint8_t *data) {
	return *(float *)data;
}

uint64_t iron_read_u64le(uint8_t *data) {
	return *(uint64_t *)data;
}

int64_t iron_read_s64le(uint8_t *data) {
	return *(int64_t *)data;
}

uint32_t iron_read_u32le(uint8_t *data) {
	return *(uint32_t *)data;
}

int32_t iron_read_s32le(uint8_t *data) {
	return *(int32_t *)data;
}

uint16_t iron_read_u16le(uint8_t *data) {
	return *(uint16_t *)data;
}

int16_t iron_read_s16le(uint8_t *data) {
	return *(int16_t *)data;
}

uint8_t iron_read_u8(uint8_t *data) {
	return *data;
}

int8_t iron_read_s8(uint8_t *data) {
	return *(int8_t *)data;
}

bool iron_file_writer_open(iron_file_writer_t *writer, const char *filepath) {
	writer->file = NULL;

	char path[1001];
	strcpy(path, iron_internal_save_path());
	strcat(path, filepath);

	writer->file = fopen(path, "wb");
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

char *iron_read_directory(char *path) {
	char *files = temp_string;
	files[0]    = 0;

	directory dir = open_dir(path);
	if (dir.handle == NULL) {
		return files;
	}

	while (true) {
		file f = read_next_file(&dir);
		if (!f.valid) {
			break;
		}

		if (files[0] != '\0') {
			strcat(files, "\n");
		}
		strcat(files, f.name);
	}
	close_dir(&dir);
	return files;
}

void iron_create_directory(char *path) {
#ifdef IRON_IOS
	IOSCreateDirectory(path);
#else
	char cmd[1024];
	strcpy(cmd, "mkdir -p \"");
	strcat(cmd, path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
#endif
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
	char cmd[1024];
	strcpy(cmd, "rm \"");
	strcat(cmd, path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
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

static char *file_cmd_copy = "cp";

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
	size_t read = iron_file_reader_read(&reader, data, size);
	iron_file_reader_close(&reader);
	FILE *f = fopen(dst_path, "wb");
	if (f != NULL) {
		fwrite(data, 1, read, f);
		fclose(f);
	}
	free(data);
#else
	char cmd[1024];
	strcpy(cmd, file_cmd_copy);
	strcat(cmd, " \"");
	strcat(cmd, src_path);
	strcat(cmd, "\" \"");
	strcat(cmd, dst_path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
#endif
}

void file_start(char *path) {
	char cmd[1024];
	strcpy(cmd, "open \"");
	strcat(cmd, path);
	strcat(cmd, "\"");
	iron_sys_command(cmd);
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
