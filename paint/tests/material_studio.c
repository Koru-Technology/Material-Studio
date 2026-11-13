#include "global.h"

// Material Studio: the studio builds a real node graph, presets and patterns give different surfaces, sliders and ramp
// colors do not recompile, layers with masks change the surface, undo works, the graph survives save and load

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_material_t *mat      = NULL;
slot_layer_t    *layer    = NULL;
int              compiles = 0;
char            *file     = NULL;
char            *mat_name = NULL;
float            means[32];
int              PRESETS = 16;

// As in studio_role_t, studio_value_t and studio_mask_t
int R_PATTERN = 1;
int R_RAMP    = 3;
int R_ROUGH   = 4;
int R_HEIGHT  = 5;
int R_MASK    = 6;
int R_MIX     = 7;

int V_ROUGHNESS   = 0;
int V_METALLIC    = 2;
int V_DEPTH       = 3;
int V_MASK_AMOUNT = 6;

int M_EDGES = 1;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[studio] %s %s", ok ? "ok  " : "FAIL", name);
}

// Spread of the red channel over sampled texels, 0 for a flat image
int variety(gpu_texture_t *tex) {
	buffer_t *p  = script_get_texture_pixels(tex);
	int       n  = p->length / 4;
	int       lo = 255;
	int       hi = 0;
	for (int i = 0; i < n; i += n / 2048 + 1) {
		if (buffer_get_u8(p, i * 4 + 3) < 250) {
			continue; // Outside the uv islands
		}
		int v = buffer_get_u8(p, i * 4);
		if (v < lo) {
			lo = v;
		}
		if (v > hi) {
			hi = v;
		}
	}
	return hi - lo;
}

buffer_t *before = NULL;

buffer_t *copy_pixels(gpu_texture_t *tex) {
	buffer_t *p = script_get_texture_pixels(tex);
	buffer_t *c = buffer_create(p->length);
	for (int i = 0; i < p->length; ++i) {
		buffer_set_u8(c, i, buffer_get_u8(p, i));
	}
	return c;
}

// Texels changed since the copy
int changed(gpu_texture_t *tex) {
	buffer_t *p = script_get_texture_pixels(tex);
	int       n = 0;
	for (int i = 0; i < p->length; i += 4) {
		int d = buffer_get_u8(p, i) - buffer_get_u8(before, i);
		if (d > 10 || d < -10) {
			n = n + 1;
		}
	}
	return n;
}

// Mean of the red, green and blue channels over sampled texels
float mean(gpu_texture_t *tex) {
	buffer_t *p   = script_get_texture_pixels(tex);
	int       n   = p->length / 4;
	float     sum = 0.0;
	int       c   = 0;
	for (int i = 0; i < n; i += n / 2048 + 1) {
		if (buffer_get_u8(p, i * 4 + 3) < 250) {
			continue;
		}
		sum = sum + buffer_get_u8(p, i * 4) + buffer_get_u8(p, i * 4 + 1) * 2.0 + buffer_get_u8(p, i * 4 + 2) * 3.0;
		c   = c + 1;
	}
	return c > 0 ? sum / c : 0.0;
}

// Nodes of a type in the canvas of the material
int count_type(slot_material_t *m, char *type) {
	int n = 0;
	for (int i = 0; i < m->canvas->nodes->length; ++i) {
		ui_node_t *node = m->canvas->nodes->buffer[i];
		if (string_equals(node->type, type)) {
			n = n + 1;
		}
	}
	return n;
}

void next() {
	frame = frame + 1;
	if (frame < 8) { // Commits and renders run at the start of the frames
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;
	material_studio_apply(); // The fill layers update when the studio closes, now for the checks
	int p = step - 6;

	if (step == 0) {
		mat = material_studio_new();
		check(mat != NULL && material_studio_is_open(), "new studio material opens the studio");
		check(material_studio_layers() == 1, "one layer");
		check(count_type(mat, "VALTORGB") == 1 && count_type(mat, "MAPRANGE") == 1 && count_type(mat, "MATERIAL_MIX") == 1,
		      "made of real nodes: color ramp, map range, material mix");
	}
	else if (step == 1) {
		check(material_studio_preview() != NULL && material_studio_compiles() >= 1, "preview rendered");
		layer = script_layer_new_fill();
		check(layer != NULL && layer->fill_material == mat, "fill layer with the studio material");
	}
	else if (step == 2) {
		check(variety(layer->texpaint) < 8, string("plastic is flat (%d)", variety(layer->texpaint)));
		material_studio_preset(6); // Stone
	}
	else if (step == 3) {
		check(count_type(mat, "PATTERN_CELLS") == 1, "stone preset adds a cells pattern node");
		check(variety(layer->texpaint) > 20, string("stone fills with a pattern (%d)", variety(layer->texpaint)));
		compiles = material_studio_compiles();
		material_studio_set_value(0, V_ROUGHNESS, 0.95);
		material_studio_set_input(0, R_PATTERN, 1, 9.0); // Scale
		material_studio_set_input(0, R_PATTERN, 6, 0.6); // Distort
		material_studio_set_color(0, 1, 0.9, 0.2, 0.1);
	}
	else if (step == 4) {
		check(material_studio_compiles() == compiles, string("sliders and colors render without compiling (%d, %d)", compiles, material_studio_compiles()));
		printf("[studio]   one layer: compile %.0f ms, render %.1f ms", material_studio_time(0), material_studio_time(1));
		check(material_studio_get_value(0, V_ROUGHNESS) > 0.94, "roughness set");
		check(material_studio_get_input(0, R_ROUGH, 3) > 0.95, "roughness lives in the map range node");
		material_studio_set_button(0, R_PATTERN, 0, 1); // Triplanar
	}
	else if (step == 5) {
		check(material_studio_compiles() > compiles, "mapping change compiles");
		check(variety(layer->texpaint) > 20, string("triplanar fills with a pattern (%d)", variety(layer->texpaint)));
		material_studio_set_button(0, R_PATTERN, 0, 0);
	}
	else if (p >= 0 && p <= PRESETS) {
		// Every preset, a different surface each
		if (p > 0) {
			means[p - 1] = mean(layer->texpaint);
		}
		if (p < PRESETS) {
			material_studio_preset(p);
		}
		else {
			int distinct = 1;
			for (int i = 0; i < PRESETS; ++i) {
				for (int j = i + 1; j < PRESETS; ++j) {
					float d = means[i] - means[j];
					if (d < 0.0) {
						d = -d;
					}
					if (d < 1.0) {
						distinct = 0;
						printf("[studio]   presets %d and %d look alike (%.1f, %.1f)", i, j, means[i], means[j]);
					}
				}
			}
			check(distinct, "every preset gives a different surface");
			material_studio_preset(6);
			material_studio_set_value(0, V_DEPTH, 0.33);
		}
	}
	else if (step == 7 + PRESETS) {
		check(material_studio_layers() == 1, "a preset without layers leaves one");
		before   = copy_pixels(layer->texpaint);
		compiles = material_studio_compiles();
		material_studio_add_layer(0); // Edge Wear
		material_studio_add_layer(1); // Dirt
	}
	else if (step == 8 + PRESETS) {
		printf("[studio]   three layers: compile %.0f ms, render %.1f ms", material_studio_time(0), material_studio_time(1));
		check(material_studio_layers() == 3, "two layers added");
		check(count_type(mat, "CURVATURE_MASK") == 2 && count_type(mat, "MATERIAL_MIX") == 3, "with curvature masks and material mixes");
		check(material_studio_compiles() > compiles, "adding layers compiles");
		int n = changed(layer->texpaint);
		check(n > 1000, string("wear and dirt change the fill (%d texels)", n));
		compiles = material_studio_compiles();
		material_studio_set_value(1, V_MASK_AMOUNT, 0.7);
		material_studio_set_value(1, V_METALLIC, 0.2);
	}
	else if (step == 9 + PRESETS) {
		check(material_studio_compiles() == compiles, "mask amount renders without compiling");
		material_studio_set_pattern(0, "PATTERN_BRICKS");
	}
	else if (step == 10 + PRESETS) {
		check(count_type(mat, "PATTERN_BRICKS") == 1 && count_type(mat, "PATTERN_CELLS") == 0, "pattern node swapped");
		check(material_studio_layers() == 3, "layers kept");
		check(material_studio_get_value(0, V_DEPTH) > 0.32, "depth kept");
		material_studio_set_value(0, V_METALLIC, 0.66);
	}
	else if (step == 11 + PRESETS) {
		history_undo();
	}
	else if (step == 12 + PRESETS) {
		check(material_studio_get_value(0, V_METALLIC) < 0.01, string("undo brings the value back (%.2f)", material_studio_get_value(0, V_METALLIC)));
		history_undo();
	}
	else if (step == 13 + PRESETS) {
		check(count_type(mat, "PATTERN_CELLS") == 1, "undo brings the pattern back");
		compiles = material_studio_compiles();
		material_studio_remove_layer(1);
	}
	else if (step == 14 + PRESETS) {
		check(material_studio_layers() == 2 && count_type(mat, "CURVATURE_MASK") == 1, "layer removed");
		check(material_studio_compiles() > compiles, "removing a layer compiles");
		material_studio_close();
		check(!material_studio_is_open(), "studio closes");
		mat_name = string_copy(mat->canvas->name);
		file     = string("%s/studio_test.arm", script_save_path());
		project_filepath_set(file);
		project_save(false);
	}
	else if (step == 15 + PRESETS) {
		script_project_new();
	}
	else if (step == 16 + PRESETS) {
		script_project_open(file);
	}
	else if (step == 17 + PRESETS) {
		slot_material_t *m = script_get_material(mat_name);
		check(m != NULL, "studio material survives save and load");
		if (m != NULL) {
			script_material_set(m);
			check(material_studio_layers() == 2 && material_studio_get_value(0, V_DEPTH) > 0.32, "its layers and values survive");
			material_studio_open(m);
		}
	}
	else if (step == 18 + PRESETS) {
		check(material_studio_is_open(), "studio reopens on it");
		material_studio_close();
	}
	else {
		printf("[studio] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "STUDIO PASS" : "STUDIO FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	script_notify_on_next_frame(next);
}
