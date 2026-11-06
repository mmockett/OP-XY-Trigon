#!/bin/sh
# Builds tools/perf/sim.cpp for the GNU Blackfin simulator against a kit, with
# the SDK's pinned Blackfin tools and its firmware flags. Not part of any kit.
#   sh tools/perf/sim.sh <kit folder> <open-xy> <out.elf>
# Building runs only the cross tools; CI runs the result in bfin-elf-run.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
KIT=$(cd "$1" && pwd)
SDK=$(cd "$2" && pwd)
OUT=$3
TOOLS=$SDK/local/toolchain/bin
WORK=$(mktemp -d "${TMPDIR:-/tmp}/perf-sim.XXXXXX")
FLAGS="-mcpu=bf707 -O2 -std=gnu++11 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
 -fno-use-cxa-atexit -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common
 -fno-tree-loop-distribute-patterns -fomit-frame-pointer -nostdinc++ -Wall -Werror -Wno-unused-variable"
INC="-I$SDK -I$SDK/sdk/include -I$SDK/sdk/platform -I$SDK/sdk/platform/opxy-1.2.1 -I$KIT -I$SDK/sdk/platform/blackfin/freestanding"
DEFS=$(python3 -I "$HERE/kit.py" "$KIT" defines)
objects=""
cc() {   # source object
    "$TOOLS/bfin-elf-g++" $FLAGS $INC $DEFS -S "$1" -o "$WORK/$2.s"
    "$TOOLS/bfin-elf-as" -mcpu=bf707 -o "$WORK/$2.o" "$WORK/$2.s"
    objects="$objects $WORK/$2.o"
}
cc "$HERE/sim.cpp" sim
for s in $(python3 -I "$HERE/kit.py" "$KIT" sources); do cc "$KIT/$s" "$(basename "$s" .cpp)"; done
cc "$SDK/sdk/platform/blackfin/memory.cpp" memory
"$TOOLS/bfin-elf-as" -mcpu=bf707 -o "$WORK/divide.o" "$SDK/sdk/platform/blackfin/divide.s"
"$TOOLS/bfin-elf-as" -mcpu=bf707 -o "$WORK/crt0.o" "$HERE/crt0.s"
"$TOOLS/bfin-elf-ld" -T "$HERE/sim.ld" -o "$OUT" "$WORK/crt0.o" $objects "$WORK/divide.o"
cp "$WORK/sim.s" "${OUT%.elf}.s"
rm -rf "$WORK"
