#include "../global.h"
#include "../material_maker/mm_internal.h"
#include <math.h>

// Material Maker nodes (mm_nodes.c): startup, every type of the library tree registered with its menu path and code
// generation. Included last of the mm_nodes files.

// Registration

static char *mm_nodes_slug(const char *s) {
	char *r = string_copy((char *)s);
	for (char *p = r; *p; ++p) {
		*p = (*p >= 'A' && *p <= 'Z') ? *p + 32 : ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) ? *p : '_';
	}
	return r;
}

// hidden: not in the add menu, for imported graphs (every .mmg and website node of nodes.pack, the inline types)
static void mm_nodes_register(mm_json_t *entry, char *type, char *mm_type, const char *path, bool hidden) {
	mm_json_t *copy = mm_json_copy(entry);
	mm_json_set(copy, "icon_data", NULL);
	mm_json_set(copy, "icon", NULL);

	mm_nodes_type_t *t = calloc(1, sizeof(mm_nodes_type_t));
	t->type            = type;
	t->mm_type         = string_copy(mm_type);
	t->path            = string_copy((char *)path);
	t->entry           = string_copy(mm_json_text(copy));
	t->seed_button     = -1;
	t->dynamic         = mm_nodes_is_dynamic(mm_type);
	const char *name   = mm_nodes_jstr(entry, "display_name");
	const char *slash  = strrchr(path, '/');
	t->def             = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                            .name    = string_copy((char *)(name != NULL ? name : slash != NULL ? slash + 1 : path)),
	                                            .type    = type,
	                                            .x       = 0,
	                                            .y       = 0,
	                                            .color   = MM_NODES_COLOR,
	                                            .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                                            .outputs = any_array_create_from_raw((void *[]){}, 0),
	                                            .buttons = any_array_create_from_raw((void *[]){}, 0),
	                                            .width   = 0,
	                                            .flags   = 0});
	any_map_set(mm_nodes_types, type, t);
	mm_nodes_list                 = realloc(mm_nodes_list, sizeof(mm_nodes_type_t *) * (mm_nodes_n + 1));
	mm_nodes_list[mm_nodes_n++]   = t;
	if (!hidden) {
		any_array_push(nodes_material_mm, t->def);
		ui_nodes_set_menu_path(type, string_tmp("Material Maker/%s", path));
		mm_nodes_visible++;
	}
	any_map_set(parser_material_node_outputs, type, mm_bridge_output); // Their code in material shaders (mm_bridge.c)
}

void mm_nodes_init() {
	any_map_set(ui_nodes_custom_buttons, "mm_label", mm_nodes_label_button);
	any_map_set(ui_nodes_custom_buttons, "mm_value", mm_nodes_value_button);
	any_map_set(ui_nodes_custom_buttons, "mm_seed", mm_nodes_seed_button);
	any_map_set(ui_nodes_custom_buttons, "mm_gradient", mm_nodes_gradient_button);
	any_map_set(ui_nodes_custom_buttons, "mm_curve", mm_nodes_curve_button);
	any_map_set(ui_nodes_custom_buttons, "mm_state", mm_nodes_state_button_draw);
	any_map_set(ui_nodes_custom_buttons, "mm_string", mm_nodes_string_button);
	ui_nodes_on_socket_label = mm_nodes_socket_label;
	if (mm_nodes_done_init) {
		return;
	}
	mm_nodes_done_init = true;
	mm_nodes_types     = any_map_create();
	double t0          = iron_time();

	buffer_t *blob = data_get_blob("material_maker/nodes.pack");
	if (blob == NULL) {
		mm_nodes_skip("%s%s", "material_maker/nodes.pack not found", "");
		return;
	}
	u8 *data = malloc(blob->length); // The pack keeps pointing into it
	memcpy(data, blob->buffer, blob->length);
	mm_nodes_pack = mm_pack_open(data, blob->length);
	if (mm_nodes_pack == NULL) {
		mm_nodes_skip("%s%s", "material_maker/nodes.pack is not a pack", "");
		return;
	}
	mm_set_nodes(mm_nodes_pack);

	any_map_t *in_pack = any_map_create(); // .mmg names
	for (i32 i = 0; i < mm_pack_count(mm_nodes_pack); ++i) {
		mm_json_t  *e    = mm_pack_entry(mm_nodes_pack, i);
		const char *kind = mm_json_str(mm_json_get(e, "kind"), "");
		if (strcmp(kind, "mmg") == 0) {
			any_map_set(in_pack, (char *)mm_json_str(mm_json_get(e, "name"), ""), e);
		}
	}

	i32 k = mm_pack_find(mm_nodes_pack, "name", "library_base");
	if (k < 0) {
		mm_nodes_skip("%s%s", "nodes.pack has no library_base, run build_library.py", "");
		return;
	}
	mm_mark_t  m   = mm_arena_mark();
	size_t     len = 0;
	char      *txt = mm_pack_read(mm_nodes_pack, k, &len);
	mm_json_t *lib = txt != NULL ? mm_json_get(mm_json_parse(txt, len), "lib") : NULL;
	free(txt);
	for (i32 i = 0; lib != NULL && i < lib->count; ++i) {
		mm_json_t  *e    = lib->items[i];
		const char *type = mm_nodes_jstr(e, "type");
		const char *path = mm_nodes_jstr(e, "tree_item");
		if (type == NULL || path == NULL) {
			continue;
		}
		if (mm_json_get(e, "nodes") != NULL) { // An inline graph
			mm_nodes_register(e, string_copy(string_tmp("MM_graph_%s", mm_nodes_slug(path))), "graph", path, false);
		}
		else if (any_map_get(in_pack, (char *)type) != NULL || mm_nodes_is_builtin(type)) {
			char *t = string_tmp("MM_%s", type);
			if (mm_nodes_find(t) != NULL) { // A preset: the same node with other defaults, MM_<type>@<name>
				const char *dn = mm_nodes_jstr(e, "display_name");
				t              = string_tmp("MM_%s@%s", type, mm_nodes_slug(dn != NULL ? dn : path));
			}
			if (mm_nodes_find(t) != NULL) {
				mm_nodes_skip("%s: %s is registered twice", path, t);
			}
			else {
				mm_nodes_register(e, string_copy(t), (char *)type, path, false);
			}
		}
		else {
			mm_nodes_skip("%s: %s is written in GDScript, not a node definition", path, type);
		}
	}
	// Every other node definition of the pack, and Material Maker's inline nodes, for imported graphs
	for (i32 i = 0; i < mm_pack_count(mm_nodes_pack); ++i) {
		mm_json_t  *pe   = mm_pack_entry(mm_nodes_pack, i);
		const char *name = mm_json_str(mm_json_get(pe, "name"), "");
		char       *t    = string_tmp("MM_%s", name);
		if (strcmp(mm_json_str(mm_json_get(pe, "kind"), ""), "mmg") == 0 && mm_nodes_find(t) == NULL && !starts_with((char *)name, "material")) { // MM_MATERIAL takes materials
			mm_json_t *e = mm_json_new(MM_JSON_OBJECT);
			mm_json_set(e, "type", mm_json_string(name));
			mm_json_set(e, "display_name", mm_json_string(name));
			mm_nodes_register(e, string_copy(t), (char *)name, name, true);
		}
	}
	const char *inline_types[][2] = {{"graph", "Graph"}, {"shader", "Shader"}, {"sdf", "Easy SDF"}};
	for (i32 i = 0; i < 3; ++i) {
		mm_json_t *e = mm_json_new(MM_JSON_OBJECT);
		mm_json_set(e, "type", mm_json_string(inline_types[i][0]));
		mm_json_set(e, "display_name", mm_json_string(inline_types[i][1]));
		mm_nodes_register(e, string_copy(string_tmp("MM_%s", inline_types[i][0])), (char *)inline_types[i][0], inline_types[i][1], true);
	}
	mm_arena_release(m);
	map_free(in_pack);
	mm_nodes_ms = (f32)((iron_time() - t0) * 1000.0);
}
