#include "global.h"

// Every library material with controls (data/material_maker/controls.json), imported with its Variables: one Variable per
// control, every target found, the same look as without them at the defaults, each control changes the material when it
// moves, and the material compiles without shader failures with every control moved.
//
// Long: run in shards, each in its own app, with full Material Maker buffers rendered as they are asked for:
//   sed 's/^int shard  = 0;/int shard  = 2;/; s/^int shards = 1;/int shards = 6;/' tests/material_controls_all.c > shard2.c
//   MM_BUFFERS_SYNC=1 MaterialStudio --script $PWD/shard2.c
// Lines start with [bulk]; a failing material is listed with FAIL and the reasons.

int shard  = 0;
int shards = 1;

int step     = 0;
int wait     = 0;
int at       = 0; // Index in the controls list
int tested   = 0;
int failed   = 0;
int controls = 0;
int inert    = 0; // Controls that change nothing visible

slot_material_t *m      = NULL; // With controls
slot_material_t *p      = NULL; // Without
any_array_t     *trash  = NULL; // Previews, freed a step later
int              before = 0;
char            *why    = "";

// Material node outputs compared: Base Color, Height, Roughness, Normal Map, Metallic, Emission, Opacity, Occlusion
int sockets[8] = {0, 7, 3, 5, 4, 6, 1, 2};

int mine(int i) {
	return i - (i / shards) * shards == shard;
}

gpu_texture_t *preview(slot_material_t *s, int socket) {
	script_material_set(s);
	gpu_texture_t *t = mm_import_preview(mm_import_material_node(s), socket, 64);
	any_array_push(trash, t);
	return t;
}

// Pixels per thousand that differ between the material with controls and the one without, on one output
int changed(int socket) {
	gpu_texture_t *b = preview(p, socket);
	gpu_texture_t *a = preview(m, socket);
	return mm_import_image_changed(a, b);
}

// The first output that differs, -1 when none does
int differs() {
	for (int k = 0; k < 8; ++k) {
		if (changed(sockets[k]) > 3) {
			return sockets[k];
		}
	}
	return -1;
}

// Moves a Variable away from its default: the other end of its range, the next option, inverted colors
void move(ui_node_t *v) {
	int   kind = variable_kind(v);
	float x    = variable_value(v, 0);
	if (kind == 0 || kind == 1) {
		float lo = variable_param(v, 0);
		float hi = variable_param(v, 1);
		float to = lo + (hi - lo) * 0.8;
		if (x > lo + (hi - lo) * 0.5) {
			to = lo + (hi - lo) * 0.2;
		}
		if (kind == 1) {
			to = floor(to + 0.5);
			if (to == x) {
				to = x + 1.0 <= hi ? x + 1.0 : x - 1.0;
			}
		}
		variable_set_value(v, 0, to);
	}
	else if (kind == 2) {
		variable_set_value(v, 0, x != 0.0 ? 0.0 : 1.0);
	}
	else if (kind == 3) {
		float n = variable_param(v, 3);
		variable_set_value(v, 0, x + 1.0 < n ? x + 1.0 : 0.0);
	}
	else if (kind == 4) {
		for (int c = 0; c < 3; ++c) {
			float y = variable_value(v, c);
			variable_set_value(v, c, y > 0.5 ? y * 0.2 : 1.0 - (1.0 - y) * 0.2);
		}
	}
	else if (kind == 5) {
		int len = (int)variable_param(v, 4);
		for (int s = 0; s + 5 <= len; s += 5) {
			for (int c = 0; c < 3; ++c) {
				float y = variable_value(v, s + c);
				variable_set_value(v, s + c, y > 0.5 ? y * 0.2 : 1.0 - (1.0 - y) * 0.2);
			}
		}
	}
}

// The values of a Variable, to put it back
any_array_t *save(ui_node_t *v) {
	any_array_t *a   = any_array_create(0);
	int          len = (int)variable_param(v, 4);
	for (int i = 0; i < len; ++i) {
		any_array_push(a, string("%f", variable_value(v, i)));
	}
	return a;
}

void restore(ui_node_t *v, any_array_t *a) {
	for (int i = 0; i < a->length; ++i) {
		variable_set_value(v, i, parse_float(a->buffer[i]));
	}
}

void empty_trash() {
	if (trash != NULL) {
		for (int i = 0; i < trash->length; ++i) {
			if (trash->buffer[i] != NULL) {
				mm_import_texture_free(trash->buffer[i]);
			}
		}
	}
	trash = any_array_create(0);
}

void fail(char *reason) {
	why = string("%s %s;", why, reason);
}

// Imports the material twice, with and without its controls
void step_import() {
	empty_trash();
	int id = mm_controls_material_id(at);
	why    = "";
	before = gpu_shader_failures();
	m      = mm_import_library(id);
	char *report = string_copy(mm_import_report());
	p      = script_material_create(string("plain_%d", id));
	mm_import_library_into(id, p);
	int want = mm_controls_material_controls(at);
	int vars = m != NULL ? variable_count(m->canvas) : -1;
	controls = controls + want;
	if (vars != want) {
		fail(string("%d Variables of %d controls", vars, want));
	}
	if (string_index_of(report, "control ") >= 0) {
		fail("a target was not found");
	}
}

// Defaults look the same, then each control alone changes the material
void step_controls() {
	if (m == NULL) {
		return;
	}
	int same = changed(0);
	if (same < 0 || same > 10) {
		fail(string("looks different at the defaults (%d per thousand of the base color)", same));
	}
	int k = differs();
	if (k >= 0 && k != 0) {
		fail(string("output %d looks different at the defaults", k));
	}
	ui_node_canvas_t *c = m->canvas;
	for (int i = 0; i < variable_count(c); ++i) {
		ui_node_t   *v    = variable_at(c, i);
		any_array_t *keep = save(v);
		move(v);
		variable_propagate_canvas(c);
		if (differs() < 0) {
			inert = inert + 1;
			fail(string("%s changes nothing", variable_name(v)));
		}
		restore(v, keep);
	}
	// Every control moved at once, the paint shader
	for (int i = 0; i < variable_count(c); ++i) {
		move(variable_at(c, i));
	}
	script_material_set(m);
	script_material_update();
}

void step_check() {
	int id = mm_controls_material_id(at);
	if (gpu_shader_failures() != before) {
		fail(string("%d shader failures", gpu_shader_failures() - before));
	}
	tested = tested + 1;
	if (string_length(why) > 0) {
		failed = failed + 1;
		printf("[bulk] FAIL %d %s:%s", id, mm_import_library_name(id), why);
	}
	else {
		printf("[bulk] ok   %d %s", id, mm_import_library_name(id));
	}
	if (m != NULL) {
		script_material_delete(m);
	}
	if (p != NULL) {
		script_material_delete(p);
	}
	m = NULL;
	p = NULL;
}

void next() {
	if (wait > 0) {
		wait = wait - 1;
		script_notify_on_next_frame(next);
		return;
	}
	while (at < mm_controls_material_count() && !mine(at)) {
		at = at + 1;
	}
	if (at >= mm_controls_material_count()) {
		printf("[bulk] shard %d of %d: %d materials, %d failed, %d controls, %d change nothing", shard, shards, tested, failed, controls, inert);
		printf(failed == 0 ? "BULK PASS" : "BULK FAIL");
		script_quit();
		return;
	}
	wait = 2;
	if (step == 0) {
		step_import();
	}
	else if (step == 1) {
		step_controls();
	}
	else {
		step_check();
		at = at + 1;
		step = -1;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
