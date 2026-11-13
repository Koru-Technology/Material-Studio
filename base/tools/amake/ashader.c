// Shader compiler: GLSL (iron_glsl.c) to the Metal shader files the app loads

#include "../../sources/iron_glsl.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef IRON_WINDOWS
#include <windows.h>
#endif

typedef enum {
	IRON_LOG_LEVEL_INFO,
	IRON_LOG_LEVEL_ERROR
} iron_log_level_t;

bool ashader_ios = false; // Metal output for iOS instead of macOS

// GLSL source (#ifdef VERT / FRAG) to <to>.vert.metal and <to>.frag.metal, see iron_glsl.h
static int glsl_compile(char *from, char *to, char **defines, int defines_count) {
	FILE *fp = fopen(from, "rb");
	if (fp == NULL) {
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	char *glsl = malloc(size + 1);
	fread(glsl, 1, size, fp);
	glsl[size] = 0;
	fclose(fp);

	char pre[4096] = "";
	for (int i = 0; i < defines_count; ++i) {
		strcat(pre, "#define ");
		strcat(pre, defines[i]);
		strcat(pre, "\n");
	}

	char base[1024];
	strcpy(base, to);
	char *dot = strrchr(base, '.');
	if (dot != NULL) {
		*dot = 0;
	}
	for (int stage = 0; stage < 2; ++stage) {
		char *error = NULL;
		char *msl   = iron_glsl_to_msl(glsl, stage, pre, ashader_ios, &error);
		if (msl == NULL) {
			fprintf(stderr, "Error: %s\n%s\n", from, error != NULL ? error : "");
			free(glsl);
			return 1;
		}
		char path[1100];
		snprintf(path, sizeof(path), "%s.%s.metal", base, stage == 0 ? "vert" : "frag");
		FILE *out = fopen(path, "wb");
		if (out == NULL) {
			fprintf(stderr, "Error: can not write %s\n", path);
			free(glsl);
			return 1;
		}
		fprintf(out, "//>%s\n%s", stage == 0 ? "vert" : "frag", msl);
		fclose(out);
	}
	free(glsl);
	return 0;
}

int ashader(char *shader_lang, char *from, char *to, char **defines, int defines_count) {
	// shader_lang == hlsl || metal || spirv || wgsl
	size_t n = strlen(from);
	if (n < 6 || strcmp(from + n - 5, ".glsl") != 0) {
		fprintf(stderr, "Error: %s: shaders are .glsl files\n", from);
		return 1;
	}
	if (strcmp(shader_lang, "metal") != 0) {
		fprintf(stderr, "Error: %s: glsl shaders compile to metal only\n", from);
		return 1;
	}
	return glsl_compile(from, to, defines, defines_count);
}

#ifdef IRON_WINDOWS
void iron_microsoft_format(const char *format, va_list args, wchar_t *buffer) {
	char cbuffer[4096];
	vsprintf(cbuffer, format, args);
	MultiByteToWideChar(CP_UTF8, 0, cbuffer, -1, buffer, 4096);
}
#endif

void iron_log_args(iron_log_level_t level, const char *format, va_list args) {
#ifdef IRON_WINDOWS
	wchar_t buffer[4096];
	iron_microsoft_format(format, args, buffer);
	wcscat(buffer, L"\r\n");
	OutputDebugStringW(buffer);
	DWORD written;
	WriteConsoleW(GetStdHandle(level == IRON_LOG_LEVEL_INFO ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE), buffer, (DWORD)wcslen(buffer), &written, NULL);
#else
	char buffer[4096];
	vsnprintf(buffer, 4090, format, args);
	strcat(buffer, "\n");
	fprintf(level == IRON_LOG_LEVEL_INFO ? stdout : stderr, "%s", buffer);
#endif
}

void iron_log(const char *format, ...) {
	va_list args;
	va_start(args, format);
	iron_log_args(IRON_LOG_LEVEL_INFO, format == NULL ? "null" : format, args);
	va_end(args);
}
