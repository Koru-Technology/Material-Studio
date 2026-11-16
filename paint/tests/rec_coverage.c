#include "global.h"

// Every stroke painted with timelines on adds one entry to its layer's timeline (history_paint): a TL_STROKE when its
// record replays (brush, eraser), a TL_RASTER (its tile state) otherwise: the tools wave 2 does not replay yet (fill,
// decal, blur, clone).

int fails  = 0;
int checks = 0;
int kase   = 0;
int frame  = 0;
int before = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[rec_coverage] %s %s", ok ? "ok  " : "FAIL", name);
}

int tool_of(int k) {
	if (k == 1) {
		return TOOL_TYPE_ERASER;
	}
	if (k == 2) {
		return TOOL_TYPE_FILL;
	}
	if (k == 3) {
		return TOOL_TYPE_DECAL;
	}
	if (k == 4) {
		return TOOL_TYPE_BLUR;
	}
	if (k == 5) {
		return TOOL_TYPE_CLONE;
	}
	return TOOL_TYPE_BRUSH;
}

char *name_of(int k) {
	if (k == 0) {
		return "brush";
	}
	if (k == 1) {
		return "eraser";
	}
	if (k == 2) {
		return "fill";
	}
	if (k == 3) {
		return "decal";
	}
	if (k == 4) {
		return "blur";
	}
	return "clone";
}

void next() {
	frame           = frame + 1;
	slot_layer_t *l = script_get_context()->layer;
	if (frame == 2) {
		script_brush_defaults();
		script_get_context()->brush_radius = 0.3;
		context_select_tool(tool_of(kase));
		make_paint_set_batching(1); // Parses the paint material again
		before = timeline_test_count(l);
		if (before < 0) { // No timeline before its first change
			before = 0;
		}
	}
	else if (frame == 6) {
		for (int i = 0; i < 12; ++i) {
			script_paint(0.4 + i * 0.01, 0.5);
		}
		script_paint_end();
		paint_rec_flush();
	}
	else if (frame == 8) {
		int n    = timeline_test_count(l);
		int kind = timeline_test_kind(l, n - 1);
		int want = kase <= 1 ? 1 : 12;
		check(n == before + 1, string("%s: one entry (%d to %d)", name_of(kase), before, n));
		check(kind == want, string("%s: a %s entry (kind %d)", name_of(kase), want == 1 ? "TL_STROKE" : "TL_RASTER", kind));
		kase  = kase + 1;
		frame = 0;
		if (kase == 6) {
			check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
			printf("[rec_coverage] %d checks, %d failures", checks, fails);
			printf(fails == 0 ? "REC_COVERAGE PASS" : "REC_COVERAGE FAIL");
			script_quit();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	app_layer_new(0);
	script_notify_on_next_frame(next);
}
