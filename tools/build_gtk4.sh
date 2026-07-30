#!/bin/bash
# Build the GTK4 front end.
#
# Not part of the autotools build: it links the same core objects as xastir but
# with xa_draw_gtk4.o in place of the X11 backend and no Motif at all, which is
# a second binary from one object set rather than a configure option.  Keeping
# it a script means the Motif build is untouched by its existence.
#
# The core objects must already be built (./build.sh).
set -eu
cd "$(dirname "$0")/.."
SRC=src
OUT="${1:-$SRC/xastir-gtk4}"

[ -f "$SRC/main.o" ] || { echo "run ./build.sh first" >&2; exit 1; }

# The same rule link_null.py uses: everything except the Motif front end and
# the X11 backend, which here means the four backend files as well.
CORE=$(cd $SRC && ls *.o \
  | grep -v '_gui\.o$' \
  | grep -vE '^(main|xa_draw_x11|rotated|color|cairo_text|xa_draw_null|xa_draw_gtk4)\.o$' \
  | grep -vE '^(xastir_udp_client|testdbfawk|callpass)\.o$' \
  | tr '\n' ' ')

# The library set the Motif build uses, minus every X and Motif library, plus
# gtk4.  If a link error here names an X symbol, something crept back in.
LIBS=$(make -n -C $SRC -W main.c xastir 2>/dev/null \
  | tr ' ' '\n' | grep -E '^-(l|L)' \
  | grep -vE '^-l(X[a-zA-Z0-9]*|Xm|Xt|ICE|SM)$' | tr '\n' ' ')

# The include flags the core is actually compiled with, taken from make rather
# than guessed: GraphicsMagick lives under its own prefix, and a front end that
# includes its headers has to be told where they are the same way every other
# file is.
CFLAGS_CORE=$(make -n -C $SRC -W db.c db.o 2>/dev/null \
  | tr ' ' '\n' | grep -E '^-(I|D)' | grep -v '^-DHAVE_CONFIG_H$' \
  | sort -u | tr '\n' ' ')

echo "linking $(echo $CORE | wc -w) core objects + gtk4 backend"
gcc -DHAVE_CONFIG_H -I$SRC -I. $CFLAGS_CORE -O2 -g -Wall $(pkg-config --cflags gtk4) \
    $SRC/gtk4/xa_gtk4_main.c $SRC/gtk4/xa_gtk4_palette.c $SRC/xa_draw_gtk4.c \
    $(for o in $CORE; do echo "$SRC/$o"; done | tr '\n' ' ') \
    -L$SRC/rtree $LIBS $(pkg-config --libs gtk4) -lm -o "$OUT"
echo "built $OUT"
