#include "global.h"

// The files' bits are the export's own (context export_bits), apart from the layers' (base_bits). The export sheet
// opening (app_export_get, then app_export_set as png) and its pickers leave a 16 bit project's layers at 16 bit, their
// pixels and the undo steps as they were. Exports of either depth from layers of either: png of 16 bit layers as png of
// the same layers at 8 bit, 16 bit exr of 8 bit layers as of 16 bit ones.

int   fails  = 0;
int   checks = 0;
int   step   = 0;
int   frame  = 0;
char *root   = "";
int   format16 = 0;
int   nlayers  = 0;
int   hashes[24];
int   undos = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[export_bits] %s %s", ok ? "ok  " : "FAIL", name);
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

slot_layer_t *top() {
	return script_layer_at(script_layer_count() - 1);
}

void snapshot() {
	nlayers = script_layer_count();
	for (int i = 0; i < nlayers * 3 && i < 24; ++i) {
		hashes[i] = project_test_layer_hash(i / 3, i % 3);
	}
}

// The layers at 16 bit, their pixels and the undo steps as they were
int unchanged() {
	int ok = script_layer_count() == nlayers && history_test_undos() == undos;
	for (int i = 0; i < nlayers; ++i) {
		slot_layer_t *l = script_layer_at(i);
		ok              = ok && l->texpaint->format == format16;
	}
	for (int i = 0; i < nlayers * 3 && i < 24; ++i) {
		ok = ok && project_test_layer_hash(i / 3, i % 3) == hashes[i];
	}
	return ok;
}

int file_count(char *name, char *ext) {
	any_array_t *files = file_read_directory(string("%s/%s", root, name));
	int          n     = 0;
	for (int i = 0; files != NULL && i < files->length; ++i) {
		n = n + ends_with(files->buffer[i], ext);
	}
	return n;
}

// The png files of a and b within tol (1/255)
int similar_png(char *a, char *b, float tol) {
	any_array_t *fa = file_read_directory(string("%s/%s", root, a));
	int          ok = fa != NULL && fa->length == 5;
	for (int i = 0; ok && i < fa->length; ++i) {
		int d = script_diff_images(string("%s/%s/%s", root, a, fa->buffer[i]), string("%s/%s/%s", root, b, fa->buffer[i]), tol);
		if (d != 0) {
			printf("[export_bits]   %s: %d values over %.1f, max %.1f", fa->buffer[i], d, tol, script_diff_max());
			ok = 0;
		}
	}
	return ok;
}

// The exr files of a and b on an 8 x 8 grid within tol
int similar_exr(char *a, char *b, float tol) {
	any_array_t *fa = file_read_directory(string("%s/%s", root, a));
	int          ok = fa != NULL && fa->length == 5;
	float        mx = 0.0;
	for (int i = 0; ok && i < fa->length; ++i) {
		f32_array_t *s = f32_array_create(0);
		ok             = ok && script_image_read(string("%s/%s/%s", root, a, fa->buffer[i]));
		for (int k = 0; ok && k < 64 * 3; ++k) {
			f32_array_push(s, script_texel(0.06 + (k / 3 % 8) * 0.125, 0.06 + (k / 24) * 0.125, k % 3));
		}
		ok = ok && script_image_read(string("%s/%s/%s", root, b, fa->buffer[i]));
		for (int k = 0; ok && k < 64 * 3; ++k) {
			float d = fabs(script_texel(0.06 + (k / 3 % 8) * 0.125, 0.06 + (k / 24) * 0.125, k % 3) - s->buffer[k]);
			mx      = d > mx ? d : mx;
		}
	}
	printf("[export_bits]   %s and %s: max diff %.2f/255", a, b, mx * 255.0);
	return ok && mx <= tol / 255.0;
}

void build_scene() {
	context_t *ctx = script_get_context();
	script_layer_new_color(0xffcc2020, 0.7, 0.4, 0.3);
	slot_layer_t *green = script_layer_new_color(0xff20cc20, 0.5, 0.6, 0.1);
	green->mask_opacity = 0.6;
	green->blending     = 2; // Multiply
	script_layer_update(green);
	script_layer_new();
	context_select_tool(TOOL_TYPE_BRUSH);
	ctx->brush_radius = 0.3;
	for (int i = 0; i < 10; ++i) {
		script_paint(0.3 + i * 0.04, 0.4 + i * 0.02);
	}
	script_paint_end();
}

void next() {
	frame = frame + 1;
	if (frame < 3) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		script_project_new();
		layers_test_set_bits(16);
		export_texture_test_options("generic", 0, 0, 8, 0);
	}
	else if (step == 1) {
		build_scene();
	}
	else if (step == 2) { // The export sheet opening and its pickers
		timeline_finalize();
		format16 = top()->texpaint->format;
		snapshot();
		undos = history_test_undos();
		int f = app_export_test_sheet_open();
		check(f == 0, string("the sheet reads the export's format, png (%d)", f));
		check(unchanged(), "the sheet opening left the 16 bit layers as they were");
		export_texture_test_options("generic", 0, 0, 16, 0); // 16 bit exr picked before (the export box, Mac)
		f = app_export_test_sheet_open();
		check(f == 2, string("the sheet reads 16 bit exr (%d)", f));
		check(unchanged(), "the sheet setting png left the 16 bit layers as they were");
		app_export_test_pick(0, 1);
		app_export_test_pick(0, 0);
		check(unchanged(), "the pickers left the layers as they were");
	}
	else if (step == 3) { // Exports of the 16 bit layers
		export_tiled_test_tile(512);
		export_texture_test_options("generic", 0, 0, 8, 0);
		export_texture_run(fresh_dir("p16"), 0);
		export_texture_test_options("generic", 0, 0, 16, 0);
		export_texture_run(fresh_dir("e16"), 0);
		check(file_count("p16", ".png") == 5 && file_count("e16", ".exr") == 5,
		      string("16 bit layers: png and exr written (%d png, %d exr)", file_count("p16", ".png"), file_count("e16", ".exr")));
		check(unchanged(), "the exports left the 16 bit layers as they were");
		layers_test_set_bits(8); // The same layers at 8 bit
	}
	else if (step == 4) {
		export_texture_test_options("generic", 0, 0, 8, 0);
		export_texture_run(fresh_dir("p8"), 0);
		export_texture_test_options("generic", 0, 0, 16, 0);
		export_texture_run(fresh_dir("e8"), 0);
		check(top()->texpaint->format != format16, "the layers at 8 bit");
		check(similar_png("p16", "p8", 2.0), "png of 16 bit layers as png of the same layers at 8 bit");
		check(file_count("e8", ".exr") == 5 && similar_exr("e16", "e8", 2.0), "16 bit exr of 8 bit layers as of the 16 bit ones");
	}
	else {
		export_texture_test_options("generic", 0, 0, 8, 0);
		export_tiled_test_tile(0);
		script_project_new();
		printf("[export_bits] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "EXPORT_BITS PASS" : "EXPORT_BITS FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	root = script_tmp_dir("materialstudio_export_bits"); // Per run: suites of several worktrees run at once
	iron_create_directory(root);
	script_brush_defaults();
	script_notify_on_next_frame(next);
}
