#include "global.h"

// Material Maker nodes in material shaders (nodes_material/mm_bridge.c): their own code, our nodes feeding them at the
// main uv and at others (tile2x2 reads its inputs at 2*uv), them feeding our nodes, nested both ways.
// Every check renders the flat node preview of a node and reads a texel.

int fails  = 0;
int checks = 0;
int step   = 0;
int size   = 16;
int bgra   = 0;

slot_material_t *mat = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[mm_bridge] %s %s", ok ? "ok  " : "FAIL", name);
}

int px_r = 0;
int px_g = 0;
int px_b = 0;
void texel(ui_node_t *node, int x, int y) {
	px_r                 = -1;
	px_g                 = -1;
	px_b                 = -1;
	gpu_texture_t *image = script_material_node_preview(node, NULL, size);
	buffer_t      *b     = script_get_texture_pixels(image);
	if (b == NULL) {
		return;
	}
	int i = (y * size + x) * 4;
	px_r  = buffer_get_u8(b, i + (bgra ? 2 : 0));
	px_g  = buffer_get_u8(b, i + 1);
	px_b  = buffer_get_u8(b, i + (bgra ? 0 : 2));
}

int near(int a, int b) {
	int d = a - b;
	return d >= -4 && d <= 4;
}

char *got() {
	return string("got %d %d %d", px_r, px_g, px_b);
}

ui_node_t *rgb(float r, float g, float b) {
	ui_node_t *n = script_canvas_create_node(NULL, "RGB", NULL);
	script_material_set_color(n, 0, 0, r, g, b, 1.0);
	return n;
}

ui_node_t *mm(char *type, int x, int y) {
	return script_material_create_node_at(type, x, y);
}

void test_own_code() {
	ui_node_t *red = rgb(1.0, 0.0, 0.0); // Texel order of the readback
	texel(red, size / 2, size / 2);
	bgra = px_b > 200 && px_r < 50;
	ui_node_t *uni = mm("MM_uniform", -400, 0);
	check(uni != NULL, "Material Maker uniform created");
	if (uni == NULL) {
		return;
	}
	texel(uni, size / 2, size / 2); // The library entry's color, not the definition's white
	check(near(px_r, 0) && near(px_g, 106) && near(px_b, 255), string("uniform renders its library color (%s)", got()));
	ui_node_t *fbm = mm("MM_fbm4", -400, 200);
	texel(fbm, size / 2, size / 2);
	check(px_r >= 0 && px_r == px_g && px_g == px_b, string("fbm4 renders a grey noise (%s)", got()));
}

void test_native_into_mm() {
	ui_node_t *tile = mm("MM_tile2x2", -200, 0);
	check(tile != NULL, "tile2x2 created");
	if (tile == NULL) {
		return;
	}
	ui_node_t *red = rgb(1.0, 0.0, 0.0);
	for (int i = 0; i < 4; ++i) {
		script_canvas_connect(NULL, red, 0, tile, i);
	}
	texel(tile, size / 2, size / 2);
	check(near(px_r, 255) && near(px_g, 0) && near(px_b, 0), string("our color read at 2*uv by tile2x2 (%s)", got()));

	// The uv itself: x at the texel's center is 8.5/16, tile2x2 reads 2*x - 1 there. The main uv would give 136.
	ui_node_t *coord = script_canvas_create_node(NULL, "TEX_COORD", NULL);
	for (int i = 0; i < 4; ++i) {
		script_canvas_connect(NULL, coord, 2, tile, i);
	}
	texel(tile, size / 2, size / 2);
	check(px_r >= 0 && px_r < 40, string("our UV evaluated at tile2x2's uv (%s)", got()));
	texel(tile, 4, size / 2); // x = 4.5/16, read at 9/16
	check(near(px_r, 143), string("our UV at 2*uv on the left half (%s)", got()));
}

void test_mm_into_native() {
	ui_node_t *uni = mm("MM_uniform", -400, 400);
	ui_node_t *inv = script_canvas_create_node(NULL, "INVERT_COLOR", NULL);
	script_canvas_connect(NULL, uni, 0, inv, 1);
	texel(inv, size / 2, size / 2);
	check(near(px_r, 255) && near(px_g, 149) && near(px_b, 0), string("our invert of a Material Maker color (%s)", got()));
}

void test_nested() {
	// UV -> tile2x2 -> invert (ours) -> tile2x2: the inner tile2x2 runs inside a scope at the outer one's uv
	ui_node_t *coord = script_canvas_create_node(NULL, "TEX_COORD", NULL);
	ui_node_t *t1    = mm("MM_tile2x2", -600, 600);
	ui_node_t *inv   = script_canvas_create_node(NULL, "INVERT_COLOR", NULL);
	ui_node_t *t2    = mm("MM_tile2x2", -200, 600);
	for (int i = 0; i < 4; ++i) {
		script_canvas_connect(NULL, coord, 2, t1, i);
		script_canvas_connect(NULL, inv, 0, t2, i);
	}
	script_canvas_connect(NULL, t1, 0, inv, 1);
	// x = 8.5/16: t2 reads at 2x-1 = 1/16, t1 there reads 2/16 (left half), inverted: 1 - 2/16
	texel(t2, size / 2, size / 2);
	check(near(px_r, 223), string("nested Material Maker / ours / Material Maker (%s)", got()));
}

void next() {
	if (step == 0) {
		mat = script_material_create("mm_bridge");
		test_own_code();
	}
	else if (step == 1) {
		test_native_into_mm();
	}
	else if (step == 2) {
		test_mm_into_native();
	}
	else if (step == 3) {
		test_nested();
	}
	else {
		script_material_delete(mat);
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[mm_bridge] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MM_BRIDGE PASS" : "MM_BRIDGE FAIL");
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
