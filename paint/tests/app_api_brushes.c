#include "global.h"

// The native Brushes and Color popovers' interface (app_api_brushes.h): brushes and groups, stroke previews, the picked color,
// applying it to the material, swatches.

int step   = 0;
int frame  = 0;
int checks = 0;
int fails  = 0;

int brushes0  = 0;
int groups0   = 0;
int picked0   = 1; // Brush picked at the start, picked again at the end
int copy      = -1;
int group     = -1;
int user_group = -1; // Group the copy of a built-in brush went to
int swatches0 = 0;
int materials0 = 0;
int waited    = 0;

void check(bool ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[app_api_brushes] %s %s", ok ? "ok  " : "FAIL", name);
}

bool group_listed(int id) {
	return !string_equals(app_brush_group_name(id), "");
}

bool near(float a, float b) {
	return a > b - 0.01 && a < b + 0.01;
}

float base_red() {
	ui_node_t        *n = script_get_context()->material->canvas->nodes->buffer[0];
	ui_node_socket_t *s = n->inputs->buffer[0];
	return s->default_value->buffer[0];
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		brushes0 = app_brush_count();
		groups0  = app_brush_group_count();
		picked0  = script_get_context()->brush_selected;
		check(brushes0 >= 12 && groups0 >= 4, "the library lists the brushes and groups");
		check(app_brush_id_at(0) == 1, "the built-in brushes come first");
		check(app_brush_use(2) && script_get_context()->brush_selected == 2, "a brush is used");
		check(app_brush_recent(0) == 2, "and is the latest recent brush");
		check(!app_brush_delete(1) && app_brush_count() == brushes0, "a built-in brush is not deleted");
		check(!app_brush_edit(1), "a built-in brush is not edited");
		copy = app_brush_duplicate(1);
		check(copy >= 1000 && app_brush_count() == brushes0 + 1, "a built-in brush duplicates");
		check(script_get_context()->brush_selected == copy, "the copy is picked");
		user_group = app_brush_group_of(copy);
		check(group_listed(user_group) && !app_brush_group_builtin(user_group), "into a group of your own");
		group = app_brush_group_add("Native Test Group");
		check(group >= 1000 && app_brush_group_count() >= groups0 + 1, "a group is added");
		check(app_brush_move(copy, group) && app_brush_group_of(copy) == group, "the copy moves to it");
		check(!app_brush_move(copy, 1), "not to a built-in group");
		check(app_brush_rename(copy, "Native Copy") && string_equals(app_brush_name(copy), "Native Copy"), "the copy is renamed");
		check(app_brush_group_rename(group, "Native Test Group 2") && string_equals(app_brush_group_name(group), "Native Test Group 2"),
		      "the group is renamed");
		check(!app_brush_group_rename(1, "X") && !app_brush_group_delete(1), "a built-in group stays");
		app_brush_previews_request(group); // Its strokes render first
	}
	else if (step == 1) { // The copy's stroke renders, a frame at a time
		if (app_brush_preview_size(copy) == 0 && waited < 600) {
			waited = waited + 1;
			script_notify_on_next_frame(next);
			return;
		}
		int n = app_brush_preview_size(copy);
		check(n > 0, string("the copy's stroke preview arrives (%d frames)", waited));
		buffer_t *b  = buffer_create(n);
		buffer_t *wb = buffer_create(4);
		buffer_t *hb = buffer_create(4);
		check(app_brush_preview(copy, b->buffer, n, wb->buffer, hb->buffer) == n, "it reads back");
		int w   = buffer_get_i32(wb, 0);
		int h   = buffer_get_i32(hb, 0);
		int lit = 0;
		for (int i = 0; i < w * h; ++i) {
			if (buffer_get_u8(b, i * 4 + 3) > 100) {
				lit = lit + 1;
			}
		}
		check(w * h * 4 == n && w > h, string("a %dx%d strip", w, h));
		check(lit > 50, string("with a stroke (%d texels)", lit));
	}
	else if (step == 2) {
		check(app_brush_group_delete(group) && !group_listed(group), "the group is deleted");
		check(app_brush_group_of(copy) == -1 && app_brush_count() == brushes0, "with its brushes");
		if (app_brush_group_count() > groups0) { // The copy made a group of your own, there was none
			app_brush_group_delete(user_group);
		}
		check(app_brush_group_count() == groups0, "the groups are as they were");
		app_brush_use(picked0);
	}
	else if (step == 3) {
		check(app_color_can_apply(), "a new project's material takes the picked color");
		app_color_set_base(0x336699);
		check((app_color_base() & 0xffffff) == 0x336699, "the picked color is set");
		app_color_set_value(2, 0.3);
		check(near(app_color_value(2), 0.3), "roughness is set");
		app_color_set_value(3, 2.0);
		check(near(app_color_value(3), 1.0), "metallic is clamped");
		app_color_apply();
		app_color_apply(); // One apply
	}
	else if (step == 4) {
		check(near(base_red(), 0x33 / 255.0), "the material has the picked color");
		swatches0 = app_swatch_count();
		check(swatches0 > 0, "the project has swatches");
		int i = app_swatch_add();
		check(i == swatches0 && app_swatch_count() == swatches0 + 1 && app_swatch_selected() == i, "the picked color is a new swatch");
		check((app_swatch_base(i) & 0xffffff) == 0x336699, "of the picked color");
		check(app_swatch_pick(0) && app_swatch_selected() == 0 && app_color_base() == app_swatch_base(0), "a swatch is picked");
	}
	else if (step == 5) {
		float red = ((app_swatch_base(0) >> 16) & 255) / 255.0;
		check(near(base_red(), red), "and applied to the material");
		check(app_swatch_delete(swatches0) && app_swatch_count() == swatches0, "the swatch is deleted");
		check(!app_swatch_delete(99999), "an index out of range is not");
		materials0 = script_material_count();
		app_color_new_material();
	}
	else if (step == 6) {
		check(script_material_count() == materials0 + 1, "a material of the picked color is made");
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[app_api_brushes] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "APP_API_BRUSHES PASS" : "APP_API_BRUSHES FAIL");
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
