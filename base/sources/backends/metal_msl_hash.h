#pragma once

// The key of a precompiled Metal library (metal_msl_pack.h): FNV-1a 64 of the shader source with its Material Maker
// generator ids renumbered. Material Maker code names everything of a generator o<id>_..., the id being the generator's
// creation order: the app's graph (mm_bridge.c) numbers them otherwise than the generator run offline, the code is the
// same. Each distinct o<digits>_ (the o not after a letter or digit) is renumbered by order of first appearance, so two
// sources differing only by those ids, two equivalent shaders (resources bind by index, names do not matter), have the
// same key. Plain C, also built into paint/tools/material_maker/mm_check.c.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define METAL_MSL_HASH_IDS 1024 // Distinct ids renumbered, the ones past it are hashed as they are

typedef struct {
	uint64_t hash;
	size_t   length; // Of the renumbered source
} metal_msl_key_t;

static inline bool metal_msl_hash_alnum(char c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static metal_msl_key_t metal_msl_hash(const char *s, size_t n) {
	struct {
		const char *at;
		uint32_t    len;
	} ids[METAL_MSL_HASH_IDS];
	int             count = 0;
	metal_msl_key_t k     = {0xcbf29ce484222325ull, 0};
#define METAL_MSL_HASH_BYTE(c)        \
	do {                              \
		k.hash ^= (uint8_t)(c);       \
		k.hash *= 0x100000001b3ull;   \
		k.length++;                   \
	} while (0)
	for (size_t i = 0; i < n; ++i) {
		METAL_MSL_HASH_BYTE(s[i]);
		if (s[i] != 'o' || (i > 0 && metal_msl_hash_alnum(s[i - 1])) || i + 1 >= n || s[i + 1] < '0' || s[i + 1] > '9') {
			continue;
		}
		size_t j = i + 1;
		while (j < n && s[j] >= '0' && s[j] <= '9') {
			j++;
		}
		if (j >= n || s[j] != '_' || j - i - 1 > 20) {
			continue;
		}
		const char *digits = s + i + 1;
		uint32_t    len    = (uint32_t)(j - i - 1);
		int         index  = -1;
		for (int k2 = 0; k2 < count; ++k2) {
			if (ids[k2].len == len && memcmp(ids[k2].at, digits, len) == 0) {
				index = k2;
				break;
			}
		}
		if (index < 0 && count < METAL_MSL_HASH_IDS) {
			ids[count].at  = digits;
			ids[count].len = len;
			index          = count++;
		}
		if (index < 0) {
			continue; // Hashed as it is
		}
		char num[16];
		int  m = 0;
		do {
			num[m++] = (char)('0' + index % 10);
			index /= 10;
		} while (index > 0);
		while (m > 0) {
			METAL_MSL_HASH_BYTE(num[--m]);
		}
		i = j - 1; // The '_' next
	}
#undef METAL_MSL_HASH_BYTE
	return k;
}
