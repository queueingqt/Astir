#!/bin/bash
# Build and run the GTK4 backend smoke test.  See tools/gtk4_smoke.c.
#
# Headless: no display, no GTK main loop.  Cairo image surfaces and Pango need
# neither, which is what makes the backend testable at all without a front end.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../src"
OUT="${1:-/tmp/xastir_gtk4_smoke}"
gcc -I"$SRC" -I"$HERE/.." -O2 -Wall $(pkg-config --cflags gtk4) \
    "$HERE/gtk4_smoke.c" "$SRC/xa_draw_gtk4.c" \
    $(pkg-config --libs gtk4) -lm -o "$OUT"
echo "built $OUT"
"$OUT"
