
#include "global.h"

void script_show_envmap(bool b) {
	g_context->show_envmap = b;
	context_load_envmap();
	g_context->ddirty = 2;
}

static transform_t *_script_tween_transform = NULL;

static void script_timer_done(void *fn) {
	minic_call_fn(fn, NULL, 0);
}

void script_timer(f32 delay, void *fn) {
	tween_timer(delay, script_timer_done, fn);
}

void *script_update_fn = NULL;
void  script_on_update(void *_) {
    iron_delay_idle_sleep();
    minic_call_fn(script_update_fn, NULL, 0);
}
void script_notify_on_update(void *fn) {
	if (script_update_fn == NULL) {
		sys_notify_on_update(script_on_update, NULL);
	}
	script_update_fn = fn;
}

void *script_next_frame_fn = NULL;
void  script_on_next_frame(void *_) {
    void *fn             = script_next_frame_fn;
    script_next_frame_fn = NULL;
    if (fn != NULL) {
        minic_call_fn(fn, NULL, 0);
    }
}
void script_notify_on_next_frame(void *fn) {
	iron_delay_idle_sleep(); // The next frame comes even when nothing else changes
	sys_notify_on_next_frame(script_on_next_frame, NULL);
	script_next_frame_fn = fn;
}

bool script_is_running(void) {
	return script_update_fn != NULL || script_next_frame_fn != NULL;
}

void script_stop(void) {
	if (script_update_fn != NULL) {
		sys_remove_update(script_on_update);
		script_update_fn = NULL;
	}
	script_next_frame_fn = NULL;
}

void *_ui_files_done;
void  _ui_files_show_done(char *path) {
    minic_val_t args[1] = {minic_val_ptr(path)};
    minic_call_fn(_ui_files_done, args, 1);
}
void ui_files_show2(char *filters, bool is_save, bool open_multiple, void *files_done) {
	_ui_files_done = files_done;
	ui_files_show(filters, is_save, open_multiple, _ui_files_show_done);
}

static void *_ui_box_commands;
static void *_ui_box_on_hide;

static void _ui_box_show_custom_commands(void) {
	minic_call_fn(_ui_box_commands, NULL, 0);
}

static void _ui_box_show_custom_on_hide(void) {
	minic_call_fn(_ui_box_on_hide, NULL, 0);
}

// Draggable window drawn by a script function, on_hide may be NULL
void ui_box_show_custom2(void *commands, i32 mw, i32 mh, void *on_hide, char *title) {
	_ui_box_commands = commands;
	_ui_box_on_hide  = on_hide;
	ui_box_show_custom(_ui_box_show_custom_commands, mw, mh, _ui_box_show_custom_on_hide, true, title);
}

bool script_box_shown() {
	return ui_box_show;
}

// Seconds the last frame took
f32 script_frame_time() {
	return (f32)sys_real_delta();
}

// Writable folder of the app, scripts keep their files there on ios where /tmp is not accessible
char *script_save_path() {
	return (char *)iron_internal_save_path();
}

char *project_filepath_get() {
#ifdef IRON_WINDOWS
	return string_replace_all(g_project->_->filepath, "\\", "/");
#else
	return g_project->_->filepath;
#endif
}
char *project_basepath_get() {
	char *path = project_filepath_get();
	return substring(path, 0, string_last_index_of(path, "/"));
}
void project_filepath_set(char *s) {
#ifdef IRON_WINDOWS
	s = string_replace_all(s, "/", "\\");
#endif
	g_project->_->filepath = string_copy(s);
}
camera_object_t *script_get_camera() {
	return scene_camera;
}
context_t *script_get_context() {
	return g_context;
}
config_t *script_get_config() {
	return g_config;
}
platform_t script_get_platform() {
#if defined(IRON_WINDOWS)
	return PLATFORM_WINDOWS;
#elif defined(IRON_MACOS)
	return PLATFORM_MACOS;
#elif defined(IRON_IOS)
	return PLATFORM_IOS;
#elif defined(IRON_ANDROID)
	return PLATFORM_ANDROID;
#elif defined(IRON_WASM)
	return PLATFORM_WASM;
#else
	return PLATFORM_LINUX;
#endif
}
project_t *script_get_project() {
	return g_project;
}

world_data_t *script_get_world() {
	return scene_world;
}

object_t *script_get_object(char *s) {
	for (int i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (string_equals(g_project->_->paint_objects->buffer[i]->base->name, s)) {
			return g_project->_->paint_objects->buffer[i]->base;
		}
	}
	return NULL;
}

slot_material_t *script_get_material(char *s) {
	for (int i = 0; i < g_project->_->materials->length; ++i) {
		if (string_equals(g_project->_->materials->buffer[i]->canvas->name, s)) {
			return g_project->_->materials->buffer[i];
		}
	}
	return NULL;
}

gpu_texture_t *script_get_texture(char *s) {
	for (int i = 0; i < g_project->_->assets->length; ++i) {
		if (string_equals(g_project->_->assets->buffer[i]->name, s)) {
			return g_project->_->assets->buffer[i]->image;
		}
	}
	render_target_t *rt = any_map_get(render_path_render_targets, s); // Or a render target, for tests
	return rt != NULL ? rt->_image : NULL;
}

void script_texture_delete(char *s) {
	if (s == NULL || g_project == NULL || g_project->_ == NULL || g_project->_->assets == NULL) {
		return;
	}
	for (int i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *asset = g_project->_->assets->buffer[i];
		if (string_equals(asset->name, s)) {
			tab_textures_delete_texture(asset);
			return;
		}
	}
}

static bool script_paint_active = false;
static bool script_paint_first  = true;

static bool script_paint_allowed(void) {
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL) {
		return false;
	}
	if (!script_paint_active && layers_fill_paint_tool(g_context->tool)) {
		layers_fill_paint_to_mask(); // A fill layer is painted through its mask
	}
	if (slot_layer_is_group(g_context->layer)) {
		return false;
	}
	if (g_context->layer->fill_material != NULL && g_context->tool != TOOL_TYPE_PICKER &&
	    g_context->tool != TOOL_TYPE_COLORID) {
		return false;
	}
	return true;
}

static void script_paint_begin_stroke(void) {
	if (script_paint_active) {
		return;
	}
	base_init_undo_layers(); // Allocated on the first stroke, a new project has none yet
	make_material_parse_paint_material(false);
	render_path_paint_clear_blend();
	render_path_base_draw_gbuffer();

	history_push_undo = true;
	if (history_undo_layers != NULL) {
		history_paint();
	}

	script_paint_active         = true;
	script_paint_first          = true;
	g_context->brush_time       = sys_delta();
	g_context->prev_paint_vec_x = -1.0f;
	g_context->prev_paint_vec_y = -1.0f;
}

static void script_paint_at(f32 x, f32 y) {
	if (!script_paint_allowed()) {
		return;
	}

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}

	script_paint_begin_stroke();

	f32 prev_x = script_paint_first ? x : g_context->paint_vec.x;
	f32 prev_y = script_paint_first ? y : g_context->paint_vec.y;

	if (script_paint_first) {
		g_context->grab_start_x = x;
		g_context->grab_start_y = y;
	}
	g_context->decal_x          = x;
	g_context->decal_y          = y;
	g_context->paint_vec.x      = x;
	g_context->paint_vec.y      = y;
	g_context->last_paint_vec_x = prev_x;
	g_context->last_paint_vec_y = prev_y;
	g_context->prev_paint_vec_x = prev_x;
	g_context->prev_paint_vec_y = prev_y;
	g_context->pdirty           = 1;

	if (render_path_paint_stamps()) { // Spacing, scatter and jitter as with the pen
		if (script_paint_first) {
			g_context->brush_dab_x = -1.0f;
			g_context->brush_dab_y = -1.0f;
		}
		render_path_paint_commands_stamps();
	}
	else {
		render_path_paint_commands_symmetry(); // Mirror and radial copies as with the pen
		render_path_paint_commands_paint(false);
	}

	script_paint_first = false;

	if (in_use) {
		draw_begin(current, false, 0);
	}
}

void script_paint(f32 x, f32 y) {
	script_paint_at(x, y);
}

void script_paint_world(f32 x, f32 y, f32 z) {
	vec4_t clip = vec4_apply_mat4((vec4_t){x, y, z, 1.0f}, scene_camera->vp);
	if (clip.w <= 0.0f) {
		return;
	}
	f32 sx = (clip.x / clip.w + 1.0f) * 0.5f;
	f32 sy = (-clip.y / clip.w + 1.0f) * 0.5f;
	script_paint_at(sx, sy);
}

void script_paint_end(void) {
	if (!script_paint_active) {
		return;
	}

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}

	render_path_paint_dilate(true, true);
	layers_update_linked_layers();

	g_context->pdirty              = 0;
	g_context->rtdirty             = 1;
	g_context->ddirty              = 2;
	g_context->brush_time          = 0.0f;
	g_context->brush_blend_dirty   = true;
	g_context->layer_preview_dirty = true;
	g_context->prev_paint_vec_x    = -1.0f;
	g_context->prev_paint_vec_y    = -1.0f;
	g_context->brush_dab_x         = -1.0f;
	g_context->brush_dab_y         = -1.0f;

	script_paint_active = false;
	script_paint_first  = true;

	if (in_use) {
		draw_begin(current, false, 0);
	}
}

void script_fill_layer(void) {
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL) {
		return;
	}
	if (slot_layer_is_group(g_context->layer)) {
		return;
	}

	history_push_undo = true;
	if (history_undo_layers != NULL) {
		history_paint();
	}
	layers_update_fill_layer(true);
	g_context->layer_preview_dirty = true;
	g_context->rtdirty             = 1;
	g_context->ddirty              = 2;
}

static ui_node_canvas_t *script_material_canvas(void) {
	if (g_context == NULL || g_context->material == NULL) {
		return NULL;
	}
	return g_context->material->canvas;
}

static void script_gpu_begin(gpu_texture_t **out_current, bool *out_in_use) {
	*out_current = _draw_current;
	*out_in_use  = gpu_in_use;
	if (*out_in_use) {
		draw_end();
	}
}

static void script_gpu_end(gpu_texture_t *current, bool in_use) {
	if (in_use) {
		draw_begin(current, false, 0);
	}
}

buffer_t *script_get_texture_pixels(gpu_texture_t *texture) {
	if (texture == NULL || texture->format == GPU_TEXTURE_FORMAT_D32) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	buffer_t *b = gpu_get_texture_pixels(texture);
	script_gpu_end(current, in_use);
	return b;
}

bool tab_layers_can_delete(slot_layer_t *l);
void tab_layers_delete_layer(slot_layer_t *l);
void base_init_undo_layers();

static bool script_layer_valid(slot_layer_t *l) {
	return l != NULL && g_project != NULL && g_project->_ != NULL && array_index_of(g_project->_->layers, l) >= 0;
}

slot_layer_t *script_layer_get(char *name) {
	if (name == NULL || g_project == NULL || g_project->_ == NULL) {
		return NULL;
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (string_equals(g_project->_->layers->buffer[i]->name, name)) {
			return g_project->_->layers->buffer[i];
		}
	}
	return NULL;
}

void script_layer_set(slot_layer_t *l) {
	if (!script_layer_valid(l)) {
		return;
	}
	context_set_layer(l);
	g_context->ddirty = 2;
}

static slot_layer_t *script_layer_new_cleared(i32 base_color, f32 occlusion, f32 roughness, f32 metallic) {
	if (g_context == NULL || g_context->layer == NULL) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_t *l = layers_new_layer(false, -1, NULL);
	if (l != NULL) {
		history_new_layer();
		slot_layer_clear(l, base_color, NULL, occlusion, roughness, metallic);
	}
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
	return l;
}

slot_layer_t *script_layer_new(void) {
	return script_layer_new_cleared(0x00000000, 1.0, layers_default_rough, 0.0);
}

slot_layer_t *script_layer_new_color(i32 base_color, f32 occlusion, f32 roughness, f32 metallic) {
	return script_layer_new_cleared(base_color, occlusion, roughness, metallic);
}

slot_layer_t *script_layer_new_fill(void) {
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_t *l = layers_new_layer(false, -1, NULL);
	if (l != NULL) {
		history_new_layer();
		l->uv_type = UV_TYPE_UVMAP;
		history_to_fill_layer();
		slot_layer_to_fill_layer(l);
	}
	script_gpu_end(current, in_use);
	g_context->rtdirty = 1;
	g_context->ddirty  = 2;
	return l;
}

void script_layer_delete(slot_layer_t *l) {
	if (!script_layer_valid(l) || !tab_layers_can_delete(l)) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	tab_layers_delete_layer(l);
	script_gpu_end(current, in_use);
}

i32 script_layer_count(void) {
	if (g_project == NULL || g_project->_ == NULL) {
		return 0;
	}
	return g_project->_->layers->length;
}

slot_layer_t *script_layer_at(i32 i) {
	if (i < 0 || i >= script_layer_count()) {
		return NULL;
	}
	return g_project->_->layers->buffer[i];
}

i32 script_layer_index(slot_layer_t *l) {
	if (l == NULL || g_project == NULL || g_project->_ == NULL) {
		return -1;
	}
	return array_index_of(g_project->_->layers, l);
}

slot_layer_t *script_layer_new_mask(slot_layer_t *l, i32 type) {
	// type: 0 black, 1 white, 2 fill with the current material
	if (!script_layer_valid(l) || type < 0 || type > 2) {
		return NULL;
	}
	if (type == 2 && g_context->material == NULL) {
		return NULL;
	}
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		l = l->parent;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	context_set_layer(l);
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *m        = layers_new_mask(false, l, -1);
	if (m != NULL) {
		tab_layers_remap_all_layer_pointers(pointers);
		if (type == 0) {
			history_new_black_mask();
			slot_layer_clear(m, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		}
		else if (type == 1) {
			history_new_white_mask();
			slot_layer_clear(m, 0xffffffff, NULL, 1.0, layers_default_rough, 0.0);
		}
		else {
			history_new_fill_mask();
			slot_layer_clear(m, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
			slot_layer_to_fill_layer(m);
		}
		layers_update_fill_layers();
	}
	script_gpu_end(current, in_use);
	g_context->layers_preview_dirty = true;
	g_context->rtdirty              = 1;
	g_context->ddirty               = 2;
	return m;
}

slot_layer_t *script_layer_new_group(slot_layer_t *l) {
	if (!script_layer_valid(l)) {
		return NULL;
	}
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		l = l->parent;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *group    = layers_new_group();
	if (group != NULL) {
		context_set_layer(l);
		array_remove(g_project->_->layers, group);
		array_insert(g_project->_->layers, array_index_of(g_project->_->layers, l) + 1, group);
		group->parent = l->parent;
		l->parent     = group;
		tab_layers_remap_all_layer_pointers(pointers);
		context_set_layer(group);
		history_new_group();
	}
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
	return group;
}

static void script_layer_refill(slot_layer_t *l) {
	slot_material_t *material = g_context->material;
	g_context->material       = l->fill_material;
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	layers_update_fill_layers();
	context_set_material(material);
}

void script_layer_set_name(slot_layer_t *l, char *name) {
	if (!script_layer_valid(l) || name == NULL || name[0] == '\0') {
		return;
	}
	l->name = string_copy(slot_layer_unique_name(l, name));
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

void script_layer_update(slot_layer_t *l) {
	if (!script_layer_valid(l)) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	if (l->fill_material != NULL) {
		script_layer_refill(l);
	}
	if (l == g_project->_->layers->buffer[0] && l->mask_opacity < 1.0) {
		layers_enable_translucency();
	}
	make_material_parse_mesh_material();
	script_gpu_end(current, in_use);
	g_context->layers_preview_dirty = true;
	g_context->rtdirty              = 1;
	g_context->ddirty               = 2;
}

bool script_layer_move(slot_layer_t *l, i32 to) {
	if (!script_layer_valid(l) || to < 0 || to >= g_project->_->layers->length) {
		return false;
	}
	if (!slot_layer_can_move(l, to)) {
		return false;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_move(l, to);
	make_material_parse_mesh_material();
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
	return true;
}

bool script_layer_set_group(slot_layer_t *l, slot_layer_t *group) {
	if (!script_layer_valid(l) || slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		return false;
	}
	if (group == NULL) {
		if (l->parent == NULL) {
			return true;
		}
		return script_layer_move(l, g_project->_->layers->length - 1);
	}
	if (!script_layer_valid(group) || !slot_layer_is_group(group)) {
		return false;
	}
	if (l->parent == group) {
		return true;
	}
	// Place directly below the group and its masks, slot_layer_move then parents it to the group
	i32 anchor = array_index_of(g_project->_->layers, group);
	while (anchor > 0 && slot_layer_is_mask(g_project->_->layers->buffer[anchor - 1]) && g_project->_->layers->buffer[anchor - 1]->parent == group) {
		--anchor;
	}
	i32  from         = array_index_of(g_project->_->layers, l);
	i32  to           = from < anchor ? anchor - 1 : anchor;
	bool show_panel   = group->show_panel;
	group->show_panel = true; // A collapsed group would make the move skip past its children
	bool moved        = script_layer_move(l, to);
	group->show_panel = show_panel;
	return moved && l->parent == group;
}

void script_layer_set_object(slot_layer_t *l, object_t *object) {
	if (!script_layer_valid(l)) {
		return;
	}
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		l = l->parent;
	}
	i32 mask = 0;
	if (object != NULL) {
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			if (g_project->_->paint_objects->buffer[i]->base == object) {
				mask = i + 1;
				break;
			}
		}
		if (mask == 0) {
			return;
		}
	}
	if (l->object_mask == mask) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	context_set_layer(l);
	history_layer_object();
	l->object_mask = mask;
	make_material_parse_mesh_material();
	if (l->fill_material != NULL) {
		script_layer_refill(l);
	}
	else {
		layers_set_object_mask();
	}
	script_gpu_end(current, in_use);
	g_context->rtdirty = 1;
	g_context->ddirty  = 2;
}

// Layer context menu operations

static void script_layer_op_end(gpu_texture_t *current, bool in_use) {
	make_material_parse_mesh_material();
	script_gpu_end(current, in_use);
	g_context->layers_preview_dirty = true;
	g_context->rtdirty              = 1;
	g_context->ddirty               = 2;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

slot_layer_t *script_layer_duplicate(slot_layer_t *l) {
	if (!script_layer_valid(l)) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	context_set_layer(l);
	history_duplicate_layer();
	layers_duplicate_layer(l);
	script_layer_op_end(current, in_use);
	return g_context->layer;
}

slot_layer_t *script_layer_merge_down(slot_layer_t *l) {
	if (!script_layer_valid(l) || !tab_layers_can_merge_down(l)) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	context_set_layer(l);
	history_merge_layers();
	layers_merge_down();
	if (g_context->layer->fill_material != NULL) {
		slot_layer_to_paint_layer(g_context->layer);
	}
	script_layer_op_end(current, in_use);
	return g_context->layer;
}

slot_layer_t *script_layer_merge_group(slot_layer_t *group) {
	if (!script_layer_valid(group) || !slot_layer_is_group(group) || slot_layer_get_children(group) == NULL) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	if (slot_layer_shows(group) || tab_layers_can_delete(group)) {
		history_merge_group(group);
	}
	slot_layer_t *l = layers_merge_group(group);
	if (l != NULL) {
		context_set_layer(l);
	}
	script_layer_op_end(current, in_use);
	return l;
}

void script_layer_clear(slot_layer_t *l) {
	if (!script_layer_valid(l) || l->fill_material != NULL) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	context_set_layer(l);
	if (slot_layer_is_group(l)) {
		slot_layer_t_array_t *children = slot_layer_get_children(l);
		for (i32 i = 0; children != NULL && i < children->length; ++i) {
			slot_layer_t *c  = children->buffer[i];
			g_context->layer = c;
			history_clear_layer();
			slot_layer_clear(c, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		}
		g_context->layer = l;
	}
	else {
		history_clear_layer();
		slot_layer_clear(l, slot_layer_is_mask(l) ? 0xffffffff : 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		util_layer_clear_path_points(l);
	}
	if (l == g_project->_->layers->buffer[0] || l == g_project->_->layers->buffer[0]->parent) {
		layers_enable_translucency();
	}
	script_layer_op_end(current, in_use);
}

bool script_layer_invert_mask(slot_layer_t *m) {
	if (!script_layer_valid(m) || !slot_layer_is_mask(m) || m->fill_material != NULL) {
		return false;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	context_set_layer(m);
	history_invert_mask();
	slot_layer_invert_mask(m);
	script_layer_op_end(current, in_use);
	return true;
}

slot_layer_t *script_layer_apply_mask(slot_layer_t *m) {
	if (!script_layer_valid(m) || !slot_layer_is_mask(m)) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_t *parent = m->parent;
	g_context->layer     = m;
	history_apply_mask();
	slot_layer_apply_mask(m);
	context_set_layer(parent);
	script_layer_op_end(current, in_use);
	return parent;
}

bool script_layer_to_fill(slot_layer_t *l) {
	// Fills with the current material, a fill layer takes it in place of its own
	if (script_layer_valid(l) && slot_layer_is_layer(l) && l->fill_material != NULL && g_context->material != NULL &&
	    l->fill_material != g_context->material) {
		gpu_texture_t *current;
		bool           in_use;
		script_gpu_begin(&current, &in_use);
		base_init_undo_layers();
		slot_layer_set_fill_material(l, g_context->material);
		script_layer_op_end(current, in_use);
		return true;
	}
	if (!script_layer_valid(l) || slot_layer_is_group(l) || l->fill_material != NULL || l->path_material != NULL || g_context->material == NULL) {
		return false;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	context_set_layer(l);
	slot_layer_is_layer(l) ? history_to_fill_layer() : history_to_fill_mask();
	slot_layer_to_fill_layer(l);
	script_layer_op_end(current, in_use);
	return true;
}

bool script_layer_to_paint(slot_layer_t *l) {
	if (!script_layer_valid(l) || slot_layer_is_group(l) || (l->fill_material == NULL && l->path_material == NULL)) {
		return false;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	if (slot_layer_is_filter(l)) {
		slot_layer_relocate(l, array_index_of(g_project->_->layers, l->parent));
		l->parent = l->parent->parent;
	}
	context_set_layer(l);
	slot_layer_is_layer(l) ? history_to_paint_layer() : history_to_paint_mask();
	slot_layer_to_paint_layer(l);
	script_layer_op_end(current, in_use);
	return true;
}

// Path, curve and text layers paint with the current material along the points added to them

slot_layer_t *script_layer_new_path(bool curved) {
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_t *l = layers_new_path_layer(curved);
	if (l != NULL) {
		history_new_layer();
	}
	script_layer_op_end(current, in_use);
	return l;
}

slot_layer_t *script_layer_new_text(char *text) {
	// The layer name is the painted text
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL || text == NULL || text[0] == '\0') {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_t *l = layers_new_text_layer();
	if (l != NULL) {
		history_new_layer();
		script_layer_set_name(l, text);
	}
	script_layer_op_end(current, in_use);
	return l;
}

bool script_layer_add_path_point(slot_layer_t *l, f32 x, f32 y) {
	// x, y in 0..1 viewport space like script_paint(), a curve takes anchor, control, anchor, control, ...
	if (!script_layer_valid(l) || !slot_layer_is_path(l)) {
		return false;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	tool_type_t tool = g_context->tool;
	if (l->path_tool != -1) {
		g_context->tool = l->path_tool;
	}
	else if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_BLUR && tool != TOOL_TYPE_PARTICLE) {
		g_context->tool = TOOL_TYPE_BRUSH; // The first point picks the tool the path paints with
	}
	i32 count              = l->path_points->length;
	path_layer_last_active = count / 2 - 1; // Continue from the last point
	util_layer_add_path_point(l, x, y);
	g_context->tool = tool;
	script_layer_op_end(current, in_use);
	return l->path_points->length > count;
}

void script_layer_repaint_path(slot_layer_t *l) {
	// Repaints with the current path material, after its nodes or the path points changed
	if (!script_layer_valid(l) || !slot_layer_is_path(l)) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	util_layer_repaint_path(l);
	script_layer_op_end(current, in_use);
}

// Brush settings of the session: taken before a --script runs and put back when it quits, scripts change them freely
// and the config is saved on exit
typedef struct script_brush_state {
	bool             taken;
	i32              tool;
	i32              selected; // Brush library id
	i32              blending;
	f32              radius;
	f32              opacity;
	f32              strength;
	f32              mask_value;
	brush_settings_t s;
	gpu_texture_t   *tip_image;
} script_brush_state_t;

static script_brush_state_t script_brush_saved = {0};

void script_brush_take(void) {
	script_brush_state_t *s = &script_brush_saved;
	s->taken                = true;
	s->tool                 = g_context->tool;
	s->selected             = g_context->brush_selected;
	s->blending             = g_context->brush_blending;
	s->radius               = g_context->brush_radius;
	s->opacity              = g_context->brush_opacity;
	s->strength             = g_context->brush_strength;
	s->mask_value           = g_context->mask_value;
	s->tip_image            = brush_library_tip_image_get();
	brush_library_capture_settings(&s->s);
}

static void script_brush_apply(script_brush_state_t *s) {
	g_context->tool           = s->tool;
	g_context->brush_selected = s->selected;
	g_context->brush_blending = s->blending;
	g_context->brush_radius   = s->radius;
	g_context->brush_opacity  = s->opacity;
	g_context->brush_strength = s->strength;
	g_context->mask_value     = s->mask_value;
	brush_library_apply_settings(&s->s, s->tip_image);
}

static void script_brush_put_back(void) {
	if (!script_brush_saved.taken) {
		return;
	}
	script_brush_apply(&script_brush_saved);
	script_brush_saved.taken = false;
}

// Swaps the user's brush settings with the script's: the config saves the user's while a script runs, not the test brush.
// Twice puts things back as they were.
void script_brush_swap(void) {
	if (!script_brush_saved.taken) {
		return;
	}
	script_brush_state_t user = script_brush_saved;
	script_brush_take();
	script_brush_state_t script = script_brush_saved;
	script_brush_apply(&user);
	script_brush_saved = script;
}

// A plain round brush for tests, whatever brush the user left picked: no tip, full strength, no pressure, jitter or taper
void script_brush_defaults(void) {
	brush_settings_t s       = brush_settings_default();
	s.pressure_size          = 0.0;
	g_context->brush_blending = BLEND_TYPE_MIX;
	g_context->brush_opacity  = 1.0;
	g_context->mask_value     = 1.0;
	brush_library_apply_settings(&s, NULL);
	brush_output_node_parse_inputs();
	make_material_parse_paint_material(true);
}

// Settings of the current brush by name, see brush_settings_t: scripts tune any of them
static f32 *script_brush_setting(brush_settings_t *s, char *name) {
#define X(k)                                                                                                                                           \
	if (string_equals(name, #k)) {                                                                                                                     \
		return &s->k;                                                                                                                                  \
	}
	BRUSH_LIBRARY_SETTINGS(X)
#undef X
	return NULL;
}

void script_brush_set(char *name, f32 value) {
	brush_settings_t s;
	brush_library_capture_settings(&s);
	f32 *v = script_brush_setting(&s, name);
	if (v != NULL) {
		*v = value;
	}
	else if (string_equals(name, "tip")) {
		s.tip = (i32)value;
	}
	else if (string_equals(name, "rotation")) {
		s.rotation = (i32)value;
	}
	else if (string_equals(name, "tilt_shading")) {
		s.tilt_shading = value > 0.0;
	}
	brush_library_apply_settings(&s, brush_library_tip_image_get());
	brush_output_node_parse_inputs();
	make_material_parse_paint_material(true);
}

// Picks the brush of the library with this id
void script_brush_use(i32 id) {
	brush_library_use(brush_library_find(id));
}

void script_brush_delete(i32 id) {
	brush_library_delete(brush_library_find(id));
}

// An editable copy of the brush with this id in a group of your own, its id
i32 script_brush_duplicate(i32 id) {
	brush_def_t *c = brush_library_duplicate(brush_library_find(id), brush_library_user_group());
	return c != NULL ? c->id : -1;
}

f32 script_brush_get(char *name) {
	brush_settings_t s;
	brush_library_capture_settings(&s);
	f32 *v = script_brush_setting(&s, name);
	if (v != NULL) {
		return *v;
	}
	if (string_equals(name, "tip")) {
		return (f32)s.tip;
	}
	if (string_equals(name, "rotation")) {
		return (f32)s.rotation;
	}
	if (string_equals(name, "tilt_shading")) {
		return s.tilt_shading ? 1.0 : 0.0;
	}
	return 0.0;
}

// Renders the stroke preview of the current brush, painted as a Pencil stroke, and returns it
gpu_texture_t *script_brush_wave(void) {
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	util_render_make_brush_preview();
	script_gpu_end(current, in_use);
	return g_context->brush->image_wave;
}

void script_quit(void) {
	script_brush_put_back();
	iron_stop();
}

void script_project_new(void) {
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	project_new(true);
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

// New project on a template mesh by name, cube_bevel, sphere_2048, ...
void script_project_new_template(char *name) {
	project_new_template = name;
	script_project_new();
	project_new_template = NULL;
}

static char *script_resolve_path(char *path) {
	// iron_file_exists() checks relative paths against the cwd, data_get_blob() against data/
	if (data_is_abs(path) || data_is_up(path) || starts_with(path, "./")) {
		return path;
	}
	return string("./%s", path);
}

void script_project_open(char *path) {
	if (path == NULL || !iron_file_exists(path)) {
		return;
	}
	path = script_resolve_path(path);
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	import_arm_run_project(path);
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

// The asset becomes the project's ID map if it is one, returns 1 then
i32 script_set_id_map(i32 index) {
	if (index < 0 || index >= g_project->_->assets->length) {
		return 0;
	}
	return project_set_id_map(g_project->_->assets->buffer[index]) ? 1 : 0;
}

i32 script_asset_index(char *search) {
	for (i32 i = g_project->_->assets->length - 1; i >= 0; --i) {
		asset_t *a = g_project->_->assets->buffer[i];
		if (string_index_of(a->file, search) >= 0) {
			return i;
		}
	}
	return -1;
}

void script_import_asset(char *path, bool hdr_as_envmap) {
	if (path == NULL || !iron_file_exists(path)) {
		return;
	}
	path = script_resolve_path(path);
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	import_asset_run(path, -1, -1, false, hdr_as_envmap, NULL);
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

extern bool import_mesh_clear_layers;
extern bool import_mesh_no_reset;
extern bool import_mesh_append;

static void script_append_mesh_finish(i32 first) {
	if (g_project->_->paint_objects->length > first) {
		import_mesh_finish_import(NULL);
	}
	import_mesh_append = false;
}

void script_append_mesh(char *path) {
	if (path == NULL || !iron_file_exists(path)) {
		return;
	}
	path = script_resolve_path(path);
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	i32 first = g_project->_->paint_objects->length;
	import_mesh_run(path, false, false, true);
	script_append_mesh_finish(first);
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

buffer_t *script_load_blob(char *path) {
	return iron_load_blob(path);
}

void script_append_mesh_obj(char *data) {
	if (data == NULL || data[0] == '\0') {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	import_mesh_clear_layers = false;
	import_mesh_no_reset     = true;
	import_mesh_append       = true;
	g_context->layer_filter  = 0;
	buffer_t *b              = buffer_create_from_raw((u8 *)data, strlen(data));
	obj_parse_y_to_z_up      = false;
	i32 first                = g_project->_->paint_objects->length;
	import_obj_parse(b, false);
	obj_parse_y_to_z_up = true;
	free(b);
	script_append_mesh_finish(first);
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

bool script_packed_asset_save(char *suffix, char *path) {
	for (i32 i = 0; i < g_project->packed_assets->length; ++i) {
		packed_asset_t *pa = g_project->packed_assets->buffer[i];
		if (ends_with(pa->name, suffix)) {
			iron_file_save_bytes(path, (buffer_t *)pa->bytes, ((buffer_t *)pa->bytes)->length);
			return true;
		}
	}
	return false;
}

i32 script_packed_assets_remove(char *search) {
	if (g_project->packed_assets == NULL || search == NULL) {
		return 0;
	}
	i32 removed = 0;
	i32 i       = 0;
	while (i < g_project->packed_assets->length) {
		if (string_index_of(g_project->packed_assets->buffer[i]->name, search) >= 0) {
			array_splice(g_project->packed_assets, i, 1);
			removed++;
		}
		else {
			i++;
		}
	}
	return removed;
}

i32 script_assets_remove(char *search) {
	if (search == NULL) {
		return 0;
	}
	i32 removed = 0;
	i32 i       = 0;
	while (i < g_project->_->assets->length) {
		asset_t *a = g_project->_->assets->buffer[i];
		if (string_index_of(a->file, search) >= 0) {
			tab_textures_delete_texture(a);
			removed++;
		}
		else {
			i++;
		}
	}
	return removed;
}

i32 script_pack_assets(char *search) {
	if (search == NULL) {
		return 0;
	}
	asset_t_array_t *assets = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		if (string_index_of(a->file, search) >= 0 && (g_project->packed_assets == NULL || !project_packed_asset_exists(g_project->packed_assets, a->file))) {
			any_array_push(assets, a);
		}
	}
	i32 packed = assets->length;
	if (packed > 0) {
		gpu_texture_t *current;
		bool           in_use;
		script_gpu_begin(&current, &in_use);
		export_arm_pack_assets(g_project, assets);
		script_gpu_end(current, in_use);
	}
	array_free(assets);
	free(assets);
	return packed;
}

void script_set_pack_assets(i32 pack) {
	g_context->pack_assets_on_save = pack != 0;
}

void script_export_mesh(char *path) {
	if (path == NULL) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	export_mesh_run(path, NULL);
	script_gpu_end(current, in_use);
}

void script_export_material(char *path) {
	if (path == NULL || g_context == NULL || g_context->material == NULL) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	export_arm_run_material(path);
	script_gpu_end(current, in_use);
}

slot_material_t *script_material_create(char *name) {
	if (g_project == NULL || g_project->_ == NULL || g_project->_->materials == NULL || g_project->_->materials->length == 0) {
		return NULL;
	}
	shader_data_t   *data = g_project->_->materials->buffer[0]->data;
	slot_material_t *m    = slot_material_create(data, NULL);
	if (name != NULL && name[0] != '\0') {
		m->canvas->name = string_copy(slot_material_unique_name(m->canvas, name));
	}
	any_array_push(g_project->_->materials, m);

	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	context_set_material(m);
	util_render_make_material_preview();
	script_gpu_end(current, in_use);

	history_new_material();
	ui_dock_redraw(1);
	return m;
}

slot_material_t *script_material_get() {
	return g_context->material;
}

i32 script_material_count() {
	return g_project->_->materials->length;
}

void script_material_set(slot_material_t *m) {
	if (m == NULL) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	context_set_material(m);
	util_render_make_material_preview();
	script_gpu_end(current, in_use);
}

static shader_data_t   *script_object_material_data = NULL;

void script_material_delete(slot_material_t *m) {
	if (m == NULL || g_project == NULL || g_project->_ == NULL || g_project->_->materials == NULL) {
		return;
	}
	if (g_project->_->materials->length <= 1) {
		return;
	}
	if (array_index_of(g_project->_->materials, m) < 0) {
		return;
	}
	tab_materials_delete_material(m);
}

ui_node_t *script_material_create_node(char *type) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (type == NULL || canvas == NULL) {
		return NULL;
	}
	nodes_material_init();
	ui_node_t *n = nodes_material_get_node_t(type);
	if (n == NULL) {
		return NULL;
	}
	ui_node_t *node = ui_nodes_make_node(n, g_context->material->nodes, canvas);
	any_array_push(canvas->nodes, node);
	ui_nodes_hwnd->redraws = 2;
	return node;
}

ui_node_t *script_material_create_node_at(char *type, f32 x, f32 y) {
	ui_node_t *node = script_material_create_node(type);
	if (node != NULL) {
		node->x = x;
		node->y = y;
	}
	return node;
}

ui_node_t *script_material_get_node(char *type) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (canvas == NULL || type == NULL) {
		return NULL;
	}
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->type, type)) {
			return n;
		}
	}
	return NULL;
}

ui_node_t *script_material_get_node_id(i32 id) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (canvas == NULL) {
		return NULL;
	}
	return ui_get_node(canvas->nodes, id);
}

static void script_material_remove_links_to(ui_node_canvas_t *canvas, i32 to_id, i32 to_socket) {
	i32 i = 0;
	while (i < canvas->links->length) {
		ui_node_link_t *l = canvas->links->buffer[i];
		if (l->to_id == to_id && l->to_socket == to_socket) {
			for (i32 j = i; j < canvas->links->length - 1; ++j) {
				canvas->links->buffer[j] = canvas->links->buffer[j + 1];
			}
			canvas->links->length--;
		}
		else {
			i++;
		}
	}
}

void script_material_connect(ui_node_t *from, i32 from_socket, ui_node_t *to, i32 to_socket) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (canvas == NULL || from == NULL || to == NULL) {
		return;
	}
	if (from_socket < 0 || from_socket >= from->outputs->length) {
		return;
	}
	if (to_socket < 0 || to_socket >= to->inputs->length) {
		return;
	}

	script_material_remove_links_to(canvas, to->id, to_socket);

	ui_node_link_t *link = project_create_node_link(canvas->links, from->id, from_socket, to->id, to_socket);
	any_array_push(canvas->links, link);

	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

void script_material_disconnect(ui_node_t *to, i32 to_socket) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (canvas == NULL || to == NULL) {
		return;
	}
	script_material_remove_links_to(canvas, to->id, to_socket);
	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

void script_material_remove_node(ui_node_t *node) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (canvas == NULL || node == NULL) {
		return;
	}
	if (string_equals(node->type, "OUTPUT_MATERIAL_PBR")) {
		return;
	}
	ui_remove_node(node, canvas);
	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

// Node groups from scripts: a group canvas, nodes and links in any canvas (NULL is the current material's), group sockets

void nodes_material_sync_group_sockets(ui_node_canvas_t *canvas, char *group_name, ui_node_t *node);

static ui_node_t *script_group_io_node(i32 id, char *name, char *type, char *button) {
	return ALLOC_INIT(ui_node_t, {.id      = id,
	                              .name    = name,
	                              .type    = type,
	                              .x       = id == 0 ? 50 : 450,
	                              .y       = 200,
	                              .color   = 0xff448c6d,
	                              .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                              .outputs = any_array_create_from_raw((void *[]){}, 0),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_button_t,
	                                                 {.name = button, .type = "CUSTOM", .height = 1, .output = -1, .default_value = f32_array_create_x(0.0)}),
	                                  },
	                                  1)});
}

ui_node_canvas_t *script_material_group_create(char *name) {
	if (name == NULL || parser_material_get_group(name) != NULL) {
		return NULL;
	}
	ui_node_canvas_t *canvas = ALLOC_INIT(ui_node_canvas_t, {.name  = string_copy(name),
	                                                         .nodes = any_array_create_from_raw(
	                                                             (void *[]){
	                                                                 script_group_io_node(0, "Group Input", "GROUP_INPUT", "nodes_material_group_input_button"),
	                                                                 script_group_io_node(1, "Group Output", "GROUP_OUTPUT", "nodes_material_group_output_button"),
	                                                             },
	                                                             2),
	                                                         .links = any_array_create_from_raw((void *[]){}, 0)});
	node_group_t     *ng     = ALLOC_INIT(node_group_t, {.canvas = canvas, .nodes = ui_nodes_create()});
	any_array_push(g_project->_->material_groups, ng);
	return canvas;
}

void script_material_group_remove(ui_node_canvas_t *group) {
	for (i32 i = 0; group != NULL && i < g_project->_->material_groups->length; ++i) {
		if (g_project->_->material_groups->buffer[i]->canvas == group) {
			array_splice(g_project->_->material_groups, i, 1);
			return;
		}
	}
}

// Adds a socket to the group's inputs (Group Input outputs) or outputs (Group Output inputs), group nodes follow
ui_node_socket_t *script_material_group_add_socket(ui_node_canvas_t *group, bool is_output, char *type, char *name) {
	if (group == NULL || type == NULL) {
		return NULL;
	}
	ui_node_t *io = parser_material_node_by_type(group->nodes, is_output ? "GROUP_OUTPUT" : "GROUP_INPUT");
	if (io == NULL) {
		return NULL;
	}
	ui_node_socket_t *s = nodes_material_create_socket(NULL, io, name != NULL ? string_copy(name) : NULL, string_copy(type), group, 0.0, 1.0, NULL);
	any_array_push(is_output ? io->inputs : io->outputs, s);
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		nodes_material_sync_group_sockets(g_project->_->materials->buffer[i]->canvas, group->name, io);
	}
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		nodes_material_sync_group_sockets(g_project->_->material_groups->buffer[i]->canvas, group->name, io);
	}
	return s;
}

// Node of the type in the canvas, or with type "GROUP" a group node of the group group_name
ui_node_t *script_canvas_create_node(ui_node_canvas_t *canvas, char *type, char *group_name) {
	canvas = canvas != NULL ? canvas : script_material_canvas();
	if (canvas == NULL || type == NULL || g_context->material == NULL) {
		return NULL;
	}
	ui_node_t *node = NULL;
	if (string_equals(type, "GROUP")) {
		ui_node_canvas_t *g = group_name != NULL ? parser_material_get_group(group_name) : NULL;
		if (g == NULL) {
			return NULL;
		}
		node = ui_nodes_make_group_node(g, g_context->material->nodes, canvas);
	}
	else {
		nodes_material_init();
		ui_node_t *n = nodes_material_get_node_t(type);
		if (n == NULL) {
			return NULL;
		}
		node = ui_nodes_make_node(n, g_context->material->nodes, canvas);
	}
	any_array_push(canvas->nodes, node);
	return node;
}

void script_canvas_connect(ui_node_canvas_t *canvas, ui_node_t *from, i32 from_socket, ui_node_t *to, i32 to_socket) {
	canvas = canvas != NULL ? canvas : script_material_canvas();
	if (canvas == NULL || from == NULL || to == NULL || from_socket < 0 || from_socket >= from->outputs->length || to_socket < 0 ||
	    to_socket >= to->inputs->length) {
		return;
	}
	script_material_remove_links_to(canvas, to->id, to_socket);
	any_array_push(canvas->links, project_create_node_link(canvas->links, from->id, from_socket, to->id, to_socket));
}

// Renders the flat node preview of the node's first output into a new RGBA32 target of size x size. parents lists the
// group nodes from the current material's canvas into the node's canvas, NULL or empty for a node of the material.
gpu_texture_t *script_material_node_preview(ui_node_t *node, any_array_t *parents, i32 size) {
	if (node == NULL || g_context->material == NULL) {
		return NULL;
	}
	ui_node_canvas_t *group = NULL;
	if (parents != NULL && parents->length > 0) {
		ui_node_t *p = parents->buffer[parents->length - 1];
		group        = parser_material_get_group(p->name);
	}
	gpu_texture_t *image = gpu_create_render_target(size, size, GPU_TEXTURE_FORMAT_RGBA32);
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	util_render_make_node_preview(g_context->material->canvas, node, image, group, group != NULL ? (ui_node_t_array_t *)parents : NULL);
	script_gpu_end(current, in_use);
	return image;
}

static ui_node_socket_t *script_material_socket(ui_node_t *node, bool is_input, i32 socket) {
	if (node == NULL) {
		return NULL;
	}
	if (is_input) {
		if (socket < 0 || socket >= node->inputs->length) {
			return NULL;
		}
		return node->inputs->buffer[socket];
	}
	if (socket < 0 || socket >= node->outputs->length) {
		return NULL;
	}
	return node->outputs->buffer[socket];
}

void script_material_set_float(ui_node_t *node, i32 is_input, i32 socket, f32 value) {
	ui_node_socket_t *soc = script_material_socket(node, is_input != 0, socket);
	if (soc == NULL || soc->default_value == NULL || soc->default_value->length < 1) {
		return;
	}
	soc->default_value->buffer[0] = value;
}

void script_material_set_color(ui_node_t *node, i32 is_input, i32 socket, f32 r, f32 g, f32 b, f32 a) {
	ui_node_socket_t *soc = script_material_socket(node, is_input != 0, socket);
	if (soc == NULL || soc->default_value == NULL || soc->default_value->length < 3) {
		return;
	}
	soc->default_value->buffer[0] = r;
	soc->default_value->buffer[1] = g;
	soc->default_value->buffer[2] = b;
	if (soc->default_value->length >= 4) {
		soc->default_value->buffer[3] = a;
	}
}

void script_material_set_vector(ui_node_t *node, i32 is_input, i32 socket, f32 x, f32 y, f32 z) {
	ui_node_socket_t *soc = script_material_socket(node, is_input != 0, socket);
	if (soc == NULL || soc->default_value == NULL || soc->default_value->length < 3) {
		return;
	}
	soc->default_value->buffer[0] = x;
	soc->default_value->buffer[1] = y;
	soc->default_value->buffer[2] = z;
}

void script_material_set_button(ui_node_t *node, i32 button, f32 value) {
	if (node == NULL || button < 0 || button >= node->buttons->length) {
		return;
	}
	ui_node_button_t *but = node->buttons->buffer[button];
	if (but->default_value == NULL || but->default_value->length < 1) {
		return;
	}
	but->default_value->buffer[0] = value;
}

void script_material_set_text(ui_node_t *node, i32 button, char *text) {
	if (node == NULL || text == NULL || button < 0 || button >= node->buttons->length) {
		return;
	}
	ui_node_button_t *but = node->buttons->buffer[button];
	but->data             = u8_array_create_from_string(text);
	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

void script_material_update(void) {
	if (g_context == NULL || g_context->material == NULL) {
		return;
	}

	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);

	make_material_parse_paint_material(true);
	util_render_make_material_preview();
	if (context_is_decal()) {
		util_render_make_decal_preview();
	}
	script_gpu_end(current, in_use);

	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
	if (ui_header_handle != NULL) {
		ui_header_handle->redraws = 2;
	}
	ui_dock_redraw(1);
	g_context->ddirty  = 2;
	g_context->rtdirty = 1;
}

typedef struct script_click {
	i32 x;
	i32 y;
	i32 button;
} script_click_t;

static void script_mouse_click_up(void *data) {
	script_click_t *c = data;
	iron_internal_mouse_trigger_release(c->button, c->x, c->y);
	free(c);
}

static void script_mouse_click_down(void *data) {
	script_click_t *c = data;
	iron_internal_mouse_trigger_move(c->x, c->y);
	ui_mouse_move(g_ui, c->x, c->y, 0, 0);
	iron_internal_mouse_trigger_press(c->button, c->x, c->y);
	sys_notify_on_next_frame(script_mouse_click_up, c);
}

// Press at window pixel x, y next frame and release the frame after, button 0 left, 1 right, 2 middle
void script_mouse_click(i32 x, i32 y, i32 button) {
	if (button < 0 || button > 4) {
		return;
	}
	script_click_t *c = malloc(sizeof(script_click_t));
	c->x              = x;
	c->y              = y;
	c->button         = button;
	sys_notify_on_next_frame(script_mouse_click_down, c);
}

// Raw mouse events at window pixel x, y, for strokes that go through the live input path
void script_mouse_press(i32 x, i32 y, i32 button) {
	iron_internal_mouse_trigger_move(x, y);
	ui_mouse_move(g_ui, x, y, 0, 0);
	iron_internal_mouse_trigger_press(button, x, y);
}

void script_mouse_move(i32 x, i32 y) {
	iron_internal_mouse_trigger_move(x, y);
	ui_mouse_move(g_ui, x, y, 0, 0);
#if defined(IRON_IOS) || defined(IRON_ANDROID)
	ui_touch_move(g_ui, 0, x, y); // The ui takes moves from touches there
#endif
}

void script_mouse_release(i32 x, i32 y, i32 button) {
	iron_internal_mouse_trigger_release(button, x, y);
}

static void script_key_press_up(void *data) {
	iron_internal_keyboard_trigger_key_up((i32)(intptr_t)data);
}

static void script_key_press_down(void *data) {
	i32 code = (i32)(intptr_t)data;
	iron_internal_keyboard_trigger_key_down(code);
	char *name = keyboard_key_code(code);
	if (strlen(name) == 1) {
		iron_internal_keyboard_trigger_key_press(name[0]);
	}
	sys_notify_on_next_frame(script_key_press_up, data);
}

// Press key next frame and release the frame after
void script_key_press(char *key) {
	for (i32 code = 1; code < 256; ++code) {
		if (string_equals(keyboard_key_code(code), key)) {
			sys_notify_on_next_frame(script_key_press_down, (void *)(intptr_t)code);
			return;
		}
	}
}

// The paint objects as mesh_object_t, the scenes meshes
any_array_t *script_get_mesh_objects() {
	return (any_array_t *)g_project->_->paint_objects;
}

// Scripts have no indirect calls, a function stored in a variable is called through here
void script_call(void *fn) {
	if (fn != NULL) {
		minic_call_fn(fn, NULL, 0);
	}
}

extern string_array_t *_path_texture_formats;
extern string_array_t *_path_mesh_formats;
extern string_array_t *_path_text_formats;

static any_map_t      *custom_texture_importers = NULL;
static any_map_t      *custom_mesh_importers    = NULL;
static any_map_t      *custom_text_importers    = NULL;
static string_array_t *custom_text_formats      = NULL;

gpu_texture_t *script_import_custom_texture(char *path) {
	char       *format  = path_ext(path);
	void       *fn      = any_map_get(custom_texture_importers, format);
	minic_val_t args[1] = {minic_val_ptr(path)};
	minic_val_t r       = minic_call_fn(fn, args, 1);
	return r.p;
}

raw_mesh_t *script_import_custom_mesh(char *path) {
	char       *format  = path_ext(path);
	void       *fn      = any_map_get(custom_mesh_importers, format);
	minic_val_t args[1] = {minic_val_ptr(path)};
	minic_val_t r       = minic_call_fn(fn, args, 1);
	return r.p;
}

void script_import_custom_text(char *path) {
	char       *format  = path_ext(path);
	void       *fn      = any_map_get(custom_text_importers, format);
	minic_val_t args[1] = {minic_val_ptr(path)};
	minic_call_fn(fn, args, 1);
}

void script_register_text(char *format, void *fn) {
	any_map_set(import_text_importers, format, script_import_custom_text);

	if (custom_text_formats == NULL) {
		custom_text_formats = string_array_create(0);
	}
	if (string_array_index_of(path_text_formats(), format) < 0) {
		any_array_push((any_array_t *)_path_text_formats, format);
		string_array_push(custom_text_formats, format);
	}

	if (custom_text_importers == NULL) {
		custom_text_importers = any_map_create();
	}
	any_map_set(custom_text_importers, format, fn);
}

void script_unregister_text(char *format) {
	map_delete(import_text_importers, format);
	map_delete(custom_text_importers, format);

	i32 i = custom_text_formats != NULL ? string_array_index_of(custom_text_formats, format) : -1;
	if (i >= 0) {
		array_splice((any_array_t *)custom_text_formats, i, 1);
		array_splice((any_array_t *)_path_text_formats, string_array_index_of(_path_text_formats, format), 1);
	}
}

void script_register_texture(char *format, void *fn) {
	any_map_set(import_texture_importers, format, script_import_custom_texture);
	any_array_push((any_array_t *)_path_texture_formats, format);

	if (custom_texture_importers == NULL) {
		custom_texture_importers = any_map_create();
	}
	any_map_set(custom_texture_importers, format, fn);
}

void script_unregister_texture(char *format) {
	map_delete(import_texture_importers, format);
	array_splice((any_array_t *)_path_texture_formats, string_array_index_of(_path_texture_formats, format), 1);
}

void script_register_mesh(char *format, void *fn) {
	any_map_set(import_mesh_importers, format, script_import_custom_mesh);
	any_array_push((any_array_t *)_path_mesh_formats, format);

	if (custom_mesh_importers == NULL) {
		custom_mesh_importers = any_map_create();
	}
	any_map_set(custom_mesh_importers, format, fn);
}

void script_unregister_mesh(char *format) {
	map_delete(import_mesh_importers, format);
	array_splice((any_array_t *)_path_mesh_formats, string_array_index_of(_path_mesh_formats, format), 1);
}

raw_mesh_t *script_make_raw_mesh(char *name, i16_array_t *posa, i16_array_t *nora, u32_array_t *inda, float scale_pos) {
	raw_mesh_t *mesh = calloc(1, sizeof(raw_mesh_t));
	memset(mesh, 0, sizeof(raw_mesh_t));
	mesh->name         = name;
	mesh->posa         = posa;
	mesh->nora         = nora;
	mesh->inda         = inda;
	mesh->scale_pos    = scale_pos;
	mesh->scale_tex    = 1.0f;
	mesh->vertex_count = posa->length / 4;
	mesh->index_count  = inda->length;
	return mesh;
}

void script_material_category_add(char *category_name, any_array_t *node_list) {
	any_array_push(nodes_material_categories, category_name);
	nodes_material_init();
	any_array_push(nodes_material_list, node_list);
}

void script_brush_category_add(char *category_name, any_array_t *node_list) {
	any_array_push(nodes_brush_categories, category_name);
	nodes_brush_init();
	any_array_push(nodes_brush_list, node_list);
}

void script_material_category_remove(char *category_name) {
	int i = array_index_of(nodes_material_categories, category_name);
	array_splice(nodes_material_list, i, 1);
	array_splice(nodes_material_categories, i, 1);
}

void script_brush_category_remove(char *category_name) {
	int i = array_index_of(nodes_brush_categories, category_name);
	array_splice(nodes_brush_list, i, 1);
	array_splice(nodes_brush_categories, i, 1);
}

void script_material_custom_nodes_set(char *node_type, void *fn) {
	any_map_set(parser_material_custom_nodes, node_type, fn);
}

void script_brush_custom_nodes_set(char *node_type, void *fn) {
	any_map_set(parser_logic_custom_nodes, node_type, fn);
}

void script_material_custom_nodes_remove(char *node_type) {
	map_delete(parser_material_custom_nodes, node_type);
}

void script_brush_custom_nodes_remove(char *node_type) {
	map_delete(parser_logic_custom_nodes, node_type);
}

void *script_material_shader_get() {
	return parser_material_shader;
}

static char *_script_message    = NULL;
static i32   _script_message_id = 0;

static void script_message_draw(void *_) {
	if (_script_message == NULL) {
		return;
	}

	f32 scale = UI_SCALE();
	i32 size  = math_floor(22 * scale);

	draw_begin(NULL, false, 0);
	draw_set_font(g_font, size);

	f32 w = draw_string_width(draw_font, draw_font_size, _script_message);
	f32 h = draw_font_height(draw_font, draw_font_size);
	f32 x = (iron_window_width() - w) / 2.0;
	f32 y = iron_window_height() - 100 * scale;

	draw_set_color(0x99000000);
	draw_filled_rect(x - 12 * scale, y - 6 * scale, w + 24 * scale, h + 12 * scale);
	draw_set_color(0xffffffff);
	draw_string(_script_message, x, y);
	draw_end();
}

static void script_message_hide(void *data) {
	if ((i32)(intptr_t)data != _script_message_id) {
		return;
	}
	sys_remove_update(script_message_draw);
	_script_message = NULL;
}

void script_show_message(char *text, f32 seconds) {
	if (text == NULL) {
		return;
	}
	if (_script_message == NULL) {
		sys_notify_on_update(script_message_draw, NULL);
	}
	_script_message = string_copy(text);
	_script_message_id++;
	tween_timer(seconds, script_message_hide, (void *)(intptr_t)_script_message_id);
}

static f32   _script_fade_opacity = 0.0f;
static char *_script_fade_stage   = NULL;

typedef struct particle {
	float frame;
	float x;
	float y;
	float vx;
	float vy;
	float sc;
	float sca;
	int   flag;
} particle_t;

#define NUM_PARTICLES 128
static particle_t particles[NUM_PARTICLES];

void script_draw_particles(gpu_texture_t *texture, float x, float y, float w, float h, int atlas_x, int atlas_frames) {
	float screen_w = iron_window_width();
	float screen_h = iron_window_height();
	float cell_w   = texture->width / atlas_x;

	for (int i = 0; i < NUM_PARTICLES; ++i) {
		particle_t *p = &particles[i];
		if ((p->vx == 0 && p->vy == 0) || (p->x > screen_w || p->y > screen_h)) {
			p->x     = iron_random_get_in(-screen_w / 3.0, screen_w);
			p->y     = iron_random_get_in(-screen_w / 3.0, -cell_w);
			p->vx    = iron_random_get_in(0, 200) / 100.0;
			p->vy    = iron_random_get_in(0, 200) / 100.0;
			p->frame = iron_random_get_in(0, atlas_frames - 1);
			p->sc    = iron_random_get_in(0, 100) / 100.0;
			p->sca   = iron_random_get_in(0, 100) / 100.0;
		}

		p->frame += 0.5 * p->vy;
		if (p->frame >= atlas_frames)
			p->frame = 0;
		int frame_x = (int)p->frame % atlas_x;
		int frame_y = (int)p->frame / atlas_x;
		p->x += p->vx;
		p->y += p->vy;

		int col = ((int)(255 * p->sca) << 24) | (255 << 16) | (255 << 8) | 255;
		draw_set_color(col);
		// draw_sub_image(texture, frame_x * cell_w, frame_y * cell_w, cell_w, cell_w, p->x, p->y);
		draw_scaled_sub_image(texture, frame_x * cell_w, frame_y * cell_w, cell_w, cell_w, p->x, p->y, cell_w * 2, cell_w * 2);
	}
}

typedef struct script_screenshot_request {
	char *path;
	i32   frames;
	void (*done)(void);
} script_screenshot_request_t;

static any_array_t   *_script_screenshot_queue  = NULL;
static gpu_texture_t *_script_screenshot_target = NULL;
static i32            _script_screenshot_frames = 0;

static void script_screenshot_next(void);

static void script_screenshot_end_frame(void *_) {
	script_screenshot_request_t *r = _script_screenshot_queue->buffer[0];
	gpu_framebuffer_redirect       = NULL;

	draw_begin(NULL, false, 0);
	draw_image(_script_screenshot_target, 0, 0);
	draw_end();

	buffer_t *pixels = gpu_get_texture_pixels(_script_screenshot_target);
	for (i32 i = 3; i < pixels->length; i += 4) {
		pixels->buffer[i] = 255;
	}
	iron_write_png(r->path, pixels, _script_screenshot_target->width, _script_screenshot_target->height, 0);
	gpu_delete_texture(_script_screenshot_target);
	_script_screenshot_target = NULL;

	array_shift(_script_screenshot_queue);
	if (r->done != NULL) {
		r->done();
	}
	script_screenshot_next();
}

static void script_screenshot_capture(void *_) {
	iron_delay_idle_sleep();
	script_screenshot_request_t *r = _script_screenshot_queue->buffer[0];
	if (++_script_screenshot_frames < r->frames) {
		g_context->ddirty = 2;
		base_redraw_ui();
		sys_notify_on_next_frame(script_screenshot_capture, NULL);
		return;
	}
	_script_screenshot_target = gpu_create_render_target(iron_window_width(), iron_window_height(), GPU_TEXTURE_FORMAT_RGBA32);
	gpu_framebuffer_redirect  = _script_screenshot_target;
	sys_notify_on_end_frame(script_screenshot_end_frame, NULL);
}

static void script_screenshot_next(void) {
	if (_script_screenshot_queue->length == 0) {
		return;
	}
	_script_screenshot_frames = 0;
	sys_notify_on_next_frame(script_screenshot_capture, NULL);
}

void script_screenshot_queue(char *path, i32 frames, void (*done)(void)) {
	if (_script_screenshot_queue == NULL) {
		_script_screenshot_queue = any_array_create(0);
	}
	script_screenshot_request_t *r = ALLOC_INIT(script_screenshot_request_t, {.path = string_copy(path), .frames = frames, .done = done});
	any_array_push(_script_screenshot_queue, r);
	if (_script_screenshot_queue->length == 1) {
		script_screenshot_next();
	}
}

void script_screenshot(char *path) {
	if (path != NULL) {
		script_screenshot_queue(path, 1, NULL);
	}
}
