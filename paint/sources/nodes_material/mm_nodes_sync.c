#include "../global.h"
#include "../material_maker/mm_internal.h"
#include <math.h>

// Material Maker nodes (mm_nodes.c): dynamic nodes kept consistent (switch sockets, "any" socket types, remotes writing
// into the nodes they control) and a node set from a Material Maker graph node. Included after mm_nodes_params.c.

// The Material Maker nodes of the canvas as Material Maker would save them (remote widgets resolve their targets there)
static mm_json_t *mm_nodes_canvas_data(ui_node_canvas_t *canvas, ui_node_t *except) {
	mm_json_t *rv = mm_json_new(MM_JSON_ARRAY);
	for (i32 i = 0; canvas != NULL && i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (n != except && mm_node_is(n)) {
			mm_json_push(rv, mm_node_data(n));
		}
	}
	return rv;
}

// New sockets for the node, matched by name with the old ones: those keep their id, value and links (a link moves with
// its socket, as Material Maker reconnects a switch), links of the others are removed
static void mm_nodes_adopt_sockets(ui_node_t *node, ui_node_socket_array_t *old, ui_node_socket_array_t *now, bool inputs, ui_node_canvas_t *canvas) {
	i32  n     = now->length;
	i32 *remap = mm_alloc(sizeof(i32) * (old->length + 1));
	ui_node_socket_t **keep = mm_alloc(sizeof(ui_node_socket_t *) * (n + 1));
	for (i32 i = 0; i < old->length; ++i) {
		remap[i] = -1;
		for (i32 k = 0; k < n; ++k) {
			if (keep[k] == NULL && string_equals(old->buffer[i]->name, now->buffer[k]->name)) {
				remap[i] = k;
				keep[k]  = old->buffer[i];
				break;
			}
		}
	}
	if (canvas != NULL) {
		i32 i = 0;
		while (i < canvas->links->length) {
			ui_node_link_t *l    = canvas->links->buffer[i];
			bool            mine = inputs ? l->to_id == node->id : l->from_id == node->id;
			i32            *s    = inputs ? &l->to_socket : &l->from_socket;
			if (mine) {
				i32 to = *s >= 0 && *s < old->length ? remap[*s] : -1;
				if (to < 0) {
					for (i32 j = i; j < canvas->links->length - 1; ++j) {
						canvas->links->buffer[j] = canvas->links->buffer[j + 1];
					}
					canvas->links->length--;
					continue;
				}
				*s = to;
			}
			i++;
		}
	}
	i32 id = canvas != NULL ? ui_get_socket_id(canvas->nodes) : 0;
	old->length = 0;
	for (i32 k = 0; k < n; ++k) {
		ui_node_socket_t *s = keep[k];
		if (s == NULL) {
			s          = now->buffer[k];
			s->id      = id++;
			s->node_id = node->id;
		}
		any_array_push(old, s);
	}
}

// Rebuilds the sockets and buttons of a dynamic node from data (as mm_node_data gives it), the seed and name stay
static void mm_nodes_reshape(ui_node_t *node, mm_json_t *data, ui_node_canvas_t *canvas, bool buttons) {
	mm_nodes_type_t *t = mm_nodes_built(node->type);
	if (t == NULL || !t->dynamic) {
		return;
	}
	mm_nodes_model_t m;
	mm_nodes_make_model(t, data, strcmp(t->mm_type, "remote") == 0 ? mm_nodes_canvas_data(canvas, node) : NULL, &m);
	if (m.error != NULL && strcmp(t->mm_type, "remote") != 0) {
		return;
	}
	ui_node_t         scratch = {0};
	mm_nodes_param_t *params  = NULL;
	i32               count   = 0;
	scratch.inputs            = any_array_create_from_raw((void *[]){}, 0);
	scratch.outputs           = any_array_create_from_raw((void *[]){}, 0);
	scratch.buttons           = any_array_create_from_raw((void *[]){}, 0);
	mm_nodes_fill(NULL, &scratch, &m, &params, &count);
	for (i32 i = 0; i < node->inputs->length; ++i) { // Parameter sockets stay, after the new ports
		if (mm_node_is_param_socket(node->inputs->buffer[i])) {
			any_array_push(scratch.inputs, node->inputs->buffer[i]);
		}
	}
	mm_nodes_adopt_sockets(node, node->inputs, scratch.inputs, true, canvas);
	mm_nodes_adopt_sockets(node, node->outputs, scratch.outputs, false, canvas);
	if (buttons) {
		i32               si   = mm_nodes_seed_index(node);
		ui_node_button_t *seed = si >= 0 ? node->buttons->buffer[si] : mm_nodes_seed_button_of(data);
		node->buttons->length  = 0;
		for (i32 i = 0; i < scratch.buttons->length; ++i) {
			any_array_push(node->buttons, scratch.buttons->buffer[i]);
		}
		any_array_push(node->buttons, mm_nodes_state_button(mm_nodes_state_of(data, params, count)));
		any_array_push(node->buttons, seed);
	}
	else { // Same buttons: only the source range of a switch follows its choices
		mm_nodes_param_t *src = mm_nodes_find_param(node, "source");
		if (src != NULL) {
			ui_node_button_t *b = node->buttons->buffer[src->button];
			b->max              = mm_nodes_switch_size(data, "choices", 2, 2) - 1;
			b->default_value->buffer[0] = mm_nodes_clampi(b->default_value->buffer[0], 0, (i32)b->max);
		}
	}
	for (i32 i = 0; i < count; ++i) {
		free(params[i].name); // The labels are the buttons' names
	}
	free(params);
	array_free(scratch.inputs);
	array_free(scratch.outputs);
	array_free(scratch.buttons);
	free(scratch.inputs);
	free(scratch.outputs);
	free(scratch.buttons);
}

// Sockets of type "any" (switch, reroute, portal) take the type of what they are linked to, MM_ANY while unlinked
static ui_node_link_t *mm_nodes_link_to(ui_node_canvas_t *canvas, ui_node_t *node, i32 socket, bool input) {
	for (i32 i = 0; i < canvas->links->length; ++i) {
		ui_node_link_t *l = canvas->links->buffer[i];
		if (input ? (l->to_id == node->id && l->to_socket == socket) : (l->from_id == node->id && l->from_socket == socket)) {
			return l;
		}
	}
	return NULL;
}

static char *mm_nodes_linked_type(ui_node_canvas_t *canvas, ui_node_t *node, i32 socket, bool input) {
	ui_node_link_t *l     = mm_nodes_link_to(canvas, node, socket, input);
	ui_node_t      *other = l != NULL ? ui_get_node(canvas->nodes, input ? l->from_id : l->to_id) : NULL;
	if (other == NULL) {
		return NULL;
	}
	ui_node_socket_array_t *sockets = input ? other->outputs : other->inputs;
	i32                     i       = input ? l->from_socket : l->to_socket;
	return i >= 0 && i < sockets->length && !string_equals(sockets->buffer[i]->type, "MM_ANY") ? sockets->buffer[i]->type : NULL;
}

static void mm_nodes_set_socket_type(ui_node_socket_t *s, char *type) {
	if (!string_equals(s->type, type)) {
		s->type  = string_copy(type);
		s->color = mm_nodes_socket_color(type);
		if (s->default_value == NULL || (i32)s->default_value->length < mm_nodes_socket_size(type)) {
			f32 v[4] = {0, 0, 0, 1};
			s->default_value = f32_array_create(4);
			for (i32 i = 0; i < 4; ++i) {
				s->default_value->buffer[i] = v[i];
			}
		}
	}
}

static char *mm_nodes_string_param(ui_node_t *node, const char *name) {
	mm_nodes_param_t *p = mm_nodes_find_param(node, name);
	u8_array_t       *d = p != NULL ? node->buttons->buffer[p->button]->data : NULL;
	return d != NULL && d->length > 0 ? (char *)d->buffer : "";
}

static void mm_nodes_adopt_types(ui_node_t *node, mm_nodes_type_t *t, ui_node_canvas_t *canvas) {
	if (strcmp(t->mm_type, "reroute") == 0 && node->inputs->length > 0 && node->outputs->length > 0) {
		char *type = mm_nodes_linked_type(canvas, node, 0, true);
		type       = type != NULL ? type : mm_nodes_linked_type(canvas, node, 0, false);
		type       = type != NULL ? type : "MM_ANY";
		mm_nodes_set_socket_type(node->inputs->buffer[0], type);
		mm_nodes_set_socket_type(node->outputs->buffer[0], type);
	}
	else if (strcmp(t->mm_type, "switch") == 0) {
		i32 outputs = node->outputs->length;
		i32 ports   = mm_node_port_count(node);
		for (i32 o = 0; o < outputs; ++o) {
			char *type = NULL;
			for (i32 i = o; i < ports && type == NULL; i += outputs) {
				type = mm_nodes_linked_type(canvas, node, i, true);
			}
			type = type != NULL ? type : mm_nodes_linked_type(canvas, node, o, false);
			type = type != NULL ? type : "MM_ANY";
			for (i32 i = o; i < ports; i += outputs) {
				mm_nodes_set_socket_type(node->inputs->buffer[i], type);
			}
			mm_nodes_set_socket_type(node->outputs->buffer[o], type);
		}
	}
	else if (strcmp(t->mm_type, "portal") == 0) {
		if (node->inputs->length > 0) { // In: what is linked to it
			char *type = mm_nodes_linked_type(canvas, node, 0, true);
			mm_nodes_set_socket_type(node->inputs->buffer[0], type != NULL ? type : "MM_ANY");
		}
		if (node->outputs->length > 0) { // Out: what its In of the same link carries
			char *link = mm_nodes_string_param(node, "link");
			char *type = NULL;
			for (i32 i = 0; i < canvas->nodes->length && type == NULL; ++i) {
				ui_node_t *n = canvas->nodes->buffer[i];
				if (n != node && starts_with(n->type, "MM_portal") && n->inputs->length > 0 && string_equals(mm_nodes_string_param(n, "link"), link)) {
					char *it = n->inputs->buffer[0]->type;
					type     = string_equals(it, "MM_ANY") ? NULL : it;
				}
			}
			mm_nodes_set_socket_type(node->outputs->buffer[0], type != NULL ? type : "MM_ANY");
		}
	}
}

// Keeps a dynamic node consistent with its values, called as it is drawn and after edits: a switch rebuilds its sockets
// when outputs or choices change, "any" sockets follow their links, a remote applies its changed controls
void mm_node_update(ui_node_t *node, ui_node_canvas_t *canvas) {
	mm_nodes_type_t *t = node != NULL && canvas != NULL ? mm_nodes_built(node->type) : NULL;
	if (t == NULL || !t->dynamic) {
		return;
	}
	mm_mark_t mk = mm_arena_mark();
	if (strcmp(t->mm_type, "switch") == 0) {
		mm_json_t *d       = mm_node_data(node);
		i32        outputs = mm_nodes_switch_size(d, "outputs", 2, 1);
		i32        choices = mm_nodes_switch_size(d, "choices", 2, 2);
		if (node->outputs->length != outputs || mm_node_port_count(node) != outputs * choices) {
			mm_nodes_reshape(node, d, canvas, false);
		}
	}
	if (strcmp(t->mm_type, "remote") == 0) {
		if (mm_nodes_remote_seen == NULL) {
			mm_nodes_remote_seen = any_map_create();
		}
		char      *key  = string_tmp("%p", node);
		char      *last = any_map_get(mm_nodes_remote_seen, key);
		mm_json_t *now  = mm_node_parameters(node);
		char      *text = mm_json_text(now);
		if (last != NULL && strcmp(last, text) != 0) {
			mm_json_t *before = mm_json_parse(last, strlen(last));
			for (i32 i = 0; i < now->count; ++i) {
				mm_json_t *b = mm_json_get(before, now->keys[i]);
				if (b == NULL || strcmp(mm_json_text(b), mm_json_text(now->items[i])) != 0) {
					mm_nodes_remote_apply(node, now->keys[i], now->items[i], canvas, 0);
				}
			}
		}
		if (last == NULL || strcmp(last, text) != 0) {
			any_map_set(mm_nodes_remote_seen, string_copy(key), string_copy(text));
		}
	}
	mm_nodes_adopt_types(node, t, canvas);
	mm_arena_release(mk);
}

// Every Material Maker node of the canvas: remotes rebuild their controls from the nodes they control, then updates
void mm_nodes_sync(ui_node_canvas_t *canvas) {
	for (i32 i = 0; canvas != NULL && i < canvas->nodes->length; ++i) {
		ui_node_t       *n = canvas->nodes->buffer[i];
		mm_nodes_type_t *t = mm_node_is(n) ? mm_nodes_built(n->type) : NULL;
		if (t != NULL && strcmp(t->mm_type, "remote") == 0) {
			mm_mark_t mk = mm_arena_mark();
			mm_nodes_reshape(n, mm_node_data(n), canvas, true);
			mm_arena_release(mk);
		}
	}
	for (i32 i = 0; canvas != NULL && i < canvas->nodes->length; ++i) {
		mm_node_update(canvas->nodes->buffer[i], canvas);
	}
}

// Sets the node from a node of a Material Maker graph (as mm_node_data gives it, or read from a .ptex): name, seed,
// version, widgets, io, port_type and parameters. Dynamic nodes rebuild their sockets and buttons, links that no longer
// fit are removed.
void mm_node_set_data(ui_node_t *node, mm_json_t *data, ui_node_canvas_t *canvas) {
	mm_nodes_type_t *t = node != NULL && data != NULL ? mm_nodes_built(node->type) : NULL;
	if (t == NULL) {
		return;
	}
	mm_mark_t  mk     = mm_arena_mark();
	mm_json_t *merged = mm_node_data(node);
	mm_json_t *params = mm_json_get(merged, "parameters");
	for (i32 i = 0; i < data->count; ++i) {
		if (data->items[i] == NULL) {
			continue;
		}
		if (strcmp(data->keys[i], "parameters") == 0 && data->items[i]->type == MM_JSON_OBJECT) {
			for (i32 k = 0; k < data->items[i]->count; ++k) {
				mm_json_set(params, data->items[i]->keys[k], data->items[i]->items[k]);
			}
		}
		else if (strcmp(data->keys[i], "type") != 0) {
			mm_json_set(merged, data->keys[i], data->items[i]);
		}
	}
	if (t->dynamic) {
		mm_nodes_reshape(node, merged, canvas, true);
	}
	i32               count = 0;
	mm_nodes_param_t *ps    = mm_nodes_layout(node, &count);
	for (i32 i = 0; i < count; ++i) {
		if (ps[i].button >= 0 && ps[i].button < node->buttons->length) {
			mm_nodes_store(&ps[i], node->buttons->buffer[ps[i].button], mm_json_get(params, ps[i].name));
		}
	}
	bool   locked = false;
	bool   has    = false;
	double seed   = mm_nodes_seed_of(data, &locked, &has);
	i32    si     = mm_nodes_seed_index(node);
	if (has && si >= 0) {
		f32 *s = node->buttons->buffer[si]->default_value->buffer;
		s[0]   = (f32)seed;
		s[1]   = (f32)(seed - s[0]);
		s[2]   = locked ? 1 : 0;
		s[3]   = 1;
	}
	mm_nodes_set_name(node, mm_nodes_jstr(data, "name"));
	if (mm_nodes_remote_seen != NULL) { // Values set here are not edits to apply
		map_delete(mm_nodes_remote_seen, string_tmp("%p", node));
	}
	mm_node_update(node, canvas);
	mm_arena_release(mk);
}

void mm_node_set_data_json(ui_node_t *node, char *json, ui_node_canvas_t *canvas) {
	mm_mark_t mk = mm_arena_mark();
	mm_node_set_data(node, mm_json_parse(json, strlen(json)), canvas);
	mm_arena_release(mk);
}
