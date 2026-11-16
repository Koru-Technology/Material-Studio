#include "../global.h"

// Project files, version 31 (see the plan's "Project format v31"): uids, sizes, and the container (to come)

// ---- Test hooks (paint/tests/legacy_upgrade.c) ----

u32 project_test_layer_uid(i32 i) {
	return i >= 0 && i < g_project->_->layers->length ? g_project->_->layers->buffer[i]->uid : 0;
}

u32 project_test_material_uid(i32 i) {
	return i >= 0 && i < g_project->_->materials->length ? g_project->_->materials->buffer[i]->uid : 0;
}

u32 project_test_uid_next() {
	return project_uid_next;
}

// 0 width, 1 height of the project's layers
i32 project_test_res(i32 axis) {
	return axis == 0 ? config_get_texture_res_x() : config_get_texture_res_y();
}

// Layers resized to w x h (the Custom resolution of the layers tab), on the next frame
void project_test_set_res(i32 w, i32 h) {
	base_res   = TEXTURE_RES_CUSTOM;
	base_res_x = (f32)w;
	base_res_y = (f32)h;
	layers_on_resized();
}

// 31 bits of the content hash of the pixels of layer i's texture: 0 base (mask: its value), 1 normal, 2 pack. 0 for none.
i32 project_test_layer_hash(i32 i, i32 channel) {
	if (i < 0 || i >= g_project->_->layers->length) {
		return 0;
	}
	slot_layer_t  *l = g_project->_->layers->buffer[i];
	gpu_texture_t *t = channel == 0 ? l->texpaint : channel == 1 ? l->texpaint_nor : l->texpaint_pack;
	if (t == NULL) {
		return 0;
	}
	buffer_t *b = gpu_get_texture_pixels(t);
	hash128_t h = hash128(b->buffer, b->length);
	gpu_release_texture_pixels(t);
	return (i32)(h.lo & 0x7fffffff);
}
