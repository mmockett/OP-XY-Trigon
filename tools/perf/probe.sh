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
for p in outc exit exitlow write; do
    "$T/bfin-elf-as" -mcpu=bf707 -o "$W/$p.o" "$HERE/probe/$p.s" && "$T/bfin-elf-ld" -T "$HERE/sim.ld" -o "$W/$p.elf" "$W/$p.o" || continue
    for opt in "" "--memory-size 0x8000000" "--memory-region 0,0x8000000" "--model bf537 --memory-region 0,0x8000000" "--environment user --memory-region 0,0x8000000"; do
        out=$(perl -e 'alarm 15; exec @ARGV' "$RUN" $opt "$W/$p.elf" 2>&1); rc=$?
        echo "probe $p [$opt]: exit $rc, output: $(echo "$out" | head -3 | tr '\n' '|')"
    done
done
# crt0.s with a main that returns at once; then without its cycle counter, then without its bss clear
"$T/bfin-elf-as" -mcpu=bf707 -o "$W/main.o" "$HERE/probe/main.s"
sed -e '/SYSCFG/d' -e '/BITSET/d' -e '/CSYNC/d' "$HERE/crt0.s" > "$W/nocyc.s"
sed -e 's/^    CC = P0 < P1 (IU);/    CC = R0 < R0;/' "$HERE/crt0.s" > "$W/nobss.s"
for c in crt0 nocyc nobss; do
    src="$HERE/crt0.s"; [ $c = crt0 ] || src="$W/$c.s"
    "$T/bfin-elf-as" -mcpu=bf707 -o "$W/$c.o" "$src" && "$T/bfin-elf-ld" -T "$HERE/sim.ld" -o "$W/$c.elf" "$W/$c.o" "$W/main.o" || { echo "probe $c did not build"; continue; }
    out=$(perl -e 'alarm 15; exec @ARGV' "$RUN" "$W/$c.elf" 2>&1); rc=$?
    echo "probe $c: exit $rc, output: $(echo "$out" | head -4 | tr '\n' '|')"
done
# step by step: each adds one piece of the start-up code, prints a letter and stops
steps="sp:SP.H = 0x07ff; SP.L = 0xfff0;
fp:FP = 0;
syscfg:R0 = SYSCFG; BITSET (R0, 1); SYSCFG = R0;
csync:CSYNC;
loop:P0 = 0; P1 = 64; .Lc: CC = P0 < P1 (IU); IF !CC JUMP .Ld; P0 += 4; JUMP .Lc; .Ld: NOP;
store:P0.H = 0; P0.L = 0x3000; R0 = 0; [P0++] = R0; [P0] = R0;
call:CALL f;
spadd:SP += -12; SP += 12;"
body=""
echo "$steps" | while IFS= read -r line; do
    name=${line%%:*}; code=${line#*:}
    body="$body
    $code"
    printf '.section .text.start\n.global start\nstart:%s\n    OUTC 0x6b;\n    OUTC 0x0a;\n    HLT;\nf:\n    OUTC 0x66;\n    RTS;\n' "$(echo "$body" | sed 's/; */;\n    /g')" > "$W/step.s"
    if "$T/bfin-elf-as" -mcpu=bf707 -o "$W/step.o" "$W/step.s" 2> "$W/as.err" && "$T/bfin-elf-ld" -T "$HERE/sim.ld" -o "$W/step.elf" "$W/step.o"; then
        out=$(perl -e 'alarm 10; exec @ARGV' "$RUN" "$W/step.elf" 2>&1); rc=$?
        echo "step $name: exit $rc, output: $(echo "$out" | head -2 | tr '\n' '|')"
    else
        echo "step $name: did not assemble: $(head -2 "$W/as.err" | tr '\n' '|')"
    fi
done
echo "exit, step by step:"
perl -e 'alarm 15; exec @ARGV' "$RUN" --debug-insn "$W/exit.elf" 2>&1 | head -40
perl -e 'alarm 15; exec @ARGV' "$RUN" --trace-insn "$W/exit.elf" 2>&1 | head -40
