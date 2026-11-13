#include "global.h"

// Materials keep their texture scale and rotation for paint layers, apart from the brush tip angle,
// fill layers start with them, projects save them and older projects open with the defaults

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

char            *tmp_root = "/tmp/materialstudio_uv";
char            *file_new = NULL;
char            *file_old = NULL;
slot_material_t *checker  = NULL;
slot_material_t *red      = NULL;
slot_layer_t    *paint    = NULL;
buffer_t        *base     = NULL;
buffer_t        *scaled   = NULL;
buffer_t        *rotated  = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[uv] %s %s", ok ? "ok  " : "FAIL", name);
}

buffer_t *copy_pixels(slot_layer_t *l) {
	buffer_t *p = script_get_texture_pixels(l->texpaint);
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

int diff(buffer_t *a, buffer_t *b) {
	int n = 0;
	for (int i = 0; i < a->length; i += 4) {
		int d = buffer_get_u8(a, i) - buffer_get_u8(b, i);
		int e = buffer_get_u8(a, i + 2) - buffer_get_u8(b, i + 2);
		if (d > 8 || d < -8 || e > 8 || e < -8) {
			n = n + 1;
		}
	}
	return n;
}

// The same stroke each time, the layer is copied and the stroke undone
buffer_t *stroke_copy() {
	for (int i = 0; i < 20; ++i) {
		script_paint(0.35 + i * 0.015, 0.45 + i * 0.004);
	}
	script_paint_end();
	return NULL;
}

slot_layer_t *find_fill() {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->fill_material != NULL) {
			return l;
		}
	}
	return NULL;
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame          = 0;
	context_t *ctx = script_get_context();

	if (step == 0) {
		paint = ctx->layer;
		check(checker->uv_scale == 1.0 && checker->uv_angle == 0.0, "new materials start at scale 1, rotation 0");
		stroke_copy();
	}
	else if (step == 1) {
		base = copy_pixels(paint);
		history_undo();
		checker->uv_scale = 3.0;
	}
	else if (step == 2) {
		stroke_copy();
	}
	else if (step == 3) {
		scaled = copy_pixels(paint);
		int d  = diff(base, scaled);
		check(d > 1000, string("material uv scale changes the painted texture (%d)", d));
		history_undo();
		checker->uv_angle = 45.0;
	}
	else if (step == 4) {
		stroke_copy();
	}
	else if (step == 5) {
		rotated = copy_pixels(paint);
		int d   = diff(scaled, rotated);
		check(d > 1000, string("material uv rotation changes the painted texture (%d)", d));
		history_undo();
		ctx->brush_angle = 90.0; // A round tip turned, the texture stays
		context_select_tool(TOOL_TYPE_BRUSH);
	}
	else if (step == 6) {
		stroke_copy();
	}
	else if (step == 7) {
		buffer_t *tipped = copy_pixels(paint);
		int       d      = diff(rotated, tipped);
		check(d * 50 < diff(scaled, rotated), string("tip angle leaves the texture alone (%d)", d));
		history_undo();
		ctx->brush_angle = 0.0;
		script_material_set(red);
	}
	else if (step == 8) {
		script_material_set(checker);
		check(ctx->material == checker && checker->uv_scale == 3.0 && checker->uv_angle == 45.0, "picking the material again keeps its scale and rotation");
		script_layer_new_fill();
	}
	else if (step == 9) {
		slot_layer_t *f = find_fill();
		check(f != NULL && f->scale == 3.0 && f->angle == 45.0, "fill layer starts with the material's scale and rotation");
		project_filepath_set(file_new);
		project_save(false);
	}
	else if (step == 10) {
		check(iron_file_exists(file_new), "project saved");
		// The same file marked as version 26, before materials had a scale and rotation
		buffer_t *b = script_load_blob(file_new);
		buffer_set_u8(b, 23, 54); // "27" -> "26"
		iron_file_save_bytes(file_old, b, b->length);
		script_project_new();
	}
	else if (step == 11) {
		script_project_open(file_new);
	}
	else if (step == 12) {
		slot_layer_t *f = find_fill();
		check(f != NULL && f->fill_material != NULL, "saved project opens with its fill layer");
		check(f != NULL && f->fill_material->uv_scale == 3.0 && f->fill_material->uv_angle == 45.0, "material scale and rotation are saved");
		check(f != NULL && f->scale == 3.0 && f->angle == 45.0, "fill layer scale and rotation are saved");
		script_project_open(file_old);
	}
	else if (step == 13) {
		slot_layer_t *f = find_fill();
		check(f != NULL && f->fill_material != NULL, "version 26 project opens");
		check(f != NULL && f->fill_material->uv_scale == 1.0 && f->fill_material->uv_angle == 0.0, "version 26 materials get scale 1, rotation 0");
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[uv] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "UV PASS" : "UV FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults(); // A plain round brush, whatever the user left picked
	char *save = script_save_path();
	if (string_index_of(save, "/var/mobile/") >= 0) { // ios, /tmp is outside the sandbox
		tmp_root = string("%s/materialstudio_uv", save);
	}
	iron_create_directory(tmp_root);
	file_new = string("%s/uv_new.arm", tmp_root);
	file_old = string("%s/uv_old.arm", tmp_root);

	script_project_new();

	red          = script_material_create("uv_red");
	ui_node_t *r = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(r, 0, 0, 0.9, 0.1, 0.1, 1.0);
	script_material_connect(r, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();

	checker      = script_material_create("uv_checker");
	ui_node_t *c = script_material_create_node_at("TEX_CHECKER", -300.0, 0.0);
	ui_node_t *t = script_material_create_node_at("TEX_COORD", -600.0, 0.0);
	script_material_connect(t, 2, c, 0);
	script_material_set_float(c, 1, 3, 8.0);
	script_material_connect(c, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();

	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.4;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
