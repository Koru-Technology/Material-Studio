#include "../global.h"

// Bake Mesh Maps dialog and Inpaint tool, based on the work of Falkor.
// Bakes mesh maps of the paint objects, optionally projected from a high poly file.
// Each map is saved as <name>_<map>.png and added to the project textures.
// The ray tracer runs natively on worker threads, see plugins/bake_maps/bake_maps.c.

#define BBM_MAP_COUNT     13
#define BBM_INPAINT_COLOR 0xa6ff0d0d

#ifdef WITH_PLUGINS

typedef struct {
	char *name;
	char *suffix;
	i32   type; // bake_type_t, 14 is cavity
	bool  enabled;
} bbm_map_t;

static bbm_map_t bbm_maps[BBM_MAP_COUNT] = {
    {"Normal", "normal", 1, true},
    {"Height", "height", 3, false},
    {"Ambient Occlusion", "ao", 10, true},
    {"Curvature", "curvature", 0, true},
    {"Thickness", "thickness", 13, true},
    {"Bent Normal", "bent_normal", 12, false},
    {"World Space Normal", "world_normal", 2, false},
    {"Position", "position", 5, true},
    {"Material ID", "material_id", 7, false},
    {"Object ID", "object_id", 8, false},
    {"Vertex Color", "vertex_color", 9, false},
    {"TexCoord", "texcoord", 6, false},
    {"Cavity", "cavity", 14, false},
};

static i32  bbm_tab      = 0;
static bool bbm_box_open = false;

static i32 bbm_res_index       = 1;
static f32 bbm_rays            = 64;
static i32 bbm_device          = 0;  // Raytraced maps on the GPU 0 or CPU 1
static f32 bbm_gpu_samples     = 16; // 64 rays each
static i32 bbm_axis            = 0;  // XYZ, X, Y, Z, -X, -Y, -Z
static i32 bbm_up_axis         = 0;  // Z, Y
static f32 bbm_ao_strength     = 1.0;
static f32 bbm_ao_radius       = 1.0;
static f32 bbm_ao_offset       = 1.0;
static f32 bbm_curv_strength   = 1.0;
static f32 bbm_curv_radius     = 1.0;
static f32 bbm_curv_offset     = 0.0;
static f32 bbm_curv_smooth     = 1;
static f32 bbm_cavity_size     = 2.0;
static f32 bbm_cavity_contrast = 1.0;

static bool  bbm_use_high    = false;
static char *bbm_high_path   = "";
static char *bbm_high_loaded = ""; // Path currently held by the ray tracer
static char *bbm_high_tried  = ""; // Do not retry a failing file every frame
static i32   bbm_high_count  = 0;
static bool  bbm_average     = true;
static f32   bbm_cage_dist   = 0.02;

static char *bbm_out_name   = "";
static char *bbm_auto_name  = ""; // Default name last applied, replaced while the user did not edit it
static char *bbm_out_folder = "";

static bool bbm_preview_fast = false; // Cheaper render while orbiting
static f32  bbm_yaw          = 0.8;
static f32  bbm_pitch        = 0.35;
static f32  bbm_zoom         = 1.0;
static bool bbm_show_low     = true;
static bool bbm_show_high    = false;
static bool bbm_show_cage    = true;
static bool bbm_show_miss    = true;

static bool bbm_running = false;
static i32  bbm_queue[BBM_MAP_COUNT];
static i32  bbm_queue_len  = 0;
static i32  bbm_queue_i    = 0;
static i32  bbm_done_count = 0;
static i32  bbm_bake_res   = 2048;
static bool bbm_add_layer  = false; // Add Baked Layer clicked, run outside of the ui pass

static i32            bbm_inpaint_mode    = 0;     // Copy, Smooth
static i32            bbm_inpaint_target  = 0;     // 0: every baked map, n: the n-th of them
static bool           bbm_inpaint_ready   = false; // Mask layout prepared for the current stroke
static bool           bbm_inpaint_tried   = false; // One attempt per stroke
static gpu_texture_t *bbm_inpaint_overlay = NULL;

static bool bbm_any_selected(i32 a, i32 b) {
	for (i32 i = 0; i < BBM_MAP_COUNT; ++i) {
		if (bbm_maps[i].enabled && (bbm_maps[i].type == a || bbm_maps[i].type == b)) {
			return true;
		}
	}
	return false;
}

static char *bbm_default_name() {
	if (g_context->paint_object != NULL && g_context->paint_object->base->name != NULL) {
		return g_context->paint_object->base->name;
	}
	return "bake";
}

static char *bbm_default_folder() {
	char *path = project_filepath_get();
	i32   i    = string_last_index_of(path, "/");
	i32   j    = string_last_index_of(path, "\\");
	if (j > i) {
		i = j;
	}
	if (i < 0) {
		return "";
	}
	return substring(path, 0, i);
}

static char *bbm_name() {
	return string_equals(bbm_out_name, "") ? bbm_default_name() : bbm_out_name;
}

static char *bbm_folder() {
	return string_equals(bbm_out_folder, "") ? bbm_default_folder() : bbm_out_folder;
}

static void bbm_unload_high() {
	bake_maps_load_high("");
	bbm_high_loaded = "";
	bbm_high_tried  = "";
	bbm_high_count  = 0;
}

static bool bbm_load_high() {
	if (string_equals(bbm_high_loaded, bbm_high_path)) {
		return bbm_high_count > 0;
	}
	bbm_high_tried = string_copy(bbm_high_path);
	bbm_high_count = bake_maps_load_high(bbm_high_path);
	if (bbm_high_count > 0) {
		bbm_high_loaded = string_copy(bbm_high_path);
	}
	return bbm_high_count > 0;
}

// Releases the high poly and the bake data, they can take gigabytes
static void bbm_release() {
	bake_maps_end();
	bbm_high_loaded = "";
	bbm_high_tried  = "";
	bbm_high_count  = 0;
}

static char *bbm_baked_material_name() {
	return string("%s Baked", bbm_name());
}

// Recreates the "<name> Baked" material with the baked Occlusion, Normal and Height maps linked to the output
static void bbm_update_material() {
	char *suffixes[3] = {"ao", "normal", "height"};
	i32   sockets[3]  = {2, 5, 7}; // Occlusion, Normal Map, Height of OUTPUT_MATERIAL_PBR
	i32   assets[3];
	bool  any = false;
	for (i32 k = 0; k < 3; ++k) {
		assets[k] = script_asset_index(string("%s%s%s_%s.png", bbm_out_folder, PATH_SEP, bbm_out_name, suffixes[k]));
		any       = any || assets[k] >= 0;
	}
	if (!any) {
		return;
	}

	char            *name = bbm_baked_material_name();
	slot_material_t *old  = script_get_material(name);
	if (old != NULL) {
		script_material_delete(old);
	}
	if (script_material_create(name) == NULL) {
		return;
	}
	// Image nodes go left of the default nodes
	ui_node_t *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
	ui_node_t *rgb = script_material_get_node("RGB");
	f32        x   = (rgb != NULL && rgb->x < out->x ? rgb->x : out->x) - 260;
	for (i32 k = 0; k < 3; ++k) {
		if (assets[k] < 0) {
			continue;
		}
		ui_node_t *n = script_material_create_node_at("TEX_IMAGE", x, 100 + 180 * k);
		script_material_set_button(n, 0, assets[k]);
		script_material_connect(n, 0, out, sockets[k]);
	}
	script_material_update();
	console_info(string("%s %s", tr("Baked maps linked in material"), name));
}

static void bbm_finish() {
	bbm_running = false;
	bake_maps_end_images();
	console_info(string("%s: %d", tr("Baked maps"), bbm_done_count));
	if (bbm_done_count > 0) {
		bbm_update_material();
	}
	if (!bbm_box_open) {
		bbm_release(); // Window was closed while baking
	}
	base_redraw_ui();
}

// Occlusion, bent normal and thickness from the low poly can bake on the GPU
static bool bbm_gpu_available() {
	return !bbm_use_high && bake_maps_gpu_supported();
}

static bool bbm_use_gpu(i32 type) {
	return bbm_device == 0 && bbm_gpu_available() && (type == 10 || type == 12 || type == 13);
}

static void bbm_start_next() {
	while (bbm_queue_i < bbm_queue_len) {
		i32  type = bbm_maps[bbm_queue[bbm_queue_i]].type;
		bool started;
		if (bbm_use_gpu(type)) {
			started = bake_maps_gpu_start(type, (i32)bbm_gpu_samples, bbm_ao_strength, bbm_ao_radius, bbm_ao_offset);
		}
		else {
			started = bake_maps_start(type, (i32)bbm_rays, bbm_axis, bbm_up_axis, bbm_use_high, bbm_bake_res, bbm_bake_res);
		}
		if (started) {
			return;
		}
		console_error(tr("Unable to bake, check that the mesh has a UV map"));
		bbm_queue_i = bbm_queue_len;
	}
	bbm_finish();
}

static void bbm_run() {
	bbm_queue_len = 0;
	for (i32 i = 0; i < BBM_MAP_COUNT; ++i) {
		if (bbm_maps[i].enabled) {
			bbm_queue[bbm_queue_len++] = i;
		}
	}
	if (bbm_queue_len == 0) {
		console_error(tr("No maps selected"));
		return;
	}
	if (string_equals(bbm_out_name, "")) {
		bbm_out_name = string_copy(bbm_default_name());
	}
	if (bbm_use_high) {
		if (string_equals(bbm_high_path, "")) {
			console_error(tr("Select a high poly mesh file or disable High Poly"));
			return;
		}
		if (!bbm_load_high()) {
			return;
		}
	}

	// Read the current paint objects, they may have changed since the preview
	if (bake_maps_set_low() == 0) {
		console_error(tr("Unable to bake, check that the mesh has a UV map"));
		return;
	}
	bake_maps_set_cage(bbm_cage_dist, bbm_average);
	bake_maps_set_ao(bbm_ao_strength, bbm_ao_radius, bbm_ao_offset);
	bake_maps_set_curvature(bbm_curv_strength, bbm_curv_radius, bbm_curv_offset, (i32)bbm_curv_smooth);
	bake_maps_set_cavity(bbm_cavity_size, bbm_cavity_contrast);

	i32 sizes[4]   = {1024, 2048, 4096, 8192};
	bbm_bake_res   = sizes[bbm_res_index];
	bbm_running    = true;
	bbm_tab        = 0; // The progress is the only tab
	bbm_queue_i    = 0;
	bbm_done_count = 0;
	bbm_start_next();
}

// Called every update, steps the bake and runs deferred actions
void box_bake_maps_update() {
	if (bbm_add_layer) {
		bbm_add_layer          = false;
		slot_material_t *baked = script_get_material(bbm_baked_material_name());
		if (baked != NULL) {
			script_material_set(baked);
			script_layer_new_fill();
		}
	}
	if (!bbm_running) {
		return;
	}
	iron_delay_idle_sleep();
	base_redraw_ui(); // Keep the progress and the Stop button live
	if (!bake_maps_step()) {
		return;
	}
	i32 i = bbm_queue[bbm_queue_i];
	if (bake_maps_save(bbm_out_folder, string("%s_%s.png", bbm_out_name, bbm_maps[i].suffix))) {
		bbm_done_count++;
	}
	bbm_queue_i++;
	bbm_start_next();
}

bool box_bake_maps_running() {
	return bbm_running;
}

static void bbm_folder_picked(char *path) {
	bbm_out_folder = string_copy(path);
}

static void bbm_folder_picked_and_run(char *path) {
	bbm_folder_picked(path);
	bbm_run();
}

static void bbm_high_picked(char *path) {
	bbm_high_path = string_copy(path);
	bbm_use_high  = true;
}

static void bbm_section(char *title) {
	ui_text(title, UI_ALIGN_LEFT, g_theme->SEPARATOR_COL);
}

static void bbm_draw_progress() {
	f32 progress = bake_maps_progress();
	i32 i        = bbm_queue[bbm_queue_i < bbm_queue_len ? bbm_queue_i : bbm_queue_len - 1];
	ui_text(string_tmp("%s %d/%d: %s %d%%", tr("Baking"), bbm_queue_i + 1, bbm_queue_len, tr(bbm_maps[i].name), (i32)(progress * 100)), UI_ALIGN_LEFT,
	        0x00000000);
	f32 total = (bbm_queue_i + progress) / bbm_queue_len;
	ui_slider(&total, "", 0.0, 1.0, true, 100, false, UI_ALIGN_CENTER, false);
	if (ui_icon_button(tr("Stop"), ICON_STOP, UI_ALIGN_CENTER)) {
		bake_maps_cancel();
		bbm_finish();
	}
}

static void bbm_draw_maps() {
	bbm_section(tr("Maps"));
	for (i32 i = 0; i < BBM_MAP_COUNT; i += 2) {
		ui_row2();
		ui_check(&bbm_maps[i].enabled, tr(bbm_maps[i].name), "");
		if (i + 1 < BBM_MAP_COUNT) {
			ui_check(&bbm_maps[i + 1].enabled, tr(bbm_maps[i + 1].name), "");
		}
		else {
			ui_end_element(); // Odd count, complete the row
		}
	}
	ui_row2();
	if (ui_button(tr("Select All"), UI_ALIGN_CENTER, "")) {
		for (i32 i = 0; i < BBM_MAP_COUNT; ++i) {
			bbm_maps[i].enabled = true;
		}
	}
	if (ui_button(tr("Select None"), UI_ALIGN_CENTER, "")) {
		for (i32 i = 0; i < BBM_MAP_COUNT; ++i) {
			bbm_maps[i].enabled = false;
		}
	}
}

static void bbm_draw_high_poly() {
	bbm_section(tr("High Poly"));
	ui_check(&bbm_use_high, tr("Bake from High Poly"), "");
	if (!bbm_use_high) {
		return;
	}
	ui_row(f32_array_create_from_raw_tmp((f32[]){0.8, 0.2}, 2));
	ui_text(string_equals(bbm_high_path, "") ? tr("No file selected") : bbm_high_path, UI_ALIGN_LEFT, 0x00000000);
	if (ui_button("...", UI_ALIGN_CENTER, "")) {
		ui_files_show("obj,fbx,gltf,glb", false, false, &bbm_high_picked);
	}
	if (bbm_high_count > 0 && string_equals(bbm_high_loaded, bbm_high_path)) {
		ui_text(string_tmp("%d %s", bbm_high_count, tr("triangles")), UI_ALIGN_LEFT, 0x00000000);
		f32 fit = bake_maps_high_fit();
		if (fit == 0.0) {
			ui_text(tr("Warning: the high poly does not overlap the low poly"), UI_ALIGN_LEFT, 0x00000000);
		}
		else if (fit < 0.5 || fit > 2.0) {
			ui_text(tr("Warning: the high poly and low poly sizes differ a lot"), UI_ALIGN_LEFT, 0x00000000);
		}
	}
	ui_check(&bbm_average, tr("Average Normals"), "");
}

static void bbm_draw_settings() {
	bbm_section(tr("Common"));
	string_array_t *res_combo = any_array_create_from_raw_tmp((void *[]){"1024", "2048", "4096", "8192"}, 4);
	ui_combo(&bbm_res_index, res_combo, tr("Resolution"), true, UI_ALIGN_LEFT, true);

	if (bbm_any_selected(2, 5) || bbm_any_selected(12, 12)) { // World space normal, position, bent normal
		string_array_t *up_combo = any_array_create_from_raw_tmp((void *[]){"Z", "Y"}, 2);
		ui_combo(&bbm_up_axis, up_combo, tr("Up Axis"), true, UI_ALIGN_LEFT, true);
	}
	if (bbm_any_selected(0, 0)) { // Curvature
		string_array_t *axis_combo = any_array_create_from_raw_tmp((void *[]){"XYZ", "X", "Y", "Z", "-X", "-Y", "-Z"}, 7);
		ui_combo(&bbm_axis, axis_combo, tr("Axis"), true, UI_ALIGN_LEFT, true);
	}
	if (bbm_any_selected(10, 13) || bbm_any_selected(12, 12)) { // Occlusion, thickness, bent normal
		bbm_section(tr("Raytracing"));
		if (bbm_gpu_available()) {
			string_array_t *device_combo = any_array_create_from_raw_tmp((void *[]){"GPU", "CPU"}, 2);
			ui_combo(&bbm_device, device_combo, tr("Bake on"), true, UI_ALIGN_LEFT, true);
		}
		if (bbm_device == 0 && bbm_gpu_available()) {
			ui_slider(&bbm_gpu_samples, tr("Samples"), 1, 128, true, 1, true, UI_ALIGN_RIGHT, true);
			bbm_gpu_samples = math_floor(bbm_gpu_samples);
			ui_text(string_tmp("%s (%d)", tr("64 rays per sample, at the layer resolution"), bake_maps_gpu_res()), UI_ALIGN_LEFT, 0x00000000);
		}
		else {
			ui_slider(&bbm_rays, tr("Rays"), 4, 512, true, 1, true, UI_ALIGN_RIGHT, true);
			bbm_rays = math_floor(bbm_rays);
		}
	}
	if (bbm_any_selected(10, 10)) {
		bbm_section(tr("Ambient Occlusion"));
		ui_slider(&bbm_ao_strength, tr("Strength"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&bbm_ao_radius, tr("Radius"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&bbm_ao_offset, tr("Offset"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	}
	if (bbm_any_selected(0, 0)) {
		bbm_section(tr("Curvature"));
		ui_slider(&bbm_curv_strength, tr("Strength"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&bbm_curv_radius, tr("Radius"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&bbm_curv_offset, tr("Offset"), -2.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&bbm_curv_smooth, tr("Smooth"), 0, 5, false, 1, true, UI_ALIGN_RIGHT, true);
		bbm_curv_smooth = math_floor(bbm_curv_smooth);
	}
	if (bbm_any_selected(14, 14)) {
		bbm_section(tr("Cavity"));
		ui_slider(&bbm_cavity_size, tr("Detail Size"), 1.0, 16.0, true, 10.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&bbm_cavity_contrast, tr("Contrast"), 0.5, 6.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	}
}

static void bbm_draw_preview() {
	bool has_high = bbm_use_high && !string_equals(bbm_high_path, "");
	if (has_high && (bbm_show_high || bbm_show_miss)) {
		if (!string_equals(bbm_high_loaded, bbm_high_path) && !string_equals(bbm_high_tried, bbm_high_path)) {
			bbm_load_high();
		}
	}
	else if (!has_high && !string_equals(bbm_high_loaded, "")) {
		bbm_unload_high();
	}
	bake_maps_set_cage(bbm_cage_dist, bbm_average);

	// Rendered only when something changed, the low poly is reread when the paint objects change
	i32            pw    = math_floor(math_min(g_ui->_w, 800)); // Software rendered, keep it cheap
	i32            ph    = math_floor(pw * 0.76);
	gpu_texture_t *image = bake_maps_preview(pw, ph, bbm_yaw, bbm_pitch, bbm_zoom, bbm_show_low, bbm_show_high, bbm_show_cage, bbm_show_miss, bbm_preview_fast);
	i32            state = image != NULL ? ui_image(image, 0xffffffff, -1) : UI_STATE_IDLE;
	if (state == UI_STATE_DOWN) {
		if (mouse_movement_x != 0.0 || mouse_movement_y != 0.0) {
			bbm_yaw          = bbm_yaw - mouse_movement_x * 0.01;
			bbm_pitch        = bbm_pitch + mouse_movement_y * 0.01;
			bbm_pitch        = bbm_pitch < -1.5 ? -1.5 : bbm_pitch > 1.5 ? 1.5 : bbm_pitch;
			bbm_preview_fast = true;
		}
	}
	else if (state == UI_STATE_HOVERED && mouse_wheel_delta != 0.0) {
		bbm_zoom         = bbm_zoom * (1.0 + mouse_wheel_delta * 0.1);
		bbm_zoom         = bbm_zoom < 0.2 ? 0.2 : bbm_zoom > 5.0 ? 5.0 : bbm_zoom;
		bbm_preview_fast = true;
	}
	else {
		bbm_preview_fast = false; // Interaction ended, refine with exact rays
	}

	ui_row4();
	ui_check(&bbm_show_low, tr("Low Poly"), "");
	g_ui->enabled = has_high;
	ui_check(&bbm_show_high, tr("High Poly"), "");
	g_ui->enabled = true;
	ui_check(&bbm_show_cage, tr("Cage"), "");
	g_ui->enabled = has_high;
	ui_check(&bbm_show_miss, tr("Missed Rays"), "");
	g_ui->enabled = true;
	ui_slider(&bbm_cage_dist, tr("Max Distance"), 0.001, 0.2, true, 1000.0, true, UI_ALIGN_RIGHT, true);
	if (!has_high) {
		ui_text(tr("Enable High Poly and pick a file to see the areas the rays miss"), UI_ALIGN_LEFT, 0x00000000);
	}
	else if (bbm_show_miss && bbm_high_count > 0) {
		ui_text(string_tmp("%.1f%% %s", bake_maps_miss_ratio() * 100.0, tr("of rays miss the high poly (red), increase Max Distance")), UI_ALIGN_LEFT,
		        0x00000000);
	}
}

static void bbm_draw_output() {
	bbm_section(tr("Output"));
	ui_text_input(&bbm_out_name, tr("Name"), UI_ALIGN_LEFT, true, false);
	ui_row(f32_array_create_from_raw_tmp((f32[]){0.8, 0.2}, 2));
	ui_text(string_equals(bbm_out_folder, "") ? tr("No folder selected") : bbm_out_folder, UI_ALIGN_LEFT, 0x00000000);
	if (ui_button("...", UI_ALIGN_CENTER, "")) {
		ui_files_show("png", true, false, &bbm_folder_picked);
	}
}

static void bbm_draw_buttons() {
	ui_separator(8, false);
	slot_material_t *baked = script_get_material(bbm_baked_material_name());
	ui_row3();
	if (ui_icon_button(tr("Close"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
	g_ui->enabled = baked != NULL;
	if (ui_icon_button(tr("Add Baked Layer"), ICON_LAYERS, UI_ALIGN_CENTER) && baked != NULL) {
		bbm_add_layer = true;
	}
	g_ui->enabled = true;
	if (g_ui->is_hovered) {
		ui_tooltip(tr("New fill layer with the baked occlusion, normal and height maps"));
	}
	if (ui_icon_button(tr("Bake"), ICON_BAKE, UI_ALIGN_CENTER)) {
		if (string_equals(bbm_out_folder, "")) {
			ui_files_show("png", true, false, &bbm_folder_picked_and_run);
		}
		else {
			bbm_run();
		}
	}
}

// Two column layout, settings on the left and the preview on the right
static f32 bbm_column_x;
static f32 bbm_column_y;
static f32 bbm_column_w;
static f32 bbm_column_bottom;

// 0 starts the left column, 1 the right column at the same height, -1 continues below both at full width
static void bbm_column(i32 column) {
	f32 gap = 8 * UI_SCALE();
	f32 lw  = math_floor(bbm_column_w * 0.42);
	if (column == 0) {
		bbm_column_x      = g_ui->_x;
		bbm_column_y      = g_ui->_y;
		bbm_column_w      = g_ui->_w;
		bbm_column_bottom = g_ui->_y;
		g_ui->_w          = math_floor(bbm_column_w * 0.42);
	}
	else if (column == 1) {
		bbm_column_bottom = math_max(bbm_column_bottom, g_ui->_y);
		g_ui->_x          = bbm_column_x + lw + gap;
		g_ui->_y          = bbm_column_y;
		g_ui->_w          = bbm_column_w - lw - gap;
	}
	else {
		g_ui->_x = bbm_column_x;
		g_ui->_y = math_max(bbm_column_bottom, g_ui->_y);
		g_ui->_w = bbm_column_w;
	}
}

static void bbm_draw_box() {
	if (bbm_running) {
		if (ui_tab(&bbm_tab, tr("Bake Mesh Maps"), false, -1, false)) {
			bbm_draw_progress();
		}
		return;
	}
	if (string_equals(bbm_out_name, "") || string_equals(bbm_out_name, bbm_auto_name)) {
		bbm_out_name  = string_copy(bbm_default_name());
		bbm_auto_name = bbm_out_name;
	}

	// Settings on the left, 3D preview on the right, buttons below both
	if (ui_tab(&bbm_tab, tr("Maps"), false, -1, false)) {
		bbm_column(0);
		bbm_draw_maps();
		bbm_draw_high_poly();
		bbm_draw_output();
		bbm_column(1);
		bbm_draw_preview();
		bbm_column(-1);
		bbm_draw_buttons();
	}
	if (ui_tab(&bbm_tab, tr("Settings"), false, -1, false)) {
		bbm_column(0);
		bbm_draw_settings();
		bbm_column(1);
		bbm_draw_preview();
		bbm_column(-1);
		bbm_draw_buttons();
	}
}

// The high poly and the bake data can take gigabytes, released with the window
static void bbm_on_hide() {
	bbm_box_open = false;
	if (!bbm_running) {
		bbm_release();
	}
}

void box_bake_maps_show() {
	if (string_equals(bbm_out_folder, "")) {
		bbm_out_folder = bbm_default_folder(); // Project folder when saved
	}
	bbm_box_open = true;
	ui_box_show_custom(&bbm_draw_box, 1100, 720, &bbm_on_hide, true, tr("Bake Mesh Maps"));
}

// Bakes the selected maps with the current settings into the project folder, for scripts
void box_bake_maps_bake() {
	if (string_equals(bbm_out_folder, "")) {
		bbm_out_folder = bbm_default_folder();
	}
	if (string_equals(bbm_out_folder, "")) {
		console_error(tr("Save the project first, the maps are saved next to it"));
		return;
	}
	if (!bbm_running) {
		bbm_run();
	}
}

// Inpaint tool: paint over bake artifacts, the baked maps are repaired on release

// Path of a baked map, as saved by the last bake
static char *bbm_map_path(i32 i) {
	return string("%s%s%s_%s.png", bbm_folder(), PATH_SEP, bbm_name(), bbm_maps[i].suffix);
}

// Maps with a texture in the project, repaired by the stroke
static i32 bbm_inpaint_targets(i32 *out) {
	i32 count = 0;
	for (i32 i = 0; i < BBM_MAP_COUNT; ++i) {
		if (script_asset_index(bbm_map_path(i)) >= 0) {
			out[count++] = i;
		}
	}
	return count;
}

static void bbm_set_overlay(gpu_texture_t *image) {
	bool reparse        = (bbm_inpaint_overlay == NULL) != (image == NULL);
	bbm_inpaint_overlay = image;
	if (reparse) {
		make_material_parse_mesh_material();
	}
	g_context->ddirty = 2;
}

void box_bake_maps_inpaint_stroke() {
	if (!bbm_inpaint_ready) {
		if (bbm_inpaint_tried) {
			return;
		}
		bbm_inpaint_tried = true;
		if (bbm_running) {
			console_error(tr("Inpaint is not available while baking"));
			return;
		}
		i32 targets[BBM_MAP_COUNT];
		i32 count = bbm_inpaint_targets(targets);
		if (count == 0) {
			console_error(tr("Inpaint: no baked maps found for this mesh, bake them first"));
			return;
		}
		if (!bake_maps_inpaint_begin(bbm_map_path(targets[0]))) {
			console_error(tr("Inpaint needs a mesh with a UV map"));
			return;
		}
		bbm_inpaint_ready = true;
	}
	bake_maps_inpaint_stroke();
	bbm_set_overlay(bake_maps_inpaint_mask());
}

void box_bake_maps_inpaint_stroke_end() {
	if (bbm_inpaint_ready) {
		i32 targets[BBM_MAP_COUNT];
		i32 count = bbm_inpaint_targets(targets);
		i32 done  = 0;
		for (i32 k = 0; k < count; ++k) {
			if (bbm_inpaint_target > 0 && bbm_inpaint_target != k + 1) {
				continue;
			}
			i32  i      = targets[k];
			bool vector = string_index_of(bbm_maps[i].suffix, "normal") >= 0;
			if (bake_maps_inpaint_apply(bbm_map_path(i), bbm_inpaint_mode, vector)) {
				done++;
			}
		}
		console_info(string("%s: %d", tr("Inpaint: maps repaired"), done));
	}
	bake_maps_inpaint_clear();
	bbm_set_overlay(NULL);
	bbm_inpaint_ready = false;
	bbm_inpaint_tried = false;
}

void box_bake_maps_inpaint_header() {
	string_array_t *mode_combo = any_array_create_from_raw_tmp((void *[]){tr("Copy"), tr("Smooth")}, 2);
	ui_combo(&bbm_inpaint_mode, mode_combo, tr("Mode"), true, UI_ALIGN_LEFT, true);

	i32             targets[BBM_MAP_COUNT];
	i32             count     = bbm_inpaint_targets(targets);
	string_array_t *map_combo = any_array_create_from_raw_tmp((void *[]){tr("All Baked Maps")}, 1);
	for (i32 k = 0; k < count; ++k) {
		any_array_push(map_combo, tr(bbm_maps[targets[k]].name));
	}
	if (bbm_inpaint_target > count) {
		bbm_inpaint_target = 0;
	}
	ui_combo(&bbm_inpaint_target, map_combo, tr("Maps"), false, UI_ALIGN_LEFT, true);

	ui_slider(&g_context->brush_radius, tr("Radius"), 0.01, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (count == 0) {
		ui_text(tr("No baked maps for this mesh"), UI_ALIGN_LEFT, 0x00000000);
	}
}

// Painted mask while an Inpaint stroke is in progress, tinted over the mesh
gpu_texture_t *box_bake_maps_inpaint_overlay() {
	return g_context->tool == TOOL_TYPE_INPAINT ? bbm_inpaint_overlay : NULL;
}

#else

void box_bake_maps_update() {}
bool box_bake_maps_running() {
	return false;
}
void box_bake_maps_show() {}
void box_bake_maps_bake() {}
void box_bake_maps_inpaint_stroke() {}
void box_bake_maps_inpaint_stroke_end() {}
void box_bake_maps_inpaint_header() {
	ui_text(tr("Not available on this platform"), UI_ALIGN_LEFT, 0x00000000);
}
gpu_texture_t *box_bake_maps_inpaint_overlay() {
	return NULL;
}

#endif

i32 box_bake_maps_inpaint_color() {
	return BBM_INPAINT_COLOR;
}
