#include "global.h"

// Material Maker buffers (nodes_material/mm_buffers.c): library materials planned with mm_plan and rendered by the app's
// GPU layer match the checker's renders (paint/tools/material_maker, mm_render.m: Material Maker's exports to under
// 1/255): an iterate buffer of 1000 iterations with buffers inside its loop (846, a Turing pattern), buffers and normal
// maps and a text node (2, 3, 88, 113). Rendered passes resolve through their texture links, unchanged passes come
// from the cache, unused ones stay while the cache fits its budget and the least recently used are evicted past it.
// The checker's renders are the Mac's: texels match them within 3/255 there. Other GPUs (the iPad's M1) evaluate the sin
// based hashes of Material Maker's noises a little differently, fast math (the safe math mode matches less): noises and
// random placements shift, and the texels are only logged. Every platform checks the maps' means and deviations over
// a grid against the checker's.

int fails  = 0;
int checks = 0;
int step   = 0;

int ids[5] = {846, 2, 3, 88, 113};

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mm_buffers] %s %s", ok ? "ok  " : "FAIL", name);
}

int has(char *s, char *part) {
	return s != NULL && string_index_of(s, part) >= 0;
}

// Expected texels: id, map, x, y, r, g, b, a (0-255, the checker's PNGs at 512)
int texel_count = 150;

int byte_of(float v) {
	if (v != v || v < 0.0) {
		return 0;
	}
	if (v > 1.0) {
		return 255;
	}
	return (int)(v * 255.0 + 0.5);
}

// The checker's maps (512) over a 32x32 grid (8 + 16 i): id, map, mean r g b a, standard deviation r g b a (0-255)
int stat_count = 30;

void compare_stats(int id) {
	int t[300] = {
	846, 0, 97, 97, 97, 255, 117, 117, 117, 0,
	846, 1, 255, 255, 0, 255, 0, 0, 0, 0,
	846, 2, 0, 0, 0, 255, 0, 0, 0, 0,
	846, 3, 128, 128, 128, 255, 0, 0, 0, 0,
	846, 4, 255, 255, 255, 255, 0, 0, 0, 0,
	846, 5, 0, 0, 0, 255, 0, 0, 0, 0,
	2, 0, 254, 177, 36, 255, 1, 73, 88, 0,
	2, 1, 62, 160, 0, 255, 43, 73, 0, 0,
	2, 2, 0, 0, 0, 255, 0, 0, 0, 0,
	2, 3, 131, 120, 220, 255, 57, 59, 31, 0,
	2, 4, 23, 23, 23, 255, 18, 18, 18, 0,
	2, 5, 0, 0, 0, 255, 0, 0, 0, 0,
	3, 0, 158, 68, 63, 255, 120, 79, 50, 0,
	3, 1, 123, 82, 39, 255, 59, 86, 45, 0,
	3, 2, 0, 0, 0, 255, 0, 0, 0, 0,
	3, 3, 126, 129, 102, 255, 34, 34, 119, 0,
	3, 4, 138, 138, 138, 255, 112, 112, 112, 0,
	3, 5, 0, 0, 0, 255, 0, 0, 0, 0,
	88, 0, 22, 8, 1, 133, 26, 10, 2, 127,
	88, 1, 221, 110, 0, 255, 52, 119, 0, 0,
	88, 2, 0, 0, 0, 255, 0, 0, 0, 0,
	88, 3, 127, 138, 117, 255, 57, 33, 127, 0,
	88, 4, 31, 31, 31, 255, 66, 66, 66, 0,
	88, 5, 0, 0, 0, 255, 0, 0, 0, 0,
	113, 0, 125, 115, 78, 255, 37, 27, 19, 0,
	113, 1, 224, 80, 255, 255, 45, 35, 0, 0,
	113, 2, 0, 0, 0, 255, 0, 0, 0, 0,
	113, 3, 129, 128, 245, 255, 31, 31, 22, 0,
	113, 4, 66, 66, 66, 255, 36, 36, 36, 0,
	113, 5, 0, 0, 0, 255, 0, 0, 0, 0,
	};
	int   worst_mean = 0;
	int   worst_dev  = 0;
	int   n          = 0;
	float sum[4];
	float sq[4];
	for (int i = 0; i < stat_count; ++i) {
		int k = i * 10;
		if (t[k] != id) {
			continue;
		}
		int map = t[k + 1];
		n       = n + 1;
		if (mm_buffers_test_map_size(map) != 512) {
			worst_mean = 255;
			continue;
		}
		for (int c = 0; c < 4; ++c) {
			sum[c] = 0.0;
			sq[c]  = 0.0;
		}
		for (int gy = 0; gy < 32; ++gy) {
			for (int gx = 0; gx < 32; ++gx) {
				for (int c = 0; c < 4; ++c) {
					float v = byte_of(mm_buffers_test_texel(map, 8 + 16 * gx, 8 + 16 * gy, c));
					sum[c]  = sum[c] + v;
					sq[c]   = sq[c] + v * v;
				}
			}
		}
		for (int c = 0; c < 4; ++c) {
			float mean = sum[c] / 1024.0;
			float var  = sq[c] / 1024.0 - mean * mean;
			float dev  = var > 0.0 ? sqrt(var) : 0.0;
			int   dm   = (int)(mean - t[k + 2 + c] + 0.5);
			int   dd   = (int)(dev - t[k + 6 + c] + 0.5);
			dm         = dm < 0 ? -dm : dm;
			dd         = dd < 0 ? -dd : dd;
			if (dm > worst_mean || dd > worst_dev) {
				printf("[mm_buffers] %d map %d channel %d: mean %.1f (checker %d), deviation %.1f (checker %d)", id, map, c, mean, t[k + 2 + c], dev, t[k + 6 + c]);
			}
			worst_mean = dm > worst_mean ? dm : worst_mean;
			worst_dev  = dd > worst_dev ? dd : worst_dev;
		}
	}
	check(n > 0 && worst_mean <= 8 && worst_dev <= 10,
	      string("%d matches the checker's maps on average (means within 8/255: %d, deviations within 10/255: %d)", id, worst_mean, worst_dev));
}

int compare_texels(int id) {
	int t[1440] = {
846, 0, 37, 91, 0, 0, 0, 255,
	846, 0, 256, 256, 149, 149, 149, 255,
	846, 0, 400, 13, 0, 0, 0, 255,
	846, 0, 123, 456, 0, 0, 0, 255,
	846, 0, 500, 300, 0, 0, 0, 255,
	846, 1, 37, 91, 255, 255, 0, 255,
	846, 1, 256, 256, 255, 255, 0, 255,
	846, 1, 400, 13, 255, 255, 0, 255,
	846, 1, 123, 456, 255, 255, 0, 255,
	846, 1, 500, 300, 255, 255, 0, 255,
	846, 2, 37, 91, 0, 0, 0, 255,
	846, 2, 256, 256, 0, 0, 0, 255,
	846, 2, 400, 13, 0, 0, 0, 255,
	846, 2, 123, 456, 0, 0, 0, 255,
	846, 2, 500, 300, 0, 0, 0, 255,
	846, 3, 37, 91, 128, 128, 128, 255,
	846, 3, 256, 256, 128, 128, 128, 255,
	846, 3, 400, 13, 128, 128, 128, 255,
	846, 3, 123, 456, 128, 128, 128, 255,
	846, 3, 500, 300, 128, 128, 128, 255,
	846, 4, 37, 91, 255, 255, 255, 255,
	846, 4, 256, 256, 255, 255, 255, 255,
	846, 4, 400, 13, 255, 255, 255, 255,
	846, 4, 123, 456, 255, 255, 255, 255,
	846, 4, 500, 300, 255, 255, 255, 255,
	846, 5, 37, 91, 0, 0, 0, 255,
	846, 5, 256, 256, 0, 0, 0, 255,
	846, 5, 400, 13, 0, 0, 0, 255,
	846, 5, 123, 456, 0, 0, 0, 255,
	846, 5, 500, 300, 0, 0, 0, 255,
	2, 0, 37, 91, 253, 255, 0, 255,
	2, 0, 256, 256, 255, 108, 0, 255,
	2, 0, 400, 13, 253, 255, 0, 255,
	2, 0, 123, 456, 255, 108, 0, 255,
	2, 0, 500, 300, 253, 255, 0, 255,
	2, 1, 37, 91, 118, 65, 0, 255,
	2, 1, 256, 256, 39, 199, 0, 255,
	2, 1, 400, 13, 37, 202, 0, 255,
	2, 1, 123, 456, 35, 206, 0, 255,
	2, 1, 500, 300, 102, 92, 0, 255,
	2, 2, 37, 91, 0, 0, 0, 255,
	2, 2, 256, 256, 0, 0, 0, 255,
	2, 2, 400, 13, 0, 0, 0, 255,
	2, 2, 123, 456, 0, 0, 0, 255,
	2, 2, 500, 300, 0, 0, 0, 255,
	2, 3, 37, 91, 91, 215, 213, 255,
	2, 3, 256, 256, 148, 70, 240, 255,
	2, 3, 400, 13, 228, 144, 204, 255,
	2, 3, 123, 456, 119, 94, 250, 255,
	2, 3, 500, 300, 216, 111, 217, 255,
	2, 4, 37, 91, 46, 46, 46, 255,
	2, 4, 256, 256, 14, 14, 14, 255,
	2, 4, 400, 13, 13, 13, 13, 255,
	2, 4, 123, 456, 12, 12, 12, 255,
	2, 4, 500, 300, 39, 39, 39, 255,
	2, 5, 37, 91, 0, 0, 0, 255,
	2, 5, 256, 256, 0, 0, 0, 255,
	2, 5, 400, 13, 0, 0, 0, 255,
	2, 5, 123, 456, 0, 0, 0, 255,
	2, 5, 500, 300, 0, 0, 0, 255,
	3, 0, 37, 91, 0, 0, 0, 255,
	3, 0, 256, 256, 255, 91, 135, 255,
	3, 0, 400, 13, 0, 0, 0, 255,
	3, 0, 123, 456, 0, 0, 0, 255,
	3, 0, 500, 300, 0, 0, 0, 255,
	3, 1, 37, 91, 170, 0, 0, 255,
	3, 1, 256, 256, 70, 159, 91, 255,
	3, 1, 400, 13, 170, 0, 0, 255,
	3, 1, 123, 456, 170, 0, 0, 255,
	3, 1, 500, 300, 170, 0, 0, 255,
	3, 2, 37, 91, 0, 0, 0, 255,
	3, 2, 256, 256, 0, 0, 0, 255,
	3, 2, 400, 13, 0, 0, 0, 255,
	3, 2, 123, 456, 0, 0, 0, 255,
	3, 2, 500, 300, 0, 0, 0, 255,
	3, 3, 37, 91, 128, 128, 0, 255,
	3, 3, 256, 256, 121, 148, 237, 255,
	3, 3, 400, 13, 128, 128, 0, 255,
	3, 3, 123, 456, 128, 128, 0, 255,
	3, 3, 500, 300, 128, 128, 0, 255,
	3, 4, 37, 91, 0, 0, 0, 255,
	3, 4, 256, 256, 255, 255, 255, 255,
	3, 4, 400, 13, 0, 0, 0, 255,
	3, 4, 123, 456, 0, 0, 0, 255,
	3, 4, 500, 300, 0, 0, 0, 255,
	3, 5, 37, 91, 0, 0, 0, 255,
	3, 5, 256, 256, 0, 0, 0, 255,
	3, 5, 400, 13, 0, 0, 0, 255,
	3, 5, 123, 456, 0, 0, 0, 255,
	3, 5, 500, 300, 0, 0, 0, 255,
	88, 0, 37, 91, 0, 0, 0, 0,
	88, 0, 256, 256, 0, 0, 0, 0,
	88, 0, 400, 13, 67, 23, 4, 255,
	88, 0, 123, 456, 46, 19, 3, 255,
	88, 0, 500, 300, 32, 12, 2, 255,
	88, 1, 37, 91, 151, 0, 0, 255,
	88, 1, 256, 256, 236, 0, 0, 255,
	88, 1, 400, 13, 239, 242, 0, 255,
	88, 1, 123, 456, 255, 230, 0, 255,
	88, 1, 500, 300, 255, 235, 0, 255,
	88, 2, 37, 91, 0, 0, 0, 255,
	88, 2, 256, 256, 0, 0, 0, 255,
	88, 2, 400, 13, 0, 0, 0, 255,
	88, 2, 123, 456, 0, 0, 0, 255,
	88, 2, 500, 300, 0, 0, 0, 255,
	88, 3, 37, 91, 128, 128, 0, 255,
	88, 3, 256, 256, 128, 128, 0, 255,
	88, 3, 400, 13, 146, 241, 254, 255,
	88, 3, 123, 456, 200, 116, 255, 255,
	88, 3, 500, 300, 254, 129, 255, 255,
	88, 4, 37, 91, 0, 0, 0, 255,
	88, 4, 256, 256, 0, 0, 0, 255,
	88, 4, 400, 13, 134, 134, 134, 255,
	88, 4, 123, 456, 19, 19, 19, 255,
	88, 4, 500, 300, 188, 188, 188, 255,
	88, 5, 37, 91, 0, 0, 0, 255,
	88, 5, 256, 256, 0, 0, 0, 255,
	88, 5, 400, 13, 0, 0, 0, 255,
	88, 5, 123, 456, 0, 0, 0, 255,
	88, 5, 500, 300, 0, 0, 0, 255,
	113, 0, 37, 91, 85, 86, 61, 255,
	113, 0, 256, 256, 157, 135, 93, 255,
	113, 0, 400, 13, 168, 146, 100, 255,
	113, 0, 123, 456, 198, 181, 121, 255,
	113, 0, 500, 300, 174, 152, 104, 255,
	113, 1, 37, 91, 207, 122, 255, 255,
	113, 1, 256, 256, 189, 53, 255, 255,
	113, 1, 400, 13, 255, 54, 255, 255,
	113, 1, 123, 456, 255, 36, 255, 255,
	113, 1, 500, 300, 255, 38, 255, 255,
	113, 2, 37, 91, 0, 0, 0, 255,
	113, 2, 256, 256, 0, 0, 0, 255,
	113, 2, 400, 13, 0, 0, 0, 255,
	113, 2, 123, 456, 0, 0, 0, 255,
	113, 2, 500, 300, 0, 0, 0, 255,
	113, 3, 37, 91, 120, 135, 255, 255,
	113, 3, 256, 256, 143, 133, 254, 255,
	113, 3, 400, 13, 203, 166, 223, 255,
	113, 3, 123, 456, 96, 106, 249, 255,
	113, 3, 500, 300, 101, 144, 251, 255,
	113, 4, 37, 91, 37, 37, 37, 255,
	113, 4, 256, 256, 61, 61, 61, 255,
	113, 4, 400, 13, 84, 84, 84, 255,
	113, 4, 123, 456, 181, 181, 181, 255,
	113, 4, 500, 300, 102, 102, 102, 255,
	113, 5, 37, 91, 0, 0, 0, 255,
	113, 5, 256, 256, 0, 0, 0, 255,
	113, 5, 400, 13, 0, 0, 0, 255,
	113, 5, 123, 456, 0, 0, 0, 255,
	113, 5, 500, 300, 0, 0, 0, 255,
	};
	int bad = 0;
	int n   = 0;
	for (int i = 0; i < texel_count; ++i) {
		int k = i * 8;
		if (t[k] != id) {
			continue;
		}
		int map = t[k + 1];
		int x   = t[k + 2];
		int y   = t[k + 3];
		n       = n + 1;
		if (mm_buffers_test_map_size(map) != 512) {
			printf("[mm_buffers] %d map %d missing (size %d)", id, map, mm_buffers_test_map_size(map));
			bad = bad + 1;
			continue;
		}
		int worst = 0;
		for (int c = 0; c < 4; ++c) {
			int d = byte_of(mm_buffers_test_texel(map, x, y, c)) - t[k + 4 + c];
			d     = d < 0 ? -d : d;
			worst = d > worst ? d : worst;
		}
		if (worst > 3) {
			printf("[mm_buffers] %d map %d (%d, %d): %d %d %d %d, expected %d %d %d %d", id, map, x, y, byte_of(mm_buffers_test_texel(map, x, y, 0)),
			       byte_of(mm_buffers_test_texel(map, x, y, 1)), byte_of(mm_buffers_test_texel(map, x, y, 2)), byte_of(mm_buffers_test_texel(map, x, y, 3)),
			       t[k + 4], t[k + 5], t[k + 6], t[k + 7]);
			bad = bad + 1;
		}
	}
	if (script_get_platform() == 2) { // macOS, the checker's GPU
		check(n > 0 && bad == 0, string("%d matches the checker (%d of %d texels within 3/255)", id, n - bad, n));
	}
	else {
		check(n > 0, string("%d has the checker's maps (%d of %d texels within 3/255, not checked on this GPU)", id, n - bad, n));
	}
	compare_stats(id);
	return bad;
}

// Renders id, two other materials (its textures are evicted, its recipes kept), then resolves its maps' links
void evict_and_resolve(char *id, int in_pass) {
	char *s = mm_buffers_test_ptex(id, 9);
	check(!has(s, "error") && mm_buffers_test_resident(0) == 1, string("%s rendered (%s)", id, s));
	mm_buffers_test_budget(1); // Past the budget: the other materials' renders evict its textures
	mm_buffers_test_other("2", 9);
	mm_buffers_test_other("3", 9);
	mm_buffers_test_budget(0);
	int evicted = 0;
	for (int m = 0; m < 6; ++m) {
		evicted = evicted + (mm_buffers_test_resident(m) == 0 ? 1 : 0);
	}
	printf("[mm_buffers] %s: %d maps evicted, %d recipes, %d KB of recipes, %d KB of textures", id, evicted, mm_buffers_stat(9), mm_buffers_stat(10), mm_buffers_stat(6));
	check(mm_buffers_test_resident(0) == 0, string("%s: textures evicted, recipes kept", id));
	int before = mm_buffers_stat(8);
	int n      = mm_buffers_test_resolve(in_pass);
	check(n == 6, string("%s: links resolve to textures rendered again%s (%d maps)", id, in_pass ? ", inside a render pass that keeps its content" : "", n));
	check(mm_buffers_stat(8) > before, string("%s: %d textures rendered again", id, mm_buffers_stat(8) - before));
	compare_texels(parse_int(id));
}

void render_material(int id) {
	char *s = mm_buffers_test_ptex(string("%d", id), 9);
	printf("[mm_buffers] %d: %s", id, s);
	check(!has(s, "error"), string("%d plans and renders", id));
	check(has(s, "failed 0 "), string("%d: every pass compiles", id));
	check(has(s, "bad_links 0 ") && !has(s, " links 0 "), string("%d: passes resolve through their texture links", id));
	mm_buffers_test_write(string("%s/mm_buffers_%d", script_save_path(), id));
	compare_texels(id);
}

void next() {
	if (step < 5) {
		render_material(ids[step]);
	}
	else if (step == 5) { // Unchanged (the last one, its text texture too): every pass from the cache
		int entries = mm_buffers_stat(4);
		char *s     = mm_buffers_test_ptex("113", 9);
		printf("[mm_buffers] 113 again: %s", s);
		check(mm_buffers_stat(0) == 0 && mm_buffers_stat(1) > 0 && mm_buffers_stat(3) == 0, "unchanged material: every pass from the cache, nothing drawn");
		check(mm_buffers_stat(4) <= entries, "nothing added to the cache");
		compare_texels(113);
	}
	else if (step == 6) {
		mm_buffers_test_ptex("3", 9);
	}
	else if (step == 7) { // 113 unused for two renders: kept while the cache fits its budget, evicted past it
		mm_buffers_test_ptex("88", 9);
		printf("[mm_buffers] cache: %d textures, %d pipelines, %d KB", mm_buffers_stat(4), mm_buffers_stat(5), mm_buffers_stat(6));
		char *s = mm_buffers_test_ptex("113", 9);
		check(mm_buffers_stat(0) == 0 && mm_buffers_stat(1) > 0, string("entries unused for two renders stay within the budget (113 from the cache: %s)", s));
		check(mm_buffers_stat(6) <= mm_buffers_stat(7), string("cache memory within its budget (%d of %d KB)", mm_buffers_stat(6), mm_buffers_stat(7)));
		mm_buffers_test_budget(1); // Past the budget: only the last render's textures stay
		mm_buffers_test_ptex("3", 9);
		mm_buffers_test_ptex("88", 9);
		s = mm_buffers_test_ptex("113", 9);
		mm_buffers_test_budget(0);
		check(mm_buffers_stat(0) > 0 && mm_buffers_stat(1) == 0, string("past the budget the least recently used are evicted (113 renders again: %s)", s));
		compare_texels(113);
	}
	else if (step == 8) { // Evicted textures render again from their recipes when their links resolve
		evict_and_resolve("113", 0);
	}
	else if (step == 9) { // Resolved inside a render pass (a material draw): the pass is suspended and continues
		evict_and_resolve("113", 1);
	}
	else if (step == 10) { // Loop passes keep their last iteration, have links, render again with their iterate buffer
		char *s = mm_buffers_test_ptex("846", 9);
		check(has(s, " links 13 ") && has(s, "bad_links 0 "), string("846: every pass has a link, loop passes too (%s)", s));
		evict_and_resolve("846", 1);
	}
	else if (step == 11) { // CPU textures (text) come back from their recipes' pixels
		evict_and_resolve("113", 0);
	}
	else if (step == 12) {
		mm_buffers_clear();
		check(mm_buffers_stat(4) == 0 && mm_buffers_stat(5) == 0 && mm_buffers_stat(9) == 0, "clear frees every texture, recipe and pipeline");
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[mm_buffers] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MM_BUFFERS PASS" : "MM_BUFFERS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	printf("[mm_buffers] 32-bit float buffers: %s", mm_buffers_stat(13) == 1 ? "yes" : "no, 16-bit unless read with texelFetch (32-bit float filtering unsupported)");
	script_notify_on_next_frame(next);
}
