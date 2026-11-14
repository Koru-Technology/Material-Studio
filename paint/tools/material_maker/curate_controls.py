#!/usr/bin/env python3
# Chooses the controls of each library material (controls.md): Claude reads the material's preview, name and a summary
# of its graph (graphsum.py) and proposes up to six controls; each is checked against the graph, defaults set to the
# graph's values, and kept only when valid. Writes controls.json. Needs ANTHROPIC_API_KEY and the anthropic package.
#
# curate_controls.py --cache <dir> [--ids 30,106,...] [--limit n] [--model claude-sonnet-5-5] [--jobs 8]
#   --cache  answers by id, so a run can stop and go on; delete an answer to ask again
#
# controls.json is rewritten from every answer in the cache, the materials not asked yet keep what they had.

import argparse, base64, concurrent.futures as cf, json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from packs import Pack
from graphsum import Defs, summary, walk_nodes, params_of, clean_label

HERE = os.path.dirname(os.path.abspath(__file__))
MM = os.path.join(HERE, '..', '..', 'assets', 'material_maker')
OUT = os.path.join(HERE, 'controls.json')

TYPES = ['slider', 'toggle', 'choice', 'color', 'gradient', 'seed']
GROUPS = ['color', 'pattern', 'surface', 'detail']
UNITS = ['percent', 'degrees', 'count', 'multiplier']
COMMON = ['Hue', 'Saturation', 'Brightness', 'Roughness', 'Metallic', 'Bumps', 'Height', 'Glow', 'Scale', 'Rotation', 'Shuffle']

SCALAR = {'anyOf': [{'type': 'number'}, {'type': 'string'}, {'type': 'boolean'}, {'type': 'null'}]}
NUM = {'anyOf': [{'type': 'number'}, {'type': 'null'}]}
SCHEMA = {
    'type': 'object', 'additionalProperties': False, 'required': ['controls', 'notes'],
    'properties': {
        'notes': {'type': 'string'},
        'controls': {'type': 'array', 'items': {
            'type': 'object', 'additionalProperties': False,
            'required': ['id', 'label', 'type', 'group', 'min', 'max', 'default', 'step', 'unit', 'options', 'targets', 'why'],
            'properties': {
                'id': {'type': 'string'}, 'label': {'type': 'string'},
                'type': {'type': 'string', 'enum': TYPES}, 'group': {'type': 'string', 'enum': GROUPS},
                'min': NUM, 'max': NUM, 'default': SCALAR, 'step': NUM,
                'unit': {'anyOf': [{'type': 'string', 'enum': UNITS}, {'type': 'null'}]},
                'options': {'anyOf': [{'type': 'null'}, {'type': 'array', 'items': {
                    'type': 'object', 'additionalProperties': False, 'required': ['label', 'values'],
                    'properties': {'label': {'type': 'string'}, 'values': {'type': 'array', 'items': SCALAR}}}}]},
                'targets': {'type': 'array', 'items': {
                    'type': 'object', 'additionalProperties': False, 'required': ['node', 'param', 'to', 'stop', 'off', 'on'],
                    'properties': {'node': {'type': 'string'}, 'param': {'type': 'string'},
                                   'to': {'anyOf': [{'type': 'null'}, {'type': 'array', 'items': {'type': 'number'}}]},
                                   'stop': {'anyOf': [{'type': 'integer'}, {'type': 'null'}]}, 'off': SCALAR, 'on': SCALAR}}},
                'why': {'type': 'string'}}}}}}

SYSTEM = """You choose the controls of a material in Material Studio, a 3D texture painting app for casual artists. Its library has about 1000 procedural materials made in Material Maker, each a node graph. A control is a slider, switch, swatch or menu that changes parameters of the material's nodes, so a user can make the material their own without the node editor.

Every material already has these common controls, applied after the graph: %s (hue, saturation and brightness of the whole base color; roughness and metallic shifts; normal and height strength; emission; scale and rotation of the whole material; a new random seed). Never propose one of these again. A narrower version is fine: the color of the moss, the size of the bricks.

You get the material's name, section, style, tags, a 128 px preview on a sphere, and its graph: every node with its path, type, label and editable parameters (name, label, type, range, current value), the links between top-level nodes, and the controls the material's author exposed, if any.

Choose 0 to 6 controls, the most useful first. Good controls are what an artist would want to change in THIS material, each making a clear, visible difference over its range:
- colors of its distinct parts: the colors of the gradients (colorize) or colors that become the base color. A gradient of 2 to 4 stops with one meaning can be a "gradient" control; a stop that is one part ("Mortar Color", "Moss Color") a "color" control on that stop (stop: its index in the gradient as listed). Follow the links: only colors that reach the base color.
- layout and size of its elements: rows and columns of bricks or tiles, plank width, cell count, pattern type ("choice").
- amounts of wear, moss, dirt, rust, cracks, puddles, snow: the blend amount or threshold that masks that layer in.
- shape of the elements: roundness, bevel, gap or mortar width, irregularity.
Rules:
- Targets must be nodes and parameters listed in the graph, by exact path and parameter name. A control may set several parameters together (rows and columns, both scales of a noise); "to": [value at min, value at max] maps the control's range onto a target's when they differ, else null.
- Sliders: min, max, default (the current value), step (1 for counts, else null), unit (percent only for a 0 to 1 amount, count, degrees, or null). Keep the range where the material still looks good, usually narrower than the parameter's.
- Toggles: default true or false; targets boolean parameters, or give each target "off" and "on" values.
- Choices: 2 to 6 options, each with a label and "values" (one per target, an enum option by its name as listed); default the label of the option in use. Only meaningful choices (brick bond, tile shape), never blend modes or noise algorithms.
- Colors: default the current color as #rrggbb. Gradients: default null.
- Never: seeds, blend modes, noise algorithm or octave settings, normal map or occlusion strengths, transforms of the whole material, anything about quality, resolution or buffers, nodes whose effect you cannot trace to the output.
- Labels: Title Case, one to three plain words a casual user understands ("Brick Rows", "Moss Amount", "Plank Width"), no node jargon (no "Persistence", "Tones Step", "Blend Amount").
- ids: snake_case, unique.
- group: color, pattern (layout, size, shape of elements), surface (relief, wear, dirt, moss, rust, cracks), detail (finer noise, variation).
- why: one short sentence on what the control does to the material.
- Fewer good controls beat many. A material may need none beyond the common ones: return an empty list and say why in notes. Use notes also for an obvious control you left out and why.""" % ', '.join(COMMON)


def hex_color(v):
    if isinstance(v, dict):
        return '#%02x%02x%02x' % tuple(int(round(max(0, min(1, v.get(c, 0))) * 255)) for c in 'rgb')
    return None


def node_index(g):
    return {path: nd for path, nd in walk_nodes(g)}


def validate(defs, g, answer):
    """The valid controls with the graph's values as defaults, and the problems of the others."""
    nodes = node_index(g)
    kept, problems, ids = [], [], set()
    for c in answer.get('controls', []):
        err = []
        if c['label'].strip().lower() in [x.lower() for x in COMMON]:
            err.append('repeats a common control')
        cid = re.sub(r'[^a-z0-9_]', '_', c['id'].lower())
        if cid in ids:
            err.append('duplicate id')
        targets = []
        for t in c.get('targets', []):
            nd = nodes.get(t['node'])
            if nd is None:
                err.append('no node %s' % t['node'])
                continue
            ps = params_of(defs, nd)
            if t['param'] not in ps:
                err.append('no parameter %s.%s' % (t['node'], t['param']))
                continue
            pd, value = ps[t['param']]
            targets.append((t, pd, value))
        if not targets and c['type'] != 'seed':
            err.append('no target')
        out = {'id': cid, 'label': c['label'].strip(), 'type': c['type'], 'group': c['group']}
        ty = c['type']
        if ty == 'slider' and targets:
            for t, pd, v in targets:
                if pd.get('type') != 'float':
                    err.append('%s.%s is %s, not float' % (t['node'], t['param'], pd.get('type')))
            t0, pd0, v0 = targets[0]
            lo, hi = c.get('min'), c.get('max')
            out['targets'] = []
            if lo is None or hi is None or not lo < hi:
                err.append('no range')
            elif isinstance(v0, (int, float)):
                def maps(t):
                    return t.get('to') and len(t['to']) == 2

                def f(t, x):  # The target's value for the control's value x
                    return t['to'][0] + (x - lo) / (hi - lo) * (t['to'][1] - t['to'][0]) if maps(t) else x
                # The default: the first target's value, through its mapping when it has one
                if maps(t0) and t0['to'][1] != t0['to'][0]:
                    d = lo + (v0 - t0['to'][0]) / (t0['to'][1] - t0['to'][0]) * (hi - lo)
                else:
                    d = v0
                # Every target keeps its value at the default: the material looks as it did until the control moves
                for t, pd, v in targets:
                    span = abs(pd.get('max', 1.0) - pd.get('min', 0.0)) or 1.0
                    if not isinstance(v, (int, float)) or abs(f(t, d) - v) > max(1e-3, 0.01 * span):
                        err.append('at its default it would set %s.%s to %.4g, the graph has %s' % (t['node'], t['param'], f(t, d), v))
                nlo, nhi = min(lo, d), max(hi, d)  # The current look stays reachable, the mappings extended to the wider range
                for t, _, _ in targets:
                    tt = {'node': t['node'], 'param': t['param']}
                    if maps(t):
                        tt['to'] = [round(f(t, nlo), 6), round(f(t, nhi), 6)]
                    out['targets'].append(tt)
                out.update(min=nlo, max=nhi, default=round(d, 6))
                if c.get('step'):
                    out['step'] = c['step']
                if c.get('unit') and not (c['unit'] == 'percent' and not (-1.0 <= nlo and nhi <= 4.0)):
                    out['unit'] = c['unit']
            else:
                err.append('%s.%s has no number to start from (%s)' % (t0['node'], t0['param'], json.dumps(v0)))
        elif ty == 'toggle':
            out['targets'] = []
            for t, pd, v in targets:
                if pd.get('type') == 'boolean' and t.get('off') is None:
                    out['targets'].append({'node': t['node'], 'param': t['param']})
                elif t.get('off') is not None and t.get('on') is not None:
                    out['targets'].append({'node': t['node'], 'param': t['param'], 'off': t['off'], 'on': t['on']})
                else:
                    err.append('%s.%s needs off and on values' % (t['node'], t['param']))
            if targets:
                t0, pd0, v0 = targets[0]
                out['default'] = bool(v0) if t0.get('off') is None else v0 == t0.get('on')
        elif ty == 'choice':
            opts = c.get('options') or []
            if not 2 <= len(opts) <= 6:
                err.append('needs 2 to 6 options')
            fixed = []
            for o in opts:
                vals = o.get('values') or []
                if len(vals) != len(targets):
                    err.append('option %s: %d values for %d targets' % (o.get('label'), len(vals), len(targets)))
                    continue
                conv = []
                for (t, pd, v), val in zip(targets, vals):
                    if pd.get('type') == 'enum':
                        names = [x.get('name') for x in pd.get('values', [])]
                        if isinstance(val, str) and val in names:
                            val = names.index(val)
                        elif not (isinstance(val, int) and 0 <= val < len(names)):
                            err.append('option %s: %s is not an option of %s.%s' % (o.get('label'), val, t['node'], t['param']))
                    conv.append(val)
                fixed.append({'label': o['label'].strip(), 'values': conv})
            out['options'] = fixed
            out['targets'] = [{'node': t['node'], 'param': t['param']} for t, _, _ in targets]
            current = [v for _, _, v in targets]
            match = [o['label'] for o in fixed if o['values'] == current]
            out['default'] = match[0] if match else c.get('default')
            if not match:
                err.append('no option matches the current values')
        elif ty == 'color':
            out['targets'] = []
            for t, pd, v in targets:
                if pd.get('type') == 'color':
                    out['targets'].append({'node': t['node'], 'param': t['param']})
                    out.setdefault('default', hex_color(v))
                elif pd.get('type') == 'gradient' and t.get('stop') is not None:
                    pts = (v or {}).get('points', [])
                    order = sorted(range(len(pts)), key=lambda k: pts[k].get('pos', 0))  # Stops as the summary lists them
                    if 0 <= t['stop'] < len(pts):
                        out['targets'].append({'node': t['node'], 'param': t['param'], 'stop': order[t['stop']]})
                        out.setdefault('default', hex_color(dict(pts[order[t['stop']]], type='Color')))
                    else:
                        err.append('%s.%s has no stop %s' % (t['node'], t['param'], t['stop']))
                else:
                    err.append('%s.%s is not a color or a gradient stop' % (t['node'], t['param']))
            colors = set()
            for tt in out['targets']:
                v = [x for x in targets if x[0]['node'] == tt['node'] and x[0]['param'] == tt['param']][0][2]
                colors.add(hex_color(v) if 'stop' not in tt else hex_color(dict(v['points'][tt['stop']], type='Color')))
            if len(colors) > 1:
                err.append('its colors differ now (%s), one control would make them the same' % ', '.join(sorted(colors)))
        elif ty == 'gradient':
            out['targets'] = []
            for t, pd, v in targets:
                if pd.get('type') == 'gradient':
                    out['targets'].append({'node': t['node'], 'param': t['param']})
                else:
                    err.append('%s.%s is not a gradient' % (t['node'], t['param']))
            if len({json.dumps(v, sort_keys=True) for _, _, v in targets}) > 1:
                err.append('its gradients differ now, one control would make them the same')
        elif ty == 'seed':
            out['targets'] = [{'node': t['node'], 'param': t['param']} for t, _, _ in targets]
            err.append('seeds are the common Shuffle')
        if err:
            problems.append({'id': cid, 'label': c['label'], 'problems': err})
        else:
            ids.add(cid)
            kept.append(out)
    return kept[:6], problems


def ask(client, model, effort, e, g, name, meta, thumb, defs, retry=None):
    content = [{'type': 'text', 'text': 'Material "%s" (originally "%s"). Section: %s. Style: %s. Tags: %s. Kind: %s.' % (
        name, e['name'], meta.get('section', e['section']), meta.get('style', '?'), e.get('tags') or '(none)', e['kind'])}]
    if thumb:
        content.append({'type': 'image', 'source': {'type': 'base64', 'media_type': 'image/jpeg', 'data': base64.standard_b64encode(thumb).decode()}})
    content.append({'type': 'text', 'text': summary(defs, g)})
    content.append({'type': 'text', 'text': 'Choose its controls.'})
    messages = [{'role': 'user', 'content': content}]
    if retry is not None:  # The first answer and why some of its controls were rejected
        previous, problems = retry
        messages.append({'role': 'assistant', 'content': json.dumps(previous)})
        messages.append({'role': 'user', 'content': 'These controls were rejected:\n' + '\n'.join(
            '- %s: %s' % (p['label'], '; '.join(p['problems'])) for p in problems) +
            '\nReturn the whole list again: fix them (a control with several targets must keep every target at its current value at the default, or drop the targets that do not fit) or leave them out.'})
    r = client.messages.create(model=model, max_tokens=16000, system=SYSTEM, messages=messages,
                               output_config={'effort': effort, 'format': {'type': 'json_schema', 'schema': SCHEMA}})
    if r.stop_reason != 'end_turn':
        raise RuntimeError('stop_reason %s' % r.stop_reason)
    return json.loads(next(b.text for b in r.content if b.type == 'text')), r.usage


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cache', required=True)
    ap.add_argument('--ids')
    ap.add_argument('--limit', type=int)
    ap.add_argument('--model', default='claude-sonnet-5-5')
    ap.add_argument('--effort', default='high')
    ap.add_argument('--jobs', type=int, default=8)
    args = ap.parse_args()
    import anthropic
    client = anthropic.Anthropic(max_retries=5)
    os.makedirs(args.cache, exist_ok=True)

    lib = Pack(os.path.join(MM, 'library.pack'))
    thumbs = Pack(os.path.join(MM, 'thumbs.pack'))
    defs = Defs(os.path.join(MM, 'nodes.pack'))
    curation = json.load(open(os.path.join(HERE, 'curation.json')))
    entries = lib.index
    if args.ids:
        want = {int(x) for x in args.ids.split(',')}
        entries = [e for e in entries if e['id'] in want]
    if args.limit:
        entries = entries[:args.limit]

    def run(e):
        path = os.path.join(args.cache, '%d.json' % e['id'])
        if os.path.exists(path):
            return e['id'], 'cached', None
        g = lib.json(e)
        te = thumbs.find('id', e['id'])
        thumb = thumbs.raw(te) if te is not None else b''
        meta = curation.get(str(e['id']), {})
        answer, usage = ask(client, args.model, args.effort, e, g, meta.get('name', e['name']), meta, thumb, defs)
        _, problems = validate(defs, g, answer)
        if problems:  # One round of repairs
            fixed, usage2 = ask(client, args.model, args.effort, e, g, meta.get('name', e['name']), meta, thumb, defs, (answer, problems))
            answer = dict(fixed, first=answer)
            usage.input_tokens += usage2.input_tokens
            usage.output_tokens += usage2.output_tokens
        json.dump(answer, open(path, 'w'), ensure_ascii=False, indent=1)
        return e['id'], 'ok', usage

    totals = [0, 0]
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        futs = {ex.submit(run, e): e['id'] for e in entries}
        for f in cf.as_completed(futs):
            try:
                i, status, usage = f.result()
                if usage is not None:
                    totals[0] += usage.input_tokens
                    totals[1] += usage.output_tokens
            except Exception as err:
                print('%d failed: %s' % (futs[f], repr(err)[:300]), flush=True)
    print('tokens in %d out %d' % tuple(totals))

    # controls.json from every cached answer, checked against the graphs
    out = json.load(open(OUT)) if os.path.exists(OUT) else {}
    report = {}
    for e in lib.index:
        path = os.path.join(args.cache, '%d.json' % e['id'])
        if not os.path.exists(path):
            continue
        answer = json.load(open(path))
        g = lib.json(e)
        kept, problems = validate(defs, g, answer)
        if 'first' in answer:  # A repaired answer: the first answer's valid controls it dropped come back, in their place
            first, _ = validate(defs, g, answer['first'])
            fixed = {c['id']: c for c in kept}
            merged = [fixed.pop(c['id'], c) for c in first] + list(fixed.values())
            seen, kept = set(), []
            for c in merged:
                if c['id'] not in seen and c['label'].lower() not in seen:
                    seen |= {c['id'], c['label'].lower()}
                    kept.append(c)
            kept = kept[:6]
        out[str(e['id'])] = {'controls': kept, 'notes': answer.get('notes', '')}
        if problems:
            report[str(e['id'])] = problems
    rows = [' %s: %s' % (json.dumps(k), json.dumps(out[k], ensure_ascii=False)) for k in sorted(out, key=int)]
    open(OUT, 'w').write('{\n' + ',\n'.join(rows) + '\n}\n')
    json.dump(report, open(os.path.join(args.cache, 'problems.json'), 'w'), ensure_ascii=False, indent=1)
    n = sum(len(v['controls']) for v in out.values())
    print('%d materials, %d controls, %d dropped as invalid' % (len(out), n, sum(len(v) for v in report.values())))


if __name__ == '__main__':
    main()
