#!/bin/bash
# export_references.sh <Material Maker.app> <cache dir> <out dir> [size] [ids file] [timeout seconds] [workers]
# Exports Material Maker's own maps (Godot 4 ORM target: albedo, orm, emission, normal, heightmap, sss) for every cached
# community material, as references for checking our .ptex rendering.
# One Material Maker run per material with a time limit: some materials hang Material Maker. Several workers run at once,
# each takes every n-th material. Each material's outcome goes to <out>/tried.txt (id, ok / nomaps / timeout), materials
# tried already are skipped when it runs again. Progress in <out>/progress.txt, logs in <out>/logs.
APP="$1"
CACHE="$2"
OUT="$3"
SIZE="${4:-512}"
IDS="${5:-}"
TIMEOUT="${6:-30}"
WORKERS="${7:-4}"
BIN="$(find "$APP/Contents/MacOS" -maxdepth 1 -type f -perm +111 | head -1)"
mkdir -p "$OUT/in" "$OUT/maps" "$OUT/logs"
touch "$OUT/tried.txt"
python3 - "$CACHE" "$OUT/in" "$IDS" <<'EOF'
import json, os, sys
cache, out, ids = sys.argv[1], sys.argv[2], sys.argv[3]
files = os.listdir(os.path.join(cache, 'ptex'))
if ids:
    keep = set(l.strip() + '.json' for l in open(ids) if l.strip())
    files = [f for f in files if f in keep]
for f in files:
    p = os.path.join(out, f.replace('.json', '.ptex'))
    if not os.path.exists(p):
        open(p, 'w').write(json.load(open(os.path.join(cache, 'ptex', f)))['json'])
EOF
# Materials still to export
ls "$OUT/in" | sort -n > "$OUT/list.txt"
TOTAL=$(wc -l < "$OUT/list.txt" | tr -d ' ')
: > "$OUT/todo.txt"
while read -r f; do
	id="${f%.ptex}"
	if ! grep -q "^$id " "$OUT/tried.txt" && [ ! -f "$OUT/maps/${id}_albedo.png" ]; then
		echo "$f" >> "$OUT/todo.txt"
	fi
done < "$OUT/list.txt"
TODO=$(wc -l < "$OUT/todo.txt" | tr -d ' ')
TRIED0=$(wc -l < "$OUT/tried.txt" | tr -d ' ')
START=$(date +%s)

progress() { # One line in $OUT/progress.txt, see ref_progress.sh
	local maps run now eta
	maps=$(ls "$OUT/maps" | grep -oE '^[0-9]+' | sort -u | wc -l | tr -d ' ')
	run=$(( $(wc -l < "$OUT/tried.txt" | tr -d ' ') - TRIED0 ))
	now=$(date +%s)
	eta="?"
	if [ "$run" -gt 0 ]; then
		eta=$(( (now - START) * (TODO - run) / run / 60 ))
	fi
	echo "$(date '+%H:%M:%S') $run/$TODO exported this run ($WORKERS workers), $maps/$TOTAL materials with maps, $(grep -c ' timeout' "$OUT/tried.txt") timed out, about $eta min left" > "$OUT/progress.tmp.$1"
	mv "$OUT/progress.tmp.$1" "$OUT/progress.txt"
}

worker() { # worker <k>: every WORKERS-th material from the k-th
	local k=$1 i=0 f id code
	while read -r f; do
		if [ $((i % WORKERS)) -eq "$k" ]; then
			id="${f%.ptex}"
			# -ApplePersistenceIgnoreState: Material Maker runs killed by the time limit count as crashes, macOS then asks
			# whether to reopen its windows in a modal alert at every launch, which blocks the export
			perl -e "alarm $TIMEOUT; exec @ARGV" "$BIN" -ApplePersistenceIgnoreState YES --export-material -t "Godot/Godot 4 ORM" -o "$OUT/maps" --output-file "%f" --size "$SIZE" "$OUT/in/$f" > "$OUT/logs/$id.log" 2>&1
			code=$?
			if [ $code -eq 142 ]; then # SIGALRM
				pkill -f "Material Maker.*$OUT/in/$f" 2>/dev/null
				echo "$id timeout" >> "$OUT/tried.txt"
			elif ls "$OUT/maps" | grep -q "^${id}_"; then
				echo "$id ok" >> "$OUT/tried.txt"
			else
				echo "$id nomaps" >> "$OUT/tried.txt"
			fi
			progress "$k"
		fi
		i=$((i + 1))
	done < "$OUT/todo.txt"
}

progress 0
for ((k = 0; k < WORKERS; k++)); do
	worker "$k" &
done
wait
echo "$(date '+%H:%M:%S') done: $(ls "$OUT/maps" | grep -oE '^[0-9]+' | sort -u | wc -l | tr -d ' ')/$TOTAL materials with maps, $(grep -c ' timeout' "$OUT/tried.txt") timed out" > "$OUT/progress.txt"
