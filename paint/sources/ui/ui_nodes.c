
#include "../global.h"

bool              ui_nodes_show_menu       = false;
bool              ui_nodes_show_menu_first = true;
bool              ui_nodes_hide_menu       = false;
i32               ui_nodes_menu_category   = 0;
f32               ui_nodes_popup_x         = 0.0;
f32               ui_nodes_popup_y         = 0.0;
i32               ui_nodes_node_search_x;
i32               ui_nodes_node_search_y;
bool              ui_nodes_uichanged_last        = false;
static bool       ui_nodes_popup_changed         = false;
bool              ui_nodes_recompile_mat         = false; // Mat preview
bool              ui_nodes_recompile_mat_final   = false;
ui_node_t        *ui_nodes_node_search_spawn     = NULL;
i32               ui_nodes_node_search_offset    = 0;
ui_node_canvas_t *ui_nodes_last_canvas           = NULL;
i32               ui_nodes_last_node_selected_id = -1;
bool              ui_nodes_release_link          = false;
bool              ui_nodes_is_node_menu_op       = false;
gpu_texture_t    *ui_nodes_grid                  = NULL;
bool              ui_nodes_controls_down         = false;
static char      *ui_nodes_canvas_name_prev      = NULL;
ui_node_link_t   *_ui_nodes_on_link_drag_link_drag;
ui_node_t        *_ui_nodes_on_link_drag_node;
ui_node_socket_t *_ui_nodes_on_socket_released_socket;
ui_node_t        *_ui_nodes_on_socket_released_node;
ui_node_t        *_ui_nodes_on_canvas_released_selected;
bool              _ui_nodes_node_search_first;
void (*_ui_nodes_node_search_done)(void);
ui_node_t *ui_nodes_node_changed = NULL;
void (*_ui_nodes_render_tmp)(i32);
any_map_t *_ui_nodes_editor_states = NULL;

ui_nodes_editor_state_t *ui_nodes_editor_state(ui_node_t *node) {
	if (_ui_nodes_editor_states == NULL) {
		_ui_nodes_editor_states = any_map_create();
	}
	char                    *key   = string_tmp("%s:%d", node->type, node->id);
	ui_nodes_editor_state_t *state = any_map_get(_ui_nodes_editor_states, key);
	if (state == NULL) {
		state = calloc(1, sizeof(ui_nodes_editor_state_t)); // Never freed, the color popup keeps a pointer
		any_map_set(_ui_nodes_editor_states, string_copy(key), state);
	}
	return state;
}


// Row toggling the code area of a node, the state is saved with the node
bool ui_nodes_code_panel(ui_node_t *node) {
	bool expanded = !(node->flags & UI_NODE_FLAG_CODE_HIDE);
	if (ui_is_visible(UI_ELEMENT_H())) {
		if (ui_get_released(UI_ELEMENT_H())) {
			expanded = !expanded;
		}
		// Down or right pointing triangle
		f32 x = g_ui->_x + g_ui->arrow_offset_x;
		f32 y = g_ui->_y + g_ui->arrow_offset_y;
		f32 s = UI_ARROW_SIZE();
		draw_set_color(g_ui->ops->theme->LABEL_COL);
		if (expanded) {
			draw_filled_triangle(x, y, x + s, y, x + s / 2.0, y + s);
		}
		else {
			draw_filled_triangle(x, y, x + s, y + s / 2.0, x, y + s);
		}
	}
	ui_end_element();
	if (expanded) {
		node->flags &= ~UI_NODE_FLAG_CODE_HIDE;
	}
	else {
		node->flags |= UI_NODE_FLAG_CODE_HIDE;
	}
	return expanded;
}

void ui_viewnodes_on_link_drag_on_node_search_done() {
	ui_nodes_t *ui_nodes = ui_nodes_get_nodes();

	i32 node_selected_id = _ui_nodes_on_link_drag_node->id;
	if (ui_nodes->nodes_selected_id->length > 0) {
		node_selected_id = ui_nodes->nodes_selected_id->buffer[0];
	}

	ui_node_link_t    *link_drag = _ui_nodes_on_link_drag_link_drag;
	ui_node_t_array_t *nodes     = ui_nodes_get_canvas(true)->nodes;
	ui_node_t         *n         = ui_get_node(nodes, node_selected_id);
	if (link_drag->to_id == -1 && n->inputs->length > 0) {
		char *from_type = _ui_nodes_on_link_drag_node->outputs->buffer[link_drag->from_socket]->type;
		// The first type-matching socket, else the first one the type can link to (Material Maker types link to few)
		i32 to_socket = -1;
		for (i32 i = 0; i < n->inputs->length && to_socket == -1; ++i) {
			if (string_equals(n->inputs->buffer[i]->type, from_type)) {
				to_socket = i;
			}
		}
		for (i32 i = 0; i < n->inputs->length && to_socket == -1; ++i) {
			if (ui_nodes_socket_types_compatible(from_type, n->inputs->buffer[i]->type)) {
				to_socket = i;
			}
		}
		if (to_socket >= 0) {
			link_drag->to_id     = n->id;
			link_drag->to_socket = to_socket;
			any_array_push(ui_nodes_get_canvas(true)->links, link_drag);
		}
	}
	else if (link_drag->from_id == -1 && n->outputs->length > 0) {
		char *to_type     = _ui_nodes_on_link_drag_node->inputs->buffer[link_drag->to_socket]->type;
		i32   from_socket = -1;
		for (i32 i = 0; i < n->outputs->length && from_socket == -1; ++i) {
			if (ui_nodes_socket_types_compatible(n->outputs->buffer[i]->type, to_type)) {
				from_socket = i;
			}
		}
		if (from_socket >= 0) {
			link_drag->from_id     = n->id;
			link_drag->from_socket = from_socket;
			any_array_push(ui_nodes_get_canvas(true)->links, link_drag);
		}
	}
}

static char *_ui_nodes_node_search_text   = "";
static i32   _ui_nodes_node_search_scroll = 0; // First result shown, the list scrolls with the selection and the mouse wheel
any_map_t   *ui_nodes_menu_paths          = NULL;

// Node type to an add menu path like "Material Maker/Noise/Fbm", kept outside ui_node_t as canvases are decoded positionally.
// The folders of the path become sub menus of the node's category, a leading folder named like the category is skipped.
void ui_nodes_set_menu_path(char *node_type, char *path) {
	if (ui_nodes_menu_paths == NULL) {
		ui_nodes_menu_paths = any_map_create();
	}
	any_map_set(ui_nodes_menu_paths, string_copy(node_type), path != NULL ? string_copy(path) : NULL);
}

char *ui_nodes_get_menu_path(char *node_type) {
	return ui_nodes_menu_paths != NULL ? any_map_get(ui_nodes_menu_paths, node_type) : NULL;
}

static char ui_nodes_lower(char c) {
	return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

// Case insensitive substring test, to_lower_case would allocate for every node on every frame
static bool ui_nodes_search_has(char *s, char *search) {
	if (search[0] == '\0') {
		return true;
	}
	for (i32 i = 0; s != NULL && s[i] != '\0'; ++i) {
		i32 j = 0;
		while (search[j] != '\0' && ui_nodes_lower(s[i + j]) == ui_nodes_lower(search[j])) {
			j++;
		}
		if (search[j] == '\0') {
			return true;
		}
	}
	return false;
}

static bool ui_nodes_search_match(ui_node_t *n, char *search) {
	return ui_nodes_search_has(tr(n->name), search) || ui_nodes_search_has(n->type, search) || ui_nodes_search_has(ui_nodes_get_menu_path(n->type), search);
}

// Last folder of a menu path, shown next to search results to tell apart nodes with the same name
static char *ui_nodes_menu_path_folder(char *path) {
	char *last = path != NULL ? strrchr(path, '/') : NULL;
	if (last == NULL) {
		return "";
	}
	char *first = last;
	while (first > path && first[-1] != '/') {
		first--;
	}
	i32   len    = (i32)(last - first);
	char *folder = string_tmp_alloc(len + 1);
	memcpy(folder, first, len);
	return folder;
}

void ui_nodes_node_search_menu() {
	i32 rows             = math_floor(math_min(20, math_max(4, iron_window_height() / UI_ELEMENT_H() - 3)));
	ui_menu_h            = UI_ELEMENT_H() * (rows + 1);
	char *search         = ui_text_input(&_ui_nodes_node_search_text, "", UI_ALIGN_LEFT, true, true);
	bool  search_changed = ui_item_changed();
	g_ui->changed        = false;
	if (_ui_nodes_node_search_first) {
		_ui_nodes_node_search_first  = false;
		_ui_nodes_node_search_text   = "";
		_ui_nodes_node_search_scroll = 0;
		ui_nodes_node_search_offset  = 0;
		ui_start_text_edit(&_ui_nodes_node_search_text, UI_ALIGN_LEFT); // Focus search bar
	}

	if (search_changed) {
		ui_nodes_node_search_offset  = 0;
		_ui_nodes_node_search_scroll = 0;
	}

	node_list_t_array_t *node_list = ui_nodes_get_node_list();
	i32                  total     = 0;
	for (i32 i = 0; i < node_list->length; ++i) {
		ui_node_t_array_t *list = node_list->buffer[i];
		for (i32 j = 0; j < list->length; ++j) {
			if (ui_nodes_search_match(list->buffer[j], search)) {
				total++;
			}
		}
	}

	bool key_moved = false;
	if (g_ui->is_key_pressed) { // Move selection
		if (g_ui->key_code == KEY_CODE_DOWN && ui_nodes_node_search_offset < total - 1) {
			ui_nodes_node_search_offset++;
			key_moved = true;
		}
		if (g_ui->key_code == KEY_CODE_UP && ui_nodes_node_search_offset > 0) {
			ui_nodes_node_search_offset--;
			key_moved = true;
		}
	}
	if (key_moved) { // Keep the selection in view
		if (ui_nodes_node_search_offset < _ui_nodes_node_search_scroll) {
			_ui_nodes_node_search_scroll = ui_nodes_node_search_offset;
		}
		if (ui_nodes_node_search_offset >= _ui_nodes_node_search_scroll + rows) {
			_ui_nodes_node_search_scroll = ui_nodes_node_search_offset - rows + 1;
		}
	}
	else if (g_ui->input_wheel_delta != 0) {
		_ui_nodes_node_search_scroll += g_ui->input_wheel_delta > 0 ? 3 : -3;
	}
	_ui_nodes_node_search_scroll = math_floor(math_max(0, math_min(_ui_nodes_node_search_scroll, total - rows)));
	// Scrolling drags the selection along, enter picks what is shown highlighted
	ui_nodes_node_search_offset = math_floor(math_max(_ui_nodes_node_search_scroll, math_min(ui_nodes_node_search_offset, _ui_nodes_node_search_scroll + rows - 1)));
	ui_nodes_node_search_offset = math_floor(math_max(0, math_min(ui_nodes_node_search_offset, total - 1)));

	bool enter              = keyboard_down("enter");
	i32  count              = 0;
	bool done               = false;
	i32  _BUTTON_COL        = g_theme->BUTTON_COL;
	bool _FILL_BUTTON_BG    = g_theme->FILL_BUTTON_BG;
	g_theme->FILL_BUTTON_BG = true;
	bool _SHADOWS           = g_theme->SHADOWS;
	g_theme->SHADOWS        = false;

	for (i32 i = 0; i < node_list->length && !done; ++i) {
		ui_node_t_array_t *list = node_list->buffer[i];
		for (i32 j = 0; j < list->length && !done; ++j) {
			ui_node_t *n = list->buffer[j];
			if (!ui_nodes_search_match(n, search)) {
				continue;
			}
			i32 index = count++;
			if (index < _ui_nodes_node_search_scroll) {
				continue;
			}
			if (index >= _ui_nodes_node_search_scroll + rows) {
				done = true;
				break;
			}
			g_theme->BUTTON_COL = index == ui_nodes_node_search_offset ? g_theme->HIGHLIGHT_COL : g_theme->SEPARATOR_COL;

			char *folder = ui_nodes_menu_path_folder(ui_nodes_get_menu_path(n->type));
			if (ui_button(tr(n->name), UI_ALIGN_LEFT, folder) || (enter && index == ui_nodes_node_search_offset)) {
				ui_nodes_push_undo(NULL);
				ui_nodes_t       *nodes    = ui_nodes_get_nodes();
				ui_node_canvas_t *canvas   = ui_nodes_get_canvas(true);
				ui_nodes_node_search_spawn = ui_nodes_make_node(n, nodes, canvas); // Spawn selected node
				any_array_push(canvas->nodes, ui_nodes_node_search_spawn);
				nodes->nodes_selected_id = i32_array_create_from_raw(
				    (i32[]){
				        ui_nodes_node_search_spawn->id,
				    },
				    1);
				nodes->nodes_drag = true;

				ui_nodes_hwnd->redraws = 2;
				if (enter) {
					g_ui->changed = true;
					done          = true;
				}
				if (_ui_nodes_node_search_done != NULL) {
					_ui_nodes_node_search_done();
				}
			}
		}
	}
	if (enter && total == 0) { // Hide popup on enter when node is not found
		g_ui->changed              = true;
		_ui_nodes_node_search_text = "";
	}
	g_theme->BUTTON_COL     = _BUTTON_COL;
	g_theme->FILL_BUTTON_BG = _FILL_BUTTON_BG;
	g_theme->SHADOWS        = _SHADOWS;
}

void ui_nodes_node_search(i32 x, i32 y, void (*done)(void)) {
	_ui_nodes_node_search_first = true;
	_ui_nodes_node_search_done  = done;
	ui_menu_draw(&ui_nodes_node_search_menu, x, y);
}

void ui_viewnodes_on_link_drag(i32 link_drag_id, bool is_new_link) {
	if (!is_new_link) {
		return;
	}

	ui_node_link_t_array_t *links     = ui_nodes_get_canvas(true)->links;
	ui_node_link_t         *link_drag = ui_get_link(links, link_drag_id);
	ui_node_t_array_t      *nodes     = ui_nodes_get_canvas(true)->nodes;
	ui_node_t              *node      = ui_get_node(nodes, link_drag->from_id > -1 ? link_drag->from_id : link_drag->to_id);
	i32                     link_x    = g_ui->_window_x + UI_NODE_X(node);
	i32                     link_y    = g_ui->_window_y + UI_NODE_Y(node);
	if (link_drag->from_id > -1) {
		link_x += UI_NODE_W(node);
		link_y += UI_OUTPUT_Y(node, link_drag->from_socket);
	}
	else {
		link_y += ui_nodes_INPUT_Y(ui_nodes_get_canvas(true), node, link_drag->to_socket) + UI_OUTPUTS_H(node, -1) + UI_BUTTONS_H(node);
	}

	if (math_abs(mouse_x - link_x) > 5 || math_abs(mouse_y - link_y) > 5) { // Link length

		_ui_nodes_on_link_drag_link_drag = link_drag;
		_ui_nodes_on_link_drag_node      = node;
		ui_nodes_node_search(-1, -1, &ui_viewnodes_on_link_drag_on_node_search_done);
	}
	// Selecting which node socket to preview
	else if (link_drag->from_id > -1) {
		i32_imap_set(g_context->node_preview_socket_map, node->id, link_drag->from_socket);
		ui_nodes_node_changed = node;
	}
}

void ui_viewnodes_on_socket_released_group_edit_box() {
	ui_node_socket_t *socket = _ui_nodes_on_socket_released_socket;
	ui_node_t        *node   = _ui_nodes_on_socket_released_node;
	if (string_equals(socket->type, "MESH")) {
		// Mesh input of a script node, only the name can be edited
		char *name = ui_text_input(&_ui_nodes_name, tr("Name"), UI_ALIGN_LEFT, true, false);
		if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER)) {
			bool is_group = string_equals(node->type, "GROUP_INPUT") || string_equals(node->type, "GROUP_OUTPUT");
			if (!is_group) {
				ui_nodes_push_undo(NULL);
			}
			socket->name = string_copy(name);
			ui_box_hide();
			if (is_group) {
				nodes_material_sync_sockets(node);
			}
			else {
				ui_nodes_canvas_changed();
			}
			ui_nodes_hwnd->redraws = 2;
		}
		return;
	}
	string_array_t *type_combo = any_array_create_from_raw_tmp(
	    (void *[]){
	        tr("Color"),
	        tr("Vector"),
	        tr("Value"),
	    },
	    3);
	i32 type = ui_combo(&_ui_nodes_type, type_combo, tr("Type"), true, UI_ALIGN_LEFT, true);
	if (ui_item_changed()) {
		_ui_nodes_name = type == 0 ? tr("Color") : type == 1 ? tr("Vector") : tr("Value");
	}
	char        *name          = ui_text_input(&_ui_nodes_name, tr("Name"), UI_ALIGN_LEFT, true, false);
	f32          min           = ui_float_input(&_ui_nodes_min, tr("Min"), UI_ALIGN_LEFT, 1000.0);
	f32          max           = ui_float_input(&_ui_nodes_max, tr("Max"), UI_ALIGN_LEFT, 1000.0);
	f32_array_t *default_value = NULL;
	if (type == 0) {
		ui_row4();
		ui_float_input(&_ui_nodes_vals[0], tr("R"), UI_ALIGN_LEFT, 1000.0);
		ui_float_input(&_ui_nodes_vals[1], tr("G"), UI_ALIGN_LEFT, 1000.0);
		ui_float_input(&_ui_nodes_vals[2], tr("B"), UI_ALIGN_LEFT, 1000.0);
		ui_float_input(&_ui_nodes_vals[3], tr("A"), UI_ALIGN_LEFT, 1000.0);
		default_value = f32_array_create_xyzw(_ui_nodes_vals[0], _ui_nodes_vals[1], _ui_nodes_vals[2], _ui_nodes_vals[3]);
	}
	else if (type == 1) {
		ui_row3();
		_ui_nodes_vals[0] = ui_float_input(&_ui_nodes_vals[0], tr("X"), UI_ALIGN_LEFT, 1000.0);
		_ui_nodes_vals[1] = ui_float_input(&_ui_nodes_vals[1], tr("Y"), UI_ALIGN_LEFT, 1000.0);
		_ui_nodes_vals[2] = ui_float_input(&_ui_nodes_vals[2], tr("Z"), UI_ALIGN_LEFT, 1000.0);
		default_value     = f32_array_create_xyz(_ui_nodes_vals[0], _ui_nodes_vals[1], _ui_nodes_vals[2]);
	}
	else {
		f32 f         = ui_float_input(&_ui_nodes_vals[0], tr("Value"), UI_ALIGN_LEFT, 1000.0);
		default_value = f32_array_create_x(f);
	}
	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER)) {
		bool is_group = string_equals(node->type, "GROUP_INPUT") || string_equals(node->type, "GROUP_OUTPUT");
		if (!is_group) {
			ui_nodes_push_undo(NULL);
		}
		socket->name          = string_copy(name);
		socket->type          = type == 0 ? "RGBA" : type == 1 ? "VECTOR" : "VALUE";
		socket->color         = nodes_material_get_socket_color(socket->type);
		socket->min           = min;
		socket->max           = max;
		socket->default_value = default_value;
		ui_box_hide();
		if (!is_group) {
			ui_nodes_canvas_changed();
		}
		else {
			nodes_material_sync_sockets(node);
		}
		ui_nodes_hwnd->redraws = 2;
	}
}

void ui_viewnodes_on_socket_released_group_edit(void *_) {
	ui_end_input();
	ui_box_show_custom(&ui_viewnodes_on_socket_released_group_edit_box, 400, 250, NULL, true, tr("Socket"));
}

void ui_viewnodes_on_socket_released_group_menu_draw() {
	ui_node_socket_t *socket = _ui_nodes_on_socket_released_socket;
	ui_node_t        *node   = _ui_nodes_on_socket_released_node;
	if (ui_menu_button(tr("Edit"), "", ICON_EDIT)) {
		_ui_nodes_type = string_equals(socket->type, "RGBA") ? 0 : string_equals(socket->type, "VECTOR") ? 1 : 2;
		_ui_nodes_name = string_copy(socket->name);
		_ui_nodes_min  = socket->min;
		_ui_nodes_max  = socket->max;
		if (string_equals(socket->type, "RGBA") || string_equals(socket->type, "VECTOR")) {
			_ui_nodes_vals[0] = socket->default_value->buffer[0];
			_ui_nodes_vals[1] = socket->default_value->buffer[1];
			_ui_nodes_vals[2] = socket->default_value->buffer[2];
			if (string_equals(socket->type, "RGBA")) {
				_ui_nodes_vals[3] = socket->default_value->buffer[3];
			}
		}
		else {
			_ui_nodes_vals[0] = socket->default_value->buffer[0];
		}
		sys_notify_on_next_frame(&ui_viewnodes_on_socket_released_group_edit, NULL);
	}
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		bool is_group = string_equals(node->type, "GROUP_INPUT") || string_equals(node->type, "GROUP_OUTPUT");
		if (!is_group) {
			ui_nodes_push_undo(NULL);
		}
		i32               i      = 0;
		ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
		i32               output = array_index_of(node->outputs, socket);
		i32               input  = array_index_of(node->inputs, socket);
		// Remove links connected to the socket
		while (i < canvas->links->length) {
			ui_node_link_t *l = canvas->links->buffer[i];
			if ((l->from_id == node->id && l->from_socket == output) || (l->to_id == node->id && l->to_socket == input)) {
				array_splice(canvas->links, i, 1);
			}
			else {
				// Links of the sockets below move up
				if (l->from_id == node->id && output > -1 && l->from_socket > output) {
					l->from_socket--;
				}
				if (l->to_id == node->id && input > -1 && l->to_socket > input) {
					l->to_socket--;
				}
				i++;
			}
		}
		// Remove socket
		array_remove(node->inputs, socket);
		array_remove(node->outputs, socket);
		if (!is_group) {
			ui_nodes_canvas_changed();
		}
		else {
			nodes_material_sync_sockets(node);
		}
	}
}

void ui_viewnodes_on_socket_released_group_menu(void *_) {
	ui_menu_draw(&ui_viewnodes_on_socket_released_group_menu_draw, -1, -1);
}

void ui_viewnodes_on_canvas_context_menu() {
	ui_node_t *selected = _ui_nodes_on_canvas_released_selected;
	g_ui->_y += 1;
	bool is_protected = selected == NULL || string_equals(selected->type, "OUTPUT_MATERIAL_PBR") || string_equals(selected->type, "GROUP_INPUT") ||
	                    string_equals(selected->type, "GROUP_OUTPUT") || string_equals(selected->type, "brush_output_node") ||
	                    string_equals(selected->type, "MESH_OUTPUT");
	g_ui->enabled = !is_protected;
	if (ui_menu_button(tr("Cut"), "ctrl+x", ICON_CUT)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_cut, NULL);
	}
	if (ui_menu_button(tr("Copy"), "ctrl+c", ICON_COPY)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_copy, NULL);
	}
	g_ui->enabled = !string_equals(ui_clipboard, "");
	if (ui_menu_button(tr("Paste"), "ctrl+v", ICON_PASTE)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_paste, NULL);
	}
	g_ui->enabled = !is_protected;
	if (ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_delete, NULL);
	}
	if (ui_menu_button(tr("Duplicate"), "ctrl+d", ICON_DUPLICATE)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_released_duplicate, NULL);
	}
	if (selected != NULL && string_equals(selected->type, "RGB")) {
		if (ui_menu_button(tr("Add Swatch"), "", ICON_PALETTE)) {
			f32_array_t    *color      = selected->outputs->buffer[0]->default_value;
			swatch_color_t *new_swatch = project_make_swatch(color_from_floats(color->buffer[0], color->buffer[1], color->buffer[2], color->buffer[3]));
			g_context->swatch          = new_swatch;
			any_array_push(g_project->swatches, new_swatch);
			history_new_swatch();
			ui_dock_redraw(TAB_AREA_STATUS);
		}
	}
	if (ui_menu_button(tr("Capture Output"), "", ICON_PHOTO)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_capture_output, NULL);
	}
	if (ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL) {
		ui_menu_separator();
		if (ui_menu_button(tr("2D View"), "", ICON_NONE)) {
			ui_base_show_2d_view(VIEW_2D_TYPE_NODE);
		}
	}
	g_ui->enabled = true;
}

void ui_viewnodes_on_canvas_released() {
	if (g_ui->input_released_r && context_in_nodes() && math_abs(g_ui->input_x - g_ui->input_started_x) < 2 &&
	    math_abs(g_ui->input_y - g_ui->input_started_y) < 2) {

		// Node selection
		ui_nodes_t       *nodes    = ui_nodes_get_nodes();
		ui_node_canvas_t *canvas   = ui_nodes_get_canvas(true);
		ui_node_t        *selected = NULL;
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *node = canvas->nodes->buffer[i];
			if (ui_input_in_rect(g_ui->_window_x + UI_NODE_X(node), g_ui->_window_y + UI_NODE_Y(node), UI_NODE_W(node), UI_NODE_H(canvas, node))) {
				selected = node;
				break;
			}
		}
		if (selected == NULL) {
			nodes->nodes_selected_id = i32_array_create_from_raw((i32[]){}, 0);
		}
		else if (i32_array_index_of(nodes->nodes_selected_id, selected->id) == -1) {
			nodes->nodes_selected_id = i32_array_create_from_raw(
			    (i32[]){
			        selected->id,
			    },
			    1);
		}

		// Node context menu
		if (!ui_nodes_socket_released) {
			_ui_nodes_on_canvas_released_selected = selected;
			ui_menu_draw(&ui_viewnodes_on_canvas_context_menu, -1, -1);
		}
	}

	if (g_ui->input_released) {
		ui_nodes_t       *nodes  = ui_nodes_get_nodes();
		ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *node = canvas->nodes->buffer[i];
			if (ui_input_in_rect(g_ui->_window_x + UI_NODE_X(node), g_ui->_window_y + UI_NODE_Y(node), UI_NODE_W(node), UI_NODE_H(canvas, node))) {
				if (nodes->nodes_selected_id->length > 0 && node->id == nodes->nodes_selected_id->buffer[0]) {
					ui_view2d_hwnd->redraws = 2;
					if (sys_time() - g_context->select_time < 0.2)
						ui_base_show_2d_view(VIEW_2D_TYPE_NODE);
					g_context->select_time = sys_time();
				}
				break;
			}
		}
	}
}

void ui_viewnodes_on_socket_released(i32 socket_id) {
	ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
	ui_node_socket_t *socket = ui_get_socket(canvas->nodes, socket_id);
	ui_node_t        *node   = ui_get_node(canvas->nodes, socket->node_id);
	if (g_ui->input_released_r) {
		i32  input = array_index_of(node->inputs, socket);
		bool is_script_input =
		    (string_equals(node->type, "MESH_SCRIPT") && input > 0) || string_equals(node->type, "SHADER_GPU"); // Shader node inputs and outputs
		if (string_equals(node->type, "GROUP_INPUT") || string_equals(node->type, "GROUP_OUTPUT") || is_script_input) {

			_ui_nodes_on_socket_released_socket = socket;
			_ui_nodes_on_socket_released_node   = node;
			sys_notify_on_next_frame(&ui_viewnodes_on_socket_released_group_menu, NULL);
		}
		else {
			ui_viewnodes_on_canvas_released();
		}
	}
	// Selecting which node socket to preview
	// else {
	// 	let i: i32 = array_index_of(node.outputs, socket);
	// 	if (i > -1) {
	// 		i32_imap_set(g_context.node_preview_socket_map, node.id, i);
	// 		ui_nodes_node_changed = node;
	// 	}
	// }
}

ui_canvas_control_t *ui_viewnodes_on_canvas_control() {
	ui_canvas_control_t *control = ui_nodes_get_canvas_control(ui_nodes_controls_down, true);
	ui_nodes_controls_down       = control->controls_down;
	return control;
}

f32 ui_nodes_get_zoom_delta() {
	return g_config->zoom_direction == ZOOM_DIRECTION_VERTICAL              ? -g_ui->input_dy
	       : g_config->zoom_direction == ZOOM_DIRECTION_VERTICAL_INVERTED   ? -g_ui->input_dy
	       : g_config->zoom_direction == ZOOM_DIRECTION_HORIZONTAL          ? g_ui->input_dx
	       : g_config->zoom_direction == ZOOM_DIRECTION_HORIZONTAL_INVERTED ? g_ui->input_dx
	                                                                        : -(g_ui->input_dy - g_ui->input_dx);
}

void ui_nodes_wrap_mouse(bool controls_down, f32 x, f32 y, f32 w, f32 h) {
	if (!g_config->wrap_mouse || !controls_down) {
		return;
	}
	if (g_ui->input_x < x) {
		g_ui->input_x = x + w;
		iron_mouse_set_position(math_floor(g_ui->input_x), math_floor(g_ui->input_y));
	}
	else if (g_ui->input_x > x + w) {
		g_ui->input_x = x;
		iron_mouse_set_position(math_floor(g_ui->input_x), math_floor(g_ui->input_y));
	}
	else if (g_ui->input_y < y) {
		g_ui->input_y = y + h;
		iron_mouse_set_position(math_floor(g_ui->input_x), math_floor(g_ui->input_y));
	}
	else if (g_ui->input_y > y + h) {
		g_ui->input_y = y;
		iron_mouse_set_position(math_floor(g_ui->input_x), math_floor(g_ui->input_y));
	}
}

ui_canvas_control_t *ui_nodes_get_canvas_control(bool controls_down, bool is_node_view) {
	if (keymap_shortcut(any_map_get(g_keymap, "action_pan"), SHORTCUT_TYPE_STARTED) ||
	    keymap_shortcut(any_map_get(g_keymap, "action_zoom"), SHORTCUT_TYPE_STARTED) || g_ui->input_started_r || g_ui->input_wheel_delta != 0.0) {
		controls_down = true;
	}
	else if (!keymap_shortcut(any_map_get(g_keymap, "action_pan"), SHORTCUT_TYPE_DOWN) &&
	         !keymap_shortcut(any_map_get(g_keymap, "action_zoom"), SHORTCUT_TYPE_DOWN) && !g_ui->input_down_r && g_ui->input_wheel_delta == 0.0) {
		controls_down = false;
	}

	if (!controls_down) {
		ui_canvas_control_t *cc = ALLOC_INIT(ui_canvas_control_t, {.pan_x = 0, .pan_y = 0, .zoom = 0, .controls_down = controls_down});
		return cc;
	}

	bool                 pan        = g_ui->input_down_r || keymap_shortcut(any_map_get(g_keymap, "action_pan"), SHORTCUT_TYPE_DOWN);
	f32                  zoom_delta = keymap_shortcut(any_map_get(g_keymap, "action_zoom"), SHORTCUT_TYPE_DOWN) ? ui_nodes_get_zoom_delta() / 100.0 : 0.0;
	ui_canvas_control_t *control    = ALLOC_INIT(ui_canvas_control_t, {.pan_x = pan ? g_ui->input_dx : 0.0,
	                                                                   .pan_y = pan ? g_ui->input_dy : 0.0,
	                                                                   .zoom  = g_ui->input_wheel_delta != 0.0 ? -g_ui->input_wheel_delta / 10 : zoom_delta,
	                                                                   .controls_down = controls_down});

	if (is_node_view && g_ui->input_x < g_ui->_window_x) {
		control->pan_x = 0.0;
		control->pan_y = 0.0;
		control->zoom  = 0.0;
	}

	if (g_ui->combo_selected_id != 0) {
		control->zoom = 0.0;
	}
	if (control->zoom != 0.0) {
		ui_nodes_grid_redraw = true;
	}

	return control;
}

gpu_texture_t *ui_nodes_draw_grid(f32 zoom) {
	i32 ww   = ui_nodes_show ? ui_nodes_ww : ui_view2d_ww;
	i32 wh   = ui_nodes_show ? ui_nodes_wh : ui_view2d_wh;
	f32 step = ui_nodes_grid_cell_w * zoom;
	i32 mult = 5 * UI_SCALE();
	i32 w    = math_floor(ww + step * mult);
	i32 h    = math_floor(wh + step * mult);
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
	gpu_texture_t *grid = gpu_create_render_target(w, h, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(grid, true, g_theme->SEPARATOR_COL);

	i32 sep_col      = g_theme->SEPARATOR_COL;
	i32 line_primary = base_darker(sep_col, 0x00050505);
	if (line_primary < 0xff000000) {
		line_primary = sep_col + 0x00050505;
	}

	i32 line_secondary = base_darker(sep_col, 0x00090909);
	if (line_secondary < 0xff000000) {
		line_secondary = sep_col + 0x00090909;
	}

	draw_set_color(line_primary);
	step = ui_nodes_grid_small_cell_w * zoom;
	for (i32 i = 0; i < math_floor(h / (float)step) + 1; ++i) {
		draw_line(0, i * step, w, i * step, 1.0);
	}
	for (i32 i = 0; i < math_floor(w / (float)step) + 1; ++i) {
		draw_line(i * step, 0, i * step, h, 1.0);
	}
	draw_set_color(line_secondary);
	step = ui_nodes_grid_cell_w * zoom;
	for (i32 i = 0; i < math_floor(h / (float)step) + 1; ++i) {
		draw_line(0, i * step, w, i * step, 1.0);
	}
	for (i32 i = 0; i < math_floor(w / (float)step) + 1; ++i) {
		draw_line(i * step, 0, i * step, h, 1.0);
	}
	draw_end();
	return grid;
}

void ui_nodes_render_color_picker_callback(swatch_color_t *color) {
	_ui_nodes_render_tmp(color->base);
	ui_nodes_hwnd->redraws = 2;
	bool material_live     = g_config->material_live;
	if (material_live) {
		ui_nodes_canvas_changed();
	}
}

static char ui_nodes_menu_open[256]  = "";    // Open sub menu folder of the add menu, like "Noise/Simple/"
static bool ui_nodes_menu_keep       = false; // A sub menu button was pressed, keep the add menu open
static i32  ui_nodes_menu_more_first = 0;     // First category listed in the "..." menu

// As ui_menubar_button sizes itself, plus the gap to the next button
static f32 ui_nodes_menubar_button_w(char *text) {
	return draw_string_width(g_ui->ops->font, g_ui->font_size, text) + 25.0 * UI_SCALE() + 3;
}

// Opens the add menu of a category under the menubar button just drawn
static void ui_nodes_open_category(i32 i, i32 ew) {
	if (i >= 0) {
		if (ui_nodes_menu_category != i) {
			ui_nodes_menu_open[0] = '\0';
		}
		ui_nodes_show_menu     = true;
		ui_nodes_menu_category = i;
	}
	ui_nodes_popup_x = ui_nodes_wx + g_ui->_x;
	ui_nodes_popup_y = ui_nodes_wy + g_ui->_y;
	i32 menuw = math_floor(ew * 2.3);
	if (g_config->touch_ui) {
		ui_nodes_show_menu_first = true;
		ui_nodes_popup_x -= menuw / 2.0;
		ui_nodes_popup_x += g_ui->_w / 2.0;
	}
	// The last buttons of a full row would open it under the docked panels
	ui_nodes_popup_x = math_max(ui_nodes_wx, math_min(ui_nodes_popup_x, ui_nodes_wx + ui_nodes_ww - menuw));
}

static void ui_nodes_menu_more() {
	string_array_t *cats = ui_nodes_get_categories();
	for (i32 i = ui_nodes_menu_more_first; i < cats->length; ++i) {
		if (ui_menu_button(tr(cats->buffer[i]), "", ICON_NONE)) {
			ui_nodes_menu_open[0]    = '\0';
			ui_nodes_show_menu       = true;
			ui_nodes_show_menu_first = true; // Skip the release that picked the category
			ui_nodes_menu_category   = i;
		}
	}
}

// Entry of the add menu, a node or a sub menu named by its folder path like "Noise/"
typedef struct ui_nodes_menu_entry {
	ui_node_t *node;
	char      *sub;
} ui_nodes_menu_entry_t;

// Folder of a node below its category, "Noise/" for "Material Maker/Noise/Fbm" in the "Material Maker" category, "" for the top level
static char *ui_nodes_menu_dir(ui_node_t *n, char *cat_name) {
	char *path = ui_nodes_get_menu_path(n->type);
	char *last = path != NULL ? strrchr(path, '/') : NULL;
	if (last == NULL) {
		return "";
	}
	i32 cl = strlen(cat_name);
	if (strncmp(path, cat_name, cl) == 0 && path[cl] == '/') {
		path += cl + 1;
	}
	i32 len = (i32)(last - path) + 1; // Keep the trailing slash
	if (len <= 0) {
		return "";
	}
	char *dir = string_tmp_alloc(len + 1);
	memcpy(dir, path, len);
	return dir;
}

// Entries of one menu level: its nodes and one sub menu per folder below it, in registration order.
// Categories without menu paths have only nodes, listed as before.
static ui_nodes_menu_entry_t *ui_nodes_menu_entries(ui_node_t_array_t *category, char *cat_name, char *prefix, i32 *count) {
	ui_nodes_menu_entry_t *entries = (ui_nodes_menu_entry_t *)string_tmp_alloc(sizeof(ui_nodes_menu_entry_t) * (category->length + 1));
	i32                    n       = 0;
	i32                    pl      = strlen(prefix);
	for (i32 i = 0; i < category->length; ++i) {
		ui_node_t *node = category->buffer[i];
		char      *dir  = ui_nodes_menu_dir(node, cat_name);
		if (strncmp(dir, prefix, pl) != 0) {
			continue;
		}
		char *slash = strchr(dir + pl, '/');
		if (slash == NULL) {
			entries[n].node = node;
			entries[n].sub  = NULL;
			n++;
			continue;
		}
		i32  len  = (i32)(slash - dir) + 1;
		bool seen = false;
		for (i32 j = 0; j < n && !seen; ++j) {
			seen = entries[j].sub != NULL && strncmp(entries[j].sub, dir, len) == 0 && entries[j].sub[len] == '\0';
		}
		if (!seen) {
			char *sub = string_tmp_alloc(len + 1);
			memcpy(sub, dir, len);
			entries[n].node = node;
			entries[n].sub  = sub;
			n++;
		}
	}
	*count = n;
	return entries;
}

static void ui_nodes_menu_spawn(ui_node_t *n) {
	ui_nodes_push_undo(NULL);
	ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
	ui_nodes_t       *nodes  = ui_nodes_get_nodes();
	ui_node_t        *node   = ui_nodes_make_node(n, nodes, canvas);
	any_array_push(canvas->nodes, node);
	nodes->nodes_selected_id = i32_array_create_from_raw(
	    (i32[]){
	        node->id,
	    },
	    1);
	nodes->nodes_drag = true;
}

// Draws one entry at the cursor, reports the open sub menu of this level and where its button is
static void ui_nodes_menu_entry(ui_nodes_menu_entry_t *e, char *prefix, i32 *open_y, char **open_sub) {
	if (e->sub == NULL) {
		g_ui->is_hovered = false;
		if (ui_menu_button(tr(e->node->name), "", ICON_NONE)) {
			ui_nodes_menu_spawn(e->node);
		}
		// Hovering a node closes deeper sub menus of this level, unless the pointer heads sideways into one
		i32 pl = strlen(prefix);
		if (g_ui->is_hovered && math_abs(g_ui->input_dy) > math_abs(g_ui->input_dx) && strlen(ui_nodes_menu_open) > pl &&
		    strncmp(ui_nodes_menu_open, prefix, pl) == 0) {
			ui_nodes_menu_open[pl] = '\0';
		}
		return;
	}
	i32 y   = g_ui->_y;
	i32 len = strlen(e->sub) - 1; // Label is the last folder, without the slash
	i32 beg = len;
	while (beg > 0 && e->sub[beg - 1] != '/') {
		beg--;
	}
	char *label = string_tmp_alloc(len - beg + 1);
	memcpy(label, e->sub + beg, len - beg);
	bool keep_open   = ui_menu_keep_open; // ui_menu_button sets it for ">" buttons, it belongs to ui_menu
	g_ui->is_hovered = false;
	if (ui_menu_button(tr(label), ">", ICON_NONE)) {
		snprintf(ui_nodes_menu_open, sizeof(ui_nodes_menu_open), "%s", e->sub);
		ui_nodes_menu_keep = true;
	}
	ui_menu_keep_open = keep_open;
	if (g_ui->is_hovered && !g_config->touch_ui) {
		snprintf(ui_nodes_menu_open, sizeof(ui_nodes_menu_open), "%s", e->sub);
	}
	if (strncmp(ui_nodes_menu_open, e->sub, strlen(e->sub)) == 0) {
		*open_y   = y;
		*open_sub = e->sub;
	}
}

// Sub menu level next to its parent, wraps into columns when taller than the node view, then draws its open sub menu.
// Kept inside the node view like the top level, the docked panels are drawn over it and take its input.
static void ui_nodes_menu_level(ui_node_t_array_t *category, char *cat_name, char *prefix, i32 x, i32 y, i32 menuw, i32 parent_x, bool left) {
	i32                    count   = 0;
	ui_nodes_menu_entry_t *entries = ui_nodes_menu_entries(category, cat_name, prefix, &count);
	if (count == 0) {
		return;
	}
	i32 pad  = ui_menu_padding();
	i32 eh   = UI_ELEMENT_H();
	i32 rows = math_floor(math_max(1, (ui_nodes_wh - pad * 2) / eh));
	if (rows > count) {
		rows = count;
	}
	i32 cols = (count + rows - 1) / rows;
	i32 w    = cols * menuw;
	i32 h    = rows * eh + pad * 2;
	// Opens on the right of the parent, or on its left when there is no room, and keeps that side for deeper levels
	if (left ? parent_x - w >= ui_nodes_wx : x + w > ui_nodes_wx + ui_nodes_ww) {
		left = true;
		x    = math_floor(math_max(ui_nodes_wx, parent_x - w));
	}
	else {
		left = false;
	}
	y -= pad;
	if (y + h > ui_nodes_wy + ui_nodes_wh) {
		y = math_floor(math_max(ui_nodes_wy, ui_nodes_wy + ui_nodes_wh - h));
	}
	ui_draw_shadow(x, y, w, h);
	draw_set_color(g_theme->SEPARATOR_COL);
	ui_draw_rect(true, true, x, y, w, h);
	draw_set_color(0xffffffff);
	i32   open_y   = 0;
	char *open_sub = NULL;
	for (i32 i = 0; i < count; ++i) {
		g_ui->_x = x + (i / rows) * menuw + pad;
		g_ui->_y = y + pad + (i % rows) * eh;
		g_ui->_w = menuw - pad * 2;
		ui_nodes_menu_entry(&entries[i], prefix, &open_y, &open_sub);
	}
	if (open_sub != NULL) {
		ui_nodes_menu_level(category, cat_name, open_sub, x + w, open_y, menuw, x, left);
	}
}

void ui_nodes_draw_menubar_search(void *_) {
	ui_nodes_node_search(math_floor(ui_nodes_node_search_x), math_floor(ui_nodes_node_search_y), NULL);
}

// Editing a material from the Materials tab (its Edit): the graph when the edit began, for Cancel. Save to Library and
// Cancel end the edit and close the editor, as in the Material Studio.
static slot_material_t  *ui_nodes_edit_mat      = NULL;
static ui_node_canvas_t *ui_nodes_edit_snapshot = NULL;

void ui_nodes_edit_material(slot_material_t *m) {
	ui_nodes_edit_mat            = m;
	ui_nodes_edit_snapshot       = util_clone_canvas(m->canvas);
	ui_nodes_group_stack->length = 0;
	if (!ui_nodes_show || ui_nodes_canvas_type != CANVAS_TYPE_MATERIAL) {
		ui_base_show_material_nodes();
	}
}

static bool ui_nodes_editing() {
	return ui_nodes_edit_mat != NULL && ui_nodes_edit_mat == g_context->material && ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL &&
	       ui_nodes_group_stack->length == 0 && array_index_of(g_project->_->materials, ui_nodes_edit_mat) >= 0;
}

static void ui_nodes_edit_end() {
	ui_nodes_edit_mat      = NULL;
	ui_nodes_edit_snapshot = NULL;
	if (ui_nodes_show && ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL) {
		ui_base_show_material_nodes(); // Closes the editor
	}
	ui_dock_redraw(-1);
}

static void ui_nodes_edit_save(void *_) {
	slot_material_t *m = ui_nodes_edit_mat;
	if (m != NULL && saved_materials_save(m)) {
		console_info(string("%s %s", tr("Saved to the library:"), m->canvas->name));
	}
	ui_nodes_edit_end();
}

static void ui_nodes_edit_cancel(void *_) {
	slot_material_t *m = ui_nodes_edit_mat;
	if (m != NULL && ui_nodes_edit_snapshot != NULL) {
		history_edit_nodes(m->canvas, CANVAS_TYPE_MATERIAL, -1); // Cancel can be undone
		m->canvas = ui_nodes_edit_snapshot;
		ui_nodes_t *nodes                = ui_nodes_get_nodes();
		nodes->nodes_selected_id->length = 0;
		ui_nodes_canvas_changed();
	}
	ui_nodes_edit_end();
}

void ui_nodes_draw_menubar() {
	ui_node_canvas_t *c     = ui_nodes_get_canvas(true);
	i32               ew    = math_floor(UI_ELEMENT_W() * 0.7);
	i32               top_y = ui_menu_top_y();
	draw_set_color(g_theme->WINDOW_BG_COL);
	draw_filled_rect(0, top_y, ui_nodes_ww, UI_ELEMENT_H() + UI_ELEMENT_OFFSET() * 2);
	draw_set_color(0xffffffff);

	i32 start_y = top_y + UI_ELEMENT_OFFSET();
	g_ui->_x    = 0;
	g_ui->_y    = 2 + start_y;

	// Back button
	if (g_config->touch_ui && !base_view3d_show && !ui_view2d_show) {
		g_ui->_w = math_floor(ew + 3);
		if (ui_icon_button("Back", ICON_ARROW_LEFT, UI_ALIGN_CENTER)) {
			g_ui->input_released = false;
			base_set_workspace(WORKSPACE_PRESET_VIEW_3D);
		}
		g_ui->_x += ew + 3;
		g_ui->_y = 2 + start_y;
	}

	bool full = true;
#ifdef IRON_IOS
	if (config_is_iphone()) {
		full = false;
	}
#endif

	// Editable canvas name
	if (full) {
		char *new_name      = c->name; // Applied below once the rename is validated
		g_ui->_w            = math_floor(math_min(draw_string_width(g_font, g_ui->font_size, new_name) + 15 * UI_SCALE(), 100 * UI_SCALE()));
		ui_id_t name_id     = ui_widget_id(&c->name, UI_ID_TEXT);
		bool    was_editing = g_ui->text_selected_id == name_id;
		if (!was_editing) {
			ui_nodes_canvas_name_prev = c->name;
		}
		ui_set_next_id((ui_id_t)&c->name);
		ui_text_input(&new_name, "", UI_ALIGN_LEFT, true, false);
		bool name_changed = ui_item_changed();
		bool commit       = was_editing && g_ui->text_selected_id != name_id;
		g_ui->_x += g_ui->_w + 3;
		g_ui->_y = 2 + start_y;
		g_ui->_w = ew;
		if (name_changed) { // Check whether renaming is possible and update group links
			if (ui_nodes_group_stack->length > 0) {
				bool can_rename = ui_nodes_find_group(new_name) == NULL; // Name already used
				if (can_rename) {
					char *old_name                     = c->name;
					c->name                            = string_copy(new_name);
					ui_node_canvas_t_array_t *canvases = any_array_create_from_raw((void *[]){}, 0);
					for (i32 i = 0; ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL && i < g_project->_->materials->length; ++i) {
						slot_material_t *m = g_project->_->materials->buffer[i];
						any_array_push(canvases, m->canvas);
					}
					for (i32 i = 0; ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL && i < g_project->_->material_groups->length; ++i) {
						node_group_t *m = g_project->_->material_groups->buffer[i];
						any_array_push(canvases, m->canvas);
					}
					for (i32 i = 0; i < canvases->length; ++i) {
						ui_node_canvas_t *canvas = canvases->buffer[i];
						for (i32 i = 0; i < canvas->nodes->length; ++i) {
							ui_node_t *n = canvas->nodes->buffer[i];
							if (string_equals(n->type, "GROUP") && string_equals(n->name, old_name)) {
								n->name = string_copy(c->name);
							}
						}
					}
				}
			}
			else {
				c->name = string_copy(new_name);
			}
		}
		if (commit && ui_nodes_group_stack->length == 0) {
			char *name = c->name;
			if (string_equals(name, "")) {
				name = ui_nodes_canvas_name_prev;
			}
			name    = ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL ? slot_material_unique_name(c, name) : slot_brush_unique_name(c, name);
			c->name = string_copy(name);
		}
	}

	// Category buttons
	i32  _BUTTON_COL     = g_theme->BUTTON_COL;
	bool _SHADOWS        = g_theme->SHADOWS;
	g_theme->BUTTON_COL  = g_theme->WINDOW_BG_COL;
	g_theme->SHADOWS     = false;
	string_array_t *cats = ui_nodes_get_categories();
	// Categories that do not fit the row go to a "..." menu, scripts and node libraries can add many
	f32 search_w = g_config->touch_ui ? 36 * UI_SCALE() : ui_nodes_menubar_button_w(tr("Search"));
	f32 back_w   = ui_nodes_group_stack->length > 0 ? ew : 0;
	bool editing = ui_nodes_editing();
	f32  save_w  = ui_nodes_menubar_button_w(tr("Save to Library"));
	f32  cancel_w = ui_nodes_menubar_button_w(tr("Cancel"));
	f32  edit_w  = editing ? save_w + cancel_w + 9 : 0;
	f32 avail    = ui_nodes_ww - g_ui->_x - search_w - back_w - edit_w;
	f32 used     = 0;
	for (i32 i = 0; i < cats->length; ++i) {
		used += ui_nodes_menubar_button_w(tr(cats->buffer[i]));
	}
	i32 shown = cats->length;
	if (used > avail) {
		avail -= ui_nodes_menubar_button_w("...");
		used  = 0;
		shown = 0;
		while (shown < cats->length && used + ui_nodes_menubar_button_w(tr(cats->buffer[shown])) <= avail) {
			used += ui_nodes_menubar_button_w(tr(cats->buffer[shown]));
			shown++;
		}
	}
	for (i32 i = 0; i < shown; ++i) {
		g_ui->is_hovered = false; // Not updated when the pointer is outside the window, a stale value would switch the category
		if ((ui_menubar_button(tr(cats->buffer[i]))) || (g_ui->is_hovered && ui_nodes_show_menu)) {
			ui_nodes_open_category(i, ew);
		}
		g_ui->_x += g_ui->_w + 3;
		g_ui->_y = 2 + start_y;
	}
	if (shown < cats->length) {
		if (ui_menubar_button("...")) {
			ui_nodes_menu_more_first = shown;
			ui_nodes_open_category(-1, ew); // Only places the popup, the category is picked from the menu
			ui_menu_draw(&ui_nodes_menu_more, math_floor(ui_nodes_wx + g_ui->_x), math_floor(ui_nodes_wy + g_ui->_y));
		}
		g_ui->_x += g_ui->_w + 3;
		g_ui->_y = 2 + start_y;
	}
	if (g_config->touch_ui) {
		i32 _w   = g_ui->_w;
		g_ui->_w = math_floor(36 * UI_SCALE());
		g_ui->_y = 4 * UI_SCALE() + start_y;
		if (ui_menubar_icon_button(ICON_SEARCH)) {
			ui_nodes_node_search(math_floor(g_ui->_window_x + g_ui->_x), math_floor(g_ui->_window_y + g_ui->_y), NULL);
		}
		g_ui->_w = _w;
	}
	else {
		if (ui_menubar_button(tr("Search"))) {
			ui_nodes_node_search_x = g_ui->_window_x + g_ui->_x;
			ui_nodes_node_search_y = g_ui->_window_y + g_ui->_y;
			// Allow for node menu to be closed first
			sys_notify_on_next_frame(&ui_nodes_draw_menubar_search, NULL);
		}
	}
	if (g_ui->is_hovered) {
		ui_tooltip(string_tmp("%s (%s)", tr("Search for nodes"), any_map_get(g_keymap, "node_search")));
	}

	g_ui->_x += g_ui->_w + 3;
	g_ui->_y            = 2 + start_y;
	g_theme->BUTTON_COL = _BUTTON_COL;
	g_theme->SHADOWS    = _SHADOWS;

	// Close node group
	g_ui->_w = ew;
	if (ui_nodes_group_stack->length > 0 && ui_icon_button(tr("Back"), ICON_ARROW_LEFT, UI_ALIGN_CENTER)) {
		array_pop(ui_nodes_group_stack);
	}

	// Editing a material: at the right end
	if (editing) {
		g_ui->_x = ui_nodes_ww - edit_w;
		g_ui->_y = 2 + start_y;
		g_ui->_w = cancel_w;
		if (ui_button(tr("Cancel"), UI_ALIGN_CENTER, "")) {
			sys_notify_on_next_frame(&ui_nodes_edit_cancel, NULL);
		}
		g_ui->_x = ui_nodes_ww - save_w - 3;
		g_ui->_y = 2 + start_y;
		g_ui->_w = save_w;
		if (ui_button(tr("Save to Library"), UI_ALIGN_CENTER, "")) {
			sys_notify_on_next_frame(&ui_nodes_edit_save, NULL);
		}
	}
}

// Nodes take the tab they are docked in
void ui_nodes_update_rect() {
	ui_dock_layout();
}

void ui_nodes_update(void *_) {
	if (ui_nodes_show && base_ui_enabled) {

		ui_nodes_update_rect();

		i32  mx      = mouse_x;
		i32  my      = mouse_y;
		bool enabled = true;
		i32  tabs_h  = ui_base_show ? ui_header_h * 2 : 0;
		if (mx < ui_nodes_wx || mx > ui_nodes_wx + ui_nodes_ww || my < ui_nodes_wy + tabs_h || my > ui_nodes_wy + ui_nodes_wh) {
			enabled = false;
		}
		if (g_ui->is_typing || !g_ui->input_enabled) {
			enabled = false;
		}

		if (enabled) {
			ui_nodes_t *nodes = ui_nodes_get_nodes();
			if (nodes->nodes_selected_id->length > 0 && g_ui->is_key_pressed) {
				if (g_ui->key_code == KEY_CODE_LEFT) {
					for (i32 i = 0; i < nodes->nodes_selected_id->length; ++i) {
						i32                n      = nodes->nodes_selected_id->buffer[i];
						ui_node_t_array_t *_nodes = ui_nodes_get_canvas(true)->nodes;
						ui_get_node(_nodes, n)->x -= 1;
					}
				}
				else if (g_ui->key_code == KEY_CODE_RIGHT) {
					for (i32 i = 0; i < nodes->nodes_selected_id->length; ++i) {
						i32                n      = nodes->nodes_selected_id->buffer[i];
						ui_node_t_array_t *_nodes = ui_nodes_get_canvas(true)->nodes;
						ui_get_node(_nodes, n)->x += 1;
					}
				}
				if (g_ui->key_code == KEY_CODE_UP) {
					for (i32 i = 0; i < nodes->nodes_selected_id->length; ++i) {
						i32                n      = nodes->nodes_selected_id->buffer[i];
						ui_node_t_array_t *_nodes = ui_nodes_get_canvas(true)->nodes;
						ui_get_node(_nodes, n)->y -= 1;
					}
				}
				else if (g_ui->key_code == KEY_CODE_DOWN) {
					for (i32 i = 0; i < nodes->nodes_selected_id->length; ++i) {
						i32                n      = nodes->nodes_selected_id->buffer[i];
						ui_node_t_array_t *_nodes = ui_nodes_get_canvas(true)->nodes;
						ui_get_node(_nodes, n)->y += 1;
					}
				}
			}

			// Node search popup
			if (keymap_shortcut(any_map_get(g_keymap, "node_search"), SHORTCUT_TYPE_STARTED)) {
				ui_nodes_node_search(-1, -1, NULL);
			}
			if (ui_nodes_node_search_spawn != NULL) {
				g_ui->input_x              = mouse_x; // Fix input_dx after popup removal
				g_ui->input_y              = mouse_y;
				ui_nodes_node_search_spawn = NULL;
			}

			if (keymap_shortcut(any_map_get(g_keymap, "view_reset"), SHORTCUT_TYPE_STARTED)) {
				nodes->pan_x = 0.0;
				nodes->pan_y = 0.0;
				nodes->zoom  = 1.0;
			}

			if (keymap_shortcut(any_map_get(g_keymap, "node_overview"), SHORTCUT_TYPE_STARTED)) {
				nodes->zoom = nodes->zoom == 1.0 ? 0.2 : 1.0;
				nodes->uiw  = ui_nodes_ww;
				nodes->uih  = ui_nodes_wh;
			}
		}
	}

	// Render
	ui_nodes_recompile();

	ui_nodes_t *ui_nodes = ui_nodes_get_nodes();

	// Remove dragged link when mouse is released out of the node viewport
	ui_node_canvas_t *c = ui_nodes_get_canvas(true);
	if (ui_nodes_release_link && ui_nodes->link_drag_id != -1) {
		array_remove(c->links, ui_get_link(c->links, ui_nodes->link_drag_id));
		ui_nodes->link_drag_id = -1;
	}
	ui_nodes_release_link = g_ui->input_released;

	if (!ui_nodes_show) {
		return;
	}

	g_ui->input_enabled = base_ui_enabled;

	if (ui_nodes_last_zoom != ui_nodes->zoom) {
		ui_nodes_last_zoom   = ui_nodes->zoom;
		ui_nodes_grid_redraw = true;
	}

	if (ui_nodes_grid_redraw) {
		if (ui_nodes_grid != NULL) {
			gpu_delete_texture(ui_nodes_grid);
		}
		ui_nodes_grid        = ui_nodes_draw_grid(ui_nodes->zoom);
		ui_nodes_grid_redraw = false;
	}

	// Selected node preview
	if (ui_nodes->nodes_selected_id->length > 0 && ui_nodes->nodes_selected_id->buffer[0] != ui_nodes_last_node_selected_id) {
		ui_nodes_last_node_selected_id = ui_nodes->nodes_selected_id->buffer[0];
		ui_node_t *sel                 = ui_get_node(c->nodes, ui_nodes->nodes_selected_id->buffer[0]);
		ui_nodes_make_node_preview(sel);

		// If script node is present, update selected node preview on every frame
		bool              has_script_node = false;
		ui_node_canvas_t *canvas          = g_context->material->canvas;
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *n = canvas->nodes->buffer[i];
			if (string_equals(n->type, "SCRIPT_CPU")) {
				has_script_node = true;
				break;
			}
		}
		if (has_script_node) {
			ui_nodes_last_node_selected_id = -1;
			ui_view2d_hwnd->redraws        = 2;
			iron_delay_idle_sleep();
		}
	}
	else if (ui_nodes->nodes_selected_id->length == 0) {
		ui_nodes_last_node_selected_id = -1;
	}

	// Update node previews
	if (ui_nodes_node_changed != NULL && !g_ui->input_down) {
		ui_node_t_array_t *linked_nodes = any_array_create_from_raw((void *[]){}, 0);
		ui_nodes_get_linked_nodes(linked_nodes, ui_nodes_node_changed, c);

		ui_node_t *sel = ui_nodes->nodes_selected_id->length > 0 ? ui_get_node(c->nodes, ui_nodes->nodes_selected_id->buffer[0]) : NULL;
		if (sel != NULL && array_index_of(linked_nodes, sel) == -1) {
			any_array_push(linked_nodes, sel);
		}

		for (i32 i = 0; i < linked_nodes->length; ++i) {
			ui_nodes_make_node_preview(linked_nodes->buffer[i]);
		}
		ui_nodes_node_changed = NULL;
	}

	// Start with UI
	ui_begin(g_ui);

	// Make window
	ui_nodes_update_rect();
	i32 ew = math_floor(UI_ELEMENT_W() * 0.7);

	ui_nodes_wrap_mouse(ui_nodes_controls_down, ui_nodes_wx, ui_nodes_wy, ui_nodes_ww, ui_nodes_wh);

	if (ui_nodes_controls_down) {
		ui_nodes_hwnd->redraws = 2;
	}

	if (ui_window(ui_nodes_hwnd, ui_nodes_wx, ui_nodes_wy, ui_nodes_ww, ui_nodes_wh, false)) {

		if (!g_config->touch_ui && ui_base_show) {
			ui_dock_view_tabs(TAB_NODES);
		}

		// Grid
		draw_set_color(0xffffffff);
		f32                  step            = ui_nodes_grid_cell_w * ui_nodes->zoom;
		ui_canvas_control_t *pending_control = ui_nodes_on_canvas_control();
		f32                  x               = math_fmod(UI_NODES_PAN_X() + pending_control->pan_x, step) - step;
		f32                  y               = math_fmod(UI_NODES_PAN_Y() + pending_control->pan_y, step) - step;
		draw_image(ui_nodes_grid, x, y);

		// Undo
		if (g_ui->input_started || g_ui->is_key_pressed) {
			ui_nodes_last_canvas = util_clone_canvas(ui_nodes_get_canvas(true));
		}

		// Nodes
		bool _input_enabled        = g_ui->input_enabled;
		i32  header_h              = ui_base_show ? UI_ELEMENT_H() * 2 + UI_ELEMENT_OFFSET() * 2 : 0;
		bool header_hover          = g_ui->input_y < g_ui->_window_y + header_h;
		g_ui->input_enabled        = _input_enabled && !ui_nodes_show_menu && !header_hover;
		g_ui->window_border_right  = iron_window_width() - ui_nodes_wx - ui_nodes_ww;
		g_ui->window_border_top    = ui_nodes_wy + (ui_base_show ? ui_header_h * 2 : 0);
		g_ui->window_border_bottom = iron_window_height() - ui_nodes_wy - ui_nodes_wh;

		ui_node_canvas(ui_nodes, c);
		g_ui->input_enabled = _input_enabled;

		if (ui_nodes_popup_changed) {
			ui_nodes_popup_changed = false;
			g_ui->changed          = true;
		}

		if (ui_nodes->color_picker_callback != NULL) {
			g_context->color_picker_previous_tool = g_context->tool;
			context_select_tool(TOOL_TYPE_PICKER);
			_ui_nodes_render_tmp             = ui_nodes->color_picker_callback;
			g_context->color_picker_callback = &ui_nodes_render_color_picker_callback;
			ui_nodes->color_picker_callback  = NULL;
		}

		// Remove nodes with unknown id for this canvas type
		// The live node lists are searched, categories registered at runtime (script_material_category_add) are found too
		if (ui_is_paste) {
			node_list_t_array_t *node_list = ui_nodes_get_node_list();

			i32 i = 0;
			while (i++ < c->nodes->length) {
				ui_node_t *canvas_node = c->nodes->buffer[i - 1];
				if (string_array_index_of(ui_nodes_exclude_remove, canvas_node->type) >= 0) {
					continue;
				}
				bool found = false;
				for (i32 i = 0; i < node_list->length; ++i) {
					ui_node_t_array_t *list = node_list->buffer[i];
					for (i32 i = 0; i < list->length; ++i) {
						ui_node_t *list_node = list->buffer[i];
						if (string_equals(canvas_node->type, list_node->type)) {
							found = true;
							break;
						}
					}
					if (found) {
						break;
					}
				}
				if (string_equals(canvas_node->type, "GROUP") && !ui_nodes_can_place_group(canvas_node->name)) {
					found = false;
				}
				if (!found) {
					ui_remove_node(canvas_node, c);
					i32_array_remove(ui_nodes->nodes_selected_id, canvas_node->id);
					i--;
				}
			}
		}

		if (ui_nodes_is_node_menu_op) {
			ui_is_copy           = false;
			ui_is_cut            = false;
			ui_is_paste          = false;
			g_ui->is_delete_down = false;
		}

		// Recompile material on change
		if (g_ui->changed) {
			ui_nodes_recompile_mat = (g_ui->input_dx != 0 || g_ui->input_dy != 0 || !ui_nodes_uichanged_last) && g_config->material_live; // Instant preview
		}
		else if (ui_nodes_uichanged_last) {
			ui_nodes_canvas_changed();
			ui_nodes_push_undo(ui_nodes_last_canvas);
		}
		ui_nodes_uichanged_last = g_ui->changed;

		// Node previews
		if (g_context->selected_node_preview && ui_nodes->nodes_selected_id->length > 0) {
			ui_node_t     *sel = ui_get_node(c->nodes, ui_nodes->nodes_selected_id->buffer[0]);
			gpu_texture_t *img = ui_nodes_get_node_preview_image(sel);
			if (img != NULL && !(sel->flags & UI_NODE_FLAG_PREVIEW)) {
				f32 tw = 128 * UI_SCALE();
				f32 th = tw * (img->height / (float)img->width);
				f32 tx = ui_nodes_ww - tw - 8 * UI_SCALE();
				f32 ty = ui_nodes_wh - th - 8 * UI_SCALE();
				if (img == any_imap_get(g_context->node_preview_map, sel->id)) {
					ui_draw_shadow(tx, ty, tw, th);
				}
				bool single_channel = string_equals(sel->type, "LAYER_MASK");
				if (single_channel) {
					draw_set_pipeline(ui_view2d_pipe);
					gpu_set_int(ui_view2d_channel_loc, 1);
				}
				draw_set_color(0xffffffff);
				draw_scaled_image(img, tx, ty, tw, th);
				if (single_channel) {
					draw_set_pipeline(NULL);
				}
			}
		}

		if (ui_base_show) {
			ui_nodes_draw_menubar();
		}

		g_ui->window_border_right  = 0;
		g_ui->window_border_top    = 0;
		g_ui->window_border_bottom = 0;
	}

	ui_end();

	if (ui_nodes_show_menu && ui_nodes_menu_category >= ui_nodes_get_node_list()->length) { // Category removed by a script
		ui_nodes_show_menu = false;
	}

	if (ui_nodes_show_menu) {
		node_list_t_array_t   *list              = ui_nodes_get_node_list();
		ui_node_t_array_t     *category          = list->buffer[ui_nodes_menu_category];
		char                  *cat_name          = ui_nodes_get_categories()->buffer[ui_nodes_menu_category];
		bool                   is_group_category = ui_nodes_canvas_type != CANVAS_TYPE_BRUSH && string_equals(cat_name, "Group");
		i32                    root_count        = 0;
		ui_nodes_menu_entry_t *root              = ui_nodes_menu_entries(category, cat_name, "", &root_count);
		node_group_t_array_t  *groups            = ui_nodes_groups();
		i32                    py                = ui_nodes_popup_y;
		i32                    menuw             = math_floor(ew * 2.3);
		draw_begin(NULL, false, 0);
		ui_begin_region(g_ui, math_floor(ui_nodes_popup_x), math_floor(py), menuw);
		i32 _FILL_BUTTON_BG     = g_theme->FILL_BUTTON_BG;
		g_theme->FILL_BUTTON_BG = false;
		i32 _ELEMENT_OFFSET     = g_theme->ELEMENT_OFFSET;
		g_theme->ELEMENT_OFFSET = 0;
		i32 _ELEMENT_H          = g_theme->ELEMENT_H;
		g_theme->ELEMENT_H      = g_config->touch_ui ? (28 + 2) : 28;
		ui_menu_h               = root_count * UI_ELEMENT_H();
		if (is_group_category) {
			ui_menu_h += groups->length * UI_ELEMENT_H();
		}
		ui_menu_begin();
		i32   cols     = 1;
		i32   open_y   = 0;
		char *open_sub = NULL;
		for (i32 i = 0; i < root_count; ++i) {
			ui_nodes_menu_entry(&root[i], "", &open_y, &open_sub);
			// Next column
			if (g_ui->_y - ui_nodes_wy + UI_ELEMENT_H() / 2.0 > ui_nodes_wh) {
				g_ui->_x += menuw;
				g_ui->_y = py;
				cols++;
			}
		}
		if (is_group_category) {
			for (i32 i = 0; i < groups->length; ++i) {
				node_group_t *g  = groups->buffer[i];
				g_ui->enabled    = ui_nodes_can_place_group(g->canvas->name);
				f32_array_t *row = f32_array_create_from_raw_tmp(
				    (f32[]){
				        5 / 6.0,
				        1 / 6.0,
				    },
				    2);
				ui_row(row);
				if (ui_button(string_tmp("%s%s", config_button_spacing, g->canvas->name), UI_ALIGN_LEFT, "")) {
					ui_nodes_push_undo(NULL);
					ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
					ui_nodes_t       *nodes  = ui_nodes_get_nodes();
					ui_node_t        *node   = ui_nodes_make_group_node(g->canvas, nodes, canvas);
					any_array_push(canvas->nodes, node);
					nodes->nodes_selected_id = i32_array_create_from_raw(
					    (i32[]){
					        node->id,
					    },
					    1);
					nodes->nodes_drag = true;
				}
				g_ui->enabled = !ui_nodes_group_in_use(g);
				if (ui_button("x", UI_ALIGN_CENTER, "")) {
					history_delete_material_group(g);
					array_remove(groups, g);
				}
				g_ui->enabled = true;
			}
		}
		if (open_sub != NULL) {
			i32 popup_x = math_floor(ui_nodes_popup_x);
			ui_nodes_menu_level(category, cat_name, open_sub, popup_x + cols * menuw, open_y, menuw, popup_x, false);
		}
		ui_nodes_hide_menu = g_ui->combo_selected_id == 0 && !ui_nodes_show_menu_first && !ui_nodes_menu_keep &&
		                     (g_ui->changed || g_ui->input_released || g_ui->input_released_r || g_ui->is_escape_down);
		ui_nodes_show_menu_first = false;
		ui_nodes_menu_keep       = false;
		g_theme->FILL_BUTTON_BG  = _FILL_BUTTON_BG;
		g_theme->ELEMENT_OFFSET  = _ELEMENT_OFFSET;
		g_theme->ELEMENT_H       = _ELEMENT_H;
		ui_menu_end();
		ui_end_region();
		draw_end();
	}

	if (ui_nodes_hide_menu) {
		ui_nodes_show_menu       = false;
		ui_nodes_show_menu_first = true;
		ui_nodes_menu_open[0]    = '\0';
	}

	g_ui->input_enabled = true;
}

void ui_nodes_render_popup() {
	if (!ui_nodes_show || !ui_nodes_popup_open()) {
		return;
	}
	if (g_ui->input_started || g_ui->is_key_pressed) {
		ui_nodes_last_canvas = util_clone_canvas(ui_nodes_get_canvas(true));
	}
	draw_begin(NULL, false, 0);
	g_ui->input_enabled = g_ui->combo_selected_id == 0 && !ui_box_show && !ui_menu_show;
	ui_nodes_popup_render(ui_nodes_get_canvas(true));
	g_ui->input_enabled = true;
	draw_end();
	if (g_ui->changed) {
		ui_nodes_popup_changed = true;
		ui_nodes_hwnd->redraws = 2;
	}
}

void ui_nodes_accept_asset_drop(i32 index) {
	ui_nodes_push_undo(NULL);
	node_group_t *g = ui_nodes_group_stack->length > 0 ? ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1] : NULL;
	ui_node_t    *n = ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL ? nodes_material_create_node("TEX_IMAGE", g)
	                                                                 : nodes_brush_create_node("TEX_IMAGE");
	n->buttons->buffer[0]->default_value->buffer[0] = index;
	ui_nodes_get_nodes()->nodes_selected_id         = i32_array_create_from_raw(
        (i32[]){
            n->id,
        },
        1);
}

void ui_nodes_accept_layer_drop(i32 index) {
	ui_nodes_push_undo(NULL);
	if (slot_layer_is_group(g_project->_->layers->buffer[index])) {
		return;
	}
	node_group_t *g                                 = ui_nodes_group_stack->length > 0 ? ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1] : NULL;
	ui_node_t    *n = nodes_material_create_node(slot_layer_is_mask(g_context->layer) ? "LAYER_MASK" : "LAYER", g);
	n->buttons->buffer[0]->default_value->buffer[0] = index;
	ui_nodes_get_nodes()->nodes_selected_id         = i32_array_create_from_raw(
        (i32[]){
            n->id,
        },
        1);
}

void ui_nodes_accept_material_drop(i32 index) {
	ui_nodes_push_undo(NULL);
	node_group_t *g                                 = ui_nodes_group_stack->length > 0 ? ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1] : NULL;
	ui_node_t    *n                                 = nodes_material_create_node("MATERIAL", g);
	n->buttons->buffer[0]->default_value->buffer[0] = index;
	ui_nodes_get_nodes()->nodes_selected_id         = i32_array_create_from_raw(
        (i32[]){
            n->id,
        },
        1);
}

void ui_nodes_accept_swatch_drop(swatch_color_t *swatch) {
	ui_nodes_push_undo(NULL);
	node_group_t *g                         = ui_nodes_group_stack->length > 0 ? ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1] : NULL;
	ui_node_t    *n                         = nodes_material_create_node("RGB", g);
	n->outputs->buffer[0]->default_value    = f32_array_create_xyzw(color_get_rb(swatch->base) / 255.0, color_get_gb(swatch->base) / 255.0,
	                                                                color_get_bb(swatch->base) / 255.0, color_get_ab(swatch->base) / 255.0);
	ui_nodes_get_nodes()->nodes_selected_id = i32_array_create_from_raw(
	    (i32[]){
	        n->id,
	    },
	    1);
}

void ui_viewnodes_init() {
	ui_nodes_preview_texture = ui_nodes_get_node_preview_image;

	ui_nodes_on_link_drag = ui_viewnodes_on_link_drag;

	ui_nodes_on_node_remove = ui_viewnodes_on_node_remove;

	ui_nodes_on_node_changed = ui_viewnodes_on_node_changed;

	ui_nodes_on_socket_released = ui_viewnodes_on_socket_released;

	ui_nodes_on_canvas_released = ui_viewnodes_on_canvas_released;

	ui_nodes_on_canvas_control = ui_viewnodes_on_canvas_control;

	ui_nodes_grid_snap = g_config->grid_snap;
	nodes_material_init();
}
