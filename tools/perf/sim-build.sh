#!/bin/sh
# Builds the GNU Blackfin simulator (bfin-elf-run) from a GDB release for CI's
# blackfin-count job. Not part of any kit.
#   sh tools/perf/sim-build.sh <install prefix>
set -e
PREFIX=$1
VERSION=15.2
WORK=${RUNNER_TEMP:-${TMPDIR:-/tmp}}/gdb-sim
mkdir -p "$WORK" "$PREFIX"
cd "$WORK"
curl -sSfL -o gdb.tar.xz "https://ftp.gnu.org/gnu/gdb/gdb-$VERSION.tar.xz"
shasum -a 256 gdb.tar.xz
tar -xJf gdb.tar.xz
mkdir -p build
cd build
"../gdb-$VERSION/configure" --target=bfin-elf --prefix="$PREFIX" --disable-gdb --disable-gdbserver \
    --disable-binutils --disable-ld --disable-gas --disable-gprof --disable-nls --disable-werror \
    --disable-readline --disable-libdecnumber --without-zstd --with-system-zlib > configure.log 2>&1 ||
    { tail -40 configure.log; exit 1; }
make -j4 all-sim > make.log 2>&1 || { grep -n -E "Error|error:|No rule" make.log | head -30; tail -30 make.log; exit 1; }
mkdir -p "$PREFIX/bin"
cp sim/bfin/run "$PREFIX/bin/bfin-elf-run"
"$PREFIX/bin/bfin-elf-run" --version | head -1
