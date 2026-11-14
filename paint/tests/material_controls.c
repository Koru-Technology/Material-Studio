#include "global.h"

// Material controls (nodes_material/variable_node.c, mm_controls.c): library materials imported with their controls
// (data/material_maker/controls.json) get a Variable per control wired to the parameters it drives, look as they did
// at the defaults, follow the controls' mappings (Map Range) and choices (Select Value), compile without shader failures
// when a control moves, open in the Material Studio, survive save and load. Export Parameter wires a Variable to a
// parameter.

int fails  = 0;
int checks = 0;
int step   = 0;
int wait   = 0;

// The pilot: id and number of controls
int ids[5]      = {30, 133, 1271, 1391, 1714};
int controls[5] = {5, 3, 5, 4, 5};
int count       = 5;

any_array_t *mats   = NULL; // With controls
any_array_t *plains = NULL; // The same imported without
any_array_t *prints = NULL;
any_array_t *names  = NULL;
int          failures_before = 0;
int          compiles        = 0;
char        *file            = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[controls] %s %s", ok ? "ok  " : "FAIL", name);
}

int near(float a, float b) {
	float d = a - b;
	return d < 0.001 && d > -0.001;
}

slot_material_t *mat(int id) {
	for (int i = 0; i < count; ++i) {
		if (ids[i] == id) {
			return mats->buffer[i];
		}
	}
	return NULL;
}

// Variables (kind, name, values) and links of a material, to compare after reload
char *fingerprint(slot_material_t *m) {
	ui_node_canvas_t *c = m->canvas;
	char             *s = string("%d nodes %d links;", c->nodes->length, c->links->length);
	for (int i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		if (string_equals(n->type, "VARIABLE")) {
			s = string("%s%s %d %s %.4f;", s, n->name, variable_kind(n), variable_name(n), variable_value(n, 0));
		}
	}
	return s;
}

// Mean difference of the base color previews of a material with and without its controls
int base_diff(slot_material_t *a, slot_material_t *b) {
	script_material_set(a);
	gpu_texture_t *ta = mm_import_preview(mm_import_material_node(a), 0, 64);
	script_material_set(b);
	gpu_texture_t *tb = mm_import_preview(mm_import_material_node(b), 0, 64);
	return mm_import_image_diff(ta, tb);
}

void step_import() {
	mats            = any_array_create(0);
	plains          = any_array_create(0);
	failures_before = gpu_shader_failures();
	for (int i = 0; i < count; ++i) {
		int              id = ids[i];
		slot_material_t *m  = mm_import_library(id);
		any_array_push(mats, m);
		char *report = string_copy(mm_import_report());
		int   vars   = m != NULL ? variable_count(m->canvas) : -1;
		check(vars == controls[i], string("%d %s: %d Variables of %d controls", id, mm_import_library_name(id), vars, controls[i]));
		check(string_index_of(report, "control ") < 0, string("%d: every control's targets found", id));
		slot_material_t *p = script_material_create(string("plain_%d", id));
		mm_import_library_into(id, p);
		any_array_push(plains, p);
		check(variable_count(p->canvas) == 0, string("%d imported into a material (bulk checks) has no controls", id));
	}
}

void step_defaults() {
	check(gpu_shader_failures() == failures_before, string("the pilot compiles without shader failures (%d)", gpu_shader_failures() - failures_before));
	for (int i = 0; i < count; ++i) {
		int d = base_diff(mats->buffer[i], plains->buffer[i]);
		check(d >= 0 && d <= 1, string("%d looks the same with its controls at their defaults (difference %d)", ids[i], d));
	}
}

void step_mappings() {
	ui_node_canvas_t *c    = mat(1714)->canvas;
	ui_node_t        *moss = variable_find(c, "moss_amount");
	check(moss != NULL && variable_kind(moss) == 0, "1714 Moss Amount is a Number");
	float moss0 = variable_value(moss, 0);
	check(near(moss0, (0.6 - mm_node_param_value(c, "tones_step_9", "value", -1, 0)) / 0.5),
	      string("1714 Moss Amount's default maps back to the graph's value (%.3f)", moss0));
	variable_set_value(moss, 0, 0.0);
	variable_propagate_canvas(c);
	float at0 = mm_node_param_value(c, "tones_step_9", "value", -1, 0);
	variable_set_value(moss, 0, 1.0);
	variable_propagate_canvas(c);
	float at1 = mm_node_param_value(c, "tones_step_9", "value", -1, 0);
	check(near(at0, 0.6) && near(at1, 0.1), string("1714 Moss Amount maps 0..1 to 0.6..0.1 through Map Range (%.3f, %.3f)", at0, at1));
	variable_set_value(moss, 0, moss0);

	ui_node_t *cols = variable_find(c, "shingle_columns");
	check(cols != NULL && variable_kind(cols) == 1, "1714 Shingle Columns is a Whole Number");
	float cols0 = variable_value(cols, 0);
	variable_set_value(cols, 0, 8.0);
	variable_propagate_canvas(c);
	float tx = mm_node_param_value(c, "tiler", "tx", -1, 0);
	float sx = mm_node_param_value(c, "tiler", "scale_x", -1, 0);
	check(near(tx, 8.0) && near(sx, 0.24), string("1714 Shingle Columns 8 sets 8 columns of scale 0.24 (%.3f, %.3f)", tx, sx));
	variable_set_value(cols, 0, cols0);

	ui_node_t *moss_color = variable_find(c, "moss_color");
	check(moss_color != NULL && variable_kind(moss_color) == 4, "1714 Moss Color is a Color");
	float other = mm_node_param_value(c, "colorize_5", "gradient", 1, 0);
	variable_set_value(moss_color, 0, 1.0);
	variable_set_value(moss_color, 1, 0.0);
	variable_set_value(moss_color, 2, 0.0);
	variable_propagate_canvas(c);
	check(near(mm_node_param_value(c, "colorize_5", "gradient", 0, 0), 1.0) && near(mm_node_param_value(c, "colorize_5", "gradient", 1, 0), other),
	      "1714 Moss Color sets the color of the gradient's first stop only");

	ui_node_canvas_t *t       = mat(1271)->canvas;
	ui_node_t        *pattern = variable_find(t, "tile_pattern");
	check(pattern != NULL && variable_kind(pattern) == 3, "1271 Tile Pattern is a Choice");
	float p0 = variable_value(pattern, 0);
	variable_set_value(pattern, 0, 1.0); // Basket Weave
	variable_propagate_canvas(t);
	int all = 1;
	char *bricks[5] = {"bricks", "bricks_2", "bricks_3", "bricks_4", "bricks_5"};
	for (int i = 0; i < 5; ++i) {
		all = all && near(mm_node_param_value(t, bricks[i], "pattern", -1, 0), 4.0);
	}
	check(all, "1271 Tile Pattern Basket Weave sets the pattern of the five bricks nodes (through Select Value)");
	variable_set_value(pattern, 0, p0);
	variable_propagate_canvas(t);
	variable_propagate_canvas(c);
}

// A moved control compiles without failures and changes the material
void step_change() {
	failures_before = gpu_shader_failures();
	slot_material_t *m = mat(133);
	ui_node_t       *v = variable_find(m->canvas, "denim_color");
	variable_set_value(v, 0, 0.9);
	variable_set_value(v, 1, 0.1);
	variable_set_value(v, 2, 0.1);
	script_material_set(m);
	script_material_update();
}

void step_change_check() {
	check(gpu_shader_failures() == failures_before, "133 recompiles without shader failures after Denim Color moves");
	int d = base_diff(mat(133), plains->buffer[1]);
	check(d > 2, string("133 with a red Denim Color differs from the original (difference %d)", d));
}

void step_studio() {
	slot_material_t *m = mat(1391);
	material_studio_open(m);
	check(material_studio_is_open(), "a library material with Variables opens in the Material Studio");
	compiles = material_studio_compiles();
}

void step_studio_change() {
	ui_node_t *v = variable_find(mat(1391)->canvas, "dent_count");
	check(v != NULL, "1391 has Dent Count");
	if (v != NULL) {
		variable_set_value(v, 0, variable_value(v, 0) + 4.0);
	}
}

void step_studio_check() {
	check(material_studio_compiles() > compiles, string("moving a Variable compiles the Studio preview (%d compiles)", material_studio_compiles() - compiles));
	check(gpu_shader_failures() == failures_before, "the Studio preview compiles without shader failures");
	material_studio_close();
}

void step_export() {
	slot_material_t *m = script_material_create("export_2");
	mm_import_library_into(2, m);
	ui_node_t *colorize = mm_node_named(m->canvas, "colorize");
	float      before   = mm_node_param_value(m->canvas, "colorize", "gradient", 0, 0);
	ui_node_t *v        = colorize != NULL ? mm_node_export_param(m->canvas, colorize, "gradient", -1) : NULL;
	check(v != NULL && variable_kind(v) == 5 && variable_count(m->canvas) == 1, "Export Parameter makes a Gradient Variable of a gradient");
	variable_propagate_canvas(m->canvas);
	check(near(mm_node_param_value(m->canvas, "colorize", "gradient", 0, 0), before), "the exported Variable keeps the parameter's value");
	ui_node_t *stop = colorize != NULL ? mm_node_export_param(m->canvas, colorize, "gradient", 1) : NULL;
	check(stop != NULL && variable_kind(stop) == 4, "Export Parameter of a gradient stop makes a Color Variable");
}

void step_save() {
	prints = any_array_create(0);
	names  = any_array_create(0);
	for (int i = 0; i < count; ++i) {
		slot_material_t *m = mats->buffer[i];
		any_array_push(names, string_copy(m->canvas->name));
		any_array_push(prints, fingerprint(m));
	}
	file = string("%s/material_controls_test.arm", script_save_path());
	project_filepath_set(file);
	project_save(0);
}

void step_reload_check() {
	int same = 0;
	for (int i = 0; i < names->length; ++i) {
		slot_material_t *m = script_get_material(names->buffer[i]);
		char            *f = m != NULL ? fingerprint(m) : "";
		if (string_equals(f, prints->buffer[i])) {
			same = same + 1;
		}
		else {
			printf("[controls] %s before %s", names->buffer[i], prints->buffer[i]);
			printf("[controls] %s after  %s", names->buffer[i], f);
		}
	}
	check(same == names->length, string("Variables and their links survive save and load (%d of %d)", same, names->length));
	check(material_studio_is_open() == 0, "the Studio closed");
}

void next() {
	if (wait > 0) {
		wait = wait - 1;
		script_notify_on_next_frame(next);
		return;
	}
	wait = 8; // Frames for compiles and Material Maker buffers to land
	if (step == 0) {
		step_import();
	}
	else if (step == 1) {
		step_defaults();
	}
	else if (step == 2) {
		step_mappings();
	}
	else if (step == 3) {
		step_change();
	}
	else if (step == 4) {
		step_change_check();
	}
	else if (step == 5) {
		step_studio();
	}
	else if (step == 6) {
		step_studio_change();
	}
	else if (step == 7) {
		step_studio_check();
	}
	else if (step == 8) {
		step_export();
	}
	else if (step == 9) {
		step_save();
	}
	else if (step == 10) {
		script_project_new();
	}
	else if (step == 11) {
		script_project_open(file);
	}
	else if (step == 12) {
		step_reload_check();
	}
	else {
		printf("[controls] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "CONTROLS PASS" : "CONTROLS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
