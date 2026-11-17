#include "../global.h"

// The views share the window: the 3D view on the left, the 2D view above the node editor on the right.
// A hidden view gives its space to the others.

#if defined(IRON_IOS)
#define DOCK_RIGHT_RATIO 0.43
#else
#define DOCK_RIGHT_RATIO 0.47
#endif

static bool ui_dock_shown[DOCK_VIEW_COUNT] = {true, false, false};
static bool ui_dock_laid_out               = false;
static i32  dock_view3d_w                  = 1;
static i32  dock_view3d_h                  = 1;

void ui_dock_layout() {
	i32  w     = iron_window_width();
	i32  h     = iron_window_height();
	bool v3d   = ui_dock_shown[DOCK_VIEW_3D];
	bool v2d   = ui_dock_shown[DOCK_VIEW_2D];
	bool nodes = ui_dock_shown[DOCK_VIEW_NODES];

	// The 2D view and the node editor share the right column, the full width without the 3D view
	i32 right_w = !v2d && !nodes ? 0 : !v3d ? w : math_floor(w * DOCK_RIGHT_RATIO);
	i32 left_w  = w - right_w;
	i32 split_y = v2d && nodes ? math_floor(h / 2.0) : v2d ? h : 0;

	base_view3d_show = v3d;
	ui_view2d_show   = v2d;
	ui_nodes_show    = nodes;

	if (v2d) {
		ui_view2d_wx = left_w;
		ui_view2d_wy = 0;
		ui_view2d_ww = right_w;
		ui_view2d_wh = split_y;
	}
	if (nodes) {
		ui_nodes_wx = left_w;
		ui_nodes_wy = split_y;
		ui_nodes_ww = right_w;
		ui_nodes_wh = h - split_y;
	}
	if (v3d) {
		dock_view3d_w = left_w;
		dock_view3d_h = h;
		base_appx     = 0;
		base_appy     = 0;
	}
	else {
		dock_view3d_w = 1;
		dock_view3d_h = 1;
		base_appx     = -1;
		base_appy     = 0;
	}
	ui_dock_laid_out = true;
}

// The 3D view alone
void ui_dock_reset() {
	ui_dock_shown[DOCK_VIEW_3D]    = true;
	ui_dock_shown[DOCK_VIEW_2D]    = false;
	ui_dock_shown[DOCK_VIEW_NODES] = false;
	ui_dock_layout();
}

void ui_dock_show_view(dock_view_t view, bool show) {
	ui_dock_shown[view] = show;
	ui_dock_layout();
}

i32 ui_dock_view3d_w() {
	return ui_dock_laid_out ? dock_view3d_w : iron_window_width();
}

i32 ui_dock_view3d_h() {
	return ui_dock_laid_out ? dock_view3d_h : iron_window_height();
}
