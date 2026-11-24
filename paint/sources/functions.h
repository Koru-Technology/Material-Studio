#pragma once

#include "engine.h"
#include "enums.h"
#include "iron_draw.h"
#include "iron_obj.h"
#include "iron_raycast.h"
#include "iron_ui.h"
#include "types.h"

struct mm_json;
struct mm_plan;
typedef struct make_material_async make_material_async_t;

// ---- base/sources/iron_ui_ext.c ----
f32         ui_MENUBAR_H(ui_t *ui);
ui_t       *ui_create(ui_options_t *ops);
ui_theme_t *ui_theme_create();
void        ui_set_font(ui_t *ui, draw_font_t *font);

// ---- base/sources/iron_ui_nodes.c ----
f32               ui_nodes_INPUT_Y(ui_node_canvas_t *canvas, ui_node_t *node, i32 pos);
void              nodes_on_custom_button(i32 node_id, char *button_name);
ui_nodes_t       *ui_nodes_create();
ui_node_socket_t *ui_get_socket(ui_node_t_array_t *nodes, i32 id);

// ---- io/export_arm.c ----
void      export_arm_run_mesh(char *path, mesh_object_t_array_t *paint_objects);
buffer_t *export_arm_encode_project(char *path);
bool      export_arm_run_project(char *path);
void      export_arm_run_material(char *path);
buffer_t *export_arm_encode_library_material(slot_material_t *m);
void      export_arm_pack_assets(project_t *raw, asset_t_array_t *assets);
void      export_arm_run_swatches(char *path);

// ---- io/export_exr.c ----
buffer_t *export_exr_run(i32 width, i32 height, buffer_t *src, i32 bits, i32 type, i32 off);
FILE     *export_exr_begin(char *path, i32 width, i32 height, i32 bits);
void      export_exr_band(FILE *f, buffer_t *src, i32 width, i32 y0, i32 rows, i32 bits, i32 type, i32 off);

// ---- io/export_mesh.c ----
void export_mesh_run(char *path, mesh_object_t_array_t *paint_objects);

// ---- io/export_obj.c ----
void export_obj_run(char *path, mesh_object_t_array_t *paint_objects);

// ---- io/export_texture.c ----
void export_texture_run(char *path, bool bake_material);
void export_texture_test_options(char *preset, i32 layers_export, bool padding, i32 bits, i32 format);

// ---- io/import_arm_assets.c ----
char           *import_arm_abs(char *file, char *base);
void            import_arm_make_pink(char *abs);
bool            import_arm_texture_has_alpha(gpu_texture_t *t);
void            import_arm_init_nodes(ui_node_t_array_t *nodes);
packed_asset_t *import_arm_take_packed_asset(project_t *project, char *abs, char *file, bool copy);
bool            import_arm_is_decodable(char *abs);
char           *import_arm_load_asset(project_t *project, char *file, char *base);

// ---- io/import_arm.c ----
void import_arm_set_preview_given(slot_material_t *m);
bool import_arm_take_preview_given(slot_material_t *m);
void import_arm_run_project(char *path);
bool       import_arm_has_version(buffer_t *b);
i32        import_arm_version(buffer_t *b);
project_t *import_arm_decode(buffer_t *b);

// ---- io/import_arm_library.c ----
void import_arm_run_material_from_project_on_next_frame(slot_material_t_array_t *imported);
void import_arm_material_data(slot_material_t *m, material_data2_t *md);
void import_arm_import_materials(project_t *project, char *path, i32_array_t *selected, bool delete_blob);
void import_arm_run_material_from_project(project_t *project, char *path);
void import_arm_run_swatches_from_project(project_t *project, char *path, bool replace_existing);
bool import_arm_run_material(char *path);
void import_arm_run_brush(char *path);
void import_arm_run_brush_from_project(project_t *project, char *path);
void import_arm_run_swatches(char *path, bool replace_existing);

// ---- io/import_arm_mesh.c ----
mesh_data_t_array_t *import_arm_get_mesh_datas(project_t *project, string_array_t *mesh_names);
void                 import_arm_run_mesh(project_t *raw);
bool                 import_arm_is_selected(i32_array_t *selected, i32 i);
bool                 import_arm_object_name_exists(char *name);

// ---- io/import_asset.c ----
void import_asset_run(char *path, f32 drop_x, f32 drop_y, bool show_box, bool hdr_as_envmap, void (*done)(void));

// ---- io/import_blend_mesh.c ----
void import_blend_mesh_ui();
void import_blend_mesh_run(char *path, bool replace_existing);

// ---- io/import_envmap.c ----
void import_envmap_run(char *path, gpu_texture_t *image);

// ---- io/import_folder.c ----
bool             import_folder_find_maps(string_array_t *files, char **maps);
slot_material_t *import_folder_make_material(char *name, char *dir, char **maps);
void             import_folder_run(char *path);
void             import_folder_run_named(char *path, char *name);

// ---- io/import_font.c ----
void import_font_run(char *path);

// ---- io/import_json_material.c ----
void import_json_material_run(char *path);

// ---- io/import_keymap.c ----
void import_keymap_run(char *path);

// ---- io/import_lut.c ----
void import_lut_free();
void import_lut_run(const char *path);

// ---- io/import_mesh.c ----
void         import_mesh_run(char *path, bool _clear_layers, bool replace_existing, bool keep_camera);
void         import_mesh_finish_import(void *_);
char        *_import_mesh_unique_name(char *name);
void         import_mesh_make_mesh(raw_mesh_t *mesh);
void         import_mesh_add_mesh(raw_mesh_t *mesh);
mesh_data_t *import_mesh_raw_mesh(raw_mesh_t *mesh);

// ---- io/import_obj.c ----
void import_obj_run(char *path, bool replace_existing);
void import_obj_parse(buffer_t *b, bool replace_existing);

// ---- io/import_stl.c ----
void import_stl_init();

// ---- io/import_text.c ----
void import_text_run(char *path);

// ---- io/import_texture.c ----
void import_texture_run(char *path, bool hdr_as_envmap);

// ---- io/import_theme.c ----
void import_theme_run(char *path);

// ---- nodes_brush/boolean_node.c ----
void boolean_node_init();

// ---- nodes_brush/brush_output_node.c ----
void brush_output_node_parse_inputs();
void brush_output_node_init();

// ---- nodes_brush/color_node.c ----
void color_node_init();

// ---- nodes_brush/float_node.c ----
void float_node_init();

// ---- nodes_brush/input_node.c ----
void input_node_init();

// ---- nodes_brush/integer_node.c ----
void integer_node_init();

// ---- nodes_brush/math_node.c ----
void math_node_init();

// ---- nodes_brush/null_node.c ----
void null_node_init();

// ---- nodes_brush/random_node.c ----
void random_node_init();

// ---- nodes_brush/separate_vector_node.c ----
void separate_vector_node_init();

// ---- nodes_brush/string_node.c ----
void string_node_init();

// ---- nodes_brush/tex_image_node.c ----
void tex_image_node_init();

// ---- nodes_brush/time_node.c ----
void time_node_init();

// ---- nodes_brush/vector_math_node.c ----
void vector_math_node_init();

// ---- nodes_brush/vector_node.c ----
void vector_node_init();

// ---- nodes_material/attribute_node.c ----
void attribute_node_init();

// ---- nodes_material/bake_texture_node.c ----
bool bake_to_target(render_target_t *rt, bake_type_t bake_type, void (*done)(void *), void *data);

// ---- nodes_material/blur_node.c ----
char *blur_node_input_alpha(ui_node_t *node);
i32   blur_node_seamless_passes(ui_node_t *node);
void  blur_node_init();

// ---- nodes_material/brick_texture_node.c ----
void brick_texture_node_init();

// ---- nodes_material/brightness_contrast_node.c ----
void brightness_contrast_node_init();

// ---- nodes_material/bump_node.c ----
void bump_node_init();

// ---- nodes_material/camera_texture_node.c ----
void camera_texture_node_init();

// ---- nodes_material/checker_texture_node.c ----
void checker_texture_node_init();

// ---- nodes_material/clamp_node.c ----
void clamp_node_init();

// ---- nodes_material/color_mask_node.c ----
void color_mask_node_init();

// ---- nodes_material/color_ramp_node.c ----
void nodes_material_color_ramp_button(i32 node_id);
void color_ramp_node_init();

// ---- nodes_material/combine_color_node.c ----
void combine_color_node_init();

// ---- nodes_material/combine_xyz_node.c ----
void combine_xyz_node_init();

// ---- nodes_material/curvature_bake_node.c ----
void curvature_bake_node_init();

// ---- nodes_material/float_curve_node.c ----
void float_curve_node_init();

// ---- nodes_material/gabor_texture_node.c ----
void gabor_texture_node_init();

// ---- nodes_material/gamma_node.c ----
void gamma_node_init();

// ---- nodes_material/geometry_node.c ----
void geometry_node_init();

// ---- nodes_material/gradient_texture_node.c ----
void gradient_texture_node_init();

// ---- nodes_material/group_node.c ----
void nodes_material_new_group_button(i32 node_id);
void nodes_material_group_input_button(i32 node_id);
void nodes_material_group_output_button(i32 node_id);
void nodes_material_sync_sockets(ui_node_t *node);
void group_node_init();

// ---- nodes_material/hue_saturation_value_node.c ----
void hue_saturation_value_node_init();

// ---- nodes_material/image_texture_node.c ----
char *parser_material_texture_store(ui_node_t *node, bind_tex_t *tex, char *tex_name, i32 color_space);
void  image_texture_node_init();

// ---- nodes_material/invert_color_node.c ----
void invert_color_node_init();

// ---- nodes_material/layer_mask_node.c ----
void layer_mask_node_init();

// ---- nodes_material/layer_node.c ----
void layer_node_init();

// ---- nodes_material/magic_texture_node.c ----
void magic_texture_node_init();

// ---- nodes_material/map_range_node.c ----
void map_range_node_init();

// ---- nodes_material/mask_nodes.c ----
slot_material_t *nodes_material_node_material(ui_node_t *node);
char            *curvature_mask_bake_id(ui_node_t *node, slot_material_t *m);
void             curvature_mask_bake_meshes(ui_node_t *node, mesh_object_t_array_t *objs, i32 res, char *id);
void             curvature_mask_bake(ui_node_t *node);
void             curvature_mask_preview_begin(slot_material_t *m);
void             curvature_mask_preview_end();
void             mask_nodes_init();

// ---- nodes_material/material_mix_node.c ----
void material_mix_node_init();

// ---- nodes_material/mapping_node.c ----
void mapping_node_init();

// ---- nodes_material/material_node.c ----
void material_node_init();

// ---- nodes_material/math2_node.c ----
void math2_node_init();

// ---- nodes_material/mm_nodes.c ----
bool  mm_node_is(ui_node_t *node); // A Material Maker node
i32   mm_nodes_count();
i32   mm_nodes_count_all();
f32   mm_nodes_init_ms();
char *mm_nodes_skipped();
void  mm_node_def_ensure(ui_node_t *def); // Builds the sockets and buttons of a node type
f32   mm_nodes_build_ms();
i32   mm_nodes_build_all();

// ---- nodes_material/mm_nodes_params.c ----
char           *mm_node_name(ui_node_t *node);    // Its name in Material Maker's graph
void            mm_node_created(ui_node_t *node); // Gives a new node its seed
double          mm_node_seed(ui_node_t *node, bool *locked);
char           *mm_node_input_expression(ui_node_t *node, i32 input);
struct mm_json *mm_node_parameters(ui_node_t *node); // Its parameters in Material Maker's format
struct mm_json *mm_node_data(ui_node_t *node);       // As a Material Maker node: type, parameters, seed
char           *mm_node_parameters_json(ui_node_t *node);
char           *mm_node_data_json(ui_node_t *node);
ui_node_t      *mm_node_named(ui_node_canvas_t *canvas, char *name);
bool            mm_node_is_param_socket(ui_node_socket_t *s); // A parameter wired to a Variable
i32             mm_node_port_count(ui_node_t *node);
bool            mm_node_param_socket_key(ui_node_t *node, i32 input, char **param, i32 *stop);
i32             mm_node_param_kind(ui_node_t *node, char *param);
i32             mm_node_param_socket(ui_node_t *node, char *param, i32 stop, ui_node_canvas_t *canvas);
void            mm_nodes_prune_param_sockets(ui_node_canvas_t *canvas);
i32             mm_node_param_get(ui_node_t *node, char *param, i32 stop, f32 *out, i32 max, i32 *interp);
char           *mm_node_param_options(ui_node_t *node, char *param);
bool            mm_node_param_range(ui_node_t *node, char *param, f32 *min, f32 *max, f32 *step);
bool            mm_node_param_set(ui_node_t *node, char *param, i32 stop, f32 *v, i32 n, i32 interp);
i32             mm_node_export_count(ui_node_t *node);
bool            mm_node_export_param_at(ui_node_t *node, i32 i, char **name, char **label, i32 *kind);

// ---- nodes_material/mm_nodes_sync.c ----
void mm_node_update(ui_node_t *node, ui_node_canvas_t *canvas); // Switch sockets, "any" types, remotes
void mm_nodes_sync(ui_node_canvas_t *canvas);
void mm_node_set_data(ui_node_t *node, struct mm_json *data, ui_node_canvas_t *canvas); // From a Material Maker node
void mm_node_set_data_json(ui_node_t *node, char *json, ui_node_canvas_t *canvas);

// ---- nodes_material/mm_nodes_buttons.c ----
void mm_nodes_gradient_edit(ui_node_t *node, i32 button, char *label);

// ---- nodes_material/mm_nodes_register.c ----
void mm_nodes_init(); // nodes_material/mm_nodes.c: Material Maker nodes

// ---- nodes_material/mm_material_node.c ----
bool mm_material_is(ui_node_t *n);
void mm_material_init(); // nodes_material/mm_material_node.c: MM Material

// ---- nodes_material/mm_import.c ----
i32              mm_import_count(i32 what);
char            *mm_import_report();
char            *mm_material_parameters_json(ui_node_t *node);
bool             mm_import_graph(struct mm_json *graph, slot_material_t *target);
bool             mm_import_ptex(char *ptex_text, slot_material_t *target);
slot_material_t *mm_import_file(char *path);
i32              mm_import_library_count();
i32              mm_import_library_id(i32 i);
char            *mm_import_library_name(i32 id);
char            *mm_import_library_kind(i32 id);
slot_material_t *mm_import_library(i32 id);
bool             mm_import_library_into(i32 id, slot_material_t *target);
ui_node_t       *mm_import_material_node(slot_material_t *m);
gpu_texture_t   *mm_import_preview(ui_node_t *node, i32 socket, i32 size);
void             mm_import_compile(slot_material_t *m);
void             mm_import_save_png(gpu_texture_t *t, char *path);
void             mm_import_texture_free(gpu_texture_t *t);
i32              mm_import_image_changed(gpu_texture_t *a, gpu_texture_t *b);
i32              mm_import_image_diff(gpu_texture_t *a, gpu_texture_t *b);

// ---- nodes_material/mm_library.c ----
i32            mm_library_count(); // nodes_material/mm_library.c: Material Maker's library in the Library tab
i32            mm_library_section_count();
char          *mm_library_section_name(i32 s);
i32            mm_library_section(i32 i);
bool           mm_library_stylized(i32 i);
i32            mm_library_id(i32 i);
char          *mm_library_name(i32 i);
char          *mm_library_credit(i32 i);
bool           mm_library_matches(i32 i, char *search);
i32            mm_library_find(i32 id);
void           mm_library_use(i32 i);
gpu_texture_t *mm_library_icon(i32 i);
bool           mm_library_icon_failed(i32 i);
bool           mm_library_icons_loading();
i32            mm_library_icons_loaded();
void           mm_thumbs_generate(char *out, i32 first, i32 count);
i32            mm_thumbs_progress();
bool           mm_thumbs_finished();
char          *mm_thumbs_report();
i32            mm_thumbs_count(i32 what);
bool           mm_thumbs_has(i32 id);
f32            mm_library_search_ms(char *search);

// ---- nodes_material/mm_controls.c ----
bool       mm_controls_apply(slot_material_t *m, i32 id); // nodes_material/mm_controls.c: the library's controls as Variables
bool       mm_controls_common_missing(ui_node_canvas_t *canvas);
void       mm_controls_common(slot_material_t *m); // and the common controls of every library material
i32        mm_controls_material_count(void);
i32        mm_controls_material_id(i32 i);
i32        mm_controls_material_controls(i32 i);
f32        mm_node_param_value(ui_node_canvas_t *canvas, char *name, char *param, i32 stop, i32 i);
ui_node_t *mm_node_export_param(ui_node_canvas_t *canvas, ui_node_t *node, char *param, i32 stop);

// ---- nodes_material/mm_bridge.c ----
char *mm_bridge_output(ui_node_t *node, ui_node_socket_t *socket);
void  mm_bridge_begin(void);
void  mm_bridge_finalize(node_shader_t *shader);
void  mm_bridge_request_builtins(node_shader_t *shader);
void  mm_bridge_init(void); // nodes_material/mm_bridge.c: Material Maker nodes' code in material shaders

// ---- nodes_material/mm_buffers.c ----
#define MM_BUFFERS_TINY 1 // Tiers of a Material Maker buffer: tiny (on the spot, ~300 ms for the whole material),
#define MM_BUFFERS_LOW  2 // low (512),
#define MM_BUFFERS_FULL 3 // full (the pass's own size, from full inputs)

void mm_buffers_set_size_cap(i32 size);
i32  mm_buffers_require(i32 level); // What links need: 0 any, else at least that tier now; returns the previous
void mm_buffers_test_res(i32 res);
void mm_buffers_trim(void);
void mm_buffers_test_budget(i32 kb);
void mm_buffers_set_export_cap(i32 cap); // Full sizes up to cap during an export, 0 the normal cap
i64  mm_buffers_export_budget(i64 reserved);

// ---- nodes_material/mm_buffers_pipes.c ----
void mm_buffers_hold_workers(bool hold);

// ---- nodes_material/mm_buffers_render.c ----
void            mm_buffers_batch_end(void);
void            mm_buffers_batch_begin(void);
gpu_texture_t **mm_buffers_render(struct mm_plan *plan);         // nodes_material/mm_buffers.c: Material Maker buffers
char           *mm_buffers_link(struct mm_plan *plan, i32 pass); // "_mmbuf<key>", the texture link of a rendered pass
i32             mm_buffers_rush_skipped(void);                   // Grows when a paint draw's on-the-spot render is skipped or cut short
gpu_texture_t  *mm_buffers_texture(char *link);
i32             mm_buffers_paint_keys(shader_context_t *scon, u64 *out, i32 max); // The buffer keys a shader samples
i32             mm_buffers_links_tier(u64 *keys, i32 n);                          // The lowest tier among them (0: one has no texture, -1: one is gone)
i32             mm_buffers_landed_count(void);                                    // Grows whenever a buffer texture lands
void            mm_buffers_finish(void);                                          // Every queued pass rendered full now
void            mm_buffers_clear();
i32             mm_buffers_stat(i32 what);
void            mm_buffers_init(); // Image node files, the text node's font
i32             mm_buffers_export_begin(u64 *keys, i32 n, i32 max_size, i64 budget); // Export: the cap chosen, the passes pinned
i32             mm_buffers_export_step(bool sync);                                 // Renders them full (sync) or queues them, how many are left
void            mm_buffers_export_end(void);                                       // Unpinned, larger textures freed, the normal cap
i32             mm_buffers_export_largest(void);

// ---- nodes_material/mm_buffers_test.c ----
char *mm_buffers_test_ptex(char *path_or_id, i32 size);
char *mm_buffers_test_other(char *path_or_id, i32 size);
i32   mm_buffers_test_resident(i32 map);
i32   mm_buffers_test_resolve(i32 in_pass);
i32   mm_buffers_test_map_size(i32 map);
f32   mm_buffers_test_texel(i32 map, i32 x, i32 y, i32 channel);
void  mm_buffers_test_write(char *prefix);
i32   mm_buffers_test_export(i32 max_size, i32 budget_kb);
i32   mm_buffers_test_tex_size(i32 map);

// ---- nodes_material/mix_color_node.c ----
void mix_color_node_init();

// ---- nodes_material/mix_normal_map_node.c ----
void mix_normal_map_node_init();

// ---- nodes_material/noise_texture_node.c ----
void noise_texture_node_init();

// ---- nodes_material/normal_map_node.c ----
void normal_map_node_init();

// ---- nodes_material/normal_node.c ----
void normal_node_init();

// ---- nodes_material/object_info_node.c ----
void object_info_node_init();

// ---- nodes_material/pattern_nodes.c ----
i32               pattern_node_count();
char             *pattern_node_type(i32 i);
char             *pattern_node_name(i32 i);
i32               pattern_node_index(char *type);
ui_node_button_t *nodes_material_button_def(char *name, char *type, f32 value, char *data);
void              pattern_lib_use(i32 i);
void              pattern_lib_add(node_shader_t *shader, i32 i);
char             *pattern_def_fn(i32 i);
f32               pattern_def_scale(i32 i);
i32               pattern_def_extras(i32 i);
char             *pattern_def_extra_name(i32 i, i32 j);
f32               pattern_def_extra_value(i32 i, i32 j);
f32               pattern_def_extra_max(i32 i, i32 j);
void              pattern_nodes_init();

// ---- nodes_material/picker_node.c ----
void picker_node_init();

// ---- nodes_material/quantize_node.c ----
void quantize_node_init();

// ---- nodes_material/replace_color_node.c ----
void replace_color_node_init();

// ---- nodes_material/rgb_curves_node.c ----
void rgb_curves_node_init();

// ---- nodes_material/rgb_node.c ----
void rgb_node_init();

// ---- nodes_material/rgb_to_bw_node.c ----
void rgb_to_bw_node_init();

// ---- nodes_material/separate_color_node.c ----
void separate_color_node_init();

// ---- nodes_material/separate_xyz_node.c ----
void separate_xyz_node_init();

// ---- nodes_material/studio_graph.c ----
i32        studio_role(ui_node_t *n);
i32        studio_layer(ui_node_t *n);
bool       studio_graph_read(ui_node_canvas_t *c, studio_graph_t *g);
bool       studio_graph_is(ui_node_canvas_t *c);
void       studio_graph_create(ui_node_canvas_t *c);
void       studio_graph_set_pattern(ui_node_canvas_t *c, i32 k, char *type);
void       studio_graph_set_mask(ui_node_canvas_t *c, i32 k, i32 mask);
i32        studio_graph_mask(ui_node_canvas_t *c, i32 k);
i32        studio_graph_add_layer(ui_node_canvas_t *c);
void       studio_graph_remove_layer(ui_node_canvas_t *c, i32 k);
void       studio_graph_move_layer(ui_node_canvas_t *c, i32 k, i32 dir);
ui_node_t *studio_graph_node(ui_node_canvas_t *c, i32 k, i32 role);
i32        studio_graph_layers(ui_node_canvas_t *c);
f32        studio_graph_get(ui_node_canvas_t *c, i32 k, i32 value);
void       studio_graph_set(ui_node_canvas_t *c, i32 k, i32 value, f32 v);
i32        studio_graph_height_blend(ui_node_canvas_t *c, i32 k);
void       studio_graph_set_height_blend(ui_node_canvas_t *c, i32 k, i32 blend);
bool       studio_graph_set_pattern_input(ui_node_canvas_t *c, i32 k, char *name, f32 v);
void       studio_graph_set_mapping(ui_node_canvas_t *c, i32 k, bool triplanar);
void       studio_graph_set_ramp(ui_node_canvas_t *c, i32 k, i32 count, f32 (*color)[3], f32 *pos, bool hard);
bool       studio_graph_ramp_hard(ui_node_canvas_t *c, i32 k);
void       studio_graph_set_ramp_hard(ui_node_canvas_t *c, i32 k, bool hard);
void       studio_graph_set_layer_name(ui_node_canvas_t *c, i32 k, char *name);
bool       studio_graph_channel(ui_node_canvas_t *c, i32 k, i32 ch);
void       studio_graph_set_channel(ui_node_canvas_t *c, i32 k, i32 ch, bool on);
i32        studio_preset_count();
char      *studio_preset_name(i32 i);
i32        studio_layer_preset_count();
char      *studio_layer_preset_name(i32 i);
i32        studio_graph_add_layer_preset(ui_node_canvas_t *c, i32 i);
void       studio_graph_apply_preset(ui_node_canvas_t *c, i32 i);

// ---- nodes_material/text_texture_node.c ----
void text_texture_node_init();

// ---- nodes_material/texture_coordinate_node.c ----
void texture_coordinate_node_init();

// ---- nodes_material/tilesheet_node.c ----
void tilesheet_node_init();

// ---- nodes_material/uv_map_node.c ----
void uv_map_node_init();

// ---- nodes_material/value_node.c ----
void value_node_init();

// ---- nodes_material/variable_node.c ----
bool               variable_is(ui_node_t *node);
i32                variable_kind(ui_node_t *node); // 0 Number, 1 Whole Number, 2 Toggle, 3 Choice, 4 Color, 5 Gradient
char              *variable_name(ui_node_t *node);
char              *variable_id(ui_node_t *node);
string_array_t    *variable_options(ui_node_t *node);
void               variable_range(ui_node_t *node, f32 *min, f32 *max, f32 *step);
i32                variable_group(ui_node_t *node); // 0 color, 1 pattern, 2 surface, 3 detail
i32                variable_order(ui_node_t *node);
i32                variable_unit(ui_node_t *node); // 0 none, 1 percent, 2 degrees, 3 count, 4 multiplier
char              *variable_group_name(i32 group);
i32                variable_get(ui_node_t *node, f32 *out, i32 max, i32 *interp);
f32_array_t       *variable_value_array(ui_node_t *node);
void               variable_set(ui_node_t *node, f32 *v, i32 n, i32 interp);
void               variable_sync(ui_node_t *node);
i32                variable_eval(ui_node_t_array_t *nodes, ui_node_link_t_array_t *links, ui_node_t *node, i32 output, f32 *out, i32 max, i32 *interp);
bool               variable_propagate(ui_node_t_array_t *nodes, ui_node_link_t_array_t *links);
f32                variable_value(ui_node_t *node, i32 i);
void               variable_set_value(ui_node_t *node, i32 i, f32 x);
bool               variable_propagate_canvas(ui_node_canvas_t *canvas);
ui_node_t_array_t *variable_list(ui_node_canvas_t *canvas);
i32                variable_count(ui_node_canvas_t *canvas);
f32                variable_shuffle(ui_node_t_array_t *nodes);
bool               variable_compiles(ui_node_canvas_t *canvas, ui_node_t *node);
ui_node_t         *variable_find(ui_node_canvas_t *canvas, char *id);
ui_node_t         *variable_at(ui_node_canvas_t *canvas, i32 i);
f32                variable_param(ui_node_t *node, i32 what); // 0 min, 1 max, 2 step, 3 options, 4 value length, 5 order
void               variable_setup(ui_node_t *node, i32 kind, char *label, char *id, char *group, char *unit, i32 order);
void               variable_set_range(ui_node_t *node, f32 min, f32 max, f32 step);
void               variable_set_options(ui_node_t *node, char *options);
void               variable_node_init(); // nodes_material/variable_node.c: exported values, controls of the material
void               select_value_node_init();

// ---- nodes_material/vector_curves_node.c ----
f32   vector_curves_eval_cpu(f32 *points, i32 num, f32 t);
char *vector_curves_eval(char *name, char *fac, f32 *points, i32 num);
void  nodes_material_vector_curves_button(i32 node_id);
void  vector_curves_node_init();

// ---- nodes_material/vector_math2_node.c ----
void vector_math2_node_init();

// ---- nodes_material/vector_rotate_node.c ----
void vector_rotate_node_init();

// ---- nodes_material/vector_transform_node.c ----
void vector_transform_node_init();

// ---- nodes_material/voronoi_texture_node.c ----
void voronoi_texture_node_init();

// ---- nodes_material/warp_node.c ----
void warp_node_init();

// ---- nodes_material/wave_texture_node.c ----
void wave_texture_node_init();

// ---- nodes_material/wireframe_node.c ----
void wireframe_node_init();

// ---- render/make_bake.c ----
void make_bake_run(node_shader_context_t *con, node_shader_t *shader);
void make_bake_position_normal(node_shader_t *shader);
void make_bake_set_color_writes(node_shader_context_t *con_paint);

// ---- render/make_blur.c ----
void make_blur_run(node_shader_t *shader);

// ---- render/make_brush.c ----
void make_brush_run(node_shader_t *shader, bool depth_reject);
void make_brush_run_batched(node_shader_t *shader, bool depth_reject);
void make_brush_run_batched_coverage(node_shader_t *shader);

// ---- render/make_clone.c ----
void make_clone_run(node_shader_t *shader, char *tex_src, char *tex_nor_src, char *tex_pack_src);

// ---- render/make_curvature.c ----
void make_curvature_bake(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents, char *id);
void make_curvature_mesh_changed();
void make_curvature_bake_mesh(char *id, mesh_object_t_array_t *objs, i32 res_x, i32 res_y, f32 strength, f32 radius, f32 offset, i32 side, bool use_axis,
                              bool normalize, f32 spread);

// ---- render/make_depth.c ----
node_shader_context_t *make_depth_run(material_t *data);

// ---- render/make_discard.c ----
void make_discard_material_id(node_shader_t *shader, char *tex_coord);
void make_discard_picked_color(node_shader_t *shader, char *tex_src, char *tex_src_link);

// ---- render/make_material.c ----
bool                   make_material_is_default(shader_data_t *m);
void                   make_material_free_context(shader_context_t *c);
void                   make_material_delete_context(shader_context_t *c);
void                   make_material_parse_mesh_material();
void                   make_material_parse_mesh_material_async(void);
bool                   make_material_mesh_pending(void);
void                   make_material_parse_mesh_preview_material();
void                   make_material_restore_mesh_material();
char                  *make_material_node_preview_id(ui_node_t *node, ui_node_t_array_t *parents);
void                   make_material_delete_node_previews(slot_material_t *m, string_array_t *keep);
void                   make_material_bake_node_previews();
void                   make_material_bake_node_previews_linked();
void                   make_material_parse_paint_material(bool bake_previews);
void                   make_material_use_pick_material();
make_material_async_t *make_material_async_start(void);
make_material_async_t *make_material_async_compile(shader_context_t *scon, char *glsl);
void                   make_material_async_drop(make_material_async_t *a);
bool                   make_material_async_started(make_material_async_t *a);
bool                   make_material_async_ready(make_material_async_t *a);
shader_context_t      *make_material_async_scon(make_material_async_t *a);
void                   make_material_async_free(make_material_async_t *a);
bool                   make_material_paint_ready(f64 wait);
bool                   make_material_paint_pending(void);
void                   make_material_parse_paint_material_async(bool bake_previews);
i32                    make_material_paint_stat(i32 what);
void                   make_material_free_paint_scon(shader_context_t *scon);
void                   make_material_save_paint_material();
void                   make_material_restore_paint_material();
shader_context_t      *make_material_parse_node_preview_material(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents);
void                   make_material_parse_brush();
bool                   make_material_blend_mode_channels(i32 blending);
char                  *make_material_blend_mode(node_shader_t *shader, i32 blending, char *cola, char *colb, char *opac);
void                   make_material_parse_depth_material();

// ---- render/make_mesh.c ----
char                  *make_mesh_context_id(i32 layer_pass);
node_shader_context_t *make_mesh_run(material_t *data, i32 layer_pass);

// ---- render/layer_cache.c ----
bool layer_cache_active(void);
bool layer_cache_has(slot_layer_t *l);
void layer_cache_prepare(void);
typedef struct layer_cache_plan layer_cache_plan_t;
layer_cache_plan_t *layer_cache_plan_take(shader_context_t **scon, char **glsl, void (*made)(void *), void *data);
void                layer_cache_plan_put(layer_cache_plan_t *p, shader_context_t *scon);
void                layer_cache_plan_free(layer_cache_plan_t *p);
void layer_cache_bind(void);
void layer_cache_update(void);
bool layer_cache_pending(void);
i32  layer_cache_bake_count(void);

// ---- render/make_mesh_preview.c ----
node_shader_context_t *make_mesh_preview_run(material_t *data, bool viewport);

// ---- render/make_node_preview.c ----
node_shader_context_t *make_node_preview_run(material_t *data, ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents);

// ---- render/make_paint.c ----
extern bool            make_paint_defer_translation;
extern bool            make_mesh_defer_translation;
extern char           *make_mesh_deferred_glsl;
extern char           *make_paint_deferred_glsl;
void                   make_paint_cache_put(char *shader, char *vs, char *fs);
node_shader_context_t *make_paint_run(material_t *data);
extern bool            make_paint_batched;
bool                   make_paint_is_batched();
void                   make_paint_set_batching(bool on);
node_shader_context_t *make_paint_run_context(material_t *data, char *context_id);
node_shader_context_t *make_paint_cache_bake_run(material_t *data, char *format, bool *emis_used, bool *subs_used, bool *height_used, char **glsl, char **why);

// ---- render/make_particle.c ----
shader_data_t *make_particle_get_bullet_material();
void           make_particle_mask(node_shader_t *shader);

// ---- render/make_picking.c ----
bool make_picking_visible();
void make_picking_run(node_shader_t *shader);

// ---- render/make_texcoord.c ----
void make_texcoord_run(node_shader_t *shader, bool is_atlas);

// ---- render/render_envsphere.c ----
mesh_object_t *render_sphere_object(char *name, char *basecol, f32 roughness, f32 metallic);
void           render_envsphere();

// ---- render/render_gizmo.c ----
void render_gizmo_draw();
void render_gizmo_update();

// ---- render/render_grid.c ----
void render_grid();

// ---- render/render_pass.c ----
render_pass_t *render_pass_create(char *frag, render_pass_stage_t stage);
void           render_pass_set_vec4(render_pass_t *p, char *name, f32 x, f32 y, f32 z, f32 w);
vec4_t         render_pass_get_vec4(char *link);
void           render_pass_remove(render_pass_t *p);
bool           render_pass_any();
void           render_pass_draw(render_pass_stage_t stage, char *target);

// ---- render/render_path_base.c ----
void              render_path_base_init();
void              render_path_base_apply_config();
f32               render_path_base_get_super_sampling();
gpu_texture_t    *render_path_target_image(char *name);
gpu_texture_t    *render_path_empty_black();
gpu_texture_t    *render_path_empty_white();
void              render_path_base_redraw();
void              render_path_base_commands(void (*draw_commands)(void));
void              render_path_base_draw_bloom(char *source, char *target);
void              render_path_base_init_ssao();
void              render_path_base_init_paint_depth();
bool              render_path_base_has_paint_depth();
char             *render_path_base_ssao_target();
void              render_path_base_draw_ssao();
bool              render_path_base_gbuffer_uv();
void              render_path_base_draw_deferred_light();
extern mat4_t     render_path_base_taa_prev_vp;
void              render_path_base_draw_taa(char *bufa, char *bufb);
void              render_path_base_swap_buf(char *bufb);
shader_context_t *render_path_base_mesh_scon(i32 layer_pass);
shader_context_t *render_path_base_own_scon(mesh_object_t *o);
void              render_path_base_draw_meshes(shader_context_t *scon, bool own);
void              render_path_base_draw_overrides(mesh_data_t *data);
void              render_path_base_draw_gbuffer();

// ---- render/render_path_lit.c ----
void render_path_draw_overlays(char *target);
void render_path_lit_commands();

// ---- render/paint_cull.c ----
void paint_cull_set_enabled(bool on);
f32  paint_cull_last_kept();

// ---- render/render_path_paint.c ----
void                  render_path_paint_init();
void                  render_path_paint_set_band(i32 y, i32 w, i32 h);
mesh_object_t        *render_path_paint_target();
void                  render_path_paint_draw_target();
void                  render_path_paint_release_scratch();
bool                  render_path_paint_batched();
void                  render_path_paint_commands_paint(bool dilation); // From outside the stroke model: isolated (paint_exec.c)
void                  render_path_paint_commands_paint_stroke(bool dilation);
void                  render_path_paint_use_live_layer(bool use);
bool                  render_path_paint_symmetry_on();
bool                  render_path_paint_paint_enabled();
void                  render_path_paint_begin();
void                  render_path_paint_end();
bool                  render_path_paint_is_rt_bake();
void                  render_path_paint_clear_blend();
void                  render_path_paint_clear_blend_stroke();
void                  render_path_paint_draw();
void                  render_path_paint_bind_layers();
void                  render_path_paint_unbind_layers();
void                  render_path_paint_live_layer_used();
void                  render_path_paint_live_layer_copy();
bool                  render_path_paint_live_shown();
void                  render_path_paint_live_layer_release_idle();
void                  render_path_paint_dilate_after_paint();
void                  render_path_paint_dilate(bool base, bool nor_pack);
void                  render_path_paint_dilate_stroke(bool base, bool nor_pack);

// ---- render/render_path_paint_picker.c ----
void           context_set_fill_type(i32 type);
void           script_camera_turn(f32 x, f32 y);
void           script_set_stencil(gpu_texture_t *t);
void           script_set_text(char *text);
void           script_set_blur_type(i32 type);
void           script_camera_save();
bool           paint_asset_test_roundtrip(gpu_texture_t *t);
void           script_camera_restore();
void           script_set_clone_delta(f32 x, f32 y);
void           script_set_uv_type(i32 uv);
vec4_t         uniforms_decal_mask();
char          *render_path_paint_uv_target();
void           render_path_paint_ref_invalidate();
extern bool    render_path_paint_ref_taken;
bool           render_path_paint_fill_picks();
void           render_path_paint_fill_pick_now();
extern mesh_object_t *render_path_paint_fill_object;
extern i32            render_path_paint_fill_tri;
void           render_path_paint_picker_poll();
void           render_path_paint_picker_read(bool is_mask);

// ---- render/render_path_paint_cursor.c ----
void render_path_paint_commands_cursor();

// ---- render/render_path_paint_stroke.c ----
void render_path_paint_set_stamp_seed(i32 seed);
void render_path_paint_test_set_taper(f32 start, f32 end, f32 size, f32 opacity);
bool render_path_paint_filter_on();
void render_path_paint_filter_apply(render_path_paint_filter_t *f, f32 *x, f32 *y, f32 dt);
f32  render_path_paint_spacing();
bool render_path_paint_stamps();
void render_path_paint_tail_flush();
void render_path_paint_tail_drop();
void render_path_paint_stroke_end();
void render_path_paint_commands_stamps();
bool render_path_paint_use_samples();
void render_path_paint_samples_reset();
void render_path_paint_commands_samples();
void render_path_paint_commands_samples_stamps();

// ---- render/render_path_paint_2d.c ----
void render_path_paint_set_plane_mesh();
void render_path_paint_restore_plane_mesh();

// ---- render/render_path_preview.c ----
void render_path_preview_init();
void render_path_preview_commands_preview();
void render_path_preview_commands_decal();

// ---- render/render_path_raytrace.c ----
void           render_path_raytrace_set_env_pixels(char *file, buffer_t *pixels, i32 w, i32 h);
gpu_texture_t *render_path_raytrace_update_env_cdf();
void           render_path_raytrace_init();
void           render_path_raytrace_draw_overrides(bool all);
void           render_path_raytrace_setup(char *shader_name, bool build);
void           render_path_raytrace_draw(bool use_live_layer);

// ---- render/render_path_raytrace_bake.c ----
bool render_path_raytrace_bake_commands(void (*parse_paint_material)(bool));

// ---- render/render_path_unlit.c ----
void render_path_unlit_commands();

// ---- render/render_pathsphere.c ----
void render_pathsphere();

// ---- slots/slot_brush.c ----
char         *slot_brush_unique_name(ui_node_canvas_t *self, char *name);
slot_brush_t *slot_brush_create(ui_node_canvas_t *c);

// ---- slots/slot_font.c ----
slot_font_t *slot_font_create(char *name, draw_font_t *font, char *file);

// ---- slots/slot_layer.c ----
char                 *blend_type_name(blend_type_t type);
void                  slot_layer_alloc_textures(slot_layer_t *raw);
char                 *slot_layer_unique_name(slot_layer_t *self, char *name);
slot_layer_t         *slot_layer_create(char *ext, layer_slot_type_t type, slot_layer_t *parent);
slot_layer_t         *slot_layer_find_uid(u32 uid); // Layers of the project, NULL if none has it
void                  slot_layer_delete(slot_layer_t *raw);
void                  slot_layer_unload(slot_layer_t *raw);
void                  slot_layer_swap(slot_layer_t *raw, slot_layer_t *other);
void                  slot_layer_clear(slot_layer_t *raw, i32 base_color, gpu_texture_t *base_image, f32 occlusion, f32 roughness, f32 metallic);
void                  slot_layer_invert_mask(slot_layer_t *raw);
void                  layers_apply_mask(slot_layer_t *l, slot_layer_t *m);
void                  layers_apply_mask_pass(gpu_texture_t *dst, gpu_texture_t *temp, gpu_texture_t *mask_tex, f32 opac);
void                  slot_layer_apply_mask(slot_layer_t *raw);
slot_layer_t         *slot_layer_duplicate(slot_layer_t *raw);
slot_layer_t         *slot_layer_clone(slot_layer_t *raw);
void                  slot_layer_resize_and_set_bits(slot_layer_t *raw);
void                  slot_layer_to_fill_layer(slot_layer_t *raw, fill_t *f);
void                  slot_layer_to_paint_layer(slot_layer_t *raw);
bool                  slot_layer_is_visible(slot_layer_t *raw);
bool                  slot_layer_shows(slot_layer_t *raw);
bool                  slot_layer_is_descendant(slot_layer_t *raw, slot_layer_t *ancestor);
i32                   slot_layer_count_descendants(slot_layer_t *raw);
slot_layer_t_array_t *slot_layer_get_children(slot_layer_t *raw);
slot_layer_t_array_t *slot_layer_get_direct_children(slot_layer_t *raw);
slot_layer_t_array_t *slot_layer_get_masks(slot_layer_t *raw, bool include_group_masks);
bool                  slot_layer_has_masks(slot_layer_t *raw, bool include_group_masks);
slot_layer_t_array_t *slot_layer_get_filters(slot_layer_t *raw, bool include_group_filters);
bool                  slot_layer_has_visible_filter(slot_layer_t *raw);
f32                   slot_layer_get_opacity(slot_layer_t *raw);
i32                   slot_layer_get_object_mask(slot_layer_t *raw);
i32                   slot_layer_get_blending(slot_layer_t *raw);
bool                  slot_layer_is_layer(slot_layer_t *raw);
bool                  slot_layer_is_path(slot_layer_t *raw);
bool                  slot_layer_is_group(slot_layer_t *raw);
bool                  slot_layer_is_filter(slot_layer_t *raw);
bool                  slot_layer_is_mask(slot_layer_t *raw);
bool                  slot_layer_is_group_mask(slot_layer_t *raw);
bool                  slot_layer_is_layer_mask(slot_layer_t *raw);
bool                  slot_layer_can_move(slot_layer_t *raw, i32 to);
void                  slot_layer_relocate(slot_layer_t *raw, i32 to);
void                  slot_layer_move(slot_layer_t *raw, i32 to);

// ---- slots/slot_material.c ----
char            *slot_material_unique_name(ui_node_canvas_t *self, char *name);
slot_material_t *slot_material_create(shader_data_t *m, ui_node_canvas_t *c);
slot_material_t *slot_material_find_uid(u32 uid);
u32              project_uid_take(); // Next stable uid of a layer or material (slot_layer_t.uid, slot_material_t.uid)
char            *project_uuid_get(); // Random 128-bit id of the project in hex, names its history journal
void             project_uuid_new();
void             slot_material_unload(slot_material_t *raw);
void slot_material_delete(slot_material_t *raw);
void slot_material_delete_unused();
void slot_material_new_on_next_frame(void *_);
bool slot_material_is_plain(slot_material_t *m);
void slot_material_apply_picked_color(swatch_color_t *swatch);
void slot_material_color_to_picked(slot_material_t *m);
void slot_material_new_from_swatch(swatch_color_t *swatch);
void slot_material_refresh();
void slot_material_duplicate(i32 i);
bool slot_material_can_remove(slot_material_t *m);
bool slot_material_remove(slot_material_t *m);

// ---- app_api_bake.c ----
void           app_bake_update();
bool           app_bake_running();

// ---- ui/box_export.c ----
void box_export_tiled_update(void *_);
void box_export_fetch_presets();
void box_export_parse_preset();
char *box_export_preset_to_json(export_preset_t *p);
void box_export_show_textures();
void box_export_show_bake_material();
void box_export_show_mesh();

// ---- ui/box_import_mesh.c ----
void project_import_mesh_box(char *path, bool replace_existing, bool clear_layers, bool keep_camera, void (*done)(void));

// ---- ui/box_projects.c ----
char *box_projects_recent_path(i32 i);
void  box_projects_cancel_load();
void  box_projects_load(char *path);
void  base_opening_begin(char *path);
void  base_opening_end();
void  box_projects_delete_files(char *path);
char *box_projects_path_of(char *path);
void  box_projects_delete(i32 i);
void  box_projects_new();
void  box_projects_import();
void  box_projects_show();
void  box_projects_recent_tab();

// ---- ui/ui_base.c ----
void                  ui_base_init_on_next_frame(void *_);
bool                  ui_base_picker_button();
void                  ui_base_init();
void                  ui_base_menu_draw_viewport_mode();
void                  ui_base_update_next_frame(void *_);
void                  ui_base_update(void *_);
void                  ui_base_show_material_nodes();
void                  ui_base_show_2d_view(view_2d_type_t type);
i32                   ui_base_view_mode();
void                  ui_base_cycle_view_mode();
i32                   ui_base_panel_shadow();
void                  ui_base_fill_round(f32 x, f32 y, f32 w, f32 h, f32 r);
bool                  ui_base_panel_window(ui_window_t *handle, i32 x, i32 y, i32 w, i32 h, i32 corners, bool shadow_left);

// ---- ui/ui_box.c ----
void ui_box_render();
void ui_box_show_message(char *title, char *text, bool copyable);
void ui_box_show_custom(void (*commands)(void), i32 mw, i32 mh, void (*on_hide)(void), bool draggable, char *title);
void ui_box_hide();

// ---- ui/ui_dock.c ----
void ui_dock_layout();
void ui_dock_reset();
void ui_dock_show_view(dock_view_t view, bool show);
i32  ui_dock_view3d_w();
i32  ui_dock_view3d_h();
void ui_dock_load();

// ---- ui/ui_files.c ----
void  ui_files_show(char *filters, bool is_save, bool open_multiple, void (*files_done)(char *));
char *ui_files_save_preset(char *folder, char *name, char *json);

// ---- ui/ui_id_mask.c ----
asset_t *project_id_map_asset();
bool     project_is_id_map(asset_t *a);
bool     project_set_id_map(asset_t *a);
void     project_id_map_removed(asset_t *a);
void     ui_id_mask_begin();
void     ui_id_mask_begin_effect(slot_layer_t *m, i32 i);
void     ui_id_mask_cancel();
void     ui_id_mask_toggle(u32 color);
i32      ui_id_mask_selected();
void     ui_id_mask_done();
void     ui_id_mask_update();
i32      ui_id_mask_part_count();
u32      ui_id_mask_part_color(i32 i);
char    *ui_id_mask_part_label(i32 i);
bool     ui_id_mask_part_picked(i32 i);
u32      ui_id_mask_pick_version();
i32      ui_id_mask_pick_max();

// ---- ui/ui_menu.c ----
i32  ui_menu_padding();
void ui_menu_render();
void ui_menu_draw(void (*commands)(void), i32 x, i32 y);
void ui_menu_separator();
bool ui_menu_button(char *text, char *label, icon_t icon);
bool ui_icon_button(char *text, icon_t icon, ui_align_t align);
void ui_menu_begin();
void ui_menu_end();

// ---- history_tiles.c ----
extern i32      history_tiles_size;
extern i32      history_tiles_block;
extern i32      history_tiles_blocks;
i32             history_tiles_test_memory_kb();
i32             history_tiles_test_steps();
void            history_tiles_source_set(gpu_texture_t *base, gpu_texture_t *nor, gpu_texture_t *pack);
bool            history_tiles_supported(slot_layer_t *l);
i64             history_tiles_memory();
void            history_tiles_reset();
void            history_tiles_release(history_step_t *step);
bool            history_tiles_valid(history_step_t *step, slot_layer_t *l);
void            history_tiles_keep_newest(i32 steps);
void            history_tiles_finalize();
void            history_tiles_begin(history_step_t *step, slot_layer_t *l);
history_step_t *history_tiles_pending_step(slot_layer_t **layer);
gpu_texture_t  *history_tiles_stroke_source(i32 i);
void            history_tiles_swap(history_step_t *step, slot_layer_t *l);
void            history_tiles_trim();

// ---- ui/ui_quick_menu.c ----
extern bool ui_quick_menu_show;
void        ui_quick_menu_brush_used(i32 id);
char       *ui_quick_menu_item_name(i32 item);
void        ui_quick_menu_open(f32 x, f32 y);
void        ui_quick_menu_close();
bool        ui_quick_menu_blocking();
void        ui_quick_menu_update();
void        ui_quick_menu_render();

// ---- ui/ui_nodes.c ----
ui_nodes_editor_state_t *ui_nodes_editor_state(ui_node_t *node);
void                     ui_nodes_set_menu_path(char *node_type, char *path);
char                    *ui_nodes_get_menu_path(char *node_type);
void                     ui_nodes_wrap_mouse(bool controls_down, f32 x, f32 y, f32 w, f32 h);
ui_canvas_control_t     *ui_nodes_get_canvas_control(bool controls_down, bool is_node_view);
gpu_texture_t           *ui_nodes_draw_grid(f32 zoom);
void                     ui_nodes_edit_material(slot_material_t *m); // ui/ui_nodes.c: the node editor on m, with Save and Cancel
void                     ui_nodes_update_rect();
void                     ui_nodes_update(void *_);
void                     ui_nodes_render_popup();
void                     ui_nodes_accept_asset_drop(i32 index);
void                     ui_viewnodes_init();

// ---- ui/ui_brush_creator.c ----
bool ui_brush_creator_shown();
void ui_brush_creator_redraw();
void ui_brush_creator_new(i32 group);
void ui_brush_creator_edit(brush_def_t *d);
void ui_brush_creator_close(bool save);
void ui_brush_creator_render_ui();

// ---- ui/ui_material_studio.c ----
extern bool      material_studio_show;
slot_material_t *material_studio_new();
void             render_path_preview_commands_studio();
void             material_studio_apply();
bool             material_studio_can_open(slot_material_t *m);
void             material_studio_open(slot_material_t *m);
void             material_studio_close();
bool            material_studio_busy();
void             material_studio_save();
void             material_studio_cancel();
bool             material_studio_is_open();
gpu_texture_t   *material_studio_preview();
f32              material_studio_time(i32 which);
void             material_studio_edit_nodes();
i32              material_studio_compiles();
void             material_studio_preset(i32 i);
i32              material_studio_add_layer(i32 preset);
void             material_studio_remove_layer(i32 k);
void             material_studio_set_pattern(i32 k, char *type);
void             material_studio_set_mask(i32 k, i32 mask);
void             material_studio_set_value(i32 k, i32 value, f32 v);
f32              material_studio_get_value(i32 k, i32 value);
void             material_studio_set_input(i32 k, i32 role, i32 i, f32 v);
f32              material_studio_get_input(i32 k, i32 role, i32 i);
void             material_studio_set_button(i32 k, i32 role, i32 i, f32 v);
i32              material_studio_layers();
void             material_studio_set_color(i32 k, i32 i, f32 r, f32 g, f32 b);
void             ui_material_studio_render_ui();

// ---- ui/ui_search.c ----
void ui_base_operator_search_menu_draw();
void ui_base_operator_search();

// ---- ui/ui_view2d.c ----
void          ui_view2d_init();
void          ui_view2d_tex_rect(f32 aspect, f32 *x, f32 *y, f32 *w, f32 *h);
f32           ui_view2d_world_scale();
slot_layer_t *ui_view2d_move_layer(i32 *dx, i32 *dy);
void          ui_view2d_update(void *_);

// ---- util/util_brush.c ----
void   util_brush_render_pick_loupe();
f32    util_brush_pen_pressure();
bool   util_brush_pen_active();
f32    util_brush_pen_tilt();
f32    util_brush_pen_azimuth();
f32    util_brush_tip_angle();
vec2_t util_brush_tilt_shape();
bool   util_brush_shaped();
f32    util_brush_dynamic(f32 pressure, f32 tilt, bool grows);
void   util_brush_update();

// ---- util/util_clone.c ----
ui_node_socket_t_array_t *util_clone_canvas_sockets(ui_node_socket_t_array_t *sockets);
ui_node_t                *util_clone_canvas_node(ui_node_t *n);
ui_node_canvas_t         *util_clone_canvas(ui_node_canvas_t *c);
swatch_color_t           *util_clone_swatch_color(swatch_color_t *s);

// ---- util/util_cursor.c ----
void util_cursor_draw_2d(f32 cx, f32 cy, f32 radius, i32 color);
void util_cursor_render(void *_);

// ---- util/util_encode.c ----
buffer_t *util_encode_scene(scene_t *raw);
buffer_t  *util_encode_project(project_t *raw);
project_t *util_decode_project(buffer_t *b);

// ---- util/util_env.c ----
void env_begin(i32 w, i32 h, f32 r, f32 g, f32 b);
void env_add_lobe(f32 dx, f32 dy, f32 dz, f32 power, f32 r, f32 g, f32 b);
void env_add_disc(f32 dx, f32 dy, f32 dz, f32 deg, f32 irradiance, f32 r, f32 g, f32 b);
void env_write(char *path);

// ---- util/util_layer.c ----
bool                 layers_room(i32 n);
i32                  layers_test_set_max(i32 n);
gpu_texture_format_t layers_format(texture_bits_t bits);
gpu_pipeline_t      *layers_copy_pipe(texture_bits_t bits);
void                 util_layer_update_preview();
void                 layers_thumb_queue(slot_layer_t *l);
void                 layers_thumb_set(slot_layer_t *l, const u8 *rgba, i32 size);
void                 layers_thumbs_flush(void);
bool                 layers_thumbs_busy(void);
bool                 app_reads_busy(void);
bool                 mm_buffers_keys_read_cpu(u64 *keys, i32 n);
bool                 fill_tiles_loading(void);
typedef struct disk_cache_job disk_cache_job_t;
disk_cache_job_t    *disk_cache_read(const char *sub, u64 key);
bool                 disk_cache_job_done(disk_cache_job_t *j);
u8                  *disk_cache_job_take(disk_cache_job_t *j, u32 *len);
void                 disk_cache_job_drop(disk_cache_job_t *j);
void                 disk_cache_write(const char *sub, u64 key, u8 *data, u32 len, u32 w, u32 bpt);
bool                 disk_cache_has(const char *sub, u64 key);
void                 layers_init();
void                 layers_resize();
void                 layers_resize_targets();
void                 layers_set_bits();
void                 layers_test_set_bits(i32 bits);
void                 layers_make_temp_img();
void                 layers_make_temp_mask_img();
void                 layers_make_export_img();
void layers_commands_merge_pack(gpu_pipeline_t *pipe, gpu_texture_t *i0, gpu_texture_t *i1, gpu_texture_t *i1pack, f32 i1mask_opacity, gpu_texture_t *i1texmask,
                                i32 i1blending, gpu_texture_t *temp);
void layers_enable_translucency();
void layers_set_object_mask();
void layers_history_with_prev(void (*push)(void));
bool layers_sync_nodes();
slot_layer_t_array_t *layers_init_layer_map();
void                  layers_remap_layer_pointers(slot_layer_t_array_t *old_layers);

// ---- util/util_layer_create.c ----
slot_layer_t *layers_new_layer(bool clear, i32 position, slot_layer_t *parent);
slot_layer_t *layers_new_mask(bool clear, slot_layer_t *parent, i32 position);
slot_layer_t *layers_new_group();
slot_layer_t *layers_new_path_layer(bool curved);
slot_layer_t *layers_new_text_layer();
void          layers_create_filter();
void          layers_create_image_mask(asset_t *asset);
void          layers_create_image_layer(asset_t *asset);
void          layers_create_image_at(i32 i, bool mask);
void          layers_create_color_layer(i32 base_color, f32 occlusion, f32 roughness, f32 metallic, i32 position);
bool          layers_duplicate_layer(slot_layer_t *l);

// ---- util/util_layer_ops.c ----
slot_layer_t *layers_op_get(char *name);
void          layers_op_set(slot_layer_t *l);
slot_layer_t *layers_op_new(void);
slot_layer_t *layers_op_new_color(i32 base_color, f32 occlusion, f32 roughness, f32 metallic);
slot_layer_t *layers_op_new_fill(void);
void          layers_op_delete(slot_layer_t *l);
i32           layers_op_count(void);
slot_layer_t *layers_op_at(i32 i);
i32           layers_op_index(slot_layer_t *l);
slot_layer_t *layers_op_new_mask(slot_layer_t *l, i32 type);
slot_layer_t *layers_op_new_group(slot_layer_t *l);
void          layers_op_set_name(slot_layer_t *l, char *name);
void          layers_op_update(slot_layer_t *l);
bool          layers_op_move(slot_layer_t *l, i32 to);
bool          layers_op_set_group(slot_layer_t *l, slot_layer_t *group);
void          layers_op_set_object(slot_layer_t *l, object_t *object);
void          layers_op_end(draw_saved_t g);
slot_layer_t *layers_op_duplicate(slot_layer_t *l);
slot_layer_t *layers_op_merge_down(slot_layer_t *l);
slot_layer_t *layers_op_merge_group(slot_layer_t *group);
void          layers_op_clear(slot_layer_t *l);
void          layers_op_fill_paint(slot_layer_t *l);
bool          layers_op_invert_mask(slot_layer_t *m);
slot_layer_t *layers_op_apply_mask(slot_layer_t *m);
bool          layers_op_to_fill(slot_layer_t *l);
bool          layers_op_to_paint(slot_layer_t *l);
slot_layer_t *layers_op_new_path(bool curved);
slot_layer_t *layers_op_new_text(char *text);
bool          layers_op_add_path_point(slot_layer_t *l, f32 x, f32 y);
void          layers_op_repaint_path(slot_layer_t *l);
void layers_delete(slot_layer_t *l);
bool layers_can_delete(slot_layer_t *l);
bool layers_can_merge_down(slot_layer_t *l);
void layers_op_fill_material(slot_layer_t *l, slot_material_t *m);
slot_layer_t *layers_fill_target(slot_layer_t *l);
void          layers_repaint_text(slot_layer_t *l);
void          layers_toggle_visible(slot_layer_t *l);
bool          layers_has_children(slot_layer_t *l);
void          layers_export_on_file_picked(char *path);
bool          layers_mask_has_stack(slot_layer_t *l);

// ---- util/util_layer_fill.c ----
bool    layers_is_path_material();
void    layers_update_path_layers();
void    layers_update_linked_layers();
void    layers_fill_with_material(slot_layer_t *l);
bool    layers_fill_paint_to_mask();
bool    layers_fill_paint_needs_mask();
void    layers_fill_paint_confirm_mask();
bool    layers_fill_paint_tool(i32 tool);
fill_t *layers_new_fill_layer_fill(void);
mat4_t  layers_default_decal_box(void);
void    layers_create_fill_layer(i32 projection, mat4_t decal_mat, i32 position, bool from_material);

// ---- util/util_fill.c ----
extern vec4_t    fill_uv_tile;
void             fill_source_reset(fill_source_t *s, i32 c, i32 kind);
void             fill_source_set_kind(fill_source_t *s, i32 c, i32 kind);
void             fill_pattern_params_reset(fill_source_t *s);
fill_t          *fill_create(i32 projection);
fill_t          *fill_copy(fill_t *f);
fill_t          *fill_from_material(slot_material_t *m, i32 projection);
void             fill_set_material(fill_t *f, slot_material_t *m);
slot_material_t *fill_source_material(fill_source_t *s);
void             fill_stand_in_set(u32 uid, slot_material_t *m);
asset_t         *fill_source_asset(fill_source_t *s);
slot_material_t *fill_main_material(fill_t *f);
bool             fill_uses_material(fill_t *f, slot_material_t *m);
void             fill_material_removed(slot_material_t *m);
f32_array_t     *fill_encode(fill_t *f, bool by_index);
fill_t          *fill_decode_with(f32_array_t *a, bool by_index, slot_material_t_array_t *materials);
fill_t          *fill_decode(f32_array_t *a, bool by_index);
u64              fill_shape(slot_layer_t *l);
void             fill_changed(slot_layer_t *l);
void             fill_reshaped(slot_layer_t *l);
gpu_texture_t   *fill_params_texture(void);
void             fill_params_invalidate(void);
gpu_texture_t   *fill_image_texture(i32 asset_id);
gpu_texture_t   *fill_tile_texture(u32 uid, i32 k);
void             fill_tile_invalidate(slot_material_t *m);
i32              fill_tiles_mm_keys(u64 *out, i32 max);
void             fill_reset(void);
bool             fill_render(slot_layer_t *l, gpu_texture_t **out, const f32 *uv_rect);
void             fill_rasterize(slot_layer_t *l);
void             fill_ensure(slot_layer_t *l);
void             fill_ensure_all(void);
void             fill_update(void);
i32              fill_stat(i32 what);
char            *fill_test_shader(i32 id);

// ---- render/make_fill.c ----
char                  *make_fill_image_texture(i32 asset_id);
char                  *make_fill_tile_texture(u32 material_uid, i32 k);
void                   make_fill_declare(node_shader_t *shader, slot_layer_t *l);
void                   make_fill_uses(bool *emis, bool *height);
i32                    make_fill_texture_count(slot_layer_t *l);
node_shader_context_t *make_fill_raster_run(material_t *data, slot_layer_t *l, char *format);
node_shader_context_t *make_fill_tile_run(material_t *data, slot_material_t *m, char *format);

// ---- util/util_layer_merge.c ----
void           layers_merge_down();
slot_layer_t  *layers_merge_group(slot_layer_t *l);
void           layers_merge_layer(slot_layer_t *l0, slot_layer_t *l1, bool use_mask);
void           layers_merge_layer_ex(slot_layer_t *l0, slot_layer_t *l1, bool use_mask, gpu_texture_t *temp);
gpu_pipeline_t *layers_pipe_for(gpu_pipeline_t *base, gpu_texture_format_t format);
void           layers_merge_mask(gpu_texture_t *dst, slot_layer_t *m);
void           layers_merge_mask_ex(gpu_texture_t *dst, gpu_texture_t *temp, slot_layer_t *m, gpu_texture_t *mask_tex);
gpu_texture_t *layers_height_below(slot_layer_t *l);
slot_layer_t  *layers_udim_remap(slot_layer_t *l, gpu_texture_t **mask, i32 slot, bool to_tile);
i32            layers_udim_tile_slot(slot_layer_t *l);
slot_layer_t  *layers_flatten(bool height_to_normal, slot_layer_t_array_t *layers);
f32            layers_bump_scale(i32 res);
gpu_texture_format_t layers_pack_format(texture_bits_t bits);
void           layers_raise(gpu_texture_t *pack, gpu_texture_t *temp, gpu_texture_t *texpaint, gpu_texture_t *mask, f32 opacity, f32 raise);
void           layers_height_to_normal(gpu_texture_t *nor, gpu_texture_t *temp, gpu_texture_t *pack, f32 scale);

// ---- util/util_mask_stack.c ----
mask_effect_param_t *mask_effect_params(i32 type, i32 *count);
mask_effect_param_t *mask_effect_param_at(i32 type, i32 index);
f32                  mask_effect_param_clamp(i32 type, i32 index, f32 value);
bool                 slot_layer_takes_mask(slot_layer_t *l);
char                *mask_effect_name(i32 type);
char                *mask_blend_name(i32 blending);
bool                 mask_effect_is_filter(i32 type);
bool                 mask_effect_is_generator(i32 type);
bool                 mask_effect_is_rendered(mask_effect_t *e);
bool                 mask_effect_rendered_as(mask_effect_t *e, mask_effect_t *out);
bool                 mask_effect_reads_below_whole(mask_effect_t *e);
i32                  mask_effect_fill_source(mask_effect_t *e);
asset_t             *mask_effect_image(mask_effect_t *e);
bool                 mask_stack_uses_asset(i32 index);
void                 mask_stack_asset_removed(i32 index);
mask_effect_t        mask_effect_default(i32 type);
i32                  mask_stack_count(slot_layer_t *m);
mask_effect_t        mask_stack_get(slot_layer_t *m, i32 i);
void                 mask_stack_materialize(slot_layer_t *m);
void                 mask_stack_pack(f32 *f, mask_effect_t e); // e into MASK_EFFECT_STRIDE floats
void                 mask_stack_set(slot_layer_t *m, i32 i, mask_effect_t e);
i32                  mask_stack_paint_index(slot_layer_t *m);
bool                 mask_stack_is_trivial(slot_layer_t *m);
f32_array_t         *mask_stack_copy_effects(f32_array_t *effects);
i32                  mask_stack_add(slot_layer_t *m, i32 type, i32 index);
bool                 mask_stack_remove(slot_layer_t *m, i32 i);
bool                 mask_stack_can_move(slot_layer_t *m, i32 from, i32 to);
void                 mask_stack_move(slot_layer_t *m, i32 from, i32 to);
void                 mask_stack_mark_dirty(slot_layer_t *m);
gpu_texture_t       *mask_stack_get_scratch(i32 w, i32 h);
void                 mask_stack_layer_changed(slot_layer_t *l);
bool                 slot_layer_alive(slot_layer_t *l);
slot_layer_t        *mask_stack_layer_by_id(i32 id);
f32_array_t         *mask_stack_effects_to_file(f32_array_t *effects, slot_layer_t **layers, i32 count, bool images);
void                 mask_stack_effects_from_file(f32_array_t *effects, slot_layer_t **layers, i32 count);
slot_layer_t        *mask_effect_builder_layer(mask_effect_t *e);
bool                 mask_stack_reads(slot_layer_t *m, slot_layer_t *target);
bool                 mask_stack_has_height_blend(slot_layer_t *m);
f32                  mask_effect_blur_radius(mask_effect_t *e);
gpu_texture_t       *slot_layer_mask_texture(slot_layer_t *m);
void                 mask_stack_evaluate_dirty();
void                 mask_stack_evaluate(slot_layer_t *m);
bool                 mask_stack_blur_prefix(slot_layer_t *m, i32 index, gpu_texture_t **out);
void                 mask_stack_evaluate_tile(slot_layer_t *m, gpu_texture_t *dst, gpu_texture_t *scratch, gpu_texture_t *gen_out, gpu_texture_t *gen_scratch, f32 *rect,
                                              gpu_texture_t *(*src)(slot_layer_t *, mask_effect_t *, i32, i32, void *), void *data);
slot_layer_t        *mask_stack_anchor_target(slot_layer_t *m, mask_effect_t *e, i32 *kind);
void                 mask_stack_free_texture(slot_layer_t *m);

// ---- util/util_mask_generators.c ----
void           mask_gen_mesh_changed();
i32            mask_gen_renders();
bool           mask_gen_stale(slot_layer_t *m);
void           mask_gen_update(slot_layer_t *m);
gpu_texture_t *mask_gen_texture(slot_layer_t *m, mask_effect_t e);
void           mask_gen_render_rect(slot_layer_t *m, mask_effect_t e, gpu_texture_t *scratch, gpu_texture_t *dst, f32 *rect);
void           mask_gen_free(slot_layer_t *m);
void           mask_gen_trim();

// ---- util/util_mesh.c ----
void                   util_mesh_remove_merged();
mesh_object_t_array_t *util_mesh_get_visible();
mesh_object_t_array_t *util_mesh_get_hierarchy(mesh_object_t *o);
i32                    util_mesh_data_owner(mesh_data_t *data);
char                  *util_mesh_link_name(i32 source_index, char *object_name);
bool                   util_mesh_link_parse(char *name, i32 *source_index, char **object_name);
i32                    util_mesh_udim_tile(char *name);
bool                   util_mesh_udim_active();
i32                    util_mesh_udim_slot(i32 tile);
bool                   util_mesh_udim_layer(slot_layer_t *l);
i32                    util_mesh_atlas_stride();
i32                    util_mesh_atlas_slot(object_t *object);
void                   util_mesh_delete_data_uncache(void *data);
void                   util_mesh_remove_objects(mesh_object_t_array_t *objects, mesh_object_t *keep);
bool                   util_mesh_merged_partial();
void                   util_mesh_merge(mesh_object_t_array_t *paint_objects);
bool                   util_mesh_merge_refresh();
void                   util_mesh_transform_changed();
void                   util_mesh_visibility_changed();
void                   util_mesh_uv_unwrap(mesh_object_t_array_t *objects);
void                   util_mesh_sort_hierarchy();
u32_array_t           *util_mesh_connected_indices(mesh_data_t *g, i32 tri);
i32                    util_mesh_raycast_triangle(mesh_object_t *o, ray_t *ray);
u32_array_t           *util_mesh_uv_island_indices(mesh_data_t *g, i32 tri, i32 uv_map);
vec4_t                 util_mesh_triangle_normal(mesh_object_t *o, i32 tri);
f32                    util_mesh_raycast_triangle_dist(mesh_object_t *o, ray_t *ray, i32 tri);

// ---- util/util_mesh_maps.c ----
char          *mesh_map_name(i32 type);
char          *mesh_map_suffix(i32 type);
i32            mesh_map_from_file(char *path);
asset_t       *mesh_maps_get_asset(i32 type);
void           mesh_maps_set(i32 type, asset_t *asset);
void           mesh_maps_asset_removed(asset_t *asset);
void           mesh_maps_texture_imported(asset_t *asset);
gpu_texture_t *mesh_maps_auto_curvature();
gpu_texture_t *mesh_maps_texture(i32 type);
void           mesh_maps_preview_neutral(bool on);
void           mesh_maps_mesh_changed();
void           mesh_maps_update();
u64            mesh_maps_generation();
gpu_texture_t *mesh_maps_texture_model(i32 type);

// ---- util/util_nodes.c ----
void                  ui_nodes_get_linked_nodes(ui_node_t_array_t *linked_nodes, ui_node_t *n, ui_node_canvas_t *c);
node_group_t_array_t *ui_nodes_groups();
node_group_t         *ui_nodes_find_group(char *group_name);
bool                  ui_nodes_group_in_use(node_group_t *group);
bool                  ui_nodes_contains_node_group_recursive(node_group_t *group, char *group_name);
bool                  ui_nodes_can_place_group(char *group_name);
gpu_texture_t        *ui_nodes_get_node_preview_image(ui_node_t *n);
bool                  ui_nodes_has_group(ui_node_canvas_t *c);
ui_node_canvas_t     *ui_nodes_get_group(ui_node_canvas_t_array_t *canvases, char *name);
void                  ui_nodes_traverse_group(ui_node_canvas_t_array_t *mgroups, ui_node_canvas_t *c);
i32                   ui_nodes_get_node_x();
i32                   ui_nodes_get_node_y();
node_list_t_array_t  *ui_nodes_get_node_list();
string_array_t       *ui_nodes_get_categories();
ui_node_canvas_t     *ui_nodes_get_canvas(bool groups);
ui_nodes_t           *ui_nodes_get_nodes();
void                  ui_nodes_canvas_changed();
void                  ui_nodes_push_undo(ui_node_canvas_t *last_canvas);
void                  ui_viewnodes_on_node_remove(ui_node_t *n);
void                  ui_viewnodes_on_node_changed(ui_node_t *n);
void                  ui_nodes_capture_output();
void                  ui_viewnodes_on_canvas_capture_output(void *_);
void                  ui_viewnodes_on_canvas_released_duplicate(void *_);
void                  ui_viewnodes_on_canvas_delete_on_next_frame(void *_);
void                  ui_viewnodes_on_canvas_delete(void *_);
void                  ui_viewnodes_on_canvas_paste(void *_);
void                  ui_viewnodes_on_canvas_copy(void *_);
void                  ui_viewnodes_on_canvas_cut(void *_);
void                  ui_nodes_recompile();
ui_node_t            *ui_nodes_make_group_node(ui_node_canvas_t *group_canvas, ui_nodes_t *nodes, ui_node_canvas_t *canvas);
ui_node_t            *ui_nodes_make_group_node_from(ui_node_t *def, ui_node_canvas_t *group_canvas, ui_nodes_t *nodes, ui_node_canvas_t *canvas);
void                  ui_nodes_make_node_preview(ui_node_t *node);
ui_node_t            *ui_nodes_make_node(ui_node_t *n, ui_nodes_t *nodes, ui_node_canvas_t *canvas);

// ---- util/util_particle.c ----
void  util_particle_init_mesh();
void  util_particle_update();
f32   util_particle_radius(i32 i);
bool  util_particle_alive();

// ---- util/util_path.c ----
bool   util_layer_is_path_point_dragging();
vec4_t util_layer_path_segment_a();
vec4_t util_layer_path_segment_b();
f32    util_layer_path_length();
f32    util_layer_brush_screen_radius();
void   util_layer_clear_path_points(slot_layer_t *l);
void   util_layer_remove_path_point(slot_layer_t *l);
void   util_layer_add_path_point(slot_layer_t *l, f32 screen_x, f32 screen_y);
void   util_layer_repaint_path(slot_layer_t *l);
void   util_layer_check_path_grab();
void   util_layer_update_path();
bool   util_project_to_screen(vec4_t wpos, f32 *sx, f32 *sy);
void   util_camera_get(f32 *c);
void   util_camera_set(const f32 *c);

// ---- util/util_curve_stroke.c ----
bool util_curve_stroke_active();
i32  util_curve_stroke_count();
bool util_curve_stroke_previewing();
void util_curve_stroke_capture_begin();
void util_curve_stroke_capture_end();
bool util_curve_stroke_capture();
bool util_curve_stroke_touch(bool started);
void util_curve_stroke_render();
void util_curve_stroke_confirm();
void util_curve_stroke_discard();
bool util_curve_stroke_point(i32 i, f32 *x, f32 *y);
void util_curve_stroke_move(i32 i, f32 x, f32 y);
bool util_curve_stroke_remove(i32 i);
f32  util_curve_stroke_size(i32 i);
void util_curve_stroke_set_size(i32 i, f32 size);
f32  util_curve_stroke_width();
void util_curve_stroke_set_width(f32 width);
void util_curve_stroke_update();
bool util_curve_stroke_test_hold(f32_array_t *xyp);
f32  util_curve_stroke_test_point(i32 i, i32 axis);

// ---- util/util_primitives.c ----
raw_mesh_t  *geom_make_plane(f32 size_x, f32 size_y, i32 verts_x, i32 verts_y, f32 uv_scale);
raw_mesh_t  *geom_make_uv_sphere(f32 radius, i32 width_segments, i32 height_segments, bool stretch_uv, f32 uv_scale);
mesh_geo_t  *util_geo_create(i32 vertex_count, i32 index_count);
void         util_geo_free(mesh_geo_t *g);
mesh_data_t *nodes_mesh_geo_to_data(mesh_geo_t *g, char *name);
mesh_geo_t  *nodes_mesh_shape(i32 shape, f32 *size, i32 segments);
mesh_data_t *util_shape_make_data(char *name);

// ---- util/util_raycast.c ----
vec4_t raycast_aabb_mouse(object_t *object);
bool   point_in_aabb(object_t *object, vec4_t point);

// ---- util/util_render.c ----
extern gpu_buffer_t *util_render_screen_aligned_full_vb;
extern gpu_buffer_t *util_render_screen_aligned_full_ib;
void                 util_render_make_default_world();
bool                 util_render_is_default_envmap(char *file);
gpu_texture_t       *util_render_default_envmap();
buffer_t            *util_render_default_envmap_pixels();
void                 util_render_preview_lighting_begin(f32 ambient, f32 key);
void                 util_render_preview_lighting_end();
void                 util_render_make_material_preview();
void                 util_render_make_decal_preview();
void                 util_render_make_text_preview();
void                 util_render_make_font_preview();
void                 util_render_brush_scene_begin(util_render_brush_scene_t *sc);
void                 util_render_brush_scene_end(util_render_brush_scene_t *sc);
void                 util_render_make_brush_preview();
bool                 util_render_load_material_icon(slot_material_t *m, buffer_t *icon);
void                 util_render_set_material_preview(slot_material_t *m, gpu_texture_t *src);
void                 util_render_resize_asset_icons(void *_);
void                 util_render_create_screen_aligned_full_data();
void   util_render_make_node_preview(ui_node_canvas_t *canvas, ui_node_t *node, gpu_texture_t *image, ui_node_canvas_t *group, ui_node_t_array_t *parents);
void   util_render_pick_pos_nor_tex();
void   util_render_pick_pos_nor_tex_async(gpu_transfer_t *t, void (*done)(gpu_transfer_t *t, void *ud), void *ud);
extern gpu_transfer_t *util_render_pick_transfer;
bool   util_render_picked_on_mesh();

// ---- util/util_shortcut.c ----
void util_shortcut_undo_redo();
void util_shortcut_global();
void util_shortcut_brush();
void util_shortcut_viewport();

// ---- util/util_stencil.c ----
i32  util_stencil_handle_size();
void util_stencil_transform();

// ---- util/util_stroke_refine.c ----

// ---- util/util_material_cache.c ----
u64            material_cache_graph_signature(slot_material_t *m);
bool           material_cache_paint_info(bool is_atlas, bool *emis_used, bool *subs_used, bool *height_used); // util/util_material_cache.c
gpu_texture_t *material_cache_texture(i32 i);
void           material_cache_paint_check(void);
void           material_cache_reset(void);
bool           material_cache_building(void);
void           material_cache_mesh_changed(void);
void           material_cache_trim(void);
void           material_cache_set_enabled(i32 on);
void           material_cache_test_budget(i32 mb);
void           material_cache_test_min_ms(f32 ms);
i32            material_cache_stat(i32 what);
typedef struct material_cache_replay material_cache_replay_t;
material_cache_replay_t *material_cache_replay_get(hash128_t snap, i32 w, i32 h, const f32 *uv_rect);
i32                      material_cache_replay_build(material_cache_replay_t *e, const f32 *uv, bool sync);
void                     material_cache_replay_bind(material_cache_replay_t *e);
bool                     material_cache_replay_is_bound(void);
void                     material_cache_replay_release(void);
void                     material_cache_replay_set_enabled(i32 on);
i32                      material_cache_replay_stat(i32 what);

// ---- util/util_texture.c ----
char *util_texture_packed_name(char *basename);
void  util_copy_texture(gpu_texture_t *dst, gpu_texture_t *src, gpu_pipeline_t *pipe);
void  util_texture_capture_output(gpu_texture_t *img, char *basename);
bool  util_texture_ensure_target(gpu_texture_t **t, i32 w, i32 h, gpu_texture_format_t format);
void  util_texture_release_target(gpu_texture_t **t);
i64   util_texture_tracked_bytes();
i64   util_texture_tracked_peak();
void  util_texture_tracked_peak_reset();
void util_texture_delete(asset_t *asset);
void util_texture_delete_unused();
bool util_texture_is_used(i32 index);
char *util_texture_mesh_maps_of(asset_t *asset);

// ---- util/util_touch.c ----
void util_touch_update();

// ---- util/util_pencil.c ----
void util_pencil_on_gesture(int gesture, float x, float y);
void util_pencil_update();

// ---- util/util_ui.c ----
f32     ui_base_get_radius_increment();
rect_t *ui_base_get_brush_stencil_rect();
bool    ui_base_hit_rect(f32 mx, f32 my, i32 x, i32 y, i32 w, i32 h);
void    ui_base_make_empty_envmap(i32 col);
f32     ui_base_asset_scale();
i32     ui_base_asset_icon_size();
void    ui_base_set_icon_scale();
void    ui_base_set_viewport_col(i32 col);

// ---- util/util_uv.c ----
void util_uv_cache_uv_map();
void util_uv_cache_dilate_map();
void util_uv_cache_seam_map(i32 res_x, i32 res_y);
void util_uv_blur_seamless(gpu_texture_t *dst, gpu_texture_t *tmp, gpu_texture_t *src, i32 passes);
void util_uv_grow_seamless(gpu_texture_t *dst, gpu_texture_t *tmp, gpu_texture_t *src, i32 passes, bool shrink, bool square);

// ---- util/util_uv_unwrap.c ----
void util_uv_unwrap_run(raw_mesh_t *mesh);

// ---- args.c ----
void args_parse();
void args_run();

// ---- base.c ----
void            base_on_open_file(char *path);
bool            base_startup_covering();
bool            base_startup_loading();
void            base_startup_cover_done();
char           *base_ui_font_file();
void            base_init();
i32             base_w();
i32             base_view3d_w();
bool            base_is_split();
i32             base_view_index();
i32             base_view_w();
i32             base_view_x(i32 view_index);
i32             base_view_at_mouse();
i32             base_h();
bool            base_paint_in_2d();
i32             base_paint_view_x();
i32             base_paint_view_y();
i32             base_paint_view_w();
i32             base_paint_view_h();
f32             base_window_aspect();
f32             base_paint_view_aspect();
f32             base_scale2d(i32 h);
f32             base_paint_x(f32 screen_x);
f32             base_paint_y(f32 screen_y);
f32             base_paint_screen_x(f32 paint_x);
f32             base_paint_screen_y(f32 paint_y);
i32             base_x();
i32             base_y();
void            base_on_resize();
void            base_resize();
string_array_t *base_combo_enum_texts(char *node_type);
any_array_t    *base_combo_enum_textures(char *node_type);
i32             base_get_asset_index_by_file(char *file);
bool            base_is_decal_layer();
void            base_redraw_ui();
void            base_set_workspace(workspace_preset_t preset);
void            base_apply_workspace();
void            base_update_tools();
uint32_t        base_darker(uint32_t x, uint32_t y);
string_array_t *base_get_viewport_modes();
string_array_t *base_get_viewport_mode_shortcuts();
void base_draw_progress(f32 progress);

// ---- camera.c ----
void camera_roll(f32 radians, bool held);
void camera_turn(f32 x, f32 y, vec4_t pivot);
vec4_t camera_view_pivot();
void camera_init();
i32  camera_index();
void camera_update(void *_);
f32  camera_distance();
void camera_reset(i32 view_index);

// ---- config.c ----
void                   config_load();
void                   config_save();
void                   config_brush_save_update();
i32_array_t           *config_default_quick_menu_slots();
void                   config_init();
config_t              *config_from_json(char *s);
char                  *config_get_sha();
iron_window_options_t *config_get_options();
void                   config_restore();
void                   config_import_from(config_t *from);
void                   config_apply();
i32                    config_get_super_sample_quality(f32 f);
f32                    config_get_super_sample_size(i32 i);
void                   config_set_texture_res(i32 pos);
i32                    config_get_texture_res_x();
i32                    config_get_texture_res_y();
i32                    config_get_texture_res_pos(i32 i);
void                   config_load_theme(char *theme, bool tag_redraw);
bool                   config_is_iphone();
bool                   config_is_raytrace_fast();

// ---- library.c ----
gpu_texture_t   *library_entry_icon(library_entry_t *e);
void             library_rescan();
bool            library_busy();
void             library_init();
bool             library_has_folder();
char            *library_status();
i32              library_entry_count();
library_entry_t *library_entry_get(i32 i);
bool             library_entry_ready(library_entry_t *e);
bool             library_entry_broken(library_entry_t *e);
char            *library_entry_name(library_entry_t *e);
char            *library_entry_rel(library_entry_t *e);
void             library_use(library_entry_t *e);

// ---- builtin_brushes.c ----
gpu_texture_t *brush_tip_texture(i32 tip);

// ---- brush_library.c ----
brush_settings_t brush_settings_default();
void             brush_library_save();
i32              brush_library_count();
brush_def_t     *brush_library_at(i32 i);
brush_def_t     *brush_library_find(i32 id);
char            *brush_library_name(brush_def_t *d);
i32              brush_group_count();
brush_group_t   *brush_group_at(i32 i);
brush_group_t   *brush_group_find(i32 id);
char            *brush_group_name(brush_group_t *g);
brush_def_t     *brush_library_selected();
gpu_texture_t   *brush_library_tip_of(brush_def_t *d);
gpu_texture_t   *brush_library_tip();
void             brush_library_apply_settings(brush_settings_t *s, gpu_texture_t *tip_image);
void             brush_library_capture_settings(brush_settings_t *s);
gpu_texture_t   *brush_library_tip_image_get();
void             brush_library_apply(brush_def_t *d);
void             brush_library_applied();
void             brush_library_use(brush_def_t *d);
i32              brush_library_user_group();
brush_group_t   *brush_group_add(char *name);
void             brush_group_rename(brush_group_t *g, char *name);
void             brush_group_delete(brush_group_t *g);
brush_def_t     *brush_library_new_def();
void             brush_library_add(brush_def_t *d, gpu_texture_t *tip_image);
void             brush_library_set_tip(brush_def_t *d, gpu_texture_t *tip_image);
brush_def_t     *brush_library_duplicate(brush_def_t *d, i32 group);
void             brush_library_delete(brush_def_t *d);
gpu_texture_t   *brush_library_load_tip(char *path);

// ---- brush_pad.c ----
void brush_pad_clear();
void brush_pad_reset();
void brush_pad_changed();
void brush_pad_set_size(f32 preview_size);
void brush_pad_input(f32 x, f32 y, f32 w, f32 h);
void brush_pad_draw(f32 x, f32 y, f32 w, f32 h);
bool brush_pad_busy();

// ---- smart_materials.c ----
void           smart_materials_refresh();
i32            smart_materials_count();
char          *smart_materials_name(i32 i);
i32            smart_materials_find(char *name);
gpu_texture_t *smart_materials_icon(i32 i);
bool           smart_materials_save(slot_layer_t *root, char *name);
slot_layer_t  *smart_materials_insert(char *name);
slot_layer_t  *smart_materials_apply(char *name);
void           smart_materials_delete(i32 i);
void smart_materials_show_save();
slot_layer_t *smart_materials_save_target();

// ---- saved_materials.c ----
void           saved_materials_refresh();
i32            saved_materials_count();
char          *saved_materials_name(i32 i);
gpu_texture_t *saved_materials_icon(i32 i);
bool           saved_materials_save(slot_material_t *m);
bool           saved_materials_has(slot_material_t *m);
void           saved_materials_use(i32 i);
void           saved_materials_delete(i32 i);

// ---- brush_previews.c ----
void brush_previews_start();
void brush_previews_update();
i32  brush_previews_stat(i32 what);
void brush_previews_forget(bool delete_files);
void brush_previews_render_row(brush_settings_t *s, gpu_texture_t *tip, gpu_texture_t **dst);
void brush_previews_draw_stroke(gpu_texture_t *img, f32 x, f32 y, f32 w, f32 h);

// ---- console.c ----
void console_toast(char *s);
void console_info(char *s);
void console_error(char *s);
void console_alert(char *s);
void console_log(char *s);

// ---- context.c ----
void           context_init();
void           context_select_material(i32 i);
void           context_set_material(slot_material_t *m);
void           context_select_layer(i32 i);
void           context_set_layer(slot_layer_t *l);
void           context_pick_layer(slot_layer_t *l);
void           context_select_tool(i32 i);
void           context_init_tool();
void           context_parse_mesh_material();
void           context_select_paint_object(mesh_object_t *o);
mesh_object_t *context_main_object();
bool           context_layer_filter_used();
bool           context_object_mask_used();
bool           context_in_3d_view();
bool           context_in_paint_area();
bool           context_in_2d_view(view_2d_type_t type);
bool           context_in_nodes();
bool           context_is_pixel_snap();
slot_material_t *context_decal_material();
slot_material_t *context_user_material();
bool           context_decal_swap();
bool           context_is_decal();
bool           context_is_decal_mask();
bool           context_is_stencil_editing();
bool           context_is_stencil_hidden();
bool           context_is_ruler_paint();
bool           context_is_brush_camera_align();
bool           context_is_decal_mask_paint();
bool           context_is_decal_mask_paint_pass();
void           context_set_viewport_mode(viewport_mode_t mode);
void           context_set_camera_controls(int i);
void           context_load_envmap();
void           context_update_envmap();
void           context_set_viewport_shader(void *viewport_shader);
void           context_set_viewport_texture(gpu_texture_t *image);
void           context_set_render_path();
extern bool    context_script_touch;
bool           context_finger_paint_blocked();

// ---- history.c ----
void            history_flush_restores(slot_layer_t *l);
bool            history_busy();
i32             history_test_restore_runs();
i32             history_test_undos();
void            history_on_memory_warning();
void            history_drop_oldest();
void            history_step_dispose(history_step_t *step);
u32             history_serial_peek();
void            history_steps_replace(any_array_t *steps, i32 undos, i32 redos, u32 serial_next);
void            history_undo();
void            history_redo();
void            history_user_undo(); // An undo the user asked for: the UI notices it (history_notice_*)
void            history_user_redo();
i32             history_test_notice_serial();
bool            history_test_notice_redo();
char           *history_test_notice_name();
void            history_reset();
history_step_t *history_push(history_action_t action);

// ---- history_swap.c ----
slot_layer_t_array_t *history_merge_saved_layers();
void                  history_swap_merge(history_step_t *step, bool undo);
void                  history_records_take(history_step_t *step);
void                  history_records_copy(history_step_t *step, slot_layer_t_array_t *layers, i32 start, i32 end);
void                  history_record_children(const u8 *data, u32 len, void (*visit)(hash128_t child, void *ud), void *ud);
slot_layer_t         *history_record_layer(hash128_t record);
void                  history_records_free(history_step_t *step);
void                  history_swap_order(history_step_t *step);
slot_layer_t         *history_path_copy(slot_layer_t *l);
void                  history_path_free(slot_layer_t *p);
void                  history_swap_path(history_step_t *step, slot_layer_t *l);
void                  history_restore_applied_mask(history_step_t *step, slot_layer_t *m);
void                  history_swap_layer_props(history_step_t *step);
void                  history_new_swatch_slot(history_step_t *step);
void                  history_delete_swatch_slot(history_step_t *step);
void                  history_swap_swatches(history_step_t *step);
void                  history_swap_swatch(history_step_t *step);
node_group_t_array_t *history_groups(history_step_t *step);
void                  history_swap_canvas(history_step_t *step);

// ---- timeline.c ----
struct timeline;
struct tl_entry;
struct paint_target;
bool        timeline_layer_paints(slot_layer_t *l);
void        timeline_allow_fill(bool on);
void        timeline_record_apply_mask(slot_layer_t *l, slot_layer_t *m, struct timeline *src);
void        timeline_record_merge(slot_layer_t *l0, slot_layer_t *l1);
void        timeline_record_path(slot_layer_t *l);
void        timeline_truncate(struct timeline *tl);
void        timeline_truncate_all();
void        timeline_checkpoint_set(struct timeline *tl, i32 pos, hash128_t tilemap, bool pinned);
hash128_t   timeline_state(struct timeline *tl, i32 pos);
hash128_t   timeline_state_at(struct timeline *tl, i32 pos, i32 w, i32 h);
struct timeline *timeline_find_layer(u32 layer_uid);
bool        timeline_entry_replays(struct timeline *tl, i32 i);
i32         timeline_floor(struct timeline *tl, i32 pos);
bool        timeline_upscalable(struct timeline *tl, i32 pos);
gpu_pipeline_t *timeline_copy_pipe(gpu_texture_t *t);
bool        timeline_state_resample(hash128_t h, struct paint_target *t);
i32         timeline_tiles_restore_step(hash128_t map, struct paint_target *dst, i32 from, i32 batches);
bool        timeline_captures_pending();
void        timeline_target_live(slot_layer_t *l, struct paint_target *t);
bool        timeline_live_map_fresh(slot_layer_t *l);
hash128_t   timeline_live_map(slot_layer_t *l);
void        timeline_live_map_set(slot_layer_t *l, hash128_t h);
struct timeline *timeline_ensure(slot_layer_t *l);
struct tl_entry *timeline_record(slot_layer_t *l, i32 kind, hash128_t payload, const f32 *uv_bounds, u16 flags);
void        timeline_stroke_begin(history_step_t *step, slot_layer_t *l);
void        timeline_stroke_dirty(history_step_t *step, i32_array_t *tiles);
void        timeline_finalize();
bool        timeline_entry_replay(struct timeline *tl, i32 i, struct paint_target *t, bool fast);
void        timeline_step_apply(history_step_t *step, bool undo);
void        timeline_fork(slot_layer_t *src, slot_layer_t *dst);
bool        timeline_materialize_live(struct timeline *tl, slot_layer_t *l, i32 pos);
i32         timeline_test_count(slot_layer_t *l);
i32         timeline_test_kind(slot_layer_t *l, i32 i);
i32         timeline_test_cursor(slot_layer_t *l);
i32         timeline_test_states(slot_layer_t *l);
bool        timeline_test_materialize(slot_layer_t *l, i32 pos);
bool        timeline_test_render(slot_layer_t *l, i32 pos, i32 res_w, i32 res_h, f32 u0, f32 v0, f32 u1, f32 v1);
bool        timeline_test_replay_entry(slot_layer_t *l, i32 i);
bool        timeline_test_scale_live(slot_layer_t *l, i32 res_w, i32 res_h);
gpu_texture_t *timeline_test_out(i32 k);
i32         timeline_test_quadrants(slot_layer_t *l, i32 res, i32 tol);
i32         timeline_test_floor(slot_layer_t *l);
bool        timeline_test_upscalable(slot_layer_t *l);
void        timeline_test_keep(i32 slot);
void        timeline_set_keep_recent(i32 n);
i32         timeline_thin_all(i32 round);
void        timeline_clear_states_all();
i32         timeline_test_checkpoints(slot_layer_t *l);
gpu_texture_t *timeline_test_kept(i32 slot, i32 k);

// ---- timeline_ops.c ----
void timeline_retag_last(slot_layer_t *l, i32 kind, const void *params, u32 len, u16 flags);
void timeline_entry_upgrade(struct timeline *tl, i32 index, i32 kind, hash128_t payload);
void timeline_ops_children(const u8 *data, u32 len, void (*visit)(hash128_t child, void *ud), void *ud);
void timeline_record_fill(slot_layer_t *l);
void timeline_record_image(slot_layer_t *l, asset_t *asset);
bool timeline_ops_replays(struct timeline *tl, i32 index);
bool timeline_ops_overwrites(struct timeline *tl, i32 index);
void timeline_ops_reset();

// ---- timeline_tiles.c ----
void timeline_tiles_cache_reset();
bool timeline_tiles_info(hash128_t h, i32 *width, i32 *height, i32 *format, i32 *channels);
bool timeline_tiles_restore_from(hash128_t map, hash128_t prev, struct paint_target *dst);
bool timeline_tiles_uniform(hash128_t h);
bool timeline_tiles_base(hash128_t h, hash128_t base);
hash128_t timeline_tiles_rebase(hash128_t h, hash128_t base);
i32  timeline_tiles_test(slot_layer_t *l);
i32  timeline_tiles_test_reads();

// ---- history_steps.c ----
void            history_edit_nodes(ui_node_canvas_t *canvas, i32 canvas_type, i32 canvas_group);
void            history_paint();
void            history_new_layer();
void            history_new_image_layer(asset_t *asset);
void            history_new_black_mask();
void            history_new_white_mask();
void            history_new_group();
void            history_duplicate_layer();
history_step_t *history_delete_layer(slot_layer_t *l);
void            history_clear_layer(history_action_t action, u32 color);
void            history_order_layers(i32 prev_order);
void            history_merge_layers();
void            history_merge_group(slot_layer_t *l);
void            history_apply_mask();
void            history_invert_mask();
void            history_to_fill_layer();
void            history_to_paint_layer();
void            history_to_paint_mask();
void            history_layer_opacity();
void            history_layer_raise();
void            history_layer_blending();
void            history_layer_visible(slot_layer_t *l);
void            history_layer_name(slot_layer_t *l, char *prev_name);
void            history_layer_object();
void            history_smart_material(char *name);
void            history_layer_fill(slot_layer_t *l);
void            history_settle(void);
void            history_settle_after_stroke(void);
void            history_new_swatch();
void            history_delete_swatch(swatch_color_t *swatch);
void            history_replace_swatches(char *name);
void            history_new_material();
void            history_delete_material(slot_material_t *m);
void            history_duplicate_material();
void            history_delete_material_group(node_group_t *group);
void            history_mask_stack();
void            history_resolution(i32 w0, i32 h0, i32 w1, i32 h1);
void            history_path(slot_layer_t *l);

// ---- keymap.c ----
void       keymap_load();
void       keymap_save();
char      *keymap_to_json(any_map_t *keymap);
any_map_t *keymap_get_default();
bool       keymap_shortcut(char *s, shortcut_type_t type);

// ---- logic_node.c ----
logic_node_t       *logic_node_create(logic_node_ext_t *ext);
void                logic_node_add_input(logic_node_t *self, logic_node_ext_t *node, i32 from);
void                logic_node_add_outputs(logic_node_t *self, logic_node_t_array_t *nodes);
logic_node_value_t *logic_node_input_get(logic_node_input_t *self);
gpu_texture_t      *logic_node_input_get_as_image(logic_node_input_t *self);
void                logic_node_input_set(logic_node_input_t *self, f32_array_t *value);

// ---- minic_impl.c ----
void              script_show_envmap(bool b);
void              script_timer(f32 delay, void *fn);
void              script_notify_on_update(void *fn);
void              script_notify_on_next_frame(void *fn);
bool              script_is_running(void);
void              ui_files_show2(char *filters, bool is_save, bool open_multiple, void *files_done);
void              ui_box_show_custom2(void *commands, i32 mw, i32 mh, void *on_hide, char *title);
bool              script_box_shown();
f32               script_frame_time();
char             *script_save_path();
char             *script_documents_dir();
char             *project_filepath_get();
char             *project_basepath_get();
void              project_filepath_set(char *s);
i32               project_modified_get();
i32               script_alert_serial();
camera_object_t  *script_get_camera();
context_t        *script_get_context();
config_t         *script_get_config();
platform_t        script_get_platform();
project_t        *script_get_project();
world_data_t     *script_get_world();
object_t         *script_get_object(char *s);
slot_material_t  *script_get_material(char *s);
gpu_texture_t    *script_get_texture(char *s);
void              script_texture_delete(char *s);
void              script_paint(f32 x, f32 y);
void              script_paint_world(f32 x, f32 y, f32 z);
void              script_paint_end(void);
void              script_fill_layer(void);
buffer_t         *script_get_texture_pixels(gpu_texture_t *texture);
i32               script_mask_add_effect(slot_layer_t *m, i32 type, i32 index);
bool              script_mask_remove_effect(slot_layer_t *m, i32 i);
void              script_mask_move_effect(slot_layer_t *m, i32 from, i32 to);
i32               script_mask_effect_type(slot_layer_t *m, i32 i);
void              script_mask_set_effect(slot_layer_t *m, i32 i, bool visible, f32 opacity, i32 blending);
void              script_mask_set_effect_param(slot_layer_t *m, i32 i, i32 param, f32 value);
f32               script_mask_get_effect_param(slot_layer_t *m, i32 i, i32 param);
char             *script_mask_effects_string(slot_layer_t *m);
char             *script_mesh_map_name(i32 type);
void              script_mesh_map_set_name(i32 type, char *name);
void              script_brush_take(void);
bool              script_brush_taken(void);
void              script_projects_take(void);
void              script_projects_put_back(void);
void              script_projects_saved_to(char *path);
void              script_brush_swap(void);
void              script_brush_defaults(void);
void              script_brush_set(char *name, f32 value);
void              script_brush_use(i32 id);
void              script_brush_delete(i32 id);
i32               script_brush_duplicate(i32 id);
f32               script_brush_get(char *name);
gpu_texture_t    *script_brush_wave(void);
void              script_quit(void);
void              script_project_new(void);
void              script_project_new_template(char *name);
void              script_project_open(char *path);
i32               script_set_id_map(i32 index);
asset_t          *script_get_asset(i32 index);
i32               script_asset_index(char *search);
void              script_import_asset(char *path, bool hdr_as_envmap);
void              script_append_mesh(char *path);
buffer_t         *script_load_blob(char *path);
void              script_append_mesh_obj(char *data);
bool              script_packed_asset_save(char *suffix, char *path);
i32               script_packed_assets_remove(char *search);
i32               script_assets_remove(char *search);
i32               script_pack_assets(char *search);
void              script_set_pack_assets(i32 pack);
void              script_export_mesh(char *path);
void              script_export_material(char *path);
slot_material_t  *script_material_create(char *name);
slot_material_t  *script_material_get();
i32               script_material_count();
void              script_material_set(slot_material_t *m);
void              script_material_delete(slot_material_t *m);
ui_node_t        *script_material_create_node(char *type);
ui_node_t        *script_material_create_node_at(char *type, f32 x, f32 y);
ui_node_t        *script_material_get_node(char *type);
ui_node_t        *script_material_get_node_id(i32 id);
void              script_material_connect(ui_node_t *from, i32 from_socket, ui_node_t *to, i32 to_socket);
void              script_material_disconnect(ui_node_t *to, i32 to_socket);
void              script_material_remove_node(ui_node_t *node);
ui_node_canvas_t *script_material_group_create(char *name);
void              script_material_group_remove(ui_node_canvas_t *group);
ui_node_socket_t *script_material_group_add_socket(ui_node_canvas_t *group, bool is_output, char *type, char *name);
ui_node_t        *script_canvas_create_node(ui_node_canvas_t *canvas, char *type, char *group_name);
void              script_canvas_connect(ui_node_canvas_t *canvas, ui_node_t *from, i32 from_socket, ui_node_t *to, i32 to_socket);
gpu_texture_t    *script_material_node_preview(ui_node_t *node, any_array_t *parents, i32 size);
void              script_material_set_float(ui_node_t *node, i32 is_input, i32 socket, f32 value);
void              script_material_set_color(ui_node_t *node, i32 is_input, i32 socket, f32 r, f32 g, f32 b, f32 a);
void              script_material_set_vector(ui_node_t *node, i32 is_input, i32 socket, f32 x, f32 y, f32 z);
void              script_material_set_button(ui_node_t *node, i32 button, f32 value);
void              script_material_set_text(ui_node_t *node, i32 button, char *text);
void              script_material_update(void);
void              script_mouse_click(i32 x, i32 y, i32 button);
void              script_mouse_press(i32 x, i32 y, i32 button);
void              script_mouse_move(i32 x, i32 y);
void              script_mouse_release(i32 x, i32 y, i32 button);
void              script_key_press(char *key);
any_array_t      *script_get_mesh_objects();
void              script_call(void *fn);
void              script_register_text(char *format, void *fn);
void              script_unregister_text(char *format);
void              script_register_texture(char *format, void *fn);
void              script_unregister_texture(char *format);
void              script_register_mesh(char *format, void *fn);
void              script_unregister_mesh(char *format);
raw_mesh_t       *script_make_raw_mesh(char *name, i16_array_t *posa, i16_array_t *nora, u32_array_t *inda, float scale_pos);
void              script_material_category_add(char *category_name, any_array_t *node_list);
void              script_brush_category_add(char *category_name, any_array_t *node_list);
void              script_material_category_remove(char *category_name);
void              script_brush_category_remove(char *category_name);
void              script_material_custom_nodes_set(char *node_type, void *fn);
void              script_brush_custom_nodes_set(char *node_type, void *fn);
void              script_material_custom_nodes_remove(char *node_type);
void              script_brush_custom_nodes_remove(char *node_type);
void             *script_material_shader_get();
void              script_show_message(char *text, f32 seconds);
void              script_draw_particles(gpu_texture_t *texture, float x, float y, float w, float h, int atlas_x, int atlas_frames);
void              script_screenshot_queue(char *path, i32 frames, void (*done)(void));
void              script_screenshot(char *path);

// ---- app_api.c ----
void app_notify_previews(void);

// ---- node_shader.c ----
void                      node_shader_add_out(node_shader_t *raw, char *s);
char                     *node_shader_engine_type(char *t);
void                      node_shader_add_constant(node_shader_t *raw, char *s, char *link);
void                      node_shader_add_texture(node_shader_t *raw, char *name, char *link);
void                      node_shader_add_function(node_shader_t *raw, char *s);
void                      node_shader_write_vert(node_shader_t *raw, char *s);
void                      node_shader_write_end_vert(node_shader_t *raw, char *s);
void                      node_shader_write_attrib_vert(node_shader_t *raw, char *s);
void                      node_shader_write_frag(node_shader_t *raw, char *s);
void                      node_shader_write_attrib_frag(node_shader_t *raw, char *s);
void                      node_shader_rename(buffer_t *out, char *code, char *from, char *to);
char                     *node_shader_get(node_shader_t *raw);
void                      node_shader_context_free(node_shader_context_t *raw);
vertex_element_t_array_t *node_shader_mesh_elements();
node_shader_context_t    *node_shader_context_create(material_t *material, shader_context_t *props);
void                      node_shader_context_add_elem(node_shader_context_t *raw, char *name, char *data_type);
bool                      node_shader_context_is_elem(node_shader_context_t *raw, char *name);
node_shader_t            *node_shader_context_make_shader(node_shader_context_t *raw);

// ---- nodes_brush.c ----
void       nodes_brush_init();
ui_node_t *nodes_brush_create_node(char *node_type);

// ---- nodes_material.c ----
void              nodes_material_init();
string_array_t   *nodes_material_category_names();
ui_node_t        *nodes_material_get_node_t(char *node_type);
ui_node_t        *nodes_material_create_node(char *node_type, node_group_t *group);
ui_node_socket_t *nodes_material_socket_def(char *name, char *type, f32_array_t *value, f32 min, f32 max);
ui_node_socket_t *nodes_material_socket_ex(char *name, char *type, u32 color, f32_array_t *value, f32 min, f32 max, f32 precision, i32 display);
u32               nodes_material_get_socket_color(char *type);
f32_array_t      *nodes_material_get_socket_default_value(char *type);
char             *nodes_material_get_socket_name(char *type);
ui_node_socket_t *nodes_material_create_socket(ui_nodes_t *nodes, ui_node_t *node, char *name, char *type, ui_node_canvas_t *canvas, f32 min, f32 max,
                                               void *default_value);

// ---- parser_logic.c ----
void parser_logic_parse(ui_node_canvas_t *canvas);

// ---- parser_material.c ----
ui_node_t               *parser_material_get_node(i32 id);
ui_node_link_t          *parser_material_get_input_link(ui_node_socket_t *inp);
void                     parser_material_init();
shader_out_t            *parser_material_parse(ui_node_canvas_t *canvas, node_shader_context_t *_con, node_shader_t *_shader);
void                     parser_material_finalize(node_shader_context_t *con);
ui_node_canvas_t        *parser_material_get_group(char *name);
bool                     parser_material_is_open(ui_node_canvas_t *g);
void                     parser_material_push_group(ui_node_canvas_t *g);
void                     parser_material_pop_group();
void                     parser_material_enter_parents(ui_node_t_array_t *parents);
char                    *parser_material_socket_glsl_type(char *type);
char                    *parser_material_socket_zero(char *type);
char                    *parser_material_greyscale(char *s);
char                    *parser_material_convert(char *s, char *from, char *to);
char                    *parser_material_parse_input_as(ui_node_socket_t *inp, char *type, char *default_expr);
char                    *parser_material_parse_input(ui_node_socket_t *inp);
char                    *parser_material_preview_rgb(char *res, char *type);
i32                      parser_material_socket_index(ui_node_t *node, ui_node_socket_t *socket);
char                    *parser_material_parse_group(ui_node_t *node, ui_node_socket_t *socket);
extern buffer_t         *parser_material_capture; // parser_material.c: code written there instead of the shader
void                     parser_material_write(node_shader_t *raw, char *s);
char                    *parser_material_parse_vector_input(ui_node_socket_t *inp);
char                    *parser_material_parse_value_input(ui_node_socket_t *inp, bool vector_as_grayscale);
char                    *parser_material_parse_value(ui_node_t *node, ui_node_socket_t *socket);
char                    *parser_material_get_coord(ui_node_t *node);
char                    *parser_material_safesrc(char *s);
char                    *parser_material_res_var_name(ui_node_t *node, ui_node_socket_t *socket);
char                    *parser_material_write_result(ui_node_link_t *l);
char                    *parser_material_store_var_name(ui_node_t *node);
char                    *parser_material_vec1(f32 v);
char                    *parser_material_vec3(f32_array_t *v);
char                    *parser_material_to_vec3(char *s);
ui_node_t               *parser_material_node_by_type(ui_node_t_array_t *nodes, char *ntype);
char                    *parser_material_node_name(ui_node_t *node, ui_node_t_array_t *_parents);
bind_tex_t              *parser_material_make_bind_tex(char *tex_name, char *file);
char                    *u8_array_string_at(u8_array_t *a, i32 i);
extern ui_node_canvas_t *parser_material_live_canvas;
void                     parser_material_live_begin(ui_node_canvas_t *canvas, i32 table);
void                     parser_material_live_end();
gpu_texture_t           *parser_material_live_texture(char *link);
char                    *parser_material_live_input(ui_node_socket_t *inp);
char                    *parser_material_live_variable(ui_node_t *node);
char                    *parser_material_live_ramp_color(ui_node_t *node, i32 index);
char                    *parser_material_live_ramp_pos(ui_node_t *node, i32 pack);
void                     parser_material_live_update(ui_node_canvas_t *canvas, i32 table);

// ---- pipes.c ----
gpu_pipeline_t *pipes_get_merge_r();
gpu_pipeline_t *pipes_get_merge_g();
gpu_pipeline_t *pipes_get_merge_b();
gpu_pipeline_t *pipes_get_merge_a();
void            pipes_init();
i32             pipes_get_constant_location(char *type);

// ---- project.c ----
void                   project_open();
void                   project_autosave_update();
void                   project_autosave_init();
void                   project_save_now();
bool                   project_leave();
bool                   project_is_open(char *path);
void                   project_scan_folders();
void                   project_save(bool save_and_quit);
char                  *project_icon_path(char *path);
char                  *project_take_import(char *path);
char                  *project_recent_entry(char *path);
#ifdef IRON_IOS
char                  *project_documents_dir();
char                  *project_rename(char *path, char *name);
char                  *project_duplicate(char *path);
char                  *project_folder();
char                  *project_subfolder(char *name);
#endif
void                   project_save_as(bool save_and_quit);
void                   project_fetch_default_meshes();
void                   project_new(bool reset_layers);
void                   project_set_default_envmap();
void                   project_import_material();
ui_node_link_t        *project_create_node_link(ui_node_link_t_array_t *links, i32 from_id, i32 from_socket, i32 to_id, i32 to_socket);
void                   project_import_brush();
void                   project_import_mesh(bool replace_existing, void (*done)(void));
void                   project_reimport_mesh();
void                   project_unwrap_mesh_box();
void                   project_import_asset(char *filters, bool hdr_as_envmap);
void                   project_import_swatches(bool replace_existing);
void                   project_reimport_textures();
void                   project_reimport_texture_load(char *path, asset_t *asset);
void                   project_reimport_texture(asset_t *asset);
gpu_texture_t         *project_get_image(asset_t *asset);
asset_t_array_t       *project_get_assets();
string_array_t        *project_get_used_atlases();
bool                   project_is_atlas_object(mesh_object_t *p);
mesh_object_t_array_t *project_get_atlas_objects(i32 object_mask);
bool                   project_packed_asset_exists(packed_asset_t_array_t *packed_assets, char *name);
void                   project_export_swatches();
swatch_color_t        *project_make_swatch(i32 base);
swatch_color_t        *project_clone_swatch(swatch_color_t *swatch);
void                   project_set_default_swatches();
node_group_t          *project_get_material_group_by_name(char *group_name);
bool                   project_is_material_group_in_use(node_group_t *group);
void project_delete_swatch(swatch_color_t *swatch);

// ---- resource.c ----
void           resource_load(string_array_t *names);
gpu_texture_t *resource_get(char *name);
rect_t        *resource_tile50(gpu_texture_t *img, i32 i);

// ---- strings.c ----
char *strings_arm_file_expected();
char *strings_unknown_asset_format();
char *strings_could_not_locate_texture();
char *strings_failed_to_read_mesh_data();
char *strings_asset_already_imported();
char *strings_number_ext(i32 i);
i32   strings_split_number_ext(char *name, char **base);

// ---- translator.c ----
char           *_tr(char *s);
char           *vtr(char *id, any_map_t *vars);
char           *tr(char *id);
void            translator_load_translations(char *new_locale);
string_array_t *translator_get_supported_locales();

// ---- uniforms.c ----
f32            uniforms_brush_radius();
f32            uniforms_brush_flow();
f32            uniforms_brush_opacity();
f32            uniforms_brush_hardness();
vec2_t         uniforms_brush_dab_angle();
vec2_t         uniforms_brush_angle();
vec4_t         uniforms_input_brush();
vec4_t         uniforms_input_brush_last();
gpu_texture_t *uniforms_ext_tex_link(object_t *object, shader_data_t *mat, char *link);
void           uniforms_ext_init();

// ---- viewport.c ----
void viewport_scale_to_bounds(f32 bounds);
void viewport_reset();
void viewport_frame();
void viewport_set_view(f32 x, f32 y, f32 z, f32 rx, f32 ry, f32 rz);
void viewport_orbit(f32 x, f32 y);
void viewport_orbit_opposite();
void viewport_zoom(f32 f);
void viewport_update_camera_type(i32 camera_type);
void viewport_save_texture(gpu_texture_t *screenshot);
void viewport_save_texture_to_file(gpu_texture_t *screenshot, char *path);
void viewport_capture_screenshot();
void viewport_capture_screenshot_to(gpu_texture_t *target, float x, float y, float w, float h);
void viewport_set_face(i32 face);

// ---- minic_api.c ----
char *minic_api_header_generate();

// ---- startup.c ----
scene_t *startup_get_scene(void);

// ---- paint/plugins/bake_maps/bake_maps.c ----
#ifdef WITH_PLUGINS
i32            bake_maps_load_high(char *path);
f32            bake_maps_high_fit(void);
i32            bake_maps_id_part_count(void);
char          *bake_maps_id_part_name(i32 part);
u32            bake_maps_id_part_rgb(i32 part);
i32            bake_maps_set_low(void);
void           bake_maps_set_cage(f32 dist, bool average);
void           bake_maps_set_ao(f32 strength, f32 radius, f32 offset);
void           bake_maps_set_curvature(f32 strength, f32 radius, f32 offset, i32 smooth);
void           bake_maps_set_cavity(f32 detail_texels, f32 contrast);
bool           bake_maps_start(i32 type, i32 rays, bool use_high, i32 w, i32 h);
f32            bake_maps_progress(void);
bool           bake_maps_step(void);
void           bake_maps_cancel(void);
bool           bake_maps_save(char *folder, char *file_name);
u8            *bake_maps_preview(i32 w, i32 h, f32 yaw, f32 pitch, f32 zoom, bool show_low, bool show_high, bool show_miss, bool fast);
f32            bake_maps_miss_ratio(void);
void           bake_maps_end_images(void);
void           bake_maps_end(void);
#endif

// ---- history_store.c ----
void  hstore_chunk_header_write(u8 *p, u8 kind, u8 codec, u16 flags, hash128_t h, u32 raw_len, u32 comp_len);
bool  hstore_chunk_header_read(const u8 *p, hstore_chunk_t *c);
u8   *hstore_chunk_decode(const hstore_chunk_t *c, const u8 *payload, bool verify);
bool  hstore_locate(hash128_t h, hstore_loc_t *loc);
u8   *hstore_read_located(const hstore_loc_t *loc, void *dctx, bool *disk);
void  hstore_count_disk_reads(i64 n);
char *hstore_journal_path(char *uuid);
void  hstore_journal_mark_saved();
bool  hstore_journal_pending(char *uuid);
i32   hstore_recover();
i64   hstore_live_bytes();
i32   hstore_kind_of(hash128_t h);
hash128_t hstore_recovered_last(i32 kind);
void  hstore_recover_end();
void  hstore_journal_delete(char *uuid);
void  hstore_journal_rebind(char *old_uuid);
void  hstore_attach_project(char *path, const hstore_index_entry_t *entries, i32 n);
void  hstore_each(const hash128_t *roots, i32 n, bool referenced, bool (*fn)(hash128_t h, void *ud), void *ud);
u8   *hstore_chunk_payload(hash128_t h, hstore_chunk_t *c);
i32   hstore_test_put(i32 kind, i32 seed, i32 len, bool noise);
i32   hstore_test_get(i32 id, i32 seed, i32 len, bool noise);
bool  hstore_test_has(i32 id);
void  hstore_test_ref(i32 id);
void  hstore_test_unref(i32 id);
f32   hstore_test_stat(i32 field);
void  hstore_drop_tier(i32 tier);
void  hstore_test_flush();
void  hstore_test_trim(bool memory_warning);
void  hstore_test_reset();
void  hstore_test_crash();
void  hstore_test_tear(i32 bytes);
char *hstore_test_journal_path();
void  hstore_test_children(i32 kind, bool on);
void  hstore_test_list_clear();
void  hstore_test_list_add(i32 id);
i32   hstore_test_put_list(i32 kind);
void  hstore_test_rebuild();
i32   hstore_test_refs(i32 id);

// ---- io/project_container.c ----
bool        project_container_is(char *path);
project_t  *project_container_open(char *path);
void        project_container_attach();
void        project_container_forget();
bool        project_container_writing();
u8_array_t *project_container_layer_tilemap(slot_layer_t *l);
bool        project_container_layer_restore(layer_data_t *ld, slot_layer_t *l, bool *has_alpha);
i32         project_container_roots(hash128_t *out, i32 cap);
u8_array_t *project_container_layer_thumb(slot_layer_t *l);
bool        project_container_set_uuid(char *path, char *uuid);
void        project_container_layer_thumb_restore(layer_data_t *ld, slot_layer_t *l);
void        project_container_loaded();
i32         project_container_test_index_count(i32 kind);
bool        project_container_writing_session();
u8_array_t *project_container_history_encode();
i64         project_container_history_budget();
void        project_clear_history();
bool        project_session_write();
bool        project_session_due();
bool        project_session_changed();
void        project_session_update();
void        project_session_forget();
void        project_session_loaded();
bool        project_session_pending(char *path);
bool        project_session_recover(char *path);
bool        project_session_overlay(project_t *p);
void        project_container_pack_assets(project_t *p);
void        project_container_unpack_assets(project_t *p);
bool        project_container_save(char *path);
bool        project_container_save_copy(char *path);
bool        project_container_writing_history();
char       *project_file_uuid(char *path);
void        hstore_journal_delete_for_file(char *path);
i32         project_container_test_reads();
i32         project_container_test_appended();
void        project_test_truncate(char *path, i32 bytes);
i32         project_test_file_size(char *path);
i32         project_test_mask_unequal(i32 i);
u32  project_test_layer_uid(i32 i);
u32  project_test_material_uid(i32 i);
u32  project_test_uid_next();
i32  project_test_res(i32 axis);
void project_test_set_res(i32 w, i32 h);
i32  project_test_layer_hash(i32 i, i32 channel);

// ---- util/util_test.c ----
char *script_tmp_dir(char *name);
char *script_dir(void);
i32   script_diff_images(char *path_a, char *path_b, f32 tol);
i32   script_diff_textures(gpu_texture_t *a, gpu_texture_t *b, f32 tol);
f32   script_diff_max();
f32   script_diff_mean();
void  script_diff_mask(gpu_texture_t *mask, i32 mode);
char *script_texture_hash(gpu_texture_t *t);
i32   script_diff_files(char *path_a, char *path_b);
void  script_texture_read(gpu_texture_t *t);
bool  script_image_read(char *path);
f32   script_texel(f32 u, f32 v, i32 c);

// ---- timeline_io.c ----
u8_array_t *timeline_io_encode();
bool        timeline_io_decode(u8_array_t *a);
hash128_t  *timeline_roots(i32 *n);
i32         timeline_io_test_state(slot_layer_t *l, i32 pos);

// ---- io/export_tiled.c ----
bool           export_tiled_start(char *path, bool bake_material);
bool           export_tiled_step(f64 budget_ms);
void           export_tiled_cancel();
bool           export_tiled_running();
bool           export_tiled_frozen();
hash128_t      export_tiled_evicted_state(slot_layer_t *l);
bool           export_tiled_evicted_size(slot_layer_t *l, i32 *w, i32 *h);
i64            export_tiled_evicted_layer_bytes();
void           export_tiled_abort_for_project_change();
void           export_tiled_layer_unloading(slot_layer_t *l);
void           export_tiled_history_action(history_action_t a);
i32            app_export_test_sheet_open(); // app_api_export.c
void           app_export_test_pick(i32 preset, i32 layers);
f32            export_tiled_progress();
void           export_tiled_run(char *path, bool bake_material);
void           export_tiled_set_resolution(i32 w, i32 h);
i32            export_tiled_not_upscaled(i32 w, i32 h);
char          *export_tiled_not_upscaled_name(i32 i);
void           export_tiled_test_no_fill(bool on);
void           export_tiled_test_whole(bool on);
void           export_tiled_set_mode(i32 mode);
void           export_tiled_test_tile(i32 size);
void           export_tiled_test_raster(bool on);
void           export_tiled_test_mm(bool on);
i32            export_tiled_stat(i32 what);
gpu_texture_t *export_tiled_test_coverage();

// ---- paint_exec.c ----
// Resolved dabs. A brush paint draw reads its dabs from _texpaintdabs, the target of the dab resolve
// pass (render/make_dab_resolve.c): PAINT_DAB_TEXELS RGBA128 texels a dab, PAINT_DAB_ROW dabs a row, dab k at
// x = (k % PAINT_DAB_ROW) * PAINT_DAB_TEXELS + t, y = k / PAINT_DAB_ROW. Texel t:
//   0  p.xyz world position of the dab, w the homogeneous w of its unprojection (1 / clip w: camera align, decals)
//   1  p_last.xyz, w_last: the other end of the capsule
//   2  normal at p, octahedral xy on a 1/32767 grid; z 1 when p is off the mesh, w 1 when p_last is
//   3  radius, opacity, hardness, flow
//   4  s.xy, s_last.xy: the screen points the dab was resolved from (0-1, y down)
//   5  uv at s, uv at s_last (gbuffer2: pixel snap)
//   6  tip mask rotation cos, sin (brush angle and dab angle), tilt shape xy (util_brush_tilt_shape)
//   7  brush angle cos, sin, dab angle cos, sin
//   8  q of p (PAINT_DAB_QUANT steps an axis, as floats), w 0; off the mesh (not quantized) p itself, w 1
//   9  q of p_last, the same
// On the mesh, p and p_last are quantized: object space of the paint target (world_unpack), PAINT_DAB_QUANT steps an axis
// over its bounds (paint_exec_space: -1.01 to 1.01, positions are packed normalized), back to world. Off the mesh they are the unquantized far plane point (a decal
// anchor: at the depth of the world origin), recordings keep them as floats. The input of the pass (CPU, the same layout):
// t0 s.xy s_last.xy, t1.x the mode, t3, t6, t7; the pass writes the rest. Replaying (paint_exec_replay), the input is a
// recorded row: t0 and t1 are made again from q (t8, t9) when quantized, the rest is copied.
#define PAINT_DAB_TEXELS      10
#define PAINT_DAB_ROW         64
#define PAINT_DAB_MAX         2048 // A flush of the executor resolves at most this many
#define PAINT_DAB_QUANT       2097151.0 // 2^21 - 1
#define PAINT_DAB_QUANT_S     "2097151.0" // In GLSL
#define PAINT_DAB_TEXELS_S    "10"
#define PAINT_DAB_MODE_BRUSH  0.0 // Off the mesh: far plane
#define PAINT_DAB_MODE_ANCHOR 1.0 // Decal anchor, off the mesh: at the depth of the world origin, as make_texcoord_run did

bool             paint_exec_on(); // The paint shader in use reads resolved dabs
void             paint_exec_begin();
void             paint_exec_end();
void             paint_exec_isolate_begin(); // Work outside the stroke model: at once, apart from the open scope
void             paint_exec_isolate_end();
void             paint_exec_test_scope(i32 open);
char            *paint_exec_test_fill_rect(slot_layer_t *l, i32 w);
void             paint_exec_draw(bool dilation);
void             paint_exec_batch_add();
void             paint_exec_batch_flush(bool dilation);
void             paint_exec_dilate(bool base, bool nor_pack);
void             paint_exec_dilate_after_paint();
void             paint_exec_clear_blend();
void             paint_exec_clone_flatten();
paint_exec_op_t *paint_exec_current(); // The op being executed, NULL outside of them
f32             *paint_exec_dab(i32 k); // PAINT_DAB_TEXELS * 4 floats of input of dab k
gpu_texture_t   *paint_exec_dabs_in();
void             paint_exec_space(mat4_t *obj, mat4_t *obj_inv, vec4_t *bmin, vec4_t *bsize);
void             render_path_paint_commands_paint_now(bool dilation);
void             render_path_paint_dilate_after_paint_now();
void             render_path_paint_clear_blend_now();
void             render_path_paint_clone_snapshot();
void             render_path_paint_scratch_resize(i32 w, i32 h);
bool             render_path_paint_resolved();
vec4_t           uniforms_brush_direction_now();
vec4_t           uniforms_pixel_snap_uv();

// ---- render/make_dab_resolve.c ----
void make_dab_resolve_run(i32 count);
void make_dab_resolve_prewarm();
void make_dab_resolve_prewarm_on_next_frame(void *_);
char *make_brush_dab(char *k, i32 t);

// ---- paint_rec.c ----
bool paint_rec_recording();
void paint_rec_foreign();
void paint_state_capture(paint_state_t *s, hash128_t material);
i32  paint_rec_flush_dabs(i32 count);
void paint_rec_op(paint_exec_op_t *op, i32 base);
void paint_rec_op_done(paint_exec_op_t *op);
bool paint_rec_decode(const u8 *b, u32 len, paint_rec_t *r);
void paint_rec_free(paint_rec_t *r);
void paint_rec_finish();
void paint_rec_init();
void paint_rec_flush(); // stroke_rec.h, for scripts
i32  paint_rec_test_stored();
f32  paint_rec_test_bounds(slot_layer_t *l, i32 index, i32 c);
bool timeline_tiles_changed_bounds(hash128_t prev, hash128_t cur, f32 out[4]);
hash128_t timeline_state(struct timeline *tl, i32 pos);
u64  make_paint_glsl_hash(shader_context_t *scon);
bool make_paint_scon_uses_matcache(shader_context_t *scon);
gpu_texture_t *paint_replay_dilate_map(i32 w, i32 h, const f32 *uv_rect);
i32            paint_replay_prepare(struct timeline *tl, i32 index, struct paint_target *t);
i32            paint_replay_mm_keys(struct timeline *tl, i32 index, struct paint_target *t, u64 *keys, i32 n, i32 max);
void paint_mat_snapshot_reset();
void paint_mat_snapshot_init();
void paint_mat_snapshot_parse_begin(slot_material_t *m);
void paint_mat_snapshot_parse_end();
void paint_mat_snapshot_replay_begin(slot_material_t *m);
void paint_mat_snapshot_replay_end();
void render_path_paint_dilate_now(bool base, bool nor_pack);

// ---- paint_refine.c ----
void paint_refine_note(struct timeline *tl, i32 index, const u64 *keys, i32 n);
void paint_refine_init();
void paint_refine_reset();
i32  paint_refine_stat(i32 what);
bool paint_rec_active();

// ---- paint_replay.c ----
extern gpu_texture_t *paint_replay_s0[3];
extern mat4_t         paint_replay_obj;
extern mat4_t         paint_replay_obj_inv;
extern paint_rec_cam_t paint_replay_cam;
hash128_t             paint_asset_intern(gpu_texture_t *t);
gpu_texture_t        *paint_asset_texture(hash128_t h);
void                  paint_asset_stroke_begin();
i32                   paint_replay_test_fallbacks();
i32                   paint_replay_test_count();
bool                  paint_replay_test_entry(slot_layer_t *l, i32 index);
bool                  paint_replay_test_into(slot_layer_t *src, i32 index, slot_layer_t *dst);
void                  paint_replay_test_set_fast(bool on);
bool                  paint_replay_test_tiles(slot_layer_t *src, i32 index, slot_layer_t *dst, i32 n, i32 halo);
void                  paint_exec_replay_push(paint_exec_op_t *op, i32 ndabs);
i32                   paint_exec_pending_ops();
void                  render_path_base_draw_paint_depth_into(char *color, char *depth);
shader_context_t     *make_material_parse_paint_kept(void);

// ---- history_io.c ----
u8  *history_io_encode(u32 *len);
bool history_io_decode(u8 *data, u32 len);
i32  history_io_roots(hash128_t **out);
void history_io_thin(i64 budget);
void history_io_clear();
i32  history_io_test_roundtrip(i32 _);

// ---- util/util_layer_resolution.c ----
void layers_resolution_apply(i32 w, i32 h);
bool layers_resolution_valid(i32 w, i32 h);
bool layers_resolution_begin(i32 w, i32 h);
bool layers_resolution_step();
bool layers_resolution_running();
f32  layers_resolution_progress();
void layers_resolution_reset();
i32  layers_resolution_test_restores();
