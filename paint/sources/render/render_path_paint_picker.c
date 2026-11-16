#include "../global.h"

// Picking: the color picker and its values read back from the GPU

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
	gpu_texture_t *textures[4] = {render_path_target_image("texpaint_picker"), render_path_target_image("texpaint_uv_picker"),
	                              is_mask ? NULL : render_path_target_image("texpaint_nor_picker"),
	                              is_mask ? NULL : render_path_target_image("texpaint_pack_picker")};
	render_path_paint_picker_mask = is_mask;
	render_path_paint_picker_tool = g_context->tool;
	gpu_read_textures_async(textures, is_mask ? 2 : 4);
}
