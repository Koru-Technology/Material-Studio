#!/usr/bin/env python3
# Writes a script (for the app's --script) that finds which control targets of library materials change their look at
# the defaults: each target is exported alone (Export Parameter) on a fresh copy and compared with the plain import.
#
# target_check.py <id>[,<id>...] <out.c>

import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSET = os.path.join(HERE, '..', '..', 'assets', 'material_maker', 'controls.json')

HEAD = r'''#include "global.h"

int   step  = 0;
int   wait  = 0;
int   at    = 0;
int   count = %d;
int   ids[%d]    = {%s};
char *nodes[%d]  = {%s};
char *params[%d] = {%s};
int   stops[%d]  = {%s};
slot_material_t *p = NULL;
slot_material_t *m = NULL;
int   pid = -1;
int   warm = 60; // Frames before the first material, for the app's own start

int diff(slot_material_t *a, slot_material_t *b) {
	script_material_set(a);
	gpu_texture_t *ta = mm_import_preview(mm_import_material_node(a), 0, 64);
	script_material_set(b);
	gpu_texture_t *tb = mm_import_preview(mm_import_material_node(b), 0, 64);
	int d = mm_import_image_changed(ta, tb);
	mm_import_texture_free(ta);
	mm_import_texture_free(tb);
	return d;
}

void next() {
	if (warm > 0) {
		warm = warm - 1;
		script_notify_on_next_frame(next);
		return;
	}
	if (mm_buffers_stat(11) > 0 || wait > 0) {
		wait = wait > 0 ? wait - 1 : 0;
		script_notify_on_next_frame(next);
		return;
	}
	if (at >= count) {
		printf("[target] done");
		script_quit();
		return;
	}
	wait = 4;
	if (step == 0) {
		mm_buffers_test_res(256);
		if (pid != ids[at]) {
			p   = script_material_create(string("plain_%%d", ids[at]));
			mm_import_library_into(ids[at], p);
			pid = ids[at];
		}
		m = script_material_create(string("t_%%d", at));
		mm_import_library_into(ids[at], m);
		ui_node_t *n = mm_node_named(m->canvas, nodes[at]);
		ui_node_t *v = n != NULL ? mm_node_export_param(m->canvas, n, params[at], stops[at]) : NULL;
		if (v == NULL) {
			printf("[target] %%d %%s.%%s#%%d: not exported", ids[at], nodes[at], params[at], stops[at]);
		}
		diff(m, p); // Asks for the buffers
		step = 1;
	}
	else {
		printf("[target] %%d %%s.%%s#%%d: changed %%d", ids[at], nodes[at], params[at], stops[at], diff(m, p));
		script_material_delete(m);
		at   = at + 1;
		step = 0;
	}
	script_notify_on_next_frame(next);
}

void main() {
	script_notify_on_next_frame(next);
}
'''


def main():
    ids = [x.strip() for x in sys.argv[1].split(',')]
    c = json.load(open(ASSET))
    rows = []
    for i in ids:
        seen = set()
        for ctl in c.get(i, []):
            for t in ctl['targets']:
                k = (t['node'], t['param'], t.get('stop', -1))
                if k not in seen:
                    seen.add(k)
                    rows.append((int(i),) + k)
    n = len(rows)
    q = lambda s: '"%s"' % s
    open(sys.argv[2], 'w').write(HEAD % (n, n, ', '.join(str(r[0]) for r in rows), n, ', '.join(q(r[1]) for r in rows), n,
                                         ', '.join(q(r[2]) for r in rows), n, ', '.join(str(r[3]) for r in rows)))
    print('%d targets' % n)


if __name__ == '__main__':
    main()
