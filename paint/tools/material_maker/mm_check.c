// Material Maker generator checker (macOS, not shipped): loads every material of library.pack, plans its passes and
// compiles each to MSL the way the app does (iron_glsl). Usage: mm_check <assets/material_maker> <out dir> [id ...]
// Failing passes are written to <out>/<id>_<pass>.glsl, a summary to stdout.
// Env MM_DUMP_MSL=<dir>: writes the buffer passes' Metal sources instead (dump_msl), for build_metallib.sh.

#include "../../sources/material_maker/mm.h"
#include "../../../base/sources/iron_glsl.h"
#include "../../../base/sources/backends/metal_msl_hash.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define STB_IMAGE_IMPLEMENTATION
#include "../../../base/sources/libs/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../base/sources/libs/stb_image_write.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../../base/sources/libs/stb_truetype.h"

// Image node files, relative to the assets dir (mm_set_file_reader)
static const char *assets_dir;
static uint8_t    *read_file(const char *path, size_t *size);
static uint8_t    *read_asset(const char *path, size_t *size) {
	char full[1024];
	snprintf(full, sizeof(full), "%s/%s", path[0] == '/' ? "" : assets_dir, path);
	return read_file(full, size);
}

// Render mode (MM_REFS=<Material Maker export dir>): renders the maps at 512 (mm_render.m), writes them to the out dir
// and compares them with Material Maker's (<id>_albedo.png, ...) in <out>/renders.tsv: mean absolute difference
// (0-255) and the share of pixels off by more than 24.
bool            mm_render(mm_plan_t *plan, float **maps, int *failed);
static uint8_t *read_file(const char *path, size_t *size);

static const char *map_files[MM_MAP_COUNT] = {"albedo", "orm", "emission", "normal", "heightmap", "sss"};

static void render_compare(mm_plan_t *plan, int id, const char *refs, const char *out, FILE *log) {
	float *maps[MM_MAP_COUNT] = {0};
	for (int m = 0; m < MM_MAP_COUNT; ++m) {
		if (plan->map_pass[m] >= 0) {
			int s   = plan->passes[plan->map_pass[m]].size;
			maps[m] = malloc(sizeof(float) * 4 * s * s);
		}
	}
	int failed = 0;
	mm_render(plan, maps, &failed);
	for (int m = 0; m < MM_MAP_COUNT; ++m) {
		if (maps[m] == NULL) {
			continue;
		}
		int      s    = plan->passes[plan->map_pass[m]].size;
		uint8_t *ours = malloc(4 * s * s);
		for (int i = 0; i < 4 * s * s; ++i) {
			float v = maps[m][i];
			v       = v != v ? 0.0f : v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
			ours[i] = (uint8_t)(v * 255.0f + 0.5f);
		}
		char path[1024];
		snprintf(path, sizeof(path), "%s/%d_%s.png", out, id, map_files[m]);
		stbi_write_png(path, s, s, 4, ours, s * 4);
		snprintf(path, sizeof(path), "%s/%d_%s.png", refs, id, map_files[m]);
		int      w = 0, h = 0, c;
		size_t   png_size;
		uint8_t *png = read_file(path, &png_size);
		uint8_t *ref = png != NULL ? stbi_load_from_memory(png, (int)png_size, &w, &h, &c, 4) : NULL;
		free(png);
		if (ref != NULL && w == s && h == s) {
			double sum = 0, ch[3] = {0}, g_inv = 0;
			int    off = 0;
			for (int i = 0; i < s * s; ++i) {
				int d = 0;
				for (int k = 0; k < 3; ++k) {
					int e = abs((int)ours[i * 4 + k] - (int)ref[i * 4 + k]);
					sum += e;
					ch[k] += e;
					d = e > d ? e : d;
				}
				g_inv += abs((int)ours[i * 4 + 1] - (255 - (int)ref[i * 4 + 1]));
				off += d > 24;
			}
			double n = (double)s * s;
			// Mean abs diff of 8x8 block averages: tolerant to pixel-level noise, catches systematic differences
			double block = 0;
			int    bs = 8, nb = s / bs;
			for (int by = 0; by < nb; ++by) {
				for (int bx = 0; bx < nb; ++bx) {
					for (int k = 0; k < 3; ++k) {
						double a = 0, b = 0;
						for (int y = 0; y < bs; ++y) {
							for (int x = 0; x < bs; ++x) {
								int i = ((by * bs + y) * s + bx * bs + x) * 4 + k;
								a += ours[i];
								b += ref[i];
							}
						}
						block += fabs(a - b) / (bs * bs);
					}
				}
			}
			block /= 3.0 * nb * nb;
			// Columns: id, map, connected, mean abs diff, share off by >24, failed passes, per channel R G B, G vs inverted
			// G, 8x8 block mean diff
			fprintf(log, "%d\t%s\t%d\t%.2f\t%.4f\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.2f\n", id, map_files[m], plan->map_connected[m], sum / (3.0 * n), off / n,
			        failed, ch[0] / n, ch[1] / n, ch[2] / n, g_inv / n, block);
		}
		else if (plan->map_connected[m]) {
			fprintf(log, "%d\t%s\t%d\tnoref\t\t%d\n", id, map_files[m], plan->map_connected[m], failed);
		}
		stbi_image_free(ref);
		free(ours);
		free(maps[m]);
	}
	fflush(log);
}

int  gpu_shader_failure_count = 0;
void iron_log(const char *format, ...) {
	(void)format;
}

static uint8_t *read_file(const char *path, size_t *size) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *d = malloc(*size + 1);
	fread(d, 1, *size, f);
	d[*size] = 0;
	fclose(f);
	return d;
}

static char *first_error(const char *e) {
	static char line[512];
	const char *s = strstr(e, "ERROR: ");
	if (s == NULL) {
		s = e;
	}
	size_t n = strcspn(s, "\n");
	if (n > sizeof(line) - 1) {
		n = sizeof(line) - 1;
	}
	memcpy(line, s, n);
	line[n] = 0;
	return line;
}

// Precompiled Metal libraries (MM_DUMP_MSL=<dir>, see build_metallib.py): every pass's stage blobs exactly as the app
// hands them to metal_library (gpu_create_shaders_from_glsl: "//>vert\n" or "//>frag\n" then the MSL, no trailing NUL),
// written once per key to <dir>/<platform>/<hash>.metal, the key the app looks them up by (metal_msl_hash.h: generator
// ids renumbered, the app's graph numbers them otherwise). The GLSL goes to <dir>/glsl/<hash>.glsl, the hash FNV-1a 64
// of its bytes, to compare with the app's (IRON_GLSL_DUMP). <dir>/materials.tsv lists each material's blobs.
static uint64_t fnv1a64(const char *s, size_t n) {
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 0; i < n; ++i) {
		h ^= (uint8_t)s[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

static int   dump_new = 0, dump_seen = 0;
static int   dump_id; // The material being dumped
static FILE *dump_manifest = NULL; // <dir>/materials.tsv: material id, glsl/ios/macos, hash, key length (packs group by material)

static void dump_file(const char *dir, const char *sub, const char *ext, const char *head, const char *text) {
	size_t   hn = strlen(head), tn = strlen(text);
	char    *b  = malloc(hn + tn + 1);
	memcpy(b, head, hn);
	memcpy(b + hn, text, tn + 1);
	char            path[1024];
	metal_msl_key_t key = {fnv1a64(b, hn + tn), hn + tn};
	if (strcmp(ext, "metal") == 0) {
		key = metal_msl_hash(b, hn + tn);
	}
	snprintf(path, sizeof(path), "%s/%s/%016llx.%s", dir, sub, (unsigned long long)key.hash, ext);
	if (dump_manifest != NULL) {
		fprintf(dump_manifest, "%d\t%s\t%016llx\t%zu\n", dump_id, sub, (unsigned long long)key.hash, key.length);
	}
	dump_seen++;
	FILE *f = fopen(path, "rb");
	if (f != NULL) {
		fclose(f);
	}
	else if ((f = fopen(path, "wb")) != NULL) {
		fwrite(b, 1, hn + tn, f);
		fclose(f);
		dump_new++;
	}
	free(b);
}

static void dump_msl(const char *glsl) {
	const char *dir = getenv("MM_DUMP_MSL");
	dump_file(dir, "glsl", "glsl", "", glsl);
	for (int ios = 1; ios >= 0; --ios) {
		for (int stage = 0; stage < 2; ++stage) {
			char *msl = iron_glsl_to_msl(glsl, (iron_glsl_stage_t)stage, NULL, ios, NULL);
			if (msl != NULL) {
				dump_file(dir, ios ? "ios" : "macos", "metal", stage == 0 ? "//>vert\n" : "//>frag\n", msl);
			}
		}
	}
}

static bool compile(const char *glsl, const char *out, int id, int pass, const char *what, FILE *log) {
	if (getenv("MM_DUMP_MSL") != NULL) {
		dump_msl(glsl);
		return true;
	}
	if (getenv("MM_NO_COMPILE") != NULL) {
		return true;
	}
	for (int stage = 0; stage < 2; ++stage) {
		char *error = NULL;
		char *msl   = iron_glsl_to_msl(glsl, (iron_glsl_stage_t)stage, NULL, false, &error);
		if (msl == NULL) {
			char path[1024];
			snprintf(path, sizeof(path), "%s/%d_%d%s.glsl", out, id, pass, what);
			FILE *f = fopen(path, "wb");
			if (f != NULL) {
				fputs(glsl, f);
				fclose(f);
			}
			fprintf(log, "%d\tpass %d%s %s\t%s\n", id, pass, what, stage == 0 ? "vert" : "frag", error != NULL ? first_error(error) : "?");
			return false;
		}
	}
	return true;
}

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: mm_check <assets/material_maker> <out dir> [id ...]\n");
		return 1;
	}
	char   path[1024];
	size_t nsize, lsize;
	snprintf(path, sizeof(path), "%s/nodes.pack", argv[1]);
	uint8_t *nodes = read_file(path, &nsize);
	snprintf(path, sizeof(path), "%s/library.pack", argv[1]);
	uint8_t *lib = read_file(path, &lsize);
	if (nodes == NULL || lib == NULL) {
		fprintf(stderr, "Missing packs in %s\n", argv[1]);
		return 1;
	}
	mm_set_nodes(mm_pack_open(nodes, nsize));
	// Text and image nodes: the shipped font (MM_FALLBACK_FONT: a font for the characters it lacks) and files
	assets_dir = argv[1];
	size_t fsize;
	snprintf(path, sizeof(path), "%s/OpenSans-SemiBold.ttf", argv[1]);
	uint8_t *font = read_file(path, &fsize);
	if (font != NULL) {
		mm_add_font(font, fsize);
	}
	uint8_t *fallback = getenv("MM_FALLBACK_FONT") != NULL ? read_file(getenv("MM_FALLBACK_FONT"), &fsize) : NULL;
	if (fallback != NULL) {
		mm_add_font(fallback, fsize);
	}
	mm_set_file_reader(read_asset);
	mm_pack_t *library = mm_pack_open(lib, lsize);

	if (getenv("MM_DUMP_MSL") != NULL) {
		const char *subs[] = {"", "/glsl", "/ios", "/macos"};
		for (int i = 0; i < 4; ++i) {
			snprintf(path, sizeof(path), "%s%s", getenv("MM_DUMP_MSL"), subs[i]);
			mkdir(path, 0755);
		}
		snprintf(path, sizeof(path), "%s/materials.tsv", getenv("MM_DUMP_MSL"));
		dump_manifest = fopen(path, "wb"); // The sources from older runs stay (compiled once), the manifest says what is packed
	}
	snprintf(path, sizeof(path), "%s/failures.tsv", argv[2]);
	FILE *log = fopen(path, "wb");
	if (log == NULL) {
		fprintf(stderr, "Can not write %s\n", path);
		return 1;
	}
	const char *refs    = getenv("MM_REFS");
	FILE       *renders = NULL;
	if (refs != NULL) {
		snprintf(path, sizeof(path), "%s/renders.tsv", argv[2]);
		renders = fopen(path, "wb");
	}
	int materials = 0, ok = 0, plan_errors = 0, passes = 0, failed_passes = 0;
	int n = mm_pack_count(library);
	for (int i = 0; i < n; ++i) {
		int id = (int)mm_json_num(mm_json_get(mm_pack_entry(library, i), "id"), 0);
		if (argc > 3) {
			bool want = false;
			for (int a = 3; a < argc; ++a) {
				if (argv[a][0] == '@') { // A file of ids, one per line
					static char  *list = NULL;
					static size_t list_size;
					if (list == NULL) {
						list = (char *)read_file(argv[a] + 1, &list_size);
					}
					for (char *s = list; s != NULL && *s && !want; s = strchr(s, '\n') ? strchr(s, '\n') + 1 : NULL) {
						want |= atoi(s) == id;
					}
				}
				else {
					want |= atoi(argv[a]) == id;
				}
			}
			if (!want) {
				continue;
			}
		}
		size_t len;
		char  *ptex = mm_pack_read(library, i, &len);
		materials++;
		dump_id = id;
		if (getenv("MM_VERBOSE") != NULL) {
			fprintf(stderr, "material %d\n", id);
		}
		mm_arena_begin();
		mm_material_t *m    = mm_load(ptex, len);
		mm_plan_t     *plan = mm_plan(m, refs != NULL ? 9 : 0);
		bool           good = plan->error == NULL;
		if (refs != NULL) {
			render_compare(plan, id, refs, argv[2], renders);
			mm_arena_end();
			free(ptex);
			continue;
		}
		if (plan->error != NULL) {
			fprintf(log, "%d\tplan\t%s\n", id, plan->error);
			plan_errors++;
		}
		for (int p = 0; p < plan->count; ++p) {
			bool map = false; // The maps are passes of the checker only: the app emits them into its material shader
			for (int k = 0; k < MM_MAP_COUNT; ++k) {
				map |= plan->map_pass[k] == p;
			}
			if (map && getenv("MM_DUMP_MSL") != NULL) {
				continue;
			}
			passes++;
			bool pass_ok = plan->passes[p].glsl == NULL || compile(plan->passes[p].glsl, argv[2], id, p, "", log); // NULL: CPU texture
			if (pass_ok && plan->passes[p].iterate) {
				pass_ok = compile(plan->passes[p].glsl_loop, argv[2], id, p, "_loop", log);
			}
			if (!pass_ok) {
				failed_passes++;
				good = false;
			}
		}
		ok += good;
		mm_arena_end();
		free(ptex);
		fflush(log);
		if (materials % 50 == 0) {
			fprintf(stderr, "%d materials, %d ok\n", materials, ok);
		}
	}
	// .ptex files given as arguments (experiments): named by their numeric basename, e.g. 90001.ptex
	for (int a = 3; a < argc; ++a) {
		size_t l = strlen(argv[a]);
		if (l < 5 || strcmp(argv[a] + l - 5, ".ptex") != 0) {
			continue;
		}
		size_t len;
		char  *ptex = (char *)read_file(argv[a], &len);
		if (ptex == NULL) {
			fprintf(stderr, "Can not read %s\n", argv[a]);
			continue;
		}
		const char *base = strrchr(argv[a], '/');
		int         id   = atoi(base != NULL ? base + 1 : argv[a]);
		materials++;
		mm_arena_begin();
		mm_material_t *m    = mm_load(ptex, len);
		mm_plan_t     *plan = mm_plan(m, refs != NULL ? 9 : 0);
		if (plan->error != NULL) {
			fprintf(log, "%d\tplan\t%s\n", id, plan->error);
			plan_errors++;
		}
		if (getenv("MM_DUMP") != NULL) { // Every pass's GLSL next to the results
			for (int p = 0; p < plan->count; ++p) {
				if (plan->passes[p].glsl == NULL) {
					continue;
				}
				char dump[1024];
				snprintf(dump, sizeof(dump), "%s/%d_pass%d.glsl", argv[2], id, p);
				FILE *f = fopen(dump, "wb");
				if (f != NULL) {
					fputs(plan->passes[p].glsl, f);
					fclose(f);
				}
			}
		}
		if (refs != NULL) {
			render_compare(plan, id, refs, argv[2], renders);
		}
		mm_arena_end();
		free(ptex);
	}
	// .glsl files given as arguments with MM_DUMP_MSL: the app's buffer passes (IRON_GLSL_DUMP, those declaring the
	// constants vec4 mm), to see what of them a pack has (metallib_test.m)
	for (int a = 3; a < argc && dump_manifest != NULL; ++a) {
		size_t l = strlen(argv[a]);
		if (l < 5 || strcmp(argv[a] + l - 5, ".glsl") != 0) {
			continue;
		}
		size_t len;
		char  *glsl = (char *)read_file(argv[a], &len);
		if (glsl != NULL && strstr(glsl, "vec4 mm;") != NULL) {
			dump_id = -1;
			dump_msl(glsl);
		}
		free(glsl);
	}
	fclose(log);
	printf("%d materials: %d ok, %d plan errors, %d/%d passes failed to compile\n", materials, ok, plan_errors, failed_passes, passes);
	if (dump_manifest != NULL) {
		fclose(dump_manifest);
		printf("MM_DUMP_MSL: %d blobs, %d new\n", dump_seen, dump_new);
	}
	return 0;
}
