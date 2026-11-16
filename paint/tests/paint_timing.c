#include "global.h"

// Paint timing on fixed strokes, the baseline of the paint performance gate. Run with IRON_PAINT_TIMING=1: every paint
// executor frame logs "[paint] t <ms> dabs <n> cpu <ms> gpu <ms>" to stderr. Each case below drags the mouse through the
// same per frame path as a finger or pencil stroke, between "[paint_timing] begin <case>" and "end <case>" lines; the
// lines of a case are summed by tests/paint_timing.sh.
// Cases: batched stamps, unbatched stamps (batching off), symmetry (one draw per copy), a continuous brush (spacing 0),
// with paint_resolved_dabs off, then on (names end in _off, _on).

int frame = 0;
int phase = 0;
int kase  = 0;
int frames_painted = 0;

char *case_kind(int k) {
	k = k % 4;
	if (k == 0) {
		return "batched";
	}
	if (k == 1) {
		return "unbatched";
	}
	if (k == 2) {
		return "symmetry";
	}
	return "continuous";
}

char *case_name(int k) {
	return string("%s_%s", case_kind(k), k >= 4 ? "on" : "off");
}

void case_setup(int k) {
	script_get_config()->paint_resolved_dabs = k >= 4;
	k                  = k % 4;
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.5;
	ctx->brush_opacity = 1.0;
	ctx->sym_x         = k == 2;
	ctx->brush_spacing = k == 3 ? 0.0 : 0.03;
	make_paint_set_batching(k != 1); // Parses the paint material again
}

void next() {
	frame  = frame + 1;
	int cx = sys_x() + sys_w() / 2;
	int cy = sys_h() / 2;
	int y  = cy + (kase % 2) * 40 - 20;
	if (phase == 0) { // Settle
		if (frame > 20) {
			case_setup(kase);
			phase = 1;
			frame = 0;
		}
	}
	else if (phase == 1) {
		if (frame > 5) {
			printf("[paint_timing] begin %s batched %d", case_name(kase), make_paint_is_batched());
			script_mouse_press(cx - 360, y, 0);
			phase = 2;
			frame = 0;
		}
	}
	else if (phase == 2) { // Drag, 60 frames
		script_mouse_move(cx - 360 + frame * 12, y + (frame % 20) - 10);
		if (frame >= 60) {
			script_mouse_release(cx - 360 + frame * 12, y, 0);
			phase = 3;
			frame = 0;
		}
	}
	else if (phase == 3) {
		if (frame > 30) { // GPU times of the case have landed
			printf("[paint_timing] end %s frames 60", case_name(kase));
			kase  = kase + 1;
			phase = 0;
			frame = 0;
			if (kase == 8) {
				script_get_config()->paint_resolved_dabs = 0;
				printf("PAINT_TIMING PASS");
				script_quit();
				return;
			}
		}
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_brush_defaults();
	script_project_new();
	slot_material_t *m   = script_material_create("timing_blue");
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, 0.1, 0.3, 0.9, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	render_path_paint_set_stamp_seed(4242);
	context_select_tool(TOOL_TYPE_BRUSH);
	script_notify_on_next_frame(next);
}
