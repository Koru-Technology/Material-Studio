// app_api_test.h, #included by main.c after the other app_api*.c files (it uses their helpers)

#include "app_api_test.h"

// Layers

int32_t app_layer_id_at(int32_t index) {
	app_layer_info_t info;
	return app_layer_info(index, &info) ? info.id : -1;
}

int32_t app_layer_thumbnail_size(int32_t id) {
	int32_t w, h;
	return app_layer_thumbnail(id, NULL, 0, &w, &h);
}

// Thumbnails and icons being read back (util_layer_thumbs.c, app_read_rgba8): their landing bumps the previews version
bool app_thumbnails_busy(void) {
	return layers_thumbs_busy() || app_reads_busy();
}

// Its texels are there to show (read back, or from the project file)
bool app_layer_thumbnail_ready(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && l->thumb != NULL;
}

bool app_layer_select(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	if (l == NULL) {
		return false;
	}
	layers_op_set(l);
	return true;
}

bool app_layer_add(void) {
	return app_ready() && layers_op_new() != NULL;
}

bool app_layer_can_delete(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && layers_can_delete(l);
}

// Layer properties and mask effects

bool app_layer_expanded(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && l->show_panel;
}

// A fill layer's projection (app_fill_projection_t), -1 when it is no fill
int32_t app_layer_fill_projection(int32_t id) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && l->fill != NULL ? l->fill->projection : -1;
}

// The kind of a fill layer's source (app_fill_source_kind_t), -1 when it is no fill
int32_t app_layer_fill_kind(int32_t id, int32_t channel) {
	slot_layer_t *l = app_layer_by_id(id);
	return l != NULL && l->fill != NULL && channel >= 0 && channel < FILL_CHANNEL_COUNT ? l->fill->ch[channel].kind : -1;
}

int32_t app_mask_effect_type(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) ? e.type : -1;
}

bool app_mask_effect_visible(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) && e.visible;
}

int32_t app_mask_effect_blending(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) ? e.blending : -1;
}

float app_mask_effect_value(int32_t id, int32_t index, int32_t field) {
	app_mask_effect_t e;
	if (!app_mask_effect(id, index, &e) || field < -1 || field >= APP_MASK_EFFECT_PARAMS) {
		return 0.0;
	}
	return field < 0 ? e.opacity : e.params[field];
}

bool app_mask_effect_needs_ao(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) && e.needs_ao;
}

bool app_mask_effect_needs_thickness(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) && e.needs_thickness;
}

bool app_mask_effect_needs_id_map(int32_t id, int32_t index) {
	app_mask_effect_t e;
	return app_mask_effect(id, index, &e) && e.needs_id_map;
}

uint32_t app_layer_previews_version(void) {
	return app_previews_counter;
}

// Materials

int32_t app_material_id_at(int32_t index) {
	app_material_info_t info;
	return app_material_info(index, &info) ? info.id : -1;
}

int32_t app_material_preview_version(int32_t id) {
	slot_material_t *m = app_mat_by_id(id);
	app_mat_track_t *t = m != NULL ? app_mat_track_of(m) : NULL;
	return t != NULL ? (int32_t)t->version : -1;
}

int32_t app_material_thumbnail_size(int32_t id, int32_t max_size) {
	int32_t w, h;
	return app_material_thumbnail(id, max_size, NULL, 0, &w, &h);
}

int32_t app_library_icon_size(int32_t index) {
	int32_t w, h;
	return app_library_icon(index, NULL, 0, &w, &h);
}

int32_t app_library_find(const char *name) {
	app_library_item_t item;
	for (i32 i = 0; i < app_library_count(); ++i) {
		if (app_library_item(i, &item) && name != NULL && strcmp(item.name, name) == 0) {
			return i;
		}
	}
	return -1;
}

int32_t app_smart_icon_size(int32_t index) {
	int32_t w, h;
	return app_smart_icon(index, NULL, 0, &w, &h);
}

// Brushes and color

int32_t app_brush_id_at(int32_t index) {
	app_brush_info_t info;
	return app_brush_info(index, &info) ? info.id : -1;
}

int32_t app_brush_group_of(int32_t id) {
	brush_def_t *d = app_ready() ? brush_library_find(id) : NULL;
	return d != NULL ? d->group : -1;
}

const char *app_brush_name(int32_t id) {
	brush_def_t *d = app_ready() ? brush_library_find(id) : NULL;
	return app_str(d != NULL ? brush_library_name(d) : NULL);
}

const char *app_brush_group_name(int32_t id) {
	brush_group_t *g = app_ready() ? brush_group_find(id) : NULL;
	return app_str(g != NULL ? brush_group_name(g) : NULL);
}

bool app_brush_group_builtin(int32_t id) {
	brush_group_t *g = app_ready() ? brush_group_find(id) : NULL;
	return g != NULL && g->builtin;
}

uint32_t app_color_base(void) {
	app_color_t c;
	return app_color_get(&c) ? c.base : 0;
}

float app_color_value(int32_t which) {
	if (!app_ready() || g_context->picked_color == NULL) {
		return 0.0f;
	}
	f32 *v = app_color_field(g_context->picked_color, which);
	return v != NULL ? *v : 0.0f;
}

uint32_t app_swatch_base(int32_t index) {
	app_color_t c;
	return app_swatch_info(index, &c) ? c.base : 0;
}

// Bake Mesh Maps

void app_test_bake_options(int32_t maps, int32_t size, int32_t rays) {
	app_bake_options_t o;
	app_bake_get(&o);
	o.maps = maps;
	o.size = size;
	o.rays = rays;
	app_bake_set(&o);
}

float app_test_bake_progress(void) {
	app_bake_state_t s;
	app_bake_state(&s);
	return s.progress;
}

float app_test_bake_missed(void) {
	app_bake_state_t s;
	app_bake_state(&s);
	return s.missed;
}

void app_test_bake_max_distance(float d) {
	app_bake_options_t o;
	app_bake_get(&o);
	o.max_distance = d;
	app_bake_set(&o);
}

// The selected layer's channels (bits of app_layer_channel_t) and name: the baked layer is selected once added
uint32_t app_test_layer_channels(void) {
	app_layer_props_t p;
	return g_context->layer != NULL && app_layer_props(g_context->layer->id, &p) ? p.channels : 0;
}

const char *app_test_layer_name(void) {
	return g_context->layer != NULL ? app_str(g_context->layer->name) : "";
}

int32_t app_test_bake_preview(int32_t high) {
	static uint8_t pixels[320 * 240 * 4];
	int32_t        w, h;
	return app_bake_preview(0.8, 0.35, 1.0, high != 0, false, 320, 240, pixels, sizeof(pixels), &w, &h);
}

// An obj of the mesh in src plus a copy scaled by scale around its center: a shell over the surface, raised detail
// for a high poly bake. only: the scaled copy alone. False when src can not be read.
bool app_test_write_shell_obj(char *src, char *dst, float scale, bool only) {
	FILE *in = fopen(src, "r");
	if (in == NULL) {
		return false;
	}
	FILE *out = fopen(dst, "w");
	if (out == NULL) {
		fclose(in);
		return false;
	}
	f32  lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
	i32  verts = 0;
	char line[1024];
	while (fgets(line, sizeof(line), in) != NULL) {
		f32 x, y, z;
		if (sscanf(line, "v %f %f %f", &x, &y, &z) == 3) {
			f32 v[3] = {x, y, z};
			for (i32 k = 0; k < 3; ++k) {
				lo[k] = v[k] < lo[k] ? v[k] : lo[k];
				hi[k] = v[k] > hi[k] ? v[k] : hi[k];
			}
			verts++;
		}
	}
	// Pass 1: the mesh as it is. Pass 2: its vertices scaled, its faces offset past the first copy's vertices.
	for (i32 pass = only ? 1 : 0; pass < 2; ++pass) {
		rewind(in);
		while (fgets(line, sizeof(line), in) != NULL) {
			f32 x, y, z;
			if (sscanf(line, "v %f %f %f", &x, &y, &z) == 3) {
				f32 v[3] = {x, y, z};
				for (i32 k = 0; k < 3 && pass == 1; ++k) {
					f32 c = (lo[k] + hi[k]) * 0.5;
					v[k]  = c + (v[k] - c) * scale;
				}
				fprintf(out, "v %f %f %f\n", v[0], v[1], v[2]);
			}
			else if (line[0] == 'f' && line[1] == ' ') {
				fputs("f", out);
				char *tok = strtok(line + 2, " \n");
				while (tok != NULL) {
					i32 vi = atoi(tok);
					fprintf(out, " %d", vi + (pass == 1 && !only ? verts : 0)); // Positions only: normals are rebuilt from faces
					tok = strtok(NULL, " \n");
				}
				fputs("\n", out);
			}
		}
	}
	fclose(in);
	fclose(out);
	return true;
}

// Fraction of a project texture's texels brighter than half, in its red channel
float app_test_texture_bright(int32_t texture) {
	if (texture < 0 || texture >= (i32)g_project->_->assets->length) {
		return -1.0;
	}
	gpu_texture_t *t = project_get_image(g_project->_->assets->buffer[texture]);
	buffer_t      *p = t != NULL ? gpu_get_texture_pixels(t) : NULL;
	if (p == NULL || p->length < 4) {
		return -1.0;
	}
	i32 bright = 0;
	for (i32 i = 0; i < p->length; i += 4) {
		bright += p->buffer[i + 2] > 127 ? 1 : 0; // BGRA or RGBA: a gray map has the same value in each
	}
	return bright / (f32)(p->length / 4);
}

// An obj of the mesh in src as two objects: its first half of faces, then its second half. The same shape, two parts
// for an ID map.
bool app_test_write_split_obj(char *src, char *dst) {
	FILE *in = fopen(src, "r");
	if (in == NULL) {
		return false;
	}
	FILE *out = fopen(dst, "w");
	if (out == NULL) {
		fclose(in);
		return false;
	}
	i32  verts = 0, faces = 0;
	char line[1024];
	while (fgets(line, sizeof(line), in) != NULL) {
		verts += line[0] == 'v' && line[1] == ' ' ? 1 : 0;
		faces += line[0] == 'f' && line[1] == ' ' ? 1 : 0;
	}
	for (i32 part = 0; part < 2; ++part) {
		fprintf(out, "o part%d\n", part); // Two objects, as an exporter writes them
		rewind(in);
		while (fgets(line, sizeof(line), in) != NULL) {
			if (line[0] == 'v' && line[1] == ' ') {
				fputs(line, out);
			}
		}
		rewind(in);
		i32 face = 0;
		while (fgets(line, sizeof(line), in) != NULL) {
			if (line[0] != 'f' || line[1] != ' ') {
				continue;
			}
			bool mine = part == 0 ? face < faces / 2 : face >= faces / 2;
			face++;
			if (!mine) {
				continue;
			}
			fputs("f", out);
			char *tok = strtok(line + 2, " \n");
			while (tok != NULL) {
				fprintf(out, " %d", atoi(tok) + part * verts);
				tok = strtok(NULL, " \n");
			}
			fputs("\n", out);
		}
	}
	fclose(in);
	fclose(out);
	return true;
}

// Colors (rgb) other than black covering at least 1% of a project texture, -1 when unreadable
int32_t app_test_texture_colors(int32_t texture) {
	if (texture < 0 || texture >= (i32)g_project->_->assets->length) {
		return -1;
	}
	gpu_texture_t *t = project_get_image(g_project->_->assets->buffer[texture]);
	buffer_t      *p = t != NULL ? gpu_get_texture_pixels(t) : NULL;
	if (p == NULL || p->length < 4) {
		return -1;
	}
	enum { SLOTS = 4096 };
	u32 *keys = calloc(SLOTS, sizeof(u32));
	i32 *hits = calloc(SLOTS, sizeof(i32));
	i32  n    = p->length / 4;
	for (i32 i = 0; i < n; ++i) {
		u32 c = 0x1000000 | (p->buffer[i * 4] << 16) | (p->buffer[i * 4 + 1] << 8) | p->buffer[i * 4 + 2];
		u32 h = (c * 2654435761u) % SLOTS;
		for (i32 k = 0; k < SLOTS && keys[h] != 0 && keys[h] != c; ++k) {
			h = (h + 1) % SLOTS;
		}
		keys[h] = c;
		hits[h]++;
	}
	i32 colors = 0;
	for (i32 i = 0; i < SLOTS; ++i) {
		colors += hits[i] >= n / 100 && keys[i] != 0x1000000 ? 1 : 0; // Black: texels out of the uv islands
	}
	free(keys);
	free(hits);
	return colors;
}

int32_t app_test_id_parts(void) {
	app_id_pick_t p;
	app_id_pick(&p);
	return p.parts;
}

const char *app_test_id_map(void) {
	return app_str(g_project->id_map);
}
