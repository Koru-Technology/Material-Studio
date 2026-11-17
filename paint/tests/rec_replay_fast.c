#include "global.h"

// Composite-fast replays (paint_replay.c, util_material_cache.c): a stroke replayed with its material read from a
// replay cache (a material snapshot rendered once for the replay's target, RGBA128) paints the same bits as the same
// replay evaluating the material for every dab. Strokes are recorded at 1024, the project goes to 2048 and each record
// replays into new layers both ways, whole and in 4 tiles: the layers must be byte-equal. Then at the recorded size.
// Materials: a checker at the uv coordinates (batched brush, unbatched brush), a checker at the position (unfit:
// evaluated, still equal), a heavy Material Maker library material (Old Brick Wall, 784) once its buffers are full.

int fails  = 0;
int checks = 0;
int frame  = 0;
int phase  = 0;
double t0  = 0.0;

slot_material_t *m_uv  = NULL;
slot_material_t *m_pos = NULL;
slot_layer_t    *rec   = NULL; // Strokes recorded at 1024
int              entries[8];
char            *names[8];
int              n = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[rec_fast] %s %s", ok ? "ok  " : "FAIL", name);
}

slot_layer_t *new_layer() {
	int count = app_layer_count();
	app_layer_new(0);
	if (app_layer_count() != count + 1) { // iOS caps the layers of a project
		check(0, string("a new layer, %d layers before", count));
	}
	return script_get_context()->layer;
}

slot_material_t *make_checker(char *name, int uv) {
	slot_material_t *m   = script_material_create(name);
	ui_node_t       *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
	ui_node_t       *chk = script_material_create_node("TEX_CHECKER");
	if (uv) {
		ui_node_t *tc = script_material_create_node("TEX_COORD");
		script_material_connect(tc, 2, chk, 0); // UV
	}
	script_material_set_color(chk, 1, 1, 0.9, 0.2, 0.1, 1.0);
	script_material_set_color(chk, 1, 2, 0.1, 0.3, 0.8, 1.0);
	script_material_set_float(chk, 1, 3, 23.0);
	script_material_connect(chk, 0, out, 0); // Base color
	script_material_connect(chk, 1, out, 3); // Roughness from the factor
	script_material_update();
	return m;
}

void stroke(float y) {
	for (int i = 0; i < 30; ++i) {
		script_paint(0.3 + i * 0.012, y + (i % 7) * 0.004);
	}
	script_paint_end();
}

// The last entry of rec noted under name
void note(char *name) {
	paint_rec_flush();
	int e = timeline_test_count(rec) - 1;
	check(timeline_test_kind(rec, e) == 1, string("%s: a TL_STROKE", name));
	entries[n] = e;
	names[n]   = name;
	n          = n + 1;
}

int diff3(slot_layer_t *a, slot_layer_t *b, float tol) {
	int d = script_diff_textures(a->texpaint, b->texpaint, tol);
	d     = d + script_diff_textures(a->texpaint_nor, b->texpaint_nor, tol);
	d     = d + script_diff_textures(a->texpaint_pack, b->texpaint_pack, tol);
	return d;
}

// Entry k replayed into new layers evaluating its material and from replay caches, whole and in tiles: byte-equal
void compare(int k, char *where) {
	slot_layer_t *full  = new_layer();
	slot_layer_t *fast  = new_layer();
	slot_layer_t *fullt = new_layer();
	slot_layer_t *fastt = new_layer();
	slot_layer_t *blank = new_layer();
	int           fb    = paint_replay_test_fallbacks();
	int           built = material_cache_replay_stat(1);
	paint_replay_test_set_fast(0);
	int ok = paint_replay_test_into(rec, entries[k], full) && paint_replay_test_tiles(rec, entries[k], fullt, 2, 32);
	paint_replay_test_set_fast(1);
	ok = ok && paint_replay_test_into(rec, entries[k], fast) && paint_replay_test_tiles(rec, entries[k], fastt, 2, 32);
	paint_replay_test_set_fast(0);
	check(ok && paint_replay_test_fallbacks() == fb, string("%s %s: replays both ways, none falls back", names[k], where));
	int painted = diff3(full, blank, 0.0);
	int whole   = diff3(full, fast, 0.0);
	int tiles   = diff3(fullt, fastt, 0.0);
	printf("[rec_fast] %s %s: %d channel values painted, %d differ whole, %d in tiles; %d replay caches rendered", names[k], where, painted, whole, tiles,
	       material_cache_replay_stat(1) - built);
	check(painted > 1000, string("%s %s: the replay paints (%d)", names[k], where, painted));
	check(whole == 0, string("%s %s: composite-fast is byte-equal to the full replay (%d differ)", names[k], where, whole));
	check(tiles == 0, string("%s %s: and in 4 tiles (%d differ)", names[k], where, tiles));
	script_layer_delete(full); // Within the layer cap of iOS
	script_layer_delete(fast);
	script_layer_delete(fullt);
	script_layer_delete(fastt);
	script_layer_delete(blank);
}

void finish() {
	printf("[rec_fast] replay caches: %d rendered, %d unfit, %d over the budget, %d bands, longest %.1f ms of GPU, last %.1f ms, shader %.1f ms",
	       material_cache_replay_stat(1), material_cache_replay_stat(2), material_cache_replay_stat(3), material_cache_replay_stat(4),
	       material_cache_replay_stat(5) / 1000.0, material_cache_replay_stat(7) / 1000.0, material_cache_replay_stat(6) / 1000.0);
	check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
	printf("[rec_fast] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "REC_FAST PASS" : "REC_FAST FAIL");
	script_quit();
}

void next() {
	frame = frame + 1;
	if (phase == 0) {
		if (frame == 2) {
			project_test_set_res(1024, 1024);
		}
		else if (frame == 4) {
			rec   = new_layer();
			m_uv  = make_checker("FastUV", 1);
			m_pos = make_checker("FastPos", 0);
			script_material_set(m_uv);
			make_paint_set_batching(1);
		}
		else if (frame == 6) {
			stroke(0.3);
			note("checker batched");
			make_paint_set_batching(0);
		}
		else if (frame == 8) {
			stroke(0.4);
			note("checker unbatched");
			make_paint_set_batching(1);
			script_material_set(m_pos);
		}
		else if (frame == 10) {
			stroke(0.5);
			note("checker at the position (unfit)");
			script_material_set(m_uv);
		}
		else if (frame == 12) {
			mm_library_count();
			int i = mm_library_find(784);
			printf("[rec_fast] picking %s", mm_library_name(i));
			mm_library_use(i);
			t0    = sys_time();
			phase = 1;
			frame = 0;
		}
	}
	else if (phase == 1) { // Its buffers full
		if (frame > 3 && mm_buffers_stat(11) == 0 && make_material_paint_stat(0) == 0) {
			stroke(0.6);
			note("Old Brick Wall");
			project_test_set_res(2048, 2048);
			phase = 2;
			frame = 0;
		}
		else if (sys_time() - t0 > 120.0) {
			check(false, "Old Brick Wall's buffers become full");
			finish();
			return;
		}
	}
	else if (phase == 2) {
		if (frame == 2) {
			for (int k = 0; k < n; ++k) {
				compare(k, "at 2048");
			}
			check(material_cache_replay_stat(2) >= 1, "the material at the position is unfit");
			printf("[rec_fast] replay caches hold %d KB", material_cache_replay_stat(8));
			project_test_set_res(1024, 1024);
		}
		else if (frame == 4) {
			for (int k = 0; k < n; ++k) {
				compare(k, "at 1024");
			}
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
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.3;
	ctx->brush_spacing = 0.05;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
