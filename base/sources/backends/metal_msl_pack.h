#pragma once

// Precompiled Metal libraries looked up by their source (metal_library): a pack made offline, the libraries of sources
// known in advance (paint/tools/material_maker/build_metallib.py: the Material Maker library's buffer passes), so they
// load without compiling. A source is found by its key (metal_msl_hash.h: a hash of the source, generator ids
// renumbered), a source that changed otherwise simply misses.
// Pack (little endian): "MTLP", u32 version 1, u32 entry count, u32 block count; blocks of 16 bytes (u32 offset from
// the file start, u32 stored size, u32 size, u32 compression: 0 none, 1 LZMA in an xz container); entries of 24 bytes
// sorted by hash (u64 key hash, u32 block, u32 offset in the block, u32 size, u32 key length); then the blocks. A block
// holds the metallibs of the shaders a material adds, compressed together: they are asked for together and share a lot.
// Self-contained (the test in paint/tools/material_maker/metallib_test.m includes it): needs Foundation and iron_file.h.

#import <Foundation/Foundation.h>
#include "metal_msl_hash.h"
#include <os/lock.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	uint32_t offset;
	uint32_t stored;
	uint32_t size;
	uint32_t compression;
} metal_msl_pack_block_t;

typedef struct {
	uint64_t hash;
	uint32_t block;
	uint32_t offset;
	uint32_t size;
	uint32_t length; // metal_msl_key_t length, a check on top of the hash
} metal_msl_pack_entry_t;

#ifndef METAL_MSL_PACK_PATH
#ifdef IRON_IOS
#define METAL_MSL_PACK_PATH "./data/material_maker/msl_ios.pack"
#else
#define METAL_MSL_PACK_PATH "./data/material_maker/msl_macos.pack"
#endif
#endif

#define METAL_MSL_PACK_CACHE 8 // Blocks kept decompressed: a material's shaders are asked for together

static struct {
	const char             *path;
	bool                    loaded;
	uint32_t                count;
	uint32_t                nblocks;
	metal_msl_pack_entry_t *entries;
	metal_msl_pack_block_t *blocks;
	int32_t                 cached_block[METAL_MSL_PACK_CACHE];
	void                   *cached_data[METAL_MSL_PACK_CACHE]; // NSData, retained
	uint32_t                cached_use[METAL_MSL_PACK_CACHE];
	uint32_t                use;
	uint32_t                decompressed; // Blocks read, to see how often the cache misses
	os_unfair_lock          lock;
} metal_msl_pack = {.path = METAL_MSL_PACK_PATH, .lock = OS_UNFAIR_LOCK_INIT};

static bool metal_msl_pack_read(iron_file_reader_t *r, size_t offset, void *dst, size_t n) {
	return offset + n <= iron_file_reader_size(r) && iron_file_reader_seek(r, offset) && iron_file_reader_read(r, dst, n) == n;
}

// The index, once (locked by the caller). A missing or damaged pack: nothing is found.
static void metal_msl_pack_load(void) {
	metal_msl_pack.loaded = true;
	for (int i = 0; i < METAL_MSL_PACK_CACHE; ++i) {
		metal_msl_pack.cached_block[i] = -1;
	}
	iron_file_reader_t r;
	if (metal_msl_pack.path == NULL || !iron_file_reader_open(&r, metal_msl_pack.path, IRON_FILE_TYPE_ASSET)) {
		return;
	}
	uint32_t head[4];
	if (metal_msl_pack_read(&r, 0, head, sizeof(head)) && memcmp(head, "MTLP", 4) == 0 && head[1] == 1 && head[2] < (1u << 24) && head[3] < (1u << 24)) {
		metal_msl_pack_block_t *blocks  = malloc(sizeof(metal_msl_pack_block_t) * head[3] + 1);
		metal_msl_pack_entry_t *entries = malloc(sizeof(metal_msl_pack_entry_t) * head[2] + 1);
		if (metal_msl_pack_read(&r, sizeof(head), blocks, sizeof(metal_msl_pack_block_t) * head[3]) &&
		    metal_msl_pack_read(&r, sizeof(head) + sizeof(metal_msl_pack_block_t) * head[3], entries, sizeof(metal_msl_pack_entry_t) * head[2])) {
			metal_msl_pack.blocks  = blocks;
			metal_msl_pack.entries = entries;
			metal_msl_pack.nblocks = head[3];
			metal_msl_pack.count   = head[2];
		}
		else {
			free(blocks);
			free(entries);
		}
	}
	iron_file_reader_close(&r);
}

// A block's bytes, decompressed (locked by the caller)
static NSData *metal_msl_pack_block(uint32_t b) {
	metal_msl_pack.use++;
	int oldest = 0;
	for (int i = 0; i < METAL_MSL_PACK_CACHE; ++i) {
		if (metal_msl_pack.cached_block[i] == (int32_t)b) {
			metal_msl_pack.cached_use[i] = metal_msl_pack.use;
			return (__bridge NSData *)metal_msl_pack.cached_data[i];
		}
		if (metal_msl_pack.cached_use[i] < metal_msl_pack.cached_use[oldest]) {
			oldest = i;
		}
	}
	metal_msl_pack_block_t *k = &metal_msl_pack.blocks[b];
	iron_file_reader_t      r;
	if (!iron_file_reader_open(&r, metal_msl_pack.path, IRON_FILE_TYPE_ASSET)) {
		return nil;
	}
	NSMutableData *stored = [NSMutableData dataWithLength:k->stored];
	bool           read   = metal_msl_pack_read(&r, k->offset, stored.mutableBytes, k->stored);
	iron_file_reader_close(&r);
	if (!read) {
		return nil;
	}
	NSData *data = stored;
	if (k->compression == 1) {
		NSError *error = nil;
		data           = [stored decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmLZMA error:&error];
	}
	else if (k->compression != 0) {
		data = nil;
	}
	if (data == nil || data.length != k->size) {
		return nil;
	}
	metal_msl_pack.decompressed++;
	if (metal_msl_pack.cached_data[oldest] != NULL) {
		CFRelease(metal_msl_pack.cached_data[oldest]);
	}
	metal_msl_pack.cached_data[oldest]  = (__bridge_retained void *)data;
	metal_msl_pack.cached_block[oldest] = (int32_t)b;
	metal_msl_pack.cached_use[oldest]   = metal_msl_pack.use;
	return data;
}

// The metallib compiled from that source, nil when the pack has none. Any thread.
static dispatch_data_t metal_msl_pack_find(const char *source, size_t length) {
	metal_msl_key_t key  = metal_msl_hash(source, length); // Outside the lock
	uint64_t        hash = key.hash;
	dispatch_data_t rv   = nil;
	os_unfair_lock_lock(&metal_msl_pack.lock);
	if (!metal_msl_pack.loaded) {
		metal_msl_pack_load();
	}
	uint32_t lo = 0, hi = metal_msl_pack.count;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		if (metal_msl_pack.entries[mid].hash < hash) {
			lo = mid + 1;
		}
		else {
			hi = mid;
		}
	}
	if (lo < metal_msl_pack.count && metal_msl_pack.entries[lo].hash == hash && metal_msl_pack.entries[lo].length == key.length) {
		metal_msl_pack_entry_t *e = &metal_msl_pack.entries[lo];
		NSData                 *d = e->block < metal_msl_pack.nblocks ? metal_msl_pack_block(e->block) : nil;
		if (d != nil && (uint64_t)e->offset + e->size <= d.length) {
			rv = dispatch_data_create((const uint8_t *)d.bytes + e->offset, e->size, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT); // A copy
		}
	}
	os_unfair_lock_unlock(&metal_msl_pack.lock);
	return rv;
}
