// Checks a pack of build_metallib.py with the app's reader (base/sources/backends/metal_msl_pack.h), macOS:
//   clang -fobjc-arc -framework Foundation -framework Metal metallib_test.m -o metallib_test
//   metallib_test <msl_<platform>.pack> <dump>/<platform> [--load] [--material <id>]
// Every source of the dump is looked up as metal_library does (metal_msl_hash.h); a hit's bytes must be those of
// <dump>/<platform>_lib/<hash>.metallib, <hash> the source's file name. A dump of the app's own passes (mm_check with
// .glsl arguments) gives the share of them the pack has. --load (the macOS pack): each hit is also made a library with
// newLibraryWithData, whose function must be the one the source names ("//>vert" or "//>frag"), and a few are timed
// against compiling the source as the app does (newLibraryWithSource:options:nil).

#import <Metal/Metal.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

// The few iron_file.h calls the reader makes
#define IRON_FILE_TYPE_ASSET 0
typedef struct {
	FILE  *f;
	size_t size;
} iron_file_reader_t;
static bool iron_file_reader_open(iron_file_reader_t *r, const char *path, int type) {
	r->f = fopen(path, "rb");
	if (r->f == NULL) {
		return false;
	}
	fseek(r->f, 0, SEEK_END);
	r->size = ftell(r->f);
	return true;
}
static size_t iron_file_reader_size(iron_file_reader_t *r) {
	return r->size;
}
static bool iron_file_reader_seek(iron_file_reader_t *r, size_t pos) {
	return fseek(r->f, pos, SEEK_SET) == 0;
}
static size_t iron_file_reader_read(iron_file_reader_t *r, void *dst, size_t n) {
	return fread(dst, 1, n, r->f);
}
static bool iron_file_reader_close(iron_file_reader_t *r) {
	fclose(r->f);
	return true;
}

#include "../../../base/sources/backends/metal_msl_pack.h"

static double now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: metallib_test <pack> <dump>/<platform> [--load] [--material <id>]\n");
		return 1;
	}
	metal_msl_pack.path = argv[1];
	bool load           = false;
	int  material       = 0; // --material: its sources in the order the dump lists them (<dump>/materials.tsv), as asked for
	for (int a = 3; a < argc; ++a) {
		load |= strcmp(argv[a], "--load") == 0;
		if (strcmp(argv[a], "--material") == 0 && a + 1 < argc) {
			material = atoi(argv[++a]);
		}
	}
	NSMutableArray<NSString *> *names = [NSMutableArray new];
	if (material != 0) {
		NSString *dir      = [@(argv[2]) stringByDeletingLastPathComponent];
		NSString *platform = [@(argv[2]) lastPathComponent];
		NSString *tsv      = [NSString stringWithContentsOfFile:[dir stringByAppendingPathComponent:@"materials.tsv"] encoding:NSUTF8StringEncoding error:nil];
		for (NSString *line in [tsv componentsSeparatedByString:@"\n"]) {
			NSArray<NSString *> *f = [line componentsSeparatedByString:@"\t"];
			if (f.count >= 3 && f[0].intValue == material && [f[1] isEqualToString:platform]) {
				[names addObject:[f[2] stringByAppendingString:@".metal"]];
			}
		}
	}
	else {
		for (NSString *f in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:@(argv[2]) error:nil]) {
			if ([f hasSuffix:@".metal"]) {
				[names addObject:f];
			}
		}
	}
	id<MTLDevice> dev  = MTLCreateSystemDefaultDevice();
	int           hits = 0, misses = 0, bad = 0, loaded = 0, timed = 0;
	double        t_find = 0, t_data = 0, t_source = 0;
	for (NSString *file in names) {
		const char *fname = file.UTF8String;
		char        path[2048];
		snprintf(path, sizeof(path), "%s/%s", argv[2], fname);
		NSData *src = [NSData dataWithContentsOfFile:@(path)];
		double  t0  = now_ms();
		dispatch_data_t lib = metal_msl_pack_find(src.bytes, src.length);
		t_find += now_ms() - t0;
		if (lib == nil) {
			misses++;
			continue;
		}
		hits++;
		// The file name is the key the generator computed, the reader's must be the same
		uint64_t h = metal_msl_hash(src.bytes, src.length).hash;
		char     want[32];
		snprintf(want, sizeof(want), "%016llx.metal", (unsigned long long)h);
		snprintf(path, sizeof(path), "%s_lib/%016llx.metallib", argv[2], (unsigned long long)h);
		NSData *ref = [NSData dataWithContentsOfFile:@(path)];
		if (strcmp(want, fname) != 0 || (ref != nil && ![(NSData *)lib isEqualToData:ref])) { // No ref: not compiled here
			fprintf(stderr, "mismatch %s\n", fname);
			bad++;
			continue;
		}
		if (!load) {
			continue;
		}
		NSString *name  = [[NSString alloc] initWithBytes:(const char *)src.bytes + 3 length:4 encoding:NSUTF8StringEncoding];
		NSError  *error = nil;
		t0              = now_ms();
		id<MTLLibrary>  library = [dev newLibraryWithData:lib error:&error];
		id<MTLFunction> fn      = [library newFunctionWithName:name];
		t_data += now_ms() - t0;
		if (library == nil || fn == nil || ![library.functionNames isEqualToArray:@[ name ]]) {
			fprintf(stderr, "load failed %s: %s\n", fname, error.localizedDescription.UTF8String);
			bad++;
			continue;
		}
		loaded++;
		if (timed < 20) {
			NSString *s = [[NSString alloc] initWithData:src encoding:NSUTF8StringEncoding];
			t0          = now_ms();
			id<MTLLibrary> compiled = [dev newLibraryWithSource:s options:nil error:&error];
			t_source += now_ms() - t0;
			timed += compiled != nil;
		}
	}
	printf("%d hits, %d misses, %d bad, %d loaded with their function; lookups %.2f ms total, %u blocks decompressed\n", hits, misses, bad, loaded, t_find,
	       metal_msl_pack.decompressed);
	if (loaded > 0 && timed > 0) {
		printf("newLibraryWithData + function: %.2f ms each; newLibraryWithSource: %.1f ms each (%d timed)\n", t_data / loaded, t_source / timed, timed);
	}
	return bad != 0;
}
