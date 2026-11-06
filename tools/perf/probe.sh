#!/bin/sh
# Checks the GNU Blackfin simulator on the smallest programs, for CI's
# blackfin-count job when a kit's count hangs. Not part of any kit.
#   sh tools/perf/probe.sh <open-xy> <bfin-elf-run> [<kit elf>]
SDK=$(cd "$1" && pwd)
RUN=$2
HERE=$(cd "$(dirname "$0")" && pwd)
T=$SDK/local/toolchain/bin
W=$(mktemp -d "${TMPDIR:-/tmp}/probe.XXXXXX")
"$RUN" --help 2>&1 | grep -i -E "env|trace|insn|model|mach" | head -30
for p in exit syscfg write; do
    "$T/bfin-elf-as" -mcpu=bf707 -o "$W/$p.o" "$HERE/probe/$p.s" && "$T/bfin-elf-ld" -T "$HERE/sim.ld" -o "$W/$p.elf" "$W/$p.o" || continue
    for env in "" "--environment virtual" "--environment operating" "--environment user"; do
        out=$(perl -e 'alarm 20; exec @ARGV' "$RUN" $env "$W/$p.elf" 2>&1); rc=$?
        echo "probe $p [$env]: exit $rc, output: $(echo "$out" | head -3 | tr '\n' '|')"
    done
done
if [ -n "$3" ]; then
    echo "the kit's program, its first instructions:"
    perl -e 'alarm 20; exec @ARGV' "$RUN" --trace-insn=on "$3" 2>&1 | head -60
    echo "... and its last before 20 s:"
    perl -e 'alarm 20; exec @ARGV' "$RUN" --trace-insn=on "$3" 2>&1 | tail -40
fi
