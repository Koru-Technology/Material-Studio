#include "global.h"

// Undo and redo notices (history_user_undo): an undo or redo the user asks for leaves a notice with the step's name,
// the redo name is the step redo applies, an undo the app makes itself (history_undo) leaves none.

int fails  = 0;
int checks = 0;
int step   = 0;
int s0     = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[history_notice] %s %s", ok ? "ok  " : "FAIL", name);
}

void next() {
	if (step == 0) {
		s0 = history_test_notice_serial();
		check(app_layer_new(0), "a new layer");
		check(string_equals(app_undo_name(), "New Layer"), string("undo name: %s", app_undo_name()));
		check(string_equals(app_redo_name(), ""), "nothing to redo");
		app_undo();
	}
	else if (step == 1) {
		check(history_test_notice_serial() == s0 + 1, "the undo left a notice");
		check(string_equals(history_test_notice_name(), "New Layer") && !history_test_notice_redo(), string("its name: %s", history_test_notice_name()));
		check(string_equals(app_redo_name(), "New Layer"), string("redo name: %s", app_redo_name()));
		app_redo();
	}
	else if (step == 2) {
		check(history_test_notice_serial() == s0 + 2, "the redo left a notice");
		check(string_equals(history_test_notice_name(), "New Layer") && history_test_notice_redo(), "it is a redo of the new layer");
		history_undo(); // An undo the app makes itself
	}
	else if (step == 3) {
		check(history_test_notice_serial() == s0 + 2, "an undo the app makes leaves no notice");
		app_redo();
		app_redo(); // Nothing left to redo: no notice
	}
	else if (step == 4) {
		check(history_test_notice_serial() == s0 + 3, "a redo with nothing to redo leaves no notice");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[history_notice] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "HISTORY_NOTICE PASS" : "HISTORY_NOTICE FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	script_notify_on_next_frame(next);
}
