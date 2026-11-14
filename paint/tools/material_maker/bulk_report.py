#!/usr/bin/env python3
# Summarizes the logs of run_controls_bulk.py: materials too heavy for the device (hung, crashed, GPU errors, slow), the
# ones that look different at their defaults, the controls that change nothing, the slowest buffers.
#
# bulk_report.py <out dir of run_controls_bulk.py>

import glob, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    out = sys.argv[1]
    names = {k: v['name'] for k, v in json.load(open(os.path.join(HERE, 'curation.json'))).items()}
    last = {}  # id -> last result line (a retry replaces the earlier result)
    for path in sorted(glob.glob(os.path.join(out, 'shard*.log'))):
        for line in open(path, 'rb').read().decode(errors='replace').splitlines():
            m = re.match(r'\[bulk\] (ok  |FAIL) (\d+)\b(.*)$', line)
            if m:
                last[m.group(2)] = (m.group(1).strip(), m.group(3))
    heavy, defaults, inert, slow = [], [], [], []
    for mid, (status, rest) in sorted(last.items(), key=lambda kv: int(kv[0])):
        name = names.get(mid, '?')
        if 'hung' in rest or 'crashed' in rest or 'heavy:' in rest:
            heavy.append('%s %s:%s' % (mid, name, rest.split(':', 1)[1] if ':' in rest else rest))
            continue
        b = re.search(r'buffers (-?[0-9.]+) s', rest)
        if b and (float(b.group(1)) < 0 or float(b.group(1)) > 10):
            slow.append('%s %s: buffers %s s' % (mid, name, b.group(1)))
        if 'looks different at the defaults' in rest:
            defaults.append('%s %s' % (mid, name))
        for r in re.findall(r'([^;:]+) changes nothing', rest):
            inert.append('%s %s: %s' % (mid, name, r.strip()))
    print('%d materials tested, %d passed' % (len(last), sum(1 for s, _ in last.values() if s == 'ok')))
    for title, rows in (('Too heavy', heavy), ('Slow buffers (over 10 s at full size)', slow),
                        ('Look different at their defaults', defaults), ('Controls that change nothing', inert)):
        print('\n%s (%d)' % (title, len(rows)))
        for r in rows:
            print('  ' + r)


if __name__ == '__main__':
    main()
