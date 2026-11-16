#pragma once

// FNV-1a hashing for cache keys and signatures. Keys of a cache keep the seed they always had:
// HASH_FNV1A_BASIS, or the shorter 1469598103934665603 some caches started from.

#include <stdbool.h>
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

// 128-bit content hash (MurmurHash3 x64 128, seed 0) for content-addressed blobs: tiles, stroke records, material
// snapshots, assets (history_store.c). Persisted in projects: the function must never change.
typedef struct hash128 {
	uint64_t lo;
	uint64_t hi;
} hash128_t;

#define HASH128_NONE ((hash128_t){0, 0})

static inline bool hash128_eq(hash128_t a, hash128_t b) {
	return a.lo == b.lo && a.hi == b.hi;
}

static inline bool hash128_is_none(hash128_t a) {
	return a.lo == 0 && a.hi == 0;
}

static inline uint64_t hash128_rotl(uint64_t x, int r) {
	return (x << r) | (x >> (64 - r));
}

static inline uint64_t hash128_fmix(uint64_t k) {
	k ^= k >> 33;
	k *= 0xff51afd7ed558ccdULL;
	k ^= k >> 33;
	k *= 0xc4ceb9fe1a85ec53ULL;
	k ^= k >> 33;
	return k;
}

static inline hash128_t hash128(const void *key, size_t len) {
	const uint8_t *data = (const uint8_t *)key;
	const uint64_t c1   = 0x87c37b91114253d5ULL;
	const uint64_t c2   = 0x4cf5ad432745937fULL;
	uint64_t       h1   = 0;
	uint64_t       h2   = 0;
	size_t         n    = len / 16;
	for (size_t i = 0; i < n; ++i) {
		uint64_t k1;
		uint64_t k2;
		memcpy(&k1, data + i * 16, 8);
		memcpy(&k2, data + i * 16 + 8, 8);
		k1 *= c1;
		k1 = hash128_rotl(k1, 31);
		k1 *= c2;
		h1 ^= k1;
		h1 = hash128_rotl(h1, 27);
		h1 += h2;
		h1 = h1 * 5 + 0x52dce729;
		k2 *= c2;
		k2 = hash128_rotl(k2, 33);
		k2 *= c1;
		h2 ^= k2;
		h2 = hash128_rotl(h2, 31);
		h2 += h1;
		h2 = h2 * 5 + 0x38495ab5;
	}
	const uint8_t *tail = data + n * 16;
	size_t         rem  = len & 15;
	uint64_t       k1   = 0;
	uint64_t       k2   = 0;
	for (size_t i = 0; i < rem; ++i) { // Little endian tail, as the reference's fall-through switch
		if (i < 8) {
			k1 |= (uint64_t)tail[i] << (8 * i);
		}
		else {
			k2 |= (uint64_t)tail[i] << (8 * (i - 8));
		}
	}
	if (rem > 8) {
		k2 *= c2;
		k2 = hash128_rotl(k2, 33);
		k2 *= c1;
		h2 ^= k2;
	}
	if (rem > 0) {
		k1 *= c1;
		k1 = hash128_rotl(k1, 31);
		k1 *= c2;
		h1 ^= k1;
	}
	h1 ^= (uint64_t)len;
	h2 ^= (uint64_t)len;
	h1 += h2;
	h2 += h1;
	h1 = hash128_fmix(h1);
	h2 = hash128_fmix(h2);
	h1 += h2;
	h2 += h1;
	return (hash128_t){h1, h2};
}
