#include "../global.h"

// The Godot presets: after a job's textures, the material that uses them. godot_material writes it as a Godot resource
// (.tres) beside the textures, godot_mesh a glb of the job's meshes with the textures embedded (the files go).

#ifdef WITH_PLUGINS
void export_glb_run_textured(char *path, any_array_t *paint_objects, char *material_name, char **images);
#endif

typedef enum {
	EXPORT_GODOT_NONE,
	EXPORT_GODOT_MESH,
	EXPORT_GODOT_MATERIAL,
} export_godot_t;

static export_godot_t export_godot_target(char *name) {
	return string_equals(name, "godot_mesh") ? EXPORT_GODOT_MESH : string_equals(name, "godot_material") ? EXPORT_GODOT_MATERIAL : EXPORT_GODOT_NONE;
}

// The job's meshes: the object or the atlas it is named after, all of them otherwise
static mesh_object_t_array_t *export_godot_objects(char *object_name) {
	mesh_object_t_array_t *all = g_project->_->paint_objects;
	mesh_object_t_array_t *out = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < all->length; ++i) {
		if (string_equals(all->buffer[i]->base->name, object_name)) {
			any_array_push(out, all->buffer[i]);
			return out;
		}
	}
	i32 atlas = g_project->atlas_names != NULL ? string_array_index_of(g_project->atlas_names, object_name) : -1;
	if (atlas >= 0 && g_project->atlas_objects != NULL) {
		for (i32 i = 0; i < all->length && i < g_project->atlas_objects->length; ++i) {
			if (g_project->atlas_objects->buffer[i] == atlas) {
				any_array_push(out, all->buffer[i]);
			}
		}
	}
	return out->length > 0 ? out : all;
}

static char *export_godot_file_name(char *path) {
	return substring(path, string_last_index_of(path, PATH_SEP) + 1, string_length(path));
}

// name as the body of a Godot string
static char *export_godot_escape(char *name) {
	char *out = "";
	for (i32 i = 0; i < string_length(name); ++i) {
		char c = name[i];
		out    = string("%s%s%c", out, c == '"' || c == '\\' ? "\\" : "", c);
	}
	return out;
}

static void export_godot_material(char *path, char *name, char **images) {
	char *ids[]   = {"1_base", "2_nor", "3_orm"};
	char *props[] = {"albedo_texture", "normal_texture", "orm_texture"};
	char *s       = string("[gd_resource type=\"%s\" format=3]\n\n", images[2] != NULL ? "ORMMaterial3D" : "StandardMaterial3D");
	for (i32 k = 0; k < 3; ++k) {
		if (images[k] != NULL) { // Relative to the material
			s = string("%s[ext_resource type=\"Texture2D\" path=\"%s\" id=\"%s\"]\n", s, export_godot_escape(export_godot_file_name(images[k])), ids[k]);
		}
	}
	s = string("%s\n[resource]\nresource_name = \"%s\"\n", s, export_godot_escape(name));
	for (i32 k = 0; k < 3; ++k) {
		if (images[k] != NULL) {
			s = string("%s%s%s = ExtResource(\"%s\")\n", s, k == 1 ? "normal_enabled = true\n" : "", props[k], ids[k]);
		}
	}
	buffer_t *b = sys_string_to_buffer(s);
	if (!iron_file_save_bytes(path, b, b->length)) {
		console_error(string("Could not save %s.", path));
	}
}

// The job's textures are written->buffer[first..], one a texture of the export's preset: its Godot file joins them
void export_godot_job(export_texture_settings_t *s, char *dir, char *file, char *object_name, string_array_t *written, i32 first) {
	export_godot_t target = export_godot_target(s->preset_name);
	if (target == EXPORT_GODOT_NONE || s->destination == EXPORT_DESTINATION_PACK_INTO_PROJECT || written->length - first != s->preset->textures->length) {
		return;
	}
	char *images[3] = {NULL, NULL, NULL}; // Base color, normal, occlusion / roughness / metallic
	for (i32 i = 0; i < s->preset->textures->length; ++i) {
		export_texture_layout_t l = export_texture_layout(s->preset->textures->buffer[i]);
		if (l.src >= 0 && l.type == 1) {
			images[l.src] = written->buffer[first + i];
		}
	}

	if (target == EXPORT_GODOT_MATERIAL) {
		char *path = string("%s%s%s.tres", dir, PATH_SEP, file);
		export_godot_material(path, file, images);
		any_array_push(written, string_copy(path));
		return;
	}

#ifdef WITH_PLUGINS
	for (i32 i = first; i < written->length; ++i) {
		if (ends_with(written->buffer[i], ".exr")) {
			console_error(tr("Godot Mesh embeds png or jpg textures, not exr"));
			return;
		}
	}
	char *path = string("%s%s%s.glb", dir, PATH_SEP, file);
	export_glb_run_textured(path, export_godot_objects(object_name), file, images);
	if (!iron_file_exists(path)) {
		console_error(string("Could not save %s.", path));
		return;
	}
	for (i32 i = first; i < written->length; ++i) { // In the glb now
		iron_delete_file(written->buffer[i]);
	}
	written->length = first;
	any_array_push(written, string_copy(path));
#endif
}
