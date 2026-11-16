#include "global.h"

// Paint work from outside the stroke model runs isolated (paint_exec.c): a fill layer updated and a fill rendered into
// a target of its own (layers_render_fill_rect, as replays and export tiles render fills) come out the same with
// resolved dabs on as off, and the same again while a scope of the stroke model is open (a materialize from inside a
// stroke): the draw executes against the targets its caller swapped in, not at the scope's end.

int fails  = 0;
int checks = 0;
int frame  = 0;
int was    = 0;

slot_layer_t *fill = NULL;
char         *layer_off = "";
char         *rect_off  = "";

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[paint_exec_isolate] %s %s", ok ? "ok  " : "FAIL", name);
}

char *layer_hash(slot_layer_t *l) {
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

void next() {
	frame = frame + 1;
	if (frame == 2) {
		slot_material_t *m   = script_material_create("isolate_checker");
		ui_node_t       *chk = script_material_create_node("TEX_CHECKER");
		script_material_set_color(chk, 1, 1, 0.9, 0.2, 0.1, 1.0);
		script_material_set_color(chk, 1, 2, 0.1, 0.3, 0.8, 1.0);
		script_material_set_float(chk, 1, 3, 13.0);
		script_material_connect(chk, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
		script_material_connect(chk, 1, script_material_get_node("OUTPUT_MATERIAL_PBR"), 3); // Roughness
		script_material_update();
		fill = script_layer_new_fill();
	}
	else if (frame == 5) {
		script_layer_update(fill);
	}
	else if (frame == 7) {
		layer_off = layer_hash(fill);
		rect_off  = paint_exec_test_fill_rect(fill, 512);
		check(!string_equals(rect_off, ""), "the fill renders into a target of its own");
		script_get_config()->paint_resolved_dabs = 1;
		script_layer_update(fill);
	}
	else if (frame == 9) {
		check(string_equals(layer_hash(fill), layer_off), "the fill layer with resolved dabs as without");
		check(string_equals(paint_exec_test_fill_rect(fill, 512), rect_off), "the fill rendered into a target with resolved dabs as without");
		paint_exec_test_scope(1); // A stroke's scope open
		script_layer_update(fill);
		char *rect = paint_exec_test_fill_rect(fill, 512);
		paint_exec_test_scope(0);
		check(string_equals(rect, rect_off), "the fill rendered into a target inside an open scope");
	}
	else if (frame == 11) {
		check(string_equals(layer_hash(fill), layer_off), "the fill layer updated inside an open scope");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		script_get_config()->paint_resolved_dabs = was;
		printf("[paint_exec_isolate] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "PAINT_EXEC_ISOLATE PASS" : "PAINT_EXEC_ISOLATE FAIL");
		script_quit();
		return;
	}
	script_notify_on_next_frame(next);
}

void main() {
	was                                      = script_get_config()->paint_resolved_dabs;
	script_get_config()->paint_resolved_dabs = 0;
	script_project_new();
	script_notify_on_next_frame(next);
}
