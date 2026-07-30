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

# The core headers must stay linkable by a front end that is not Motif, which
# means they must compile standalone and pull in no X11/Xt/Motif header.  That
# property is easy to lose by adding one convenient #include, and nothing else
# would report it, so check it on every build.
core_hdr_fail=""
#
# xastir.h and interface.h are in this list because they are included by core
# files and used to name Widget/Pixmap/GC.  xastir.h in particular opened with
# #include <X11/Intrinsic.h>, which put Xt in front of the shapefile reader and
# the APRS parser.  One convenient #include puts it back, and nothing else would
# report it, so it is checked here.
for h in xa_state.h xa_settings.h xa_draw.h globals.h xastir.h interface.h; do
  [ -f "src/$h" ] || continue
  probe="$(mktemp -t xacore.XXXXXX.c)"
  printf '#include "%s"\nint main(void){return 0;}\n' "$h" > "$probe"
  if ! gcc -Isrc -H -fsyntax-only "$probe" 2>"$probe.err"; then
    core_hdr_fail="$core_hdr_fail $h(does-not-compile-standalone)"
  elif grep -qiE 'X11/|Xm/|Intrinsic' "$probe.err"; then
    core_hdr_fail="$core_hdr_fail $h(pulls-in-X)"
  fi
  rm -f "$probe" "$probe.err"
done
if [ -n "$core_hdr_fail" ]; then
  echo "WARNING: core headers are no longer front-end neutral:$core_hdr_fail" >&2
  echo "         a GTK4 front end must be able to include these without X11." >&2
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
