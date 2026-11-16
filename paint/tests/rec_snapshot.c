#include "global.h"

// Material snapshots keep what a stroke painted with (paint_mat_snapshot.c): a stroke is painted with a material that
// reads an image, a group holding another group, a material through a Material node and a Material Maker image node's
// file. Then the groups' and the nested material's colors are edited, the image asset is deleted (and its file), the
// groups and the nested material are removed, the Material Maker image's file is overwritten. The state before the stroke is restored and the stroke replayed: it must be byte-equal to the live
// stroke, without falling back. Then again after the project is saved and opened: the snapshot's files
// are saved with it, as its children (and the Material Maker image is read again, from them).

int fails  = 0;
int checks = 0;
int frame  = 0;

char *dir = "";
char *png = "";
char *mmpng = "";
int   uid = 0;

char *h0 = NULL;
char *h1 = NULL;
char *h2 = NULL;

ui_node_canvas_t *inner      = NULL;
ui_node_canvas_t *outer      = NULL;
ui_node_t        *inner_rgb  = NULL;
ui_node_t        *nested_rgb = NULL;
slot_material_t  *nested     = NULL;
slot_material_t  *mat        = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[rec_snapshot] %s %s", ok ? "ok  " : "FAIL", name);
}

char *hash_of(gpu_texture_t *t) {
	return t != NULL ? script_texture_hash(t) : "";
}

slot_layer_t *find_uid(int u) {
	for (int i = 0; i < script_layer_count(); ++i) {
		if (script_layer_at(i)->uid == u) {
			return script_layer_at(i);
		}
	}
	return NULL;
}

ui_node_t *io(ui_node_canvas_t *g, char *type) {
	for (int i = 0; i < g->nodes->length; ++i) {
		ui_node_t *n = g->nodes->buffer[i];
		if (string_equals(n->type, type)) {
			return n;
		}
	}
	return NULL;
}

ui_node_t *rgb(ui_node_canvas_t *canvas, float r, float g, float b) {
	ui_node_t *n = script_canvas_create_node(canvas, "RGB", NULL);
	script_material_set_color(n, 0, 0, r, g, b, 1.0);
	return n;
}

ui_node_t *mix_half(ui_node_canvas_t *canvas) {
	ui_node_t *n = script_canvas_create_node(canvas, "MIX_RGB", NULL);
	script_material_set_float(n, 1, 0, 0.5);
	return n;
}

void write_image(char *path, int seed) {
	int       w   = 64;
	buffer_t *buf = buffer_create(w * w * 4);
	for (int y = 0; y < w; ++y) {
		for (int x = 0; x < w; ++x) {
			int i = (y * w + x) * 4;
			buffer_set_u8(buf, i, (x * 4 + seed) % 256);
			buffer_set_u8(buf, i + 1, y * 4);
			buffer_set_u8(buf, i + 2, (x * y + seed) % 256);
			buffer_set_u8(buf, i + 3, 255);
		}
	}
	iron_write_png(path, buf, w, w, 0);
}

void make_materials() {
	inner = script_material_group_create("RS Inner");
	script_material_group_add_socket(inner, 1, "RGBA", NULL);
	inner_rgb = rgb(inner, 0.9, 0.1, 0.1);
	script_canvas_connect(inner, inner_rgb, 0, io(inner, "GROUP_OUTPUT"), 0);

	outer = script_material_group_create("RS Outer");
	script_material_group_add_socket(outer, 1, "RGBA", NULL);
	ui_node_t *in_node = script_canvas_create_node(outer, "GROUP", "RS Inner");
	script_canvas_connect(outer, in_node, 0, io(outer, "GROUP_OUTPUT"), 0);

	nested         = script_material_create("rs_nested");
	int nested_idx = script_material_count() - 1;
	nested_rgb     = rgb(NULL, 0.1, 0.8, 0.2);
	script_canvas_connect(NULL, nested_rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();

	mat           = script_material_create("rs_main");
	ui_node_t *im = script_canvas_create_node(NULL, "TEX_IMAGE", NULL);
	script_material_set_button(im, 0, script_asset_index("rec_snapshot.png"));
	ui_node_t *gn = script_canvas_create_node(NULL, "GROUP", "RS Outer");
	ui_node_t *mn = script_canvas_create_node(NULL, "MATERIAL", NULL);
	script_material_set_button(mn, 0, nested_idx);
	ui_node_t *mm = script_canvas_create_node(NULL, "MM_image", NULL);
	mm_node_set_data_json(mm, string("{\"parameters\":{\"image\":\"%s\"}}", mmpng), mat->canvas);
	check(string_index_of(mm_node_parameters_json(mm), "rec_snapshot_mm.png") >= 0, string("the Material Maker image reads the file (%s)", mm_node_parameters_json(mm)));
	ui_node_t *m1 = mix_half(NULL);
	ui_node_t *m2 = mix_half(NULL);
	ui_node_t *m3 = mix_half(NULL);
	script_canvas_connect(NULL, im, 0, m1, 1);
	script_canvas_connect(NULL, gn, 0, m1, 2);
	script_canvas_connect(NULL, m1, 0, m2, 1);
	script_canvas_connect(NULL, mn, 0, m2, 2);
	script_canvas_connect(NULL, m2, 0, m3, 1);
	script_canvas_connect(NULL, mm, 0, m3, 2);
	script_canvas_connect(NULL, m3, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
}

void setup() {
	script_material_set(mat);
	script_brush_defaults();
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.3;
	ctx->brush_opacity = 1.0;
	ctx->brush_spacing = 0.05;
	context_select_tool(TOOL_TYPE_BRUSH);
	app_layer_new(0);
	make_paint_set_batching(1);
	render_path_paint_set_stamp_seed(4242);
}

void stroke() {
	for (int i = 0; i < 30; ++i) {
		script_paint(0.35 + i * 0.01, 0.45 + (i % 7) * 0.004);
	}
	script_paint_end();
}

void note() {
	slot_layer_t *l = script_get_context()->layer;
	h0              = hash_of(l->texpaint);
	h1              = hash_of(l->texpaint_nor);
	h2              = hash_of(l->texpaint_pack);
	uid             = l->uid;
}

void compare(char *when) {
	slot_layer_t *l = find_uid(uid);
	if (l == NULL) {
		check(0, string("%s: the layer is there", when));
		return;
	}
	int n    = timeline_test_count(l);
	int kind = timeline_test_kind(l, n - 1);
	check(kind == 1, string("%s: the stroke is a TL_STROKE (kind %d, %d entries)", when, kind, n));
	timeline_test_materialize(l, n - 1);
	check(!string_equals(h0, hash_of(l->texpaint)), string("%s: the state before the stroke differs", when));
	int fb  = paint_replay_test_fallbacks();
	int ok  = paint_replay_test_entry(l, n - 1);
	int eq0 = string_equals(h0, hash_of(l->texpaint));
	int eq1 = string_equals(h1, hash_of(l->texpaint_nor));
	int eq2 = string_equals(h2, hash_of(l->texpaint_pack));
	check(ok && paint_replay_test_fallbacks() == fb, string("%s: it replays", when));
	check(eq0 && eq1 && eq2, string("%s: the replay is byte-equal to the live stroke (base %d, nor %d, pack %d)", when, eq0, eq1, eq2));
}

void next() {
	frame = frame + 1;
	if (frame == 2) {
		make_materials();
	}
	else if (frame == 3) {
		setup();
	}
	else if (frame == 6) {
		stroke();
		paint_rec_flush();
		note();
	}
	else if (frame == 8) {
		compare("as painted");
	}
	else if (frame == 9) { // Everything the material read changes or goes
		script_material_set_color(inner_rgb, 0, 0, 0.1, 0.1, 0.9, 1.0);
		script_material_set_color(nested_rgb, 0, 0, 0.9, 0.9, 0.1, 1.0);
		script_texture_delete("rec_snapshot.png");
		iron_delete_file(png);
		write_image(mmpng, 128); // Same path, other pixels
		check(script_asset_index("rec_snapshot.png") < 0, "the image asset is deleted");
	}
	else if (frame == 11) {
		script_material_group_remove(outer);
		script_material_group_remove(inner);
		script_material_set(mat);
		script_material_delete(nested);
		script_material_update();
	}
	else if (frame == 14) {
		compare("edited");
	}
	else if (frame == 16) {
		iron_create_directory(dir);
		project_filepath_set(string("%s/rec_snapshot.arm", dir));
		project_save(0);
	}
	else if (frame == 18) {
		script_project_new();
	}
	else if (frame == 20) {
		script_project_open(string("%s/rec_snapshot.arm", dir));
	}
	else if (frame == 24) {
		check(find_uid(uid) != NULL && string_equals(h0, hash_of(find_uid(uid)->texpaint)), "the layer as saved after opening");
		compare("saved and opened");
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		printf("[rec_snapshot] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "REC_SNAPSHOT PASS" : "REC_SNAPSHOT FAIL");
		script_quit();
		return;
	}
	script_notify_on_next_frame(next);
}

void main() {
	dir = script_tmp_dir("materialstudio_rec_snapshot");
	png = string("%s/rec_snapshot.png", dir);
	mmpng = string("%s/rec_snapshot_mm.png", dir);
	script_brush_defaults();
	script_project_new();
	iron_create_directory(dir);
	write_image(png, 0);
	write_image(mmpng, 60);
	script_import_asset(png, 0);
	script_notify_on_next_frame(next);
}
