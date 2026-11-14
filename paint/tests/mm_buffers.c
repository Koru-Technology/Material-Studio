#include "global.h"

// Material Maker buffers (nodes_material/mm_buffers.c): library materials planned with mm_plan and rendered by the app's
// GPU layer match the checker's renders (paint/tools/material_maker, mm_render.m: Material Maker's exports to under
// 1/255): an iterate buffer of 1000 iterations with buffers inside its loop (846, a Turing pattern), buffers and normal
// maps (2, 3, 88, 113), a text node (1217). Rendered passes resolve through their texture links, unchanged passes come
// from the cache, unused ones stay while the cache fits its budget and the least recently used are evicted past it.

int fails  = 0;
int checks = 0;
int step   = 0;

int ids[6] = {846, 2, 3, 88, 113, 1217};

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
int texel_count = 180;

int byte_of(float v) {
	if (v != v || v < 0.0) {
		return 0;
	}
	if (v > 1.0) {
		return 255;
	}
	return (int)(v * 255.0 + 0.5);
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
	1217, 0, 37, 91, 31, 123, 93, 255,
	1217, 0, 256, 256, 24, 110, 67, 255,
	1217, 0, 400, 13, 43, 134, 104, 255,
	1217, 0, 123, 456, 43, 134, 104, 255,
	1217, 0, 500, 300, 31, 123, 93, 255,
	1217, 1, 37, 91, 255, 255, 0, 255,
	1217, 1, 256, 256, 255, 255, 0, 255,
	1217, 1, 400, 13, 255, 255, 0, 255,
	1217, 1, 123, 456, 255, 255, 0, 255,
	1217, 1, 500, 300, 255, 255, 0, 255,
	1217, 2, 37, 91, 0, 0, 0, 255,
	1217, 2, 256, 256, 0, 0, 0, 255,
	1217, 2, 400, 13, 0, 0, 0, 255,
	1217, 2, 123, 456, 0, 0, 0, 255,
	1217, 2, 500, 300, 0, 0, 0, 255,
	1217, 3, 37, 91, 128, 128, 128, 255,
	1217, 3, 256, 256, 128, 128, 128, 255,
	1217, 3, 400, 13, 128, 128, 128, 255,
	1217, 3, 123, 456, 128, 128, 128, 255,
	1217, 3, 500, 300, 128, 128, 128, 255,
	1217, 4, 37, 91, 255, 255, 255, 255,
	1217, 4, 256, 256, 255, 255, 255, 255,
	1217, 4, 400, 13, 255, 255, 255, 255,
	1217, 4, 123, 456, 255, 255, 255, 255,
	1217, 4, 500, 300, 255, 255, 255, 255,
	1217, 5, 37, 91, 0, 0, 0, 255,
	1217, 5, 256, 256, 0, 0, 0, 255,
	1217, 5, 400, 13, 0, 0, 0, 255,
	1217, 5, 123, 456, 0, 0, 0, 255,
	1217, 5, 500, 300, 0, 0, 0, 255,
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
	check(n > 0 && bad == 0, string("%d matches the checker (%d of %d texels within 3/255)", id, n - bad, n));
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
	if (step < 6) {
		render_material(ids[step]);
	}
	else if (step == 6) { // Unchanged (the last one, its text texture too): every pass from the cache
		int entries = mm_buffers_stat(4);
		char *s     = mm_buffers_test_ptex("1217", 9);
		printf("[mm_buffers] 1217 again: %s", s);
		check(mm_buffers_stat(0) == 0 && mm_buffers_stat(1) > 0 && mm_buffers_stat(3) == 0, "unchanged material: every pass from the cache, nothing drawn");
		check(mm_buffers_stat(4) <= entries, "nothing added to the cache");
		compare_texels(1217);
	}
	else if (step == 7) {
		mm_buffers_test_ptex("3", 9);
	}
	else if (step == 8) { // 1217 unused for two renders: kept while the cache fits its budget, evicted past it
		mm_buffers_test_ptex("88", 9);
		printf("[mm_buffers] cache: %d textures, %d pipelines, %d KB", mm_buffers_stat(4), mm_buffers_stat(5), mm_buffers_stat(6));
		char *s = mm_buffers_test_ptex("1217", 9);
		check(mm_buffers_stat(0) == 0 && mm_buffers_stat(1) > 0, string("entries unused for two renders stay within the budget (1217 from the cache: %s)", s));
		check(mm_buffers_stat(6) <= mm_buffers_stat(7), string("cache memory within its budget (%d of %d KB)", mm_buffers_stat(6), mm_buffers_stat(7)));
		mm_buffers_test_budget(1); // Past the budget: only the last render's textures stay
		mm_buffers_test_ptex("3", 9);
		mm_buffers_test_ptex("88", 9);
		s = mm_buffers_test_ptex("1217", 9);
		mm_buffers_test_budget(0);
		check(mm_buffers_stat(0) > 0 && mm_buffers_stat(1) == 0, string("past the budget the least recently used are evicted (1217 renders again: %s)", s));
		compare_texels(1217);
	}
	else if (step == 9) { // Evicted textures render again from their recipes when their links resolve
		evict_and_resolve("113", 0);
	}
	else if (step == 10) { // Resolved inside a render pass (a material draw): the pass is suspended and continues
		evict_and_resolve("113", 1);
	}
	else if (step == 11) { // Loop passes keep their last iteration, have links, render again with their iterate buffer
		char *s = mm_buffers_test_ptex("846", 9);
		check(has(s, " links 13 ") && has(s, "bad_links 0 "), string("846: every pass has a link, loop passes too (%s)", s));
		evict_and_resolve("846", 1);
	}
	else if (step == 12) { // CPU textures (text) come back from their recipes' pixels
		evict_and_resolve("1217", 0);
	}
	else if (step == 13) {
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
	script_notify_on_next_frame(next);
}
