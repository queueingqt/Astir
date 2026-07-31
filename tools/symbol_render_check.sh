#!/bin/bash
# Build and run the symbol render check.  See symbol_render_check.c.
set -eu
cd "$(dirname "$0")/.."
gcc -DHAVE_CONFIG_H -Isrc -I. -O2 -Wall $(pkg-config --cflags gtk4) \
    tools/symbol_render_check.c \
    src/core/render/symbol_draw_vector.c src/core/render/symbols_vector.c \
    src/draw/gtk4/xa_draw_gtk4.c \
    $(pkg-config --libs gtk4) -lm -o /tmp/symbol_render_check
exec /tmp/symbol_render_check "$@"
