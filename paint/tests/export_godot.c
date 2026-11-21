#include "global.h"

// The Godot presets (io/export_godot.c): godot_material writes the base, normal and orm textures and a .tres material
// that uses them; godot_mesh writes only a .glb, the textures embedded in it as the material of its meshes.

char *root = "";

int fails  = 0;
int checks = 0;
int step   = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_godot] %s %s", ok ? "ok  " : "FAIL", name);
}

char *fresh_dir(char *name) {
	char *d = string("%s/%s", root, name);
	iron_create_directory(d);
	any_array_t *old = file_read_directory(d);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", d, old->buffer[i]));
	}
	return d;
}

// The file of dir ending with end, "" for none
char *find(char *dir, char *end) {
	any_array_t *files = file_read_directory(dir);
	for (int i = 0; files != NULL && i < files->length; ++i) {
		if (ends_with(files->buffer[i], end)) {
			return string("%s/%s", dir, files->buffer[i]);
		}
	}
	return "";
}

int count(char *dir) {
	any_array_t *files = file_read_directory(dir);
	return files != NULL ? files->length : 0;
}

void build_scene() {
	slot_material_t *m   = script_material_create("eg_red");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.8, 0.1, 0.1, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	slot_layer_t *fill = script_layer_new_fill();
	script_layer_set_name(fill, "eg_fill");
}

void next() {
	if (step == 0) {
		build_scene();
	}
	else if (step == 1) {
		export_texture_test_options("godot_material", 0, 0, 8, 0);
		export_tiled_set_resolution(512, 512);
		char *d = fresh_dir("material");
		export_texture_run(d, 0);
		check(count(d) == 4, string("material: %d files", count(d)));
		char *tres = find(d, ".tres");
		check(!string_equals(tres, ""), string("material: %s", tres));
		if (!string_equals(tres, "")) {
			char *s = sys_buffer_to_string(data_get_blob(tres));
			printf("%s", s);
			check(string_index_of(s, "ORMMaterial3D") >= 0 && string_index_of(s, "_base.png") >= 0 && string_index_of(s, "_nor.png") >= 0 &&
			          string_index_of(s, "_orm.png") >= 0,
			      "material: ORMMaterial3D of the three textures");
		}

		export_texture_test_options("godot_mesh", 0, 0, 8, 0);
		d = fresh_dir("mesh");
		export_texture_run(d, 0);
		check(count(d) == 1, string("mesh: %d files", count(d)));
		char *glb = find(d, ".glb");
		check(!string_equals(glb, ""), string("mesh: %s", glb));
		if (!string_equals(glb, "")) {
			buffer_t *b = data_get_blob(glb);
			check(buffer_get_u8(b, 0) == 103 && buffer_get_u8(b, 1) == 108 && buffer_get_u8(b, 2) == 84 && buffer_get_u8(b, 3) == 70, "mesh: glTF magic");
			check(b->length > 3 * 1000, string("mesh: %d bytes", b->length));
		}
		export_tiled_set_resolution(0, 0);
	}
	else {
		printf("[export_godot] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_GODOT PASS" : "EXPORT_GODOT FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = string("%s/godot_export", script_dir());
	iron_create_directory(root);
	script_brush_defaults();
	script_project_new();
	script_notify_on_next_frame(next);
}
