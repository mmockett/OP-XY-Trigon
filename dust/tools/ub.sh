#!/bin/sh
# DUST's undefined-behaviour and compiler check. Builds the host tests and a
# sweep of the engine and of the screen (tools/ubsweep.cpp, ubframes.cpp over the jobs of
# tools/ub_jobs.py: every preset key, each control at 0, 64 and 127, all at
# 0, all at 127, TUNE and TONE at their ends) with
#   clang++ -O2 (the reference), g++ -O2, both with -fwrapv,
#   clang++ under UBSan (-fsanitize=undefined,integer, nothing recovered),
#   clang++ under ASan,
# runs the tests under UBSan, ASan and g++, and fails unless every build's
# sweep is bit for bit the reference's. With --calibrate it also runs one
# round of tools/calibrate.py with clang++ and with g++ on copies of the
# kit and fails unless the two levels.h match.
#
#   sh tools/ub.sh [--calibrate]          (from the kit's folder or anywhere)
#
# Environment: OPEN_XY or SDK (the SDK checkout; default ../open-xy beside
# this repository), CLANG (default clang++), GXX (default g++), PY (a Python
# with numpy, for --calibrate; default python3).
#
# The integer group's unsigned checks are left out: unsigned arithmetic
# wraps by definition, and the kit's phase accumulators, noise and hashes
# rely on it. Every signed overflow, shift and conversion check stays on.
set -eu
KIT=$(cd "$(dirname "$0")/.." && pwd)
SDK=${SDK:-${OPEN_XY:-$KIT/../../open-xy}}
SDK=$(cd "$SDK" && pwd)
CLANG=${CLANG:-clang++}
GXX=${GXX:-g++}
PY=${PY:-python3}
SYSROOT=""   # Homebrew's g++ on macOS finds the C library through the SDK root
command -v xcrun > /dev/null 2>&1 && SYSROOT=$(xcrun --show-sdk-path)
[ -n "$SYSROOT" ] && export SDKROOT="$SYSROOT"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/dust-ub.XXXXXX")
INC="-I$SDK/sdk/include -I$KIT"
STD="-std=c++11 -Wall -Werror"
GSYS=""
[ -n "$SYSROOT" ] && GSYS="-isysroot $SYSROOT"
UBSAN="-O1 -g -fsanitize=undefined,integer -fno-sanitize=unsigned-integer-overflow,unsigned-shift-base -fno-sanitize-recover=all -fno-omit-frame-pointer"
ASAN="-O1 -g -fsanitize=address -fno-omit-frame-pointer"
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
export ASAN_OPTIONS=halt_on_error=1:detect_stack_use_after_return=1

echo "dust ub: $OUT"
python3 "$KIT/tools/ub_jobs.py" > "$OUT/jobs.txt"
echo "  $(wc -l < "$OUT/jobs.txt" | tr -d ' ') jobs"

build() {   # name compiler flags...
    name=$1; cxx=$2; shift 2
    $cxx $STD "$@" $INC "$KIT/tools/ubsweep.cpp" "$KIT/tables.cpp" -o "$OUT/engine-$name"
    $cxx $STD "$@" $INC "$KIT/tools/ubframes.cpp" "$KIT/tables.cpp" -o "$OUT/display-$name"
    "$OUT/engine-$name" < "$OUT/jobs.txt" > "$OUT/engine-$name.txt"
    "$OUT/display-$name" < "$OUT/jobs.txt" > "$OUT/display-$name.txt"
}
build clang "$CLANG" -O2
build clang-wrapv "$CLANG" -O2 -fwrapv
build gcc "$GXX" -O2 $GSYS
build gcc-wrapv "$GXX" -O2 -fwrapv $GSYS
build ubsan "$CLANG" $UBSAN
build asan "$CLANG" $ASAN
fail=0
for name in clang-wrapv gcc gcc-wrapv ubsan asan; do
    for part in engine display; do
        if cmp -s "$OUT/$part-clang.txt" "$OUT/$part-$name.txt"; then
            echo "  $part $name: identical to clang -O2"
        else
            echo "  FAIL $part $name differs from clang -O2:"
            diff "$OUT/$part-clang.txt" "$OUT/$part-$name.txt" | head -5
            fail=1
        fi
    done
done
grep -q " 0$" "$OUT/display-clang.txt" && { echo "  FAIL an invalid frame in the sweep"; fail=1; }

# the host tests under each build
for t in "$KIT"/tests/*.cpp; do
    n=$(basename "$t" .cpp)
    for cfg in ubsan asan gcc; do
        case $cfg in
            ubsan) "$CLANG" $STD $UBSAN $INC "$t" "$KIT/tables.cpp" -o "$OUT/test-$n-$cfg" ;;
            asan) "$CLANG" $STD $ASAN $INC "$t" "$KIT/tables.cpp" -o "$OUT/test-$n-$cfg" ;;
            gcc) $GXX $STD -O2 $GSYS $INC "$t" "$KIT/tables.cpp" -o "$OUT/test-$n-$cfg" ;;
        esac
        if "$OUT/test-$n-$cfg" > "$OUT/test-$n-$cfg.txt" 2>&1; then
            echo "  test $n under $cfg: pass"
        else
            echo "  FAIL test $n under $cfg:"
            tail -5 "$OUT/test-$n-$cfg.txt"
            fail=1
        fi
    done
done

if [ "${1:-}" = "--calibrate" ]; then
    for cfg in clang gcc; do
        mkdir -p "$OUT/cal-$cfg"
        cp -R "$KIT" "$OUT/cal-$cfg/dust"
        cxx=$CLANG
        [ $cfg = gcc ] && cxx="$GXX"
        if [ $cfg = gcc ] && [ -n "$SYSROOT" ]; then
            # g++ on macOS needs the SDK's root: a wrapper that adds it
            printf '#!/bin/sh\nexec %s -isysroot %s "$@"\n' "$GXX" "$SYSROOT" > "$OUT/gxx"
            chmod +x "$OUT/gxx"
            cxx="$OUT/gxx"
        fi
        "$PY" -I "$OUT/cal-$cfg/dust/tools/calibrate.py" --sdk "$SDK" --rounds 1 --cxx "$cxx" > "$OUT/cal-$cfg.txt"
    done
    if cmp -s "$OUT/cal-clang/dust/levels.h" "$OUT/cal-gcc/dust/levels.h"; then
        echo "  calibration: clang++ and g++ write the same levels.h"
    else
        echo "  FAIL calibration differs between clang++ and g++"
        diff "$OUT/cal-clang/dust/levels.h" "$OUT/cal-gcc/dust/levels.h" | head -10
        fail=1
    fi
fi
if [ $fail = 0 ]; then echo "dust ub: PASS"; else echo "dust ub: FAIL"; fi
exit $fail
