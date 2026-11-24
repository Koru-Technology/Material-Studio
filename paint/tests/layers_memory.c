#include "global.h"

// Memory for layers (iOS): at 8K new layers are added until one is refused. The app refuses it (layers_can_allocate,
// a toast) instead of being ended by iOS, the refusal is for memory (not the layer count cap), a duplicate is refused
// too, and an undo that brings back a deleted layer is not refused. Prints the memory left at each step: the reserve is
// judged from it. Elsewhere the memory is not known: skipped.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int added  = 0;
int count0 = 0;
int refused_before = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[layers_memory] %s %s", ok ? "ok  " : "FAIL", name);
}

void done() {
	printf("[layers_memory] %d checks, %d failures, %d gpu errors", checks, fails, gpu_errors());
	printf(fails == 0 && gpu_errors() == 0 ? "LAYERS_MEMORY PASS" : "LAYERS_MEMORY FAIL");
	script_quit();
}

void next() {
	frame = frame + 1;
	if (frame < 8) { // Deleted textures are freed a few frames later
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		if (layers_memory_available_mb() < 0) {
			printf("[layers_memory] the memory available is not known here (not iOS): skipped");
			done();
			return;
		}
		script_project_new();
	}
	else if (step == 1) {
		project_test_set_res(8192, 8192);
	}
	else if (step == 2) {
		check(config_get_texture_res_x() == 8192, "an 8K project");
		count0         = script_layer_count();
		refused_before = layers_alloc_refusals();
		printf("[layers_memory] %d layers, %d MB available", count0, layers_memory_available_mb());
	}
	else if (step == 3) { // A layer a step until one is refused
		slot_layer_t *l = script_layer_new();
		if (l != NULL) {
			added = added + 1;
			printf("[layers_memory] layer %d added, %d MB available", added, layers_memory_available_mb());
			if (added < 40) {
				script_notify_on_next_frame(next);
				return;
			}
		}
		printf("[layers_memory] refused after %d layers, %d MB available", added, layers_memory_available_mb());
		check(l == NULL, "a layer is refused before the app runs out of memory");
		check(layers_alloc_refusals() > refused_before, "refused for memory, not for the layer count");
		check(added > 0, "a layer fits at first");
		check(script_layer_count() == count0 + added, "the refused layer is not in the project");
	}
	else if (step == 4) {
		int before = layers_alloc_refusals();
		check(script_layer_duplicate(script_layer_at(script_layer_count() - 1)) == NULL, "a duplicate is refused");
		check(layers_alloc_refusals() > before, "the duplicate refused for memory");
		check(script_layer_count() == count0 + added, "no layer added by the refused duplicate");
	}
	else if (step == 5) {
		script_layer_delete(script_layer_at(script_layer_count() - 1));
	}
	else if (step == 6) {
		check(script_layer_count() == count0 + added - 1, "a layer deleted");
		history_user_undo();
	}
	else if (step == 7) {
		check(script_layer_count() == count0 + added, string("undo brings the deleted layer back (%d MB available)", layers_memory_available_mb()));
		done();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
