#pragma once

#include "enums.h"
#include "types.h"
#include <iron.h>

// ---- Shared (written by several files) ----
char        *manifest_title           = "Material Studio";
char        *manifest_version_project = "33"; // Project, material, brush and swatches files: bumped when a saved field changes meaning,
                                              // with the step that reads the older meaning (import_arm_upgrade)
i32          manifest_version_oldest  = 31;   // Older files do not open
char        *manifest_version_config  = "20";
context_t   *g_context;
project_t   *g_project;
config_t    *g_config;
any_map_t   *g_keymap;
ui_t        *g_ui;
draw_font_t *g_font;
ui_theme_t  *g_theme;

// ---- io/import_arm.c ----
i32             import_arm_progress_unpacked = 0;    // Bytes of the .arm file's assets unpacked, for the progress bar
string_array_t *import_arm_material_files    = NULL; // Files of the materials being imported: same-named textures of other folders are skipped

// ---- io/import_lut.c ----
gpu_texture_t *lut_image = NULL;
i32            lut_size  = 0;

// ---- io/import_mesh.c ----
any_map_t *import_mesh_importers;

// ---- io/import_text.c ----
any_map_t *import_text_importers;

// ---- io/import_texture.c ----
any_map_t *import_texture_importers;

// ---- nodes_material/bake_texture_node.c ----
bool           bake_texture_node_baking   = false;
gpu_texture_t *bake_texture_node_texpaint = NULL;

// ---- render/make_material.c ----
shader_context_t *make_material_paint_scon  = NULL; // Texture space paint pass
shader_context_t *make_material_depth_scon  = NULL;
any_array_t      *make_material_mesh_scons  = NULL; // shader_context_t[], one per mesh layer pass
bool              make_material_height_used = false;
bool              make_material_emis_used   = false;
bool              make_material_subs_used   = false;

// ---- render/make_mesh.c ----
i32  make_mesh_layer_pass_count = 1;
bool make_mesh_bake             = false; // make_mesh_run makes the layer cache shader (layer_cache.c)

// ---- render/render_path_base.c ----
i32  render_path_base_bloom_current_mip = 0;
f32  render_path_base_bloom_sample_scale;
bool render_path_base_buf_swapped = false;

// ---- app_api.c ----
bool app_new_project_requested = false; // The engine asked for the native new project sheet

// ---- render/render_path_paint_cursor.c ----
bool render_path_paint_cursor_preview = false; // The brush circle in the middle of the view: the native radius slider is dragged

// ---- render/render_path_paint.c ----
u32           render_path_paint_stamp_seed_fixed  = 0; // Drawing pad: strokes painted again jitter alike, 0 seeds from the time
slot_layer_t *render_path_paint_live_layer        = NULL;
i32           render_path_paint_live_layer_drawn  = 0;
bool          render_path_paint_live_layer_locked = false;
bool          render_path_paint_push_undo_last;
bool          render_path_paint_baking    = false;
bool          render_path_paint_fill_pick = false;

// ---- render/render_path_raytrace.c ----
i32            render_path_raytrace_frame       = 0;
bool           render_path_raytrace_ready       = false;
bool           render_path_raytrace_init_shader = true;
f32_array_t   *render_path_raytrace_f32a;
mat4_t         render_path_raytrace_help_mat;
gpu_texture_t *render_path_raytrace_last_envmap   = NULL;
bool           render_path_raytrace_is_bake       = false;
bool           render_path_raytrace_override_pass = false;
bool           render_path_raytrace_moving        = false;

// ---- render/render_path_raytrace_bake.c ----
i32 render_path_raytrace_bake_rays_pix       = 0;
i32 render_path_raytrace_bake_rays_sec       = 0;
i32 render_path_raytrace_bake_current_sample = 0;

// ---- ui/box_export.c ----
i32              box_export_tab;
string_array_t  *box_export_files = NULL;
i32              box_export_mesh;
i32              box_export_preset_index;
export_preset_t *box_export_preset = NULL;
string_array_t  *box_export_channels;
string_array_t  *box_export_color_spaces;

// ---- ui/box_projects.c ----
i32   box_projects_tab_index;
char *box_projects_search = "";
bool  box_projects_native_shown = false; // iPad: the native projects screen is up (app_api.h)
bool  box_projects_import_pending = false; // The model picked next starts a new project (box_projects_import)

// ---- util/util_layer_ops.c ----
slot_layer_t *layers_export_layer;

// ---- ui/ui_base.c ----
i32  ui_base_viewport_col;

// ---- ui/ui_box.c ----
bool         ui_box_show = false;
ui_window_t *ui_box_hwnd;
bool         ui_box_click_to_hide = true;
i32          ui_box_modalw        = 400;
i32          ui_box_modalh        = 170;

// ---- ui/ui_files.c ----
char      *ui_files_filename;
char      *ui_files_path;
char      *ui_files_last_path = "";
char      *ui_files_save_folder = "Exports"; // iOS: the project's folder the next save puts its file in, then Exports again

// ---- ui/ui_id_mask.c ----
bool ui_id_mask_active       = false; // Picking the parts of an ID mask, see ui_id_mask.c
bool ui_id_mask_pick_pending = false; // A tap waits for the ID map color under it, see render_path_paint
bool ui_id_mask_pressed      = false; // The press already picked, one pick per tap

// ---- ui/ui_menu.c ----
bool ui_menu_show              = false;
bool ui_menu_nested            = false;
i32  ui_menu_x                 = 0;
i32  ui_menu_y                 = 0;
i32  ui_menu_h                 = 0;
bool ui_menu_keep_open         = false;
void (*ui_menu_commands)(void) = NULL;
bool ui_menu_show_first        = true;

// ---- ui/ui_nodes.c ----
any_map_t            *ui_nodes_custom_buttons;
bool                  ui_nodes_show = false;
i32                   ui_nodes_wx;
i32                   ui_nodes_wy;
i32                   ui_nodes_ww;
i32                   ui_nodes_wh;
canvas_type_t         ui_nodes_canvas_type       = CANVAS_TYPE_MATERIAL;
bool                  ui_nodes_grid_redraw       = true;
i32                   ui_nodes_grid_cell_w       = 200;
i32                   ui_nodes_grid_small_cell_w = 40;
ui_window_t          *ui_nodes_hwnd;
node_group_t_array_t *ui_nodes_group_stack;
f32                   ui_nodes_last_zoom = 1.0;
i32                   _ui_nodes_type; // Group socket edit box
char                 *_ui_nodes_name = "";
f32                   _ui_nodes_min;
f32                   _ui_nodes_max;
f32                   _ui_nodes_vals[4];

// ---- ui/ui_view2d.c ----
gpu_pipeline_t *ui_view2d_pipe;
i32             ui_view2d_channel_loc;
view_2d_type_t  ui_view2d_type = VIEW_2D_TYPE_LAYER;
bool            ui_view2d_show = false;
i32             ui_view2d_wx;
i32             ui_view2d_wy;
i32             ui_view2d_ww;
i32             ui_view2d_wh;
ui_window_t    *ui_view2d_hwnd;
f32             ui_view2d_pan_x      = 0.0;
f32             ui_view2d_pan_y      = 0.0;
f32             ui_view2d_pan_scale  = 1.0;
f32             ui_view2d_rotation   = 0.0; // Radians the layer is turned on screen, clockwise, see ui_view2d_rotation_allowed
bool            ui_view2d_twisting   = false;
bool            ui_view2d_tiled_show = false;

// ---- util/util_brush.c ----
f32  util_brush_sample_pressure = -1.0; // Pressure of the pen sample being drawn, -1 for the current pen pressure
f32  util_brush_sample_altitude = -1.0; // Angles of the pen sample being drawn, -1 for the current pen angles
f32  util_brush_sample_azimuth  = 0.0;
f32  util_brush_sample_roll     = 0.0;
bool util_brush_preview_pen     = false; // The brush preview paints as a pen, its samples set the pressure

// ---- util/util_layer.c ----
gpu_texture_t *layers_temp_image    = NULL;
gpu_texture_t *layers_expa          = NULL;
gpu_texture_t *layers_expb          = NULL;
gpu_texture_t *layers_expc          = NULL;
f32            layers_default_base  = 0.5;
bool           layers_crop          = false;
i32            layers_crop_x        = 0;
i32            layers_crop_y        = 0;
f32            layers_default_rough = 0.4;

// ---- util/util_mesh_maps.c ----
bool mesh_maps_stale = false; // The paint meshes changed after the registered mesh maps were baked

// ---- util/util_path.c ----
extern i32 path_point_dragging;
extern i32 path_layer_last_active;

// ---- util/util_render.c ----
i32   util_render_material_preview_size = 256;
f32   util_render_brush_wave_strip      = 0.25; // Wave preview height relative to its width
f32   util_render_brush_wave_radius     = 0.25; // Brush size of the wave preview, the brush's preview size
i32   util_render_node_preview_size     = 512;
i32   util_render_decal_preview_size    = 512;
i32   util_render_layer_preview_size    = 200;
i32   util_render_font_preview_size     = 200;
char *util_render_default_envmap_name   = "World_default";
i32   util_render_default_envmap_w      = 512;

// ---- util/util_uv.c ----
gpu_texture_t *util_uv_uvmap              = NULL;
bool           util_uv_uvmap_cached       = false;
gpu_texture_t *util_uv_dilatemap          = NULL;
bool           util_uv_dilatemap_cached   = false;
bool           util_uv_seammap_cached     = false;

// ---- util/util_uv_unwrap.c ----
extern float util_uv_unwrap_margin;

// ---- args.c ----
bool args_background = false;

// ---- base.c ----
bool             base_ui_enabled        = true;
bool             base_view3d_show       = true;
i32              base_appx              = 0;
i32              base_appy              = 0;
bool             base_workspace_pending = false;
i32              base_workspace_preset  = WORKSPACE_PRESET_NONE;
i32              base_default_element_w = 100;
i32              base_default_element_h = 28;
i32              base_default_font_size = 13;
i32              base_res;
f32              base_res_x;
f32              base_res_y;
i32              base_bits;
string_array_t  *base_drop_paths;

// ---- camera.c ----
vec4_t camera_origins[2];
mat4_t camera_views[2];

// ---- config.c ----
ui_align_t config_button_align   = UI_ALIGN_LEFT;
char      *config_button_spacing = "       ";

// ---- brush_previews.c ----
i32 brush_previews_group = -1; // Group shown in the brush list, its strokes render first

// ---- console.c ----
string_array_t *console_last_traces;
char           *console_capture = NULL;

// ---- project.c ----
u32   project_uid_next = 1;    // Stable uids of layers and materials, saved with the project, never reused in it
char *project_uuid     = NULL; // 32 hex digits, project_uuid_get

// ---- history.c ----
history_step_t_array_t *history_steps;
string_array_t         *history_tool_names; // Tool names shown in the history
i32                     history_undos     = 0;     // Undos available
i32                     history_redos     = 0;     // Redos available
u32                     history_notice_serial = 0; // Bumped by every undo or redo the user asked for (history_user_undo)
bool                    history_notice_redo   = false;
char                   *history_notice_name   = "";
bool                    history_push_undo = false; // Store undo on next paint

// ---- nodes_brush.c ----
ui_node_t_array_t   *nodes_brush_category0;
string_array_t      *nodes_brush_categories;
node_list_t_array_t *nodes_brush_list;
any_map_t           *nodes_brush_creates;

// ---- nodes_material.c ----
string_array_t      *nodes_material_categories;
ui_node_t_array_t   *nodes_material_input;
ui_node_t_array_t   *nodes_material_texture;
ui_node_t_array_t   *nodes_material_color;
ui_node_t_array_t   *nodes_material_utilities;
ui_node_t_array_t   *nodes_material_group;
node_list_t_array_t *nodes_material_list = NULL;

// ---- parser_logic.c ----
any_map_t *parser_logic_custom_nodes;

// ---- parser_material.c ----
node_shader_context_t    *parser_material_con;
node_shader_t            *parser_material_shader;
string_array_t           *parser_material_parsed;
ui_node_t_array_t        *parser_material_parents;
ui_node_canvas_t_array_t *parser_material_canvases;
ui_node_t_array_t        *parser_material_nodes;
ui_node_link_t_array_t   *parser_material_links;
f32                       parser_material_eps = 0.000001;
any_map_t                *parser_material_node_values;
any_map_t                *parser_material_node_vectors;
any_map_t                *parser_material_custom_nodes;
any_map_t                *parser_material_node_outputs; // Node type to char *(*)(ui_node_t *, ui_node_socket_t *), result in the socket's own type
bool                      parser_material_parse_height       = false;
bool                      parser_material_parse_emission     = false;
bool                      parser_material_parse_subsurface   = false;
bool                      parser_material_parsing_basecolor  = false;
bool                      parser_material_triplanar          = false; // Sample using tex_coord/1/2 & tex_coord_blend
bool                      parser_material_sample_keep_aspect = false; // Adjust uvs to preserve texture aspect ratio

#ifdef WITH_MIPMAPS
bool parser_material_straight_cutouts = false;
#endif

char              *parser_material_sample_uv_scale       = "1.0";
bool               parser_material_transform_color_space = true;
bool               parser_material_blur_passthrough      = false;
bool               parser_material_warp_passthrough      = false;
ui_node_canvas_t  *parser_material_start_group           = NULL;
ui_node_t_array_t *parser_material_start_parents         = NULL;
ui_node_t         *parser_material_start_node            = NULL;
char              *parser_material_out_normaltan; // Raw tangent space normal parsed from normal map
bool               parser_material_is_frag = true;

// ---- pipes.c ----
gpu_pipeline_t *pipes_copy;
gpu_pipeline_t *pipes_copy8;
gpu_pipeline_t *pipes_copy64;
gpu_pipeline_t *pipes_copy128;
gpu_pipeline_t *pipes_copy_bgra;
gpu_pipeline_t *pipes_copy_rgb = NULL;
gpu_pipeline_t *pipes_merge    = NULL;
gpu_pipeline_t *pipes_merge_r  = NULL;
gpu_pipeline_t *pipes_merge_g  = NULL;
gpu_pipeline_t *pipes_merge_b  = NULL;
gpu_pipeline_t *pipes_merge_a  = NULL;
gpu_pipeline_t *pipes_merge_mask;
gpu_pipeline_t *pipes_invert_mask;
gpu_pipeline_t *pipes_apply_mask;
gpu_pipeline_t *pipes_colorid_to_mask;
i32             pipes_tex0;
i32             pipes_tex1;
i32             pipes_texmask;
i32             pipes_texa;
i32             pipes_opac;
i32             pipes_blending;
i32             pipes_tex1w;
i32             pipes_raise;
i32             pipes_tex0_mask;
i32             pipes_texa_mask;
i32             pipes_opac_apply_mask;
i32             pipes_tex0_merge_mask;
i32             pipes_texa_merge_mask;
i32             pipes_tex_colorid;
i32             pipes_texpaint_colorid;
i32             pipes_size_colorid;
i32             pipes_opac_merge_mask;
i32             pipes_blending_merge_mask;
gpu_texture_t  *pipes_temp_mask_image = NULL;
gpu_pipeline_t *pipes_mask_stack; // One mask stack effect, see util_mask_stack.c
i32             pipes_texa_mask_stack;
i32             pipes_texsrc_mask_stack;
i32             pipes_type_mask_stack;
i32             pipes_blending_mask_stack;
i32             pipes_opac_mask_stack;
i32             pipes_params0_mask_stack;
i32             pipes_params1_mask_stack;
i32             pipes_params2_mask_stack;
i32             pipes_params3_mask_stack;
i32             pipes_src_size_mask_stack;
gpu_pipeline_t *pipes_cursor;
i32             pipes_cursor_vp;
i32             pipes_cursor_inv_vp;
i32             pipes_cursor_mouse;
i32             pipes_cursor_tex_step;
i32             pipes_cursor_radius;
i32             pipes_cursor_camera_right;
i32             pipes_cursor_tint;
i32             pipes_cursor_camera_up;
i32             pipes_cursor_camera_align;
i32             pipes_cursor_gbufferd;
gpu_pipeline_t *pipes_cursor_decal;
i32             pipes_cursor_decal_vp;
i32             pipes_cursor_decal_inv_vp;
i32             pipes_cursor_decal_mouse;
i32             pipes_cursor_decal_tex_step;
i32             pipes_cursor_decal_radius;
i32             pipes_cursor_decal_camera_right;
i32             pipes_cursor_decal_opacity;
i32             pipes_cursor_decal_angle;
i32             pipes_cursor_decal_scale_x;
i32             pipes_cursor_decal_camera_up;
i32             pipes_cursor_decal_camera_align;
i32             pipes_cursor_decal_gbufferd;
i32             pipes_cursor_decal_texdecal;
i32             pipes_cursor_decal_gbuffer0;
gpu_pipeline_t *pipes_cursor_2d;
i32             pipes_cursor_2d_center;
i32             pipes_cursor_2d_size;
i32             pipes_cursor_2d_radius;
i32             pipes_cursor_2d_width;
gpu_pipeline_t *pipes_outline;
i32             pipes_outline_color;
i32             pipes_outline_tex_step;
i32             pipes_outline_tex;
i32             pipes_offset;

// ---- project.c ----
string_array_t *project_default_mesh_list = NULL;
char           *project_new_template      = NULL;  // Template mesh of the next new project when set, see script_project_new_template()
bool            project_modified          = false; // Changed since the last save, for auto save

// ---- resource.c ----
any_map_t *resource_bundled;

// ---- translator.c ----
any_map_t *translator_translations;

// ---- uniforms.c ----

// ---- viewport.c ----

// ---- paint_exec.c ----
vec4_t paint_exec_uv_tile  = {0.0, 0.0, 1.0, 1.0}; // _paint_uv_tile: the part of the layer painted (u0 v0 and scale), 0 0 1 1 live
vec2_t paint_exec_res0     = {0.0, 0.0};           // _paint_res0: resolution of the layer when recorded, 0 0 the layer's own
bool   make_paint_resolved = false;                // The paint shader being generated reads resolved dabs (paint_exec.c)
bool   paint_exec_replay   = false;                // The executor flushes recorded dabs and ops (paint_replay.c)
bool   paint_replay_uncached = false; // Paint shaders parsed without the material cache (a replay at another size)
char  *paint_replay_depth    = NULL;  // Depth target replay draws read, from the recorded camera
char  *paint_replay_uv       = NULL;  // and its uv map
gpu_texture_t *paint_replay_dilatemap = NULL; // UV islands of the replay's target
