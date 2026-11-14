#!/usr/bin/env python3
# Report of a sampling profile written by base/sources/iron_profile.c (env IRON_PROFILE):
#   python3 paint/tools/profile_report.py profile.txt <binary or dSYM DWARF file> [--from MARK] [--to MARK] [--tree-min PCT]
# Addresses in the main executable are symbolicated with atos, the others carry dladdr names from the device.
# Prints, for the whole run and for each window between "... start" / "... end" marks: where the main thread was busy
# (self and inclusive time per function, a call tree) and a timeline in 250 ms slices.

import argparse
import collections
import subprocess
import sys

IDLE_LEAVES = ("mach_msg", "__psynch_cvwait", "semaphore_wait", "semaphore_timedwait", "__ulock_wait", "__select", "kevent", "__semwait_signal",
               "__workq_kernreturn")


def load(path):
    image = None
    syms = {}
    marks = []
    samples = []
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == "image":
                image = int(p[1], 16)
            elif p[0] == "sym":
                syms[int(p[1], 16)] = p[2] + "`" + (p[3] if len(p) > 3 else "?")
            elif p[0] == "mark":
                marks.append((int(p[1]), " ".join(p[2:])))
            elif p[0] == "S":
                samples.append((int(p[1]), [int(x, 16) for x in p[2:]]))
    return image, syms, marks, samples


def symbolicate(binary, image, pcs, syms):
    names = dict(syms)
    todo = sorted(pc for pc in pcs if pc not in names)
    for i in range(0, len(todo), 2000):
        chunk = todo[i:i + 2000]
        out = subprocess.run(["atos", "-arch", "arm64", "-o", binary, "-l", hex(image)] + [hex(pc) for pc in chunk],
                             capture_output=True, text=True).stdout.splitlines()
        for pc, s in zip(chunk, out):
            # "func (in MaterialStudio) (file.c:123)" -> "func (file.c)"
            fn = s.split(" (in ")[0]
            loc = s.rsplit("(", 1)[-1].rstrip(")") if s.endswith(")") and ".c" in s.rsplit("(", 1)[-1] else ""
            if fn.startswith("0x"):
                fn = "?" + fn
            names[pc] = fn + (" (" + loc.split(":")[0] + ")" if loc else "")
    return names


def kind(stack):
    """cpu, gpu (blocked on a command buffer), or idle (waiting for vsync / a drawable / events)"""
    leaf = stack[0]
    waiting = any(k in leaf for k in IDLE_LEAVES)
    if not waiting:
        return "cpu"
    # The innermost of our frames says what the wait is for (iOS runs everything inside the run loop, system names are redacted)
    ours = next((s for s in stack if "`" not in s and not s.startswith("?") and not s.startswith("<")), "")
    if "gpu_acquire_framebuffer" in ours:
        return "vsync"  # Waiting for a drawable: the display, or the GPU still busy with earlier frames
    if any(k in ours for k in ("gpu_execute_and_wait", "wait_for_frames", "gpu_present_internal")) or "_MTLCommandBuffer" in " ".join(stack[:6]):
        return "gpu"
    if any("nanosleep" in s for s in stack[:3]) or "iron_internal_handle_messages" in ours or ours in ("main", "", "-[IronSceneDelegate mainLoop]"):
        return "idle"  # Frame pacing sleep or event loop
    return "wait"  # A lock or condition: worker threads, file io


def is_idle(stack):
    return kind(stack) == "idle"


def report(title, samples, names, tree_min, ms_per_sample):
    if not samples:
        return
    stacks = [[names.get(pc, hex(pc)) for pc in s] for _, s in samples]
    busy = [s for s in stacks if not is_idle(s)]
    span = (samples[-1][0] - samples[0][0]) / 1000.0
    print("=" * 100)
    kinds = collections.Counter(kind(s) for s in stacks)
    print(f"{title}: {span:.0f} ms of wall time, {len(samples)} samples; main thread: " +
          ", ".join(f"{k} ~{kinds[k] * ms_per_sample:.0f} ms" for k in ("cpu", "gpu", "vsync", "wait", "idle")))
    if not busy:
        return
    selfc = collections.Counter(s[0] for s in busy)
    incl = collections.Counter()
    for s in busy:
        for fn in set(s):
            incl[fn] += 1
    n = len(busy)
    print("\n-- self (innermost frame), % of busy")
    for fn, c in selfc.most_common(25):
        print(f"  {100 * c / n:5.1f}%  {c * ms_per_sample:7.0f} ms  {fn}")
    print("\n-- inclusive, % of busy (our code only)")
    shown = 0
    for fn, c in incl.most_common():
        if "`" in fn or fn.startswith("?"):
            continue
        print(f"  {100 * c / n:5.1f}%  {c * ms_per_sample:7.0f} ms  {fn}")
        shown += 1
        if shown == 45:
            break

    # Top-down tree from the outermost frame
    root = {}
    for s in busy:
        node = root
        for fn in reversed(s):
            e = node.setdefault(fn, [0, {}])
            e[0] += 1
            node = e[1]

    print(f"\n-- call tree (>= {tree_min}% of busy)")

    def walk(node, depth, chain):
        for fn, (c, kids) in sorted(node.items(), key=lambda kv: -kv[1][0]):
            if 100 * c / n < tree_min:
                continue
            # Collapse single-child pass-through chains of system frames
            print(f"  {100 * c / n:5.1f}% {'  ' * depth}{fn}")
            if depth < 60:
                walk(kids, depth + 1, chain)

    walk(root, 0, [])

    print("\n-- timeline, 250 ms slices: cpu / gpu-wait / idle %, and the most specific functions of ours")
    t0 = samples[0][0]
    # Frames present in most busy samples (iron_start, sys_render, ...) say nothing
    generic = {fn for fn, c in incl.items() if c > 0.6 * n}
    slices = collections.defaultdict(list)
    for (t, _), s in zip(samples, stacks):
        slices[(t - t0) // 250000].append(s)
    for k in sorted(slices):
        ss = slices[k]
        b = [s for s in ss if not is_idle(s)]
        kc = collections.Counter(kind(s) for s in ss)
        c = collections.Counter()
        for s in b:
            ours = [fn for fn in s if "`" not in fn and not fn.startswith("?") and fn not in generic]
            for fn in set(ours[-8:]):  # The outermost specific frames: the phase rather than the leaf
                c[fn] += 1
        top = ", ".join(f"{fn.split(' (')[0]} {100 * v // max(1, len(ss))}%" for fn, v in c.most_common(4))
        m = max(1, len(ss))
        print(f"  {k * 250:6d} ms  cpu {100 * kc['cpu'] // m:3d}  gpu {100 * kc['gpu'] // m:3d}  vsync {100 * kc['vsync'] // m:3d}  "
              f"wait {100 * kc['wait'] // m:3d}  idle {100 * kc['idle'] // m:3d}  {top}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profile")
    ap.add_argument("binary")
    ap.add_argument("--from", dest="frm")
    ap.add_argument("--to")
    ap.add_argument("--tree-min", type=float, default=2.0)
    ap.add_argument("--after-ms", type=float, default=0, help="window length after a start mark without an end mark")
    a = ap.parse_args()
    image, syms, marks, samples = load(a.profile)
    if not samples:
        sys.exit("no samples")
    pcs = {pc for _, s in samples for pc in s}
    names = symbolicate(a.binary, image, pcs, syms)
    gaps = [samples[i + 1][0] - samples[i][0] for i in range(min(len(samples) - 1, 5000))]
    ms_per_sample = (sorted(gaps)[len(gaps) // 2] / 1000.0) if gaps else 1.0
    print(f"{len(samples)} samples, ~{ms_per_sample:.2f} ms apart; marks:")
    for t, m in marks:
        print(f"  {t / 1000.0:9.0f} ms  {m}")

    if a.frm or a.to:
        lo = next((t for t, m in marks if a.frm and m == a.frm), samples[0][0])
        hi = next((t for t, m in marks if a.to and m == a.to), samples[-1][0])
        report(f"{a.frm or 'start'} -> {a.to or 'end'}", [s for s in samples if lo <= s[0] <= hi], names, a.tree_min, ms_per_sample)
        return
    report("whole run", samples, names, a.tree_min, ms_per_sample)
    # Each "X start" to the next "X end" (or the next start mark), plus a settle time after it for the following frames
    starts = [(t, m[:-6]) for t, m in marks if m.endswith(" start")]
    for i, (t, name) in enumerate(starts):
        nxt = starts[i + 1][0] if i + 1 < len(starts) else samples[-1][0]
        report(f"{name} (until the next phase)", [s for s in samples if t <= s[0] < nxt], names, a.tree_min, ms_per_sample)


if __name__ == "__main__":
    main()
