#include "global.h"

// Brush presets: the tool, the brush settings and the material come back, in another project too

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int count0 = 0;

slot_material_t *fur = NULL;
slot_material_t *red = NULL;

// The brush settings of the session, put back at the end: applying a preset saves them to the config
float saved_radius   = 0.0;
float saved_opacity  = 0.0;
float saved_hardness = 0.0;
float saved_spacing  = 0.0;
float saved_angle    = 0.0;
float saved_flow     = 0.0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[preset] %s %s", ok ? "ok  " : "FAIL", name);
}

int near(float a, float b) {
	float d = a - b;
	return d < 0.001 && d > -0.001;
}

slot_material_t *make_material(char *name, float r, float g, float b) {
	slot_material_t *m   = script_material_create(name);
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, r, g, b, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	return m;
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
		int old = brush_presets_find("test_fur");
		if (old >= 0) {
			brush_presets_delete(old);
		}
		count0 = brush_presets_count();

		script_material_set(fur);
		fur->uv_scale = 2.5;
		fur->uv_angle = 30.0;
		context_select_tool(TOOL_TYPE_BRUSH);
		ctx->brush_radius  = 0.33;
		ctx->brush_opacity = 0.7;
		ctx->brush_hardness = 0.4;
		ctx->brush_spacing = 0.2;
		ctx->brush_angle   = 15.0;
		int i              = brush_presets_add("test_fur");
		check(i >= 0 && brush_presets_count() == count0 + 1, "preset saved");
		check(brush_presets_find("test_fur") == i, "preset found by name");
	}
	else if (step == 1) {
		// Everything else
		script_material_set(red);
		fur->uv_scale       = 1.0;
		fur->uv_angle       = 0.0;
		ctx->brush_radius   = 0.9;
		ctx->brush_opacity  = 0.2;
		ctx->brush_hardness = 1.0;
		ctx->brush_spacing  = 0.0;
		ctx->brush_angle    = 0.0;
		context_select_tool(TOOL_TYPE_ERASER);
	}
	else if (step == 2) {
		brush_presets_apply(brush_presets_find("test_fur"));
	}
	else if (step == 3) {
		check(ctx->tool == TOOL_TYPE_BRUSH, "tool restored");
		check(near(ctx->brush_radius, 0.33) && near(ctx->brush_opacity, 0.7) && near(ctx->brush_hardness, 0.4), "radius, opacity, hardness restored");
		check(near(ctx->brush_spacing, 0.2) && near(ctx->brush_angle, 15.0), "spacing and tip angle restored");
		check(ctx->material == fur, "material selected");
		check(near(fur->uv_scale, 2.5) && near(fur->uv_angle, 30.0), "material scale and rotation restored");
		script_project_new(); // No preset_fur material here
	}
	else if (step == 4) {
		brush_presets_apply(brush_presets_find("test_fur"));
	}
	else if (step == 5) {
		slot_material_t *m = ctx->material;
		check(m != NULL && string_index_of(m->canvas->name, "preset_fur") == 0, "material brought into a new project");
		check(m != NULL && near(m->uv_scale, 2.5) && near(m->uv_angle, 30.0), "brought material keeps scale and rotation");
		check(near(ctx->brush_radius, 0.33), "settings restored in the new project");
		brush_presets_delete(brush_presets_find("test_fur"));
		check(brush_presets_count() == count0 && brush_presets_find("test_fur") < 0, "preset deleted");
	}
	else {
		ctx->brush_radius   = saved_radius;
		ctx->brush_opacity  = saved_opacity;
		ctx->brush_hardness = saved_hardness;
		ctx->brush_spacing  = saved_spacing;
		ctx->brush_angle    = saved_angle;
		ctx->brush_flow     = saved_flow;
		config_save();
		printf("[preset] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "PRESET PASS" : "PRESET FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	context_t *ctx = script_get_context();
	saved_radius   = ctx->brush_radius;
	saved_opacity  = ctx->brush_opacity;
	saved_hardness = ctx->brush_hardness;
	saved_spacing  = ctx->brush_spacing;
	saved_angle    = ctx->brush_angle;
	saved_flow     = ctx->brush_flow;
	script_project_new();
	red = make_material("preset_red", 0.9, 0.1, 0.1);
	fur = make_material("preset_fur", 0.6, 0.4, 0.2);
	script_notify_on_next_frame(next);
}
