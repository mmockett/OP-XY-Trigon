#!/bin/sh
# FELT's undefined-behaviour and compiler-agreement checks. Builds the host
# tests and a sweep of the real engine and screen (tools/sweep_harness.cpp:
# every key of every preset, each model with each control at 0, 32, 64, 96
# and 127 and SOFT at each of its steps calibrate.py measures, every control
# at 0 and at 127, TUNE at its ends) with
#   clang++ -O2, g++ -O2, both again with -fwrapv,
#   clang++ under UBSan (every undefined-behaviour check, the signed-integer
#     and shift checks of -fsanitize=integer, -fno-sanitize-recover=all), and
#   clang++ under ASan,
# runs them, and fails unless every build renders every scenario's sound and
# screen bit for bit alike and the sanitizers find nothing. (The unsigned
# checks of -fsanitize=integer are left out: a phase or a noise register
# wrapping modulo 2^32 is defined, and meant.)
#   sh tools/ub.sh [<open-xy>]     (or OPEN_XY=<open-xy>; default ../open-xy)
# CLANG and GCC choose the compilers (clang++ and g++ by default).
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
SDK=${1:-${OPEN_XY:-$HERE/../../open-xy}}
SDK=$(cd "$SDK" && pwd)
OUT=$(mktemp -d "${TMPDIR:-/tmp}/felt-ub.XXXXXX")
CLANG=${CLANG:-clang++}
GCC=${GCC:-g++}
GSYS=""   # Homebrew's g++ on macOS finds the C library through the SDK root
command -v xcrun > /dev/null 2>&1 && GSYS="-isysroot $(xcrun --show-sdk-path)"
INC="-std=c++11 -I$SDK/sdk/include -I$HERE"
UBSAN="-fsanitize=undefined,signed-integer-overflow,shift,integer-divide-by-zero,implicit-signed-integer-truncation -fno-sanitize-recover=all"
python3 -I "$HERE/tools/presets.py" --scenarios > "$OUT/scenarios.txt"
echo "$(wc -l < "$OUT/scenarios.txt" | tr -d ' ') scenarios"

build() {   # name compiler flags...
    name=$1; shift
    "$@" $INC "$HERE/tools/sweep_harness.cpp" "$HERE/tables.cpp" -o "$OUT/sweep-$name"
    "$OUT/sweep-$name" < "$OUT/scenarios.txt" > "$OUT/$name.txt"
    echo "built and ran $name"
}
build clang "$CLANG" -O2 -Wall -Werror
build gcc "$GCC" $GSYS -O2 -Wall -Werror
build clang-wrapv "$CLANG" -O2 -fwrapv
build gcc-wrapv "$GCC" $GSYS -O2 -fwrapv
build ubsan "$CLANG" -O1 -g $UBSAN
build asan "$CLANG" -O1 -g -fsanitize=address -fno-omit-frame-pointer
for name in gcc clang-wrapv gcc-wrapv ubsan asan; do
    if ! cmp -s "$OUT/clang.txt" "$OUT/$name.txt"; then
        echo "FAIL: $name renders differently from clang -O2:"
        diff "$OUT/clang.txt" "$OUT/$name.txt" | head -10
        exit 1
    fi
done
echo "every build renders every scenario's sound and screen alike"

# the host tests, under both compilers and both sanitizers
for test in "$HERE"/tests/*.cpp; do
    t=$(basename "$test" .cpp)
    for run in "clang:$CLANG -O2 -Wall -Werror" "gcc:$GCC $GSYS -O2 -Wall -Werror" "ubsan:$CLANG -O1 -g $UBSAN" \
               "asan:$CLANG -O1 -g -fsanitize=address"; do
        name=${run%%:*}; cc=${run#*:}
        $cc $INC "$test" "$HERE/tables.cpp" -o "$OUT/$t-$name"
        "$OUT/$t-$name" > "$OUT/$t-$name.log" 2>&1 || { echo "FAIL: tests/$t.cpp under $name:"; tail -20 "$OUT/$t-$name.log"; exit 1; }
    done
    cmp -s "$OUT/$t-clang.log" "$OUT/$t-gcc.log" || { echo "FAIL: tests/$t.cpp reports differently under g++"; diff "$OUT/$t-clang.log" "$OUT/$t-gcc.log" | head; exit 1; }
    echo "tests/$t.cpp passes under clang, g++, UBSan and ASan"
done
echo "PASS ($OUT)"
