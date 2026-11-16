#include "global.h"

// Timelines rendered above the resolution they were painted at (timeline.c, timeline_ops.c). A script of strokes and
// ops runs in a 1024 project with resolved dabs and timelines on; each layer's timeline is rendered at 2048 and kept.
// The same script then runs live in a new 2048 project: each layer must match its 2048 render within tolerance, and
// the paint layer closer than its 1024 pixels scaled up. Rendering in four quadrants gives the render of the whole.
// Layers: strokes; stroke, clear, stroke; to fill, to paint, stroke; a mask stroked and inverted; an image layer.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int pass   = 0; // 0: 1024 rendered at 2048, 1: live at 2048

int NCASES = 5;
any_array_t *layers = NULL; // Of the script, by case
char *dir = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[rec_hires] %s %s", ok ? "ok  " : "FAIL", name);
}

char *case_name(int k) {
	if (k == 0) {
		return "strokes";
	}
	if (k == 1) {
		return "clear";
	}
	if (k == 2) {
		return "fill";
	}
	if (k == 3) {
		return "inverted mask";
	}
	return "image";
}

void stroke(float x0, float y0, float x1, float y1) {
	for (int i = 0; i < 24; ++i) {
		float t = i / 23.0;
		script_paint(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t + (i % 5) * 0.003);
	}
	script_paint_end();
}

void hr_brush() {
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.25;
	ctx->brush_opacity = 1.0;
	ctx->brush_spacing = 0.05;
	context_select_tool(TOOL_TYPE_BRUSH);
	render_path_paint_set_stamp_seed(4242);
}

void hr_material() {
	script_material_create("rec_hires_mat");
	ui_node_t *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.8, 0.3, 0.1, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
}

void hr_image() {
	iron_create_directory(dir);
	int       w   = 64;
	buffer_t *buf = buffer_create(w * w * 4);
	for (int y = 0; y < w; ++y) {
		for (int x = 0; x < w; ++x) {
			int i = y * w + x;
			buffer_set_u8(buf, i * 4, x * 4);
			buffer_set_u8(buf, i * 4 + 1, y * 4);
			buffer_set_u8(buf, i * 4 + 2, (x + y) % 2 == 0 ? 255 : 0);
			buffer_set_u8(buf, i * 4 + 3, 255);
		}
	}
	iron_write_png(string("%s/rec_hires.png", dir), buf, w, w, 0);
	script_import_asset(string("%s/rec_hires.png", dir), 0);
}

// The script, one stage per call: the same in both projects
void hr_run(int s) {
	context_t *ctx = script_get_context();
	if (s == 0) {
		hr_brush();
		slot_layer_t *l = script_layer_new();
		any_array_push(layers, l);
		script_layer_set(l);
		stroke(0.3, 0.4, 0.65, 0.45);
		stroke(0.45, 0.3, 0.5, 0.7);
	}
	else if (s == 1) {
		slot_layer_t *l = script_layer_new();
		any_array_push(layers, l);
		script_layer_set(l);
		stroke(0.3, 0.5, 0.7, 0.55);
		script_layer_clear(l);
	}
	else if (s == 2) {
		stroke(0.35, 0.35, 0.6, 0.65);
		slot_layer_t *l = script_layer_new();
		any_array_push(layers, l);
		script_layer_to_fill(l);
	}
	else if (s == 3) {
		slot_layer_t *l = layers->buffer[2];
		script_layer_to_paint(l);
	}
	else if (s == 4) {
		script_layer_set(layers->buffer[2]);
		hr_brush();
		stroke(0.3, 0.6, 0.7, 0.4);
		slot_layer_t *l = script_layer_new();
		slot_layer_t *m = script_layer_new_mask(l, 1); // White
		any_array_push(layers, m);
		script_layer_set(m);
		hr_brush();
		stroke(0.3, 0.45, 0.7, 0.5);
	}
	else if (s == 5) {
		script_layer_invert_mask(layers->buffer[3]);
		hr_image();
	}
	else if (s == 6) {
		layers_create_image_at(script_asset_index("rec_hires.png"), 0);
	}
	else if (s == 7) {
		any_array_push(layers, ctx->layer);
		hr_brush();
		stroke(0.4, 0.3, 0.45, 0.7);
		paint_rec_flush();
	}
}

int STAGES = 8;

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		script_project_new();
		layers = any_array_create(0);
		hr_material();
		project_test_set_res(pass == 0 ? 1024 : 2048, pass == 0 ? 1024 : 2048);
	}
	else if (step >= 1 && step <= STAGES) {
		if (step == 1) {
			check(project_test_res(0) == (pass == 0 ? 1024 : 2048), string("pass %d: project at %d", pass, project_test_res(0)));
		}
		hr_run(step - 1);
	}
	else if (step == STAGES + 1 && pass == 0) {
		timeline_finalize();
		for (int k = 0; k < NCASES; ++k) {
			slot_layer_t *l = layers->buffer[k];
			int           fl = timeline_test_floor(l);
			check(fl == 0, string("%s: floor %d of %d entries", case_name(k), fl, timeline_test_count(l)));
			check(timeline_test_upscalable(l), string("%s: upscalable", case_name(k)));
			int fb = paint_replay_test_fallbacks();
			check(timeline_test_render(l, -1, 2048, 2048, 0.0, 0.0, 1.0, 1.0), string("%s: renders at 2048", case_name(k)));
			check(paint_replay_test_fallbacks() == fb, string("%s: no replay fell back", case_name(k)));
			timeline_test_keep(k);
		}
		check(timeline_test_scale_live(layers->buffer[0], 2048, 2048), "strokes: the 1024 pixels scaled up");
		timeline_test_keep(NCASES);
		int q = timeline_test_quadrants(layers->buffer[0], 2048, 2);
		check(q >= 0 && q <= 16, string("strokes: four quadrants are the whole (%d values off by more than 2)", q));
		q = timeline_test_quadrants(layers->buffer[3], 2048, 2);
		check(q == 0, string("inverted mask: four quadrants are the whole (%d)", q));
		pass = 1;
		step = -1;
	}
	else if (step == STAGES + 1 && pass == 1) {
		for (int k = 0; k < NCASES; ++k) {
			slot_layer_t   *l    = layers->buffer[k];
			int             ch   = k == 3 ? 1 : 3;
			for (int c = 0; c < ch; ++c) {
				gpu_texture_t *t = c == 0 ? l->texpaint : c == 1 ? l->texpaint_nor : l->texpaint_pack;
				int   over = script_diff_textures(timeline_test_kept(k, c), t, 24.0);
				float mean = script_diff_mean();
				float max  = script_diff_max();
				int   n    = t->width * t->height * 4;
				printf("[rec_hires] %s channel %d: %d of %d values off by more than 24/255, mean %f, max %f", case_name(k), c, over, n, mean, max);
				check(over >= 0 && over * 1000 < n && mean < 1.0, string("%s channel %d: the 2048 render is the live 2048 one", case_name(k), c));
			}
		}
		slot_layer_t *l0     = layers->buffer[0];
		int           over_r = script_diff_textures(timeline_test_kept(0, 0), l0->texpaint, 24.0);
		float mean_r = script_diff_mean();
		int   over_s = script_diff_textures(timeline_test_kept(NCASES, 0), l0->texpaint, 24.0);
		float mean_s = script_diff_mean();
		printf("[rec_hires] strokes base vs live 2048: replayed %d over (mean %f), 1024 scaled up %d over (mean %f)", over_r, mean_r, over_s, mean_s);
		check(over_r < over_s, "strokes: replay at 2048 is closer to live 2048 than scaling 1024 up");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[rec_hires] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "REC_HIRES PASS" : "REC_HIRES FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	dir = script_tmp_dir("materialstudio_rec_hires");
	script_brush_defaults();
	script_notify_on_next_frame(next);
}
