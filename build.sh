#!/bin/bash
# Build Xastir for performance work on the Getac V200-X.
#
#   ./build.sh          incremental make
#   ./build.sh full     bootstrap + configure + make (after configure changes)
#   ./build.sh clean    make clean, then full
#
# Notes specific to this machine:
#  * -j3 on purpose. This CPU has 4 threads; a fully saturated compile plus a
#    GUI application hung the Ironlake GPU on 2026-07-28 07:43 and required a
#    hard power-off, which then silently corrupted five object files. Leave a
#    thread free, and do not run xastir (or any GUI ham app) while building.
#  * Built binary is run straight from ./src/xastir. Nothing is installed, so
#    no root is needed and the packaged xastir stays untouched as a fallback.
set -eu

cd "$(dirname "$0")"

MODE="${1:-inc}"

# -O2 to match a realistic build; -g and frame pointers so perf can unwind.
export CFLAGS="-O2 -g -fno-omit-frame-pointer"
export CXXFLAGS="$CFLAGS"

if [ "$MODE" = "clean" ]; then
  make clean >/dev/null 2>&1 || true
  MODE=full
fi

if [ "$MODE" = "full" ] || [ ! -f Makefile ]; then
  ./bootstrap.sh
  ./configure --prefix=/usr --with-shapelib
fi

make -j3

echo
echo "built: $(pwd)/src/xastir"
ls -l src/xastir
echo
echo "run with:  XASTIR_PERF=1 ./src/xastir"
