#include "../global.h"

// Material Studio: a full screen editor of the node graph of a studio material (studio_graph.c) with a few controls and
// a large preview. Every control writes the inputs of real nodes, the graph stays editable in the node editor.
// The controls are native (app_api_studio.c, paint/native/Panels/MaterialStudioView.swift): the engine draws the
// background and the preview, in the rectangle the native editor leaves for it, and turns the preview with the finger.
// The preview shader reads the node values from a texture (parser_material_live_*), so dragging a slider only renders the
// preview again. Changes to the graph itself compile it. Letting go commits the edit: undo, the paint shader, the fill
// layers using the material and its sphere icon.

#define STUDIO_PREVIEW_SIZE 1024 // Own targets, the material preview ones are 512
#define STUDIO_ANIM_TIME    0.2
#define STUDIO_BACKGROUND   0xff1c1c1e // Under the native editor, its system background in dark mode

bool material_studio_show = false;

static slot_material_t  *studio_material      = NULL;
static f64               studio_anim_start    = 0.0;
static bool              studio_closing       = false;
static ui_window_t      *studio_back_handle   = NULL;
static gpu_texture_t    *studio_preview       = NULL;
static mesh_object_t    *studio_cube          = NULL;
static shader_context_t *studio_scon          = NULL;
static bool              studio_need_compile  = false; // The preview shader is out of date
static bool              studio_need_render   = false; // The preview image is out of date
static bool              studio_need_commit   = false; // An edit to commit once the touch ends
static bool              studio_native_drag   = false; // A native control is held: its edits make one undo step
static bool              studio_hooked        = false;
static ui_node_canvas_t *studio_undo_canvas   = NULL; // Canvas before the edit, for undo
static ui_node_canvas_t *studio_open_canvas   = NULL; // Canvas when the studio opened, for Cancel
static f32               studio_yaw           = 0.65;
static f32               studio_pitch         = 0.4;
static f32               studio_dist          = 1.75; // Camera distance, pinch or wheel to zoom
static bool              studio_sphere        = false; // Preview on the sphere instead of the cube
static bool              studio_orbiting      = false; // A drag that started on the preview turns it
static i32               studio_sel           = 0;    // Selected layer
static i32               studio_stop          = 0;    // Selected stop of its color ramp
static f32              studio_view[4]       = {0, 0, 0, 0}; // Preview rectangle in window pixels, from the native editor
static u32              studio_tiles_version = 0;           // The layer tiles were drawn again
static i32               studio_compile_count = 0;
static f32               studio_compile_ms    = 0.0; // Last compile and render, for scripts
static f32               studio_render_ms     = 0.0;
static u32               studio_known_struct  = 0; // Hash of the graph as compiled, undo or the node editor change it
static f64               studio_known_values  = 0.0;

#define STUDIO_THUMBS     48
#define STUDIO_THUMB_SIZE 128
static gpu_texture_t *studio_thumbs[STUDIO_THUMBS];
static gpu_texture_t *studio_thumb_none  = NULL;
static bool           studio_thumbs_made = false;

static void studio_redraw() {
	if (studio_back_handle != NULL) {
		studio_back_handle->redraws = 2;
	}
}

// Variables mode: a material that is not a studio graph shows the controls of its Variables (variable_node.c). Material
// Maker parameters wired to them are compiled in (variable_propagate), so a change compiles the preview again: at once
// when the finger lifts, at most every STUDIO_VAR_COMPILE_S while it drags. The preview starts with tiny Material Maker
// buffers and renders again as better ones land.
#define STUDIO_VAR_COMPILE_S 0.35
#define STUDIO_LAND_RENDER_S 0.25
static bool studio_vars             = false;
static f64  studio_known_vars       = 0.0;
static f64  studio_var_compile_time = 0.0;
static i32  studio_landed           = 0;
static f64  studio_land_render_time = 0.0;

static ui_node_canvas_t *studio_canvas() {
	if (studio_material == NULL) {
		return NULL;
	}
	ui_node_canvas_t *c = studio_material->canvas;
	return studio_graph_is(c) || (studio_vars && variable_count(c) > 0) ? c : NULL;
}

// The values of the Variables a change of compiles the preview in Variables mode (variable_compiles: they drive Material
// Maker parameters, or Shuffle). The others feed native nodes, which read them live: they only render again.
static f64 studio_hash_vars(ui_node_canvas_t *c) {
	f64 sum = 0.0;
	f64 w   = 1.0;
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		if (!variable_is(n) || !variable_compiles(c, n)) {
			continue;
		}
		f32 v[320];
		i32 interp = 0;
		i32 len    = variable_get(n, v, 320, &interp);
		for (i32 k = 0; k < len; ++k) {
			sum += v[k] * w;
			w += 0.731;
		}
		sum += interp * w;
		w += 0.731;
	}
	return sum;
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
	slot_material_refresh();
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
		render_path_add_target(names[i], STUDIO_PREVIEW_SIZE, STUDIO_PREVIEW_SIZE, formats[i], 1.0);
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
	f64         t0   = iron_time();
	material_t *sd   = ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
	i32         need = mm_buffers_require(MM_BUFFERS_TINY); // Material Maker buffers: a texture at once, better ones as they land
	parser_material_live_begin(studio_material->canvas, 0);
	node_shader_context_t *con = make_mesh_preview_run(sd, false);
	parser_material_live_end();
	mm_buffers_require(need);
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
	i32            swaps  = studio_swap_curvature(swap_keys, swap_saved);
	i32            need   = mm_buffers_require(MM_BUFFERS_TINY);
	i32            ddirty = g_context->ddirty;
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
	_render_path_last_w = sys_w(); // The preview size while material_preview is on: the viewport's targets are not resized to it
	_render_path_last_h = sys_h();
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
	mm_buffers_require(need);
	g_context->ddirty = studio_vars ? ddirty : 0; // A Material Maker buffer landing meanwhile still redraws the viewport
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
	studio_tiles_version++;
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
	if (layers_is_path_material()) {
		layers_update_path_layers();
	}
	fill_update();     // The material's fill tile again (the viewport, hidden behind the studio, does not render)
	fill_ensure_all(); // And the rasters of the layers it fills
	util_render_make_material_preview();
	g_context->material = _material;
	if (g_context->material != studio_material) {
		make_material_parse_paint_material(true);
	}
	ui_nodes_hwnd->redraws = 2;
}

static void studio_apply_paint_next(void *_) {
	studio_apply_paint();
}

// Commits and applies right away, for scripts checking the fill layers while the studio is open
void material_studio_apply() {
	if (studio_need_commit && !studio_native_drag && studio_canvas() != NULL) {
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
	if (studio_need_commit && !studio_native_drag) {
		studio_commit();
	}
	if (studio_vars) {
		f64 vh  = studio_hash_vars(c);
		f64 now = iron_time();
		if (vh != studio_known_vars && (!studio_native_drag || now - studio_var_compile_time > STUDIO_VAR_COMPILE_S)) {
			studio_known_vars       = vh;
			studio_var_compile_time = now;
			variable_propagate(c->nodes, c->links); // Into the parameters, then compiled
			studio_need_compile = true;
		}
		i32 landed = mm_buffers_landed_count();
		if (landed != studio_landed && now - studio_land_render_time > STUDIO_LAND_RENDER_S) {
			studio_landed           = landed;
			studio_land_render_time = now;
			studio_need_render      = true;
		}
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
	if (!studio_thumbs_made && !studio_vars) {
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
		studio_redraw();
	}
	if (!studio_vars) {
		studio_update_tiles(c, vs);
	}
}

// Studio materials, materials with Variables (controls) and Material Maker materials, which get the common controls
bool material_studio_can_open(slot_material_t *m) {
	return m != NULL && (studio_graph_is(m->canvas) || variable_count(m->canvas) > 0 || mm_controls_common_missing(m->canvas));
}

void material_studio_open(slot_material_t *m) {
	if (!material_studio_can_open(m)) {
		return;
	}
	if (mm_controls_common_missing(m->canvas)) { // Made before they existed: the same as at their defaults
		mm_controls_common(m);
		project_modified = true;
	}
	studio_vars = !studio_graph_is(m->canvas);
	if (studio_vars) {
		variable_propagate(m->canvas->nodes, m->canvas->links);
		studio_known_vars = studio_hash_vars(m->canvas);
		studio_landed     = mm_buffers_landed_count();
	}
	ui_menu_show         = false;
	studio_material      = m;
	material_studio_show = true;
	studio_closing       = false;
	studio_anim_start    = iron_time();
	studio_need_compile  = true;
	studio_need_commit   = false;
	studio_native_drag   = false;
	studio_undo_canvas   = NULL;
	studio_open_canvas   = util_clone_canvas(m->canvas);
	studio_sel           = 0;
	studio_stop          = 0;
	studio_known_struct  = studio_hash_struct(m->canvas);
	studio_known_values  = studio_hash_values(m->canvas);
	studio_layer_values  = -1.0;
	for (i32 k = 0; k < STUDIO_LAYERS; ++k) {
		studio_layer_key[k] = 0;
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
	}
	studio_redraw(); // The window keeps its last drawing, which may be another material's
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
	sys_notify_on_next_frame(&studio_apply_paint_next, NULL); // Close is tapped while the windows draw: render after
	material_studio_show = false;
	studio_closing       = true;
	studio_anim_start    = iron_time();
	base_redraw_ui();
}

// Closes the studio keeping the edits: the project's material is the edited one. The Library is not touched, the
// materials popover saves to it (app_material_save_to_library).
void material_studio_save() {
	material_studio_close(); // Commits, applies to the paint and renders the icon
}

// Closes the studio and puts the material back as it was when the studio opened. One undo step, Cancel can be undone.
void material_studio_cancel() {
	slot_material_t *m = studio_material;
	if (!material_studio_show || m == NULL) {
		return;
	}
	if (studio_open_canvas != NULL && (studio_hash_struct(m->canvas) != studio_hash_struct(studio_open_canvas) ||
	                                   studio_hash_values(m->canvas) != studio_hash_values(studio_open_canvas))) {
		history_edit_nodes(m->canvas, CANVAS_TYPE_MATERIAL, -1);
		m->canvas          = studio_open_canvas;
		studio_paint_stale = true; // The paint shader, the fill layers and the icon follow when it closes
	}
	studio_open_canvas = NULL;
	studio_need_commit = false; // Nothing to commit, the edits are gone
	studio_undo_canvas = NULL;
	material_studio_close();
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
	if (n != NULL && i >= 0 && i < n->inputs->length) {
		n->inputs->buffer[i]->default_value->buffer[0] = v;
	}
}

f32 material_studio_get_input(i32 k, i32 role, i32 i) {
	ui_node_canvas_t *c = g_context->material != NULL ? g_context->material->canvas : NULL;
	ui_node_t        *n = studio_graph_is(c) ? studio_graph_node(c, k, role) : NULL;
	return n != NULL && i >= 0 && i < n->inputs->length ? n->inputs->buffer[i]->default_value->buffer[0] : -1.0;
}

void material_studio_set_button(i32 k, i32 role, i32 i, f32 v) {
	ui_node_canvas_t *c = studio_script_canvas();
	ui_node_t        *n = c != NULL ? studio_graph_node(c, k, role) : NULL;
	if (n != NULL && i >= 0 && i < n->buttons->length) {
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
	if (v != NULL && i >= 0 && i * 5 + 2 < (i32)v->length) {
		v->buffer[i * 5 + 0] = r;
		v->buffer[i * 5 + 1] = g;
		v->buffer[i * 5 + 2] = b;
	}
}

// Stop indices of the ramp by position
#define STUDIO_RAMP_MAX 8

static i32 studio_ramp_sorted(f32_array_t *v, i32 *sorted) {
	i32 len = v->length / 5;
	len     = len > STUDIO_RAMP_MAX ? STUDIO_RAMP_MAX : len;
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

// The layer names are the names of their Material Mix nodes, shown in the node editor too
static char *studio_layer_name(ui_node_canvas_t *c, i32 k) {
	if (k == 0) {
		return tr("Base");
	}
	ui_node_t *mix = studio_graph_node(c, k, STUDIO_ROLE_MIX);
	return string_equals(mix->name, "Material Mix") ? tr("Layer") : mix->name;
}

static char *studio_pattern_label(ui_node_t *pat) {
	for (i32 i = 0; pat != NULL && i < pattern_node_count(); ++i) {
		if (string_equals(pat->type, pattern_node_type(i))) {
			return pattern_node_name(i);
		}
	}
	return tr("Plain");
}

static char *studio_layer_subtitle(ui_node_canvas_t *c, i32 k) {
	char *pattern = studio_pattern_label(studio_graph_node(c, k, STUDIO_ROLE_PATTERN));
	if (k == 0) {
		return pattern;
	}
	char *masks[STUDIO_MASK_COUNT] = {tr("Everywhere"), tr("Edges"), tr("Cavities"), tr("Facing Up"), tr("Pattern")};
	return string_tmp("%s, %s", pattern, masks[studio_graph_mask(c, k)]);
}

static void studio_open_nodes(void *_) {
	ui_base_show_material_nodes();
}

// Closes the studio, the node editor opens on the material
void material_studio_edit_nodes() {
	if (!material_studio_show) {
		return;
	}
	material_studio_close();
	sys_notify_on_next_frame(&studio_open_nodes, NULL);
}

// The background and the preview, in the rectangle the native editor leaves for it. A drag that starts on the preview
// turns it, pinch or wheel zooms.
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
	if (studio_canvas() == NULL) {
		return;
	}
	f32 k = studio_closing ? 1.0 - t : t; // Fades as the native editor does
	if (t < 1.0) {
		base_redraw_ui();
		iron_delay_idle_sleep();
	}
	if (material_studio_show && keyboard_started("escape")) {
		material_studio_close();
	}

	f32  vx   = studio_view[0];
	f32  vy   = studio_view[1];
	f32  vw   = studio_view[2];
	f32  vh   = studio_view[3];
	bool over = vw > 0 && mouse_x >= vx && mouse_x < vx + vw && mouse_y >= vy && mouse_y < vy + vh;
	if (material_studio_show) {
		if (mouse_started("left") && over) {
			studio_orbiting = true;
		}
		if (!mouse_down("left")) {
			studio_orbiting = false;
		}
		if (studio_orbiting && (mouse_movement_x != 0 || mouse_movement_y != 0)) {
			studio_yaw -= mouse_movement_x * 0.01;
			studio_pitch       = fmaxf(-1.2, fminf(1.2, studio_pitch + mouse_movement_y * 0.01));
			studio_need_render = true;
		}
		if (mouse_wheel_delta != 0 && over) {
			studio_dist        = fmaxf(1.0, fminf(4.0, studio_dist * (1.0 + mouse_wheel_delta * 0.08)));
			studio_need_render = true;
		}
	}

	studio_back_handle->redraws = 2;
	u32 window_bg               = g_theme->WINDOW_BG_COL;
	g_theme->WINDOW_BG_COL      = 0x00000000;
	if (ui_window(studio_back_handle, 0, 0, iron_window_width(), iron_window_height(), false)) {
		draw_set_color(STUDIO_BACKGROUND); // The native editor draws its controls over it
		draw_filled_rect(0, 0, iron_window_width(), iron_window_height());
		if (studio_preview != NULL && vw > 0 && vh > 0) { // The square render covers the view, cut at the sides
			f32 s = fmaxf(vw, vh);
			draw_set_color(0xffffffff);
			draw_scissor((i32)vx, (i32)vy, (i32)vw, (i32)vh);
			draw_scaled_image(studio_preview, vx + (vw - s) / 2.0, vy + (vh - s) / 2.0, s, s);
			draw_flush();
			gpu_disable_scissor();
		}
	}
	ui_window_composite_alpha = k;
	ui_end_window();
	ui_window_composite_alpha = 1.0;
	g_theme->WINDOW_BG_COL    = window_bg;

	if (studio_need_render || studio_need_compile) {
		iron_delay_idle_sleep();
	}
}

