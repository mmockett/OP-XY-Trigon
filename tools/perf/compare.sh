#!/bin/sh
# CI's compare report for one kit (.github/workflows/perf.yml): renders one
# hit of every key of the default kit and of every preset, and of every model
# at its defaults and with every control at 0, 64 and 127, at <base ref> and
# at HEAD (g++ -O2), and writes both as WAV files with tools/perf/compare.py's
# report into <out>. Reports, never fails on a difference. Not part of any kit.
#   sh tools/perf/compare.sh <kit> <open-xy> <base ref> <out>
set -e
KIT=$1
SDK=$(cd "$2" && pwd)
BASE=$3
OUT=$4
ROOT=$(pwd)
WORK=${RUNNER_TEMP:-${TMPDIR:-/tmp}}/compare-$KIT
mkdir -p "$WORK" "$OUT"
GXX=${GXX:-g++}
for side in base head; do
    ref=$BASE; [ $side = head ] && ref=HEAD
    mkdir -p "$WORK/$side"
    git archive "$ref" "$KIT" | tar -x -C "$WORK/$side"
done
python3 -I "$ROOT/tools/perf/scenarios.py" "$WORK/base/$KIT" compare > "$WORK/jobs.txt"
for side in base head; do
    src=$WORK/$side/$KIT
    sources=""
    for s in $(python3 -I "$ROOT/tools/perf/kit.py" "$src" sources); do sources="$sources $src/$s"; done
    # shellcheck disable=SC2046
    $GXX -O2 -std=c++11 -I"$src" -I"$SDK/sdk/include" -I"$SDK/sdk/platform/opxy-1.2.1" \
        $(python3 -I "$ROOT/tools/perf/kit.py" "$src" defines) "$ROOT/tools/perf/host.cpp" $sources -o "$WORK/$side.exe"
    "$WORK/$side.exe" "$WORK/$side.bin" raw < "$WORK/jobs.txt" > "$WORK/$side.idx"
done
python3 -I "$ROOT/tools/perf/compare.py" "$KIT" "$WORK/base.idx" "$WORK/base.bin" "$WORK/head.idx" "$WORK/head.bin" "$OUT"
