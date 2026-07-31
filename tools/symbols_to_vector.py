#!/usr/bin/env python3
"""Turn symbols.dat's 20x20 pixel grids into vector outlines.

The APRS symbol set has always been pixel art: symbols/symbols.dat holds 211
symbols as 20x20 grids of palette characters, and draw_symbols.c paints them
one pixel at a time into a 20x20 pixmap that is blitted to the screen 1:1.  A
station icon is 20 pixels at every zoom and on every display, and looks it.

This traces each symbol into filled polygons, so the same artwork can be drawn
at any size and at the display's real resolution.

WHAT THIS IS AND IS NOT.  The output is a faithful vector of the existing
artwork -- the outline of the pixels, not a redrawing of the subject.  Enlarged
far enough it still reads as stylised pixel art, with square corners where the
original had square pixels.  That is deliberate and it is the point: it is
mechanical, it covers all 211 symbols, and it can be checked exactly.  Hand-
drawn replacements for the common symbols can land later, one at a time,
against the same interface and the same gate.

HOW IT TRACES.  For each colour in a symbol, the cells of that colour form a
region.  Walk the boundary between "in the region" and "not in the region" as
directed unit edges -- top edges left-to-right, right edges downward, and so on
-- and those edges chain into closed loops.  Traversed that way an outer
boundary comes out clockwise and a hole comes out counter-clockwise, so the
nonzero winding rule fills the region correctly with no special handling for
holes.  Collinear runs then collapse, which is what turns a staircase of 20
unit edges into one line segment.

THE GATE.  Every traced symbol is rasterised back at 20x20 by testing whether
each pixel centre is inside the polygons, and compared against the source grid.
All 211 must match exactly, pixel for pixel, or this exits non-zero.  That is
the whole reason to trace rather than redraw: the result is checkable.

  ./tools/symbols_to_vector.py --check     trace and verify, write nothing
  ./tools/symbols_to_vector.py --write     also write the generated sources
"""
import os
import re
import sys
from collections import defaultdict

SYMBOLS_DAT = "symbols/symbols.dat"
RENDERER = "src/core/render/draw_symbols.c"
SIZE = 20

TRANSPARENT = 0xFF


def palette_from_renderer(path=RENDERER):
    """Read the character -> colour-index table out of read_symbol_from_file().

    Duplicating it here would be one table too many: symbols.dat is written in
    these characters, the renderer decides what they mean, and a copy that
    drifts would produce vectors that are exactly wrong in a way the gate could
    not see -- because the gate would compare against the same wrong copy.
    """
    with open(path, encoding="utf-8") as f:
        src = f.read()

    start = src.index("void read_symbol_from_file")
    end = src.index("// Create outline on icons", start)
    body = src[start:end]

    table = {}
    for ch, val in re.findall(r"case\('(.)'\):.*?color\s*=\s*(0x[0-9a-fA-F]+)",
                              body, re.S):
        table[ch] = int(val, 16)
    if len(table) < 15:
        raise SystemExit("only parsed %d palette entries from %s -- the switch "
                         "has changed shape" % (len(table), path))
    return table


def read_symbols(path=SYMBOLS_DAT):
    """Every symbol in the file, as (table, code, rotatable, grid-of-chars)."""
    out = []
    table = None
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()

    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("TABLE "):
            table = line[6]
        elif line.startswith("APRS "):
            code = line[5]
            # Column 19 marks a symbol drawn facing left, which the renderer
            # stores in four rotations.  A vector can rotate at draw time, so
            # this is carried through as a flag rather than as four copies.
            rotatable = len(line) >= 20 and line[19] == "l"
            description = line[20:].strip() if len(line) > 20 else ""
            grid = [row[:SIZE].ljust(SIZE, ".") for row in lines[i + 1:i + 1 + SIZE]]
            out.append((table, code, rotatable, grid, description))
            i += SIZE
        i += 1
    return out


def trace_region(cells):
    """Closed loops bounding a set of (x, y) cells, as lists of points.

    Each unit edge is emitted in the direction that keeps the region on its
    right, so outer boundaries wind clockwise and holes wind counter-clockwise
    under the nonzero rule.
    """
    edges = {}
    for (x, y) in cells:
        if (x, y - 1) not in cells:
            edges.setdefault((x, y), []).append((x + 1, y))
        if (x + 1, y) not in cells:
            edges.setdefault((x + 1, y), []).append((x + 1, y + 1))
        if (x, y + 1) not in cells:
            edges.setdefault((x + 1, y + 1), []).append((x, y + 1))
        if (x - 1, y) not in cells:
            edges.setdefault((x, y + 1), []).append((x, y))

    loops = []
    while edges:
        start = next(iter(edges))
        loop = [start]
        cur = start
        while True:
            outs = edges.get(cur)
            if not outs:
                break
            nxt = outs.pop(0)
            if not outs:
                del edges[cur]
            loop.append(nxt)
            cur = nxt
            if cur == start:
                break
        if len(loop) > 2:
            loops.append(simplify(loop))
    return loops


def simplify(loop):
    """Drop points that lie between two collinear neighbours."""
    if loop[0] == loop[-1]:
        loop = loop[:-1]
    n = len(loop)
    keep = []
    for i in range(n):
        ax, ay = loop[i - 1]
        bx, by = loop[i]
        cx, cy = loop[(i + 1) % n]
        # cross product of the two segment directions; zero means straight on
        if (bx - ax) * (cy - by) - (by - ay) * (cx - bx) != 0:
            keep.append((bx, by))
    return keep


def trace_symbol(grid, palette):
    """[(colour_index, [loop, ...]), ...] for one symbol, back to front."""
    by_colour = defaultdict(set)
    for y, row in enumerate(grid):
        for x, ch in enumerate(row):
            idx = palette.get(ch, TRANSPARENT)
            if idx != TRANSPARENT:
                by_colour[idx].add((x, y))

    shapes = []
    for idx in sorted(by_colour):
        loops = trace_region(by_colour[idx])
        if loops:
            shapes.append((idx, loops))
    return shapes


def rasterise(shapes, size=SIZE, scale=1):
    """Fill the traced polygons back onto a grid, by the nonzero winding rule.

    Sample points sit at pixel centres, which never lie on an edge of an
    integer polygon, so every test is unambiguous -- no tie to break and no
    tolerance to choose.

    `scale` supersamples, and it is not decoration.  At scale 1 this only
    proves the outline covers the right pixel CENTRES, which is weaker than it
    looks: moving one corner of a rectilinear polygon by a whole pixel turns a
    square corner into a diagonal that can leave every centre on the side it
    was already on.  Measured, not assumed -- an injected one-point defect
    changed 0 of 400 pixels at scale 1 and the gate passed it.  At scale 4 each
    source pixel is 16 samples and that same defect has nowhere to hide.
    """
    n = size * scale
    out = [[TRANSPARENT] * n for _ in range(n)]
    for idx, loops in shapes:
        for y in range(n):
            for x in range(n):
                px, py = (x + 0.5) / scale, (y + 0.5) / scale
                wind = 0
                for loop in loops:
                    npts = len(loop)
                    for i in range(npts):
                        x0, y0 = loop[i]
                        x1, y1 = loop[(i + 1) % npts]
                        if y0 <= py < y1:            # upward crossing
                            if (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0) > 0:
                                wind += 1
                        elif y1 <= py < y0:          # downward crossing
                            if (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0) < 0:
                                wind -= 1
                if wind != 0:
                    out[y][x] = idx
    return out


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "--check"
    palette = palette_from_renderer()
    symbols = read_symbols()
    print("palette: %d characters, symbols: %d" % (len(palette), len(symbols)))

    failures = []
    total_loops = total_pts = 0

    for table, code, rotatable, grid, description in symbols:
        shapes = trace_symbol(grid, palette)
        total_loops += sum(len(l) for _, l in shapes)
        total_pts += sum(len(p) for _, ls in shapes for p in ls)

        # Checked at 4x, where each source pixel is 16 samples, so a corner
        # that is off by a whole pixel cannot slip through between centres.
        scale = 4
        want = [[palette.get(grid[y // scale][x // scale], TRANSPARENT)
                 for x in range(SIZE * scale)] for y in range(SIZE * scale)]
        got = rasterise(shapes, scale=scale)
        if got != want:
            bad = sum(1 for y in range(SIZE * scale) for x in range(SIZE * scale)
                      if got[y][x] != want[y][x])
            failures.append((table, code, bad))

    print("traced %d loops, %d points (%.1f points per symbol)"
          % (total_loops, total_pts, total_pts / len(symbols)))

    if failures:
        print("\nGATE FAILED -- %d of %d symbols do not round-trip:"
              % (len(failures), len(symbols)))
        for t, c, bad in failures[:20]:
            print("   table %r symbol %r: %d pixels wrong" % (t, c, bad))
        return 1

    print("GATE PASSED -- all %d symbols rasterise back to symbols.dat exactly"
          % len(symbols))

    if mode == "--write":
        write_svgs(symbols, palette)
        write_c_table(symbols, palette)
    return 0


# The palette characters map to colour indices, and the indices map to the RGB
# the renderer's comments record.  The SVGs need real colours to be viewable in
# anything; the C table does not, because the runtime looks the index up in
# colors[] exactly as the pixmap path did.
RGB = {
    0x51: "#000000", 0x4d: "#FFFFFF", 0x43: "#CCCCCC", 0x4a: "#EE0000",
    0x48: "#00BFFF", 0x4c: "#0000CD", 0x4b: "#00CD00", 0x47: "#00008B",
    0x40: "#FFFF00", 0x50: "#454545", 0x49: "#006400", 0x4e: "#878787",
    0x41: "#CD6500", 0x4f: "#5A5A5A", 0x46: "#CD3333", 0x42: "#A020F0",
    0x45: "#FF4040", 0x44: "#CD0000", 0x52: "#32CD32",
}


# The five tables, by the names the APRS specification uses for them.
TABLE_DIR = {
    "/": "primary",       # the primary symbol table
    "\\": "alternate",    # the alternate table, the one overlays apply to
    "#": "overlay",       # the alphanumeric glyphs drawn over an alternate symbol
    "~": "extra",         # Astir's own additions
    "!": "special",       # the "no symbol yet" placeholder
}

# Readable words for the codes that have no description and are not
# alphanumeric.  A file called "code-5e.svg" tells nobody anything.
PUNCT = {
    "!": "exclamation", '"': "quote", "#": "hash", "$": "dollar",
    "%": "percent", "&": "ampersand", "'": "apostrophe", "(": "paren-open",
    ")": "paren-close", "*": "asterisk", "+": "plus", ",": "comma",
    "-": "hyphen", ".": "period", "/": "slash", ":": "colon",
    ";": "semicolon", "<": "less-than", "=": "equals", ">": "greater-than",
    "?": "question", "@": "at", "[": "bracket-open", "\\": "backslash",
    "]": "bracket-close", "^": "caret", "_": "underscore", "`": "backtick",
    "{": "brace-open", "|": "pipe", "}": "brace-close", "~": "tilde",
    " ": "space",
}


def slugify(text):
    text = text.lower()
    text = re.sub(r"\[.*?\]", " ", text)          # drop "[used here as ...]"
    text = re.sub(r"\(.*?\)", " ", text)          # and parenthetical asides
    text = re.sub(r"[^a-z0-9]+", "-", text).strip("-")
    return text[:48].strip("-")


def readable_name(table, code, description):
    """A filename a person can find, rather than the character's hex code.

    Descriptions come from symbols.dat, which carries one for 160 of the 211
    symbols.  The rest are the overlay glyphs and the extra table, where the
    code IS the meaning -- overlay 'J' is the letter J -- so the name is built
    from the character instead.
    """
    slug = slugify(description) if description else ""
    if not slug:
        if code.isdigit():
            slug = "digit-" + code
        elif code.isalpha():
            slug = ("letter-lower-" if code.islower() else "letter-") + code.lower()
        else:
            slug = PUNCT.get(code, "code-%02x" % ord(code))
    return slug


def path_data(loops):
    parts = []
    for loop in loops:
        pts = " ".join("%d %d" % (x, y) for x, y in loop)
        parts.append("M " + pts.split(" ", 2)[0] + " " + pts.split(" ", 2)[1]
                     + " L " + " ".join("%d,%d" % p for p in loop[1:]) + " Z")
    return " ".join(parts)


def write_svgs(symbols, palette, outdir="symbols/svg"):
    index, used = [], {}
    for table, code, rotatable, grid, description in symbols:
        sub = TABLE_DIR.get(table, "table-%02x" % ord(table))
        os.makedirs(os.path.join(outdir, sub), exist_ok=True)

        name = readable_name(table, code, description)
        # Two symbols can share a description.  Disambiguate with the code
        # rather than a number, so the second file is still self-explaining.
        key = (sub, name)
        if key in used:
            name = "%s-%s" % (name, PUNCT.get(code, code))
        used[key] = True

        shapes = trace_symbol(grid, palette)
        body = []
        for idx, loops in shapes:
            body.append('  <path fill="%s" fill-rule="nonzero" d="%s"/>'
                        % (RGB.get(idx, "#FF00FF"), path_data(loops)))

        # No "--" anywhere in this comment: two hyphens inside an XML comment
        # are a parse error, and naming the generator's own --check flag in it
        # made all 211 files unreadable by every SVG renderer.
        svg = ('<?xml version="1.0" encoding="UTF-8"?>\n'
               '<!--\n'
               '  %s\n'
               '  APRS table %s, symbol %s.%s\n'
               '\n'
               '  Generated by tools/symbols_to_vector.py from '
               'symbols/symbols.dat.\n'
               '  Edit the .dat and regenerate, or replace this file with '
               'hand drawn art.\n'
               '  Either way the generator\'s check mode is the gate: it '
               'rasterises every\n'
               '  outline back to 20x20 and requires it to match the .dat '
               'exactly.\n'
               '-->\n'
               '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" '
               'width="%d" height="%d">\n%s\n</svg>\n'
               % (description or name.replace("-", " "), table, code,
                  "  Rotatable." if rotatable else "",
                  SIZE, SIZE, SIZE, SIZE, "\n".join(body)))

        rel = os.path.join(sub, name + ".svg")
        with open(os.path.join(outdir, rel), "w", encoding="utf-8") as f:
            f.write(svg)
        index.append((table, code, rotatable, description, rel))

    write_index(index, outdir)
    print("wrote %d SVGs to %s/ across %d tables"
          % (len(symbols), outdir, len({r[4].split(os.sep)[0] for r in index})))


def write_index(index, outdir):
    """A table of contents, so a symbol can be found by code as well as by name.

    The filenames answer "which file is the fire truck"; this answers "which
    file is symbol /k", which is the question anyone holding an APRS packet is
    actually asking.
    """
    lines = ["# APRS symbols, as outlines", "",
             "Generated by `tools/symbols_to_vector.py` from "
             "`symbols/symbols.dat`. Do not edit by hand;", 
             "regenerate, or replace an individual SVG with hand drawn art and "
             "re-run the check.", ""]
    by_table = defaultdict(list)
    for row in index:
        by_table[row[0]].append(row)
    for table in sorted(by_table, key=lambda t: list(TABLE_DIR).index(t)
                        if t in TABLE_DIR else 99):
        rows = by_table[table]
        lines += ["## Table `%s` &mdash; %s (%d symbols)"
                  % (table, TABLE_DIR.get(table, "unknown"), len(rows)), "",
                  "| code | description | rotatable | file |",
                  "|---|---|---|---|"]
        for _, code, rot, desc, rel in rows:
            lines.append("| `%s` | %s | %s | [`%s`](%s) |"
                         % (code, desc or "&mdash;", "yes" if rot else "",
                            rel, rel.replace(os.sep, "/")))
        lines.append("")
    with open(os.path.join(outdir, "INDEX.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def write_c_table(symbols, palette, out="src/core/render/symbols_vector.c"):
    """Emit the outlines as C, so the runtime parses nothing.

    Shipping only SVG would mean an XML parser or librsvg on the startup path
    to load artwork that never changes between builds.  The SVGs stay as the
    editable source; this is what the program reads.
    """
    pts, rings, shapes_t, glyphs = [], [], [], []
    for table, code, rotatable, grid, description in symbols:
        first_shape = len(shapes_t)
        for idx, loops in trace_symbol(grid, palette):
            first_ring = len(rings)
            for loop in loops:
                rings.append((len(pts), len(loop)))
                pts.extend(loop)
            shapes_t.append((idx, first_ring, len(loops)))
        glyphs.append((table, code, rotatable, first_shape,
                       len(shapes_t) - first_shape))

    assert len(pts) < 65536 and len(rings) < 65536

    def rows(seq, per, fmt):
        out = []
        for i in range(0, len(seq), per):
            out.append("  " + " ".join(fmt % v for v in seq[i:i + per]))
        return "\n".join(out)

    src = '''/*
 * Generated by tools/symbols_to_vector.py from symbols/symbols.dat.
 * Do not edit.  Run the generator; tools/symbols_to_vector.py --check is the
 * gate and it verifies every outline rasterises back to the .dat exactly.
 *
 * Coordinates are in the symbol's own 0..20 space, so a caller scales to
 * whatever size it wants.  Colours are indices into colors[], the same ones
 * the pixmap path used.
 */
#include "core/render/symbols_vector.h"

const unsigned char astir_sym_pts[] =
{
%s
};

const astir_sym_ring astir_sym_rings[] =
{
%s
};

const astir_sym_shape astir_sym_shapes[] =
{
%s
};

const astir_sym_glyph astir_sym_glyphs[] =
{
%s
};

const int astir_sym_glyph_count = %d;
''' % (rows([c for p in pts for c in p], 20, "%d,"),
       rows(rings, 6, "{%d,%d},"),
       rows(shapes_t, 5, "{0x%02x,%d,%d},"),
       "\n".join("  {'%s','%s',%d,%d,%d},"
                 % (g[0].replace("\\", "\\\\").replace("'", "\\'"),
                    g[1].replace("\\", "\\\\").replace("'", "\\'"),
                    g[2], g[3], g[4]) for g in glyphs),
       len(glyphs))

    with open(out, "w", encoding="utf-8") as f:
        f.write(src)

    hdr = '''/*
 * The APRS symbols as outlines.  Generated; see symbols_vector.c.
 *
 * Each glyph is a list of shapes, each shape a colour plus a list of rings,
 * each ring a run of points in the symbol's 0..20 space.  Rings wind so that
 * the nonzero fill rule puts holes in the right places without the caller
 * having to know which ring is a hole.
 */
#ifndef ASTIR_SYMBOLS_VECTOR_H
#define ASTIR_SYMBOLS_VECTOR_H

typedef struct { unsigned short first_pt; unsigned char npts; } astir_sym_ring;
typedef struct { unsigned char color; unsigned short first_ring; unsigned char nrings; } astir_sym_shape;
typedef struct
{
  char table, symbol;
  unsigned char rotatable;      /* drawn facing left; may be rotated by course */
  unsigned short first_shape;
  unsigned char nshapes;
} astir_sym_glyph;

extern const unsigned char  astir_sym_pts[];
extern const astir_sym_ring astir_sym_rings[];
extern const astir_sym_shape astir_sym_shapes[];
extern const astir_sym_glyph astir_sym_glyphs[];
extern const int astir_sym_glyph_count;

#endif /* ASTIR_SYMBOLS_VECTOR_H */
'''
    with open(out[:-2] + ".h", "w", encoding="utf-8") as f:
        f.write(hdr)

    print("wrote %s: %d glyphs, %d shapes, %d rings, %d points"
          % (out, len(glyphs), len(shapes_t), len(rings), len(pts)))


if __name__ == "__main__":
    sys.exit(main())
