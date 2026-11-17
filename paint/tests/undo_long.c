#include "global.h"

// Undo over a long session: 1000 strokes on 3 layers and a mask with small budgets (2 MB of RAM for the store, under the
// preference's minimum, and 64 MB of GPU undo tiles). Old tile blobs leave RAM for the journal, GPU tiles of old steps are released, and undoing to the
// start and redoing to the end still gives every layer back byte for byte.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int done   = 0; // Strokes painted
int undone = 0;

slot_layer_t *a = NULL;
slot_layer_t *b = NULL;
slot_layer_t *c = NULL;
slot_layer_t *m = NULL;

char *h_start  = NULL;
char *h_mid    = NULL; // After 500
char *h_end    = NULL;
int   ram_was  = 0;
int   gpu_was  = 0;
float t0       = 0.0;
int   max_ram  = 0; // KB, over the session
int   max_gpu  = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[undo_long] %s %s", ok ? "ok  " : "FAIL", name);
}

char *layer_hash(slot_layer_t *l) {
	if (l->texpaint_nor == NULL) {
		return script_texture_hash(l->texpaint);
	}
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

char *all_hash() {
	return string("%s | %s | %s | %s", layer_hash(a), layer_hash(b), layer_hash(c), layer_hash(m));
}

void stroke(int i) {
	slot_layer_t *l = i % 4 == 0 ? a : i % 4 == 1 ? b : i % 4 == 2 ? c : m;
	script_layer_set(l);
	float y = 0.1 + (i % 41) * 0.019;
	float x = 0.1 + (i % 13) * 0.055;
	for (int k = 0; k < 8; ++k) {
		float t = k / 7.0;
		script_paint(x + 0.12 * t, y + 0.04 * t);
	}
	script_paint_end();
}

void stats(char *when) {
	int ram = (int)hstore_test_stat(1);
	int gpu = history_tiles_test_memory_kb();
	max_ram = ram > max_ram ? ram : max_ram;
	max_gpu = gpu > max_gpu ? gpu : max_gpu;
	printf("[undo_long] %s: %d blobs, ram %d KB (budget %d), journal %d KB, dead %d KB, gpu tiles %d KB for %d steps, checkpoints %d %d %d %d", when,
	       (int)hstore_test_stat(0), ram, (int)hstore_test_stat(5), (int)hstore_test_stat(2), (int)hstore_test_stat(4), gpu, history_tiles_test_steps(),
	       timeline_test_checkpoints(a), timeline_test_checkpoints(b), timeline_test_checkpoints(c), timeline_test_checkpoints(m));
}

void finish() {
	script_get_config()->history_ram_mb = ram_was;
	script_get_config()->undo_memory_mb = gpu_was;
	printf("[undo_long] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "UNDO_LONG PASS" : "UNDO_LONG FAIL");
	script_quit();
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		a = script_layer_new();
		b = script_layer_new();
		c = script_layer_new();
		m = script_layer_new_mask(a, 1);
	}
	else if (step == 1) {
		h_start = all_hash();
		t0      = sys_time();
		step    = 2;
	}
	if (step == 2) { // 20 strokes a frame
		for (int i = 0; i < 20; ++i) {
			stroke(done);
			done = done + 1;
		}
		if (done % 200 == 0) {
			timeline_finalize();
			stats(string("after %d strokes", done));
		}
		if (done == 500) {
			h_mid = all_hash();
		}
		if (done < 1000) {
			script_notify_on_next_frame(next);
			return;
		}
		h_end = all_hash();
		printf("[undo_long] painting took %d ms", (int)((sys_time() - t0) * 1000.0));
		check(max_ram <= (int)hstore_test_stat(5), string("store RAM stays within its budget (max %d KB)", max_ram));
		check((int)hstore_test_stat(2) > 0, "old blobs went to the journal");
		check(max_gpu <= 64 * 1024 + 64 * 1024 / 4, string("GPU tiles stay within the undo memory (max %d KB)", max_gpu));
		check(history_tiles_test_steps() < 1000, string("old steps let their GPU tiles go (%d keep some)", history_tiles_test_steps()));
		t0   = sys_time();
		step = 3;
		script_notify_on_next_frame(next);
		return;
	}
	else if (step == 3) { // 100 undos a frame
		for (int i = 0; i < 100; ++i) {
			history_undo();
		}
		undone = undone + 100;
		if (undone == 500) {
			history_flush_restores(NULL);
			check(string_equals(all_hash(), h_mid), "500 undos give the layers after stroke 500 back");
		}
		if (undone < 1000) {
			script_notify_on_next_frame(next);
			return;
		}
		history_flush_restores(NULL);
		printf("[undo_long] 1000 undos took %d ms", (int)((sys_time() - t0) * 1000.0));
		check(string_equals(all_hash(), h_start), "1000 undos give the layers before the strokes back");
		stats("at the start");
		for (int i = 0; i < 1000; ++i) {
			history_redo();
		}
		history_flush_restores(NULL);
		check(string_equals(all_hash(), h_end), "1000 redos give the layers after the last stroke back");
		stats("at the end");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		finish();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	ram_was                             = script_get_config()->history_ram_mb;
	gpu_was                             = script_get_config()->undo_memory_mb;
	script_get_config()->history_ram_mb = 2;
	script_get_config()->undo_memory_mb = 64;
	script_brush_defaults();
	script_project_new();
	slot_material_t *mat = script_material_create("long_red");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.9, 0.2, 0.1, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.1;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
