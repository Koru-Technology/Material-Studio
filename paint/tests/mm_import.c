#include "global.h"

// Material Maker .ptex import (nodes_material/mm_import.c): library materials of different kinds become material graphs
// of MM nodes and an MM Material linked to the output, with every node and connection, compile without shader failures,
// preview, and survive save and load. The MM Material's normal follows our convention (a bumped surface rendered both
// ways). Every library material is imported once to count those that import cleanly.
// Previews go to <save path>/mm_import: <id>_base.png (Base Color of the MM Material, flat), <id>_sphere.png.

int fails  = 0;
int checks = 0;
int step   = 0;
int sub    = 0;
int bgra   = 0;
char *dir  = NULL;

// Library materials: kinds and node types (graph, text, remote, portal, unlit, iterate buffer, Easy SDF, switch)
int  ids[15]      = {2, 3, 846, 1217, 93, 4, 1582, 451, 171, 290, 542, 836, 1535, 129, 0};
int  buffered[15] = {0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0}; // Albedo samples a buffer (not rendered in materials yet)
int  flat[15]     = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}; // Albedo not linked: the albedo color
int  count        = 14;
int  cur          = 0;
any_array_t *mats   = NULL; // Imported materials
any_array_t *prints = NULL; // Their graphs before saving
int          failures_before = 0;
char        *file            = NULL;

// Bulk import
slot_material_t *bulk     = NULL;
int              bulk_i   = 0;
int              clean    = 0;
int              unknowns = 0;
int              lost     = 0;
int              nomat    = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mm_import] %s %s", ok ? "ok  " : "FAIL", name);
}

// Spread of a channel over the texels, 0 for a flat image
int spread(gpu_texture_t *tex, int size) {
	buffer_t *p = script_get_texture_pixels(tex);
	if (p == NULL) {
		return 0;
	}
	int best = 0;
	for (int c = 0; c < 3; ++c) {
		int lo = 255;
		int hi = 0;
		for (int i = 0; i < size * size; i += 7) {
			int v = buffer_get_u8(p, i * 4 + c);
			if (v < lo) {
				lo = v;
			}
			if (v > hi) {
				hi = v;
			}
		}
		if (hi - lo > best) {
			best = hi - lo;
		}
	}
	return best;
}

int px(gpu_texture_t *tex, int size, int x, int y, int c) {
	buffer_t *p = script_get_texture_pixels(tex);
	if (p == NULL) {
		return -1;
	}
	if (bgra && c != 1) {
		c = 2 - c;
	}
	return buffer_get_u8(p, (y * size + x) * 4 + c);
}

void save(gpu_texture_t *tex, int size, char *name) {
	if (tex != NULL) {
		iron_write_png(string("%s/%s.png", dir, name), script_get_texture_pixels(tex), size, size, 0);
	}
}

// The graph of a material as text: every node's type and Material Maker data, the links
char *fingerprint(slot_material_t *m) {
	char *s = string("%d nodes %d links;", m->canvas->nodes->length, m->canvas->links->length);
	for (int i = 0; i < m->canvas->nodes->length; ++i) {
		ui_node_t *n = m->canvas->nodes->buffer[i];
		char      *d = mm_material_is(n) ? mm_material_parameters_json(n) : mm_node_data_json(n);
		s            = string("%s%s:%s:%d,%d;", s, n->type, d, n->inputs->length, n->outputs->length);
	}
	for (int i = 0; i < m->canvas->links->length; ++i) {
		ui_node_link_t *l = m->canvas->links->buffer[i];
		s                 = string("%s%d.%d>%d.%d;", s, l->from_id, l->from_socket, l->to_id, l->to_socket);
	}
	return s;
}

void step_probe() {
	dir = string("%s/mm_import", script_save_path());
	iron_create_directory(dir);
	slot_material_t *m = script_material_create("mm_import_probe");
	ui_node_t       *n = script_canvas_create_node(NULL, "RGB", NULL);
	script_material_set_color(n, 0, 0, 1.0, 0.0, 0.0, 1.0);
	gpu_texture_t *t = mm_import_preview(n, 0, 8);
	buffer_t      *p = script_get_texture_pixels(t);
	bgra             = buffer_get_u8(p, 2) > 200 ? 1 : 0;
	printf("[mm_import] readback %s, previews in %s", bgra ? "bgra" : "rgba", dir);
	check(mm_nodes_count_all() > 400, string("every node definition registered for imports (%d types)", mm_nodes_count_all()));
	failures_before = gpu_shader_failures();
	mats            = any_array_create(0);
	prints          = any_array_create(0);
}

// Imports ids[cur], checks its nodes and links against the .ptex
void step_import() {
	int              id = ids[cur];
	slot_material_t *m  = mm_import_library(id);
	any_array_push(mats, m);
	int ptex_nodes = mm_import_count(0);
	int made       = mm_import_count(1);
	int dropped    = mm_import_count(2);
	int unknown    = mm_import_count(3);
	int ptex_links = mm_import_count(4);
	int links      = mm_import_count(5);
	int lostl      = mm_import_count(6);
	char *what     = string("%d %s (%s)", id, mm_import_library_name(id), mm_import_library_kind(id));
	check(m != NULL && mm_import_material_node(m) != NULL, string("%s imports with its material node", what));
	check(unknown == 0 && made == ptex_nodes - dropped,
	      string("%s: %d of %d nodes (%d comments / debug / export dropped, %d unknown)", what, made, ptex_nodes, dropped, unknown));
	check(lostl == 0 && links == ptex_links, string("%s: %d of %d connections", what, links, ptex_links));
	if (unknown > 0 || lostl > 0) {
		printf("[mm_import] %s", mm_import_report());
	}
}

// Next frame: compiled, previewed
void step_check() {
	int              id = ids[cur];
	slot_material_t *m  = mats->buffer[cur];
	ui_node_t       *mn = mm_import_material_node(m);
	check(gpu_shader_failures() == failures_before, string("%d compiles without shader failures (%d)", id, gpu_shader_failures() - failures_before));
	failures_before = gpu_shader_failures();
	script_material_set(m);
	gpu_texture_t *base = mm_import_preview(mn, 0, 256);
	save(base, 256, string("%d_base", id));
	mm_import_save_png(m->image, string("%s/%d_sphere.png", dir, id));
	int s = spread(base, 256);
	if (flat[cur]) {
		check(s == 0, string("%d base color is its albedo color, the albedo is not linked (spread %d)", id, s));
	}
	else if (!buffered[cur]) {
		check(s > 12, string("%d base color preview has a pattern (spread %d)", id, s));
	}
	else {
		printf("[mm_import] %d albedo samples a buffer, preview spread %d", id, s);
	}
	any_array_push(prints, fingerprint(m));
}

// Normal convention: a dome (shape) through Material Maker's normal map into the MM Material, previewed flat: right of the
// dome's center the normal leans right (red > 0.5), towards uv.y = 0 (up in our tangent space, OpenGL) green > 0.5.
// Rendered on the preview sphere three ways: converted (A), Material Maker's normal_tex as is (B), the app's own relief
// from the height of the same dome (C, the reference).
char *dome = "{\"nodes\":[{\"name\":\"shape\",\"type\":\"shape\",\"node_position\":{\"x\":0,\"y\":0},\"parameters\":{\"shape\":0,\"sides\":2,\"radius\":0.9,\"edge\":1.0}},"
             "{\"name\":\"nm\",\"type\":\"normal_map2\",\"node_position\":{\"x\":200,\"y\":0},\"parameters\":{\"param2\":0,\"size\":10,\"strength\":1,\"buffer\":false}},"
             "{\"name\":\"Material\",\"type\":\"material\",\"node_position\":{\"x\":400,\"y\":0},\"parameters\":{\"albedo_color\":{\"type\":\"Color\",\"r\":0.8,\"g\":0.8,\"b\":0.8,\"a\":1},"
             "\"metallic\":0,\"roughness\":0.5,\"normal\":1,\"depth_scale\":0.5}}],"
             "\"connections\":[{\"from\":\"shape\",\"from_port\":0,\"to\":\"nm\",\"to_port\":0},{\"from\":\"nm\",\"from_port\":0,\"to\":\"Material\",\"to_port\":4}]}";
// Domes tiled 8 x 8 with a strong normal map, for the sphere renders
char *domes = "{\"nodes\":[{\"name\":\"shape\",\"type\":\"shape\",\"node_position\":{\"x\":0,\"y\":0},\"parameters\":{\"shape\":0,\"sides\":2,\"radius\":0.9,\"edge\":1.0}},"
              "{\"name\":\"tile\",\"type\":\"transform2\",\"node_position\":{\"x\":100,\"y\":0},\"parameters\":{\"translate_x\":0,\"translate_y\":0,\"rotate\":0,\"scale_x\":0.125,\"scale_y\":0.125,\"mode\":1}},"
              "{\"name\":\"nm\",\"type\":\"normal_map2\",\"node_position\":{\"x\":200,\"y\":0},\"parameters\":{\"param2\":0,\"size\":10,\"strength\":3,\"buffer\":false}},"
              "{\"name\":\"inv\",\"type\":\"invert\",\"node_position\":{\"x\":200,\"y\":200},\"parameters\":{}},"
              "{\"name\":\"Material\",\"type\":\"material\",\"node_position\":{\"x\":400,\"y\":0},\"parameters\":{\"albedo_color\":{\"type\":\"Color\",\"r\":0.8,\"g\":0.8,\"b\":0.8,\"a\":1},"
              "\"metallic\":0,\"roughness\":0.5,\"normal\":1,\"depth_scale\":0.5}}],"
              "\"connections\":[{\"from\":\"shape\",\"from_port\":0,\"to\":\"tile\",\"to_port\":0},{\"from\":\"tile\",\"from_port\":0,\"to\":\"nm\",\"to_port\":0},"
              "{\"from\":\"tile\",\"from_port\":0,\"to\":\"inv\",\"to_port\":0},{\"from\":\"nm\",\"from_port\":0,\"to\":\"Material\",\"to_port\":4}]}";
slot_material_t *n1 = NULL; // Single dome, flat checks
slot_material_t *na = NULL;
slot_material_t *nb = NULL;
slot_material_t *nc = NULL;

void step_normals() {
	n1 = script_material_create("mm_normal_dome");
	mm_import_ptex(dome, n1);
	na = script_material_create("mm_normal_converted");
	mm_import_ptex(domes, na);
	nb = script_material_create("mm_normal_raw");
	mm_import_ptex(domes, nb);
	nc = script_material_create("mm_normal_height");
	mm_import_ptex(domes, nc);
	// B: Material Maker's normal map straight into the output
	script_material_set(nb);
	script_material_connect(script_material_get_node("MM_normal_map2"), 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 5);
	// C: no normal map, the domes as depth (Material Maker's depth is 1 - height): the app's own relief
	script_material_set(nc);
	ui_node_t *m = script_material_get_node("MM_MATERIAL");
	script_material_disconnect(m, 4);
	script_material_connect(script_material_get_node("MM_invert"), 0, m, 6);
	mm_import_compile(n1);
	mm_import_compile(na);
	mm_import_compile(nb);
	mm_import_compile(nc);
}

void step_normals_check() {
	script_material_set(n1);
	ui_node_t     *m = script_material_get_node("MM_MATERIAL");
	gpu_texture_t *t = mm_import_preview(m, 5, 64);
	// Where uv.y = 0 is: a texture coordinate preview, green is v
	ui_node_t     *tc = script_material_create_node("TEX_COORD");
	gpu_texture_t *tt = tc != NULL ? mm_import_preview(tc, 2, 64) : NULL;
	int            top = tt != NULL && px(tt, 64, 32, 4, 1) < px(tt, 64, 32, 59, 1) ? 1 : 0; // Row 0 is v = 0
	if (tc != NULL) {
		script_material_remove_node(tc);
	}
	int right = px(t, 64, 44, 32, 0);
	int left  = px(t, 64, 20, 32, 0);
	int up    = px(t, 64, 32, top ? 20 : 44, 1);
	int down  = px(t, 64, 32, top ? 44 : 20, 1);
	save(t, 64, "normal_flat_converted");
	check(right > 140 && left < 116, string("converted normal leans right of the dome's center (red %d right, %d left)", right, left));
	check(up > 140 && down < 116, string("converted normal leans up above it, OpenGL green (green %d above, %d below, row 0 is v = %s)", up, down, top ? "0" : "1"));
	int da = mm_import_image_diff(na->image, nc->image);
	int db = mm_import_image_diff(nb->image, nc->image);
	mm_import_save_png(na->image, string("%s/normal_sphere_A_converted.png", dir));
	mm_import_save_png(nb->image, string("%s/normal_sphere_B_raw_mm.png", dir));
	mm_import_save_png(nc->image, string("%s/normal_sphere_C_height_reference.png", dir));
	printf("[mm_import] sphere previews differ from the height relief by %d (converted) and %d (Material Maker's as is)", da, db);
	check(da >= 0 && da < db, string("converted normals shade like the height relief, closer than Material Maker's own (%d < %d)", da, db));
}

void step_bulk() {
	if (bulk == NULL) {
		bulk = script_material_create("mm_import_bulk");
	}
	int n = mm_import_library_count();
	for (int k = 0; k < 40 && bulk_i < n; ++k) {
		int  id = mm_import_library_id(bulk_i);
		bool ok = mm_import_library_into(id, bulk);
		if (!ok) {
			nomat = nomat + 1;
		}
		if (mm_import_count(3) > 0) {
			unknowns = unknowns + 1;
			printf("[mm_import] %d: %s", id, mm_import_report());
		}
		if (mm_import_count(6) > 0) {
			lost = lost + 1;
			printf("[mm_import] %d: %s", id, mm_import_report());
		}
		if (ok && mm_import_count(3) == 0 && mm_import_count(6) == 0) {
			clean = clean + 1;
		}
		bulk_i = bulk_i + 1;
	}
}

any_array_t *names = NULL;


void next() {
	int n_cases = count * 2;
	if (step == 0) {
		step_probe();
	}
	else if (step <= n_cases) { // Import, then check on the next frame
		cur = (step - 1) / 2;
		if ((step - 1) % 2 == 0) {
			step_import();
		}
		else {
			step_check();
		}
	}
	else if (step == n_cases + 1) {
		step_normals();
	}
	else if (step == n_cases + 2) {
		// Compiled on this frame
	}
	else if (step == n_cases + 3) {
		step_normals_check();
		// Opening a .ptex file: the app's import path makes a new material
		char     *path = string("%s/mm_import_dome.ptex", dir);
		buffer_t *b    = sys_string_to_buffer(dome);
		iron_file_save_bytes(path, b, b->length);
		int before = script_material_count();
		check(path_is_known(path), ".ptex is a known file type");
		import_asset_run(path, -1.0, -1.0, 0, 1, NULL);
		slot_material_t *m = script_get_material("mm_import_dome");
		check(script_material_count() == before + 1 && m != NULL && mm_import_material_node(m) != NULL, "opening a .ptex file imports it as a new material");
		names = any_array_create(0);
		for (int i = 0; i < mats->length; ++i) {
			slot_material_t *m = mats->buffer[i];
			any_array_push(names, string_copy(m->canvas->name));
		}
		file = string("%s/mm_import_test.arm", script_save_path());
		project_filepath_set(file);
		project_save(0);
	}
	else if (step == n_cases + 4) {
		script_project_new();
	}
	else if (step == n_cases + 5) {
		script_project_open(file);
	}
	else if (step == n_cases + 6) {
		int same = 0;
		for (int i = 0; i < names->length; ++i) {
			slot_material_t *m = script_get_material(names->buffer[i]);
			if (m == NULL) {
				check(0, string("%s survives save and load", names->buffer[i]));
				continue;
			}
			char *f = fingerprint(m);
			if (string_equals(f, prints->buffer[i])) {
				same = same + 1;
			}
			else {
				printf("[mm_import] %s differs after reload", names->buffer[i]);
				printf("[mm_import] before %s", prints->buffer[i]);
				printf("[mm_import] after  %s", f);
			}
		}
		check(same == names->length, string("imported graphs survive save and load (%d of %d identical)", same, names->length));
		failures_before = gpu_shader_failures();
	}
	else if (bulk_i < mm_import_library_count()) {
		step_bulk();
		step = step - 1; // Again on the next frame, until every material is imported
	}
	else {
		int n = mm_import_library_count();
		printf("[mm_import] library: %d of %d import cleanly, %d with unknown nodes, %d with lost connections, %d without material node", clean, n,
		       unknowns, lost, nomat);
		check(clean >= n * 9 / 10, string("library materials import cleanly (%d of %d)", clean, n));
		check(gpu_shader_failures() == failures_before, "no shader failed to compile after reload");
		printf("[mm_import] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MM_IMPORT PASS" : "MM_IMPORT FAIL");
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
