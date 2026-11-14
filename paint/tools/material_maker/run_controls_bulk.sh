#!/bin/sh
# Runs tests/material_controls_all.c in shards, each in its own Mac app, and gathers the failures.
#   tools/material_maker/run_controls_bulk.sh [shards] [out dir]     (from paint/, after a Mac build)
set -eu
N=${1:-6}
OUT=${2:-build/controls_bulk}
APP=build/dd_mac/Build/Products/Release/MaterialStudio.app/Contents/MacOS/MaterialStudio
mkdir -p "$OUT"
i=0
while [ "$i" -lt "$N" ]; do
	sed "s/^int shard  = 0;/int shard  = $i;/; s/^int shards = 1;/int shards = $N;/" tests/material_controls_all.c > "$OUT/shard$i.c"
	MM_BUFFERS_SYNC=1 "$APP" --script "$PWD/$OUT/shard$i.c" > "$OUT/shard$i.log" 2>&1 &
	i=$((i + 1))
done
wait
grep -h "^\[bulk\] shard" "$OUT"/shard*.log || true
grep -h "^\[bulk\] FAIL" "$OUT"/shard*.log | sort -k3 -n > "$OUT/failures.txt" || true
echo "$(wc -l < "$OUT/failures.txt") failing materials in $OUT/failures.txt"
for f in "$OUT"/shard*.log; do
	grep -q "BULK PASS\|BULK FAIL" "$f" || echo "$f did not finish"
done
