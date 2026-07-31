#!/bin/bash
# Build the GTK4 front end.
#
# Not part of the autotools build: it links the same core objects as astir but
# with xa_draw_gtk4.o in place of the X11 backend and no Motif at all, which is
# a second binary from one object set rather than a configure option.  Keeping
# it a script means the Motif build is untouched by its existence.
#
# The core objects must already be built (./build.sh).
set -eu
cd "$(dirname "$0")/.."
SRC=src
OUT="${1:-$SRC/astir-gtk4}"

[ -f "$SRC/ui/motif/main.o" ] || { echo "run ./build.sh first" >&2; exit 1; }

# Everything under core/, and nothing else.
#
# This used to be three greps against filenames -- exclude *_gui.o, exclude a
# hand-listed set of backend and app objects -- because "core" was a naming
# convention rather than a place.  It is a directory now, so the rule is the
# directory, and a new core file joins the link by existing rather than by
# avoiding every pattern on the exclusion list.
CORE=$(find $SRC/core -name '*.o' | sort | tr '\n' ' ')

# The library set the Motif build uses, minus every X and Motif library, plus
# gtk4.  If a link error here names an X symbol, something crept back in.
LIBS=$(make -n -C $SRC -W ui/motif/main.c astir 2>/dev/null \
  | tr ' ' '\n' | grep -E '^-(l|L)' \
  | grep -vE '^-l(X[a-zA-Z0-9]*|Xm|Xt|ICE|SM)$' | tr '\n' ' ')

# The include flags the core is actually compiled with, taken from make rather
# than guessed: GraphicsMagick lives under its own prefix, and a front end that
# includes its headers has to be told where they are the same way every other
# file is.
CFLAGS_CORE=$(make -n -C $SRC -W core/aprs/db.c core/aprs/db.o 2>/dev/null \
  | tr ' ' '\n' | grep -E '^-(I|D)' | grep -v '^-DHAVE_CONFIG_H$' \
  | sort -u | tr '\n' ' ')

echo "linking $(echo $CORE | wc -w) core objects + gtk4 backend"
gcc -DHAVE_CONFIG_H -I$SRC -I. $CFLAGS_CORE -O2 -g -Wall $(pkg-config --cflags gtk4) \
    $SRC/ui/gtk4/xa_gtk4_main.c $SRC/ui/gtk4/xa_gtk4_palette.c \
    $SRC/draw/gtk4/xa_draw_gtk4.c \
    $CORE \
    -L$SRC/rtree $LIBS $(pkg-config --libs gtk4) -lm -o "$OUT"
echo "built $OUT"
