#pragma once

// FNV-1a hashing for cache keys and signatures, and half float decoding. Keys of a cache keep the seed they always had:
// HASH_FNV1A_BASIS, or the shorter 1469598103934665603 some caches started from.

#include <stdint.h>
#include <string.h>

#define HASH_FNV1A_BASIS 14695981039346656037ULL

static inline uint64_t hash_fnv1a(uint64_t h, const void *data, size_t n) {
	const uint8_t *p = (const uint8_t *)data;
	for (size_t i = 0; i < n; ++i) {
		h ^= p[i];
		h *= 1099511628211ULL;
	}
	return h;
}

static inline uint64_t hash_fnv1a_i32(uint64_t h, int32_t v) {
	return hash_fnv1a(h, &v, sizeof(v));
}

// The characters and the length, NULL hashes as -1
static inline uint64_t hash_fnv1a_str(uint64_t h, const char *s) {
	if (s == NULL) {
		return hash_fnv1a_i32(h, -1);
	}
	size_t n = strlen(s);
	return hash_fnv1a_i32(hash_fnv1a(h, s, n), (int32_t)n);
}

// IEEE 754 half to float, subnormals, infinities and NaN included
static inline float f16_to_f32(uint16_t h) {
	uint32_t sign = (uint32_t)(h >> 15) << 31;
	int32_t  exp  = (h >> 10) & 0x1f;
	uint32_t man  = h & 0x3ff;
	uint32_t bits;
	if (exp == 0) {
		if (man == 0) {
			bits = sign;
		}
		else { // Subnormal
			exp = 127 - 15 + 1;
			while ((man & 0x400) == 0) {
				man <<= 1;
				exp--;
			}
			bits = sign | ((uint32_t)exp << 23) | ((man & 0x3ff) << 13);
		}
	}
	else if (exp == 31) {
		bits = sign | 0x7f800000 | (man << 13);
	}
	else {
		bits = sign | ((uint32_t)(exp - 15 + 127) << 23) | (man << 13);
	}
	float f;
	memcpy(&f, &bits, 4);
	return f;
}
