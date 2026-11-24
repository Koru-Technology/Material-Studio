#include "global.h"

// The layer limit (32 on the iPad, set here with layers_test_set_max): new layers, masks, groups and duplicates (a
// group with its children) are refused without an undo step when they would not fit. Undo and redo bring back what
// they took out at any count: a duplicate, a group duplicate, a new group and an applied mask come back over the limit
// (a project from the Mac holds more than the iPad makes). Applying a mask over the limit and undoing it leaves its
// layer as it was.

int fails  = 0;
int checks = 0;
int step   = 0;
int frame  = 0;
int max0   = 0;

slot_layer_t *a = NULL;
slot_layer_t *b = NULL;
slot_layer_t *c = NULL;
slot_layer_t *g = NULL;
slot_layer_t *m = NULL;
int           n = 0;
int           u = 0;
int           mask_uid = 0;
char         *c_before = NULL;
char         *c_after  = NULL;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[layer_cap] %s %s", ok ? "ok  " : "FAIL", name);
}

char *hash_of(slot_layer_t *l) {
	return string("%s %s %s", script_texture_hash(l->texpaint), script_texture_hash(l->texpaint_nor), script_texture_hash(l->texpaint_pack));
}

slot_layer_t *find_uid(int uid) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->uid == uid) {
			return l;
		}
	}
	return NULL;
}

// The limit at the count now plus room
void room(int r) {
	layers_test_set_max(script_layer_count() + r);
}

void next() {
	frame = frame + 1;
	if (frame < 4) {
		script_notify_on_next_frame(next);
		return;
	}
	frame = 0;

	if (step == 0) {
		max0 = layers_test_set_max(255);
		c    = script_layer_new_color(0xff3366cc, 1.0, 0.4, 0.0); // First: a new layer above a group's layer joins the group
		a    = script_layer_new();
		b    = script_layer_new();
		g    = script_layer_new_group(a);
		check(g != NULL && script_layer_set_group(b, g), "a group of two layers");
		check(a->parent == g && b->parent == g && c->parent == NULL, "c out of the group");
	}
	else if (step == 1) {
		room(2); // A group duplicate needs 3
		n = script_layer_count();
		u = history_test_undos();
		check(script_layer_duplicate(g) == NULL, "duplicating a group without room for its children: refused");
		check(script_layer_count() == n && history_test_undos() == u, "nothing added, no undo step");
		check(script_layer_duplicate(c) != NULL && script_layer_count() == n + 1, "a layer duplicate fits");
		check(script_layer_duplicate(c) != NULL && script_layer_count() == n + 2, "a second one fills the limit");
		u = history_test_undos();
		check(script_layer_duplicate(c) == NULL, "at the limit: a duplicate is refused");
		check(script_layer_duplicate(g) == NULL, "at the limit: a group duplicate is refused");
		check(script_layer_new() == NULL, "at the limit: a new layer is refused");
		check(script_layer_new_mask(c, 1) == NULL, "at the limit: a new mask is refused");
		check(script_layer_new_group(c) == NULL, "at the limit: a new group is refused");
		app_layer_new(1); // A fill layer, on the next frame
	}
	else if (step == 2) {
		check(script_layer_count() == n + 2 && history_test_undos() == u, "at the limit: no fill layer, nothing added, no undo step");
		history_undo(); // The second duplicate
	}
	else if (step == 3) {
		check(script_layer_count() == n + 1, "undo of a duplicate");
		room(0); // No room: redo brings it back anyway
		history_redo(); // On the next frame
	}
	else if (step == 4) {
		check(script_layer_count() == n + 2, string("redo of a duplicate at the limit (%d of %d)", script_layer_count(), n + 2));
		layers_test_set_max(255);
		n = script_layer_count();
		check(script_layer_duplicate(g) != NULL && script_layer_count() == n + 3, "with room a group duplicates with its children");
	}
	else if (step == 5) {
		history_undo();
	}
	else if (step == 6) {
		check(script_layer_count() == n, "undo of the group duplicate");
		room(0);
		history_redo();
	}
	else if (step == 7) {
		check(script_layer_count() == n + 3, string("redo of a group duplicate over the limit (%d of %d)", script_layer_count(), n + 3));
		layers_test_set_max(255);
		n = script_layer_count();
		slot_layer_t *ng = script_layer_new_group(c);
		check(ng != NULL && script_layer_count() == n + 1 && c->parent == ng, "a new group");
		history_undo();
	}
	else if (step == 8) {
		check(script_layer_count() == n && c->parent == NULL, "undo of the new group");
		room(0);
		history_redo();
		check(script_layer_count() == n + 1 && c->parent != NULL, "redo of a new group over the limit");
		history_undo(); // Back out of the group
	}
	else if (step == 9) {
		layers_test_set_max(255);
		m        = script_layer_new_mask(c, 0);
		mask_uid = m != NULL ? m->uid : 0;
		check(m != NULL && m->parent == c, "a mask on c");
	}
	else if (step == 10) {
		c_before = hash_of(c);
		room(-3); // Over the limit, as a project from the Mac
		n = script_layer_count();
		check(script_layer_apply_mask(m) == c && script_layer_count() == n - 1, "apply mask over the limit");
	}
	else if (step == 11) {
		c_after = hash_of(c); // c with the black mask applied
		check(!string_equals(c_after, c_before), "the black mask took c's paint");
		history_undo();
	}
	else if (step == 12) {
		m = find_uid(mask_uid);
		check(script_layer_count() == n, string("undo of apply mask over the limit: the mask is back (%d of %d)", script_layer_count(), n));
		check(m != NULL && m->parent == c && m->texpaint_nor == NULL, "it is a mask of c, with its uid");
		check(c->texpaint_nor != NULL && string_equals(hash_of(c), c_before), "c is back as before the apply, not cleared");
		history_redo();
	}
	else if (step == 13) {
		check(script_layer_count() == n - 1 && find_uid(mask_uid) == NULL, "redo of apply mask over the limit");
		check(string_equals(hash_of(c), c_after), "c as after the apply");
		history_undo();
	}
	else if (step == 14) {
		m = find_uid(mask_uid);
		check(m != NULL && m->parent == c, "undo again");
		room(-3);
		n = script_layer_count();
		check(script_layer_duplicate(g) == NULL && script_layer_new() == NULL && script_layer_new_mask(c, 1) == NULL, "over the limit: nothing new");
		check(script_layer_count() == n, "nothing added");
	}
	else {
		layers_test_set_max(max0);
		check(gpu_shader_failures() == 0, string("no shader failed to compile (%d)", gpu_shader_failures()));
		check(gpu_errors() == 0, string("the GPU dropped no work (%d command buffers)", gpu_errors()));
		printf("[layer_cap] %d checks, %d failures", checks, fails);
		printf(fails == 0 ? "LAYER_CAP PASS" : "LAYER_CAP FAIL");
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
