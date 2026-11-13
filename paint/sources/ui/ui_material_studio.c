#include "../global.h"

// Material Studio: a full screen popover editing the node graph of a studio material (studio_graph.c) with a few controls and
// a large preview. Every control writes the inputs of real nodes, the graph stays editable in the node editor.
// The preview shader reads the node values from a texture (parser_material_live_*), so dragging a slider only renders the
// preview again. Changes to the graph itself compile it. Letting go commits the edit: undo, the paint shader, the fill
// layers using the material and its sphere icon.

#define STUDIO_PREVIEW_SIZE 1024 // Own targets, the material preview ones are 512
#define STUDIO_ANIM_TIME    0.2
#define STUDIO_SLOTS        32 // Slider values made of several node inputs

bool material_studio_show = false;

static slot_material_t  *studio_material      = NULL;
static f64               studio_anim_start    = 0.0;
static bool              studio_closing       = false;
static ui_window_t      *studio_back_handle   = NULL;
static ui_window_t      *studio_head_handle   = NULL;
static ui_window_t      *studio_view_handle   = NULL;
static ui_window_t      *studio_ctrl_handle   = NULL;
static gpu_texture_t    *studio_preview       = NULL;
static mesh_object_t    *studio_cube          = NULL;
static shader_context_t *studio_scon          = NULL;
static bool              studio_need_compile  = false; // The preview shader is out of date
static bool              studio_need_render   = false; // The preview image is out of date
static bool              studio_need_commit   = false; // An edit to commit once the touch ends
static bool              studio_hooked        = false;
static ui_node_canvas_t *studio_undo_canvas   = NULL; // Canvas before the edit, for undo
static f32               studio_yaw           = 0.65;
static f32               studio_pitch         = 0.4;
static f32               studio_dist          = 1.75; // Camera distance, pinch or wheel to zoom
static bool              studio_sphere        = false; // Preview on the sphere instead of the cube
static i32               studio_sel           = 0;    // Selected layer
static i32               studio_stop          = 0;    // Selected stop of its color ramp
static bool              studio_ramp_drag     = false; // A stop follows the finger
static f32              *studio_color_target  = NULL; // Color the color menu edits, three floats
static ui_color_state_t  studio_color_state;
static i32               studio_compile_count = 0;
static f32               studio_compile_ms    = 0.0; // Last compile and render, for scripts
static f32               studio_render_ms     = 0.0;
static u32               studio_known_struct  = 0; // Hash of the graph as compiled, undo or the node editor change it
static f64               studio_known_values  = 0.0;
static f32               studio_slots[STUDIO_SLOTS];
static f32               studio_swipe[STUDIO_LAYERS]; // Layer row offsets, the delete button shows under them

#define STUDIO_THUMBS     16
#define STUDIO_THUMB_SIZE 128
static gpu_texture_t *studio_thumbs[STUDIO_THUMBS];
static gpu_texture_t *studio_thumb_none  = NULL;
static bool           studio_thumbs_made = false;

static void studio_redraw() {
	if (studio_ctrl_handle != NULL) {
		studio_ctrl_handle->redraws = 2;
		studio_head_handle->redraws = 2;
	}
}

static ui_node_canvas_t *studio_canvas() {
	return studio_material != NULL && studio_graph_is(studio_material->canvas) ? studio_material->canvas : NULL;
}

// The graph: nodes, their types, roles and buttons, links and the number of ramp stops. Changing it compiles the preview.
static u32 studio_hash_struct(ui_node_canvas_t *c) {
	u32 h = 2166136261u;
#define STUDIO_HASH(v) h = (h ^ (u32)(v)) * 16777619u
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		STUDIO_HASH(n->id);
		STUDIO_HASH(n->flags);
		for (char *t = n->type; *t; ++t) {
			STUDIO_HASH(*t);
		}
		for (i32 j = 0; j < n->buttons->length; ++j) {
			ui_node_button_t *b = n->buttons->buffer[j];
			if (b->default_value == NULL) {
				continue;
			}
			bool custom = string_equals(b->type, "CUSTOM");
			STUDIO_HASH(custom ? b->default_value->length : (i32)(b->default_value->buffer[0] * 1000));
			if (custom && b->data != NULL && b->data->length > 0) {
				STUDIO_HASH(b->data->buffer[0]);
			}
		}
	}
	for (i32 i = 0; i < c->links->length; ++i) {
		ui_node_link_t *l = c->links->buffer[i];
		STUDIO_HASH(l->from_id);
		STUDIO_HASH(l->from_socket);
		STUDIO_HASH(l->to_id);
		STUDIO_HASH(l->to_socket);
	}
#undef STUDIO_HASH
	return h;
}

// The values: inputs and ramp stops. Changing them renders the preview again.
static f64 studio_hash_values(ui_node_canvas_t *c) {
	f64 sum = 0.0;
	f64 w   = 1.0;
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		for (i32 j = 0; j < n->inputs->length; ++j) {
			f32_array_t *d = n->inputs->buffer[j]->default_value;
			for (i32 k = 0; d != NULL && k < (i32)d->length; ++k) {
				sum += d->buffer[k] * w;
				w += 0.731;
			}
		}
		for (i32 j = 0; j < n->buttons->length; ++j) {
			ui_node_button_t *b = n->buttons->buffer[j];
			if (b->default_value != NULL && string_equals(b->type, "CUSTOM")) {
				for (i32 k = 0; k < (i32)b->default_value->length; ++k) {
					sum += b->default_value->buffer[k] * w;
					w += 0.731;
				}
			}
		}
	}
	return sum;
}

// An edit is about to change the graph: keep it for undo
static void studio_begin_edit() {
	if (studio_undo_canvas == NULL && studio_material != NULL) {
		studio_undo_canvas = util_clone_canvas(studio_material->canvas);
	}
}

// Something changed: the next update compiles or renders what it takes, letting go commits
static void studio_edited() {
	studio_need_commit = true;
	studio_redraw();
}

// ███╗   ██╗███████╗██╗    ██╗
// ████╗  ██║██╔════╝██║    ██║
// ██╔██╗ ██║█████╗  ██║ █╗ ██║
// ██║╚██╗██║██╔══╝  ██║███╗██║
// ██║ ╚████║███████╗╚███╔███╔╝
// ╚═╝  ╚═══╝╚══════╝ ╚══╝╚══╝

// A studio material added to the project and picked, the studio opens on it
slot_material_t *material_studio_new() {
	nodes_material_init();
	slot_material_t *m = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	studio_graph_create(m->canvas);
	studio_graph_apply_preset(m->canvas, 0);

	any_array_push(g_project->_->materials, m);
	context_set_material(m);
	tab_materials_update_material();
	history_new_material();
	material_studio_open(m);
	return m;
}

// ██████╗ ██████╗ ███████╗██╗   ██╗██╗███████╗██╗    ██╗
// ██╔══██╗██╔══██╗██╔════╝██║   ██║██║██╔════╝██║    ██║
// ██████╔╝██████╔╝█████╗  ██║   ██║██║█████╗  ██║ █╗ ██║
// ██╔═══╝ ██╔══██╗██╔══╝  ╚██╗ ██╔╝██║██╔══╝  ██║███╗██║
// ██║     ██║  ██║███████╗ ╚████╔╝ ██║███████╗╚███╔███╔╝
// ╚═╝     ╚═╝  ╚═╝╚══════╝  ╚═══╝  ╚═╝╚══════╝ ╚══╝╚══╝

// The rounded cube of new projects, its edges show wear, scaled to the size of the preview sphere
static mesh_object_t *studio_get_cube() {
	if (studio_cube != NULL) {
		return studio_cube;
	}
	buffer_t *b = data_get_blob("meshes/cube_bevel.arm");
	if (b == NULL) {
		return NULL;
	}
	scene_t     *raw           = armpack_decode(b);
	mesh_data_t *md            = mesh_data_create(raw->mesh_datas->buffer[0]);
	md->name                   = ".StudioCube";
	studio_cube                = scene_add_mesh_object(md, g_project->_->materials->buffer[0]->data, NULL);
	studio_cube->base->name    = ".StudioCube";
	studio_cube->base->visible = false;

	vec4_t lo, hi;
	mesh_data_calculate_aabb_min_max(md, &lo, &hi);
	f32 extent                          = fmaxf(fmaxf(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);
	f32 s                               = extent > 0.0 ? 0.62 / extent : 1.0;
	studio_cube->base->transform->scale = (vec4_t){s, s, s, 1.0};
	transform_build_matrix(studio_cube->base->transform);
	return studio_cube;
}

// Render targets of the studio preview, made the first time it opens
static void studio_create_targets() {
	if (any_map_get(render_path_render_targets, "smain") != NULL) {
		return;
	}
	char *names[]   = {"smain", "stex", "sgbuffer0", "sgbuffer1"};
	char *formats[] = {"D32", "RGBA64", "RGBA64", "RGBA64"};
	for (i32 i = 0; i < 4; ++i) {
		render_target_t *t = render_target_create();
		t->name            = names[i];
		t->width           = STUDIO_PREVIEW_SIZE;
		t->height          = STUDIO_PREVIEW_SIZE;
		t->format          = formats[i];
		render_path_create_render_target(t);
	}
}

void render_path_preview_commands_studio() {
	render_path_set_target("sgbuffer0", NULL, "smain", GPU_CLEAR_COLOR | GPU_CLEAR_DEPTH, 0xffffffff, 1.0);
	string_array_t *additional = any_array_create_from_raw_tmp(
	    (void *[]){
	        "sgbuffer1",
	    },
	    1);
	render_path_set_target("sgbuffer0", additional, "smain", GPU_CLEAR_NONE, 0, 0.0);
	render_path_base_draw_meshes(render_path_base_mesh_scon(0), true);

	render_path_set_target("stex", NULL, "smain", GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("sgbuffer0", "gbuffer0");
	render_path_bind_target("sgbuffer1", "gbuffer1");
	render_path_bind_target("empty_white", "ssaotex");
	render_path_draw_shader("Scene/deferred_light/deferred_light");

	render_path_set_target("stex", NULL, "smain", GPU_CLEAR_NONE, 0, 0.0);
	render_path_draw_skydome("Scene/world_pass/world_pass");

	render_target_t *texpreview = any_map_get(render_path_render_targets, "texpreview");
	texpreview->_image          = studio_preview;
	render_path_set_target("texpreview", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("stex", "tex");
	render_path_draw_shader("Scene/compositor_pass/compositor_pass");
}

// Preview shader of the material with live values, kept so drags render without compiling
static void studio_compile() {
	f64         t0 = iron_time();
	material_t *sd = ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
	parser_material_live_begin(studio_material->canvas, 0);
	node_shader_context_t *con = make_mesh_preview_run(sd, false);
	parser_material_live_end();
	shader_context_load(con->data);
	if (studio_scon != NULL) {
		make_material_delete_context(studio_scon);
	}
	studio_scon = con->data;
	studio_compile_count++;
	node_shader_context_free(con);
	free(sd);
	studio_compile_ms = (iron_time() - t0) * 1000.0;
}

// Camera on a sphere around the cube, looking at its center
static void studio_set_camera() {
	f32    d   = studio_dist;
	f32    cp  = math_cos(studio_pitch);
	vec4_t pos = {math_sin(studio_yaw) * cp * d, -math_cos(studio_yaw) * cp * d, math_sin(studio_pitch) * d, 1.0};
	vec4_t f   = vec4_norm((vec4_t){-pos.x, -pos.y, -pos.z, 0.0});
	vec4_t r   = vec4_norm(vec4_cross(f, (vec4_t){0.0, 0.0, 1.0, 0.0}));
	vec4_t u   = vec4_cross(r, f);
	mat4_t m   = {r.x, r.y, r.z, 0.0, u.x, u.y, u.z, 0.0, -f.x, -f.y, -f.z, 0.0, pos.x, pos.y, pos.z, 1.0};
	transform_set_matrix(scene_camera->base->transform, m);
}

#define STUDIO_SWAP_MAX 8

// The sphere of the material icons, scaled like the cube while it renders
static mesh_object_t *studio_get_sphere() {
	object_t *o = scene_get_child(".Sphere");
	return o != NULL ? o->ext : NULL;
}

static mesh_object_t *studio_mesh() {
	return studio_sphere ? studio_get_sphere() : studio_get_cube();
}

// The preview shader samples the model's curvature textures, the preview mesh's take their place for the render
static i32 studio_swap_curvature(char **keys, gpu_texture_t **saved) {
	mesh_object_t *cube = studio_mesh();
	if (cube == NULL) {
		return 0;
	}
	if (g_context->node_previews == NULL) {
		g_context->node_previews = any_map_create();
	}
	i32               count = 0;
	ui_node_canvas_t *c     = studio_material->canvas;
	for (i32 i = 0; i < c->nodes->length && count < STUDIO_SWAP_MAX; ++i) {
		ui_node_t *node = c->nodes->buffer[i];
		if (!string_equals(node->type, "CURVATURE_MASK")) {
			continue;
		}
		bool                   cavities = node->buttons->buffer[0]->default_value->buffer[0] > 0.5;
		char                  *cube_id  = studio_sphere ? (cavities ? "studio_sphere_cavities" : "studio_sphere_edges")
		                                                : (cavities ? "studio_cube_cavities" : "studio_cube_edges");
		mesh_object_t_array_t *objs     = any_array_create_from_raw((void *[]){cube}, 1);
		curvature_mask_bake_meshes(node, objs, 1024, cube_id); // Cached after the first time
		array_free(objs);
		free(objs);
		keys[count]  = string_copy(curvature_mask_bake_id(node, studio_material));
		saved[count] = any_map_get(g_context->node_previews, keys[count]);
		any_map_set(g_context->node_previews, keys[count], any_map_get(g_context->node_previews, cube_id));
		count++;
	}
	return count;
}

static void studio_unswap_curvature(char **keys, gpu_texture_t **saved, i32 count) {
	for (i32 i = 0; i < count; ++i) {
		if (saved[i] != NULL) {
			any_map_set(g_context->node_previews, keys[i], saved[i]);
		}
		else {
			map_delete(g_context->node_previews, keys[i]);
		}
	}
}

static void studio_render() {
	mesh_object_t *cube = studio_mesh();
	if (cube == NULL || studio_scon == NULL || studio_canvas() == NULL) {
		return;
	}
	// The sphere is shown at the size of the cube
	vec4_t _scale = cube->base->transform->scale;
	if (studio_sphere) {
		vec4_t lo, hi;
		mesh_data_calculate_aabb_min_max(cube->data, &lo, &hi);
		f32 extent                   = fmaxf(fmaxf(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);
		f32 k                        = extent > 0.0 ? 0.92 / extent : 1.0;
		cube->base->transform->scale = (vec4_t){k, k, k, 1.0};
		transform_build_matrix(cube->base->transform);
	}
	char          *swap_keys[STUDIO_SWAP_MAX];
	gpu_texture_t *swap_saved[STUDIO_SWAP_MAX];
	i32            swaps = studio_swap_curvature(swap_keys, swap_saved);
	parser_material_live_update(studio_material->canvas, 0);
	if (studio_preview == NULL) {
		studio_preview = gpu_create_render_target(STUDIO_PREVIEW_SIZE, STUDIO_PREVIEW_SIZE, GPU_TEXTURE_FORMAT_RGBA64);
	}
	studio_create_targets();

	slot_material_t *_material    = g_context->material;
	g_context->material           = studio_material;
	g_context->material_preview   = true;
	cube->base->visible           = true;
	mesh_object_t_array_t *meshes = scene_meshes;
	scene_meshes                  = any_array_create_from_raw((void *[]){cube}, 1);
	mesh_object_t *painto         = g_context->paint_object;
	g_context->paint_object       = cube;
	cube->material                = g_project->_->materials->buffer[0]->data;

	g_context->saved_camera = scene_camera->base->transform->local;
	f32 saved_fov           = scene_camera->data->fov;
	scene_camera->data->fov = 0.92;
	viewport_update_camera_type(CAMERA_TYPE_PERSPECTIVE);
	studio_set_camera();

	gpu_texture_t *_envmap = scene_world->_->envmap;
	scene_world->_->envmap = g_context->preview_envmap;
	util_render_preview_lighting_begin(0.5, 0.65);
	i32 _last_w         = _render_path_last_w;
	i32 _last_h         = _render_path_last_h;
	_render_path_last_w = STUDIO_PREVIEW_SIZE;
	_render_path_last_h = STUDIO_PREVIEW_SIZE;
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);

	// The studio shader in place of the mesh shader for this render only
	if (make_material_mesh_scons == NULL) {
		make_material_mesh_scons = any_array_create(0);
	}
	if (make_material_mesh_scons->length == 0) {
		any_array_push(make_material_mesh_scons, NULL);
	}
	shader_context_t *_scon             = make_material_mesh_scons->buffer[0];
	make_material_mesh_scons->buffer[0] = studio_scon;
	void (*_commands)(void)             = render_path_commands;
	render_path_commands                = render_path_preview_commands_studio;
	render_path_render_frame();
	render_path_commands                = _commands;
	make_material_mesh_scons->buffer[0] = _scon;
	studio_unswap_curvature(swap_keys, swap_saved, swaps);

	g_context->material_preview = false;
	_render_path_last_w         = _last_w;
	_render_path_last_h         = _last_h;
	cube->base->visible         = false;
	if (studio_sphere) {
		cube->base->transform->scale = _scale;
		transform_build_matrix(cube->base->transform);
	}
	array_free(scene_meshes);
	free(scene_meshes);
	scene_meshes            = meshes;
	g_context->paint_object = painto;
	g_context->material     = _material;

	transform_set_matrix(scene_camera->base->transform, g_context->saved_camera);
	viewport_update_camera_type(g_context->camera_type);
	scene_camera->data->fov = saved_fov;
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);
	scene_world->_->envmap = _envmap;
	util_render_preview_lighting_end();
	g_context->ddirty = 0;
}

// Draws the node preview shader over the whole target
static void studio_draw_screen(shader_context_t *scon, gpu_texture_t *target) {
	if (util_render_screen_aligned_full_vb == NULL) {
		util_render_create_screen_aligned_full_data();
	}
	static string_array_t *empty = NULL;
	if (empty == NULL) {
		empty = any_array_create_from_raw((void *[]){""}, 1);
	}
	_gpu_begin(target, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(scon->_->pipe);
	uniforms_set_context_consts(scon, empty);
	uniforms_set_obj_consts(scon, g_context->paint_object->base);
	gpu_set_vertex_buffer(util_render_screen_aligned_full_vb);
	gpu_set_index_buffer(util_render_screen_aligned_full_ib);
	gpu_draw();
	gpu_end();
}

// Node preview shader of a node of the canvas, with live values from the table when it is not -1
static shader_context_t *studio_compile_node(ui_node_t *node, ui_node_canvas_t *canvas, i32 table) {
	material_t       *sd      = ALLOC_INIT(material_t, {.name = "Material", .canvas = canvas});
	ui_node_canvas_t *_canvas = g_context->material->canvas; // The preview parses the canvas of the current material
	g_context->material->canvas = canvas;
	if (table >= 0) {
		parser_material_live_begin(canvas, table);
	}
	node_shader_context_t *con = make_node_preview_run(sd, node, NULL, NULL);
	parser_material_live_end();
	g_context->material->canvas = _canvas;
	shader_context_load(con->data);
	shader_context_t *scon = con->data;
	node_shader_context_free(con);
	free(sd);
	return scon;
}

// A tile of each pattern node at its defaults, rendered like the node previews of the node editor
static void studio_make_thumbs() {
	studio_thumbs_made = true;
	studio_thumb_none  = gpu_create_render_target(STUDIO_THUMB_SIZE, STUDIO_THUMB_SIZE, GPU_TEXTURE_FORMAT_RGBA32); // A flat tile
	_gpu_begin(studio_thumb_none, NULL, NULL, GPU_CLEAR_COLOR, 0xff404040, 0.0);
	gpu_end();
	for (i32 i = 0; i < pattern_node_count() && i < STUDIO_THUMBS; ++i) {
		ui_node_canvas_t *c = ALLOC_INIT(ui_node_canvas_t, {.name  = "Thumb",
		                                                     .nodes = any_array_create_from_raw((void *[]){}, 0),
		                                                     .links = any_array_create_from_raw((void *[]){}, 0)});
		ui_node_t        *n = ui_nodes_make_node(nodes_material_get_node_t(pattern_node_type(i)), NULL, c);
		any_array_push(c->nodes, n);
		shader_context_t *scon = studio_compile_node(n, c, -1);
		if (scon == NULL) {
			continue;
		}
		studio_thumbs[i] = gpu_create_render_target(STUDIO_THUMB_SIZE, STUDIO_THUMB_SIZE, GPU_TEXTURE_FORMAT_RGBA32);
		studio_draw_screen(scon, studio_thumbs[i]);
		make_material_delete_context(scon);
	}
}

// Layer tiles: the layer's colors, its color ramp node preview. The shader of a layer compiles when its nodes change, values
// come from the live table 1 + layer, so edits only draw the tiles again.

#define STUDIO_TILE_SIZE 128
static shader_context_t *studio_layer_scon[STUDIO_LAYERS];
static u32               studio_layer_key[STUDIO_LAYERS];
static gpu_texture_t    *studio_layer_tile[STUDIO_LAYERS];
static f64               studio_layer_values = -1.0; // Values the tiles show

static u32 studio_layer_hash(studio_graph_t *g, i32 k) {
	u32 h = 2166136261u ^ (u32)(k * 977);
#define STUDIO_HASH(v) h = (h ^ (u32)(v)) * 16777619u
	for (i32 r = 1; r < STUDIO_ROLE_COUNT; ++r) {
		ui_node_t *n = g->layers[k].n[r];
		if (n == NULL || r == STUDIO_ROLE_MIX || r == STUDIO_ROLE_MASK) {
			continue;
		}
		STUDIO_HASH(r);
		STUDIO_HASH(n->id);
		for (char *t = n->type; *t; ++t) {
			STUDIO_HASH(*t);
		}
		for (i32 j = 0; j < n->buttons->length; ++j) {
			ui_node_button_t *b = n->buttons->buffer[j];
			if (b->default_value != NULL) {
				STUDIO_HASH(string_equals(b->type, "CUSTOM") ? b->default_value->length : (i32)(b->default_value->buffer[0] * 1000));
			}
		}
	}
#undef STUDIO_HASH
	return h;
}

// At most one layer shader compiles a frame, the tiles draw again when a value changed
static void studio_update_tiles(ui_node_canvas_t *c, f64 values) {
	studio_graph_t g;
	if (!studio_graph_read(c, &g)) {
		return;
	}
	bool draw = values != studio_layer_values;
	for (i32 k = 0; k < g.count; ++k) {
		u32 key = studio_layer_hash(&g, k);
		if (key == studio_layer_key[k] && studio_layer_scon[k] != NULL) {
			continue;
		}
		if (studio_layer_scon[k] != NULL) {
			make_material_delete_context(studio_layer_scon[k]);
		}
		studio_layer_scon[k] = studio_compile_node(g.layers[k].n[STUDIO_ROLE_RAMP], c, 1 + k);
		studio_layer_key[k]  = key;
		draw                 = true;
		break;
	}
	if (!draw) {
		return;
	}
	studio_layer_values = values;
	for (i32 k = 0; k < g.count; ++k) {
		if (studio_layer_scon[k] == NULL) {
			continue;
		}
		if (studio_layer_tile[k] == NULL) {
			studio_layer_tile[k] = gpu_create_render_target(STUDIO_TILE_SIZE, STUDIO_TILE_SIZE, GPU_TEXTURE_FORMAT_RGBA32);
		}
		parser_material_live_update(c, 1 + k);
		studio_draw_screen(studio_layer_scon[k], studio_layer_tile[k]);
	}
	studio_redraw();
}

static bool studio_paint_stale = false; // Committed edits not yet in the paint shader, the fill layers and the icon

// The edit is done: one undo step. The paint shader, the layers filled with the material and its icon follow when the
// studio closes, they are hidden behind it and slow to make.
static void studio_commit() {
	studio_need_commit = false;
	if (studio_undo_canvas != NULL) {
		history_edit_nodes(studio_undo_canvas, CANVAS_TYPE_MATERIAL, -1);
		studio_undo_canvas = NULL;
	}
	studio_paint_stale = true;
}

static void studio_apply_paint() {
	if (!studio_paint_stale || studio_material == NULL || array_index_of(g_project->_->materials, studio_material) < 0) {
		return;
	}
	studio_paint_stale         = false;
	slot_material_t *_material = g_context->material;
	g_context->material        = studio_material;
	make_material_parse_paint_material(true);
	if (layers_is_fill_material()) {
		layers_update_fill_layers();
	}
	if (layers_is_path_material()) {
		layers_update_path_layers();
	}
	util_render_make_material_preview();
	g_context->material = _material;
	if (g_context->material != studio_material) {
		make_material_parse_paint_material(true);
	}
	ui_dock_redraw(-1);
	ui_nodes_hwnd->redraws = 2;
}

// Commits and applies right away, for scripts checking the fill layers while the studio is open
void material_studio_apply() {
	if (studio_need_commit && studio_canvas() != NULL) {
		studio_commit();
	}
	studio_apply_paint();
}

// Rendering is not allowed while the windows draw, it happens here at the start of the frame
static void studio_update(void *_) {
	if (studio_material == NULL) {
		return;
	}
	ui_node_canvas_t *c = studio_canvas();
	if (array_index_of(g_project->_->materials, studio_material) < 0 || c == NULL) {
		material_studio_close(); // Deleted or rewired meanwhile
		return;
	}
	if (studio_need_commit && !g_ui->input_down && !ui_menu_show) {
		studio_commit();
	}
	u32 st = studio_hash_struct(c);
	f64 vs = studio_hash_values(c);
	if (st != studio_known_struct) { // An edit, undo, redo or the node editor
		studio_known_struct = st;
		studio_need_compile = true;
		studio_redraw();
	}
	if (vs != studio_known_values) {
		studio_known_values = vs;
		studio_need_render  = true;
		studio_redraw();
	}
	if (!material_studio_show) {
		return;
	}
	if (!studio_thumbs_made) {
		studio_make_thumbs();
		studio_redraw();
	}
	if (studio_need_compile) {
		studio_need_compile        = false;
		slot_material_t *_material = g_context->material;
		g_context->material        = studio_material;
		studio_compile();
		g_context->material = _material;
		studio_need_render  = true;
	}
	if (studio_need_render) {
		studio_need_render = false;
		iron_delay_idle_sleep();
		f64 t0 = iron_time();
		studio_render();
		studio_render_ms = (iron_time() - t0) * 1000.0;
		if (studio_view_handle != NULL) {
			studio_view_handle->redraws = 2;
		}
	}
	studio_update_tiles(c, vs);
}

void material_studio_open(slot_material_t *m) {
	if (m == NULL || !studio_graph_is(m->canvas)) {
		return;
	}
	ui_popovers_close();
	ui_menu_show         = false;
	studio_material      = m;
	material_studio_show = true;
	studio_closing       = false;
	studio_anim_start    = iron_time();
	studio_need_compile  = true;
	studio_need_commit   = false;
	studio_undo_canvas   = NULL;
	studio_sel           = 0;
	studio_stop          = 0;
	studio_known_struct  = studio_hash_struct(m->canvas);
	studio_known_values  = studio_hash_values(m->canvas);
	studio_layer_values  = -1.0;
	for (i32 k = 0; k < STUDIO_LAYERS; ++k) {
		studio_layer_key[k] = 0;
		studio_swipe[k]     = 0.0;
	}
	if (g_context->material != m) {
		context_set_material(m);
	}
	if (!studio_hooked) {
		studio_hooked = true;
		sys_notify_on_update(&studio_update, NULL);
	}
	if (studio_back_handle == NULL) {
		studio_back_handle = ui_window_create();
		studio_head_handle = ui_window_create();
		studio_view_handle = ui_window_create();
		studio_ctrl_handle = ui_window_create();
	}
	studio_ctrl_handle->scroll_offset = 0;
	ui_end_input(); // The tap that opened it does not reach the studio
	base_redraw_ui();
}

void material_studio_close() {
	if (!material_studio_show) {
		return;
	}
	if (studio_need_commit && studio_material != NULL && studio_canvas() != NULL) {
		studio_commit();
	}
	studio_apply_paint();
	material_studio_show = false;
	studio_closing       = true;
	studio_anim_start    = iron_time();
	base_redraw_ui();
}

bool material_studio_is_open() {
	return material_studio_show;
}

gpu_texture_t *material_studio_preview() {
	return studio_preview;
}

// Milliseconds the last preview compile (0) or render (1) took on the cpu
f32 material_studio_time(i32 which) {
	return which == 0 ? studio_compile_ms : studio_render_ms;
}

// Scrolls the controls, for scripts
void material_studio_scroll(f32 offset) {
	if (studio_ctrl_handle != NULL) {
		studio_ctrl_handle->scroll_offset = offset;
		studio_redraw();
	}
}

// Times the preview shader was compiled, a drag must not add to it
i32 material_studio_compiles() {
	return studio_compile_count;
}

// For scripts and tests: edits of the open studio material, each one undoable

static ui_node_canvas_t *studio_script_canvas() {
	if (studio_canvas() == NULL) {
		return NULL;
	}
	studio_begin_edit();
	studio_edited();
	return studio_material->canvas;
}

void material_studio_preset(i32 i) {
	ui_node_canvas_t *c = studio_script_canvas();
	if (c != NULL) {
		studio_graph_apply_preset(c, i);
		studio_sel = 0;
	}
}

i32 material_studio_add_layer(i32 preset) {
	ui_node_canvas_t *c = studio_script_canvas();
	i32               k = c != NULL ? studio_graph_add_layer_preset(c, preset) : -1;
	if (k >= 0) {
		studio_sel = k;
	}
	return k;
}

void material_studio_remove_layer(i32 k) {
	ui_node_canvas_t *c = studio_script_canvas();
	if (c != NULL) {
		studio_graph_remove_layer(c, k);
		studio_sel = 0;
	}
}

void material_studio_set_pattern(i32 k, char *type) {
	ui_node_canvas_t *c = studio_script_canvas();
	if (c != NULL) {
		studio_graph_set_pattern(c, k, type != NULL && type[0] != '\0' ? type : NULL);
	}
}

void material_studio_set_mask(i32 k, i32 mask) {
	ui_node_canvas_t *c = studio_script_canvas();
	if (c != NULL) {
		studio_graph_set_mask(c, k, mask);
	}
}

void material_studio_set_value(i32 k, i32 value, f32 v) {
	ui_node_canvas_t *c = studio_script_canvas();
	if (c != NULL) {
		studio_graph_set(c, k, value, v);
	}
}

f32 material_studio_get_value(i32 k, i32 value) {
	ui_node_canvas_t *c = g_context->material != NULL ? g_context->material->canvas : NULL;
	return studio_graph_is(c) ? studio_graph_get(c, k, value) : -1.0;
}

// Input i of the node with the role in layer k
void material_studio_set_input(i32 k, i32 role, i32 i, f32 v) {
	ui_node_canvas_t *c = studio_script_canvas();
	ui_node_t        *n = c != NULL ? studio_graph_node(c, k, role) : NULL;
	if (n != NULL && i < n->inputs->length) {
		n->inputs->buffer[i]->default_value->buffer[0] = v;
	}
}

f32 material_studio_get_input(i32 k, i32 role, i32 i) {
	ui_node_canvas_t *c = g_context->material != NULL ? g_context->material->canvas : NULL;
	ui_node_t        *n = studio_graph_is(c) ? studio_graph_node(c, k, role) : NULL;
	return n != NULL && i < n->inputs->length ? n->inputs->buffer[i]->default_value->buffer[0] : -1.0;
}

void material_studio_set_button(i32 k, i32 role, i32 i, f32 v) {
	ui_node_canvas_t *c = studio_script_canvas();
	ui_node_t        *n = c != NULL ? studio_graph_node(c, k, role) : NULL;
	if (n != NULL && i < n->buttons->length) {
		n->buttons->buffer[i]->default_value->buffer[0] = v;
	}
}

i32 material_studio_layers() {
	ui_node_canvas_t *c = g_context->material != NULL ? g_context->material->canvas : NULL;
	return studio_graph_is(c) ? studio_graph_layers(c) : 0;
}

// Color of stop i of the ramp of layer k
void material_studio_set_color(i32 k, i32 i, f32 r, f32 g, f32 b) {
	ui_node_canvas_t *c    = studio_script_canvas();
	ui_node_t        *ramp = c != NULL ? studio_graph_node(c, k, STUDIO_ROLE_RAMP) : NULL;
	f32_array_t      *v    = ramp != NULL ? ramp->buttons->buffer[0]->default_value : NULL;
	if (v != NULL && i * 5 + 2 < (i32)v->length) {
		v->buffer[i * 5 + 0] = r;
		v->buffer[i * 5 + 1] = g;
		v->buffer[i * 5 + 2] = b;
	}
}

// ██╗   ██╗██╗
// ██║   ██║██║
// ██║   ██║██║
// ██║   ██║██║
// ╚██████╔╝██║
//  ╚═════╝ ╚═╝

static void studio_label(char *text) {
	g_ui->_y += 6 * UI_SCALE();
	ui_text(text, UI_ALIGN_LEFT, 0x00000000);
}

static void studio_take_undo_on_touch() {
	if (g_ui->input_started) {
		studio_begin_edit(); // Taken before any value moves
	}
}

// A slider bound to a node input
static void studio_input_slider(ui_node_socket_t *s, char *text, f32 from, f32 to, f32 precision) {
	studio_take_undo_on_touch();
	f32 *v      = &s->default_value->buffer[0];
	f32  before = *v;
	ui_slider(v, text, from, to, true, precision, true, UI_ALIGN_LEFT, true);
	if (*v != before) {
		studio_edited();
	}
}

// A slider bound to a value made of node inputs, the slot keeps its pointer steady for the ui
static void studio_value_slider(i32 slot, i32 k, i32 value, char *text, f32 from, f32 to, f32 precision) {
	studio_take_undo_on_touch();
	ui_node_canvas_t *c = studio_material->canvas;
	studio_slots[slot]  = studio_graph_get(c, k, value);
	f32 before          = studio_slots[slot];
	ui_slider(&studio_slots[slot], text, from, to, true, precision, true, UI_ALIGN_LEFT, true);
	if (studio_slots[slot] != before) {
		studio_graph_set(c, k, value, studio_slots[slot]);
		studio_edited();
	}
}

// Chips: one choice of a few, the picked one highlighted
static i32 studio_chips(char **names, i32 count, i32 picked, i32 columns) {
	f32_array_t *row = f32_array_create(columns);
	for (i32 i = 0; i < columns; ++i) {
		row->buffer[i] = 1.0 / columns;
	}
	i32 tapped = -1;
	u32 col    = g_theme->BUTTON_COL;
	for (i32 i = 0; i < count; ++i) {
		if (i % columns == 0) {
			ui_row(row);
		}
		g_theme->BUTTON_COL = i == picked ? g_theme->HIGHLIGHT_COL : col;
		if (ui_button(names[i], UI_ALIGN_CENTER, "")) {
			tapped = i;
		}
		g_theme->BUTTON_COL = col;
	}
	for (i32 i = count; i % columns != 0; ++i) {
		ui_end_element(); // Fill the last row
	}
	array_free(row);
	free(row);
	return tapped;
}

static void studio_color_menu() {
	if (studio_color_target == NULL || studio_canvas() == NULL) {
		return;
	}
	f32 *v = studio_color_target;
	ui_fill(0, 0, g_ui->_w / (float)UI_SCALE(), g_theme->ELEMENT_H * 10, g_theme->SEPARATOR_COL);
	u32 color     = color_from_floats(v[0], v[1], v[2], 1.0);
	g_ui->changed = false;
	ui_color_wheel(&color, &studio_color_state, false, -1, 10 * g_theme->ELEMENT_H * UI_SCALE(), false, NULL, NULL);
	if (g_ui->changed) {
		v[0] = color_get_rb(color) / 255.0;
		v[1] = color_get_gb(color) / 255.0;
		v[2] = color_get_bb(color) / 255.0;
		studio_edited();
		ui_menu_keep_open = true;
	}
	if (ui_icon_button(tr("Done"), ICON_CHECK, UI_ALIGN_CENTER)) {
		ui_menu_show = false;
	}
}

// A color swatch, a tap opens the color wheel on the three floats
static bool studio_swatch(f32 *v) {
	u32 color = color_from_floats(v[0], v[1], v[2], 1.0);
	if (ui_swatch(color) == UI_STATE_RELEASED && g_ui->combo_selected_id == 0) { // On release, the lifted finger would close the menu
		studio_begin_edit();
		studio_color_target = v;
		ui_menu_draw(&studio_color_menu, -1, -1);
		return true;
	}
	return false;
}

// Stop indices of the ramp by position
static i32 studio_ramp_sorted(f32_array_t *v, i32 *sorted) {
	i32 len = v->length / 5;
	len     = len > 8 ? 8 : len;
	for (i32 i = 0; i < len; ++i) {
		sorted[i] = i;
	}
	for (i32 i = 1; i < len; ++i) {
		i32 key = sorted[i];
		i32 j   = i - 1;
		while (j >= 0 && v->buffer[sorted[j] * 5 + 4] > v->buffer[key * 5 + 4]) {
			sorted[j + 1] = sorted[j];
			j--;
		}
		sorted[j + 1] = key;
	}
	return len;
}

static void studio_ramp_color_at(f32_array_t *v, i32 *sorted, i32 len, f32 t, f32 *out) {
	i32 a = sorted[0];
	i32 b = sorted[0];
	for (i32 i = 0; i < len; ++i) {
		if (v->buffer[sorted[i] * 5 + 4] <= t) {
			a = sorted[i];
			b = i + 1 < len ? sorted[i + 1] : sorted[i];
		}
	}
	f32 pa = v->buffer[a * 5 + 4];
	f32 pb = v->buffer[b * 5 + 4];
	f32 f  = pb > pa ? fminf(fmaxf((t - pa) / (pb - pa), 0.0), 1.0) : 0.0;
	for (i32 j = 0; j < 3; ++j) {
		out[j] = v->buffer[a * 5 + j] * (1.0 - f) + v->buffer[b * 5 + j] * f;
	}
}

// The colors of a layer: the gradient from the recesses to the surface, a swatch for each stop, its position
static void studio_draw_ramp(ui_node_t *ramp, bool pattern) {
	f32_array_t *v = ramp->buttons->buffer[0]->default_value;
	i32          sorted[8];
	i32          len = studio_ramp_sorted(v, sorted);
	if (len == 0) {
		return;
	}
	if (!pattern) { // A plain layer shows the stop at the surface
		ui_row2();
		ui_text(tr("Color"), UI_ALIGN_LEFT, 0x00000000);
		if (studio_swatch(v->buffer + sorted[len - 1] * 5)) {
			studio_stop = sorted[len - 1];
		}
		return;
	}
	if (studio_stop >= len) {
		studio_stop = len - 1;
	}
	// Gradient with a mark for each stop, the picked one white
	f32 w      = g_ui->_w / (f32)UI_SCALE();
	f32 h      = g_theme->ELEMENT_H * 0.8;
	i32 slices = 48;
	for (i32 s = 0; s < slices; ++s) {
		f32 t = (s + 0.5) / slices;
		f32 col[3];
		studio_ramp_color_at(v, sorted, len, t, col);
		ui_fill(w * s / slices, 0, w / slices + 1.0, h, color_from_floats(col[0], col[1], col[2], 1.0));
	}
	for (i32 i = 0; i < len; ++i) {
		f32 x = v->buffer[sorted[i] * 5 + 4] * w;
		ui_fill(fminf(fmaxf(x - 1.5, 0.0), w - 3.0), 0, 3.0, h, sorted[i] == studio_stop ? 0xffffffff : 0xff000000);
	}
	// A touch on the gradient picks the nearest stop, dragging moves it
	f32 rx = g_ui->_window_x + g_ui->_x;
	f32 ry = g_ui->_window_y + g_ui->_y;
	f32 rw = g_ui->_w;
	f32 rh = h * UI_SCALE();
	if (g_ui->input_enabled && g_ui->input_started && g_ui->input_x >= rx && g_ui->input_x < rx + rw && g_ui->input_y >= ry &&
	    g_ui->input_y < ry + rh) {
		f32 t    = (g_ui->input_x - rx) / rw;
		f32 best = 2.0;
		for (i32 i = 0; i < len; ++i) {
			f32 d = fabsf(v->buffer[sorted[i] * 5 + 4] - t);
			if (d < best) {
				best        = d;
				studio_stop = sorted[i];
			}
		}
		studio_begin_edit();
		studio_ramp_drag = true;
	}
	if (studio_ramp_drag) {
		if (g_ui->input_down) {
			g_ui->is_scrolling = true; // The panel does not scroll under the finger
			f32 t              = fminf(fmaxf((g_ui->input_x - rx) / rw, 0.0), 1.0);
			if (v->buffer[studio_stop * 5 + 4] != t) {
				v->buffer[studio_stop * 5 + 4] = t;
				studio_edited();
			}
		}
		else {
			studio_ramp_drag = false;
		}
	}
	g_ui->_y += h * UI_SCALE() + 4 * UI_SCALE();

	// Swatches by position, then add and remove
	f32_array_t *row = f32_array_create(6);
	for (i32 i = 0; i < 6; ++i) {
		row->buffer[i] = 1.0 / 6.0;
	}
	ui_row(row);
	for (i32 i = 0; i < len && i < 4; ++i) {
		if (studio_swatch(v->buffer + sorted[i] * 5)) {
			studio_stop = sorted[i];
		}
	}
	for (i32 i = len; i < 4; ++i) {
		ui_end_element();
	}
	if (ui_button("+", UI_ALIGN_CENTER, "") && len < 4) { // A new stop half way to the next one
		studio_begin_edit();
		i32 at = 0;
		for (i32 i = 0; i < len; ++i) {
			if (sorted[i] == studio_stop) {
				at = i;
			}
		}
		i32 a = sorted[at];
		i32 b = at + 1 < len ? sorted[at + 1] : sorted[at];
		f32 p = a == b ? fminf(v->buffer[a * 5 + 4] + 0.2, 1.0) : (v->buffer[a * 5 + 4] + v->buffer[b * 5 + 4]) * 0.5;
		f32 col[3];
		studio_ramp_color_at(v, sorted, len, p, col);
		f32_array_push(v, col[0]);
		f32_array_push(v, col[1]);
		f32_array_push(v, col[2]);
		f32_array_push(v, 1.0);
		f32_array_push(v, p);
		studio_stop = len;
		studio_edited();
	}
	if (ui_button("-", UI_ALIGN_CENTER, "") && len > 1) {
		studio_begin_edit();
		for (i32 i = studio_stop * 5; i + 5 < (i32)v->length; ++i) {
			v->buffer[i] = v->buffer[i + 5];
		}
		v->length -= 5;
		studio_stop = 0;
		studio_edited();
	}
	array_free(row);
	free(row);
	// Position of the picked stop, it drags without compiling like the sliders
	studio_take_undo_on_touch();
	f32 *pos    = &v->buffer[studio_stop * 5 + 4];
	f32  before = *pos;
	ui_slider(pos, tr("Stop Position"), 0.0, 1.0, true, 100, true, UI_ALIGN_LEFT, true);
	if (*pos != before) {
		studio_edited();
	}
}

static void studio_add_layer_menu() {
	if (studio_canvas() == NULL) {
		return;
	}
	for (i32 i = 0; i < studio_layer_preset_count(); ++i) {
		if (ui_menu_button(studio_layer_preset_name(i), "", ICON_NONE)) {
			material_studio_add_layer(i);
		}
	}
	if (ui_menu_button(tr("Empty Layer"), "", ICON_NONE)) {
		material_studio_add_layer(-1);
	}
}

// The layer names are the names of their Material Mix nodes, shown in the node editor too
static char *studio_layer_name(ui_node_canvas_t *c, i32 k) {
	if (k == 0) {
		return tr("Base");
	}
	ui_node_t *mix = studio_graph_node(c, k, STUDIO_ROLE_MIX);
	return string_equals(mix->name, "Material Mix") ? tr("Layer") : mix->name;
}

// Layer list gestures: a tap picks a layer, a long press lifts it to drag it to another place, a swipe to the left shows
// the delete button under it. The base layer stays at the bottom.

#define STUDIO_ROW_H   64
#define STUDIO_TRASH_W 96

enum { LG_NONE, LG_PRESSED, LG_SWIPE, LG_DRAG, LG_SCROLL };
static i32 studio_lg_mode  = LG_NONE;
static i32 studio_lg_layer = -1;
static f32 studio_lg_x0    = 0.0;
static f32 studio_lg_y0    = 0.0;
static f32 studio_lg_off0  = 0.0;
static f32 studio_lg_grab  = 0.0; // Finger to the top of the lifted row
static f64 studio_lg_t0    = 0.0;

static char *studio_layer_subtitle(ui_node_canvas_t *c, i32 k) {
	ui_node_t *pat     = studio_graph_node(c, k, STUDIO_ROLE_PATTERN);
	char      *pattern = tr("Plain");
	for (i32 i = 0; pat != NULL && i < pattern_node_count(); ++i) {
		if (string_equals(pat->type, pattern_node_type(i))) {
			pattern = pattern_node_name(i);
		}
	}
	if (k == 0) {
		return pattern;
	}
	char *masks[STUDIO_MASK_COUNT] = {tr("Everywhere"), tr("Edges"), tr("Cavities"), tr("Facing Up"), tr("Pattern")};
	return string_tmp("%s, %s", pattern, masks[studio_graph_mask(c, k)]);
}

static void studio_draw_row(ui_node_canvas_t *c, i32 k, f32 x, f32 y, f32 w, f32 h, f32 off, bool lifted) {
	f32          r       = 10 * UI_SCALE();
	f32          pad     = 7 * UI_SCALE();
	i32          corners = UI_BASE_CORNER_TL | UI_BASE_CORNER_TR | UI_BASE_CORNER_BL | UI_BASE_CORNER_BR;
	draw_font_t *font    = g_ui->ops->font;
	if (off < 0.0) { // The delete button under the row
		draw_set_color(0xffd23c3c);
		ui_base_fill_round_rect(x, y, w, h, r, corners);
		draw_set_color(0xffffffff);
		draw_set_font(font, g_ui->font_size);
		char *del = tr("Delete");
		f32   tw  = draw_string_width(font, g_ui->font_size, del);
		draw_string(del, x + w - STUDIO_TRASH_W * UI_SCALE() / 2.0 - tw / 2.0, y + (h - draw_font_height(font, g_ui->font_size)) / 2.0);
	}
	if (lifted) {
		for (i32 i = 6; i >= 1; --i) {
			draw_set_color(0x10000000);
			ui_base_fill_round_rect(x + off - i, y - i + 3, w + i * 2, h + i * 2, r + i, corners);
		}
	}
	bool clip = off < 0.0 && !g_ui->scissor; // The sliding row stays inside its place
	if (clip) {
		draw_scissor((i32)x, (i32)y, (i32)(w + off), (i32)h);
	}
	draw_set_color(k == studio_sel ? g_theme->HIGHLIGHT_COL : g_theme->BUTTON_COL);
	ui_base_fill_round_rect(x + off, y, w, h, r, corners);
	f32 ts = h - pad * 2;
	if (studio_layer_tile[k] != NULL) {
		draw_set_color(0xffffffff);
		draw_scaled_image(studio_layer_tile[k], x + off + pad, y + pad, ts, ts);
	}
	f32 tx = x + off + pad * 2 + ts;
	f32 lh = draw_font_height(font, g_ui->font_size);
	draw_set_font(font, g_ui->font_size);
	draw_set_color(g_theme->TEXT_COL);
	draw_string(studio_layer_name(c, k), tx, y + h / 2.0 - lh - 1 * UI_SCALE());
	draw_set_color(g_theme->LABEL_COL);
	draw_string(studio_layer_subtitle(c, k), tx, y + h / 2.0 + 1 * UI_SCALE());
	draw_set_color(0xffffffff);
	if (clip) {
		draw_flush();
		gpu_disable_scissor();
	}
}

static void studio_draw_layers(ui_node_canvas_t *c) {
	i32 count = studio_graph_layers(c);
	if (studio_sel >= count) {
		studio_sel = count - 1;
	}
	f32 rh    = STUDIO_ROW_H * UI_SCALE();
	f32 gap   = 6 * UI_SCALE();
	f32 trash = STUDIO_TRASH_W * UI_SCALE();
	f32 x     = g_ui->_x + 2 * UI_SCALE();
	f32 w     = g_ui->_w - 4 * UI_SCALE();
	f32 y0    = g_ui->_y;
	f32 lx    = g_ui->input_x - g_ui->_window_x;
	f32 ly    = g_ui->input_y - g_ui->_window_y;
	bool inside = lx >= x && lx < x + w && ly >= y0 && ly < y0 + count * rh;
	i32  hit    = inside ? count - 1 - (i32)((ly - y0) / rh) : -1;

	if (g_ui->input_enabled && !ui_menu_show) {
		if (g_ui->input_started && inside && studio_lg_mode == LG_NONE) {
			if (hit > 0 && studio_swipe[hit] < 0.0 && lx > x + w - trash) { // Delete
				studio_begin_edit();
				studio_graph_remove_layer(c, hit);
				studio_edited();
				for (i32 k = 0; k < STUDIO_LAYERS; ++k) {
					studio_swipe[k] = 0.0;
				}
				if (studio_sel >= hit) {
					studio_sel = studio_sel - 1 < 0 ? 0 : studio_sel - 1;
				}
				count = studio_graph_layers(c);
			}
			else {
				studio_lg_mode  = LG_PRESSED;
				studio_lg_layer = hit;
				studio_lg_x0    = lx;
				studio_lg_y0    = ly;
				studio_lg_t0    = iron_time();
				studio_lg_off0  = studio_swipe[hit];
				studio_lg_grab  = ly - (y0 + (count - 1 - hit) * rh);
				for (i32 k = 0; k < STUDIO_LAYERS; ++k) {
					if (k != hit) {
						studio_swipe[k] = 0.0;
					}
				}
			}
		}
		if (studio_lg_mode == LG_PRESSED && g_ui->input_down) {
			f32 dx  = lx - studio_lg_x0;
			f32 dy  = ly - studio_lg_y0;
			f32 thr = 10 * UI_SCALE();
			if (studio_lg_layer > 0 && fabsf(dx) > thr && fabsf(dx) > fabsf(dy)) {
				studio_lg_mode = LG_SWIPE;
			}
			else if (fabsf(dy) > thr) {
				studio_lg_mode = LG_SCROLL; // The panel scrolls
			}
			else if (studio_lg_layer > 0 && iron_time() - studio_lg_t0 > 0.4) {
				studio_lg_mode            = LG_DRAG;
				g_ui->input_started_time = 0; // Our long press, not the ui's right click
			}
		}
		if (studio_lg_mode == LG_SWIPE && g_ui->input_down) {
			studio_swipe[studio_lg_layer] = fminf(0.0, fmaxf(-trash, studio_lg_off0 + lx - studio_lg_x0));
		}
		if ((studio_lg_mode == LG_SWIPE || studio_lg_mode == LG_DRAG) && g_ui->input_down) {
			g_ui->input_dy = 0; // The panel does not scroll under the finger
		}
	}
	// Place of the lifted row among the others, display rows top down, the base is the last
	i32 target = 0;
	if (studio_lg_mode == LG_DRAG) {
		target = (i32)((ly - studio_lg_grab - y0 + rh / 2.0) / rh);
		target = target < 0 ? 0 : target > count - 2 ? count - 2 : target;
	}
	if (studio_lg_mode != LG_NONE && !g_ui->input_down) { // Let go
		i32 k = studio_lg_layer;
		if (studio_lg_mode == LG_PRESSED) {
			if (k > 0 && studio_swipe[k] < 0.0) {
				studio_swipe[k] = 0.0;
			}
			else {
				studio_sel  = k;
				studio_stop = 0;
			}
		}
		else if (studio_lg_mode == LG_SWIPE) {
			studio_swipe[k] = studio_swipe[k] < -trash / 2.0 ? -trash : 0.0;
		}
		else if (studio_lg_mode == LG_DRAG) {
			i32 to = count - 1 - target;
			if (to != k) {
				studio_begin_edit();
				while (k != to) {
					i32 dir = to > k ? 1 : -1;
					studio_graph_move_layer(c, k, dir);
					k += dir;
				}
				studio_edited();
			}
			studio_sel = to;
		}
		studio_lg_mode = LG_NONE;
	}
	if (studio_lg_mode != LG_NONE) {
		studio_ctrl_handle->redraws = 2; // The long press and the drag go on without new input
	}

	// Rows, the lifted one last over the others
	i32 slot = 0;
	for (i32 i = 0; i < count; ++i) {
		i32 k = count - 1 - i;
		if (studio_lg_mode == LG_DRAG && k == studio_lg_layer) {
			continue;
		}
		if (studio_lg_mode == LG_DRAG && slot == target) {
			slot++; // Room for the lifted row
		}
		studio_draw_row(c, k, x, y0 + slot * rh, w, rh - gap, k > 0 ? studio_swipe[k] : 0.0, false);
		slot++;
	}
	if (studio_lg_mode == LG_DRAG) {
		f32 dy = fminf(fmaxf(ly - studio_lg_grab, y0), y0 + (count - 2) * rh);
		studio_draw_row(c, studio_lg_layer, x, dy, w, rh - gap, 0.0, true);
	}
	ui_end_element_of_size(count * rh);

	if (count < STUDIO_LAYERS && ui_icon_button(tr("Add Layer"), ICON_PLUS, UI_ALIGN_CENTER)) {
		ui_menu_draw(&studio_add_layer_menu, -1, -1);
	}
}

// Tiles of the patterns with their names, None first, the picked one framed
static i32 studio_pattern_grid(i32 current) {
	i32          cols   = 5;
	i32          count  = pattern_node_count() + 1;
	f32_array_t *row    = f32_array_create(cols);
	i32          tapped = -1;
	for (i32 i = 0; i < cols; ++i) {
		row->buffer[i] = 1.0 / cols;
	}
	for (i32 r = 0; r * cols < count; ++r) {
		ui_row(row);
		f32 size = g_ui->_w - 6 * UI_SCALE(); // The row gives each cell its width
		for (i32 j = 0; j < cols; ++j) {
			i32 i = r * cols + j;
			if (i >= count) {
				ui_end_element();
				continue;
			}
			gpu_texture_t *thumb = i == 0 ? studio_thumb_none : i - 1 < STUDIO_THUMBS ? studio_thumbs[i - 1] : NULL;
			if (i == current) {
				ui_fill(0, 0, g_ui->_w / UI_SCALE() - 2, size / UI_SCALE() + 4, g_theme->HIGHLIGHT_COL);
			}
			g_ui->_x += 3 * UI_SCALE();
			g_ui->_y += 2 * UI_SCALE();
			g_ui->image_scroll_align = false;
			i32 state                = thumb != NULL ? ui_image(thumb, 0xffffffff, size) : UI_STATE_IDLE;
			if (thumb == NULL) {
				ui_end_element();
			}
			g_ui->image_scroll_align = true;
			g_ui->_y -= 2 * UI_SCALE();
			if (state == UI_STATE_RELEASED) {
				tapped = i;
			}
		}
		ui_row(row);
		for (i32 j = 0; j < cols; ++j) {
			i32 i = r * cols + j;
			if (i >= count) {
				ui_end_element();
				continue;
			}
			ui_text(i == 0 ? tr("None") : pattern_node_name(i - 1), UI_ALIGN_CENTER, 0x00000000);
		}
	}
	array_free(row);
	free(row);
	return tapped;
}

static void studio_draw_pattern(ui_node_canvas_t *c, i32 k) {
	ui_node_t *pat     = studio_graph_node(c, k, STUDIO_ROLE_PATTERN);
	i32        count   = pattern_node_count();
	i32        current = 0;
	for (i32 i = 0; i < count; ++i) {
		if (pat != NULL && string_equals(pat->type, pattern_node_type(i))) {
			current = i + 1;
		}
	}
	i32 tapped = studio_pattern_grid(current);
	if (tapped >= 0 && tapped != current) {
		studio_begin_edit();
		studio_graph_set_pattern(c, k, tapped == 0 ? NULL : pattern_node_type(tapped - 1));
		studio_edited();
		pat = studio_graph_node(c, k, STUDIO_ROLE_PATTERN);
	}
	if (pat == NULL) {
		return;
	}
	// The pattern node's inputs by name: the shared ones, then its own
	for (i32 i = 1; i < pat->inputs->length; ++i) {
		ui_node_socket_t *s = pat->inputs->buffer[i];
		if (i == 5 || (i == 7 && pat->inputs->buffer[6]->default_value->buffer[0] <= 0.0)) {
			continue; // Seed has the Shuffle button, Distort Scale shows with Distort
		}
		studio_input_slider(s, tr(s->name), s->min, s->max, s->max > 20.0 ? 10 : 100);
	}
	ui_row2();
	if (ui_icon_button(tr("Shuffle"), ICON_REFRESH, UI_ALIGN_CENTER)) {
		studio_begin_edit();
		pat->inputs->buffer[5]->default_value->buffer[0] = (f32)iron_random_get_max(1000);
		studio_edited();
	}
	// Mapping toggles between UV and Triplanar
	i32   mapping = (i32)pat->buttons->buffer[0]->default_value->buffer[0];
	char *label   = mapping == 0 ? tr("UV Mapping") : tr("Triplanar Mapping");
	if (ui_button(label, UI_ALIGN_CENTER, "")) {
		studio_begin_edit();
		pat->buttons->buffer[0]->default_value->buffer[0] = mapping == 0 ? 1 : 0;
		studio_edited();
	}
}

static void studio_draw_mask(ui_node_canvas_t *c, i32 k) {
	char *masks[STUDIO_MASK_COUNT] = {tr("Full"), tr("Edges"), tr("Cavities"), tr("Facing Up"), tr("Pattern")};
	i32   mask                     = studio_graph_mask(c, k);
	bool  pattern                  = studio_graph_node(c, k, STUDIO_ROLE_PATTERN) != NULL;
	i32   tapped                   = studio_chips(masks, pattern ? STUDIO_MASK_COUNT : STUDIO_MASK_COUNT - 1, mask, 3);
	if (tapped >= 0 && tapped != mask) {
		studio_begin_edit();
		studio_graph_set_mask(c, k, tapped);
		studio_edited();
		mask = tapped;
	}
	if (mask != STUDIO_MASK_FULL) {
		studio_value_slider(10, k, STUDIO_VALUE_MASK_AMOUNT, tr("Amount"), 0.0, 1.0, 100);
		studio_value_slider(11, k, STUDIO_VALUE_MASK_SOFT, tr("Softness"), 0.0, 1.0, 100);
		if (mask != STUDIO_MASK_PATTERN) {
			studio_value_slider(12, k, STUDIO_VALUE_MASK_BREAKUP, tr("Breakup"), 0.0, 1.0, 100);
		}
		else {
			bool inv = studio_graph_get(c, k, STUDIO_VALUE_MASK_INVERT) > 0.5;
			bool was = inv;
			ui_check(&inv, tr("Where the pattern is low"), "");
			if (inv != was) {
				studio_begin_edit();
				studio_graph_set(c, k, STUDIO_VALUE_MASK_INVERT, inv ? 1.0 : 0.0);
				studio_edited();
			}
		}
	}
	studio_value_slider(13, k, STUDIO_VALUE_OPACITY, tr("Opacity"), 0.0, 1.0, 100);

	studio_label(tr("Changes"));
	char *channels[4] = {tr("Color"), tr("Roughness"), tr("Metallic"), tr("Height")};
	ui_row4();
	u32 col = g_theme->BUTTON_COL;
	for (i32 ch = 0; ch < 4; ++ch) {
		bool on             = studio_graph_channel(c, k, ch);
		g_theme->BUTTON_COL = on ? g_theme->HIGHLIGHT_COL : col;
		if (ui_button(channels[ch], UI_ALIGN_CENTER, "")) {
			studio_begin_edit();
			studio_graph_set_channel(c, k, ch, !on);
			studio_edited();
		}
		g_theme->BUTTON_COL = col;
	}
}

static void studio_draw_controls(ui_node_canvas_t *c) {
	studio_label(tr("Presets"));
	i32   n = studio_preset_count();
	char *presets[32];
	for (i32 i = 0; i < n && i < 32; ++i) {
		presets[i] = studio_preset_name(i);
	}
	i32 preset = studio_chips(presets, n, -1, 3);
	if (preset >= 0) {
		material_studio_preset(preset);
	}

	studio_label(tr("Layers"));
	studio_draw_layers(c);

	i32 k = studio_sel;
	if (k < 0 || k >= studio_graph_layers(c)) {
		return;
	}
	studio_label(k == 0 ? tr("Base Pattern") : tr("Pattern"));
	studio_draw_pattern(c, k);
	bool pattern = studio_graph_node(c, k, STUDIO_ROLE_PATTERN) != NULL;

	studio_label(tr("Colors"));
	studio_draw_ramp(studio_graph_node(c, k, STUDIO_ROLE_RAMP), pattern);
	if (pattern) {
		studio_value_slider(0, k, STUDIO_VALUE_CELL_VAR, tr("Cell Variation"), 0.0, 1.0, 100);
	}

	studio_label(tr("Surface"));
	studio_value_slider(1, k, STUDIO_VALUE_ROUGHNESS, tr("Roughness"), 0.0, 1.0, 100);
	if (pattern) {
		studio_value_slider(2, k, STUDIO_VALUE_ROUGH_VAR, tr("Roughness Variation"), 0.0, 1.0, 100);
	}
	studio_value_slider(3, k, STUDIO_VALUE_METALLIC, tr("Metallic"), 0.0, 1.0, 100);
	if (pattern) {
		studio_value_slider(4, k, STUDIO_VALUE_DEPTH, tr("Depth"), 0.0, 1.0, 100);
	}

	if (k > 0) {
		studio_label(tr("Mask"));
		studio_draw_mask(c, k);
	}
	g_ui->_y += 12 * UI_SCALE();
}

static void studio_open_nodes(void *_) {
	ui_base_show_material_nodes();
}

// The material name as the title, editable in place
static void studio_draw_title(f32 w, f32 h) {
	i32 _font_size = g_ui->font_size;
	i32 _element_h = g_theme->ELEMENT_H;
	i32 _button_h  = g_theme->BUTTON_H;
	u32 _button    = g_theme->BUTTON_COL;
	u32 _hover     = g_theme->HOVER_COL;
	g_ui->font_size     = _font_size * 3;
	g_theme->ELEMENT_H  = (i32)(h / UI_SCALE());
	g_theme->BUTTON_H   = g_theme->ELEMENT_H;
	g_theme->BUTTON_COL = 0x00000000; // Reads as a title, the field shows while typing
	g_theme->HOVER_COL  = 0x18ffffff;
	draw_set_font(g_ui->ops->font, g_ui->font_size);
	f32 _w     = g_ui->_w;
	g_ui->_w   = w;
	char *name = ui_text_input(&studio_material->canvas->name, "", UI_ALIGN_LEFT, true, false);
	g_ui->_w   = _w;
	g_ui->font_size     = _font_size;
	g_theme->ELEMENT_H  = _element_h;
	g_theme->BUTTON_H   = _button_h;
	g_theme->BUTTON_COL = _button;
	g_theme->HOVER_COL  = _hover;
	draw_set_font(g_ui->ops->font, g_ui->font_size);
	if (ui_item_changed() && !string_equals(name, "")) {
		studio_material->canvas->name = string_copy(slot_material_unique_name(studio_material->canvas, name));
		ui_dock_redraw(-1);
	}
}

// The name at the left, the buttons at the right of its row
static void studio_draw_header(f32 w) {
	draw_font_t *font = g_ui->ops->font;
	f32          h    = math_floor(draw_font_height(font, g_ui->font_size * 3) + 16 * UI_SCALE());
	f32          y0   = g_ui->_y;
	f32          _x   = g_ui->_x;
	f32          _w   = g_ui->_w;
	f32          bw   = math_floor(140 * UI_SCALE());
	f32          gap  = math_floor(8 * UI_SCALE());
	f32          by   = math_floor(y0 + (h - UI_ELEMENT_H()) / 2.0);

	studio_draw_title(w - bw * 2 - gap * 3, h);

	g_ui->_y = by;
	g_ui->_x = _x + w - bw * 2 - gap;
	g_ui->_w = bw;
	if (ui_icon_button(tr("Edit Nodes"), ICON_NODES, UI_ALIGN_CENTER)) {
		material_studio_close();
		sys_notify_on_next_frame(&studio_open_nodes, NULL);
	}

	g_ui->_y = by;
	g_ui->_x = _x + w - bw;
	g_ui->_w = bw;
	if (ui_icon_button(tr("Done"), ICON_CHECK, UI_ALIGN_CENTER)) {
		material_studio_close();
	}

	g_ui->_x = _x;
	g_ui->_w = _w;
	g_ui->_y = y0 + h;
}

void ui_material_studio_render_ui() {
	f64 t = (iron_time() - studio_anim_start) / STUDIO_ANIM_TIME;
	t     = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
	if (studio_closing && t >= 1.0) {
		studio_closing  = false;
		studio_material = NULL;
		make_material_parse_mesh_material(); // The viewport shader, in case a commit left another one
	}
	if (!material_studio_show && !studio_closing) {
		return;
	}
	ui_node_canvas_t *c = studio_canvas();
	if (c == NULL) {
		return;
	}
	f32 k = studio_closing ? 1.0 - t * t : 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
	if (t < 1.0) {
		base_redraw_ui();
		iron_delay_idle_sleep();
	}
	if (material_studio_show && keyboard_started("escape") && !ui_menu_show) {
		material_studio_close();
	}

	i32  margin         = math_floor(16 * UI_SCALE());
	i32  x              = margin;
	i32  y              = margin;
	i32  w              = iron_window_width() - margin * 2;
	i32  h              = iron_window_height() - margin * 2;
	bool input          = g_ui->input_enabled;
	g_ui->input_enabled = material_studio_show && !ui_menu_show && g_ui->combo_selected_id == 0;

	// Opaque panel over everything
	ui_base_panel_backdrop = NULL;
	ui_base_panel_window(studio_back_handle, x, y, w, h, UI_BASE_CORNER_TL | UI_BASE_CORNER_TR | UI_BASE_CORNER_BL | UI_BASE_CORNER_BR, true);
	ui_window_composite_alpha = k;
	ui_window_composite_scale = 0.96 + 0.04 * k;
	ui_window_composite_ox    = x + w / 2.0;
	ui_window_composite_oy    = y + h / 2.0;
	ui_end_window();

	u32 window_bg          = g_theme->WINDOW_BG_COL;
	g_theme->WINDOW_BG_COL = 0x00000000; // The panel shows through
	i32 pad                = math_floor(12 * UI_SCALE());
	i32 head_h             = math_floor(g_ui->font_size * 4 * 1.3 + 32 * UI_SCALE());
	if (ui_window(studio_head_handle, x + pad, y + pad / 2, w - pad * 2, head_h, false)) {
		studio_draw_header(w - pad * 2);
	}
	ui_end_window();

	// Preview left and controls right, the preview on top in portrait
	i32  body_y   = y + pad / 2 + head_h;
	i32  body_h   = h - head_h - pad;
	bool portrait = w < h;
	i32  view_s   = portrait ? math_floor(fminf(w - pad * 2, body_h * 0.45)) : math_floor(fminf(body_h, (w - pad * 3) * 0.55));
	i32  view_x   = portrait ? x + (w - view_s) / 2 : x + w - pad - view_s; // Preview at the right, controls at the left
	i32  ctrl_x   = x + pad;
	i32  ctrl_y   = portrait ? body_y + view_s + pad : body_y;
	i32  ctrl_w   = portrait ? w - pad * 2 : view_x - pad - ctrl_x;
	i32  ctrl_h   = portrait ? y + h - pad - ctrl_y : body_h;

	if (ui_window(studio_view_handle, view_x, body_y, view_s, view_s, false)) {
		if (studio_preview != NULL) {
			g_ui->image_scroll_align = false;
			ui_state_t state         = ui_image(studio_preview, 0xffffffff, view_s);
			g_ui->image_scroll_align = true;
			if (state == UI_STATE_DOWN && (mouse_movement_x != 0 || mouse_movement_y != 0)) { // Turn the cube with the finger
				studio_yaw -= mouse_movement_x * 0.01;
				studio_pitch       = fmaxf(-1.2, fminf(1.2, studio_pitch + mouse_movement_y * 0.01));
				studio_need_render = true;
			}
		}
		// Cube or sphere, at the bottom left
		f32 bs   = math_floor(UI_ELEMENT_H() * 1.4);
		f32 m    = math_floor(10 * UI_SCALE());
		g_ui->_x = m;
		g_ui->_y = view_s - bs - m;
		g_ui->_w = bs;
		if (ui_icon_button("", studio_sphere ? ICON_CUBE : ICON_SPHERE, UI_ALIGN_CENTER)) {
			studio_sphere      = !studio_sphere;
			studio_need_render = true;
		}
	}
	ui_end_window();
	// Pinch or wheel over the preview zooms
	if (material_studio_show && !ui_menu_show && mouse_wheel_delta != 0 && mouse_x >= view_x && mouse_x < view_x + view_s && mouse_y >= body_y &&
	    mouse_y < body_y + view_s) {
		studio_dist        = fmaxf(1.0, fminf(4.0, studio_dist * (1.0 + mouse_wheel_delta * 0.08)));
		studio_need_render = true;
	}

	if (ui_window(studio_ctrl_handle, ctrl_x, ctrl_y, ctrl_w, ctrl_h, false)) {
		studio_draw_controls(c);
	}
	ui_end_window();
	g_theme->WINDOW_BG_COL = window_bg;

	ui_window_composite_alpha = 1.0;
	ui_window_composite_scale = 1.0;
	g_ui->input_enabled       = input;

	if (studio_need_render || studio_need_compile) {
		iron_delay_idle_sleep();
	}
}
