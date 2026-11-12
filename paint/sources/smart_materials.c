// Smart materials: a fill layer with its masks, or a group of layers, saved to reuse in any project.
// smart_materials/<name>.json in the app's save folder describes the layers (kind, parent, material, uv settings,
// blending, channels), the materials are exported next to it as <name>_m<k>.arm and <name>.png is the preview.
// Painted pixels belong to one mesh's uv layout and are not kept: painted masks come back black, ready to paint
// the material in, paint layers come back empty.

#include "global.h"

#define SMART_MATERIALS_MAX 256

typedef struct smart_material_entry {
	char          *name;
	gpu_texture_t *icon;
	bool           icon_tried;
} smart_material_entry_t;

static smart_material_entry_t smart_materials[SMART_MATERIALS_MAX];
static i32                    smart_materials_n       = 0;
static bool                   smart_materials_scanned = false;

static char *smart_materials_dir() {
	return string("%ssmart_materials", iron_internal_save_path());
}

static char *smart_materials_path(char *name, char *suffix) {
	return string("%s%s%s%s", smart_materials_dir(), PATH_SEP, name, suffix);
}

// File names keep letters, digits, spaces, '-' and '_'
static char *smart_materials_clean(char *name) {
	i32   len = string_length(name);
	char *s   = string_copy(name);
	for (i32 i = 0; i < len; ++i) {
		char c = s[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
		if (!ok) {
			s[i] = '_';
		}
	}
	return s;
}

void smart_materials_refresh() {
	for (i32 i = 0; i < smart_materials_n; ++i) {
		smart_materials[i].icon       = NULL; // Cached by data_get_texture
		smart_materials[i].icon_tried = false;
	}
	smart_materials_n       = 0;
	smart_materials_scanned = true;
	char *dir               = smart_materials_dir();
	if (!iron_is_directory(dir)) {
		return;
	}
	char           *files = string_copy(iron_read_directory(dir));
	string_array_t *list  = string_split(files, "\n");
	for (i32 i = 0; i < list->length && smart_materials_n < SMART_MATERIALS_MAX; ++i) {
		char *f = list->buffer[i];
		if (!ends_with(f, ".json")) {
			continue;
		}
		smart_material_entry_t *e = &smart_materials[smart_materials_n++];
		e->name                   = string_copy(substring(f, 0, string_length(f) - 5));
		e->icon                   = NULL;
		e->icon_tried             = false;
	}
	// Sorted by name
	for (i32 i = 1; i < smart_materials_n; ++i) {
		for (i32 j = i; j > 0 && strcmp(smart_materials[j - 1].name, smart_materials[j].name) > 0; --j) {
			smart_material_entry_t t = smart_materials[j];
			smart_materials[j]       = smart_materials[j - 1];
			smart_materials[j - 1]   = t;
		}
	}
}

i32 smart_materials_count() {
	if (!smart_materials_scanned) {
		smart_materials_refresh();
	}
	return smart_materials_n;
}

char *smart_materials_name(i32 i) {
	return i >= 0 && i < smart_materials_count() ? smart_materials[i].name : NULL;
}

i32 smart_materials_find(char *name) {
	for (i32 i = 0; i < smart_materials_count(); ++i) {
		if (string_equals(smart_materials[i].name, name)) {
			return i;
		}
	}
	return -1;
}

gpu_texture_t *smart_materials_icon(i32 i) {
	if (i < 0 || i >= smart_materials_count()) {
		return NULL;
	}
	smart_material_entry_t *e = &smart_materials[i];
	if (!e->icon_tried) {
		e->icon_tried = true;
		char *path    = smart_materials_path(e->name, ".png");
		if (iron_file_exists(path)) {
			e->icon = data_get_texture(path);
		}
	}
	return e->icon;
}

// Layers saved for root: root and everything under it, in layer order (bottom first, root last)
static slot_layer_t_array_t *smart_materials_subtree(slot_layer_t *root) {
	slot_layer_t_array_t *out = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		for (slot_layer_t *p = l; p != NULL; p = p->parent) {
			if (p == root) {
				any_array_push(out, l);
				break;
			}
		}
	}
	return out;
}

static void smart_materials_write_icon(slot_material_t *m, char *path) {
	if (m == NULL || m->image == NULL) {
		return;
	}
	buffer_t *buf = buffer_half_to_u8(gpu_get_texture_pixels(m->image));
#ifdef IRON_BGRA
	buf = buffer_bgra_swap(buf);
#endif
	iron_write_png(path, buf, m->image->width, m->image->height, 0);
}

// Saves root (a fill layer with its masks, or a group) as smart material name, one of that name is replaced
bool smart_materials_save(slot_layer_t *root, char *name) {
	if (root == NULL || name == NULL || name[0] == '\0' || slot_layer_is_mask(root)) {
		return false;
	}
	name = smart_materials_clean(name);
	iron_create_directory(smart_materials_dir());
	slot_layer_t_array_t    *layers    = smart_materials_subtree(root);
	slot_material_t_array_t *materials = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < layers->length; ++i) {
		slot_material_t *m = layers->buffer[i]->fill_material;
		if (m != NULL && array_index_of(materials, m) == -1) {
			any_array_push(materials, m);
		}
	}

	// Materials as material files, with their textures, scale and rotation
	slot_material_t *_material = g_context->material;
	for (i32 k = 0; k < materials->length; ++k) {
		g_context->material = materials->buffer[k];
		export_arm_run_material(smart_materials_path(name, string_tmp("_m%d.arm", k)));
	}
	g_context->material = _material;

	json_encode_begin();
	json_encode_i32("count", layers->length);
	json_encode_i32("materials", materials->length);
	for (i32 k = 0; k < materials->length; ++k) {
		json_encode_string(string_tmp("m%d", k), materials->buffer[k]->canvas->name);
	}
	for (i32 i = 0; i < layers->length; ++i) {
		slot_layer_t *l = layers->buffer[i];
		json_encode_begin_object_key(string_tmp("l%d", i));
		json_encode_i32("kind", slot_layer_is_group(l) ? 2 : slot_layer_is_mask(l) ? 1 : 0);
		json_encode_i32("parent", l == root || l->parent == NULL ? -1 : array_index_of(layers, l->parent));
		json_encode_string("name", l->name);
		json_encode_i32("visible", l->visible);
		json_encode_i32("material", l->fill_material != NULL ? array_index_of(materials, l->fill_material) : -1);
		json_encode_f32("scale", l->scale);
		json_encode_f32("angle", l->angle);
		json_encode_i32("uv_type", l->uv_type);
		json_encode_i32("uv_map", l->uv_map);
		json_encode_f32("opacity", l->mask_opacity);
		json_encode_i32("blending", l->blending);
		json_encode_i32("paint_base", l->paint_base);
		json_encode_i32("paint_opac", l->paint_opac);
		json_encode_i32("paint_occ", l->paint_occ);
		json_encode_i32("paint_rough", l->paint_rough);
		json_encode_i32("paint_met", l->paint_met);
		json_encode_i32("paint_nor", l->paint_nor);
		json_encode_i32("paint_nor_blend", l->paint_nor_blend);
		json_encode_i32("paint_height", l->paint_height);
		json_encode_i32("paint_height_blend", l->paint_height_blend);
		json_encode_i32("paint_emis", l->paint_emis);
		json_encode_i32("paint_subs", l->paint_subs);
		json_encode_i32("height_blend", l->height_blend);
		json_encode_i32("height_blend_below", l->height_blend_below);
		json_encode_f32("height_blend_influence", l->height_blend_influence);
		json_encode_f32("height_blend_sharpness", l->height_blend_sharpness);
		json_encode_f32("height_blend_offset", l->height_blend_offset);
		json_encode_f32("height_blend_reach", l->height_blend_reach);
		json_encode_end_object();
	}
	char     *json   = json_encode_end();
	buffer_t *buffer = sys_string_to_buffer(json);
	iron_file_save_bytes(smart_materials_path(name, ".json"), buffer, 0);
	array_free(buffer);
	free(buffer);
	free(json);

	// Preview: the material of the root, else the first one
	slot_material_t *icon_material = root->fill_material != NULL ? root->fill_material : materials->length > 0 ? materials->buffer[0] : NULL;
	char            *icon_path     = smart_materials_path(name, ".png");
	if (iron_file_exists(icon_path)) {
		data_delete_texture(icon_path); // Replaced, the cached one goes
	}
	smart_materials_write_icon(icon_material, icon_path);
	smart_materials_refresh();
	return true;
}

static char *smart_materials_get(any_map_t *m, char *prefix, char *key) {
	return any_map_get(m, string_tmp("%s.%s", prefix, key));
}

static f32 smart_materials_f32(any_map_t *m, char *prefix, char *key, f32 def) {
	char *s = smart_materials_get(m, prefix, key);
	return s != NULL ? (f32)atof(s) : def;
}

static i32 smart_materials_i32(any_map_t *m, char *prefix, char *key, i32 def) {
	char *s = smart_materials_get(m, prefix, key);
	return s != NULL ? atoi(s) : def;
}

static slot_material_t *smart_materials_material(char *name, char *file) {
	// A material of that name in the project is used, applying again does not pile up copies
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		if (string_equals(m->canvas->name, name)) {
			return m;
		}
	}
	if (!iron_file_exists(file)) {
		return NULL;
	}
	i32 before = g_project->_->materials->length;
	import_arm_run_material(file);
	return g_project->_->materials->length > before ? g_project->_->materials->buffer[g_project->_->materials->length - 1] : NULL;
}

// Puts smart material name on top of the layer stack and selects its top layer, NULL when it can not be read
slot_layer_t *smart_materials_insert(char *name) {
	char *path = smart_materials_path(name, ".json");
	if (!iron_file_exists(path)) {
		return NULL;
	}
	buffer_t *blob = data_get_blob(path);
	if (blob == NULL) {
		return NULL;
	}
	char      *s = sys_buffer_to_string(blob);
	any_map_t *m = json_parse_to_map(s);
	free(s);
	data_delete_blob(path);

	char *count_s = any_map_get(m, "count");
	char *mats_s  = any_map_get(m, "materials");
	i32   count   = count_s != NULL ? atoi(count_s) : 0;
	i32   mats    = mats_s != NULL ? atoi(mats_s) : 0;
	if (count <= 0 || g_project->_->layers->length + count > layers_max_layers) {
		return NULL;
	}

	slot_material_t *_material = g_context->material;
	slot_material_t **materials = calloc(mats > 0 ? mats : 1, sizeof(slot_material_t *));
	for (i32 k = 0; k < mats; ++k) {
		char *mname  = any_map_get(m, string_tmp("m%d", k));
		materials[k] = mname != NULL ? smart_materials_material(mname, smart_materials_path(name, string_tmp("_m%d.arm", k))) : NULL;
	}
	g_context->material = _material;

	// All slots first, parents point forward (masks and children sit below their parent)
	slot_layer_t **created = calloc(count, sizeof(slot_layer_t *));
	for (i32 i = 0; i < count; ++i) {
		char *p     = string_copy(string_tmp("l%d", i));
		i32   kind  = smart_materials_i32(m, p, "kind", 0);
		created[i]  = slot_layer_create("", kind == 2 ? LAYER_SLOT_TYPE_GROUP : kind == 1 ? LAYER_SLOT_TYPE_MASK : LAYER_SLOT_TYPE_LAYER, NULL);
		any_array_push(g_project->_->layers, created[i]);
	}
	for (i32 i = 0; i < count; ++i) {
		char         *p      = string_copy(string_tmp("l%d", i));
		slot_layer_t *l      = created[i];
		i32           parent = smart_materials_i32(m, p, "parent", -1);
		l->parent            = parent >= 0 && parent < count ? created[parent] : NULL;
		char *lname          = smart_materials_get(m, p, "name");
		if (lname != NULL) {
			l->name = string_copy(slot_layer_unique_name(l, lname));
		}
		l->visible                = smart_materials_i32(m, p, "visible", 1) != 0;
		l->scale                  = smart_materials_f32(m, p, "scale", 1.0);
		l->angle                  = smart_materials_f32(m, p, "angle", 0.0);
		l->uv_type                = smart_materials_i32(m, p, "uv_type", UV_TYPE_UVMAP);
		l->uv_map                 = smart_materials_i32(m, p, "uv_map", 0);
		l->mask_opacity           = smart_materials_f32(m, p, "opacity", 1.0);
		l->blending               = smart_materials_i32(m, p, "blending", BLEND_TYPE_MIX);
		l->paint_base             = smart_materials_i32(m, p, "paint_base", 1) != 0;
		l->paint_opac             = smart_materials_i32(m, p, "paint_opac", 1) != 0;
		l->paint_occ              = smart_materials_i32(m, p, "paint_occ", 1) != 0;
		l->paint_rough            = smart_materials_i32(m, p, "paint_rough", 1) != 0;
		l->paint_met              = smart_materials_i32(m, p, "paint_met", 1) != 0;
		l->paint_nor              = smart_materials_i32(m, p, "paint_nor", 1) != 0;
		l->paint_nor_blend        = smart_materials_i32(m, p, "paint_nor_blend", 1) != 0;
		l->paint_height           = smart_materials_i32(m, p, "paint_height", 1) != 0;
		l->paint_height_blend     = smart_materials_i32(m, p, "paint_height_blend", 1) != 0;
		l->paint_emis             = smart_materials_i32(m, p, "paint_emis", 1) != 0;
		l->paint_subs             = smart_materials_i32(m, p, "paint_subs", 1) != 0;
		l->height_blend           = smart_materials_i32(m, p, "height_blend", 0) != 0;
		l->height_blend_below     = smart_materials_i32(m, p, "height_blend_below", 0) != 0;
		l->height_blend_influence = smart_materials_f32(m, p, "height_blend_influence", 1.0);
		l->height_blend_sharpness = smart_materials_f32(m, p, "height_blend_sharpness", 16.0);
		l->height_blend_offset    = smart_materials_f32(m, p, "height_blend_offset", 0.0);
		l->height_blend_reach     = smart_materials_f32(m, p, "height_blend_reach", 0.03);
		if (l->height_blend) {
			make_material_height_used = true;
		}
		i32 mat          = smart_materials_i32(m, p, "material", -1);
		l->fill_material = mat >= 0 && mat < mats ? materials[mat] : NULL;
		l->object_mask   = 0;
	}

	// Pixels: fill layers and masks from their material, painted masks black, paint layers empty
	for (i32 i = 0; i < count; ++i) {
		slot_layer_t *l = created[i];
		if (slot_layer_is_group(l)) {
			continue;
		}
		slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	}
	for (i32 i = 0; i < count; ++i) {
		if (created[i]->fill_material != NULL) {
			layers_refresh_fill_layer(created[i]);
		}
	}

	slot_layer_t *root = created[count - 1];
	free(created);
	free(materials);
	context_set_layer(root);
	make_material_parse_mesh_material();
	g_context->layers_preview_dirty = true;
	g_context->ddirty               = 2;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	return root;
}

// Applies smart material name, undoable
slot_layer_t *smart_materials_apply(char *name) {
	slot_layer_t *root = smart_materials_insert(name);
	if (root != NULL) {
		history_smart_material(name);
	}
	return root;
}

void smart_materials_delete(i32 i) {
	char *name = smart_materials_name(i);
	if (name == NULL) {
		return;
	}
	name = string_copy(name);
	iron_delete_file(smart_materials_path(name, ".json"));
	char *icon = smart_materials_path(name, ".png");
	if (iron_file_exists(icon)) {
		data_delete_texture(icon);
		iron_delete_file(icon);
	}
	for (i32 k = 0; iron_file_exists(smart_materials_path(name, string_tmp("_m%d.arm", k))); ++k) {
		iron_delete_file(smart_materials_path(name, string_tmp("_m%d.arm", k)));
	}
	smart_materials_refresh();
}
