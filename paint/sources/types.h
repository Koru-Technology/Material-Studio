
#pragma once
#include "iron_physics.h"

#include "enums.h"
#include "minic.h"
#include "minic_tcc.h"

typedef struct slot_brush {
	struct ui_nodes       *nodes;
	struct ui_node_canvas *canvas;
	struct gpu_texture    *image; // 200px
	struct gpu_texture    *image_icon;
	struct gpu_texture    *image_wave; // Stroke preview, generated at runtime
	bool                   preview_ready;
	i32                    id;
} slot_brush_t;

// Settings of a brush, edited in the Brush Creator. Picking a brush puts them in the context (brush_library_apply).
typedef struct brush_settings {
	// Stroke path
	f32 spacing;        // Distance between stamps in brush diameters, 0 for a continuous stroke
	f32 spacing_jitter; // Random change of each step, 0-1
	f32 jitter_lateral; // Random offset of each stamp across the stroke, in brush diameters
	f32 jitter_linear;  // Random offset of each stamp along the stroke, in brush diameters
	f32 falloff;        // The stroke fades out along its length, 0 never
	f32 flow;           // Paint added by each stamp, builds up to the opacity
	// Stabilization
	f32 streamline;    // The brush trails the pen, smoothing out wobbles
	f32 stabilization; // Averages the pen positions over time, steadies slow strokes
	// Taper, lengths 0-1
	f32 taper_start; // Pencil strokes
	f32 taper_end;
	f32 taper_size;    // How thin the tapered ends get
	f32 taper_opacity; // How transparent
	f32 touch_taper_start; // Finger and mouse strokes
	f32 touch_taper_end;
	// Shape
	i32 tip; // brush_tip_t
	f32 hardness;
	i32 rotation; // brush_rotation_t
	f32 angle;    // Degrees
	f32 jitter_angle;
	f32 jitter_size;
	f32 jitter_opacity;
	f32 roundness; // 1 round, smaller squashes the tip
	// Apple Pencil, how much the pen drives each property 0-1
	f32  pressure_size;
	f32  pressure_opacity;
	f32  pressure_flow;
	f32  pressure_hardness;
	f32  tilt_size;
	f32  tilt_opacity;
	bool tilt_shading; // The dab stretches into an ellipse as the pen leans
	// Preview
	f32 preview_size; // Size of the stroke in the brush list and on the drawing pad, 0-1
} brush_settings_t;

// The f32 settings by key: the library file reads keys added later as their default, scripts set them by name
#define BRUSH_LIBRARY_SETTINGS(X)                                                                                                                      \
	X(spacing)                                                                                                                                         \
	X(spacing_jitter)                                                                                                                                  \
	X(jitter_lateral)                                                                                                                                  \
	X(jitter_linear)                                                                                                                                   \
	X(falloff)                                                                                                                                         \
	X(flow)                                                                                                                                            \
	X(streamline)                                                                                                                                      \
	X(stabilization)                                                                                                                                   \
	X(taper_start)                                                                                                                                     \
	X(taper_end)                                                                                                                                       \
	X(taper_size)                                                                                                                                      \
	X(taper_opacity)                                                                                                                                   \
	X(touch_taper_start)                                                                                                                               \
	X(touch_taper_end)                                                                                                                                 \
	X(hardness)                                                                                                                                        \
	X(angle)                                                                                                                                           \
	X(jitter_angle)                                                                                                                                    \
	X(jitter_size)                                                                                                                                     \
	X(jitter_opacity)                                                                                                                                  \
	X(roundness)                                                                                                                                       \
	X(pressure_size)                                                                                                                                   \
	X(pressure_opacity)                                                                                                                                \
	X(pressure_flow)                                                                                                                                   \
	X(pressure_hardness)                                                                                                                               \
	X(tilt_size)                                                                                                                                       \
	X(tilt_opacity)                                                                                                                                    \
	X(preview_size)

// A brush of the Brushes popover: built-in, or made in the Brush Creator and kept in the brush library
typedef struct brush_def {
	i32                 id;
	char               *name;
	i32                 group; // Id of its brush_group_t
	bool                builtin;
	brush_settings_t    s;
	char               *tip_file;  // Imported tip, a png in the library folder, NULL without
	struct gpu_texture *tip_image; // Loaded imported tip
	struct gpu_texture *image_row; // Its stroke in the brush list, read from disk or rendered, see brush_previews.c
	bool                row_rt;    // image_row is a render target, rendered here
	i32                 row_state; // brush_preview_state_t of row_key
	u64                 row_key;   // Hash of what shapes its stroke, names the file of the stroke
	u64                 tip_hash;  // Hash of the imported tip file, 0 until read
} brush_def_t;

typedef struct brush_group {
	i32   id;
	char *name;
	i32   icon;
	bool  builtin;
} brush_group_t;

// What util_render_brush_scene_begin changes to paint brush strokes on a plane facing the camera, put back by util_render_brush_scene_end
typedef struct util_render_brush_scene {
	struct gpu_texture    *current;
	bool                   in_use;
	struct slot_material  *material;
	i32                    tool;
	struct slot_layer     *layer;
	struct slot_material  *fill_material;
	struct mesh_object    *paint_object;
	struct mesh_object    *plane;
	struct u8_array       *visibles;
	bool                   merged_visible;
	f32                    fov;
	f32                    radius;
	f32                    opacity;
	f32                    hardness;
	f32                    x;
	f32                    y;
	f32                    last_x;
	f32                    last_y;
	i32                    pdirty;
	f32                    posx;
	f32                    posy;
	f32                    posz;
	bool                   sym_x;
	bool                   sym_y;
	bool                   sym_z;
	i32                    sym_radial;
	f32                    speed;
	f32                    stroke_angle;
	f64                    brush_time;
} util_render_brush_scene_t;

// Streamline and stabilization state of a stroke, see render_path_paint_filter_apply
typedef struct render_path_paint_filter {
	bool started;
	f32  x; // Streamline
	f32  y;
	f32  x2; // Stabilization
	f32  y2;
} render_path_paint_filter_t;

typedef struct mesh_nodes {
	i32                    uid; // Object
	struct ui_nodes       *nodes;
	struct ui_node_canvas *canvas;
	struct mesh_data      *input;  // Original mesh
	struct mesh_data      *output; // Mesh assigned to the object, equals input unless the nodes change it
	u32                    hash;
	bool                   dirty;       // Input replaced, rebuilt once the mouse is up
	bool                   reads_frame; // A script read the timeline frame, rebuilt when it changes
	i32                    frame;       // Timeline frame of the last build
} mesh_nodes_t;

typedef struct mesh_geo {
	f32 *pos;
	f32 *nor;
	f32 *tex;
	u32 *ind;
	i32  vertex_count;
	i32  index_count;
	i32  vertex_capacity; // Allocated sizes, counts can be lowered
	i32  index_capacity;
} mesh_geo_t;

typedef struct mesh_geo_image {
	i32  width;
	i32  height;
	u8  *pixels;  // As stored in the texture, rows from v 0, read with mesh_geo_image_sample
	i32  format;  // gpu_texture_format_t of the pixels
	bool bgra;    // 8 bit rgba pixels in bgra order
	i32  channel; // 0 - 3 reads that channel in all four, -1 rgba
} mesh_geo_image_t;

typedef struct slot_layer {
	i32                   id;
	char                 *name;
	char                 *ext;
	bool                  visible;
	struct slot_layer    *parent;   // Group (for layers) or layer (for masks and filters)
	struct gpu_texture   *texpaint; // Base or mask
	struct gpu_texture   *texpaint_nor;
	struct gpu_texture   *texpaint_pack;
	struct gpu_texture   *texpaint_preview; // Layer preview
	f32                   mask_opacity;     // Opacity mask
	struct slot_material *fill_material;
	bool                  show_panel;
	blend_type_t          blending;
	i32                   object_mask;
	f32                   scale;
	f32                   angle;
	uv_type_t             uv_type;
	i32                   uv_map;
	bool                  paint_base;
	bool                  paint_opac;
	bool                  paint_occ;
	bool                  paint_rough;
	bool                  paint_met;
	bool                  paint_nor;
	bool                  paint_nor_blend;
	bool                  paint_height;
	bool                  paint_height_blend;
	bool                  paint_emis;
	bool                  paint_subs;
	mat4_t                decal_mat; // Decal layer
	f32_array_t          *path_points;        // uv-space [x, y, ..]
	f32_array_t          *path_points_world;  // world-space [x, y, z, ..]
	f32_array_t          *path_points_camera; // [loc.xyzw, rot.xyzw, aspect, ..] at time of point placement
	i32_array_t          *path_points_parent;
	i32                   path_tool;
	bool                  path_curved;
	bool                  path_text;
	struct slot_material *path_material;
	bool                  height_blend;           // Mask: the edge follows the height of its layer (fur strands over the layers below)
	bool                  height_blend_below;     // Compare with the height of the layers below, otherwise with a flat level
	f32                   height_blend_influence; // How far the height moves the mask edge
	f32                   height_blend_sharpness;
	f32                   height_blend_offset;
	f32                   height_blend_reach; // Distance the mask edge is widened over (uv), strands reach that far with a hard brush
} slot_layer_t;

typedef struct slot_font {
	struct gpu_texture *image; // 200px
	bool                preview_ready;
	i32                 id;
	struct draw_font   *font;
	char               *name;
	char               *file;
} slot_font_t;

typedef struct config {
	char *version;
	char *sha; // Commit id
	// The locale should be specified in ISO 639-1 format:
	// https://en.wikipedia.org/wiki/List_of_ISO_639-1_codes
	// "system" is a special case that will use the system locale
	char *locale;
	// Window
	i32  window_mode; // window, fullscreen
	i32  window_w;
	i32  window_h;
	i32  window_x;
	i32  window_y;
	bool window_resizable;
	bool window_maximizable;
	bool window_minimizable;
	bool window_vsync;
	i32  window_frequency;
	f32  window_scale;
	// Render path
	f32   rp_supersample;
	f32   rp_ssao;
	f32   rp_bloom;
	f32   rp_vignette;
	f32   rp_grain;
	f32   rp_contrast;
	f32   rp_gamma;
	char *lut_path; // .cube
	bool  texture_filter;
	// Application
	struct string_array *recent_projects; // Recently opened projects
	struct string_array *bookmarks;       // Bookmarked folders in browser
	char                *keymap;          // Link to keymap file
	char                *theme;           // Link to theme file
	i32                  undo_steps;      // Number of undo steps to preserve
	f32                  camera_fov;
	f32                  camera_pan_speed;
	f32                  camera_zoom_speed;
	f32                  camera_rotation_speed;
	bool                 camera_upside_down;
	i32                  zoom_direction;
	bool                 wrap_mouse;
	bool                 show_asset_names;
	bool                 material_params;
	bool                 brush_params;
	bool                 touch_ui;
	bool                 splash_screen;
	bool                 header_pinned;   // Tool properties in the header, floating toolbar otherwise
	struct i32_array    *layout_tabs;     // Docked tabs
	i32                  camera_pivot;    // Cursor, camera center
	i32                  camera_controls; // Orbit, rotate, fly
	i32                  viewport_mode;
	i32                  pathtrace_mode;
	i32                  pathtrace_frames;
	bool                 pressure_radius; // Pen pressure controls
	f32                  pressure_sensitivity;
	i32                  layer_res;
	bool                 brush_live;
	bool                 node_previews;
	bool                 pressure_hardness;
	bool                 pressure_angle;
	bool                 pressure_opacity;
	bool                 material_live;
	bool                 brush_depth_reject;
	bool                 brush_angle_reject;
	f32                  brush_alpha_discard;
	i32                  dilate_radius;
	char                *blender;
	bool                 grid_snap;
	bool                 experimental;
	i32                  neural_res;
	bool                 view2d_grid_show;
	i32                  view2d_grid_cell;
	bool                 view2d_grid_snap;
	bool                 pixel_snap;
	bool                 large_asset_previews;
	bool                 tooltips;
	char                *materials_folder; // Library root, "" when not set
	bool                 reload_project;   // Open the last project on startup
	i32                  cloud_resolution; // Cloud materials texture size, 0 - 1K, 1 - 2K, 2 - 4K, 3 - 8K
	bool                 library_cloud;      // Library tab shows the cloud materials
	i32                  library_cloud_type; // Type shown in the cloud view, -1 for all
	f32                  library_scroll;     // Scroll position of the Library tab
	f32                  brush_radius; // Brush settings kept across launches, the others come with the picked brush
	f32                  brush_opacity;
	f32                  brush_strength;
	f32                  pressure_curve; // Exponent applied to the pen pressure, below 1 soft, above 1 firm
	f32                  pressure_min;   // Share of the size, opacity... kept at the lightest pressure
	i32                  brush_selected; // Id of the brush picked in the brush library
	bool                 hide_builtin_brushes; // Brushes tab hides the built-in brushes
	i32                  rp_view_transform;    // 0 Filmic, 1 ACES, 2 AgX, 3 Standard
	bool                 autosave;
	i32                  autosave_interval; // Minutes
	i32                  finger_paint;      // 0 auto (not once the pencil is used), 1 always, 2 never
	i32                  pencil_double_tap; // Pencil gesture actions, see pencil_action_t
	i32                  pencil_squeeze;
	struct i32_array    *quick_menu_slots;  // Quick menu items clockwise from the top, see quick_menu_item_t
	i32                  undo_memory_mb;    // Undo steps are dropped, oldest first, above this budget
} config_t;

typedef struct slot_material {
	struct ui_nodes       *nodes;
	struct ui_node_canvas *canvas;
	struct gpu_texture    *image;
	struct gpu_texture    *image_icon;
	bool                   preview_ready;
	struct shader_data    *data;
	i32                    id;
	bool                   paint_base;
	bool                   paint_opac;
	bool                   paint_occ;
	bool                   paint_rough;
	bool                   paint_met;
	bool                   paint_nor;
	bool                   paint_height;
	bool                   paint_emis;
	bool                   paint_subs;
	f32                    uv_scale; // Texture scale and rotation (degrees) when painted on paint layers
	f32                    uv_angle;
} slot_material_t;

typedef struct tab_draw {
	void (*f)(i32 *);
	i32   id;   // tab_id_t
	char *name; // Untranslated
	i32   area; // tab_area_t redrawn with, -1 for views
	bool  closed;
	bool  closed_default;
} tab_draw_t;

typedef struct dock_node {
	struct dock_node *parent;
	// Split
	struct dock_node *a;         // Left or top
	struct dock_node *b;         // Right or bottom
	bool              vertical;  // Children are stacked
	i32               fixed;     // 0 - ratio, 1 - a keeps the size, 2 - b keeps the size
	f32               size;      // Ratio of a, size in pixels of the fixed child otherwise
	bool              collapsed; // Fixed child is hidden
	i32               role;      // dock_role_t
	// Leaf
	i32          tabs[32]; // tab_id_t, closed tabs included
	i32          tab_count;
	i32          active; // Position in between the open tabs
	i32          last_active;
	ui_window_t *hwnd;
	// Layout
	bool visible;
	i32  x;
	i32  y;
	i32  w;
	i32  h;
} dock_node_t;

typedef struct render_pass {
	i32                 id;
	render_pass_stage_t stage;
	char               *handle;
	shader_context_t   *scon;
} render_pass_t;

typedef struct context {
	struct asset        *texture;
	struct mesh_object  *paint_object;
	struct mesh_object  *merged_object;
	bool                 merged_object_is_atlas;
	i32                  ddirty;  // depth
	i32                  pdirty;  // paint
	i32                  rtdirty; // raytrace
	bool                 brush_blend_dirty;
	bool                 split_view;
	i32                  view_index;
	i32                  view_index_last;
	struct swatch_color *swatch;
	struct swatch_color *picked_color;
	void (*color_picker_callback)(struct swatch_color *);
	struct f32_array           *default_irradiance;
	struct gpu_texture         *default_radiance;
	struct gpu_texture_t_array *default_radiance_mipmaps;
	struct gpu_texture         *saved_envmap;
	struct gpu_texture         *empty_envmap;
	struct gpu_texture         *preview_envmap;
	bool                        envmap_loaded;
	bool                        show_envmap;
	bool                        show_envmap_blur;
	bool                        show_envmap_spheres;
	bool                        capturing_screenshot;
	bool                        capture_background;
	f32                         envmap_angle;
	f32                         light_angle;
	bool                        cull_backfaces;
	texture_ldr_format_t        format_type;
	f32                         format_quality;
	export_destination_t        layers_destination;
	bool                        export_padding;
	bool                        bake_create_material;
	split_type_t                split_by;
	f64                         select_time;
	viewport_mode_t             viewport_mode;
	void                       *viewport_shader;
	gpu_texture_t              *viewport_texture;
	bool                        hscale_was_changed;
	mesh_format_t               export_mesh_format;
	i32                         export_mesh_index;
	bool                        pack_assets_on_export;
	bool                        pack_assets_on_save;
	vec4_t                      paint_vec;
	f32                         last_paint_x;
	f32                         last_paint_y;
	bool                        foreground_event;
	i32                         painted;
	f64                         brush_time;
	f32                         clone_start_x;
	f32                         clone_start_y;
	f32                         clone_delta_x;
	f32                         clone_delta_y;
	bool                        clone_set_source;
	i32                         clone_layers;
	f32                         grab_start_x;
	f32                         grab_start_y;
	bool                        show_compass;
	bool                        show_grid;
	i32                         project_type;
	f32                         last_paint_vec_x;
	f32                         last_paint_vec_y;
	f32                         prev_paint_vec_x;
	f32                         prev_paint_vec_y;
	i32                         frame;
	bool                        paint2d_view;
	bool                        brush_locked;
	camera_type_t               camera_type;
	char                       *texture_export_path;
	i32                         last_status_position;
	camera_pivot_t              camera_pivot;
	camera_controls_t           camera_controls;
	bool                        pen_painting_only;
	struct slot_material       *material;
	struct slot_layer          *layer;
	struct slot_brush          *brush;
	struct slot_font           *font;
	tool_type_t                 tool;
	bool                        layer_preview_dirty;
	bool                        layers_preview_dirty;
	struct i32_imap            *node_preview_socket_map;
	struct any_imap            *node_preview_map;
	char                       *node_preview_name;
	struct any_map             *node_previews;
	struct string_array        *node_previews_used;
	bool                        selected_node_preview;
	struct gpu_texture         *mask_preview_rgba32;
	struct slot_layer          *mask_preview_last;
	bool                        material_preview;
	mat4_t                      saved_camera;
	tool_type_t                 color_picker_previous_tool;
	i32                         materialid_picked;
	f32                         uvx_picked;
	f32                         uvy_picked;
	bool                        picker_select_material;
	bool                        picker_material_mask;
	bool                        picker_color_mask;
	f32                         picker_color_mask_tolerance;
	bool                        picker_hide_other_materials;
	bool                        pick_pos_nor_tex;
	bool                        pick_object_id;
	f32                         posx_picked;
	f32                         posy_picked;
	f32                         posz_picked;
	f32                         norx_picked;
	f32                         nory_picked;
	f32                         norz_picked;
	bool                        draw_wireframe;
	bool                        draw_texels;
	export_mode_t               layers_export;
	struct gpu_texture         *decal_image;
	bool                        decal_preview;
	f32                         decal_x;
	f32                         decal_y;
	bool                        brush_camera_align;
	bool                        write_icon_on_export;
	struct gpu_texture         *text_tool_image;
	char                       *text_tool_text;
	i32                         layer_filter;
	struct object              *gizmo;
	struct object              *gizmo_translate_x;
	struct object              *gizmo_translate_y;
	struct object              *gizmo_translate_z;
	struct object              *gizmo_scale_x;
	struct object              *gizmo_scale_y;
	struct object              *gizmo_scale_z;
	struct object              *gizmo_rotate_x;
	struct object              *gizmo_rotate_y;
	struct object              *gizmo_rotate_z;
	bool                        gizmo_started;
	f32                         gizmo_offset;
	f32                         gizmo_drag;
	f32                         gizmo_drag_last;
	bool                        translate_x;
	bool                        translate_y;
	bool                        translate_z;
	bool                        scale_x;
	bool                        scale_y;
	bool                        scale_z;
	bool                        rotate_x;
	bool                        rotate_y;
	bool                        rotate_z;
	f32                         brush_nodes_radius;
	f32                         brush_nodes_opacity;
	bool                        brush_nodes_uses_random;
	struct gpu_texture         *brush_mask_image;
	bool                        brush_mask_image_is_alpha;
	struct gpu_texture         *brush_stencil_image;
	bool                        brush_stencil_image_is_alpha;
	f32                         brush_stencil_x;
	f32                         brush_stencil_y;
	f32                         brush_stencil_scale;
	bool                        brush_stencil_scaling;
	f32                         brush_stencil_angle;
	bool                        brush_stencil_rotating;
	f32                         brush_nodes_scale;
	f32                         brush_nodes_angle;
	f32                         brush_nodes_hardness;
	bool                        brush_directional;
	f32                         brush_radius;
	f32                         brush_scale_x;
	f32                         brush_decal_mask_radius;
	blend_type_t                brush_blending;
	f32                         brush_opacity;
	f32                         brush_scale;
	f32                         brush_angle;
	f32                         mask_value; // Gray painted on masks, 1 reveals
	f32                         brush_hardness;
	f32                         brush_lazy_radius;
	f32                         brush_lazy_step;
	f32                         brush_lazy_x;
	f32                         brush_lazy_y;
	f32                         brush_spacing;        // Stamps along the stroke, distance in brush diameters, 0 for a continuous stroke
	f32                         brush_spacing_jitter; // Random change of each step, 0-1
	f32                         brush_jitter_lateral; // Random offset of each stamp across the stroke, in brush diameters
	f32                         brush_jitter_linear;  // Random offset of each stamp along the stroke, in brush diameters
	f32                         brush_falloff;        // The stroke fades out along its length, 0 never
	f32                         brush_taper_start;    // Taper lengths 0-1 of Pencil strokes
	f32                         brush_taper_end;
	f32                         brush_touch_taper_start; // Of finger and mouse strokes
	f32                         brush_touch_taper_end;
	f32                         brush_taper_size;    // How thin the tapered ends get
	f32                         brush_taper_opacity; // How transparent
	f32                         brush_stabilization; // Averages the pen positions over time
	i32                         brush_rotation;      // brush_rotation_t
	f32                         brush_roundness;     // 1 round, smaller squashes the tip
	f32                         brush_pressure_size; // How much the pen drives each property 0-1
	f32                         brush_pressure_opacity;
	f32                         brush_pressure_flow;
	f32                         brush_pressure_hardness;
	f32                         brush_tilt_size;
	f32                         brush_tilt_opacity;
	bool                        brush_tilt_shading;
	f32                         brush_stroke_length; // Distance painted since the stroke started, in paint view heights
	f32                         brush_jitter_size;    // Random size reduction of each stamp, 0-1
	f32                         brush_jitter_angle;   // Random rotation of each stamp, 0-1 of a full turn
	f32                         brush_jitter_opacity; // Random opacity reduction of each stamp, 0-1
	f32                         brush_flow;           // Paint added by each stamp, builds up to the opacity, 1 paints the opacity at once
	f32                         brush_smoothing;      // Streamline, the brush trails the pen
	f32                         brush_strength;       // Blur and smudge strength
	f32                         brush_dab_x;          // Last stamp of the stroke, -1 before the first one
	f32                         brush_dab_y;
	f32                         brush_dab_radius;  // Jitter of the stamp being drawn
	f32                         brush_dab_opacity;
	f32                         brush_dab_angle; // Radians
	f32                         brush_speed;     // Smoothed stroke speed 0-1, for the brush dynamics
	f32                         brush_stroke_angle; // Direction of the stroke, radians
	i32                         brush_selected;     // Id of the brush picked in the brush library
	i32                         brush_tip;          // brush_tip_t painted with, the tip is the brush mask
	uv_type_t                   brush_paint;
	f32                         brush_angle_reject_dot;
	bake_type_t                 bake_type;
	bake_axis_t                 bake_axis;
	bake_up_axis_t              bake_up_axis;
	i32                         bake_samples;
	f32                         bake_ao_strength;
	f32                         bake_ao_radius;
	f32                         bake_ao_offset;
	f32                         bake_curv_strength;
	f32                         bake_curv_radius;
	f32                         bake_curv_offset;
	i32                         bake_curv_smooth;
	i32                         bake_high_poly;
	bool                        xray;
	bool                        sym_x;
	bool                        sym_y;
	bool                        sym_z;
	i32                         sym_radial;      // Radial symmetry copies around sym_radial_axis, 1 for none
	i32                         sym_radial_axis; // 0 X, 1 Y, 2 Z of the object
	bool                        sym_center;      // Mirror around the mesh bounds center instead of the object origin
	bool                        brush_straight_line; // Touch: a stroke starts at the end of the previous one, as with the ruler key
	bool                        stencil_edit;        // Touch: drag, pinch and rotate the stencil instead of painting
	bool                        stencil_hide;
	bool                        decal_mask_toggle; // Touch: paint the decal mask, as with the decal mask key
	bool                        grid_snap_toggle;
	i32                         fill_type;
	i32                         blur_type;
	bool                        paint2d;
	i32                         drag_dest;
	vec4_t                      coords;
	f32                         start_x;
	f32                         start_y;
	bool                        lock_begin;
	bool                        lock_x;
	bool                        lock_y;
	f32                         lock_start_x;
	f32                         lock_start_y;
	bool                        registered;
	f32                         particle_hit_x;
	f32                         particle_hit_y;
	f32                         particle_hit_z;
	f32                         last_particle_hit_x;
	f32                         last_particle_hit_y;
	f32                         last_particle_hit_z;
	struct physics_body        *paint_body;
	struct {
		f32                  hit_x;
		f32                  hit_y;
		f32                  hit_z;
		f32                  hit_last_x;
		f32                  hit_last_y;
		f32                  hit_last_z;
		f32                  hit_nor_x;
		f32                  hit_nor_y;
		f32                  hit_nor_z;
		f32                  contact_time;
		bool                 hit; // Touching the surface this frame
		struct tween_anim   *timer;
		struct physics_body *body;
		struct object       *bullet;
	} particles[32];
	i32             particle_index;
	f32             particle_friction;
	f32             particle_bounciness;
	f32             particle_gravity_x;
	f32             particle_gravity_y;
	f32             particle_gravity_z;
	f32             particle_lifetime;
	f32             particle_mass;
	f32             particle_random;
	f32             particle_spawn_distance;
	i32             particle_shape; // Emitter: 0 point, 1 disk, 2 ring, 3 line, spread over the brush radius
	i32             particle_count; // Particles per emit
	f32             particle_rate;  // Emits per second while held
	struct any_map *keymap;
} context_t;

typedef struct node_shader {
	struct node_shader_context *context;
	struct string_array        *ins;
	struct string_array        *outs;
	char                       *frag_out;
	struct string_array        *consts;
	struct string_array        *textures;
	struct any_map             *functions;

	buffer_t vert;
	buffer_t vert_end;
	buffer_t vert_normal;
	buffer_t vert_attribs;
	i32      vert_write_normal;

	buffer_t frag;
	buffer_t frag_end;
	buffer_t frag_normal;
	buffer_t frag_attribs;
	i32      frag_write_normal;

	// References
	bool vert_n;
	bool frag_bposition;
	bool frag_wposition;
	bool frag_mposition;
	bool frag_vposition;
	bool frag_wvpposition;
	bool frag_ndcpos;
	bool frag_wtangent;
	bool frag_vvec;
	bool frag_vvec_cam;
	bool frag_n;
	bool frag_nattr;
	bool frag_dotnv;
} node_shader_t;

typedef struct material {
	char                  *name;
	struct ui_node_canvas *canvas;
} material_t;

typedef struct node_shader_context {
	struct node_shader    *shader;
	struct shader_context *data;
	bool                   allow_vcols;
	struct material       *material;
} node_shader_context_t;

typedef struct history_step {
	char                        *name;
	history_action_t             action;
	struct ui_node_canvas       *canvas; // Node history
	i32                          canvas_group;
	i32                          layer;
	layer_slot_type_t            layer_type;
	i32                          layer_parent;
	i32                          object;
	i32                          material;
	i32                          brush;
	f32                          layer_opacity;
	i32                          layer_object;
	i32                          layer_blending;
	i32                          prev_order; // Previous layer position
	i32                          canvas_type;
	char                        *layer_name;
	f32                          layer_scale;
	f32                          layer_angle;
	i32                          layer_uv_type;
	f32                          layer_height_blend[6]; // Mask height blend: on, below, influence, sharpness, offset, reach
	i32                          swatch;
	struct swatch_color         *swatch_color;
	struct swatch_color_t_array *swatch_colors;
	vec4_t                       object_loc;
	quat_t                       object_rot;
	vec4_t                       object_scale;
	struct slot_layer_t_array   *merge_layers; // Range of the layer array on the other side of a merge
	i32_array_t                 *merge_parents;
	i32                          merge_start;
	i32                          merge_length;
	bool                         with_prev; // Undone and redone together with the previous step
	i32                          undo_ring;      // Whole layer copies the step keeps in the undo layers (history_copy_to_undo)
	bool                         undo_tiled;     // Paint step undone from its tiles, see history_tiles.c
	i32_array_t                 *undo_tiles;     // Tile and pool slot pairs, NULL until the stroke is over
	i32                          undo_tiles_gen;
} history_step_t;

typedef struct logic_node {
	struct logic_node_input_t_array *inputs;
	struct logic_node_t_array       *outputs;
	struct logic_node_value *(*get)(void *, i32);
	struct gpu_texture *(*get_as_image)(void *, i32);
	struct gpu_texture *(*get_cached_image)(void *);
	void (*set)(void *, struct f32_array *);
	struct logic_node_ext *ext;
} logic_node_t;

typedef struct logic_node_ext {
	struct logic_node *base;
} logic_node_ext_t;

typedef struct logic_node_value {
	f32    _f32;
	vec4_t _vec4;
	char  *_str;
} logic_node_value_t;

typedef struct logic_node_input {
	struct logic_node_ext *node;
	i32                    from; // Socket index
} logic_node_input_t;

typedef struct node_group {
	struct ui_nodes       *nodes;
	struct ui_node_canvas *canvas;
} node_group_t;

typedef struct {
	struct asset_t_array         *assets;
	struct slot_material_t_array *materials;
	struct slot_brush_t_array    *brushes;
	struct slot_layer_t_array    *layers;
	struct slot_font_t_array     *fonts;
	struct node_group_t_array    *material_groups;
	char                         *filepath;
	i32                           next_asset_id;
	struct mesh_object_t_array   *paint_objects;
	bool                          translucency; // See-through meshes, detected from the bottom layer and kept on
} project_runtime_t;

typedef struct project {
	char                                        *version;
	struct string_array                         *assets;  // texture_assets
	bool                                         is_bgra; // Swapped red and blue channels for layer textures
	struct packed_asset_t_array                 *packed_assets;
	char                                        *envmap; // Asset name
	f32                                          envmap_strength;
	f32                                          envmap_angle;
	bool                                         envmap_blur;
	struct f32_array                            *camera_world;
	struct f32_array                            *camera_origin;
	f32                                          camera_fov;
	f32                                          camera_near;
	f32                                          camera_far;
	bool                                         camera_frustum_culling;
	struct swatch_color_t_array                 *swatches;
	struct ui_node_canvas_t_array               *brush_nodes;
	struct buffer_t_array                       *brush_icons;
	struct ui_node_canvas_t_array               *material_nodes;
	struct ui_node_canvas_t_array               *material_groups;
	struct buffer_t_array                       *material_icons;
	struct material_data2_t_array               *material_datas;
	struct string_array                         *font_assets;
	struct layer_data_t_array                   *layer_datas;
	struct mesh_data_t_array                    *mesh_datas;
	struct string_array                         *mesh_assets;
	struct buffer_t_array                       *mesh_icons;
	struct f32_array_t_array                    *mesh_transforms;
	struct i32_array                            *mesh_parents;
	struct i32_array                            *atlas_objects;
	struct string_array                         *atlas_names;
	bool                                         linear_blending; // Layers and brush strokes blend colors in linear space
	char                                        *id_map;          // Asset name of the ID map used by ID masks, NULL for none
	project_runtime_t                           *_;
} project_t;

typedef struct asset {
	i32            id;
	char          *name;
	char          *file;
	gpu_texture_t *image;
} asset_t;

typedef struct packed_asset {
	char          *name;
	struct buffer *bytes;
} packed_asset_t;

typedef struct swatch_color {
	i32 base;
	f32 opacity;
	f32 occlusion;
	f32 roughness;
	f32 metallic;
	i32 normal;
	f32 emission;
	f32 height;
	f32 subsurface;
} swatch_color_t;

typedef struct layer_data {
	char             *name;
	i32               res; // Width pixels
	i32               bpp; // Bits per pixel
	struct buffer    *texpaint;
	f32               uv_scale;
	f32               uv_rot;
	i32               uv_type;
	struct f32_array *decal_mat;
	f32               opacity_mask;
	i32               fill_material;
	i32               object_mask;
	i32               blending;
	i32               parent;
	bool              visible;
	struct buffer    *texpaint_nor;
	struct buffer    *texpaint_pack;
	struct buffer    *texpaint_sculpt;
	bool              paint_base;
	bool              paint_opac;
	bool              paint_occ;
	bool              paint_rough;
	bool              paint_met;
	bool              paint_nor;
	bool              paint_nor_blend;
	bool              paint_height;
	bool              paint_height_blend;
	bool              paint_emis;
	bool              paint_subs;
	i32               uv_map;
	f32_array_t      *path_points;
	f32_array_t      *path_points_world;
	f32_array_t      *path_points_camera;
	i32_array_t      *path_points_parent;
	i32               path_tool;
	bool              path_curved;
	i32               path_material;
	bool              path_text;
	bool              height_blend;
	bool              height_blend_below;
	f32               height_blend_influence;
	f32               height_blend_sharpness;
	f32               height_blend_offset;
	f32               height_blend_reach;
} layer_data_t;

typedef struct shader_out {
	char *out_basecol;
	char *out_roughness;
	char *out_metallic;
	char *out_occlusion;
	char *out_opacity;
	char *out_height;
	char *out_emission;
	char *out_subsurface;
} shader_out_t;

typedef struct rect {
	i32 x;
	i32 y;
	i32 w;
	i32 h;
} rect_t;

typedef struct ui_node_t_array *node_list_t;

typedef struct export_preset {
	struct export_preset_texture_t_array *textures;
} export_preset_t;

typedef struct export_preset_texture {
	char                *name;
	struct string_array *channels;
	char                *color_space;
} export_preset_texture_t;

typedef struct neural_node_model {
	char                *name;
	char                *memory;
	char                *size;
	char                *nodes;
	struct string_array *urls;
	char                *web;
	char                *license;
	bool                 expanded; // Preferences panel
} neural_node_model_t;

typedef struct float_node {
	struct logic_node  *base;
	f32                 value;
	struct gpu_texture *image;
} float_node_t;

typedef struct node_group_t_array {
	node_group_t **buffer;
	int            length;
	int            capacity;
} node_group_t_array_t;

typedef struct neural_node_model_t_array {
	neural_node_model_t **buffer;
	int                   length;
	int                   capacity;
} neural_node_model_t_array_t;

typedef struct buffer_t_array {
	buffer_t **buffer;
	int        length;
	int        capacity;
} buffer_t_array_t;

typedef struct f32_array_t_array {
	f32_array_t **buffer;
	int           length;
	int           capacity;
} f32_array_t_array_t;

typedef struct slot_material_t_array {
	slot_material_t **buffer;
	int               length;
	int               capacity;
} slot_material_t_array_t;

typedef struct mesh_object_t_array {
	mesh_object_t **buffer;
	int             length;
	int             capacity;
} mesh_object_t_array_t;

typedef struct ui_node_canvas_t_array {
	ui_node_canvas_t **buffer;
	int                length;
	int                capacity;
} ui_node_canvas_t_array_t;

typedef struct node_list_t_array {
	node_list_t **buffer;
	int           length;
	int           capacity;
} node_list_t_array_t;

typedef struct slot_brush_t_array {
	slot_brush_t **buffer;
	int            length;
	int            capacity;
} slot_brush_t_array_t;

typedef struct asset_t_array {
	asset_t **buffer;
	int       length;
	int       capacity;
} asset_t_array_t;

typedef struct slot_font_t_array {
	slot_font_t **buffer;
	int           length;
	int           capacity;
} slot_font_t_array_t;

typedef struct slot_layer_t_array {
	slot_layer_t **buffer;
	int            length;
	int            capacity;
} slot_layer_t_array_t;

typedef struct gpu_texture_t_array {
	gpu_texture_t **buffer;
	int             length;
	int             capacity;
} gpu_texture_t_array_t;

typedef struct ui_node_socket_t_array {
	ui_node_socket_t **buffer;
	int                length;
	int                capacity;
} ui_node_socket_t_array_t;

typedef struct logic_node_t_array {
	logic_node_t **buffer;
	int            length;
	int            capacity;
} logic_node_t_array_t;

typedef struct transform_t_array {
	transform_t **buffer;
	int           length;
	int           capacity;
} transform_t_array_t;

typedef struct ui_node_button_t_array {
	ui_node_button_t **buffer;
	int                length;
	int                capacity;
} ui_node_button_t_array_t;

typedef struct logic_node_input_t_array {
	logic_node_input_t **buffer;
	int                  length;
	int                  capacity;
} logic_node_input_t_array_t;

typedef struct history_step_t_array {
	history_step_t **buffer;
	int              length;
	int              capacity;
} history_step_t_array_t;

typedef struct object_t_array {
	object_t **buffer;
	int        length;
	int        capacity;
} object_t_array_t;

typedef struct render_target_t_array {
	render_target_t **buffer;
	int               length;
	int               capacity;
} render_target_t_array_t;

typedef struct ui_node_link_t_array {
	ui_node_link_t **buffer;
	int              length;
	int              capacity;
} ui_node_link_t_array_t;

typedef struct mesh_data_t_array {
	mesh_data_t **buffer;
	int           length;
	int           capacity;
} mesh_data_t_array_t;

typedef struct shader_context_t_array {
	shader_context_t **buffer;
	int                length;
	int                capacity;
} shader_context_t_array_t;

typedef struct raw_mesh_t_array {
	raw_mesh_t **buffer;
	int          length;
	int          capacity;
} raw_mesh_t_array_t;

typedef struct export_preset_texture_t_array {
	export_preset_texture_t **buffer;
	int                       length;
	int                       capacity;
} export_preset_texture_t_array_t;

typedef struct layer_data_t_array {
	layer_data_t **buffer;
	int            length;
	int            capacity;
} layer_data_t_array_t;

typedef struct material_data2 {
	bool paint_base;
	bool paint_opac;
	bool paint_occ;
	bool paint_rough;
	bool paint_met;
	bool paint_nor;
	bool paint_height;
	bool paint_emis;
	bool paint_subs;
	f32  uv_scale;
	f32  uv_angle;
} material_data2_t;

typedef struct material_data2_t_array {
	material_data2_t **buffer;
	int                length;
	int                capacity;
} material_data2_t_array_t;

typedef struct ui_node_t_array {
	ui_node_t **buffer;
	int         length;
	int         capacity;
} ui_node_t_array_t;

typedef struct packed_asset_t_array {
	packed_asset_t **buffer;
	int              length;
	int              capacity;
} packed_asset_t_array_t;

typedef struct swatch_color_t_array {
	swatch_color_t **buffer;
	int              length;
	int              capacity;
} swatch_color_t_array_t;

// Per node ui state of the custom node editors (ui_nodes_editor_state())
typedef struct ui_nodes_editor_state {
	i32              channel;
	f32              index[4];
	f32              x;
	f32              y;
	i32              interp;
	ui_color_state_t color_state;
	i32              line;
} ui_nodes_editor_state_t;

typedef struct library_entry library_entry_t; // Material library item, library.c
typedef struct cloud_item    cloud_item_t;    // Cloud material, library_cloud.c

#define STUDIO_LAYERS 6

// Nodes of a studio material layer by role (studio_role_t), NULL where the layer has none
typedef struct studio_layer {
	ui_node_t *n[16];
} studio_layer_t;

struct studio_graph {
	ui_node_t     *output;
	i32            count;
	studio_layer_t layers[STUDIO_LAYERS];
};
