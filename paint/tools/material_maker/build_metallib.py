#!/usr/bin/env python3
# Precompiled Metal libraries of the library materials' buffer passes (mm_plan.c): the app compiles each pass's Metal
# source when a material is first used (~85 ms a shader on an iPad, dozens per material) and iOS forgets its shader cache
# on every install. metal_gpu.m (metal_library) looks the source up in these packs and loads the library compiled here.
#
# 1. Dump the sources with the checker (mm_check.c, built with build_check.sh), each blob exactly as the app makes it:
#      MM_DUMP_MSL=<dump> mm_check paint/assets/material_maker <scratch dir>
#    (<dump>/materials.tsv lists what the run made: sources of older runs stay in <dump>, unused)
# 2. build_metallib.py <dump> [--out paint/assets/material_maker/msl] [--jobs N] [--platforms ios,macos]
#    compiles <dump>/<platform>/<hash>.metal into <dump>/<platform>_lib/<hash>.metallib (kept: a run again compiles only
#    new sources) with the options of newLibraryWithSource:options:nil (fast math, default optimization), the language
#    version the deployment target defaults to, and writes <out>/msl_<platform>.pack.
# Check a pack with metallib_test.m. The keys survive the app numbering generators otherwise (metal_msl_hash.h); any
# other change of the generator or of iron_glsl.c (glslang, SPIRV-Cross) changes the sources: run 1 and 2 again.
#
# Pack format: see base/sources/backends/metal_msl_pack.h (the reader). One metallib per shader, its function keeps its
# name ("vert" or "frag"); the metallibs materials add (materials.tsv, in library order) go in xz blocks of at least
# --block bytes: a material's are asked for together and compress several times better together (library, iOS: per
# material blocks 41.6 MB, 512 KB 34.9 MB, 1 MB 33.0 MB, 4 MB 29.1 MB; Carved Stone's lookups read 19, 16, 12 blocks).
# A source that changed since simply misses and compiles at run time.

import argparse, lzma, os, struct, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'assets', 'material_maker', 'msl')  # paint/project.c ships each platform's own

# Deployment targets of the app (base/tools/amake/exporters.c): at run time options:nil compiles with the latest language
# version of the OS, the minimum OS's here so the libraries load on every supported OS
PLATFORMS = {
    'ios': ['xcrun', '-sdk', 'iphoneos', 'metal', '-std=metal3.2', '-mios-version-min=18.0'],
    'macos': ['xcrun', '-sdk', 'macosx', 'metal', '-std=metal3.1', '-mmacosx-version-min=14.0'],
}


def compile_one(cmd, src, dst):
    tmp = dst + '.tmp'
    r = subprocess.run(cmd + ['-w', '-x', 'metal', '-o', tmp, src], capture_output=True, text=True)
    if r.returncode != 0:
        return src, r.stderr.strip().splitlines()[:3]
    os.replace(tmp, dst)
    return src, None


def groups(dump, plat):
    # The shaders of each material that no material before it had, in library order (materials.tsv, from mm_check)
    # Each: (hash, key length) of metal_msl_hash.h, the source is <hash>.metal
    seen, order = set(), {}
    for line in open(os.path.join(dump, 'materials.tsv')):
        mid, p, h, length = line.split()
        if p == plat and h not in seen:
            seen.add(h)
            order.setdefault(mid, []).append((h, int(length)))
    return list(order.values())


def compress(data):
    # xz with LZMA2 alone and a CRC32 check, what Apple's decoder takes (NSDataCompressionAlgorithmLZMA). The dictionary
    # no larger than the block: the decoder allocates its declared size.
    d = 1 << 16
    while d < len(data):
        d <<= 1
    f = {'id': lzma.FILTER_LZMA2, 'preset': 9 | lzma.PRESET_EXTREME, 'dict_size': d}
    return lzma.compress(data, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32, filters=[f])


def pack(dump, plat, out_path, jobs, block_size):
    # Consecutive materials share a block until it holds block_size bytes: larger blocks compress better, a lookup
    # decompresses a whole block (metal_msl_pack.h keeps the last few)
    lib_dir = os.path.join(dump, plat + '_lib')
    blocks, size = [], block_size
    for g in groups(dump, plat):
        g = [e for e in g if os.path.exists(os.path.join(lib_dir, e[0] + '.metallib'))]  # Failed to compile: left out
        if not g:
            continue
        if size >= block_size:
            blocks.append([])
            size = 0
        blocks[-1] += g
        size += sum(os.path.getsize(os.path.join(lib_dir, h + '.metallib')) for h, _ in g)
    raws = []
    for g in blocks:
        datas = [open(os.path.join(lib_dir, h + '.metallib'), 'rb').read() for h, _ in g]
        raws.append((g, datas))
    with ThreadPoolExecutor(jobs) as ex:
        stored = list(ex.map(lambda r: compress(b''.join(r[1])), raws))
    entries, table, offset = [], [], 16 + 16 * len(blocks)
    entries_size = 24 * sum(len(g) for g in blocks)
    offset += entries_size
    for b, ((g, datas), z) in enumerate(zip(raws, stored)):
        raw = sum(map(len, datas))
        table.append(struct.pack('<IIII', offset, len(z), raw, 1))
        at = 0
        for (h, length), d in zip(g, datas):
            entries.append((int(h, 16), b, at, len(d), length))
            at += len(d)
        offset += len(z)
    entries.sort()
    head = struct.pack('<4sIII', b'MTLP', 1, len(entries), len(blocks))
    with open(out_path + '.tmp', 'wb') as f:
        f.write(head + b''.join(table) + b''.join(struct.pack('<QIIII', *e) for e in entries) + b''.join(stored))
    os.replace(out_path + '.tmp', out_path)
    return len(entries), len(blocks), offset, sum(sum(map(len, r[1])) for r in raws)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump')
    ap.add_argument('--out', default=OUT)
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    ap.add_argument('--platforms', default='ios,macos')
    ap.add_argument('--block', type=int, default=1 << 19, help='least uncompressed bytes of a block')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for plat in a.platforms.split(','):
        src_dir = os.path.join(a.dump, plat)
        lib_dir = os.path.join(a.dump, plat + '_lib')
        os.makedirs(lib_dir, exist_ok=True)
        todo = []
        for g in groups(a.dump, plat):
            for h, _ in g:
                dst = os.path.join(lib_dir, h + '.metallib')
                if not os.path.exists(dst):
                    todo.append((os.path.join(src_dir, h + '.metal'), dst))
        print('%s: %d sources to compile' % (plat, len(todo)), flush=True)
        failed = 0
        with ThreadPoolExecutor(a.jobs) as ex:
            for i, (src, err) in enumerate(ex.map(lambda t: compile_one(PLATFORMS[plat], *t), todo)):
                if err is not None:
                    failed += 1
                    print('failed %s: %s' % (src, ' / '.join(err)), file=sys.stderr)
                if (i + 1) % 1000 == 0:
                    print('  %d/%d' % (i + 1, len(todo)), flush=True)
        out_path = os.path.join(a.out, 'msl_%s.pack' % plat)
        n, nblocks, size, raw = pack(a.dump, plat, out_path, a.jobs, a.block)
        print('%s: %d libraries in %d blocks (%d failed to compile), %s: %.1f MB (%.1f MB uncompressed)' %
              (plat, n, nblocks, failed, out_path, size / 1e6, raw / 1e6))


if __name__ == '__main__':
    main()
