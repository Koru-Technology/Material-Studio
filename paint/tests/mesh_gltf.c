#include "global.h"

// glTF import: a mesh of several primitives comes in whole

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int before = 0;

// One node, one mesh of two triangle primitives (3 vertices each, not indexed)
char *two_prims =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"name\":\"two_prims\"}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}},{\"attributes\":{\"POSITION\":1}}]}],"
    "\"buffers\":[{\"byteLength\":72,\"uri\":\"data:application/octet-stream;base64,"
    "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAAAgD8AAIA/\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36}],"
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,1],\"max\":[1,1,1]}]}";

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mesh_gltf] %s %s", ok ? "ok  " : "FAIL", name);
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		before     = script_get_mesh_objects()->length;
		char *path = "/tmp/materialstudio_two_prims.gltf";
		iron_file_save_bytes(path, sys_string_to_buffer(two_prims), 0);
		script_append_mesh(path);
	}
	else if (step == 1) {
		any_array_t *objects = script_get_mesh_objects();
		check(objects->length == before + 1, "the glTF mesh was appended");
		mesh_object_t *o = objects->length > before ? objects->buffer[objects->length - 1] : NULL;
		int            n = o != NULL ? o->data->index_array->length : 0;
		check(n == 6, string("both primitives are imported (%d indices)", n));
	}
	else {
		printf("[mesh_gltf] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MESH_GLTF PASS" : "MESH_GLTF FAIL");
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
