#!/bin/sh
# Checks the GNU Blackfin simulator on the smallest programs, for CI's
# blackfin-count job when a kit's count hangs. Not part of any kit.
#   sh tools/perf/probe.sh <open-xy> <bfin-elf-run> [<kit elf>]
SDK=$(cd "$1" && pwd)
RUN=$2
HERE=$(cd "$(dirname "$0")" && pwd)
T=$SDK/local/toolchain/bin
W=$(mktemp -d "${TMPDIR:-/tmp}/probe.XXXXXX")
"$RUN" --help 2>&1 | grep -i -E "mem|debug|trace|profile|max" | head -30
for p in exit exitlow write; do
    "$T/bfin-elf-as" -mcpu=bf707 -o "$W/$p.o" "$HERE/probe/$p.s" && "$T/bfin-elf-ld" -T "$HERE/sim.ld" -o "$W/$p.elf" "$W/$p.o" || continue
    for opt in "" "--memory-size 0x8000000" "--memory-region 0,0x8000000" "--model bf537 --memory-region 0,0x8000000" "--environment user --memory-region 0,0x8000000"; do
        out=$(perl -e 'alarm 15; exec @ARGV' "$RUN" $opt "$W/$p.elf" 2>&1); rc=$?
        echo "probe $p [$opt]: exit $rc, output: $(echo "$out" | head -3 | tr '\n' '|')"
    done
done
echo "exit, step by step:"
perl -e 'alarm 15; exec @ARGV' "$RUN" --debug-insn "$W/exit.elf" 2>&1 | head -40
perl -e 'alarm 15; exec @ARGV' "$RUN" --trace-insn "$W/exit.elf" 2>&1 | head -40
