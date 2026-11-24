#include "global.h"

// Removing a material that is not the selected one (the iPad's long press Remove, app_material_delete): one undo
// brings that material back where it was with its uid and nodes, the fill channels it was the source of and the Material
// nodes pointing at it or past it; redo removes it again. The same after the history is saved and loaded, and with the
// Decal tool selected. A material a path layer paints with is not removed. Delete Unused keeps the Material nodes of the
// rest pointing at their materials through undo.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;

slot_material_t *ma = NULL; // Selected throughout
slot_material_t *mb = NULL; // Removed: a fill layer and a Material node of md use it
slot_material_t *mc = NULL; // Past mb: md's other Material node renumbers
slot_material_t *md = NULL;
slot_material_t *me = NULL; // Painted with by a path layer
slot_layer_t    *fl = NULL;
int              uid_b   = 0;
int              nodes_b = 0; // Nodes of mb
int              index_b = 0;
int              count   = 0;
int              undos   = 0;
char            *nodes_before = NULL;
char            *uids_before  = NULL;
int              unused       = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[material_remove_undo] %s %s", ok ? "ok  " : "FAIL", name);
}

int index_of_uid(int uid) {
	for (int i = 0; i < script_material_count(); ++i) {
		if (project_test_material_uid(i) == uid) {
			return i;
		}
	}
	return -1;
}

// The materials md's Material nodes point at, in node order
char *material_nodes(slot_material_t *m) {
	char *s = "";
	for (int i = 0; i < m->canvas->nodes->length; ++i) {
		ui_node_t *n = m->canvas->nodes->buffer[i];
		if (string_equals(n->type, "MATERIAL")) {
			ui_node_button_t *b = n->buttons->buffer[0];
			f32_array_t      *d = b->default_value;
			float             f = d->buffer[0];
			s                   = string("%s %d", s, (int)f);
		}
	}
	return s;
}

// fl's channels all from material uid
// The uids of the materials md's Material nodes point at
char *material_node_uids(slot_material_t *m) {
	char *s = "";
	for (int i = 0; i < m->canvas->nodes->length; ++i) {
		ui_node_t *n = m->canvas->nodes->buffer[i];
		if (string_equals(n->type, "MATERIAL")) {
			ui_node_button_t *b = n->buttons->buffer[0];
			f32_array_t      *d = b->default_value;
			float             f = d->buffer[0];
			int               v = (int)f;
			s                   = string("%s %d", s, v >= 0 && v < script_material_count() ? project_test_material_uid(v) : 0);
		}
	}
	return s;
}

int fill_from(int uid) {
	for (int c = 0; c < 9; ++c) {
		if (app_layer_fill_kind(fl->id, c) != 3) {
			return 0;
		}
	}
	slot_material_t *m = fill_main_material(fl->fill);
	return m != NULL && m->uid == uid;
}

int fill_plain() {
	for (int c = 0; c < 9; ++c) {
		if (app_layer_fill_kind(fl->id, c) == 3) {
			return 0;
		}
	}
	return 1;
}

slot_material_t *new_material(char *name, float r, float g, float b) {
	slot_material_t *m   = script_material_create(name);
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, r, g, b, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	return m;
}

// b restored: where it was, its uid and name, the fill and md's nodes as before
void check_restored(char *when) {
	int              i = index_of_uid(uid_b);
	slot_material_t *s = script_material_get();
	check(script_material_count() == count && i == index_b, string("%s: the removed material is back where it was (index %d of %d)", when, i, index_b));
	check(s->uid == uid_b && string_equals(s->canvas->name, "rmu_b") && s->canvas->nodes->length == nodes_b, string("%s: it is that material, selected (%s)", when, s->canvas->name));
	check(fill_from(uid_b), string("%s: the fill layer takes it again", when));
	check(string_equals(material_nodes(md), nodes_before), string("%s: md's Material nodes as before (%s, was %s)", when, material_nodes(md), nodes_before));
}

void check_removed(char *when) {
	check(script_material_count() == count - 1 && index_of_uid(uid_b) < 0, string("%s: the material is gone", when));
	check(fill_plain(), string("%s: the fill layer's channels are plain colors", when));
	check(string_equals(material_nodes(md), string(" %d 9999", index_b)), string("%s: md's nodes renumbered (%s)", when, material_nodes(md)));
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		ma = new_material("rmu_a", 0.9, 0.1, 0.1);
		mb = new_material("rmu_b", 0.1, 0.9, 0.1);
		fl = script_layer_new_fill(); // Of mb in every channel
		mc = new_material("rmu_c", 0.1, 0.1, 0.9);
		md = new_material("rmu_d", 0.9, 0.9, 0.1);
		index_b       = index_of_uid(mb->uid);
		ui_node_t *n1 = script_material_create_node_at("MATERIAL", -600.0, 0.0);
		script_material_set_button(n1, 0, index_b + 1); // mc
		ui_node_t *n2 = script_material_create_node_at("MATERIAL", -600.0, 200.0);
		script_material_set_button(n2, 0, index_b); // mb
		script_material_update();
		uid_b        = mb->uid;
		nodes_b      = mb->canvas->nodes->length;
		nodes_before = string_copy(material_nodes(md));
		app_material_select(ma->id);
	}
	else if (step == 1) {
		count = script_material_count();
		undos = history_test_undos();
		check(fill_from(uid_b), "the fill layer is of b");
		check(string_equals(nodes_before, string(" %d %d", index_b + 1, index_b)), string("md points at c and b (%s)", nodes_before));
		check(app_material_can_delete(mb->id), "b can be removed");
		check(app_material_delete(mb->id), "b removed, a selected");
		check(script_material_get() == ma, "a stays selected");
		check_removed("removed");
		history_undo();
	}
	else if (step == 2) {
		check(history_test_undos() == undos, "one undo takes the removal back");
		check_restored("undo");
		history_redo();
	}
	else if (step == 3) {
		check(history_test_undos() == undos + 1, "one redo");
		check_removed("redo");
		int len = history_io_test_roundtrip(0);
		check(len > 0, "the history encodes and decodes");
		history_undo();
	}
	else if (step == 4) {
		check_restored("undo after a roundtrip");
		history_redo();
	}
	else if (step == 5) {
		check_removed("redo after a roundtrip");
		history_undo();
	}
	else if (step == 6) { // With the Decal tool its own material is the context's: the removed one is still recorded
		app_material_select(ma->id);
		context_select_tool(TOOL_TYPE_DECAL);
	}
	else if (step == 7) {
		count = script_material_count();
		check(app_material_delete(mc->id), "c removed with the Decal tool selected");
		check(script_material_count() == count - 1, "c gone");
		history_undo();
	}
	else if (step == 8) {
		int i = index_of_uid(mc->uid);
		check(script_material_count() == count && i == index_b + 1, string("undo: c back where it was (%d)", i));
		check(string_equals(material_nodes(md), nodes_before), string("md's nodes as before (%s)", material_nodes(md)));
		context_select_tool(TOOL_TYPE_BRUSH);
	}
	else if (step == 9) {
		me = new_material("rmu_e", 0.5, 0.5, 0.5);
		script_layer_new_path(0);
	}
	else if (step == 10) {
		count = script_material_count();
		check(!app_material_can_delete(me->id) && !app_material_delete(me->id) && script_material_count() == count, "a material a path layer paints with is not removed");
		script_material_set(md);
		script_layer_new_fill(); // md used: Delete Unused keeps it
		new_material("rmu_f", 0.2, 0.2, 0.2); // Unused
	}
	else if (step == 11) {
		count       = script_material_count();
		uids_before = string_copy(material_node_uids(md));
		check(string_equals(uids_before, string(" %d %d", mc->uid, uid_b)), "md's nodes point at c and b");
		unused = app_material_delete_unused(); // The project's first material, a and f at least: b and c move down
		check(unused >= 2 && index_of_uid(ma->uid) < 0 && index_of_uid(uid_b) >= 0 && index_of_uid(mc->uid) >= 0 && index_of_uid(me->uid) >= 0,
		      string("Delete Unused removes the unused ones only (%d)", unused));
		check(string_equals(material_node_uids(md), uids_before), string("md's nodes follow c and b (%s)", material_node_uids(md)));
		for (int i = 0; i < unused; ++i) {
			history_undo();
		}
	}
	else if (step == 12) {
		check(script_material_count() == count, string("undo of Delete Unused: every material back (%d of %d)", script_material_count(), count));
		check(string_equals(material_node_uids(md), uids_before) && string_equals(material_nodes(md), nodes_before),
		      string("md's nodes point at c and b again (%s)", material_nodes(md)));
	}
	else {
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[material_remove_undo] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "MATERIAL_REMOVE_UNDO PASS" : "MATERIAL_REMOVE_UNDO FAIL");
		script_quit();
		return;
	}
	step = step + 1;
	script_notify_on_next_frame(next);
}

void main() {
	script_project_new();
	script_notify_on_next_frame(next);
}
