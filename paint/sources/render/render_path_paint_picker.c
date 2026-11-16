#include "../global.h"

// Picking: the paint object under the cursor, the color picker and its values read back from the GPU

// Renders object ids and returns the paint object under the cursor, NULL on a miss.
// Expects the cursor tool with pick_object_id set, the paint material is parsed for it here.
mesh_object_t *render_path_paint_pick_object() {
	viewport_mode_t _viewport_mode = g_context->viewport_mode;
	g_context->viewport_mode       = VIEWPORT_MODE_OBJECT_ID;
	make_material_parse_mesh_material();

	// Objects with assigned material do not write object id
	shader_data_t *base_material     = g_project->_->materials->buffer[0]->data;
	any_array_t   *_object_materials = any_array_create(g_project->_->paint_objects->length);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o             = g_project->_->paint_objects->buffer[i];
		_object_materials->buffer[i] = o->material;
		o->material                  = base_material;
	}

	render_path_base_draw_gbuffer();

	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		g_project->_->paint_objects->buffer[i]->material = _object_materials->buffer[i];
	}

	g_context->viewport_mode = _viewport_mode;
	make_material_parse_mesh_material();

	g_context->pdirty = 1;
	make_material_parse_paint_material(false);
	render_path_set_target("texpaint_picker", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("gbuffer1", "gbuffer1");
	render_path_bind_target("gbuffer2", "gbuffer2");
	render_path_paint_draw_fullscreen_triangle();

	buffer_t *pixel = gpu_get_texture_pixels(render_path_target_image("texpaint_picker"));
	if (buffer_get_u8(pixel, 3) > 0) {
		i32 r_byte = buffer_get_u8(pixel, 2);
		i32 g_byte = buffer_get_u8(pixel, 1);
		i32 index  = r_byte + g_byte * 256 - 1;
		if (index >= 0 && index < g_project->_->paint_objects->length) {
			return g_project->_->paint_objects->buffer[index];
		}
	}
	return NULL;
}

f32    uniforms_ext_f32_link(object_t *object, shader_data_t *mat, char *link);
vec2_t uniforms_ext_vec2_link(object_t *object, shader_data_t *mat, char *link);
vec4_t uniforms_ext_vec4_link(object_t *object, shader_data_t *mat, char *link);

// Dabs painted by one draw when the paint shader is batched (render_path_paint_batched): the stamps or samples of a frame

// Picker results: surface values under the cursor into the picked color, the material and fill layers with picker nodes.
// tool is the tool when the pick was drawn, the values may arrive a frame later.
static void render_path_paint_picker_apply(u8 *a, u8 *b, u8 *c, u8 *d, bool is_mask, tool_type_t tool) {
	i32 i0 = 2;
	i32 i1 = 1;
	i32 i2 = 0;
	i32 i3                           = 3;
	g_context->picked_color->base    = color_set_rb(g_context->picked_color->base, a[i0]);
	g_context->picked_color->base    = color_set_gb(g_context->picked_color->base, a[i1]);
	g_context->picked_color->base    = color_set_bb(g_context->picked_color->base, a[i2]);
	g_context->picked_color->opacity = a[i3] / 255.0;
	g_context->uvx_picked            = d[i0] / 255.0;
	g_context->uvy_picked            = d[i1] / 255.0;

	if (!is_mask) {
		g_context->picked_color->normal    = color_set_rb(g_context->picked_color->normal, b[i0]);
		g_context->picked_color->normal    = color_set_gb(g_context->picked_color->normal, b[i1]);
		g_context->picked_color->normal    = color_set_bb(g_context->picked_color->normal, b[i2]);
		g_context->picked_color->occlusion = c[i0] / 255.0;
		g_context->picked_color->roughness = c[i1] / 255.0;
		g_context->picked_color->metallic  = c[i2] / 255.0;
		g_context->picked_color->height    = c[i3] / 255.0;

		// Pick material
		if (g_context->color_picker_callback == NULL) {
			// matid % 3 == 0 - normal, 1 - emission, 2 - subsurface
			i32 id    = b[3];
			i32 matid = math_floor((id - (id % 3)) / 3.0);
			for (i32 i = 0; i < g_project->_->materials->length; ++i) {
				slot_material_t *m = g_project->_->materials->buffer[i];
				if (m->id == matid) {
					if (g_context->picker_select_material) {
						context_set_material(m);
					}
					g_context->materialid_picked = matid;
					break;
				}
			}
		}
	}

	if (g_context->color_picker_callback != NULL) {
		g_context->color_picker_callback(g_context->picked_color);
	}

	if (tool == TOOL_TYPE_PICKER && g_context->color_picker_callback == NULL && !g_context->picker_select_material) {
		tab_materials_apply_picked_color(g_context->picked_color);
	}

	// Update fill layers which are using materials containing a PICKER node
	slot_material_t *_material = g_context->material;
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		if (ui_canvas_find_type(m->canvas, "PICKER") != NULL) {
			g_context->material = m;
			layers_update_fill_layers();
			util_render_make_material_preview();
			ui_dock_redraw(TAB_AREA_SIDEBAR1);
		}
	}
	g_context->material = _material;
	ui_header_handle->redraws = 2;
	ui_dock_redraw(2);
}

static gpu_texture_t *render_path_paint_picker_target(char *name) {
	return render_path_target_image(name);
}

void gpu_read_textures_async(gpu_texture_t **textures, int count);
bool gpu_read_textures_async_poll(uint8_t **data);

static bool        render_path_paint_picker_mask = false;
static tool_type_t render_path_paint_picker_tool = TOOL_TYPE_PICKER;

// Applies the picker values once the gpu read them back, the pick does not wait for the gpu
void render_path_paint_picker_poll() {
	uint8_t *data[4];
	if (gpu_read_textures_async_poll(data)) {
		bool mask = render_path_paint_picker_mask;
		render_path_paint_picker_apply(data[0], mask ? NULL : data[2], mask ? NULL : data[3], data[1], mask, render_path_paint_picker_tool);
	}
}

void render_path_paint_picker_read(bool is_mask) {
	gpu_texture_t *textures[4] = {render_path_paint_picker_target("texpaint_picker"), render_path_paint_picker_target("texpaint_uv_picker"),
	                              is_mask ? NULL : render_path_paint_picker_target("texpaint_nor_picker"),
	                              is_mask ? NULL : render_path_paint_picker_target("texpaint_pack_picker")};
	render_path_paint_picker_mask = is_mask;
	render_path_paint_picker_tool = g_context->tool;
	gpu_read_textures_async(textures, is_mask ? 2 : 4);
}
