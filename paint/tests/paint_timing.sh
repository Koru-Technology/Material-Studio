#!/bin/sh
# Sums the paint timing log of tests/paint_timing.c per case: GPU and CPU ms of the paint work per stroke frame (60 a case).
# The script's own lines are buffered, the cases are told apart by the idle gap between them (> 300 ms).
# Usage: tests/paint_timing.sh log
LC_ALL=C awk '
/\[paint_timing\] begin/ { names[++k] = $3 }
/\[paint\] t/ {
	t = $3
	if (n == 0 || t - prev > 300) { c++ }
	prev = t
	n++
	units[c]++
	for (i = 1; i <= NF; i++) {
		if ($i == "dabs") d[c] += $(i + 1)
		if ($i == "cpu") cpu[c] += ($(i + 1) < 0 ? -$(i + 1) : $(i + 1))
		if ($i == "gpu" && $(i + 1) > 0) gpu[c] += $(i + 1)
	}
}
END {
	for (j = 1; j <= c; j++) {
		printf("%-11s units %4d dabs %5d  gpu %7.2f ms (%.3f/frame)  cpu %7.2f ms (%.3f/frame)\n", names[j], units[j], d[j], gpu[j], gpu[j] / 60, cpu[j], cpu[j] / 60)
	}
}' "$1"
