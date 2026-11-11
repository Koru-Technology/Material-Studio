
#include "../global.h"

// Corner and rotate handles, larger on touch
i32 util_stencil_handle_size() {
	return g_config->touch_ui ? math_floor(44 * UI_SCALE()) : 16;
}

void util_stencil_transform() {
	if (context_is_stencil_editing()) {
		rect_t *r = ui_base_get_brush_stencil_rect();
		i32     h = util_stencil_handle_size();
		if (mouse_started("left")) {
			g_context->brush_stencil_scaling = ui_base_hit_rect(mouse_x, mouse_y, r->x - h / 2, r->y - h / 2, h, h) ||
			                                   ui_base_hit_rect(mouse_x, mouse_y, r->x - h / 2, r->h + r->y - h / 2, h, h) ||
			                                   ui_base_hit_rect(mouse_x, mouse_y, r->w + r->x - h / 2, r->y - h / 2, h, h) ||
			                                   ui_base_hit_rect(mouse_x, mouse_y, r->w + r->x - h / 2, r->h + r->y - h / 2, h, h);
			f32 cosa = math_cos(-g_context->brush_stencil_angle);
			f32 sina = math_sin(-g_context->brush_stencil_angle);
			f32 ox   = 0;
			f32 oy   = -r->h / 2.0;
			f32 x    = ox * cosa - oy * sina;
			f32 y    = ox * sina + oy * cosa;
			x += r->x + r->w / 2.0;
			y += r->y + r->h / 2.0;
			g_context->brush_stencil_rotating = ui_base_hit_rect(mouse_x, mouse_y, math_floor(x - h), math_floor(y - h), h * 2, h * 2);
		}
		f32 _scale = g_context->brush_stencil_scale;
		if (mouse_down("left")) {
			if (g_context->brush_stencil_scaling) {
				i32 mult = mouse_x > r->x + r->w / 2 ? 1 : -1;
				g_context->brush_stencil_scale += mouse_movement_x / 400.0 * mult;
			}
			else if (g_context->brush_stencil_rotating) {
				f32 gizmo_x                    = r->x + r->w / 2.0;
				f32 gizmo_y                    = r->y + r->h / 2.0;
				g_context->brush_stencil_angle = -math_atan2(mouse_y - gizmo_y, mouse_x - gizmo_x) - math_pi() / 2.0;
			}
			else {
				g_context->brush_stencil_x += mouse_movement_x / (float)base_w();
				g_context->brush_stencil_y += mouse_movement_y / (float)base_h();
			}
		}
		else {
			g_context->brush_stencil_scaling = false;
		}
		if (mouse_wheel_delta != 0) {
			g_context->brush_stencil_scale -= mouse_wheel_delta / 10.0;
		}
		// Center after scale
		f32 ratio = base_h() / (float)g_context->brush_stencil_image->height;
		f32 old_w = _scale * g_context->brush_stencil_image->width * ratio;
		f32 new_w = g_context->brush_stencil_scale * g_context->brush_stencil_image->width * ratio;
		f32 old_h = _scale * base_h();
		f32 new_h = g_context->brush_stencil_scale * base_h();
		g_context->brush_stencil_x += (old_w - new_w) / (float)base_w() / 2.0;
		g_context->brush_stencil_y += (old_h - new_h) / (float)base_h() / 2.0;
	}
}
