#!/usr/bin/env python3
# Runs tests/material_controls_all.c shard after shard on the iPad (or in parallel Mac apps), resumes after a crash at the
# material after the one that crashed, and gathers the failures.
#
# run_controls_bulk.py [--shards 8] [--only 3,4] [--out build/controls_bulk] [--device <coredevice id> --bundle <id>]
#   without --device: Mac apps, all shards at once (after a Mac build)

import argparse, os, re, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

HANG_S = 10  # A frame frozen this long (the test beats twice a second) or a GPU error: too heavy, the app is stopped
START_S = 90  # For the app to start and the test to print its first line
HERE = os.path.dirname(os.path.abspath(__file__))
PAINT = os.path.normpath(os.path.join(HERE, '..', '..'))
TEST = os.path.join(PAINT, 'tests', 'material_controls_all.c')
MAC_APP = os.path.join(PAINT, 'build/dd_mac/Build/Products/Release/MaterialStudio.app/Contents/MacOS/MaterialStudio')


def script(shard, shards, first):
    s = open(TEST).read()
    s = s.replace('int shard  = 0;', 'int shard  = %d;' % shard, 1)
    s = s.replace('int shards = 1;', 'int shards = %d;' % shards, 1)
    return s.replace('int first  = 0;', 'int first  = %d;' % first, 1)


def stop_ipad_app(a):
    out = subprocess.run(['xcrun', 'devicectl', 'device', 'info', 'processes', '--device', a.device], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if 'MaterialStudio.app/MaterialStudio' in line:
            subprocess.run(['xcrun', 'devicectl', 'device', 'process', 'terminate', '--device', a.device, '--pid', line.split()[0]],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# Runs the app on the script, echoes its [bulk] results, stops it when it hangs. True when it hung.
def run_once(a, shard, first, log):
    name = 'bulk%d_%d.c' % (shard, first)
    path = os.path.join(a.out, name)
    open(path, 'w').write(script(shard, a.shards, first))
    start = os.path.getsize(log)
    with open(log, 'a') as f:
        if a.device:
            subprocess.run(['xcrun', 'devicectl', 'device', 'copy', 'to', '--device', a.device, '--domain-type', 'appDataContainer',
                            '--domain-identifier', a.bundle, '--source', path, '--destination', 'Documents/' + name],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            cmd = ['xcrun', 'devicectl', 'device', 'process', 'launch', '--console', '--terminate-existing', '--device', a.device,
                   a.bundle, '--script', '~/Documents/' + name]
        else:
            cmd = [MAC_APP, '--script', path]
        proc = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT)
    seen, last, hung, running = start, time.time(), False, False
    while proc.poll() is None:
        time.sleep(0.25)
        size = os.path.getsize(log)
        if size > seen:
            new = open(log, 'rb').read()[seen:]
            new = new[:new.rfind(b'\n') + 1]  # Whole lines only, the rest next time
            seen, last = seen + len(new), time.time()
            new = new.decode(errors='replace')
            running = running or '[bulk]' in new
            for line in new.splitlines():
                if line.startswith('[bulk] ok') or line.startswith('[bulk] FAIL') or line.startswith('[bulk] shard'):
                    print('shard %d: %s' % (shard, line[7:]), flush=True)
            if 'GPU error' in new:
                hung = True
        if time.time() - last > (HANG_S if running else START_S):
            hung = True
        if hung:
            proc.kill()
            if a.device:
                stop_ipad_app(a)
            break
    return hung


def run_shard(a, shard):
    log = os.path.join(a.out, 'shard%d.log' % shard)
    open(log, 'w').close()
    first = 0
    while True:
        hung = run_once(a, shard, first, log)
        text = open(log, errors='replace').read()
        if 'BULK PASS' in text[-2000:] or 'BULK FAIL' in text[-2000:]:
            break
        begins = re.findall(r'\[bulk\] begin (\d+) (\d+)', text)
        if not begins or int(begins[-1][0]) + 1 <= first:
            print('shard %d stopped without progress' % shard, flush=True)
            break
        index, mid = begins[-1]
        what = 'hung (GPU error or frozen for %d s)' % HANG_S if hung else 'crashed the app'
        with open(log, 'a') as f:
            f.write('\n[bulk] FAIL %s: %s\n' % (mid, what))
        print('shard %d: FAIL %s %s, resuming after it' % (shard, mid, what), flush=True)
        first = int(index) + 1
    done = re.findall(r'\[bulk\] shard .*', open(log, errors='replace').read())
    print('shard %d done %s %s' % (shard, time.strftime('%H:%M:%S'), done[-1] if done else ''), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--shards', type=int, default=8)
    ap.add_argument('--only')
    ap.add_argument('--out', default=os.path.join(PAINT, 'build', 'controls_bulk'))
    ap.add_argument('--device')
    ap.add_argument('--bundle', default='technology.koru.materialstudio')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    shards = [int(x) for x in a.only.split(',')] if a.only else list(range(a.shards))
    if a.device:
        for s in shards:
            run_shard(a, s)
    else:
        with ThreadPoolExecutor(len(shards)) as ex:
            list(ex.map(lambda s: run_shard(a, s), shards))
    fails = []
    for s in shards:
        log = os.path.join(a.out, 'shard%d.log' % s)
        fails += [l.strip() for l in open(log, errors='replace') if l.startswith('[bulk] FAIL')]
    open(os.path.join(a.out, 'failures.txt'), 'w').write('\n'.join(fails) + '\n')
    heavy = [l for l in fails if 'hung' in l or 'crashed' in l or 'heavy:' in l]  # Too heavy for the device: skipped
    open(os.path.join(a.out, 'heavy.txt'), 'w').write('\n'.join(heavy) + '\n')
    print('%d too heavy (hung or crashed), %s' % (len(heavy), os.path.join(a.out, 'heavy.txt')))
    print('%d failing materials, %s' % (len(fails), os.path.join(a.out, 'failures.txt')))


if __name__ == '__main__':
    main()
