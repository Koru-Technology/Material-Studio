#include "global.h"

// Material Maker's library in the Library tab (nodes_material/mm_library.c): every material by style and section, searched
// by name, tags, author and style, credited, thumbnails shipped in thumbs.pack and loaded as they are shown (a bounded number held),
// tapping one adds it to the project.

int fails  = 0;
int checks = 0;
int step   = 0;
int wait   = 0;
int before = 0;
int first  = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mm_library] %s %s", ok ? "ok  " : "FAIL", name);
}

int count_matches(char *search) {
	int n = 0;
	for (int i = 0; i < mm_library_count(); ++i) {
		if (mm_library_matches(i, search)) {
			n = n + 1;
		}
	}
	return n;
}

void next() {
	if (wait > 0) {
		wait = wait - 1;
		script_notify_on_next_frame(next);
		return;
	}
	int n = mm_library_count();
	if (step == 0) {
		check(n >= 980, string("the library lists %d materials", n));
		int sections_ok = 1;
		int per[12];
		for (int s = 0; s < 12; ++s) {
			per[s] = 0;
		}
		int styled = 0;
		for (int i = 0; i < n; ++i) {
			int s = mm_library_section(i);
			if (s < 0 || s >= mm_library_section_count()) {
				sections_ok = 0;
			}
			else {
				per[s] = per[s] + 1;
			}
			if (mm_library_stylized(i)) {
				styled = styled + 1;
			}
		}
		int empty = 0;
		for (int s = 0; s < mm_library_section_count(); ++s) {
			if (per[s] == 0) {
				empty = empty + 1;
			}
		}
		check(sections_ok && mm_library_section_count() == 11 && empty == 0, string("every material in one of the 11 sections (%d empty)", empty));
		check(styled > 100 && n - styled > 100, string("stylized and realistic materials (%d stylized of %d)", styled, n));
		int candy = mm_library_find(2);
		check(candy >= 0 && string_equals(mm_library_name(candy), "Candy Corn") && mm_library_stylized(candy), "library id 2 is the stylized Candy Corn");
		int stylized_found = count_matches("stylized");
		check(stylized_found >= styled && stylized_found < n, string("searching stylized finds the stylized materials (%d of %d)", stylized_found, styled));
		char *credit = mm_library_credit(candy);
		check(string_index_of(credit, "Candy") >= 0 && (string_index_of(credit, "CC0") >= 0 || string_index_of(credit, "CC-BY") >= 0 || string_index_of(credit, "CC BY") >= 0),
		      string("credited with author and licence (%s)", credit));
		int all    = count_matches("");
		int bricks = count_matches("brick");
		int by     = count_matches("rodzilla"); // Material Maker's author
		check(all == n && bricks >= 10 && bricks < n && by >= 5, string("search by name, tags and author (%d brick, %d by rodzilla)", bricks, by));
		float ms = mm_library_search_ms("stone");
		check(ms < 2.0, string("searching every material takes %.3f ms", ms));
		int shipped = 0;
		for (int i = 0; i < n; ++i) {
			if (mm_thumbs_has(mm_library_id(i))) {
				shipped = shipped + 1;
			}
		}
		check(shipped == n, string("thumbnails shipped for %d of %d materials", shipped, n));
		for (int i = 0; i < 24; ++i) {
			mm_library_icon(i);
		}
		wait = 12;
	}
	else if (step == 1) {
		int ready = 0;
		for (int i = 0; i < 24; ++i) {
			if (mm_library_icon(i) != NULL) {
				ready = ready + 1;
			}
		}
		check(ready == 24, string("shown thumbnails load from the pack (%d of 24 after 12 frames)", ready));
		gpu_texture_t *t = mm_library_icon(0);
		check(t != NULL && t->width == 128, "128 px thumbnails");
		first = 24;
	}
	else if (step < 80) { // Scrolling through: rows come into view, the ones gone are unloaded past the limit
		for (int i = first; i < first + 16 && i < n; ++i) {
			mm_library_icon(i);
		}
		first = first + 16;
	}
	else if (step == 80) {
		int held = mm_library_icons_loaded();
		check(held > 100 && held <= 300, string("thumbnails held stay bounded (%d after %d shown)", held, first));
		before = script_material_count();
		mm_library_use(mm_library_find(2));
		wait = 3;
	}
	else if (step == 81) {
		slot_material_t *m = script_material_get();
		check(script_material_count() == before + 1 && m != NULL && mm_import_material_node(m) != NULL && string_index_of(m->canvas->name, "Candy") >= 0,
		      "tapping a library material adds it to the project and selects it");
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[mm_library] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MM_LIBRARY PASS" : "MM_LIBRARY FAIL");
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
