#!/bin/sh
# CI's render-identity check for one kit (.github/workflows/perf.yml). Not
# part of any kit. Run from the repository's root, with full history:
#   sh tools/perf/identity.sh <kit> <open-xy> <base ref> [<from>]
# Every commit in <from>..HEAD (default <base ref>..HEAD) whose message has a
# line "Sound: identical" must render the kit bit for bit as its parent does,
# built with g++ -O2 and with clang++ -O2: tools/perf/scenarios.py's jobs
# from the parent's kit (the profiler's whole workload, every key of every
# preset, the kit's own sweep and control and pad sweeps). When every commit
# in <base ref>..HEAD is so marked, HEAD must also render as <base ref> does.
# Commits that leave the kit's folder alone render as their parent by
# construction and are not rendered. Fails on any difference, naming the
# run and its first differing block.
set -e
KIT=$1
SDK=$(cd "$2" && pwd)
BASE=$3
FROM=${4:-$BASE}
ROOT=$(pwd)
WORK=${RUNNER_TEMP:-${TMPDIR:-/tmp}}/identity-$KIT
mkdir -p "$WORK"
GXX=${GXX:-g++}
CLANG=${CLANG:-clang++}
status=0

tree() { git rev-parse "$1:$KIT"; }
# render <commit> <jobs commit> <compiler name>: prints the render's stem
render() {
    t=$(tree "$1"); j=$(tree "$2")
    stem="$WORK/r-$t-$j-$3"
    if [ ! -f "$stem.idx" ]; then
        src="$WORK/src-$t"
        if [ ! -d "$src/$KIT" ]; then mkdir -p "$src"; git archive "$1" "$KIT" | tar -x -C "$src"; fi
        jsrc="$WORK/src-$j"
        if [ ! -d "$jsrc/$KIT" ]; then mkdir -p "$jsrc"; git archive "$2" "$KIT" | tar -x -C "$jsrc"; fi
        [ -f "$WORK/jobs-$j.txt" ] || python3 -I "$ROOT/tools/perf/scenarios.py" "$jsrc/$KIT" identity > "$WORK/jobs-$j.txt"
        case $3 in gcc) cxx=$GXX ;; clang) cxx=$CLANG ;; esac
        sources=""
        for s in $(python3 -I "$ROOT/tools/perf/kit.py" "$src/$KIT" sources); do sources="$sources $src/$KIT/$s"; done
        # shellcheck disable=SC2046
        $cxx -O2 -std=c++11 -I"$src/$KIT" -I"$SDK/sdk/include" -I"$SDK/sdk/platform/opxy-1.2.1" \
            $(python3 -I "$ROOT/tools/perf/kit.py" "$src/$KIT" defines) "$ROOT/tools/perf/host.cpp" $sources -o "$stem.bin.exe"
        "$stem.bin.exe" "$stem.bin" hash < "$WORK/jobs-$j.txt" > "$stem.idx.part"
        mv "$stem.idx.part" "$stem.idx"
        echo "rendered $KIT at $(git rev-parse --short "$1") with $3: $(wc -l < "$stem.idx" | tr -d ' ') runs" >&2
    fi
    echo "$stem"
}
check() {   # older newer label
    for cc in gcc clang; do
        a=$(render "$1" "$1" $cc); b=$(render "$2" "$1" $cc)
        python3 -I "$ROOT/tools/perf/identity.py" "$KIT $3 ($cc)" "$a.idx" "$a.bin" "$b.idx" "$b.bin" || status=1
    done
}
marked() { git log -1 --format=%B "$1" | grep -qi '^Sound: identical'; }

for c in $(git rev-list --reverse --first-parent "$FROM..HEAD"); do
    marked "$c" || continue
    p=$(git rev-parse "$c^")
    if [ "$(tree "$p")" = "$(tree "$c")" ]; then
        echo "$(git rev-parse --short "$c") leaves $KIT alone"
        continue
    fi
    check "$p" "$c" "$(git rev-parse --short "$c") against its parent"
done
all=1
for c in $(git rev-list --first-parent "$BASE..HEAD"); do marked "$c" || { all=0; break; }; done
if [ $all = 1 ]; then
    check "$BASE" HEAD "HEAD against $BASE"
else
    echo "$KIT: $BASE..HEAD holds sound changes; only commits marked Sound: identical are held to their parents"
fi
exit $status
