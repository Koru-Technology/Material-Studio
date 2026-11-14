#include "global.h"

// Brush Creator and brush library: making, saving, copying and deleting brushes, and the stroke settings it edits shaping the stroke

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int count0 = 0; // Brushes in the library at the start
int made   = 0; // Id of the brush made
double close_start = 0.0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[brush_creator] %s %s", ok ? "ok  " : "FAIL", name);
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

// Rows of the strip with any paint, how far the stamps spread across the stroke
int spread() {
	int top = -1;
	int bottom = -1;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; x = x + 2) {
			if (buffer_get_u8(pixels, (y * w + x) * 4 + 3) > 100) {
				if (top < 0) {
					top = y;
				}
				bottom = y;
				break;
			}
		}
	}
	return top < 0 ? 0 : bottom - top;
}

// The stroke runs from 10% to 90% of the width
int head = 0;
int tail = 0;
int top  = 0;
void measure() {
	read_wave();
	head = column(0.1 + 0.8 * 0.06);
	tail = column(0.1 + 0.8 * 0.94);
	top  = peak();
	printf("[brush_creator] %dx%d head %d peak %d tail %d", w, h, head, top, tail);
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		count0 = brush_library_count();
		check(count0 >= 12, "the built-in brushes are in the library");
		ui_brush_creator_new(1);
		check(ui_brush_creator_shown(), "New Brush opens the Brush Creator");
	}
	else if (step == 1) {
		ui_brush_creator_close(0);
		close_start = sys_time();
	}
	else if (step == 2) {
		if (ui_brush_creator_shown() && sys_time() - close_start < 1.0) { // Its 0.22 s close animation, by the clock
			script_notify_on_next_frame(next);
			return;
		}
		check(!ui_brush_creator_shown(), "Cancel closes the Brush Creator");
		check(brush_library_count() == count0, "Cancel adds no brush");
		ui_brush_creator_new(1);
	}
	else if (step == 3) {
		ui_brush_creator_close(1);
	}
	else if (step == 4) {
		context_t *ctx = script_get_context();
		check(brush_library_count() == count0 + 1, "saving adds the brush");
		made = ctx->brush_selected;
		check(made >= 1000, "the new brush is picked, with a user id");
		int copy = script_brush_duplicate(1); // Round Hard
		check(copy >= 1000 && brush_library_count() == count0 + 2, "a built-in brush copies to an editable brush");
		script_brush_delete(copy);
		script_brush_delete(made);
		check(brush_library_count() == count0, "deleting removes the brushes");
		script_brush_delete(1);
		check(brush_library_count() == count0, "a built-in brush can not be deleted");
	}
	else if (step == 5) {
		script_brush_defaults();
		script_brush_set("spacing", 0.05);
		measure();
		int plain_head = head;
		check(top > 0, "a stamped stroke paints");
		script_brush_set("touch_taper_start", 1.0);
		script_brush_set("taper_start", 1.0);
		script_brush_set("taper_size", 1.0);
		measure();
		check(head * 2 < plain_head || head == 0, "the start taper narrows the start");
		script_brush_set("taper_start", 0.0);
		script_brush_set("touch_taper_start", 0.0);
		script_brush_set("taper_end", 1.0);
		script_brush_set("touch_taper_end", 1.0);
		measure();
		check(tail * 2 < top, "the end taper narrows the end");
		check(top > 0, "the end taper paints the stroke");
	}
	else if (step == 6) {
		script_brush_defaults();
		script_brush_set("spacing", 0.3);
		read_wave();
		int plain = spread();
		script_brush_set("jitter_lateral", 1.5);
		read_wave();
		int lateral = spread();
		check(lateral > plain, "lateral jitter spreads the stamps across the stroke");
		script_brush_set("jitter_lateral", 0.0);
		script_brush_set("falloff", 1.0);
		measure();
		check(tail < top || tail == 0, "fall off fades the stroke along its length");
	}
	else if (step == 7) {
		script_brush_defaults();
		script_brush_set("streamline", 0.8);
		script_brush_set("stabilization", 0.8);
		measure();
		check(top > 0, "a smoothed stroke paints");
	}
	else {
		script_brush_defaults();
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[brush_creator] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "BRUSH_CREATOR PASS" : "BRUSH_CREATOR FAIL");
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
