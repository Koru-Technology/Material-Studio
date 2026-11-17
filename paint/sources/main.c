
#include "global.h"

#ifdef IRON_MACOS
#define WITH_NEURAL // Neural nodes: they run downloaded desktop binaries (nodes_neural/neural_node.c)
#endif

#include "material_maker/mm_core.c"

#include "io/export_arm.c"
#include "io/export_exr.c"
#include "io/export_mesh.c"
#include "io/export_obj.c"
#include "io/export_texture.c"
#include "io/import_arm_assets.c"
#include "io/import_arm.c"
#include "io/import_arm_library.c"
#include "io/import_arm_mesh.c"
#include "io/import_asset.c"
#include "io/import_blend_mesh.c"
#include "io/import_envmap.c"
#include "io/import_folder.c"
#include "io/import_font.c"
#include "io/import_json_material.c"
#include "io/import_keymap.c"
#include "io/import_lut.c"
#include "io/import_mesh.c"
#include "io/import_obj.c"
#include "io/import_stl.c"
#include "io/import_text.c"
#include "io/import_texture.c"
#include "io/import_theme.c"
#include "io/project_container.c"

#include "nodes_brush/boolean_node.c"
#include "nodes_brush/brush_output_node.c"
#include "nodes_brush/color_node.c"
#include "nodes_brush/float_node.c"
#include "nodes_brush/input_node.c"
#include "nodes_brush/integer_node.c"
#include "nodes_brush/math_node.c"
#include "nodes_brush/null_node.c"
#include "nodes_brush/random_node.c"
#include "nodes_brush/separate_vector_node.c"
#include "nodes_brush/string_node.c"
#include "nodes_brush/tex_image_node.c"
#include "nodes_brush/time_node.c"
#include "nodes_brush/vector_math_node.c"
#include "nodes_brush/vector_node.c"

#include "nodes_material/attribute_node.c"
#include "nodes_material/bake_texture_node.c"
#include "nodes_material/blur_node.c"
#include "nodes_material/brick_texture_node.c"
#include "nodes_material/brightness_contrast_node.c"
#include "nodes_material/bump_node.c"
#include "nodes_material/camera_texture_node.c"
#include "nodes_material/checker_texture_node.c"
#include "nodes_material/clamp_node.c"
#include "nodes_material/color_mask_node.c"
#include "nodes_material/color_ramp_node.c"
#include "nodes_material/combine_color_node.c"
#include "nodes_material/combine_xyz_node.c"
#include "nodes_material/curvature_bake_node.c"
#include "nodes_material/float_curve_node.c"
#include "nodes_material/gabor_texture_node.c"
#include "nodes_material/gamma_node.c"
#include "nodes_material/geometry_node.c"
#include "nodes_material/gradient_texture_node.c"
#include "nodes_material/group_node.c"
#include "nodes_material/hue_saturation_value_node.c"
#include "nodes_material/image_texture_node.c"
#include "nodes_material/invert_color_node.c"
#include "nodes_material/layer_mask_node.c"
#include "nodes_material/layer_node.c"
#include "nodes_material/magic_texture_node.c"
#include "nodes_material/map_range_node.c"
#include "nodes_material/mask_nodes.c"
#include "nodes_material/material_mix_node.c"
#include "nodes_material/mapping_node.c"
#include "nodes_material/material_node.c"
#include "nodes_material/math2_node.c"
#include "nodes_material/mm_nodes.c"
#include "nodes_material/mm_nodes_params.c"
#include "nodes_material/mm_nodes_sync.c"
#include "nodes_material/mm_nodes_buttons.c"
#include "nodes_material/mm_nodes_register.c"
#include "nodes_material/mm_material_node.c"
#include "nodes_material/mm_import.c"
#include "nodes_material/mm_library.c"
#include "nodes_material/mm_controls.c"
#include "nodes_material/mm_bridge.c"
#include "nodes_material/mm_buffers.c"
#include "nodes_material/mm_buffers_pipes.c"
#include "nodes_material/mm_buffers_render.c"
#include "nodes_material/mm_buffers_test.c"
#include "nodes_material/mix_color_node.c"
#include "nodes_material/mix_normal_map_node.c"
#include "nodes_material/noise_texture_node.c"
#include "nodes_material/normal_map_node.c"
#include "nodes_material/normal_node.c"
#include "nodes_material/object_info_node.c"
#include "nodes_material/pattern_nodes.c"
#include "nodes_material/picker_node.c"
#include "nodes_material/quantize_node.c"
#include "nodes_material/replace_color_node.c"
#include "nodes_material/rgb_curves_node.c"
#include "nodes_material/rgb_node.c"
#include "nodes_material/rgb_to_bw_node.c"
#include "nodes_material/separate_color_node.c"
#include "nodes_material/separate_xyz_node.c"
#include "nodes_material/studio_graph.c"
#include "nodes_material/text_texture_node.c"
#include "nodes_material/texture_coordinate_node.c"
#include "nodes_material/tilesheet_node.c"
#include "nodes_material/uv_map_node.c"
#include "nodes_material/value_node.c"
#include "nodes_material/variable_node.c"
#include "nodes_material/vector_curves_node.c"
#include "nodes_material/vector_math2_node.c"
#include "nodes_material/vector_rotate_node.c"
#include "nodes_material/vector_transform_node.c"
#include "nodes_material/voronoi_texture_node.c"
#include "nodes_material/warp_node.c"
#include "nodes_material/wave_texture_node.c"
#include "nodes_material/wireframe_node.c"


#ifdef WITH_NEURAL
#include "nodes_neural/edit_image_node.c"
#include "nodes_neural/image_to_pbr_node.c"
#include "nodes_neural/neural_node.c"
#include "nodes_neural/neural_node_models.c"
#include "nodes_neural/repeat_node.c"
#include "nodes_neural/save_image_node.c"
#include "nodes_neural/text_to_image_node.c"
#include "nodes_neural/upscale_image_node.c"
#endif

#include "render/make_bake.c"
#include "render/make_blur.c"
#include "render/make_brush.c"
#include "render/make_clone.c"
#include "render/make_curvature.c"
#include "render/make_depth.c"
#include "render/make_discard.c"
#include "render/make_material.c"
#include "render/make_mesh.c"
#include "render/make_mesh_preview.c"
#include "render/make_node_preview.c"
#include "render/make_paint.c"
#include "render/make_particle.c"
#include "render/make_picking.c"
#include "render/make_texcoord.c"
#include "render/render_envsphere.c"
#include "render/render_gizmo.c"
#include "render/render_grid.c"
#include "render/render_pass.c"
#include "render/render_path_base.c"
#include "render/render_path_lit.c"
#include "render/paint_cull.c"
#include "render/render_path_paint.c"
#include "render/render_path_paint_picker.c"
#include "render/render_path_paint_cursor.c"
#include "render/render_path_paint_stroke.c"
#include "render/render_path_paint_2d.c"
#include "render/render_path_preview.c"
#include "render/render_path_raytrace.c"
#include "render/render_path_raytrace_bake.c"
#include "render/render_path_unlit.c"
#include "render/render_pathsphere.c"

#include "slots/slot_brush.c"
#include "slots/slot_font.c"
#include "slots/slot_layer.c"
#include "slots/slot_material.c"

#include "ui/box_bake_maps.c"
#include "ui/box_export.c"
#include "ui/box_import_mesh.c"
#include "ui/box_new_project.c"
#include "ui/box_preferences.c"
#include "ui/box_projects.c"
#include "ui/ui_base.c"
#include "ui/ui_box.c"
#include "ui/ui_dock.c"
#include "ui/ui_files.c"
#include "ui/ui_id_mask.c"
#include "ui/ui_menu.c"
#include "history_tiles.c"
#include "ui/ui_quick_menu.c"
#include "ui/ui_nodes.c"
#include "ui/ui_brush_creator.c"
#include "ui/ui_material_studio.c"
#include "ui/ui_search.c"
#include "ui/ui_view2d.c"

#include "util/util_brush.c"
#include "util/util_clone.c"
#include "util/util_cursor.c"
#include "util/util_encode.c"
#include "util/util_env.c"
#include "util/util_layer.c"
#include "util/util_layer_create.c"
#include "util/util_layer_ops.c"
#include "util/util_layer_fill.c"
#include "util/util_layer_merge.c"
#include "util/util_mask_stack.c"
#include "util/util_mask_generators.c"
#include "util/util_mesh.c"
#include "util/util_mesh_maps.c"
#include "util/util_nodes.c"
#include "util/util_particle.c"
#include "util/util_path.c"
#include "util/util_primitives.c"
#include "util/util_raycast.c"
#include "util/util_render.c"
#include "util/util_shortcut.c"
#include "util/util_stencil.c"
#include "util/util_stroke_refine.c"
#include "util/util_material_cache.c"
#include "util/util_texture.c"
#include "util/util_touch.c"
#include "util/util_pencil.c"
#include "util/util_ui.c"
#include "util/util_uv.c"
#include "util/util_uv_unwrap.c"

#include "args.c"
#include "base.c"
#include "camera.c"
#include "config.c"
#include "library.c"
#include "builtin_brushes.c"
#include "brush_library.c"
#include "brush_pad.c"
#include "smart_materials.c"
#include "saved_materials.c"
#include "brush_previews.c"
#include "console.c"
#include "context.c"
#include "history.c"
#include "history_swap.c"
#include "history_steps.c"
#include "history_store.c"
#include "timeline.c"
#include "timeline_ops.c"
#include "timeline_tiles.c"
#include "timeline_io.c"
#include "history_io.c"
#include "keymap.c"
#include "logic_node.c"
#include "minic_impl.c"
#include "app_api.c"
#include "app_api_brushes.c"
#include "app_api_layers.c"
#include "app_api_masks.c"
#include "app_api_materials.c"
#include "app_api_test.c"
#include "node_shader.c"
#include "nodes_brush.c"
#include "nodes_material.c"
#include "parser_logic.c"
#include "parser_material.c"
#include "pipes.c"
#include "project.c"
#include "resource.c"
#include "strings.c"
#include "translator.c"
#include "uniforms.c"
#include "viewport.c"
#include "paint_exec.c"
#include "paint_rec.c"
#include "paint_mat_snapshot.c"
#include "paint_refine.c"
#include "paint_replay.c"
#include "render/make_dab_resolve.c"
#include "util/util_test.c"
#include "io/export_tiled.c"
#include "app_api_export.c"
#include "util/util_layer_resolution.c"

#ifdef WITH_PLUGINS
void plugins_init();
#endif

void _kickstart() {
	_render_path_cached_shader_contexts = any_map_create();
	ui_nodes_custom_buttons             = any_map_create();

	box_export_channels = any_array_create_from_raw(
	    (void *[]){
	        "base_r", "base_g", "base_b", "height", "metal",  "nor_r",  "nor_g",  "nor_g_directx", "nor_b",  "occ", "opac", "rough",
	        "smooth", "emis",   "subs",   "diff_r", "diff_g", "diff_b", "spec_r", "spec_g",        "spec_b", "0.0", "1.0",
	    },
	    23);

	box_export_color_spaces = any_array_create_from_raw(
	    (void *[]){
	        "linear",
	        "srgb",
	    },
	    2);

	import_texture_importers = any_map_create();
	import_text_importers    = any_map_create();
	base_drop_paths          = any_array_create_from_raw((void *[]){}, 0);
	history_tool_names = any_array_create_from_raw(
	    (void *[]){
	        _tr("Brush"),   _tr("Eraser"), _tr("Fill"),     _tr("Decal"),   _tr("Text"),   _tr("Clone"),   _tr("Blur"),  _tr("Particle"),
	        _tr("ID"),      _tr("Picker"), _tr("Preview"),  _tr("Move"),    _tr("Select"), _tr("Empty"),   _tr("Plane"), _tr("Cube"),
	        _tr("Sphere"),  _tr("Cone"),   _tr("Cylinder"), _tr("Capsule"), _tr("Torus"),  _tr("3D Text"), _tr("Path"),  _tr("Curve"),
	        _tr("Bake"),
	        _tr("Inpaint"),
	    },
	    26);

	uniforms_ext_ortho_p    = mat4_ortho(-0.5, 0.5, -0.5, 0.5, -0.5, 0.5);
	import_mesh_importers   = any_map_create();
	translator_translations = any_map_create();

	g_project                     = ALLOC_INIT(project_t, {0});
	g_project->_                  = calloc(1, sizeof(project_runtime_t));
	g_project->_->filepath        = "";
	g_project->_->assets          = any_array_create_from_raw((void *[]){}, 0);
	g_project->mesh_assets        = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->material_groups = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->materials       = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->brushes         = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->layers          = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->fonts           = any_array_create_from_raw((void *[]){}, 0);

	ui_view2d_hwnd               = ui_window_create();
	parser_material_node_values  = any_map_create();
	parser_material_node_vectors = any_map_create();
	parser_material_custom_nodes = any_map_create();
	parser_material_node_outputs = any_map_create();
	parser_logic_custom_nodes    = any_map_create();
	resource_bundled             = any_map_create();
	ui_nodes_hwnd                = ui_window_create();
	ui_nodes_group_stack         = any_array_create_from_raw((void *[]){}, 0);

	nodes_brush_categories = any_array_create_from_raw(
	    (void *[]){
	        _tr("Nodes"),
	    },
	    1);

	nodes_material_categories = nodes_material_category_names(); // In the order of nodes_material_list

	console_last_traces           = any_array_create(0);
	ui_box_hwnd                   = ui_window_create();
	render_path_raytrace_f32a     = f32_array_create(24);
	render_path_raytrace_help_mat = mat4_identity();
	neural_node_results           = any_imap_create();

	sys_on_resize = base_on_resize;
	sys_on_w      = base_w;
	sys_on_h      = base_h;
	sys_on_x      = base_x;
	sys_on_y      = base_y;

	iron_set_app_name(manifest_title); // Used to locate external application data folder
	config_load();
	config_init();
	context_init();
	sys_start(config_get_options());

#ifdef is_debug
	double t_start = iron_time();
#endif

	iron_set_app_name(manifest_title);

	data_cached_scene_raws = any_map_create();
	any_map_set(data_cached_scene_raws, "Scene", startup_get_scene());
	scene_set_active("Scene");
	util_render_make_default_world();

	brush_library_apply(brush_library_selected()); // Stroke, taper, shape and pen settings of the brush picked last session
	uniforms_ext_init();
	render_path_base_init(); // Allocate gbuffer
	render_path_commands = render_path_lit_commands;

	if (!string_equals(g_config->lut_path, "") && iron_file_exists(g_config->lut_path)) {
		import_lut_run(g_config->lut_path);
	}

#ifdef WITH_PLUGINS
	plugins_init();
#endif
	import_stl_init();

	base_init();
	app_set_ready();

#ifdef is_debug
	iron_log("Started in %fs\n", iron_time() - t_start);
#endif

	iron_start();
}
