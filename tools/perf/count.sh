#!/bin/sh
# CI's blackfin-count check for one kit (.github/workflows/perf.yml). Not part
# of any kit. Run from the repository's root, with full history:
#   sh tools/perf/count.sh <kit> <open-xy> <base ref> [<bfin-elf-run>]
# Without a simulator it tables the loops only. (GDB 15.2's Blackfin
# simulator hangs on taken jumps and calls in code from the SDK's pinned
# tools, see tools/perf/probe.sh, so CI runs without it for now.)
# Compiles the kit at <base ref> and at HEAD with the SDK's pinned Blackfin
# tools (sdk.py compile), tables the loops of each voice.o (tools/perf/
# loops.py), and counts the profiler's whole drum workload on each in the GNU
# Blackfin simulator (tools/perf/sim.cpp, tools/perf/simreport.py). When HEAD
# is marked "Sound: identical", HEAD is also held to its parent: no hardware
# loop may become a software loop and no run may count over 0.5% more.
set -e
KIT=$1
SDK=$(cd "$2" && pwd)
BASE=$3
RUN=${4:-}
ROOT=$(pwd)
WORK=${RUNNER_TEMP:-${TMPDIR:-/tmp}}/count-$KIT
mkdir -p "$WORK"
OBJDUMP=$SDK/local/toolchain/bin/bfin-elf-objdump
status=0

# build <commit>: prints the stem of its listing and counts
build() {
    t=$(git rev-parse "$1:$KIT")
    stem=$WORK/$t
    if [ ! -f "$stem.counts" ]; then
        mkdir -p "$WORK/src-$t"
        [ -d "$WORK/src-$t/$KIT" ] || git archive "$1" "$KIT" | tar -x -C "$WORK/src-$t"
        rm -rf "$WORK/fw-$t"
        (cd "$SDK" && python sdk.py compile --machine "$WORK/src-$t/$KIT" --out "$WORK/fw-$t" > "$WORK/fw-$t.log" 2>&1) ||
            { cat "$WORK/fw-$t.log"; exit 1; }
        "$OBJDUMP" -d -r -C "$WORK/fw-$t/$KIT/voice.o" > "$stem.lst"
        if [ -z "$RUN" ]; then : > "$stem.counts"; echo "$stem"; return 0; fi
        sh "$ROOT/tools/perf/sim.sh" "$WORK/src-$t/$KIT" "$SDK" "$stem.elf"
        if [ ! -f "$WORK/quick.done" ]; then
            SIM_DEFS=-DSIM_QUICK sh "$ROOT/tools/perf/sim.sh" "$WORK/src-$t/$KIT" "$SDK" "$WORK/quick.elf"
            start=$(date +%s)
            script -q /dev/null perl -e 'alarm 300; exec @ARGV' "$RUN" "$WORK/quick.elf" >&2 && echo "the simulator ran the quick check in $(( $(date +%s) - start )) s" >&2 ||
                { echo "the simulator failed the quick check ($?)" >&2; sh "$ROOT/tools/perf/probe.sh" "$SDK" "$RUN" "$WORK/quick.elf" >&2; exit 1; }
            touch "$WORK/quick.done"
        fi
        start=$(date +%s)
        perl -e 'alarm 2400; exec @ARGV' "$RUN" "$stem.elf" > "$stem.counts.part" ||
            { echo "the simulator stopped ($?) after $(( $(date +%s) - start )) s; what it counted:" >&2; cat "$stem.counts.part" >&2; exit 1; }
        echo "counted $KIT at $(git rev-parse --short "$1") in $(( $(date +%s) - start )) s" >&2
        mv "$stem.counts.part" "$stem.counts"
    fi
    echo "$stem"
}
b=$(build "$BASE")
h=$(build HEAD)
python3 -I "$ROOT/tools/perf/loops.py" "$KIT" "$b.lst" "$h.lst" || status=1
[ -z "$RUN" ] || python3 -I "$ROOT/tools/perf/simreport.py" "$KIT" "$b.counts" "$h.counts" --json "$WORK/counts.json" || status=1
if git log -1 --format=%B HEAD | grep -qi '^Sound: identical' &&
   [ "$(git rev-parse "HEAD^:$KIT")" != "$(git rev-parse "HEAD:$KIT")" ]; then
    p=$(build HEAD^)
    echo "HEAD is marked Sound: identical: held to its parent"
    python3 -I "$ROOT/tools/perf/loops.py" "$KIT (HEAD against its parent)" "$p.lst" "$h.lst" --strict || status=1
    [ -z "$RUN" ] || python3 -I "$ROOT/tools/perf/simreport.py" "$KIT (HEAD against its parent)" "$p.counts" "$h.counts" --strict || status=1
fi
cp "$b.counts" "$WORK/base.counts"
cp "$h.counts" "$WORK/head.counts"
exit $status
