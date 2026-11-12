#include "../global.h"

// ID masks: the project's ID map paints each part of the model a flat color. Picking parts on the mesh makes a mask of them.
// While picking, the viewport shows the ID map (TOOL_TYPE_COLORID) and each tap adds or removes the part under it.

#define UI_ID_MASK_MAX       16    // Parts in one mask, texels of texpaint_colorid_sel
#define UI_ID_MASK_TOLERANCE 0.02f // Per channel, compressed or filtered ID maps are not exact

static slot_layer_t *ui_id_mask_target       = NULL;  // Layer the mask is made for
static i32           ui_id_mask_prev_tool    = TOOL_TYPE_BRUSH;
static u32           ui_id_mask_colors[UI_ID_MASK_MAX];
static i32           ui_id_mask_count        = 0;
static i32           ui_id_mask_assets_before = 0; // Assets before importing an ID map, an invalid one is removed again
static char         *ui_id_mask_import_path  = NULL;
static ui_window_t  *ui_id_mask_bar          = NULL;
static i32           ui_id_mask_bar_count    = -1; // Count the bar was drawn with

asset_t *project_id_map_asset() {
	if (g_project->id_map == NULL) {
		return NULL;
	}
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		if (string_equals(a->name, g_project->id_map)) {
			return a;
		}
	}
	return NULL;
}

static bool ui_id_mask_same_color(u32 a, u32 b) {
	f32 t = UI_ID_MASK_TOLERANCE * 255.0f;
	return math_abs((f32)color_get_rb(a) - color_get_rb(b)) <= t && math_abs((f32)color_get_gb(a) - color_get_gb(b)) <= t &&
	       math_abs((f32)color_get_bb(a) - color_get_bb(b)) <= t;
}

// An ID map is made of a few flat colors: the 32 most common colors of a small copy cover nearly all of it.
// Photos, base colors, normal, curvature or occlusion maps are gradients and fail. Draws, call outside of a render pass.
bool project_is_id_map(asset_t *a) {
	gpu_texture_t *image = project_get_image(a);
	if (image == NULL) {
		return false;
	}
	const i32      size = 64;
	gpu_texture_t *rt   = gpu_create_render_target(size, size, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(rt, true, 0x00000000);
	draw_set_color(0xffffffff);
	draw_scaled_image(image, 0, 0, size, size);
	draw_end();
	buffer_t *pixels = gpu_get_texture_pixels(rt);

	// Count colors, open addressing over the opaque rgb
	const i32 slots = 8192;
	u32      *keys  = calloc(slots, sizeof(u32));
	i32      *hits  = calloc(slots, sizeof(i32));
	i32       total = size * size;
	for (i32 i = 0; i < total; ++i) {
		u32 c = 0xff000000 | ((u32)buffer_get_u8(pixels, i * 4 + 0) << 16) | ((u32)buffer_get_u8(pixels, i * 4 + 1) << 8) | (u32)buffer_get_u8(pixels, i * 4 + 2);
		u32 h = (c * 2654435761u) % slots;
		while (keys[h] != 0 && keys[h] != c) {
			h = (h + 1) % slots;
		}
		keys[h] = c;
		hits[h]++;
	}

	// Coverage of the most common colors
	i32 covered = 0;
	i32 colors  = 0;
	for (i32 k = 0; k < 32; ++k) {
		i32 best = -1;
		for (i32 i = 0; i < slots; ++i) {
			if (hits[i] > 0 && (best < 0 || hits[i] > hits[best])) {
				best = i;
			}
		}
		if (best < 0) {
			break;
		}
		if (hits[best] >= total / 100) {
			colors++; // A part, not a stray edge color
		}
		covered += hits[best];
		hits[best] = 0;
	}
	free(keys);
	free(hits);
	gpu_delete_texture(rt);

	return colors >= 2 && covered >= total * 0.9;
}

static void ui_id_mask_not_an_id_map(asset_t *a) {
	ui_box_show_message(tr("Not an ID Map"),
	                    string("%s\n\n%s", a->name,
	                           tr("This image is not an ID map. An ID map paints each part of the model a single flat color, without shading or gradients.")),
	                    false);
}

// Uses the asset as the project's ID map if it is one, tells why otherwise
bool project_set_id_map(asset_t *a) {
	if (a == NULL || !project_is_id_map(a)) {
		if (a != NULL) {
			ui_id_mask_not_an_id_map(a);
		}
		return false;
	}
	g_project->id_map = string_copy(a->name);
	g_context->ddirty = 2;
	return true;
}

// The ID map texture is deleted
void project_id_map_removed(asset_t *a) {
	if (g_project->id_map != NULL && string_equals(a->name, g_project->id_map)) {
		ui_id_mask_cancel();
		g_project->id_map = NULL;
	}
}

static void ui_id_mask_write_colors(void *_) {
	render_target_t *rt = any_map_get(render_path_render_targets, "texpaint_colorid_sel");
	draw_begin(rt->_image, true, 0x00000000); // Alpha 0 marks a free slot
	for (i32 i = 0; i < ui_id_mask_count; ++i) {
		draw_set_color(0xff000000 | ui_id_mask_colors[i]);
		draw_filled_rect(i, 0, 1, 1);
	}
	draw_end();
	draw_set_color(0xffffffff);
	g_context->ddirty = 2;
}

static void ui_id_mask_start_picking() {
	ui_id_mask_count        = 0;
	ui_id_mask_pick_pending = false;
	ui_id_mask_pressed      = false;
	ui_id_mask_prev_tool    = g_context->tool == TOOL_TYPE_COLORID ? TOOL_TYPE_BRUSH : g_context->tool;
	ui_id_mask_active       = true;
	ui_id_mask_bar_count    = -1;
	sys_notify_on_next_frame(&ui_id_mask_write_colors, NULL);
	context_select_tool(TOOL_TYPE_COLORID);
	base_redraw_ui();
}

static void ui_id_mask_import_on_next_frame(void *_) {
	asset_t *a = NULL;
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *b = g_project->_->assets->buffer[i];
		if (string_equals(b->file, ui_id_mask_import_path)) {
			a = b;
		}
	}
	if (a == NULL && g_project->_->assets->length > ui_id_mask_assets_before) {
		a = g_project->_->assets->buffer[g_project->_->assets->length - 1];
	}
	if (a == NULL) {
		return;
	}
	if (project_set_id_map(a)) {
		ui_id_mask_start_picking();
	}
	else if (g_project->_->assets->length > ui_id_mask_assets_before && array_index_of(g_project->_->assets, a) >= ui_id_mask_assets_before) {
		tab_textures_delete_texture(a); // Imported for this only
	}
}

static void ui_id_mask_import_done() {
	sys_notify_on_next_frame(&ui_id_mask_import_on_next_frame, NULL);
}

static void ui_id_mask_import_picked(char *path) {
	if (!path_is_texture(path)) {
		ui_box_show_message(tr("Not an ID Map"), tr("Pick an image file."), false);
		return;
	}
	ui_id_mask_import_path   = string_copy(path);
	ui_id_mask_assets_before = g_project->_->assets->length;
	import_asset_run(path, -1.0, -1.0, false, false, &ui_id_mask_import_done);
}

static void ui_id_mask_no_map_box() {
	ui_text(tr("This project has no ID map yet."), UI_ALIGN_LEFT, 0x00000000);
	ui_text(tr("An ID map paints each part of the model a flat color."), UI_ALIGN_LEFT, 0x00000000);
	ui_text(tr("Bake one in Substance, Marmoset or Blender, then import it."), UI_ALIGN_LEFT, 0x00000000);
	ui_row2();
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
	if (ui_icon_button(tr("Import ID Map..."), ICON_FOLDER_OPEN, UI_ALIGN_CENTER)) {
		ui_box_hide();
		ui_files_show(string_array_join(path_texture_formats(), ","), false, false, &ui_id_mask_import_picked);
	}
}

// The layer an ID mask is made for: the layer of a selected mask or filter
static slot_layer_t *ui_id_mask_layer() {
	slot_layer_t *l = g_context->layer;
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		l = l->parent;
	}
	return l;
}

static void ui_id_mask_begin_on_next_frame(void *_) {
	ui_id_mask_cancel();
	slot_layer_t *l = ui_id_mask_layer();
	if (l == NULL) {
		return;
	}
	if (l != g_context->layer) {
		context_set_layer(l);
	}
	ui_id_mask_target = l;
	if (project_id_map_asset() == NULL) {
		ui_box_show_custom(&ui_id_mask_no_map_box, 500, 220, NULL, true, tr("ID Mask"));
		return;
	}
	ui_id_mask_start_picking();
}

void ui_id_mask_begin() {
	sys_notify_on_next_frame(&ui_id_mask_begin_on_next_frame, NULL);
}

void ui_id_mask_cancel() {
	if (!ui_id_mask_active) {
		return;
	}
	ui_id_mask_active       = false;
	ui_id_mask_pick_pending = false;
	if (g_context->tool == TOOL_TYPE_COLORID) {
		context_select_tool(ui_id_mask_prev_tool);
	}
	base_redraw_ui();
}

// Adds the part with this color, or removes it when picked already
void ui_id_mask_toggle(u32 color) {
	if (!ui_id_mask_active) {
		return;
	}
	color = color & 0x00ffffff;
	for (i32 i = 0; i < ui_id_mask_count; ++i) {
		if (ui_id_mask_same_color(ui_id_mask_colors[i], color)) {
			ui_id_mask_colors[i] = ui_id_mask_colors[--ui_id_mask_count];
			sys_notify_on_next_frame(&ui_id_mask_write_colors, NULL);
			return;
		}
	}
	if (ui_id_mask_count < UI_ID_MASK_MAX) {
		ui_id_mask_colors[ui_id_mask_count++] = color;
		sys_notify_on_next_frame(&ui_id_mask_write_colors, NULL);
	}
}

i32 ui_id_mask_selected() {
	return ui_id_mask_active ? ui_id_mask_count : 0;
}

static void ui_id_mask_build(void *_m) {
	slot_layer_t *m   = _m;
	asset_t      *map = project_id_map_asset();
	slot_layer_clear(m, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);

	// The black mask is undone to, redo brings the picked parts back
	slot_layer_t *_layer = g_context->layer;
	g_context->layer     = m;
	if (history_undo_layers != NULL) {
		history_paint();
		history_steps->buffer[history_steps->length - 1]->name = tr("ID Mask");
	}
	g_context->layer = _layer;

	if (map != NULL) {
		gpu_texture_t   *image = project_get_image(map);
		render_target_t *sel   = any_map_get(render_path_render_targets, "texpaint_colorid_sel");
		_gpu_begin(m->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(pipes_colorid_to_mask);
		gpu_set_texture(pipes_texpaint_colorid, sel->_image);
		gpu_set_texture(pipes_tex_colorid, image);
		gpu_set_float2(pipes_size_colorid, image->width, image->height);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
	}

	g_context->layer_preview_dirty = true;
	layers_update_fill_layers();
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	base_redraw_ui();
}

// The picked parts become a mask of the layer
void ui_id_mask_done() {
	if (!ui_id_mask_active || ui_id_mask_count == 0) {
		return;
	}
	slot_layer_t *l = ui_id_mask_target;
	ui_id_mask_cancel(); // Back to the tool from before

	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *m        = layers_new_mask(false, l, -1);
	tab_layers_remap_all_layer_pointers(pointers);
	if (m == NULL) {
		console_error(tr("Too many layers"));
		return;
	}
	history_new_black_mask();
	sys_notify_on_next_frame(&ui_id_mask_build, m);
}

void ui_id_mask_update() {
	if (!ui_id_mask_active) {
		return;
	}
	// Another tool, layer or project ends the picking
	if (g_context->tool != TOOL_TYPE_COLORID || g_context->layer != ui_id_mask_target || project_id_map_asset() == NULL ||
	    keyboard_started("escape")) {
		ui_id_mask_cancel();
		return;
	}
	if (!mouse_down("left") && !pen_down("tip")) {
		ui_id_mask_pressed = false;
	}
}

// Bar on top of the 3D view: what to do, Cancel and Done
void ui_id_mask_render_ui() {
	if (!ui_id_mask_active || !base_view3d_show) {
		return;
	}
	if (ui_id_mask_bar == NULL) {
		ui_id_mask_bar = ui_window_create();
	}
	if (ui_id_mask_bar_count != ui_id_mask_count) {
		ui_id_mask_bar_count     = ui_id_mask_count;
		ui_id_mask_bar->redraws  = 2;
	}

	i32 pad = math_floor(10 * UI_SCALE());
	i32 w   = math_floor(320 * UI_SCALE());
	i32 h   = UI_ELEMENT_H() * 2 + UI_ELEMENT_OFFSET() * 3 + pad * 2;
	i32 x   = base_view_x(0) + math_floor((base_view_w() - w) / 2.0);
	i32 y   = base_y() + ui_header_h + pad;

	if (ui_base_panel_window(ui_id_mask_bar, x, y, w, h, UI_BASE_CORNER_TL | UI_BASE_CORNER_TR | UI_BASE_CORNER_BL | UI_BASE_CORNER_BR, false)) {
		g_ui->_x += pad;
		g_ui->_y += pad;
		g_ui->_w = w - pad * 2;
		char *picked = ui_id_mask_count == 0 ? tr("Tap the parts to mask") : string("%s: %d", tr("Parts"), ui_id_mask_count);
		ui_text(picked, UI_ALIGN_CENTER, 0x00000000);
		ui_row2();
		if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
			ui_id_mask_cancel();
		}
		g_ui->enabled = ui_id_mask_count > 0;
		if (ui_icon_button(tr("Done"), ICON_CHECK, UI_ALIGN_CENTER)) {
			ui_id_mask_done();
		}
		g_ui->enabled = true;
	}
	ui_end_window();
}
