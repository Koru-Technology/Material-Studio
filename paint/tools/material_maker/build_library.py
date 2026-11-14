#!/usr/bin/env python3
# Builds the Material Maker library shipped with the app (paint/assets/material_maker):
#   library.pack  the community materials (.ptex) with an index: name, author, license, tags, section, material kind
#   nodes.pack    Material Maker's node library (.mmg), io types, shader functions, the library tree (library_base, from
#                 material_maker/library/base.json) and the website nodes the materials use
# and writes credits (paint/assets/licenses/license_material_maker_library.md) and a report of what was left out.
#
# build_library.py --cache <dir> --mm <material-maker checkout> [--fetch]
#   --fetch downloads the catalogue and every material from materialmaker.org into the cache (first run, or to update)
#   --mm    a checkout of https://github.com/RodZill4/material-maker at the pinned commit (MM_COMMIT)
#
# Pack format (little endian): "MMPK", u32 version, u32 count, u32 index size, index (json), then the blobs. The index
# is a json array of entries with "o" (blob offset after the index), "n" (deflated size) and "u" (inflated size); each
# blob is raw deflate of minified json.

import argparse, json, os, re, struct, sys, urllib.request, zlib

MM_COMMIT = '7d6daae3c74c09720afe4138ce09699ab3591ce1'  # Material Maker 1.8
API = 'https://www.materialmaker.org/api'
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(ROOT, 'assets', 'material_maker')

# Library sections in priority order: the first section with a matching word in the tags, then the name, wins
SECTIONS = [
    ('Food', ['food', 'fruit', 'vegetable', 'cookie', 'bread', 'cake', 'chocolate', 'pizza', 'cheese', 'meat', 'candy', 'sushi',
              'donut', 'biscuit', 'orange', 'apple', 'banana', 'watermelon', 'strawberry', 'lemon', 'egg', 'corn', 'potato',
              'tomato', 'pasta', 'cereal', 'waffle', 'pie', 'coffee', 'crop']),
    ('Sci-Fi & Tech', ['scifi', 'sci-fi', 'tech', 'circuit', 'panel', 'panels', 'robot', 'cyber', 'spaceship', 'neon', 'hologram',
                       'computer', 'greeble', 'industrial']),
    ('Metal', ['metal', 'metallic', 'rust', 'rusty', 'gold', 'copper', 'iron', 'steel', 'bronze', 'silver', 'chrome', 'aluminium',
               'aluminum', 'brass', 'chain']),
    ('Wood', ['wood', 'wooden', 'planks', 'plank', 'bark', 'log', 'logs', 'parquet', 'timber', 'plywood']),
    ('Bricks & Walls', ['brick', 'bricks', 'wall', 'walls', 'masonry', 'plaster', 'stucco', 'castle']),
    ('Tiles & Floors', ['tile', 'tiles', 'floor', 'pavement', 'cobblestone', 'cobblestones', 'cobble', 'paving', 'mosaic',
                        'terrazzo', 'roof', 'shingle', 'shingles', 'herringbone']),
    ('Stone & Rock', ['rock', 'rocks', 'stone', 'stones', 'cliff', 'marble', 'granite', 'sandstone', 'pebbles', 'gravel',
                      'concrete', 'asphalt', 'slate', 'basalt', 'crystal', 'crystals', 'gem', 'ice', 'lava']),
    ('Ground & Nature', ['ground', 'dirt', 'grass', 'sand', 'mud', 'moss', 'soil', 'terrain', 'forest', 'leaves', 'leaf', 'snow',
                         'desert', 'nature', 'water', 'beach', 'swamp', 'tree', 'flowers', 'flower', 'hay', 'straw']),
    ('Fabric & Leather', ['fabric', 'cloth', 'leather', 'wool', 'carpet', 'knit', 'denim', 'textile', 'velvet', 'silk', 'rug',
                          'weave', 'woven', 'quilt', 'lace']),
    ('Creatures & Organic', ['skin', 'scales', 'scale', 'fur', 'flesh', 'organic', 'alien', 'reptile', 'dragon', 'snake',
                             'feather', 'feathers', 'bone', 'bones', 'coral', 'creature', 'monster']),
    ('Stylized', ['stylized', 'stylised', 'handpainted', 'toon', 'cartoon']),
    ('Patterns', ['pattern', 'patterns', 'geometric', 'ornament', 'tribal', 'celtic', 'runes', 'rune', 'mandala', 'truchet',
                  'voronoi', 'hexagon', 'hexagons', 'stripes', 'checker']),
]
SURFACE_KINDS = {'material', 'material_tesselated', 'material_unlit', 'material_dynamic', 'material_export'}
# Names of things that are not surfaces to paint: emblems, signs, lettering (more are listed by id in overrides.json)
NOT_SURFACES = ['symbol', 'symbols', 'logo', 'icon', 'emoji', 'button', 'badge', 'sticker', 'stickers', 'decal', 'letters',
                'alphabet', 'glyph', 'glyphs']


def fetch(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'MaterialStudio-library-builder'})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def words(s):
    return set(re.findall(r'[a-z][a-z\-]+', s.lower()))


def walk(graph, out):
    for n in graph.get('nodes', []):
        out.append(n)
        if 'nodes' in n:
            walk(n, out)


def material_kind(graph):
    for n in graph.get('nodes', []):
        t = n.get('type', '')
        if n.get('name') == 'Material' or t.startswith('material'):
            if t == 'material_export' or (t == 'shader' and 'preview_shader' in n.get('shader_model', {})):
                return 'material_export'
            return t
    return None


def missing_files(nodes):
    """Paths the material needs that only existed on its author's computer."""
    out = []
    for n in nodes:
        p = n.get('parameters', {})
        if n.get('type') == 'image':
            path = str(p.get('image', ''))
            if path and not path.startswith('%MATERIAL_MAKER_PATH%'):
                out.append(path)
        if n.get('type') == 'text':
            font = str(p.get('font', ''))
            if font:
                out.append(font)
    return out


def classify(meta, overrides):
    o = overrides.get(str(meta['id']))
    if o is not None:
        return o if o != 'exclude' else None
    if words(meta['name']) & set(NOT_SURFACES):
        return None
    tags = [t.strip().lower() for t in (meta.get('tags') or '').split(',') if t.strip()]
    tw = set()
    for t in tags:
        tw |= words(t)
    nw = words(meta['name'])
    for section, keys in SECTIONS:
        if any(k in tw for k in keys) or any(k in nw for k in keys):
            return section
    return None


def write_pack(path, entries, blobs):
    data = b''
    for e, b in zip(entries, blobs):
        c = zlib.compressobj(9, zlib.DEFLATED, -15)
        z = c.compress(b) + c.flush()
        e['o'], e['n'], e['u'] = len(data), len(z), len(b)
        data += z
    index = json.dumps(entries, separators=(',', ':'), ensure_ascii=False).encode('utf-8')
    with open(path, 'wb') as f:
        f.write(b'MMPK' + struct.pack('<III', 1, len(entries), len(index)) + index + data)
    return len(index) + len(data)


def minify(obj):
    return json.dumps(obj, separators=(',', ':'), ensure_ascii=False).encode('utf-8')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cache', required=True)
    ap.add_argument('--mm', required=True)
    ap.add_argument('--fetch', action='store_true')
    args = ap.parse_args()
    cache = args.cache
    os.makedirs(os.path.join(cache, 'ptex'), exist_ok=True)

    if args.fetch or not os.path.exists(os.path.join(cache, 'catalog.json')):
        open(os.path.join(cache, 'catalog.json'), 'wb').write(fetch(API + '/getMaterials'))
    catalog = json.load(open(os.path.join(cache, 'catalog.json')))
    materials = [m for m in catalog if (int(m['type']) & 15) == 0]
    for m in materials:
        p = os.path.join(cache, 'ptex', '%s.json' % m['id'])
        if args.fetch and not os.path.exists(p):
            open(p, 'wb').write(fetch(API + '/getMaterial?id=%s' % m['id']))

    overrides_path = os.path.join(HERE, 'overrides.json')
    overrides = json.load(open(overrides_path)) if os.path.exists(overrides_path) else {}

    entries, blobs, report, website = [], [], {}, set()
    for m in sorted(materials, key=lambda m: int(m['id'])):
        p = os.path.join(cache, 'ptex', '%s.json' % m['id'])
        if not os.path.exists(p):
            report.setdefault('not downloaded', []).append(m)
            continue
        graph = json.loads(json.load(open(p))['json'])
        nodes = []
        walk(graph, nodes)
        kind = material_kind(graph)
        if kind not in SURFACE_KINDS:
            report.setdefault('not a surface (%s)' % kind, []).append(m)
            continue
        missing = missing_files(nodes)
        if missing:
            report.setdefault('needs files from its author', []).append(dict(m, missing=missing))
            continue
        section = classify(m, overrides)
        if section is None:
            report.setdefault('not categorised', []).append(m)
            continue
        size = 10
        for n in graph.get('nodes', []):
            if n.get('name') == 'Material':
                size = int(n.get('parameters', {}).get('size', 10))
        website |= {n['type'] for n in nodes if str(n.get('type', '')).startswith('website:')}
        entries.append({'id': int(m['id']), 'name': m['name'], 'author': m['author'], 'license': m['license'],
                        'tags': m.get('tags') or '', 'section': section, 'kind': kind, 'size': size})
        blobs.append(minify(graph))

    os.makedirs(OUT, exist_ok=True)
    lib_size = write_pack(os.path.join(OUT, 'library.pack'), entries, blobs)

    # Node library: every .mmg, the io types, the shader functions and the website nodes the materials use
    nodes_dir = os.path.join(args.mm, 'addons', 'material_maker', 'nodes')
    n_entries, n_blobs = [], []
    for f in sorted(os.listdir(nodes_dir)):
        if f.endswith('.mmg') or f.endswith('.mmt'):
            text = open(os.path.join(nodes_dir, f), encoding='utf-8').read()
            text = re.sub(r',(\s*[\]}])', r'\1', text)  # A few files have trailing commas
            n_entries.append({'name': f.rsplit('.', 1)[0], 'kind': f.rsplit('.', 1)[1]})
            n_blobs.append(minify(json.loads(text)))
    sf = open(os.path.join(args.mm, 'addons', 'material_maker', 'shader_functions.tres'), encoding='utf-8').read()
    m = re.search(r'(?:code|text) = "(.*)"\s*$', sf, re.S)
    n_entries.append({'name': 'shader_functions', 'kind': 'glsl'})
    n_blobs.append((m.group(1) if m else sf).encode('utf-8').decode('unicode_escape').encode('utf-8'))
    # The library tree: menu folder (tree_item), display name and preset parameters of each node (mm_nodes.c)
    base = open(os.path.join(args.mm, 'material_maker', 'library', 'base.json'), encoding='utf-8').read()
    n_entries.append({'name': 'library_base', 'kind': 'json'})
    tree = json.loads(re.sub(r',(\s*[\]}])', r'\1', base))
    for item in tree.get('lib', []):
        item.pop('icon_data', None)  # PNG thumbnails, the add menu shows no images: 0.7 MB and parse time saved
    n_blobs.append(minify(tree))
    for w in sorted(website):
        wid = w.split(':')[1]
        p = os.path.join(cache, 'website_%s.json' % wid)
        if not os.path.exists(p):
            open(p, 'wb').write(fetch(API + '/getMaterial?id=%s' % wid))
        n_entries.append({'name': w, 'kind': 'mmg'})
        n_blobs.append(minify(json.loads(json.load(open(p))['json'])))
    nodes_size = write_pack(os.path.join(OUT, 'nodes.pack'), n_entries, n_blobs)

    # Credits for every CC-BY / CC-BY-SA material, and the report
    credits = ['# Material Maker community materials', '',
               'From https://www.materialmaker.org, under the license shown for each material: CC0 1.0, CC BY 4.0 or '
               'CC BY-SA 4.0. The CC-BY-SA materials stay under CC BY-SA 4.0.', '']
    for e in sorted(entries, key=lambda e: (e['author'].lower(), e['name'].lower())):
        if e['license'] != 'CC0':
            credits.append('- "%s" by %s, %s, https://www.materialmaker.org/material?id=%d' % (e['name'], e['author'], e['license'], e['id']))
    open(os.path.join(ROOT, 'assets', 'licenses', 'license_material_maker_library.md'), 'w').write('\n'.join(credits) + '\n')

    lines = ['# Material Maker library report', '', '%d of %d materials included.' % (len(entries), len(materials)), '']
    sections = {}
    for e in entries:
        sections[e['section']] = sections.get(e['section'], 0) + 1
    for s, c in sorted(sections.items(), key=lambda x: -x[1]):
        lines.append('- %s: %d' % (s, c))
    for reason, ms in sorted(report.items()):
        lines += ['', '## Left out: %s (%d)' % (reason, len(ms)), '']
        lines += ['- %s "%s" by %s' % (x['id'], x['name'], x['author']) for x in ms]
    open(os.path.join(HERE, 'report.md'), 'w').write('\n'.join(lines) + '\n')
    print('%d materials, library.pack %.1f MB, nodes.pack %.1f MB, %d website nodes' % (len(entries), lib_size / 1e6, nodes_size / 1e6, len(website)))
    for reason, ms in sorted(report.items()):
        print('  left out, %s: %d' % (reason, len(ms)))


if __name__ == '__main__':
    main()
