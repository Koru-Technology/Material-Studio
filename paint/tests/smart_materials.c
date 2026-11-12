#include "global.h"

// Smart materials: a fill layer with its mask, or a group, saved and put into another project, undoable

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int count0 = 0;
int layers = 0;

slot_material_t *checker = NULL;
slot_material_t *red     = NULL;
slot_layer_t    *fill    = NULL;
slot_layer_t    *root    = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[smart] %s %s", ok ? "ok  " : "FAIL", name);
}

slot_material_t *make_material(char *name, int checkered) {
	slot_material_t *m = script_material_create(name);
	if (checkered) {
		ui_node_t *c = script_material_create_node_at("TEX_CHECKER", -300.0, 0.0);
		ui_node_t *t = script_material_create_node_at("TEX_COORD", -600.0, 0.0);
		script_material_connect(t, 2, c, 0);
		script_material_set_float(c, 1, 3, 8.0);
		script_material_connect(c, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	}
	else {
		ui_node_t *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
		script_material_set_color(rgb, 0, 0, 0.9, 0.1, 0.1, 1.0);
		script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	}
	script_material_update();
	return m;
}

// Texels with something in them (red is byte 0 or 2 depending on the backend)
int lit(slot_layer_t *l) {
	buffer_t *p = script_get_texture_pixels(l->texpaint);
	int       n = 0;
	for (int i = 0; i < p->length; i += 16) {
		int r = buffer_get_u8(p, i);
		int b = buffer_get_u8(p, i + 2);
		if (r > 8 || b > 8) {
			n = n + 1;
		}
	}
	return n;
}

slot_layer_t *mask_of(slot_layer_t *l) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *m = script_layer_at(i);
		if (m->parent == l && m->texpaint_nor == NULL && m->texpaint != NULL) {
			return m;
		}
	}
	return NULL;
}

slot_layer_t *child_of(slot_layer_t *g) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *c = script_layer_at(i);
		if (c->parent == g && c->texpaint_nor != NULL) {
			return c;
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
		int old = smart_materials_find("test_smart");
		if (old >= 0) {
			smart_materials_delete(old);
		}
		old = smart_materials_find("test_smart_group");
		if (old >= 0) {
			smart_materials_delete(old);
		}
		count0 = smart_materials_count();
		script_material_set(checker);
		fill = script_layer_new_fill();
	}
	else if (step == 1) {
		fill->scale = 2.0;
		fill->angle = 30.0;
		script_layer_update(fill);
		for (int i = 0; i < 20; ++i) { // Paints the mask
			script_paint(0.35 + i * 0.015, 0.45 + i * 0.004);
		}
		script_paint_end();
	}
	else if (step == 2) {
		check(mask_of(fill) != NULL && lit(mask_of(fill)) > 100, "fill layer has a painted mask");
		check(smart_materials_save(fill, "test_smart"), "smart material saved");
		check(smart_materials_count() == count0 + 1 && smart_materials_find("test_smart") >= 0, "smart material listed");

		// A group holding a red fill layer
		script_material_set(red);
		slot_layer_t *f2 = script_layer_new_fill();
		slot_layer_t *g  = script_layer_new_group(f2);
		check(g != NULL && f2->parent == g, "group made");
		check(smart_materials_save(g, "test_smart_group"), "group saved");
		script_project_new();
	}
	else if (step == 3) {
		layers = script_layer_count();
		root   = smart_materials_apply("test_smart");
	}
	else if (step == 4) {
		check(root != NULL && ctx->layer == root, "smart material put in a new project, top layer selected");
		check(root != NULL && root->fill_material != NULL && string_equals(root->fill_material->canvas->name, "smart_checker"), "material came along");
		check(root != NULL && root->scale == 2.0 && root->angle == 30.0, "uv scale and angle came along");
		slot_layer_t *m = root != NULL ? mask_of(root) : NULL;
		check(m != NULL, "mask came along");
		check(m != NULL && lit(m) == 0, "mask starts black, ready to paint");
		check(root != NULL && lit(root) > 1000, "fill layer is filled with its material");
		check(script_layer_count() == layers + 2, "two layers added");
		history_undo();
	}
	else if (step == 5) {
		check(script_layer_count() == layers, "undo removes the layer and its mask");
		history_redo();
	}
	else if (step == 6) {
		check(script_layer_count() == layers + 2, "redo puts them back");
		slot_layer_t *top = script_layer_at(script_layer_count() - 1);
		check(top->fill_material != NULL && top->scale == 2.0, "redone layer has its settings");
		layers = script_layer_count();
		root   = smart_materials_apply("test_smart_group");
	}
	else if (step == 7) {
		check(root != NULL && root->texpaint == NULL, "group put in");
		slot_layer_t *c = root != NULL ? child_of(root) : NULL;
		check(c != NULL && c->fill_material != NULL && string_equals(c->fill_material->canvas->name, "smart_red"), "group keeps its fill layer");
		check(script_layer_count() == layers + 2, "group and layer added");
		smart_materials_delete(smart_materials_find("test_smart"));
		smart_materials_delete(smart_materials_find("test_smart_group"));
		check(smart_materials_count() == count0, "smart materials deleted");
	}
	else {
		printf("[smart] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "SMART PASS" : "SMART FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults(); // A plain round brush, whatever the user left picked
	script_project_new();
	red     = make_material("smart_red", 0);
	checker = make_material("smart_checker", 1);
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.4;
	ctx->brush_opacity = 1.0;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
