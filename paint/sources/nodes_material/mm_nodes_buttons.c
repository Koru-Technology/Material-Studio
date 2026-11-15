#include "../global.h"
#include "../material_maker/mm_internal.h"
#include <math.h>

// Material Maker nodes (mm_nodes.c): the custom buttons drawn in the node editor (labels, values, seeds, strings,
// gradients, curves). Included after mm_nodes_sync.c.

// Custom buttons

// Label of the parameter whose button it is
static char *mm_nodes_button_label(ui_node_t *node, i32 button) {
	mm_mark_t         mk    = mm_arena_mark();
	i32               count = 0;
	mm_nodes_param_t *ps    = mm_nodes_layout(node, &count);
	char             *label = "";
	for (i32 i = 0; i < count; ++i) {
		if (ps[i].button == button) {
			label = string_tmp("%s", ps[i].label);
			break;
		}
	}
	mm_arena_release(mk);
	return label;
}

static ui_node_t *mm_nodes_drawn_node(i32 node_id, ui_node_button_t **but) {
	ui_node_t *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	i32        i    = ui_nodes_custom_button_index;
	if (node == NULL || i < 0 || i >= node->buttons->length) {
		return NULL;
	}
	*but = node->buttons->buffer[i];
	return node;
}

// Editor state of one button, a node may have several gradients or curves
static ui_nodes_editor_state_t *mm_nodes_ui_state(ui_node_t *node, i32 button) {
	ui_node_t key = *node;
	key.type      = string_tmp("%s#%d", node->type, button);
	return ui_nodes_editor_state(&key);
}

static void mm_nodes_label_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node != NULL) {
		ui_text(but->data != NULL ? (char *)but->data->buffer : "", UI_ALIGN_LEFT, 0);
	}
}

static void mm_nodes_value_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node != NULL) {
		ui_text(mm_nodes_button_label(node, ui_nodes_custom_button_index), UI_ALIGN_LEFT, 0);
	}
}

static void mm_nodes_seed_button(i32 node_id) {
	// Hidden, keeps the seed
}

// Hidden, keeps the state of a dynamic node and keeps the node consistent as it is drawn (mm_node_update)
static void mm_nodes_state_button_draw(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node != NULL) {
		mm_node_update(node, ui_nodes_get_canvas(true));
	}
}

// Text field of a string, file or image path parameter, kept in the button data
static void mm_nodes_string_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node == NULL) {
		return;
	}
	char *text = but->data != NULL && but->data->length > 0 ? (char *)but->data->buffer : "";
	ui_set_next_id(((ui_id_t)node->id << 16) + ((ui_nodes_custom_button_index * 4) & 0xffff) + 1); // As the editor keys its controls
	char *edited = ui_text_input(&text, mm_nodes_button_label(node, ui_nodes_custom_button_index), UI_ALIGN_LEFT, true, false);
	if (edited != NULL && (but->data == NULL || strcmp(edited, (char *)but->data->buffer) != 0)) {
		but->data = mm_nodes_text_data(edited);
	}
}

static void mm_nodes_gradient_eval(f32 *v, i32 n, i32 interp, f32 t, f32 *out) {
	// Stops sorted by position (insertion sort of indices)
	i32 order[64];
	n = n > 64 ? 64 : n;
	for (i32 i = 0; i < n; ++i) {
		i32 j = i;
		while (j > 0 && v[order[j - 1] * 5 + 4] > v[i * 5 + 4]) {
			order[j] = order[j - 1];
			j--;
		}
		order[j] = i;
	}
	i32 k = 0;
	while (k < n && v[order[k] * 5 + 4] < t) {
		k++;
	}
	f32 *a = &v[order[k > 0 ? k - 1 : 0] * 5];
	f32 *b = &v[order[k < n ? k : n - 1] * 5];
	f32  f = b[4] > a[4] ? (t - a[4]) / (b[4] - a[4]) : 0.0;
	f      = f < 0 ? 0 : f > 1 ? 1 : f;
	if (interp == 0) { // Constant: the stop before
		f = k < n && t >= b[4] ? 1.0 : 0.0;
	}
	else if (interp >= 2) { // Smooth, cubic shown smooth
		f = f * f * (3.0 - 2.0 * f);
	}
	for (i32 i = 0; i < 4; ++i) {
		out[i] = a[i] + (b[i] - a[i]) * f;
	}
}

static void mm_nodes_gradient_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node != NULL) {
		mm_nodes_gradient_edit(node, ui_nodes_custom_button_index, mm_nodes_button_label(node, ui_nodes_custom_button_index));
	}
}

// The gradient editor of a button holding [r,g,b,a,pos]*n, data[0] the interpolation: preview, + / -, interpolation,
// the stop's index, position and color. Also the Variable node's (variable_node.c).
void mm_nodes_gradient_edit(ui_node_t *node, i32 bi, char *label) {
	ui_node_button_t *but = bi >= 0 && bi < node->buttons->length ? node->buttons->buffer[bi] : NULL;
	if (but == NULL || but->default_value == NULL || but->default_value->length < 5) {
		return;
	}
	ui_nodes_t              *nodes = ui_nodes_get_nodes();
	ui_nodes_editor_state_t *state = mm_nodes_ui_state(node, bi);
	f32_array_t             *vals  = but->default_value;
	if (but->data == NULL || but->data->length < 1) {
		but->data            = u8_array_create(1);
		but->data->buffer[0] = 1;
	}
	f32 nx = g_ui->_x;
	f32 sw = g_ui->_w / (float)UI_NODES_SCALE();
	ui_text(label, UI_ALIGN_LEFT, 0);

	// Preview
	i32 len    = (i32)(vals->length / 5);
	i32 interp = but->data->buffer[0];
	for (i32 s = 0; s < 64; s++) {
		f32 t = (f32)s / 64.0;
		f32 c[4];
		mm_nodes_gradient_eval(vals->buffer, len, interp, t, c);
		ui_fill(t * sw, 0, sw / 64.0 + 1.0 / UI_NODES_SCALE(), UI_LINE_H() - 2 * UI_NODES_SCALE(), color_from_floats(c[0], c[1], c[2], 1.0));
	}
	g_ui->_y += UI_LINE_H();

	f32_array_t *row = f32_array_create_from_raw_tmp((f32[]){1 / 4.0, 1 / 4.0, 2 / 4.0}, 3);
	ui_row(row);
	if (ui_button("+", UI_ALIGN_CENTER, "") && len < 64) {
		i32 last = vals->length - 5;
		for (i32 i = 0; i < 4; ++i) {
			f32_array_push(vals, vals->buffer[last + i]);
		}
		f32_array_push(vals, 1.0);
		state->index[0] = len;
	}
	if (ui_button("-", UI_ALIGN_CENTER, "") && vals->length > 5) {
		vals->length -= 5;
		state->index[0] -= 1;
	}
	string_array_t *modes = any_array_create_from_raw_tmp((void *[]){tr("Constant"), tr("Linear"), tr("Smooth"), tr("Cubic")}, 4);
	state->interp         = but->data->buffer[0];
	but->data->buffer[0]  = ui_combo(&state->interp, modes, tr("Interpolate"), false, UI_ALIGN_LEFT, true);

	ui_row2();
	len   = (i32)(vals->length / 5);
	i32 i = math_floor(ui_slider(&state->index[0], "Index", 0, len - 1, false, 1, true, UI_ALIGN_LEFT, true));
	if (i >= len || i < 0) {
		state->index[0] = i = len - 1;
	}
	state->x                = vals->buffer[i * 5 + 4];
	vals->buffer[i * 5 + 4] = math_max(0.0, math_min(1.0, ui_slider(&state->x, "Pos", 0, 1, true, 100, true, UI_ALIGN_LEFT, true)));
	u32 color               = color_from_floats(vals->buffer[i * 5 + 0], vals->buffer[i * 5 + 1], vals->buffer[i * 5 + 2], 1.0);
	if (ui_text("", UI_ALIGN_RIGHT, color) == UI_STATE_STARTED) {
		f32 rx                = nx + g_ui->_w - ui_p(37);
		f32 ry                = g_ui->_y - ui_p(5);
		nodes->_input_started = g_ui->input_started = false;
		ui_nodes_rgba_popup(&state->color_state, vals->buffer + i * 5, math_floor(rx), math_floor(ry + UI_ELEMENT_H()));
	}
}

// MMCurve: cubic Hermite between points, ls / rs the slopes left and right of each point
static f32 mm_nodes_curve_eval(f32 *v, i32 n, f32 x) {
	if (n < 2) {
		return n == 1 ? v[1] : x;
	}
	if (x <= v[0]) {
		return v[1];
	}
	for (i32 i = 0; i < n - 1; ++i) {
		f32 *a = &v[i * 4];
		f32 *b = &v[(i + 1) * 4];
		if (x <= b[0]) {
			f32 dx = b[0] - a[0];
			if (dx <= 0.00001) {
				return b[1];
			}
			f32 t   = (x - a[0]) / dx;
			f32 t2  = t * t;
			f32 t3  = t2 * t;
			f32 h00 = 2 * t3 - 3 * t2 + 1;
			f32 h10 = t3 - 2 * t2 + t;
			f32 h01 = -2 * t3 + 3 * t2;
			f32 h11 = t3 - t2;
			return h00 * a[1] + h10 * dx * a[3] + h01 * b[1] + h11 * dx * b[2];
		}
	}
	return v[(n - 1) * 4 + 1];
}

static void mm_nodes_curve_button(i32 node_id) {
	ui_node_button_t *but  = NULL;
	ui_node_t        *node = mm_nodes_drawn_node(node_id, &but);
	if (node == NULL || but->default_value == NULL || but->default_value->length < 8) {
		return;
	}
	i32                      bi    = ui_nodes_custom_button_index;
	ui_nodes_editor_state_t *state = mm_nodes_ui_state(node, bi);
	char                    *label = mm_nodes_button_label(node, bi);
	f32_array_t             *val   = but->default_value;
	i32                      num   = (i32)(val->length / 4);
	ui_text(label, UI_ALIGN_LEFT, 0);

	f32 sw  = g_ui->_w / (float)UI_NODES_SCALE();
	f32 ph  = UI_LINE_H() * 4 - 2 * UI_NODES_SCALE();
	f32 phs = ph * UI_SCALE();
	f32 pws = sw * UI_SCALE();
	f32 bx  = g_ui->_x;
	f32 by  = g_ui->_y;
	ui_fill(0, 0, sw, ph, 0xff1a1a1a);
	ui_fill(0, ph * 0.5f, sw, 1.0f / UI_NODES_SCALE(), 0xff333333);
	ui_fill(sw * 0.5f, 0, 1.0f / UI_NODES_SCALE(), ph, 0xff333333);
	draw_set_color(0xffffffff);
	f32 px = 0, py = 0;
	for (i32 s = 0; s <= 64; s++) {
		f32 t  = (f32)s / 64.0f;
		f32 y  = mm_nodes_curve_eval(val->buffer, num, t);
		f32 ax = bx + t * pws;
		f32 ay = math_max(by, math_min(by + phs, by + (1.0f - y) * phs));
		if (s > 0) {
			draw_line_aa(px, py, ax, ay, 1.5f);
		}
		px = ax;
		py = ay;
	}
	g_ui->_y += UI_LINE_H() * 4;

	f32_array_t *row = f32_array_create_from_raw_tmp((f32[]){1 / 5.0, 1 / 5.0, 3 / 5.0}, 3);
	ui_row(row);
	i32 i = math_floor(state->index[0]);
	i     = i < 0 ? 0 : i >= num ? num - 1 : i;
	if (ui_button("+", UI_ALIGN_CENTER, "") && num < 32) { // A point halfway to the next one, on the curve
		i32 a  = i < num - 1 ? i : i - 1;
		f32 x  = (val->buffer[a * 4] + val->buffer[(a + 1) * 4]) * 0.5;
		f32 y  = mm_nodes_curve_eval(val->buffer, num, x);
		f32 dx = val->buffer[(a + 1) * 4] - val->buffer[a * 4];
		f32 sl = dx > 0.00001 ? (val->buffer[(a + 1) * 4 + 1] - val->buffer[a * 4 + 1]) / dx : 0;
		for (i32 k = 0; k < 4; ++k) {
			f32_array_push(val, 0);
		}
		for (i32 k = val->length - 1; k >= (a + 2) * 4; --k) {
			val->buffer[k] = val->buffer[k - 4];
		}
		f32 *np         = &val->buffer[(a + 1) * 4];
		np[0]           = x;
		np[1]           = y;
		np[2]           = sl;
		np[3]           = sl;
		num++;
		state->index[0] = a + 1;
	}
	if (ui_button("-", UI_ALIGN_CENTER, "") && num > 2) {
		for (i32 k = i * 4; k < (i32)val->length - 4; ++k) {
			val->buffer[k] = val->buffer[k + 4];
		}
		val->length -= 4;
		num--;
	}
	i = math_floor(ui_slider(&state->index[0], "Index", 0, num - 1, false, 1, true, UI_ALIGN_LEFT, true));
	if (i >= num || i < 0) {
		state->index[0] = i = num - 1;
	}
	ui_row2();
	f32 lo   = i > 0 ? val->buffer[(i - 1) * 4] : 0;
	f32 hi   = i < num - 1 ? val->buffer[(i + 1) * 4] : 1;
	state->x = val->buffer[i * 4 + 0];
	state->y = val->buffer[i * 4 + 1];
	f32 x    = ui_slider(&state->x, "X", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
	val->buffer[i * 4 + 0] = math_max(lo, math_min(hi, x)); // Points stay sorted
	val->buffer[i * 4 + 1] = ui_slider(&state->y, "Y", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
}
