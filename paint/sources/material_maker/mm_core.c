// Arena, strings, json and the pack format for the Material Maker support (mm.h)

#include "mm.h"
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MM_STANDALONE // The app has it in iron_compress.c
#define SINFL_IMPLEMENTATION
#endif
#include "../../../base/sources/libs/sinfl.h"

// Arena: blocks chained, freed together

typedef struct mm_block {
	struct mm_block *next;
	size_t           used;
	size_t           cap;
	char             data[];
} mm_block_t;

static mm_block_t *mm_blocks = NULL; // Newest first
static mm_block_t *mm_big    = NULL; // Large allocations, one block each, newest first
static size_t      mm_total  = 0;    // Bytes in the arena
static size_t      mm_limit  = (size_t)1 << 30;
jmp_buf           *mm_guard  = NULL;

void mm_arena_limit(size_t bytes) {
	mm_limit = bytes;
}

static void free_until(mm_block_t **list, mm_block_t *until) {
	while (*list != NULL && *list != until) {
		mm_block_t *next = (*list)->next;
		mm_total -= (*list)->cap;
		free(*list);
		*list = next;
	}
}

static mm_block_t *new_block(size_t cap) {
	if (mm_total + cap > mm_limit && mm_guard != NULL) {
		jmp_buf *g = mm_guard;
		mm_guard   = NULL; // Once: the handler may allocate
		longjmp(*g, 1);
	}
	mm_block_t *b = malloc(sizeof(mm_block_t) + cap);
	mm_total += cap;
	b->cap = cap;
	return b;
}

void mm_arena_begin(void) {
	mm_arena_end();
}

void mm_arena_end(void) {
	free_until(&mm_blocks, NULL);
	free_until(&mm_big, NULL);
}

mm_mark_t mm_arena_mark(void) {
	mm_mark_t m = {mm_blocks, mm_blocks != NULL ? mm_blocks->used : 0, mm_big};
	return m;
}

void mm_arena_release(mm_mark_t m) {
	free_until(&mm_blocks, (mm_block_t *)m.block);
	if (mm_blocks != NULL) {
		mm_blocks->used = m.used;
	}
	free_until(&mm_big, (mm_block_t *)m.big);
}

void *mm_alloc(size_t size) {
	size = (size + 15) & ~(size_t)15;
	if (size > (1 << 16)) { // Large: a block of its own, the current one stays in use
		mm_block_t *b = new_block(size);
		b->used       = size;
		b->next       = mm_big;
		mm_big        = b;
		memset(b->data, 0, size);
		return b->data;
	}
	if (mm_blocks == NULL || mm_blocks->used + size > mm_blocks->cap) {
		mm_block_t *b = new_block(1 << 20);
		b->next       = mm_blocks;
		b->used       = 0;
		mm_blocks     = b;
	}
	void *p = mm_blocks->data + mm_blocks->used;
	mm_blocks->used += size;
	memset(p, 0, size);
	return p;
}

// Strings

char *mm_strndup(const char *s, size_t n) {
	char *r = mm_alloc(n + 1);
	memcpy(r, s, n);
	r[n] = 0;
	return r;
}

char *mm_strdup(const char *s) {
	return s == NULL ? NULL : mm_strndup(s, strlen(s));
}

char *mm_printf(const char *fmt, ...) {
	va_list a;
	va_start(a, fmt);
	int n = vsnprintf(NULL, 0, fmt, a);
	va_end(a);
	char *r = mm_alloc(n + 1);
	va_start(a, fmt);
	vsnprintf(r, n + 1, fmt, a);
	va_end(a);
	return r;
}

char *mm_replace(const char *s, const char *from, const char *to) {
	size_t   fl = strlen(from);
	mm_buf_t b  = {0};
	if (fl == 0) {
		return mm_strdup(s);
	}
	const char *p = s;
	while (true) {
		const char *hit = strstr(p, from);
		if (hit == NULL) {
			mm_buf_add(&b, p);
			break;
		}
		mm_buf_addn(&b, p, hit - p);
		mm_buf_add(&b, to);
		p = hit + fl;
	}
	return mm_buf_get(&b);
}

bool mm_starts_with(const char *s, const char *prefix) {
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool mm_contains(const char *s, const char *what) {
	return s != NULL && strstr(s, what) != NULL;
}

char *mm_strip(const char *s) {
	while (*s && isspace((unsigned char)*s)) {
		s++;
	}
	size_t n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1])) {
		n--;
	}
	return mm_strndup(s, n);
}

void mm_buf_addn(mm_buf_t *b, const char *s, size_t n) {
	if (b->len + n + 1 > b->cap) {
		size_t cap = b->cap == 0 ? 256 : b->cap;
		while (b->len + n + 1 > cap) {
			cap *= 2;
		}
		char *d = mm_alloc(cap);
		if (b->data != NULL) {
			memcpy(d, b->data, b->len);
		}
		b->data = d;
		b->cap  = cap;
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = 0;
}

void mm_buf_add(mm_buf_t *b, const char *s) {
	mm_buf_addn(b, s, strlen(s));
}

void mm_buf_printf(mm_buf_t *b, const char *fmt, ...) {
	va_list a;
	va_start(a, fmt);
	int n = vsnprintf(NULL, 0, fmt, a);
	va_end(a);
	char *r = mm_alloc(n + 1);
	va_start(a, fmt);
	vsnprintf(r, n + 1, fmt, a);
	va_end(a);
	mm_buf_addn(b, r, n);
}

char *mm_buf_get(mm_buf_t *b) {
	return b->data != NULL ? b->data : mm_strdup("");
}

// Json

typedef struct {
	const char *s;
	const char *end;
	bool        error;
} mm_parser_t;

static void json_ws(mm_parser_t *p) {
	while (p->s < p->end && (isspace((unsigned char)*p->s) || *p->s == ',' && false)) {
		p->s++;
	}
}

static mm_json_t *json_value(mm_parser_t *p);

mm_json_t *mm_json_new(mm_json_type_t type) {
	mm_json_t *v = mm_alloc(sizeof(mm_json_t));
	v->type      = type;
	return v;
}

void mm_json_push(mm_json_t *arr, mm_json_t *v) {
	mm_json_t **items = mm_alloc(sizeof(mm_json_t *) * (arr->count + 1));
	char      **keys  = arr->type == MM_JSON_OBJECT ? mm_alloc(sizeof(char *) * (arr->count + 1)) : NULL;
	if (arr->count > 0) {
		memcpy(items, arr->items, sizeof(mm_json_t *) * arr->count);
		if (keys != NULL) {
			memcpy(keys, arr->keys, sizeof(char *) * arr->count);
		}
	}
	items[arr->count] = v;
	arr->items        = items;
	arr->keys         = keys;
	arr->count++;
}

void mm_json_set(mm_json_t *obj, const char *key, mm_json_t *v) {
	for (int i = 0; i < obj->count; ++i) {
		if (strcmp(obj->keys[i], key) == 0) {
			obj->items[i] = v;
			return;
		}
	}
	mm_json_push(obj, v);
	obj->keys[obj->count - 1] = mm_strdup(key);
}

static void utf8_put(mm_buf_t *b, unsigned cp) {
	char out[4];
	int  n = 0;
	if (cp < 0x80) {
		out[n++] = (char)cp;
	}
	else if (cp < 0x800) {
		out[n++] = (char)(0xC0 | (cp >> 6));
		out[n++] = (char)(0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000) {
		out[n++] = (char)(0xE0 | (cp >> 12));
		out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[n++] = (char)(0x80 | (cp & 0x3F));
	}
	else {
		out[n++] = (char)(0xF0 | (cp >> 18));
		out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
		out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[n++] = (char)(0x80 | (cp & 0x3F));
	}
	mm_buf_addn(b, out, n);
}

static char *json_string(mm_parser_t *p) {
	mm_buf_t b = {0};
	p->s++; // "
	while (p->s < p->end && *p->s != '"') {
		const char *start = p->s;
		while (p->s < p->end && *p->s != '"' && *p->s != '\\') {
			p->s++;
		}
		mm_buf_addn(&b, start, p->s - start);
		if (p->s < p->end && *p->s == '\\') {
			p->s++;
			char c = p->s < p->end ? *p->s++ : 0;
			switch (c) {
			case 'n':
				mm_buf_add(&b, "\n");
				break;
			case 't':
				mm_buf_add(&b, "\t");
				break;
			case 'r':
				mm_buf_add(&b, "\r");
				break;
			case 'b':
				mm_buf_add(&b, "\b");
				break;
			case 'f':
				mm_buf_add(&b, "\f");
				break;
			case 'u': {
				unsigned cp = 0;
				for (int i = 0; i < 4 && p->s < p->end; ++i) {
					char h = *p->s++;
					cp     = cp * 16 + (isdigit((unsigned char)h) ? h - '0' : (tolower((unsigned char)h) - 'a' + 10));
				}
				if (cp >= 0xD800 && cp < 0xDC00 && p->s + 6 <= p->end && p->s[0] == '\\' && p->s[1] == 'u') {
					unsigned lo = 0;
					p->s += 2;
					for (int i = 0; i < 4; ++i) {
						char h = *p->s++;
						lo     = lo * 16 + (isdigit((unsigned char)h) ? h - '0' : (tolower((unsigned char)h) - 'a' + 10));
					}
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}
				utf8_put(&b, cp);
				break;
			}
			default: {
				char s[2] = {c, 0};
				mm_buf_add(&b, s);
			}
			}
		}
	}
	if (p->s >= p->end) {
		p->error = true;
		return mm_strdup("");
	}
	p->s++; // "
	return mm_buf_get(&b);
}

static mm_json_t *json_value(mm_parser_t *p) {
	json_ws(p);
	if (p->s >= p->end) {
		p->error = true;
		return mm_json_new(MM_JSON_NULL);
	}
	char c = *p->s;
	if (c == '{' || c == '[') {
		bool       obj = c == '{';
		mm_json_t *v   = mm_json_new(obj ? MM_JSON_OBJECT : MM_JSON_ARRAY);
		p->s++;
		while (true) {
			json_ws(p);
			while (p->s < p->end && *p->s == ',') { // Trailing and doubled commas
				p->s++;
				json_ws(p);
			}
			if (p->s >= p->end) {
				p->error = true;
				return v;
			}
			if (*p->s == (obj ? '}' : ']')) {
				p->s++;
				return v;
			}
			char *key = NULL;
			if (obj) {
				if (*p->s != '"') {
					p->error = true;
					return v;
				}
				key = json_string(p);
				json_ws(p);
				if (p->s < p->end && *p->s == ':') {
					p->s++;
				}
			}
			mm_json_t *item = json_value(p);
			if (p->error) {
				return v;
			}
			mm_json_push(v, item);
			if (obj) {
				v->keys[v->count - 1] = key;
			}
		}
	}
	if (c == '"') {
		mm_json_t *v = mm_json_new(MM_JSON_STRING);
		v->string    = json_string(p);
		return v;
	}
	if (strncmp(p->s, "true", 4) == 0 || strncmp(p->s, "false", 5) == 0) {
		mm_json_t *v = mm_json_new(MM_JSON_BOOL);
		v->boolean   = c == 't';
		p->s += v->boolean ? 4 : 5;
		return v;
	}
	if (strncmp(p->s, "null", 4) == 0) {
		p->s += 4;
		return mm_json_new(MM_JSON_NULL);
	}
	char  *end;
	double d = strtod(p->s, &end);
	if (end == p->s) {
		p->error = true;
		return mm_json_new(MM_JSON_NULL);
	}
	mm_json_t *v = mm_json_new(MM_JSON_NUMBER);
	v->number    = d;
	v->is_int    = true;
	for (const char *q = p->s; q < end; ++q) {
		if (*q == '.' || *q == 'e' || *q == 'E') {
			v->is_int = false;
		}
	}
	p->s = end;
	return v;
}

mm_json_t *mm_json_parse(const char *text, size_t len) {
	mm_parser_t p = {text, text + len, false};
	mm_json_t  *v = json_value(&p);
	return p.error ? NULL : v;
}

// Parsed outside the current arena, kept for the app's life (library definitions)
mm_json_t *mm_json_parse_persistent(const char *text, size_t len) {
	mm_block_t *saved       = mm_blocks;
	mm_block_t *saved_big   = mm_big;
	size_t      saved_total = mm_total;
	jmp_buf    *saved_guard = mm_guard;
	mm_blocks               = NULL;
	mm_big                  = NULL;
	mm_guard                = NULL;
	mm_json_t *v            = mm_json_parse(text, len);
	mm_blocks               = saved; // The new blocks are not chained to the arena, they are never freed
	mm_big                  = saved_big;
	mm_total                = saved_total;
	mm_guard                = saved_guard;
	return v;
}

mm_json_t *mm_json_get(mm_json_t *obj, const char *key) {
	if (obj == NULL || obj->type != MM_JSON_OBJECT) {
		return NULL;
	}
	for (int i = 0; i < obj->count; ++i) {
		if (strcmp(obj->keys[i], key) == 0) {
			return obj->items[i];
		}
	}
	return NULL;
}

mm_json_t *mm_json_at(mm_json_t *arr, int i) {
	if (arr == NULL || (arr->type != MM_JSON_ARRAY && arr->type != MM_JSON_OBJECT) || i < 0 || i >= arr->count) {
		return NULL;
	}
	return arr->items[i];
}

const char *mm_json_str(mm_json_t *v, const char *fallback) {
	if (v == NULL) {
		return fallback;
	}
	if (v->type == MM_JSON_STRING) {
		return v->string;
	}
	if (v->type == MM_JSON_ARRAY) { // Multi-line strings are saved as arrays of lines
		mm_buf_t b = {0};
		for (int i = 0; i < v->count; ++i) {
			if (i > 0) {
				mm_buf_add(&b, "\n");
			}
			mm_buf_add(&b, mm_json_str(v->items[i], ""));
		}
		return mm_buf_get(&b);
	}
	if (v->type == MM_JSON_NUMBER) {
		return v->is_int ? mm_printf("%lld", (long long)v->number) : mm_printf("%.9g", v->number);
	}
	if (v->type == MM_JSON_BOOL) {
		return v->boolean ? "true" : "false";
	}
	return fallback;
}

double mm_json_num(mm_json_t *v, double fallback) {
	if (v == NULL) {
		return fallback;
	}
	if (v->type == MM_JSON_NUMBER) {
		return v->number;
	}
	if (v->type == MM_JSON_BOOL) {
		return v->boolean ? 1.0 : 0.0;
	}
	if (v->type == MM_JSON_STRING) {
		char  *end;
		double d = strtod(v->string, &end);
		return end != v->string ? d : fallback;
	}
	return fallback;
}

static void json_text(mm_buf_t *b, mm_json_t *v) {
	if (v == NULL) {
		mm_buf_add(b, "null");
		return;
	}
	switch (v->type) {
	case MM_JSON_NULL:
		mm_buf_add(b, "null");
		break;
	case MM_JSON_BOOL:
		mm_buf_add(b, v->boolean ? "true" : "false");
		break;
	case MM_JSON_NUMBER:
		mm_buf_printf(b, "%.17g", v->number);
		break;
	case MM_JSON_STRING:
		mm_buf_add(b, "\"");
		for (const char *s = v->string; *s; ++s) {
			if (*s == '"' || *s == '\\') {
				mm_buf_addn(b, "\\", 1);
			}
			if (*s == '\n') {
				mm_buf_add(b, "\\n");
				continue;
			}
			mm_buf_addn(b, s, 1);
		}
		mm_buf_add(b, "\"");
		break;
	case MM_JSON_ARRAY:
	case MM_JSON_OBJECT:
		mm_buf_add(b, v->type == MM_JSON_ARRAY ? "[" : "{");
		for (int i = 0; i < v->count; ++i) {
			if (v->items[i] == NULL && v->type == MM_JSON_OBJECT) { // Removed member (mm_json_set NULL)
				continue;
			}
			if (b->len > 0 && b->data[b->len - 1] != '[' && b->data[b->len - 1] != '{') {
				mm_buf_add(b, ",");
			}
			if (v->type == MM_JSON_OBJECT) {
				mm_json_t k = {.type = MM_JSON_STRING, .string = v->keys[i]};
				json_text(b, &k);
				mm_buf_add(b, ":");
			}
			json_text(b, v->items[i]);
		}
		mm_buf_add(b, v->type == MM_JSON_ARRAY ? "]" : "}");
		break;
	}
}

char *mm_json_text(mm_json_t *v) {
	mm_buf_t b = {0};
	json_text(&b, v);
	return mm_buf_get(&b);
}

bool mm_json_truthy(mm_json_t *v) {
	if (v == NULL) {
		return false;
	}
	switch (v->type) {
	case MM_JSON_BOOL:
		return v->boolean;
	case MM_JSON_NUMBER:
		return v->number != 0.0;
	case MM_JSON_STRING:
		return v->string[0] != 0;
	case MM_JSON_ARRAY:
	case MM_JSON_OBJECT:
		return v->count > 0;
	default:
		return false;
	}
}

bool mm_json_bool(mm_json_t *v, bool fallback) {
	if (v == NULL) {
		return fallback;
	}
	if (v->type == MM_JSON_BOOL) {
		return v->boolean;
	}
	if (v->type == MM_JSON_NUMBER) {
		return v->number != 0.0;
	}
	return fallback;
}

mm_json_t *mm_json_copy(mm_json_t *v) {
	if (v == NULL) {
		return NULL;
	}
	mm_json_t *c = mm_alloc(sizeof(mm_json_t));
	*c           = *v;
	if (v->count > 0) {
		c->items = mm_alloc(sizeof(mm_json_t *) * v->count);
		for (int i = 0; i < v->count; ++i) {
			c->items[i] = mm_json_copy(v->items[i]);
		}
		if (v->keys != NULL) {
			c->keys = mm_alloc(sizeof(char *) * v->count);
			memcpy(c->keys, v->keys, sizeof(char *) * v->count);
		}
	}
	return c;
}

// Pack: "MMPK", u32 version, u32 count, u32 index size, index json, raw deflate blobs

struct mm_pack {
	const uint8_t *data;
	size_t         size;
	size_t         blobs; // Offset of the first blob
	mm_json_t     *index;
};

static uint32_t rd32(const uint8_t *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

mm_pack_t *mm_pack_open(const uint8_t *data, size_t size) {
	if (size < 16 || memcmp(data, "MMPK", 4) != 0) {
		return NULL;
	}
	uint32_t index_size = rd32(data + 12);
	if (16 + (size_t)index_size > size) {
		return NULL;
	}
	// The index outlives arenas: it is parsed into its own arena block chain
	mm_json_t *index = mm_json_parse_persistent((const char *)data + 16, index_size); // Packs live as long as the app
	if (index == NULL) {
		return NULL;
	}
	mm_pack_t *p = calloc(1, sizeof(mm_pack_t));
	p->data      = data;
	p->size      = size;
	p->blobs     = 16 + index_size;
	p->index     = index;
	return p;
}

int mm_pack_count(mm_pack_t *p) {
	return p->index->count;
}

mm_json_t *mm_pack_entry(mm_pack_t *p, int i) {
	return mm_json_at(p->index, i);
}

char *mm_pack_read(mm_pack_t *p, int i, size_t *len) {
	mm_json_t *e = mm_pack_entry(p, i);
	if (e == NULL) {
		return NULL;
	}
	size_t o = (size_t)mm_json_num(mm_json_get(e, "o"), 0);
	int    n = (int)mm_json_num(mm_json_get(e, "n"), 0);
	int    u = (int)mm_json_num(mm_json_get(e, "u"), 0);
	if (p->blobs + o + n > p->size) {
		return NULL;
	}
	char *out = malloc(u + 1);
	if (sinflate(out, u, p->data + p->blobs + o, n) != u) {
		free(out);
		return NULL;
	}
	out[u] = 0;
	if (len != NULL) {
		*len = u;
	}
	return out;
}

int mm_pack_find(mm_pack_t *p, const char *key, const char *value) {
	for (int i = 0; i < p->index->count; ++i) {
		if (strcmp(mm_json_str(mm_json_get(p->index->items[i], key), ""), value) == 0) {
			return i;
		}
	}
	return -1;
}
