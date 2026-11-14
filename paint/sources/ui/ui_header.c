
#include "../global.h"

ui_color_state_t ui_header_color_state;
static f64       ui_header_progress_last = 0.0;

void ui_header_init() {
	ui_header_handle->layout       = UI_LAYOUT_HORIZONTAL;
	ui_header_split_handle->layout = UI_LAYOUT_HORIZONTAL;
}

void ui_header_render_ui() {
	if (g_config->touch_ui) {
		ui_header_h = ui_header_default_h + 4;
	}
	else {
		ui_header_h = ui_header_default_h;
	}
	ui_header_h = math_floor(ui_header_h * UI_SCALE());

	if (!g_config->header_pinned) {
		return;
	}

	if (!base_view3d_show) {
		return;
	}

	static bool pen_detected = false;
	if (g_ui->is_typing || pen_detected != pen_connected) {
		ui_header_handle->redraws = 2;
	}
	pen_detected = pen_connected;

	// One header per 3D view
	i32 views = base_is_split() ? 2 : 1;
	ui_window_sync_redraws(ui_header_handle, ui_header_split_handle, views == 2);
	for (i32 i = 0; i < views; ++i) {
		if (ui_window(i == 0 ? ui_header_handle : ui_header_split_handle, base_view_x(i), base_y() - ui_header_h, base_view_w(), ui_header_h, false)) {
			g_ui->_y += 2;
			ui_header_draw_tool_properties();
		}
	}
}

void ui_header_particle_menu_draw() {
	string_array_t *presets = any_array_create_from_raw_tmp(NULL, util_particle_preset_count() + 1); // Sized up front, tmp arrays can not grow
	presets->buffer[0]      = tr("Custom");
	for (i32 i = 0; i < util_particle_preset_count(); ++i) {
		presets->buffer[i + 1] = util_particle_preset_name(i);
	}
	i32 preset = 0;
	ui_combo(&preset, presets, tr("Preset"), true, UI_ALIGN_LEFT, true);
	if (preset > 0) {
		util_particle_apply_preset(preset - 1);
	}
	string_array_t *shapes = any_array_create_from_raw_tmp((void *[]){tr("Point"), tr("Disk"), tr("Ring"), tr("Line")}, 4);
	ui_combo(&g_context->particle_shape, shapes, tr("Emitter"), true, UI_ALIGN_LEFT, true);
	f32 count = g_context->particle_count;
	ui_slider(&count, tr("Count"), 1.0, 8.0, true, 1.0, true, UI_ALIGN_RIGHT, true);
	g_context->particle_count = (i32)(count + 0.5f);
	ui_slider(&g_context->particle_rate, tr("Rate"), 0.5, 20.0, true, 10.0, true, UI_ALIGN_RIGHT, true);
	ui_slider(&g_context->particle_lifetime, tr("Lifetime"), 0.0, 10.0, true, 1.0, true, UI_ALIGN_RIGHT, true);
	ui_slider(&g_context->particle_spawn_distance, tr("Distance"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	ui_slider(&g_context->particle_mass, tr("Mass"), 0.0, 3.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	ui_slider(&g_context->particle_random, tr("Random"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);

	ui_slider(&g_context->particle_friction, tr("Friction"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		physics_set_friction(g_context->particle_friction);
	}

	ui_slider(&g_context->particle_bounciness, tr("Bounce"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		physics_set_bounciness(g_context->particle_bounciness);
	}

	ui_slider(&g_context->particle_gravity_x, tr("Gravity X"), -10.0, 10.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		physics_set_gravity(g_context->particle_gravity_x, g_context->particle_gravity_y, g_context->particle_gravity_z);
	}

	ui_slider(&g_context->particle_gravity_y, tr("Gravity Y"), -10.0, 10.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		physics_set_gravity(g_context->particle_gravity_x, g_context->particle_gravity_y, g_context->particle_gravity_z);
	}

	ui_slider(&g_context->particle_gravity_z, tr("Gravity Z"), -10.0, 10.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		physics_set_gravity(g_context->particle_gravity_x, g_context->particle_gravity_y, g_context->particle_gravity_z);
	}

	if (g_ui->changed || g_ui->is_typing) {
		ui_menu_keep_open = true;
	}
}

void ui_header_draw_tool_properties_layer_preview_dirty(void *_) {
	g_context->layer_preview_dirty = true;
}

void ui_header_draw_tool_properties_color_picker_normal() {
	ui_fill(0, 0, g_ui->_w / (float)UI_SCALE(), g_theme->ELEMENT_H * 9, g_theme->SEPARATOR_COL);
	g_ui->changed = false;
	ui_color_wheel((u32 *)&g_context->picked_color->normal, &ui_header_color_state, false, -1, 10 * g_theme->ELEMENT_H * UI_SCALE(), false, NULL, NULL);
	if (g_ui->changed) {
		ui_header_handle->redraws = 2;
		ui_menu_keep_open         = true;
	}
}

void ui_header_draw_tool_properties_color_picker_base() {
	g_ui->changed = false;
	ui_color_wheel((u32 *)&g_context->picked_color->base, &ui_header_color_state, false, -1, 11 * g_theme->ELEMENT_H * UI_SCALE(), true, NULL, NULL);
	g_context->picked_color->base = color_set_ab(g_context->picked_color->base, 255);

	ui_slider(&g_context->picked_color->opacity, tr("Opacity"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	g_context->picked_color->opacity = math_max(0.0, math_min(1.0, g_context->picked_color->opacity));

	{
		ui_row2();
		ui_text(tr("Normal"), UI_ALIGN_LEFT, 0x00000000);
		ui_state_t state = ui_swatch(g_context->picked_color->normal);
		if (state == UI_STATE_RELEASED && g_ui->combo_selected_id == 0) {
			ui_menu_draw(&ui_header_draw_tool_properties_color_picker_normal, -1, -1);
		}

		ui_slider(&g_context->picked_color->occlusion, tr("Occlusion"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&g_context->picked_color->roughness, tr("Roughness"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&g_context->picked_color->metallic, tr("Metallic"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&g_context->picked_color->height, tr("Height"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	}

	if (g_ui->changed || g_ui->is_typing) {
		ui_header_handle->redraws = 2;
		ui_menu_keep_open         = true;
	}
}

// Toggles for what the keyboard modifiers do on desktop, so they work with a pen or a finger
static void ui_header_draw_toggles() {
	i32  t     = g_context->tool;
	bool brush = t == TOOL_TYPE_BRUSH || t == TOOL_TYPE_ERASER || t == TOOL_TYPE_CLONE || t == TOOL_TYPE_BLUR;
	if (brush) {
		ui_check(&g_context->brush_straight_line, tr("Straight Line"), "");
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Each stroke starts where the previous one ended, tap to draw straight lines"));
		}
	}
	if (t == TOOL_TYPE_BRUSH || t == TOOL_TYPE_ERASER || context_is_decal()) {
		ui_check(&g_context->brush_camera_align, tr("Camera Align"), "");
		if (ui_item_changed()) {
			make_material_parse_paint_material(true); // Camera aligned brushes are not batched
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("The brush faces the screen instead of following the surface"));
		}
	}
	if (context_is_decal()) {
		ui_check(&g_context->decal_mask_toggle, tr("Decal Mask"), "");
		if (ui_item_changed()) {
			make_material_parse_paint_material(true);
		}
	}
	if (brush || context_is_decal()) {
		ui_check(&g_context->grid_snap_toggle, tr("Grid Snap"), "");
	}
	if (g_context->brush_stencil_image != NULL) {
		ui_check(&g_context->stencil_edit, tr("Edit Stencil"), "");
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Drag to move the stencil, drag a corner to scale, the dot to rotate, pinch to scale"));
		}
		ui_check(&g_context->stencil_hide, tr("Hide Stencil"), "");
	}
}

// Radial symmetry and the mirror center
static void ui_header_draw_symmetry_extra(bool *changed) {
	f32 radial = g_context->sym_radial;
	ui_slider(&radial, tr("Radial"), 1.0, 16.0, true, 1.0, true, UI_ALIGN_RIGHT, true);
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Copies of the stroke turned around an axis of the object"));
	}
	i32 r = (i32)(radial + 0.5);
	if (r < 1) {
		r = 1;
	}
	if (r != g_context->sym_radial) {
		g_context->sym_radial = r;
		*changed              = true;
	}
	if (g_context->sym_radial > 1) {
		string_array_t *axes = any_array_create_from_raw_tmp((void *[]){"X", "Y", "Z"}, 3);
		ui_combo(&g_context->sym_radial_axis, axes, tr("Radial Axis"), true, UI_ALIGN_LEFT, true);
	}
	if (g_context->sym_x || g_context->sym_y || g_context->sym_z || g_context->sym_radial > 1) {
		ui_check(&g_context->sym_center, tr("Mesh Center"), "");
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Mirror and turn around the center of the mesh instead of the object origin"));
		}
	}
}

// Blur strength, the stroke settings are the brush's, see the Brush Creator
static void ui_header_draw_stroke_options(bool decal_mask) {
	if (g_context->tool == TOOL_TYPE_BLUR) {
		ui_slider(&g_context->brush_strength, tr("Strength"), 0.01, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	}
}

void ui_header_draw_tool_properties() {
	if (g_context->tool == TOOL_TYPE_PICKER) {

		ui_state_t state = ui_swatch(color_set_ab(g_context->picked_color->base, 255));
		if (state == UI_STATE_STARTED) {
			base_drag_off_x  = -(mouse_x - g_ui->_x - g_ui->_window_x - 3);
			base_drag_off_y  = -(mouse_y - g_ui->_y - g_ui->_window_y + 1);
			base_drag_swatch = project_clone_swatch(g_context->picked_color);
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Drag and drop picked color to swatches, materials, layers or to the node editor"));
		}
		if (g_ui->is_hovered && g_ui->input_released && g_ui->combo_selected_id == 0) {
			ui_menu_draw(&ui_header_draw_tool_properties_color_picker_base, -1, -1);
		}
		if (ui_icon_button(tr("Add Swatch"), ICON_PLUS, UI_ALIGN_CENTER)) {
			swatch_color_t *new_swatch = project_clone_swatch(g_context->picked_color);
			new_swatch->base           = color_set_ab(new_swatch->base, 255);
			g_context->swatch          = new_swatch;
			any_array_push(g_project->swatches, new_swatch);
			history_new_swatch();
			ui_dock_redraw(2);
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Add picked color to swatches"));
		}

		ui_check(&g_context->picker_select_material, tr("Select Material"), "");

		ui_check(&g_context->picker_material_mask, tr("Material Mask"), "");
		if (ui_item_changed()) {
			make_material_parse_paint_material(false);
		}

		ui_check(&g_context->picker_color_mask, tr("Color Mask"), "");
		if (ui_item_changed()) {
			make_material_parse_paint_material(false);
		}

		g_ui->enabled = g_context->picker_color_mask;
		ui_slider(&g_context->picker_color_mask_tolerance, tr("Tolerance"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		g_ui->enabled = true;

		i32 _w = g_ui->_w;
		g_ui->_w *= 1.4;
		ui_check(&g_context->picker_hide_other_materials, tr("Hide Other Materials"), "");
		g_ui->_w = _w;
		if (ui_item_changed()) {
			make_material_parse_mesh_material();
		}

		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_CENTER)) {
			g_context->picker_hide_other_materials = false;
			g_context->picker_material_mask        = false;
			g_context->picker_color_mask           = false;
			make_material_parse_mesh_material();
			make_material_parse_paint_material(false);
		}
	}
	else if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_FILL ||
	         g_context->tool == TOOL_TYPE_DECAL || g_context->tool == TOOL_TYPE_TEXT || g_context->tool == TOOL_TYPE_CLONE ||
	         g_context->tool == TOOL_TYPE_BLUR || g_context->tool == TOOL_TYPE_PARTICLE) {
		bool decal_mask = context_is_decal_mask();
		if (g_context->tool != TOOL_TYPE_FILL) {
			if (decal_mask) {
				ui_panels_radius_slider_id(1, &g_context->brush_decal_mask_radius);
				ui_slider(&g_context->brush_decal_mask_radius, tr("Radius"), 0.01, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
				if (g_ui->is_hovered) {
					any_map_t *vars = any_map_create();
					any_map_set(vars, "brush_radius", any_map_get(g_keymap, "brush_radius"));
					any_map_set(vars, "brush_radius_decrease", any_map_get(g_keymap, "brush_radius_decrease"));
					any_map_set(vars, "brush_radius_increase", any_map_get(g_keymap, "brush_radius_increase"));
					ui_tooltip(
					    vtr("Hold {brush_radius} and move mouse to the left or press {brush_radius_decrease} to decrease the radius\nHold {brush_radius} "
					        "and move mouse to the right or press {brush_radius_increase} to increase the radius",
					        vars));
					map_free(vars);
				}
			}
			else {
				ui_panels_radius_slider_id(1, &g_context->brush_radius);
				ui_slider(&g_context->brush_radius, tr("Radius"), 0.01, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
				if (g_ui->is_hovered) {
					any_map_t *vars = any_map_create();
					any_map_set(vars, "brush_radius", any_map_get(g_keymap, "brush_radius"));
					any_map_set(vars, "brush_radius_decrease", any_map_get(g_keymap, "brush_radius_decrease"));
					any_map_set(vars, "brush_radius_increase", any_map_get(g_keymap, "brush_radius_increase"));
					ui_tooltip(
					    vtr("Hold {brush_radius} and move mouse to the left or press {brush_radius_decrease} to decrease the radius\nHold {brush_radius} "
					        "and move mouse to the right or press {brush_radius_increase} to increase the radius",
					        vars));
					map_free(vars);
				}
			}
		}

		if (g_context->tool == TOOL_TYPE_DECAL || g_context->tool == TOOL_TYPE_TEXT) {
			ui_slider(&g_context->brush_scale_x, tr("Scale X"), 0.01, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		}

		// Masks and fill layers (painted through their mask): a gray value, and the fill layer's own material settings
		slot_layer_t *fill_target = tab_layers_fill_target(g_context->layer);
		bool          on_mask     = (slot_layer_is_mask(g_context->layer) && g_context->layer->fill_material == NULL) || fill_target == g_context->layer;
		if (on_mask && (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_FILL || g_context->tool == TOOL_TYPE_PARTICLE)) {
			ui_slider(&g_context->mask_value, tr("Value"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
			if (ui_button(tr("Swap"), UI_ALIGN_CENTER, "")) {
				g_context->mask_value = 1.0 - g_context->mask_value;
			}
			if (g_ui->is_hovered) {
				ui_tooltip(tr("White reveals the fill layer, black hides it"));
			}
		}
		bool fill_props = fill_target != NULL && g_context->tool != TOOL_TYPE_DECAL && g_context->tool != TOOL_TYPE_TEXT;
		if (fill_props && (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_FILL || g_context->tool == TOOL_TYPE_ERASER ||
		                   g_context->tool == TOOL_TYPE_BLUR || g_context->tool == TOOL_TYPE_PARTICLE)) {
			tab_layers_fill_properties(fill_target, false);
		}

		// On paint layers the material keeps its own texture scale and rotation, picking the material again brings them back
		bool decal_tool = g_context->tool == TOOL_TYPE_DECAL || g_context->tool == TOOL_TYPE_TEXT;
		if (!fill_props && !on_mask && (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_FILL)) {
			slot_material_t *m = g_context->material;
			ui_slider(&m->uv_scale, tr("UV Scale"), 0.01, 5.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
			if (ui_item_changed()) {
				project_modified = true;
			}
			ui_slider(&m->uv_angle, tr("UV Rotation"), 0.0, 360.0, true, 1, true, UI_ALIGN_RIGHT, true);
			if (ui_item_changed()) {
				project_modified = true;
			}
			if (g_ui->is_hovered) {
				ui_tooltip(tr("Rotation of the material's texture, saved with the material"));
			}
		}

		if (decal_tool) {
			ui_slider(&g_context->brush_scale, tr("UV Scale"), 0.01, 5.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
			if (ui_item_changed()) {
				gpu_texture_t *current = _draw_current;
				draw_end();
				util_render_make_decal_preview();
				draw_begin(current, false, 0);
			}
		}

		// Rotation of the brush tip (of the decal for decals), apart from the material's texture
		if (decal_tool || g_context->tool == TOOL_TYPE_BRUSH) {
			ui_slider(&g_context->brush_angle, decal_tool ? tr("Angle") : tr("Tip Angle"), 0.0, 360.0, true, 1, true, UI_ALIGN_RIGHT, true);
			bool angle_changed = ui_item_changed();
			if (g_ui->is_hovered) {
				any_map_t *vars = any_map_create();
				any_map_set(vars, "brush_angle", any_map_get(g_keymap, "brush_angle"));
				ui_tooltip(vtr(
				    "Hold {brush_angle} and move mouse to the left to decrease the angle\nHold {brush_angle} and move mouse to the right to increase the angle",
				    vars));
				map_free(vars);
			}

			if (angle_changed) {
				make_material_parse_paint_material(true);
			}
		}

		// Eraser rotates its brush mask, clone rotates the source around the brush
		if (g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE) {
			ui_slider(&g_context->brush_angle, tr("Angle"), 0.0, 360.0, true, 1, true, UI_ALIGN_RIGHT, true);
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}
		}

		ui_slider(&g_context->brush_opacity, tr("Opacity"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		g_context->brush_opacity = math_max(0.0, math_min(1.0, g_context->brush_opacity));
		if (g_ui->is_hovered) {
			any_map_t *vars = any_map_create();
			any_map_set(vars, "brush_opacity", any_map_get(g_keymap, "brush_opacity"));
			ui_tooltip(vtr("Hold {brush_opacity} and move mouse to the left to decrease the opacity\nHold {brush_opacity} and move mouse to the right to "
			               "increase the opacity",
			               vars));
			map_free(vars);
		}

		if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE || decal_mask ||
		    g_context->tool == TOOL_TYPE_PARTICLE) {
			ui_slider(&g_context->brush_hardness, tr("Hardness"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		}

		ui_header_draw_stroke_options(decal_mask);

		if (g_context->tool != TOOL_TYPE_ERASER) {
			string_array_t *brush_blending_combo = any_array_create_from_raw_tmp(
			    (void *[]){
			        tr("Mix"),
			        tr("Darken"),
			        tr("Multiply"),
			        tr("Burn"),
			        tr("Lighten"),
			        tr("Screen"),
			        tr("Dodge"),
			        tr("Add"),
			        tr("Overlay"),
			        tr("Soft Light"),
			        tr("Linear Light"),
			        tr("Difference"),
			        tr("Subtract"),
			        tr("Divide"),
			        tr("Hue"),
			        tr("Saturation"),
			        tr("Color"),
			        tr("Value"),
			    },
			    18);
			ui_combo((int *)&g_context->brush_blending, brush_blending_combo, tr("Blending"), false, UI_ALIGN_LEFT, true);
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}
		}

		if ((g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_FILL) && !fill_props) {
			string_array_t *texcoord_combo = any_array_create_from_raw_tmp(
			    (void *[]){
			        tr("UV Map"),
			        tr("Triplanar"),
			        tr("Project"),
			    },
			    3);
			ui_combo((int *)&g_context->brush_paint, texcoord_combo, tr("TexCoord"), false, UI_ALIGN_LEFT, true);
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}
		}

		if (g_context->tool == TOOL_TYPE_TEXT) {
			if (g_context->text_tool_text == NULL) {
				g_context->text_tool_text = "";
			}
			ui_id_t text_id = ui_widget_id(&g_context->text_tool_text, UI_ID_TEXT);
			i32     w       = g_ui->_w;
			if (g_ui->text_selected_id == text_id || g_ui->submit_text_id == text_id) {
				g_ui->_w *= 3;
			}
			ui_text_input(&g_context->text_tool_text, "", UI_ALIGN_LEFT, true, true);
			g_ui->_w = w;
			if (ui_item_changed()) {
				gpu_texture_t *current = _draw_current;
				draw_end();
				util_render_make_text_preview();
				util_render_make_decal_preview();
				draw_begin(current, false, 0);
			}
		}

		if (g_context->tool == TOOL_TYPE_PARTICLE) {
			if (ui_button(tr("Particle"), UI_ALIGN_CENTER, "")) {
				ui_menu_draw(&ui_header_particle_menu_draw, -1, -1);
			}
		}

		if (g_context->tool == TOOL_TYPE_CLONE) {
			string_array_t *clone_layers_combo = any_array_create_from_raw_tmp(
			    (void *[]){
			        tr("Selected"),
			        tr("Visible"),
			    },
			    2);
			ui_combo(&g_context->clone_layers, clone_layers_combo, tr("Layers"), false, UI_ALIGN_LEFT, true);
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}
			if (ui_button(g_context->clone_set_source ? tr("Setting Source") : tr("Set Source"), UI_ALIGN_CENTER, "")) {
				g_context->clone_set_source = !g_context->clone_set_source;
			}
		}

		if (g_context->tool == TOOL_TYPE_BLUR) {
			string_array_t *blur_type_combo = any_array_create_from_raw_tmp(
			    (void *[]){
			        tr("Blur"),
			        tr("Smudge"),
			    },
			    2);
			ui_combo(&g_context->blur_type, blur_type_combo, tr("Blur Type"), false, UI_ALIGN_LEFT, true);
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}
		}

		if (g_context->tool == TOOL_TYPE_FILL) {
			string_array_t *fill_mode_combo = any_array_create_from_raw_tmp(
			    (void *[]){
			        tr("Object"),
			        tr("Face"),
			        tr("Angle"),
			        tr("UV Island"),
			        tr("Mesh"),
			        tr("Submesh"),
			    },
			    6);
			ui_combo(&g_context->fill_type, fill_mode_combo, tr("Fill Mode"), false, UI_ALIGN_LEFT, true);
			if (ui_item_changed()) {
				if (g_context->fill_type == FILL_TYPE_FACE) {
					gpu_texture_t *current = _draw_current;
					draw_end();
					// cache_uv_map();
					util_uv_cache_triangle_map();
					draw_begin(current, false, 0);
					// draw_wireframe = true;
				}
				make_material_parse_paint_material(true);
				make_material_parse_mesh_material();
			}
		}
		else {
			ui_header_draw_toggles();

			i32  _w           = g_ui->_w;
			f32  sc           = UI_SCALE();
			bool touch_header = (g_config->touch_ui && g_config->header_pinned);
			if (touch_header) {
				g_ui->_x -= 4 * sc;
			}
			g_ui->_w = math_floor((touch_header ? 54 : 60) * sc);

			ui_check(&g_context->xray, tr("X-Ray"), "");
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}

			bool sym_changed = false;

			if (g_config->header_pinned) {
				if (g_config->touch_ui) {
					g_ui->_w = math_floor(19 * sc);
					ui_check(&g_context->sym_x, "", "");
					sym_changed |= ui_item_changed();
					g_ui->_x -= 4 * sc;
					ui_check(&g_context->sym_y, "", "");
					sym_changed |= ui_item_changed();
					g_ui->_x -= 4 * sc;
					ui_check(&g_context->sym_z, "", "");
					sym_changed |= ui_item_changed();
					g_ui->_x -= 4 * sc;
					g_ui->_w = math_floor(40 * sc);
					char *x  = tr("X");
					char *y  = tr("Y");
					char *z  = tr("Z");
					ui_text(string_tmp("%s%s%s", x, y, z), UI_ALIGN_LEFT, 0x00000000);
				}
				else {
					g_ui->_w = math_floor(60 * sc);
					ui_text(tr("Symmetry"), UI_ALIGN_LEFT, 0x00000000);
					g_ui->_w = math_floor(25 * sc);
					ui_check(&g_context->sym_x, tr("X"), "");
					sym_changed |= ui_item_changed();
					ui_check(&g_context->sym_y, tr("Y"), "");
					sym_changed |= ui_item_changed();
					ui_check(&g_context->sym_z, tr("Z"), "");
					sym_changed |= ui_item_changed();
				}
				g_ui->_w = _w;
			}
			else {
				// Popup
				g_ui->_w = _w;
				ui_check(&g_context->sym_x, string_tmp("%s %s", tr("Symmetry"), tr("X")), "");
				sym_changed |= ui_item_changed();
				ui_check(&g_context->sym_y, string_tmp("%s %s", tr("Symmetry"), tr("Y")), "");
				sym_changed |= ui_item_changed();
				ui_check(&g_context->sym_z, string_tmp("%s %s", tr("Symmetry"), tr("Z")), "");
				sym_changed |= ui_item_changed();
			}

			ui_header_draw_symmetry_extra(&sym_changed);

			if (sym_changed) {
				make_material_parse_paint_material(true);
			}
		}
	}
	else if (g_context->tool == TOOL_TYPE_INPAINT) {
		box_bake_maps_inpaint_header();
	}
}

void ui_header_draw_progress(f32 progress) {
	if (gpu_in_use) {
		return;
	}
	f64 t = iron_time();
	if (progress > 0.0 && progress < 1.0 && t - ui_header_progress_last < 1.0 / 30.0) {
		return;
	}
	ui_header_progress_last = t;
	i32  w                  = iron_window_width();
	i32  h                  = iron_window_height();
	i32  bw                 = w / 4;
	i32  bh                 = math_floor(16 * UI_SCALE()); // Centered on the screen
	i32  bx                 = (w - bw) / 2;
	i32  by                 = (h - bh) / 2;
	bool first_frame        = g_context->frame == 0;
	// The frame is presented on its own, the screen holds undefined content unless it is cleared
	if (base_startup_covering()) {
		draw_begin(NULL, true, 0xff000000);
	}
	else {
		draw_begin(NULL, first_frame, 0xff202020);
	}
	draw_set_color(g_theme->BUTTON_COL);
	draw_filled_rect(bx, by, bw, bh);
	draw_set_color(0xffe0393e); // Red
	draw_filled_rect(bx, by, bw * progress, bh);
	draw_end();
	gpu_present();
}
