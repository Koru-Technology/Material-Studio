#include "global.h"

// Strokes painted right after picking a heavy Material Maker library material (Medieval Stone Brick Wall, 1403) paint at once with
// tiny buffers rendered on the spot (within an allowance), then sharpen by replay once the buffers are full
// (paint_refine.c), fill layers paint again as better tiers land (util_stroke_refine.c):
// - a live stroke (raw mouse events) right after the pick: no frame of it or of the seconds after it takes 350 ms
// - a stroke (script_paint) right after the pick changes texels at once; once the buffers are full, it matches the same
//   stroke painted with full buffers; the sharpening adds no timeline entry, one undo removes the stroke, redo brings
//   it back sharpened
// - two overlapping strokes before full match their reference
// - a stroke of another material painted on top keeps its place: both match their reference
// - a fill layer made right after the pick matches one made after full
// Run with IRON_FRAME_TIMING / IRON_SHADER_TIMING for the logs.

int fails  = 0;
int checks = 0;
int phase  = 20; // 20-22 the live stroke, then 0-16
int frame  = 0;
int records = 0; // Most records seen during the live stroke
int sharpened0 = 0; // Sharpenings and records ended early before the scripted stroke
int ended0     = 0;

slot_material_t *m0       = NULL; // The material before the pick
double           t_pick   = 0.0;
double           t_stroke = 0.0;
float            max_frame = 0.0; // Longest frame from the stroke until the buffers are full
float            stroke_ms = 0.0; // The stroke's paint calls
float            max_pick  = 0.0; // Longest frame from the pick until the stroke
int              sharpened = 0;
int              wait      = 0;
slot_layer_t    *fill1     = NULL;
int              entries   = 0;    // Timeline entries after the stroke
slot_material_t *plain     = NULL; // A material that is full at once
slot_material_t *heavy     = NULL;

int STRIDE = 5; // Texels between samples

buffer_t *base_b  = NULL; // Before the strokes
buffer_t *base_n  = NULL;
buffer_t *base_p  = NULL;
buffer_t *sharp_b = NULL; // Sharpened
buffer_t *sharp_n = NULL;
buffer_t *sharp_p = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[refine] %s %s", ok ? "ok  " : "FAIL", name);
}

// Every STRIDE-th texel's 4 bytes (RGBA8 layers)
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

// Samples that differ by more than tol in a channel
int differ(buffer_t *a, buffer_t *b, int tol) {
	int n = 0;
	for (int i = 0; i < a->length; i += 4) {
		int d = 0;
		for (int c = 0; c < 4; ++c) {
			int e = absi(buffer_get_u8(a, i + c) - buffer_get_u8(b, i + c));
			d     = e > d ? e : d;
		}
		if (d > tol) {
			n = n + 1;
		}
	}
	return n;
}

// a against the reference r where r was painted (differs from base): the share of painted samples off by more than tol,
// in thousandths; logs the mean and the largest difference
int compare(char *name, buffer_t *a, buffer_t *r, buffer_t *base, int tol) {
	int painted = 0;
	int bad     = 0;
	int maxd    = 0;
	int sum     = 0;
	for (int i = 0; i < a->length; i += 4) {
		int p = 0;
		int d = 0;
		for (int c = 0; c < 4; ++c) {
			int e = absi(buffer_get_u8(r, i + c) - buffer_get_u8(base, i + c));
			p     = e > p ? e : p;
			int f = absi(buffer_get_u8(a, i + c) - buffer_get_u8(r, i + c));
			d     = f > d ? f : d;
		}
		if (p > 2 || d > 2) {
			painted = painted + 1;
			sum     = sum + d;
			maxd    = d > maxd ? d : maxd;
			if (d > tol) {
				bad = bad + 1;
			}
		}
	}
	int permille = painted > 0 ? bad * 1000 / painted : 0;
	printf("[refine] %s: %d painted samples, %d off by more than %d (%d per mille), mean %.2f, max %d", name, painted, bad, tol, permille,
	       painted > 0 ? sum / (float)painted : 0.0, maxd);
	return permille;
}

void snap_base() {
	slot_layer_t *l = script_get_context()->layer;
	base_b          = sample(l->texpaint);
	base_n          = sample(l->texpaint_nor);
	base_p          = sample(l->texpaint_pack);
}

int dump = 0; // Writes the layers compared as PNGs to the save path

void dump_layer(char *name) {
	if (dump) {
		slot_layer_t *l = script_get_context()->layer;
		mm_import_save_png(l->texpaint, string("%s/refine_%s_base.png", script_save_path(), name));
		mm_import_save_png(l->texpaint_pack, string("%s/refine_%s_pack.png", script_save_path(), name));
	}
}

void snap_sharp() {
	slot_layer_t *l = script_get_context()->layer;
	sharp_b         = sample(l->texpaint);
	sharp_n         = sample(l->texpaint_nor);
	sharp_p         = sample(l->texpaint_pack);
}

// Samples of the current layer that differ from b by more than tol, its three textures
int layer_differ(buffer_t *b, buffer_t *n, buffer_t *p, int tol) {
	slot_layer_t *l = script_get_context()->layer;
	return differ(sample(l->texpaint), b, tol) + differ(sample(l->texpaint_nor), n, tol) + differ(sample(l->texpaint_pack), p, tol);
}

// The current layer against the sharpened samples, painted where the reference differs from base
void compare_sharp(char *what) {
	slot_layer_t *l = script_get_context()->layer;
	int           b = compare(string("%s base", what), sharp_b, sample(l->texpaint), base_b, 6);
	int           n = compare(string("%s normal", what), sharp_n, sample(l->texpaint_nor), base_n, 6);
	int           p = compare(string("%s pack", what), sharp_p, sample(l->texpaint_pack), base_p, 6);
	check(b <= 10 && n <= 10 && p <= 10, string("%s: the sharpened strokes match the strokes painted with full buffers (%d, %d, %d per mille off)", what, b, n, p));
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 24; ++i) {
		float t = i / 23.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	script_paint_end();
}

void stroke_a() {
	stroke(0.3, 0.45, 0.7, 0.5);
}

void stroke_b1() {
	stroke(0.35, 0.35, 0.6, 0.6);
}

void stroke_b2() {
	stroke(0.6, 0.35, 0.4, 0.62);
}

void pick() {
	m0 = script_get_context()->material;
	mm_library_count();
	int i = mm_library_find(1403);
	printf("[refine] picking %s", mm_library_name(i));
	t_pick   = sys_time();
	max_pick = 0.0;
	mm_library_use(i);
}

// The picked material is selected and compiled: its buffers are queued
int picked() {
	return script_get_context()->material != m0 && mm_buffers_stat(11) > 0;
}

int full() {
	return mm_buffers_stat(11) == 0 && paint_refine_stat(0) == 0 && paint_refine_stat(1) == 0 && stroke_refine_stat(1) == 0;
}

void log_progress() {
	if (paint_refine_stat(2) != sharpened) {
		sharpened = paint_refine_stat(2);
		printf("[refine] sharpened by replay (%d) at %.2f s after the stroke, %d entries replayed so far", sharpened, sys_time() - t_stroke, paint_refine_stat(5));
	}
}

void finish() {
	check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
	printf("[refine] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "REFINE PASS" : "REFINE FAIL");
	script_quit();
}

void next() {
	frame = frame + 1;
	float ft = script_frame_time();
	int   cx = sys_x() + sys_w() / 2;
	int   cy = sys_h() / 2;
	if (phase == 20) { // A live stroke right after the pick
		if (frame == 4) {
			pick();
			phase = 21;
			frame = 0;
		}
	}
	else if (phase == 21) {
		// Its paint shader compiled too (a thread, up to ~600 ms with Metal's cache cold): a stroke shorter than that would be
		// held back whole (material_pick.c covers that), the buffers are far from full still
		if (picked() && make_material_paint_stat(0) == 0) {
			printf("[refine] compiled %.2f s after the pick, the live stroke starts", sys_time() - t_pick);
			t_stroke = sys_time();
			script_mouse_press(cx - 150, cy, 0);
			max_frame  = 0.0;
			sharpened  = paint_refine_stat(2);
			sharpened0 = sharpened;
			phase      = 22;
			frame      = 0;
		}
		else if (frame > 900) {
			check(false, "the picked material compiles");
			finish();
			return;
		}
	}
	else if (phase == 22) {
		if (frame == 1) { // The frame the stroke started in, after the one that compiled the material
			printf("[refine] the frame the live stroke started in: %.0f ms", ft * 1000.0);
		}
		else if (ft > max_frame) {
			max_frame = ft;
			printf("[refine] live: frame of %.0f ms at %.2f s after the stroke started", ft * 1000.0, sys_time() - t_stroke);
		}
		records = paint_refine_stat(0) > records ? paint_refine_stat(0) : records;
		log_progress();
		if (frame <= 20) {
			script_mouse_move(cx - 150 + frame * 15, cy);
		}
		if (frame == 20) {
			script_mouse_release(cx - 150 + frame * 15, cy, 0);
		}
		// Until it sharpens: the buffers full, its timeline replayed
		if (frame > 20 && (paint_refine_stat(2) > sharpened0 || sys_time() - t_stroke > 120.0)) {
			check(records == 1, "the live stroke before full buffers is noted to sharpen");
			check(paint_refine_stat(2) > sharpened0, string("the live stroke sharpened by replay (%.1f s)", sys_time() - t_stroke));
			check(max_frame < 0.35, string("no frame of the live stroke and until it sharpened takes 350 ms (at most %.0f ms)", max_frame * 1000.0));
			script_layer_new(); // The scripted strokes on a layer of their own, the buffers rendered again from nothing
			mm_buffers_clear();
			phase = 0;
			frame = 0;
		}
	}
	else if (phase == 0) { // A fresh layer
		if (frame == 4) {
			snap_base();
			pick();
			phase = 1;
			frame = 0;
		}
	}
	else if (phase == 1) { // Stroke at once once the material is compiled
		max_pick = ft > max_pick && frame > 1 ? ft : max_pick;
		if (picked()) {
			printf("[refine] compiled %.2f s after the pick (longest frame %.0f ms), %d passes queued", sys_time() - t_pick, max_pick * 1000.0, mm_buffers_stat(11));
			ended0   = paint_refine_stat(4);
			t_stroke = sys_time();
			stroke_a();
			paint_rec_flush();
			stroke_ms = (sys_time() - t_stroke) * 1000.0;
			entries   = timeline_test_count(script_get_context()->layer);
			printf("[refine] stroke painted %.0f ms after it started (%d noted)", stroke_ms, paint_refine_stat(0));
			check(paint_refine_stat(0) == 1, "the stroke before full buffers is noted to sharpen");
			max_frame  = 0.0;
			sharpened  = paint_refine_stat(2);
			sharpened0 = sharpened;
			phase      = 2;
			frame     = 0;
		}
		else if (frame > 900) {
			check(false, "the picked material compiles");
			finish();
			return;
		}
	}
	else if (phase == 2) { // Wait for full buffers: the stroke sharpens
		if (frame == 1) { // The stroke's frame, then the check's (reads the layer back), not counted
			printf("[refine] the stroke's frame: %.0f ms", ft * 1000.0);
			int changed = layer_differ(base_b, base_n, base_p, 2);
			check(changed > 1000, string("the stroke changed texels at once (%d samples)", changed));
		}
		if (frame > 2 && ft > max_frame) {
			max_frame = ft;
			printf("[refine] frame of %.0f ms at %.2f s after the stroke", ft * 1000.0, sys_time() - t_stroke);
		}
		log_progress();
		if (full() || sys_time() - t_stroke > 120.0) {
			printf("[refine] full %.2f s after the stroke, longest frame since the stroke %.0f ms", sys_time() - t_stroke, max_frame * 1000.0);
			// The stroke's own time includes script_paint parsing the paint material again (scripts only), the live stroke
			// above measures a stroke's frames
			check(max_frame < 0.35, string("the frames after the stroke (%.0f ms itself) take less than 350 ms (at most %.0f ms)", stroke_ms, max_frame * 1000.0));
			check(full(), "the buffers become full, the stroke sharpened");
			check(paint_refine_stat(2) > sharpened0 && paint_refine_stat(4) == ended0, "the stroke sharpened by replay, no note failed");
			check(timeline_test_count(script_get_context()->layer) == entries, "the sharpening adds no timeline entry");
			snap_sharp();
			dump_layer("sharp");
			history_undo();
			phase = 3;
			frame = 0;
		}
	}
	else if (phase == 3) {
		if (frame == 3) {
			int d = layer_differ(base_b, base_n, base_p, 0);
			check(d == 0, string("one undo removes the stroke (%d samples differ)", d));
			history_redo();
			phase = 4;
			frame = 0;
		}
	}
	else if (phase == 4) {
		if (frame == 3) {
			int d = layer_differ(sharp_b, sharp_n, sharp_p, 0);
			check(d == 0, string("redo brings back the sharpened stroke (%d samples differ)", d));
			history_undo();
			phase = 5;
			frame = 0;
		}
	}
	else if (phase == 5) { // The reference: the same stroke with full buffers
		if (frame == 3) {
			stroke_a();
			paint_rec_flush();
			check(paint_refine_stat(0) == 0, "a stroke with full buffers is not noted");
			phase = 6;
			frame = 0;
		}
	}
	else if (phase == 6) {
		if (frame == 3) {
			dump_layer("ref");
			compare_sharp("one stroke");
			// Two overlapping strokes before full, on a new layer, the buffers rendered again from nothing
			script_layer_new();
			mm_buffers_clear();
			phase = 7;
			frame = 0;
		}
	}
	else if (phase == 7) {
		if (frame == 3) {
			snap_base();
			pick();
			phase = 8;
			frame = 0;
		}
	}
	else if (phase == 8) {
		if (picked()) {
			t_stroke = sys_time();
			stroke_b1();
			stroke_b2();
			paint_rec_flush();
			printf("[refine] two strokes painted %.0f ms after they started", (sys_time() - t_stroke) * 1000.0);
			check(paint_refine_stat(0) == 1, "both strokes wait in the layer's note");
			sharpened = paint_refine_stat(2);
			phase     = 9;
			frame     = 0;
		}
		else if (frame > 900) {
			check(false, "the picked material compiles again");
			finish();
			return;
		}
	}
	else if (phase == 9) {
		log_progress();
		if (full() || sys_time() - t_stroke > 120.0) {
			check(full(), "the buffers become full again");
			snap_sharp();
			history_undo();
			history_undo();
			phase = 10;
			frame = 0;
		}
	}
	else if (phase == 10) {
		if (frame == 3) {
			int d = layer_differ(base_b, base_n, base_p, 0);
			check(d == 0, string("two undos remove both strokes (%d samples differ)", d));
			stroke_b1();
			stroke_b2();
			phase = 11;
			frame = 0;
		}
	}
	else if (phase == 11) {
		if (frame == 3) {
			compare_sharp("two strokes");
			// A stroke of another material on top, on a new layer, the buffers rendered again from nothing
			script_layer_new();
			mm_buffers_clear();
			phase = 30;
			frame = 0;
		}
	}
	else if (phase == 30) {
		if (frame == 3) {
			snap_base();
			pick();
			phase = 31;
			frame = 0;
		}
	}
	else if (phase == 31) {
		if (picked()) {
			heavy    = script_get_context()->material;
			t_stroke = sys_time();
			stroke_b1();
			script_material_set(plain); // Full at once: painted at its final tier
			stroke_b2();
			paint_rec_flush();
			check(paint_refine_stat(0) == 1, "the heavy stroke under the other material's is noted");
			phase = 32;
			frame = 0;
		}
		else if (frame > 900) {
			check(false, "the picked material compiles again");
			finish();
			return;
		}
	}
	else if (phase == 32) {
		log_progress();
		if (full() || sys_time() - t_stroke > 120.0) {
			check(full(), "the buffers become full again, the strokes sharpened");
			snap_sharp();
			history_undo();
			history_undo();
			phase = 33;
			frame = 0;
		}
	}
	else if (phase == 33) {
		if (frame == 3) {
			int d = layer_differ(base_b, base_n, base_p, 0);
			check(d == 0, string("two undos remove both strokes (%d samples differ)", d));
			script_material_set(heavy); // Its buffers full again before the reference (the other material may have had them go)
			phase = 35;
			frame = 0;
		}
	}
	else if (phase == 35) {
		if (frame > 3 && mm_buffers_stat(11) == 0) {
			stroke_b1();
			script_material_set(plain);
			stroke_b2();
			phase = 34;
			frame = 0;
		}
		else if (frame > 3600) {
			check(false, "the heavy material's buffers are full again");
			finish();
			return;
		}
	}
	else if (phase == 34) {
		if (frame == 3) {
			compare_sharp("a stroke of another material on top");
			script_material_set(heavy);
			mm_buffers_clear();
			phase = 12;
			frame = 0;
		}
	}
	else if (phase == 12) { // A fill layer right after the pick
		if (frame == 3) {
			pick();
			phase = 13;
			frame = 0;
		}
	}
	else if (phase == 13) {
		if (picked()) {
			t_stroke = sys_time();
			fill1    = script_layer_new_fill();
			printf("[refine] fill layer made %.0f ms after it started, %d fill layers waiting", (sys_time() - t_stroke) * 1000.0, stroke_refine_stat(1));
			check(stroke_refine_stat(1) == 1, "the fill layer waits for better buffers");
			phase = 14;
			frame = 0;
		}
		else if (frame > 900) {
			check(false, "the picked material compiles a third time");
			finish();
			return;
		}
	}
	else if (phase == 14) {
		if (full() || sys_time() - t_stroke > 120.0) {
			check(full() && stroke_refine_stat(3) >= 1, string("the fill layer was painted again %d times, the buffers are full", stroke_refine_stat(3)));
			script_layer_set(fill1);
			phase = 15;
			frame = 0;
		}
	}
	else if (phase == 15) {
		if (frame == 3) {
			snap_sharp();
			snap_base(); // Unused as a base: every texel of a fill layer is painted
			script_layer_new_fill();
			phase = 16;
			frame = 0;
		}
	}
	else if (phase == 16) {
		if (frame == 5) {
			slot_layer_t *l = script_get_context()->layer;
			int           b = differ(sample(l->texpaint), sharp_b, 6);
			int           n = differ(sample(l->texpaint_nor), sharp_n, 6);
			int           p = differ(sample(l->texpaint_pack), sharp_p, 6);
			int           s = sharp_b->length / 4;
			printf("[refine] fill layers: %d, %d, %d of %d samples differ by more than 6", b, n, p, s);
			check(l != fill1 && b * 100 <= s && n * 100 <= s && p * 100 <= s, "a fill layer made right after the pick matches one made after full");
			finish();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults(); // A plain round brush, whatever the user left picked
	script_project_new();
	render_path_paint_set_stamp_seed(4242);
	// The camera's projection jitters from frame to frame (taa) unless the brush preview is live: the strokes compared
	// would land a fraction of a pixel apart, the edges of a hard brush differ
	script_get_config()->brush_live = 1;
	plain = script_material_create("refine_plain");
	ui_node_t *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.2, 0.7, 0.3, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.4;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
