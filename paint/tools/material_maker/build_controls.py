#!/usr/bin/env python3
# Builds paint/assets/material_maker/controls.json, the controls the app wires into library materials when they are
# imported (nodes_material/mm_controls.c), from controls.json here (controls.md): minified, without notes, materials
# without controls left out. Every target is checked against library.pack.
#
# build_controls.py [--ids 30,133,...]
#   --ids  only these materials (the pilot)
#
# Not shipped yet (the app skips them, with a note): targets inside an inline graph node ("graph/node") and on the
# Material node; a control left without targets is dropped.

import argparse, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from packs import Pack
from graphsum import Defs, walk_nodes, params_of

HERE = os.path.dirname(os.path.abspath(__file__))
MM = os.path.join(HERE, '..', '..', 'assets', 'material_maker')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ids')
    args = ap.parse_args()
    controls = json.load(open(os.path.join(HERE, 'controls.json')))
    lib = Pack(os.path.join(MM, 'library.pack'))
    defs = Defs(os.path.join(MM, 'nodes.pack'))
    want = {x.strip() for x in args.ids.split(',')} if args.ids else None
    out, dropped, problems = {}, 0, []
    for key in sorted(controls, key=int):
        if want is not None and key not in want:
            continue
        e = lib.find('id', int(key))
        if e is None:
            problems.append('%s: not in library.pack' % key)
            continue
        nodes = {path: nd for path, nd in walk_nodes(lib.json(e))}
        kept = []
        for c in controls[key]['controls']:
            targets = [t for t in c['targets'] if '/' not in t['node'] and t['node'] != 'Material']
            for t in targets:
                nd = nodes.get(t['node'])
                if nd is None or t['param'] not in params_of(defs, nd):
                    problems.append('%s %s: no %s.%s' % (key, c['label'], t['node'], t['param']))
            if len(targets) != len(c['targets']):
                dropped += 1
                if not targets:
                    continue
                if c['type'] == 'choice':  # Options give one value per target: keep the values of the kept targets
                    keep = [i for i, t in enumerate(c['targets']) if t in targets]
                    c = dict(c, options=[dict(o, values=[o['values'][i] for i in keep]) for o in c['options']])
            kept.append(dict(c, targets=targets))
        if kept:
            out[key] = kept
    if problems:
        print('\n'.join(problems))
        sys.exit(1)
    path = os.path.join(MM, 'controls.json')
    open(path, 'w').write(json.dumps(out, separators=(',', ':'), ensure_ascii=False) + '\n')
    print('%d materials, %d controls (%d with targets left out), %.1f KB' % (
        len(out), sum(len(v) for v in out.values()), dropped, os.path.getsize(path) / 1e3))


if __name__ == '__main__':
    main()
