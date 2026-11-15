#include "global.h"

// Generator pass cost at 4K on a dense mesh: run with IRON_PAINT_TIMING, read the [mask_gen] lines
int           step  = 0;
int           frame = 0;
slot_layer_t *mask  = NULL;

void next() {
	frame = frame + 1;
	if (frame < 6) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;
	if (step == 0) {
		script_project_new_template("sphere_2048");
	}
	else if (step == 1) {
		config_set_texture_res(1); // 4096
		layers_resize();
	}
	else if (step == 2) {
		slot_layer_t *l = script_layer_new_color(0xffffffff, 1.0, 0.5, 0.0);
		mask            = script_layer_new_mask(l, 0);
	}
	else if (step >= 3 && step <= 6) {
		script_mask_add_effect(mask, 1 + step, -1); // Edge Wear, Dirt, Position, Directional with default noise
	}
	else if (step >= 7 && step <= 12) {
		script_mask_set_effect_param(mask, 1, 0, 0.05 * step); // A slider drag on the bottom generator
	}
	else {
		printf("[mask_gen_timing] mask %d px", slot_layer_mask_texture(mask)->width);
		config_set_texture_res(0);
		layers_resize();
		printf("MASK_GEN_TIMING DONE");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
