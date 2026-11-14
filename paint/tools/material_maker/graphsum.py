# Compact, model-readable summaries of library materials' graphs, and the parameter definitions used to validate controls.
# A node is addressed by its path: its name at the top level, "graph/node" inside an inline graph node (one level).
import json, re
from packs import Pack


def clean_label(pd, name):
    for k in ('label', 'shortdesc'):
        t = re.sub(r'^\s*\d+\s*:', '', str(pd.get(k) or '')).strip().rstrip(':').strip()
        if t:
            return t
    return name

SKIP_TYPES = {'comment', 'reroute', 'ios', 'remote', 'material', 'portal', 'debug'}
SKIP_PARAM_TYPES = {'file', 'string', 'polygon', 'polyline', 'splines', 'curve', 'pixels', 'lattice', 'size'}
QUALITY_WORDS = ('quality', 'buffer', 'resolution', 'grid size', 'sigma', 'size of the buffer')


class Defs:
    """Parameter definitions of node types (nodes.pack) and of nodes carrying their own (shader, graph)."""

    def __init__(self, nodes_pack_path):
        self.nodes = Pack(nodes_pack_path)
        self.cache = {}

    def mmg(self, t):
        if t not in self.cache:
            e = self.nodes.find('name', t)
            self.cache[t] = self.nodes.json(e) if e is not None else None
        return self.cache[t]

    def of_model(self, m, inner=None):
        """name -> definition. A shader model's parameters, or a graph's gen_parameters widgets (their range comes from the
        first node parameter they drive)."""
        if m is None:
            return {}
        if 'shader_model' in m:
            return {p['name']: p for p in m['shader_model'].get('parameters', [])}
        if 'parameters' in m and isinstance(m.get('parameters'), list):  # Already a shader model
            return {p['name']: p for p in m['parameters']}
        out = {}
        nodes = m.get('nodes', inner or [])
        gp = [n for n in nodes if n.get('name') == 'gen_parameters']
        if not gp:
            return {}
        by_name = {n.get('name'): n for n in nodes}
        for w in gp[0].get('widgets', []):
            d = {'name': w.get('name'), 'label': w.get('label') or w.get('shortdesc') or '', 'longdesc': w.get('longdesc', ''),
                 'type': 'float', 'widget': w.get('type')}
            lw = (w.get('linked_widgets') or [{}])[0]
            target = by_name.get(lw.get('node'))
            if target is not None:
                td = self.of_node(target).get(lw.get('widget'))
                if td is not None:
                    for k in ('type', 'min', 'max', 'step', 'values'):
                        if k in td:
                            d[k] = td[k]
            if w.get('type') == 'config_control':
                d['type'] = 'enum'
                d['values'] = [{'name': c, 'value': i} for i, c in enumerate(w.get('configurations', {}).keys())]
            out[d['name']] = d
        return out

    def of_node(self, nd):
        t = nd.get('type', '')
        if t == 'shader' and 'shader_model' in nd:
            return {p['name']: p for p in nd['shader_model'].get('parameters', [])}
        if t == 'graph':
            return self.of_model(nd, nd.get('nodes'))
        return self.of_model(self.mmg(t))

    def label(self, nd):
        if nd.get('label'):
            return nd['label']
        t = nd.get('type', '')
        if t == 'shader':
            return nd.get('shader_model', {}).get('name', 'shader')
        m = self.mmg(t)
        if m is not None:
            return m.get('shader_model', {}).get('shortdesc') or m.get('shortdesc') or m.get('label') or t
        return t


def fmt_value(v):
    if isinstance(v, dict) and v.get('type') == 'Color':
        return '#%02x%02x%02x' % tuple(int(max(0, min(1, v.get(c, 0))) * 255) for c in 'rgb')
    if isinstance(v, dict) and v.get('type') == 'Gradient':
        return 'gradient[' + ', '.join('%.2f:%s' % (p.get('pos', 0), fmt_value(dict(p, type='Color'))) for p in sorted(v.get('points', []), key=lambda p: p.get('pos', 0))) + ']'
    if isinstance(v, float):
        return ('%.4g' % v)
    return json.dumps(v)


def walk_nodes(g):
    """(path, node) of the top level and one level into inline graph nodes."""
    for nd in g.get('nodes', []):
        yield nd['name'], nd
        if nd.get('type') == 'graph' and nd.get('nodes'):
            for inner in nd['nodes']:
                yield nd['name'] + '/' + inner['name'], inner


def params_of(defs, nd):
    """name -> (definition, value) of the editable parameters of a node"""
    d = defs.of_node(nd)
    vals = nd.get('parameters', {}) or {}
    out = {}
    for name, pd in d.items():
        t = pd.get('type')
        if t in SKIP_PARAM_TYPES:
            continue
        label = clean_label(pd, name)
        if any(w in label.lower() for w in QUALITY_WORDS):
            continue
        v = vals.get(name, pd.get('default'))
        out[name] = (pd, v)
    return out


def summary(defs, g, max_chars=60000, inner=True):
    lines = []
    for path, nd in walk_nodes(g):
        if not inner and '/' in path:
            continue
        if nd.get('type') in SKIP_TYPES or nd.get('name') == 'gen_parameters':
            continue
        ps = params_of(defs, nd)
        parts = []
        for name, (pd, v) in ps.items():
            t = pd.get('type')
            label = clean_label(pd, name)
            if t == 'float':
                rng = '%s..%s' % (fmt_value(pd.get('min', 0.0)), fmt_value(pd.get('max', 1.0)))
                parts.append('%s "%s" float %s = %s' % (name, label, rng, fmt_value(v)))
            elif t == 'enum':
                opts = [o.get('name') for o in pd.get('values', [])]
                cur = opts[v] if isinstance(v, int) and 0 <= v < len(opts) else v
                parts.append('%s "%s" enum %s = %s' % (name, label, '|'.join(map(str, opts)), json.dumps(cur)))
            elif t == 'boolean':
                parts.append('%s "%s" boolean = %s' % (name, label, json.dumps(v)))
            elif t in ('color', 'gradient'):
                parts.append('%s "%s" %s = %s' % (name, label, t, fmt_value(v)))
        depth = path.count('/')
        head = '%s%s [%s] "%s"' % ('  ' * depth, path, nd.get('type'), defs.label(nd))
        lines.append(head + (': ' + '; '.join(parts) if parts else ''))
    conns = ['%s -> %s' % (c['from'], c['to']) for c in g.get('connections', [])
             if not str(c['from']).startswith('gen_') and not str(c['to']).startswith('gen_')]
    authored = []
    for nd in g.get('nodes', []):
        if nd.get('type') == 'remote':
            for w in nd.get('widgets', []):
                authored.append('%s -> %s' % (w.get('label'), ', '.join('%s.%s' % (lw.get('node'), lw.get('widget')) for lw in w.get('linked_widgets', []))))
    # The links and the author's controls stay whole, the nodes get what is left
    tail = '\n\nLINKS (top level)\n' + ', '.join(conns)
    if authored:
        tail += "\n\nCONTROLS THE AUTHOR EXPOSED (label -> node.param)\n" + '\n'.join(authored)
    head = 'NODES (path [type] "label": param "label" type range = value)\n' + '\n'.join(lines)
    budget = max(20000, max_chars - len(tail))
    if len(head) > budget and inner:
        return summary(defs, g, max_chars, inner=False)
    if len(head) > budget:
        head = head[:budget] + '\n[cut: more nodes]'
    text = head + tail
    return text
