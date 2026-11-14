#!/usr/bin/env python3
# Removes from controls.json the controls the bulk test (tests/material_controls_all.c) found to change nothing, from its
# failures.txt ("[bulk] FAIL <id> <name> (...): <Label> changes nothing; ..."). Prints what it removed.
#
# drop_inert_controls.py <failures.txt> [--dry]

import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    path, dry = sys.argv[1], '--dry' in sys.argv
    inert = {}
    for line in open(path, errors='replace'):
        m = re.match(r'\[bulk\] FAIL (\d+) .*?\):(.*)$', line.strip())
        if not m:
            continue
        for reason in m.group(2).split(';'):
            r = re.match(r'\s*(.+) changes nothing\s*$', reason)
            if r:
                inert.setdefault(m.group(1), set()).add(r.group(1))
    c = json.load(open(os.path.join(HERE, 'controls.json')))
    removed = 0
    for key, labels in sorted(inert.items(), key=lambda kv: int(kv[0])):
        if key not in c:
            continue
        before = c[key]['controls']
        c[key]['controls'] = [x for x in before if x['label'] not in labels]
        gone = [x['label'] for x in before if x['label'] in labels]
        removed += len(gone)
        print('%s: %s' % (key, ', '.join(gone)))
    if not dry:
        rows = [' %s: %s' % (json.dumps(k), json.dumps(c[k], ensure_ascii=False)) for k in sorted(c, key=int)]
        open(os.path.join(HERE, 'controls.json'), 'w').write('{\n' + ',\n'.join(rows) + '\n}\n')
    print('%d controls removed%s' % (removed, ' (dry run)' if dry else ''))


if __name__ == '__main__':
    main()
