#include "global.h"

// Material Maker export cap (nodes_material/mm_buffers.c, mm_buffers_export_begin): during an export the passes a material
// samples render full at the largest cap of 2048, 4096 and 8192 that fits the export's memory budget (and the export's
// size), still at most each pass's own size; afterwards the normal cap is back and textures above it are freed (buffer
// keys do not include the size, they would stay).

int fails  = 0;
int checks = 0;
int step   = 0;
int albedo = 0; // mm_map_t

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mm_export_cap] %s %s", ok ? "ok  " : "FAIL", name);
}

void next() {
	if (step == 0) { // A library material planned at 4096, its passes full at the project's 2048
		mm_buffers_test_res(2048);
		char *s = mm_buffers_test_ptex("113", 12);
		check(string_index_of(s, "error") < 0, string("material rendered (%s)", s));
		check(mm_buffers_test_tex_size(albedo) == 2048, string("normal cap: albedo at 2048 (%d)", mm_buffers_test_tex_size(albedo)));
	}
	else if (step == 1) { // A budget for all of them at 4096: the cap is 8192 (the export's size), passes stop at their own 4096
		int cap = mm_buffers_test_export(8192, 4 * 1024 * 1024);
		check(cap == 8192, string("cap 8192 chosen (%d)", cap));
		check(mm_buffers_test_tex_size(albedo) == 4096, string("albedo full at its own 4096 (%d)", mm_buffers_test_tex_size(albedo)));
		check(mm_buffers_stat(14) == 8192 && mm_buffers_stat(15) > 0, string("cap set, passes pinned (%d)", mm_buffers_stat(15)));
		mm_buffers_test_budget(1); // Eviction pressure: pinned textures stay
		mm_buffers_test_other("2", 9);
		mm_buffers_test_budget(0);
		check(mm_buffers_test_tex_size(albedo) == 4096, string("pinned albedo stays through eviction (%d)", mm_buffers_test_tex_size(albedo)));
	}
	else if (step == 2) { // Afterwards: the normal cap, the 4096 textures freed, rendered again at 2048 on demand
		mm_buffers_export_end();
		check(mm_buffers_stat(14) == 0 && mm_buffers_stat(15) == 0, "cap restored, nothing pinned");
		check(mm_buffers_test_tex_size(albedo) == 0, string("oversized albedo evicted (%d)", mm_buffers_test_tex_size(albedo)));
		mm_buffers_test_resolve(0);
		check(mm_buffers_test_tex_size(albedo) == 2048, string("albedo renders again at 2048 (%d)", mm_buffers_test_tex_size(albedo)));
	}
	else if (step == 3) { // A budget that fits 2048 only, the export at 4096
		int cap = mm_buffers_test_export(4096, 64 * 1024);
		check(cap == 2048 || cap == 0, string("small budget: cap 2048 or the normal one (%d)", cap));
		check(mm_buffers_test_tex_size(albedo) == 2048, string("albedo at 2048 (%d)", mm_buffers_test_tex_size(albedo)));
		mm_buffers_export_end();
		cap = mm_buffers_test_export(4096, 4 * 1024 * 1024);
		check(cap == 4096, string("export at 4096: cap 4096 (%d)", cap));
		mm_buffers_export_end();
		check(mm_buffers_stat(14) == 0 && mm_buffers_test_tex_size(albedo) == 0, "cap restored again, 4096 textures freed");
		mm_buffers_test_res(0);
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[mm_export_cap] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MM_EXPORT_CAP PASS" : "MM_EXPORT_CAP FAIL");
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
