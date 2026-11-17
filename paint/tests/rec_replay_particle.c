#include "global.h"

// The particle tool records a TL_STROKE (paint_exec_particles, paint_rec.c): a draw a particle that hit the mesh, where
// it hit and its radius in the op. A live stroke (raw mouse events) at 1024 until its particles are gone, the camera
// turns, the state before is restored and the record replays: byte-equal. Then the record replays into a 2048 layer:
// each 2x2 block of it close to the texel of the 1024 result (the physics does not run the same twice, there is no
// live stroke at 2048 to compare with; the layer at 2048 is itself the record replayed, resolution_change.c).

int fails  = 0;
int checks = 0;
int frame  = 0;
int phase  = 0;

slot_layer_t *base  = NULL;
slot_layer_t *layer = NULL;
int           entry = 0;
char         *hash  = NULL;
buffer_t     *low   = NULL; // Every third texel of the 1024 result's base color (STEP)
int           STEP  = 3;
int           cx    = 0;
int           cy    = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[rec_particle] %s %s", ok ? "ok  " : "FAIL", name);
}

slot_layer_t *new_layer() {
	int count = app_layer_count();
	app_layer_new(0);
	if (app_layer_count() != count + 1) { // iOS caps the layers of a project
		check(0, string("a new layer, %d layers before", count));
	}
	return script_get_context()->layer;
}

char *hash3(slot_layer_t *l) {
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

int diff3(slot_layer_t *a, slot_layer_t *b, float tol) {
	int d = script_diff_textures(a->texpaint, b->texpaint, tol);
	d     = d + script_diff_textures(a->texpaint_nor, b->texpaint_nor, tol);
	d     = d + script_diff_textures(a->texpaint_pack, b->texpaint_pack, tol);
	return d;
}

void finish() {
	check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
	check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
	printf("[rec_particle] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "REC_PARTICLE PASS" : "REC_PARTICLE FAIL");
	script_quit();
}

void next() {
	frame = frame + 1;
	if (phase == 0) {
		if (frame == 2) {
			project_test_set_res(1024, 1024);
		}
		else if (frame == 4) {
			base  = new_layer();
			layer = new_layer();
			context_select_tool(TOOL_TYPE_PARTICLE);
			// The default emitter: a particle born on the surface every 0.2 s while held, each alive 5 s
			script_get_context()->brush_radius = 0.5;
			cx                     = sys_x() + sys_w() / 2;
			cy                     = sys_h() / 2;
			script_mouse_press(cx, cy, 0);
			phase = 1;
			frame = 0;
		}
	}
	else if (phase == 1) { // Held, moving, then released: the particles paint until they are gone
		if (frame <= 20) {
			script_mouse_move(cx + frame * 6, cy);
		}
		if (frame == 20) {
			script_mouse_release(cx + frame * 6, cy, 0);
		}
		if (frame > 20 && !util_particle_alive()) {
			phase = 2;
			frame = 0;
		}
		else if (frame > 3600) {
			check(false, "the particles are gone");
			finish();
			return;
		}
	}
	else if (phase == 2) {
		if (frame == 3) {
			paint_rec_flush();
			context_select_tool(TOOL_TYPE_BRUSH);
			entry = timeline_test_count(layer) - 1;
			int p = diff3(layer, base, 1.0);
			check(timeline_test_kind(layer, entry) == 1 && p > 100, string("a TL_STROKE, it paints (%d)", p));
			hash = hash3(layer);
			int ok = paint_replay_test_entry(layer, entry);
			check(ok && string_equals(hash3(layer), hash), "replays byte-equal right after");
			script_camera_save();
			script_camera_turn(0.3, 0.15);
		}
		else if (frame == 5) {
			int fb = paint_replay_test_fallbacks();
			int ok = paint_replay_test_entry(layer, entry);
			check(ok && string_equals(hash3(layer), hash), "replays byte-equal after the camera turned");
			check(paint_replay_test_fallbacks() == fb, "no replay fell back");
			script_camera_restore();
			buffer_t *p = script_get_texture_pixels(layer->texpaint); // Read again by the next call: copied
			int       n = 1024 / STEP + 1;
			low         = buffer_create(n * n * 4);
			for (int y = 0; y < n; ++y) {
				for (int x = 0; x < n; ++x) {
					for (int c = 0; c < 4; ++c) {
						int sx = x * STEP < 1024 ? x * STEP : 1023;
						int sy = y * STEP < 1024 ? y * STEP : 1023;
						buffer_set_u8(low, (y * n + x) * 4 + c, buffer_get_u8(p, (sy * 1024 + sx) * 4 + c));
					}
				}
			}
			project_test_set_res(2048, 2048);
		}
		else if (frame == 8) {
			slot_layer_t *blank   = new_layer();
			slot_layer_t *full    = new_layer();
			int           ok      = paint_replay_test_into(layer, entry, full);
			int           painted = diff3(full, blank, 0.0);
			check(ok && painted > 100, string("replays at 2048 and paints (%d)", painted));
			// Every third texel of the 1024 result painted against the mean of its 2x2 block at 2048
			buffer_t *high    = script_get_texture_pixels(full->texpaint);
			int       samples = 0;
			int       bad     = 0;
			int       n       = 1024 / STEP + 1;
			for (int y = 0; y < 1024; y += STEP) {
				for (int x = 0; x < 1024; x += STEP) {
					int i = (y / STEP * n + x / STEP) * 4;
					if (buffer_get_u8(low, i + 3) > 0) {
						samples = samples + 1;
						int d   = 0;
						for (int c = 0; c < 4; ++c) {
							int j = (y * 2 * 2048 + x * 2) * 4 + c;
							int m = (buffer_get_u8(high, j) + buffer_get_u8(high, j + 4) + buffer_get_u8(high, j + 2048 * 4) + buffer_get_u8(high, j + 2048 * 4 + 4)) / 4;
							int e = m - buffer_get_u8(low, i + c);
							e     = e < 0 ? -e : e;
							d     = e > d ? e : d;
						}
						if (d > 8) {
							bad = bad + 1;
						}
					}
				}
			}
			printf("[rec_particle] at 2048: %d painted, %d of %d samples of the 1024 result over 8/255 against their 2x2 block", painted, bad, samples);
			check(samples > 1000 && bad * 20 <= samples, string("the 1024 record at 2048 is within 8/255 of the 1024 result on 95 percent of its blocks (%d of %d)", bad, samples));
			finish();
			return;
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	material_cache_set_enabled(0); // No live cache building on a thread when the script quits
	script_brush_defaults();
	script_project_new();
	slot_material_t *m   = script_material_create("ParticleUV");
	ui_node_t       *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
	ui_node_t       *chk = script_material_create_node("TEX_CHECKER");
	ui_node_t       *tc  = script_material_create_node("TEX_COORD");
	script_material_connect(tc, 2, chk, 0); // UV
	script_material_set_color(chk, 1, 1, 0.9, 0.2, 0.1, 1.0);
	script_material_set_color(chk, 1, 2, 0.1, 0.3, 0.8, 1.0);
	script_material_set_float(chk, 1, 3, 3.0); // Large squares: few edges where resolutions differ
	script_material_connect(chk, 0, out, 0);
	script_material_connect(chk, 1, out, 3);
	script_material_update();
	script_get_config()->brush_live = 1; // The camera does not jitter from frame to frame
	script_notify_on_next_frame(next);
}
