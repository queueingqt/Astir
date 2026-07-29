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

# Guard against a silent-failure trap that already cost a session's time:
# maps.c, map_geo.c and map_WMS.c used to be ISO-8859.  Under a UTF-8 locale
# GNU grep classifies such a file as binary and prints NOTHING while exiting 1
# -- no warning -- so a search for a symbol that is present reports nothing at
# all.  Editing them through a UTF-8 tool also silently re-encodes their
# non-ASCII bytes.  Warn loudly if one reappears.
bad_encoding=""
for f in src/*.c src/*.h; do
  iconv -f UTF-8 -t UTF-8 "$f" >/dev/null 2>&1 || bad_encoding="$bad_encoding $f"
done
if [ -n "$bad_encoding" ]; then
  echo "WARNING: not valid UTF-8:$bad_encoding" >&2
  echo "         grep will silently find nothing in these files." >&2
  echo "         Search them with LC_ALL=C, edit them on bytes, or convert." >&2
fi

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
