#include "global.h"

// Picking a library material: its paint shader is parsed at once, translated and compiled on a thread
// (make_material_parse_paint_material_async), the frames go on meanwhile.
// - the longest frame of each pick (Mosaic Tiles 1081, 1819, 784, 569), from the pick until its paint shader is in use
// - a stroke the frame after the pick, while the shader compiles, waits for it and paints the new material, not the
//   previous one (a magenta material)
// - a stroke start as util_brush_update makes it: waits at most 250 ms, then starts on the first frame the shader is
//   ready; paints the new material
// - a stroke ~100 ms after the pick paints the new material
// Run with PAINT_SHADER_ASYNC=0 for the numbers before (compiled at once). IRON_SHADER_TIMING / IRON_FRAME_TIMING log.

int fails  = 0;
int checks = 0;
int phase  = 0;
int frame  = 0;

int    ids[4]     = {1081, 1819, 784, 569};
float  longest[4] = {0.0, 0.0, 0.0, 0.0};
int    pick_k     = 0;
int    parses0    = 0; // make_material_paint_stat(1) before a pick
int    ready_f    = 0; // Frames since its shader is in use
double t_pick     = 0.0;
double t_parse    = 0.0;
int    others[3]  = {-1, -1, -1}; // Library indices of three more materials, not picked before (no cached Metal library)
int    gate_r     = 0;
float  gate_ms    = 0.0;
float  poll_max   = 0.0;
int    poll_n     = 0;

slot_material_t *m_magenta = NULL;

int STRIDE = 5;

buffer_t *base_b = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[pick] %s %s", ok ? "ok  " : "FAIL", name);
}

buffer_t *sample(gpu_texture_t *t) {
	buffer_t *p = script_get_texture_pixels(t);
	int       n = p->length / (4 * STRIDE);
	buffer_t *c = buffer_create(n * 4);
	for (int i = 0; i < n; ++i) {
		int s = i * 4 * STRIDE;
		buffer_set_u8(c, i * 4, buffer_get_u8(p, s));
		buffer_set_u8(c, i * 4 + 1, buffer_get_u8(p, s + 1));
		buffer_set_u8(c, i * 4 + 2, buffer_get_u8(p, s + 2));
		buffer_set_u8(c, i * 4 + 3, buffer_get_u8(p, s + 3));
	}
	return c;
}

int absi(int v) {
	return v < 0 ? -v : v;
}

void snap_base() {
	base_b = sample(script_get_context()->layer->texpaint);
}

int painted = 0; // Samples the last stroke changed
int magenta = 0; // Of them, magenta ones

void measure() {
	buffer_t *a = sample(script_get_context()->layer->texpaint);
	painted     = 0;
	magenta     = 0;
	for (int i = 0; i < a->length; i += 4) {
		int d = 0;
		for (int c = 0; c < 3; ++c) {
			int e = absi(buffer_get_u8(a, i + c) - buffer_get_u8(base_b, i + c));
			d     = e > d ? e : d;
		}
		if (d > 8) {
			painted = painted + 1;
			if (buffer_get_u8(a, i) > 170 && buffer_get_u8(a, i + 1) < 90 && buffer_get_u8(a, i + 2) > 170) {
				magenta = magenta + 1;
			}
		}
	}
}

void stroke() {
	for (int i = 0; i < 24; ++i) {
		float t = i / 23.0;
		script_paint(0.3 + 0.4 * t, 0.42 + 0.12 * t);
	}
	script_paint_end();
}

int is_magenta(buffer_t *a, int i) {
	return buffer_get_u8(a, i) > 170 && buffer_get_u8(a, i + 1) < 90 && buffer_get_u8(a, i + 2) > 170;
}

// A magenta stroke, then the base the next stroke paints over (the same path)
void magenta_base() {
	stroke();
	snap_base();
}

// The stroke painted over the magenta one with the new material: (almost) no magenta left where it was, whatever the
// new material's colors and its buffers so far
void check_new(char *what) {
	buffer_t *a     = sample(script_get_context()->layer->texpaint);
	int       foot  = 0;
	int       still = 0;
	for (int i = 0; i < a->length; i += 4) {
		if (is_magenta(base_b, i)) {
			foot = foot + 1;
			if (is_magenta(a, i)) {
				still = still + 1;
			}
		}
	}
	check(foot > 1000 && still * 10 < foot, string("%s paints the new material (%d of %d magenta samples still magenta)", what, still, foot));
}

void finish() {
	check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
	printf("[pick] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "PICK PASS" : "PICK FAIL");
	script_quit();
}

void pick_index(int i) {
	parses0 = make_material_paint_stat(1);
	t_pick  = sys_time();
	printf("[pick] picking %s (%d)", mm_library_name(i), mm_library_id(i));
	mm_library_use(i);
}

// The pick's paint shader was parsed (context_set_material ran)
int parsed() {
	return make_material_paint_stat(1) > parses0 || (make_material_paint_stat(0) == 0 && script_get_context()->material != m_magenta && sys_time() - t_pick > 3.0);
}

int ready() {
	return make_material_paint_stat(0) == 0;
}

void next() {
	frame    = frame + 1;
	float ft = script_frame_time();
	if (phase == 0) { // A magenta material, the previous material of the strokes
		if (frame == 3) {
			script_layer_new();
			m_magenta        = script_material_create("Magenta");
			ui_node_t *out   = script_material_get_node("OUTPUT_MATERIAL_PBR");
			script_material_set_color(out, 1, 0, 1.0, 0.0, 1.0, 1.0);
			script_material_update();
			int n = mm_library_count();
			int k = 0;
			for (int j = 1; j < 50 && k < 3; ++j) {
				int i  = (n * j) / 7 % n;
				int id = mm_library_id(i);
				if (id != 1081 && id != 1819 && id != 784 && id != 569) {
					others[k] = i;
					k         = k + 1;
				}
			}
			phase = 1;
			frame = 0;
		}
	}
	else if (phase == 1) { // The metric tells the magenta material apart
		if (frame == 4 && ready()) {
			snap_base();
			stroke();
			measure();
			check(painted > 1000 && magenta * 10 > painted * 8, string("a magenta stroke reads magenta (%d samples painted, %d magenta)", painted, magenta));
			history_undo();
			phase = 2;
			frame = 0;
		}
		else if (frame > 300) {
			check(false, "the magenta material compiles");
			finish();
			return;
		}
	}
	else if (phase == 2) { // The longest frame of each pick
		if (frame == 4) {
			pick_index(mm_library_find(ids[pick_k]));
			longest[pick_k] = 0.0;
			ready_f         = 0;
			phase           = 21;
			frame           = 0;
		}
	}
	else if (phase == 21) {
		if (frame > 1 && ft > longest[pick_k]) {
			longest[pick_k] = ft;
		}
		if (parsed() && ready()) {
			ready_f = ready_f + 1;
		}
		if (ready_f == 4) { // The frames up to the one that started using it, measured
			printf("[pick] %d: longest frame %.0f ms, parse %.1f ms, its shader in use %.0f ms after the parse started, %.2f s after the pick", ids[pick_k],
			       longest[pick_k] * 1000.0, make_material_paint_stat(4) / 1000.0, make_material_paint_stat(5) / 1000.0, sys_time() - t_pick);
			pick_k = pick_k + 1;
			phase  = pick_k < 4 ? 2 : 3;
			frame  = 0;
		}
		else if (sys_time() - t_pick > 30.0) {
			check(false, string("material %d is picked and compiled within 30 s", ids[pick_k]));
			finish();
			return;
		}
	}
	else if (phase == 3) { // A stroke the frame after the pick
		if (frame == 1) {
			float worst = 0.0;
			for (int k = 0; k < 4; ++k) {
				worst = longest[k] > worst ? longest[k] : worst;
			}
			check(worst < 0.25, string("no frame of the picks takes 250 ms (%.0f, %.0f, %.0f, %.0f ms)", longest[0] * 1000.0, longest[1] * 1000.0, longest[2] * 1000.0,
			                           longest[3] * 1000.0));
			script_material_set(m_magenta);
		}
		if (frame == 4 && ready()) {
			magenta_base();
			pick_index(others[0]);
			phase = 31;
			frame = 0;
		}
	}
	else if (phase == 31) {
		if (parsed()) {
			int pending = make_material_paint_stat(0);
			int waits   = make_material_paint_stat(6);
			printf("[pick] stroke %d frames after the pick started, the shader %s", frame, pending ? "compiling" : "ready");
			stroke();
			check(pending == 1 && make_material_paint_stat(6) > waits, "the stroke started while the shader compiled and waited for it");
			check(ready(), "the stroke uses the new shader");
			check_new("a stroke the frame after the pick");
			history_undo();
			history_undo();
			phase = 4;
			frame = 0;
		}
		else if (sys_time() - t_pick > 30.0) {
			check(false, "the material is picked");
			finish();
			return;
		}
	}
	else if (phase == 4) { // The stroke start of util_brush_update: a wait of 250 ms at most, then a frame at a time
		if (frame == 1) {
			script_material_set(m_magenta);
		}
		if (frame == 4 && ready()) {
			magenta_base();
			pick_index(others[1]);
			phase = 41;
			frame = 0;
		}
	}
	else if (phase == 41) {
		if (parsed()) {
			int    pending = make_material_paint_stat(0);
			double t       = sys_time();
			gate_r         = make_material_paint_ready(0.25);
			gate_ms        = (sys_time() - t) * 1000.0;
			printf("[pick] stroke start: the shader %s, waited %.0f ms, %s", pending ? "compiling" : "ready", gate_ms, gate_r ? "ready" : "still compiling");
			check(gate_ms < 300.0, string("a stroke start waits at most 250 ms (%.0f ms)", gate_ms));
			poll_max = 0.0;
			poll_n   = 0;
			phase    = 42;
			frame    = 0;
		}
		else if (sys_time() - t_pick > 30.0) {
			check(false, "the material is picked");
			finish();
			return;
		}
	}
	else if (phase == 42) {
		if (frame > 1 && ft > poll_max) {
			poll_max = ft;
		}
		if (gate_r || make_material_paint_ready(0.0)) {
			printf("[pick] the held back stroke starts after %d more frames (longest %.0f ms)", poll_n, poll_max * 1000.0);
			stroke();
			check(poll_max < 0.1, string("frames while the stroke is held back take less than 100 ms (%.0f ms)", poll_max * 1000.0));
			check_new("a stroke held back until the shader is ready");
			history_undo();
			history_undo();
			phase = 5;
			frame = 0;
		}
		poll_n = poll_n + 1;
	}
	else if (phase == 5) { // A stroke ~100 ms after the pick
		if (frame == 1) {
			script_material_set(m_magenta);
		}
		if (frame == 4 && ready()) {
			magenta_base();
			pick_index(others[2]);
			t_parse = 0.0;
			phase   = 51;
			frame   = 0;
		}
	}
	else if (phase == 51) {
		if (t_parse == 0.0 && parsed()) {
			t_parse = sys_time();
		}
		if (t_parse > 0.0 && sys_time() - t_parse >= 0.1) {
			int pending = make_material_paint_stat(0);
			printf("[pick] stroke %.0f ms after the parse, the shader %s", (sys_time() - t_parse) * 1000.0, pending ? "compiling" : "ready");
			stroke();
			check_new("a stroke 100 ms after the pick");
			history_undo();
			history_undo();
			finish();
			return;
		}
		else if (sys_time() - t_pick > 30.0) {
			check(false, "the material is picked");
			finish();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults(); // A plain round brush, whatever the user left picked
	script_project_new();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.4;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
