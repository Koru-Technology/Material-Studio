#include "global.h"

// Brush preview: the stroke is painted as a Pencil stroke, pen pressure tapers its ends when it drives the size

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[brush_preview] %s %s", ok ? "ok  " : "FAIL", name);
}

// Lit texels of a column of the stroke strip, its width is four times its height
buffer_t *pixels = NULL;
int       w      = 0;
int       h      = 0;

void read_wave() {
	pixels = script_get_texture_pixels(script_brush_wave());
	int n  = pixels->length / 4;
	w      = 1;
	while (w * (w / 4) < n) {
		w = w + 1;
	}
	h = n / w;
}

int column(float fx) {
	int x     = (int)(fx * w);
	int count = 0;
	for (int y = 0; y < h; ++y) {
		if (buffer_get_u8(pixels, (y * w + x) * 4 + 3) > 100) {
			count = count + 1;
		}
	}
	return count;
}

int peak() {
	int best = 0;
	for (int i = 20; i <= 80; ++i) {
		int c = column(i / 100.0);
		if (c > best) {
			best = c;
		}
	}
	return best;
}

// The stroke runs from 10% to 90% of the width
int   head = 0;
int   tail = 0;
int   top  = 0;
void measure() {
	read_wave();
	head = column(0.1 + 0.8 * 0.06);
	tail = column(0.1 + 0.8 * 0.94);
	top  = peak();
	printf("[brush_preview] %dx%d head %d peak %d tail %d", w, h, head, top, tail);
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame          = 0;
	config_t *cfg  = script_get_config();

	if (step == 0) {
		script_brush_defaults();
		script_brush_set("pressure_size", 1.0);
		cfg->pressure_curve       = 1.0;
		cfg->pressure_min         = 0.0;
		cfg->pressure_sensitivity = 1.0;
		measure();
		check(top > 0, "the stroke is painted");
		check(top < h * 9 / 10, "the stroke fits in the strip");
		check(head * 2 < top, "pressure tapers the start");
		check(tail * 2 < top, "pressure tapers the end");
	}
	else if (step == 1) {
		script_brush_set("pressure_size", 0.0);
		measure();
		check(head * 10 > top * 7 && tail * 10 > top * 7, "without size pressure the stroke keeps its width");
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[brush_preview] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "BRUSH_PREVIEW PASS" : "BRUSH_PREVIEW FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
