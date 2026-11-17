#include "global.h"

// Mesh maps: textures named <name>_<map> register on import, reimport keeps them, clearing and deleting unregister them,
// the curvature is baked from the mesh without a map, and a curvature map replaces the Curvature Mask's own bake

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

asset_t      *ao    = NULL;
slot_layer_t *layer = NULL;
buffer_t     *before = NULL;

int AO        = 0;
int CURVATURE = 1;
int NORMAL    = 6;
int COUNT     = 7;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mesh_maps] %s %s", ok ? "ok  " : "FAIL", name);
}

// 64x64 image, a gradient or flat gray
void write_image(char *path, int gray) {
	int       w = 64;
	buffer_t *b = buffer_create(w * w * 4);
	for (int y = 0; y < w; ++y) {
		for (int x = 0; x < w; ++x) {
			int i = (y * w + x) * 4;
			int v = gray ? 128 : x * 4;
			buffer_set_u8(b, i, v);
			buffer_set_u8(b, i + 1, gray ? 128 : y * 4);
			buffer_set_u8(b, i + 2, v);
			buffer_set_u8(b, i + 3, 255);
		}
	}
	iron_write_png(path, b, w, w, 0);
}

char *test_path(char *name) {
	return string("%s/%s", script_save_path(), name);
}

asset_t *asset_named(char *name) {
	return script_get_asset(script_asset_index(name));
}

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

void next() {
	frame = frame + 1;
	if (frame < 8) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;
	material_studio_apply();

	if (step == 0) {
		write_image(test_path("koru_mm_test_ao.png"), 0);
		write_image(test_path("koru_mm_test2_ao.png"), 0);
		write_image(test_path("koru_mm_test_bent_normal.png"), 0);
		write_image(test_path("koru_mm_test_curvature.png"), 1);
		script_import_asset(test_path("koru_mm_test_ao.png"), 0);
	}
	else if (step == 1) {
		ao = asset_named("koru_mm_test_ao.png");
		check(ao != NULL && mesh_maps_get_asset(AO) == ao, "importing *_ao.png registers it");
		check(mesh_maps_texture(AO) == project_get_image(ao), "the registered image is the map texture");
		script_import_asset(test_path("koru_mm_test2_ao.png"), 0);
		script_import_asset(test_path("koru_mm_test_bent_normal.png"), 0);
	}
	else if (step == 2) {
		check(mesh_maps_get_asset(AO) == ao, "a second *_ao.png leaves the first registered");
		check(mesh_maps_get_asset(NORMAL) == NULL, "a bent normal is not a normal map");
		int none = 0;
		for (int i = 0; i < COUNT; ++i) {
			if (mesh_maps_texture(i) == NULL) {
				none = none + 1;
			}
		}
		check(none == 0, "every map has a texture");
		check(mesh_maps_auto_curvature() != NULL, "the curvature is baked from the mesh");
		check(mesh_maps_texture(CURVATURE) == mesh_maps_auto_curvature(), "without a map the baked curvature is used");
		project_reimport_texture(ao);
	}
	else if (step == 3) {
		asset_t *a = asset_named("koru_mm_test_ao.png");
		check(a != NULL && mesh_maps_get_asset(AO) == a, "reimport keeps the registration");
		check(mesh_maps_texture(AO) == project_get_image(a), "and the map is the reimported image");
		mesh_maps_set(AO, NULL);
		check(mesh_maps_get_asset(AO) == NULL, "set to none clears it");
		check(mesh_maps_texture(AO) != NULL && mesh_maps_texture(AO) != project_get_image(a), "cleared, the map is neutral");
		mesh_maps_set(AO, a);
		check(mesh_maps_get_asset(AO) == a, "set registers it again");
		script_assets_remove("koru_mm_test_ao.png");
		check(mesh_maps_get_asset(AO) == NULL, "deleting the texture clears the map");
		check(mesh_maps_texture(AO) != NULL, "and the map is neutral");
	}
	else if (step == 4) {
		material_studio_new();
		material_studio_add_layer(0); // Edge Wear, a Curvature Mask
	}
	else if (step == 5) {
		layer = script_layer_new_fill();
	}
	else if (step == 6) {
		before = copy_pixels(layer->texpaint);
		script_import_asset(test_path("koru_mm_test_curvature.png"), 0);
	}
	else if (step == 7) {
		asset_t *c = asset_named("koru_mm_test_curvature.png");
		check(c != NULL && mesh_maps_get_asset(CURVATURE) == c, "importing *_curvature.png registers it");
		check(mesh_maps_texture(CURVATURE) == project_get_image(c), "the curvature map replaces the baked one");
		int n = changed(layer->texpaint);
		check(n > 100, string("a flat curvature map takes the edge wear away (%d texels)", n));
		mesh_maps_set(CURVATURE, NULL);
	}
	else if (step == 8) {
		int n = changed(layer->texpaint);
		check(n < 100, string("without the map the mask bakes its own curvature again (%d texels)", n));
	}
	else {
		material_studio_close();
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[mesh_maps] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MESH_MAPS PASS" : "MESH_MAPS FAIL");
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
