#!/bin/sh
# FLICKER's checks against undefined behaviour and compiler differences. The
# OP-XY's GCC wraps a signed overflow or a shift of a negative value however
# it likes, and Apple clang -O2 can hide one, so the host must never meet one:
#
#   1. the host tests (tests/*.cpp) built with clang under UBSan (every
#      undefined check, the integer checks, no recovery) and ASan, and with
#      g++ -O2;
#   2. the engine and the screen played through a sweep of scenarios
#      (tools/ub_scenarios.py: every preset key, every control at 0, 64 and
#      127, every control at 0 and at 127, the pad controls' ends) built with
#      clang -O2, clang -O2 -fwrapv, g++ -O2, g++ -O2 -fwrapv, UBSan and
#      ASan: every build must hash every sample and every frame alike.
#
# The integer checks leave out unsigned wrap-around and unsigned shifts that
# drop bits: phases, hashes and noise are meant to wrap. Not part of the
# machine.
#
#   sh tools/ub.sh [<open-xy SDK>]   (or OPEN_XY=<open-xy>; default ../open-xy)
#
# CLANG, GCC and PY choose the tools (clang++, g++ and python3 by default).
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
SDK=${1:-${OPEN_XY:-$HERE/../../open-xy}}
SDK=$(cd "$SDK" && pwd)
PY=${PY:-python3}
CLANG=${CLANG:-clang++}
GCC=${GCC:-g++}
GSYS=""   # Homebrew's g++ on macOS finds the C library through the SDK root
command -v xcrun > /dev/null 2>&1 && GSYS="-isysroot $(xcrun --show-sdk-path)"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/flicker-ub.XXXXXX")
INC="-std=c++11 -I$SDK/sdk/include -I$HERE"
UB="-O2 -g -fno-omit-frame-pointer -fsanitize=undefined,integer -fno-sanitize=unsigned-integer-overflow,unsigned-shift-base -fno-sanitize-recover=all"
AS="-O1 -g -fno-omit-frame-pointer -fsanitize=address -fno-sanitize-recover=all"
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
fail=0

echo "== host tests"
for t in "$HERE"/tests/*.cpp; do
  n=$(basename "$t" .cpp)
  for cfg in ubsan asan gcc; do
    case $cfg in
      ubsan) "$CLANG" $INC -Wall -Werror $UB "$t" -o "$OUT/$n-$cfg" ;;
      asan) "$CLANG" $INC -Wall -Werror $AS "$t" -o "$OUT/$n-$cfg" ;;
      gcc) "$GCC" $GSYS $INC -Wall -Werror -O2 "$t" -o "$OUT/$n-$cfg" ;;
    esac
    if "$OUT/$n-$cfg" > "$OUT/$n-$cfg.log" 2>&1; then echo "   $n ($cfg): pass"; else echo "   $n ($cfg): FAIL"; tail -20 "$OUT/$n-$cfg.log"; fail=1; fi
  done
done

echo "== the sweep, bit for bit"
"$PY" -I "$HERE/tools/ub_scenarios.py" > "$OUT/scenarios.txt"
echo "   $(wc -l < "$OUT/scenarios.txt" | tr -d ' ') scenarios"
for part in audio display; do
  for cfg in clang clang-wrapv gcc gcc-wrapv ubsan asan; do
    case $cfg in
      clang) "$CLANG" $INC -O2 "$HERE/tools/ub_$part.cpp" -o "$OUT/$part-$cfg" ;;
      clang-wrapv) "$CLANG" $INC -O2 -fwrapv "$HERE/tools/ub_$part.cpp" -o "$OUT/$part-$cfg" ;;
      gcc) "$GCC" $GSYS $INC -O2 "$HERE/tools/ub_$part.cpp" -o "$OUT/$part-$cfg" ;;
      gcc-wrapv) "$GCC" $GSYS $INC -O2 -fwrapv "$HERE/tools/ub_$part.cpp" -o "$OUT/$part-$cfg" ;;
      ubsan) "$CLANG" $INC $UB "$HERE/tools/ub_$part.cpp" -o "$OUT/$part-$cfg" ;;
      asan) "$CLANG" $INC $AS "$HERE/tools/ub_$part.cpp" -o "$OUT/$part-$cfg" ;;
    esac
    if ! "$OUT/$part-$cfg" < "$OUT/scenarios.txt" > "$OUT/$part-$cfg.txt" 2> "$OUT/$part-$cfg.err"; then
      echo "   $part ($cfg): FAIL"; tail -20 "$OUT/$part-$cfg.err"; fail=1; continue
    fi
    if [ -s "$OUT/$part-$cfg.err" ]; then echo "   $part ($cfg): FAIL, it reports"; head -20 "$OUT/$part-$cfg.err"; fail=1; fi
    if [ "$cfg" != clang ] && ! cmp -s "$OUT/$part-clang.txt" "$OUT/$part-$cfg.txt"; then
      echo "   $part ($cfg): FAIL, differs from clang -O2 in $(diff "$OUT/$part-clang.txt" "$OUT/$part-$cfg.txt" | grep -c '^>') scenarios"; fail=1
    else
      echo "   $part ($cfg): identical"
    fi
  done
done
if grep -q " 0$" "$OUT/display-clang.txt"; then echo "   display: FAIL, an invalid frame"; fail=1; fi

if [ $fail = 0 ]; then echo "ub.sh: pass"; rm -rf "$OUT"; else echo "ub.sh: FAIL (logs in $OUT)"; exit 1; fi
