#!/bin/bash
# Builds mm_check (macOS). Usage: build_check.sh <out binary> <glslang/SPIRV-Cross object dir>
# The object dir holds glslang and SPIRV-Cross compiled with SPIRV_CROSS_C_API_MSL=1 (any build of the libs the app uses).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../../.."
OUT="$1"
OBJ="$2"
LIBS="$ROOT/base/sources/libs"
INC="-I$LIBS/glslang -I$LIBS/spirv_cross -I$LIBS"
MM="$ROOT/paint/sources/material_maker"
clang -std=c11 -O1 -g -DMM_STANDALONE -DWITH_GLSL $INC -I$ROOT/base/sources \
  "$HERE/mm_check.c" "$ROOT/base/sources/iron_glsl.c" -x objective-c -fobjc-arc "$HERE/mm_render.m" -x none \
  "$MM/mm_core.c" "$MM/mm_gen.c" "$MM/mm_graph.c" "$MM/mm_plan.c" "$MM/mm_types_extra.c" "$MM/mm_sdf.c" "$MM/mm_texture.c" "$MM/mm_embed.c" \
  $(ls "$OBJ"/*.o | grep -v glsl2msl.o) -lc++ -framework Metal -framework Foundation -o "$OUT"
echo "built $OUT"
