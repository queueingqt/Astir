#!/usr/bin/env python3
"""Count Xlib call sites in src/*.c, ignoring comments and strings.

The plan's original audit was a plain grep, which counted mentions inside
comments and silently skipped three ISO-8859 files entirely.  Strip C comments
and string/char literals first so the number reflects code that must actually
be converted.

Two numbers, not one.  DRAWING is the Stage 2 scope -- the primitives xa_draw.h
replaced -- and counting only those answers "how much of what we set out to
convert is done".  It does NOT answer "how much Xlib is left", because fonts,
colours, images, regions and cursors were never on that list.  Reporting only
the first number made the tree look closer to toolkit-independent than it is,
so the rest is reported too, and separately.

Deciding what counts as Xlib is done against the real headers rather than a
hand-kept list.  An X-prefixed name is not proof: XTIFFClose is libtiff,
XRotDrawAlignedString is rotated.c's own API, XA_CHECK is a local macro in
xa_draw_x11.c, and XTPOINTER_TO_INT is a macro too.  All four would inflate the
count.  A name counts only if <X11/Xlib.h> or <X11/Xutil.h> declares it.
"""
import re, sys, glob, os, collections

# The Stage 2 conversion scope: primitives that now go through xa_draw.h.
DRAWING = [
    "XSetForeground", "XSetBackground", "XCopyArea", "XSetLineAttributes",
    "XCreatePixmap", "XDrawLine", "XDrawLines", "XSetFillStyle", "XFillPolygon",
    "XFreePixmap", "XFillRectangle", "XDrawString", "XDrawRectangle",
    "XCreateGC", "XFreeGC", "XSetClipMask", "XSetClipRectangles", "XSetStipple",
    "XSetTSOrigin", "XSetFunction", "XSetDashes", "XDrawArc", "XFillArc",
    "XDrawPoint", "XDrawSegments", "XCreateBitmapFromData", "XPutImage",
    "XGetImage", "XSetFont", "XDrawImageString", "XDrawPoints",
    "XFillRectangles", "XSetLineWidth", "XSetPlaneMask", "XSetSubwindowMode",
]

# Kept for backward compatibility: earlier commits and scripts import FNS.
FNS = DRAWING

XLIB_HEADERS = ["/usr/include/X11/Xlib.h", "/usr/include/X11/Xutil.h"]

# Objects a new front end replaces outright; their Xlib use is not the core's
# problem.  Same list as core_boundary.py, in .c form.
GUI_FILES = {
    "main.c", "db_gui.c", "interface_gui.c", "list_gui.c", "locate_gui.c",
    "location_gui.c", "popup_gui.c", "objects_gui.c", "bulletin_gui.c",
    "geocoder_gui.c", "messages_gui.c", "track_gui.c", "view_message_gui.c",
    "cad_objects_gui.c",
    "wx_gui.c", "xa_config_gui.c", "map_gui.c",
}


def strip(src):
    """Remove comments and string/char literal contents."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i+1] == '*':
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            out.append(' ')
        elif c == '/' and i + 1 < n and src[i+1] == '/':
            j = src.find('\n', i)
            i = n if j < 0 else j
            out.append(' ')
        elif c in '"\'':
            q, j = c, i + 1
            while j < n:
                if src[j] == '\\':
                    j += 2
                    continue
                if src[j] == q:
                    j += 1
                    break
                j += 1
            i = j
            out.append('""')
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def xlib_names():
    """Every function <X11/Xlib.h> and <X11/Xutil.h> declare.

    Returns None if the headers are not installed, so the caller can say the
    second number is unavailable rather than silently report zero.
    """
    names, found_any = set(), False
    for h in XLIB_HEADERS:
        try:
            with open(h, "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        found_any = True
        names.update(re.findall(r'\b(X[A-Za-z0-9]+)\s*\(', text))
    return names if found_any else None


def scan(srcdir):
    drawing_pat = re.compile(r'\b(' + '|'.join(DRAWING) + r')\s*\(')
    any_x_pat = re.compile(r'\b(X[A-Za-z0-9_]+)\s*\(')
    xlib = xlib_names()

    draw_file, draw_fn = collections.Counter(), collections.Counter()
    other_fn = collections.Counter()
    other_where = collections.defaultdict(collections.Counter)

    for path in sorted(glob.glob(os.path.join(srcdir, "*.c"))):
        base = os.path.basename(path)
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            code = strip(f.read())

        hits = drawing_pat.findall(code)
        if hits:
            draw_file[base] = len(hits)
            draw_fn.update(hits)

        if xlib is None:
            continue
        for fn in any_x_pat.findall(code):
            if fn in DRAWING or fn not in xlib:
                continue
            other_fn[fn] += 1
            other_where[fn][base] += 1

    return draw_file, draw_fn, other_fn, other_where, xlib


def main():
    srcdir = sys.argv[1] if len(sys.argv) > 1 else "."
    draw_file, draw_fn, other_fn, other_where, xlib = scan(srcdir)

    print("=== drawing primitives: the Stage 2 scope ===")
    print("--- per file ---")
    for f, n in sorted(draw_file.items(), key=lambda kv: -kv[1]):
        print("  %-22s %4d" % (f, n))
    print("--- per primitive ---")
    for fn, n in draw_fn.most_common():
        print("  %-24s %4d" % (fn, n))
    print("\ntotal call sites : %d" % sum(draw_file.values()))
    print("files            : %d" % len(draw_file))
    print("distinct calls   : %d" % len(draw_fn))

    if xlib is None:
        print("\n=== other Xlib: UNAVAILABLE ===")
        print("  %s not found, so only the Stage 2 scope could be counted."
              % " / ".join(XLIB_HEADERS))
        print("  Do not read the total above as 'the Xlib left in the tree'.")
        return

    core_total = gui_total = 0
    rows = []
    for fn, n in other_fn.most_common():
        where = other_where[fn]
        core = sum(v for k, v in where.items() if k not in GUI_FILES)
        gui = n - core
        core_total += core
        gui_total += gui
        rows.append((fn, core, gui, where))

    print("\n=== other Xlib, never in the Stage 2 scope ===")
    print("(fonts, colours, images, regions, cursors -- checked against %d"
          " names the real headers declare)" % len(xlib))
    print("  %-24s %5s %5s   %s" % ("", "core", "gui", "where (core files first)"))
    for fn, core, gui, where in rows:
        if core == 0:
            continue
        top = ", ".join("%s:%d" % (k, v) for k, v in where.most_common()
                        if k not in GUI_FILES)
        print("  %-24s %5d %5d   %s" % (fn, core, gui, top))
    print("\n  (%d further call sites are in GUI-only files and are not listed)"
          % sum(g for _, c, g, _ in rows if c == 0))

    print("\nother Xlib in core files : %d" % core_total)
    print("other Xlib in GUI files  : %d" % gui_total)
    print("\nXlib still reachable from core code: %d drawing + %d other = %d"
          % (sum(n for f, n in draw_file.items() if f not in GUI_FILES),
             core_total,
             sum(n for f, n in draw_file.items() if f not in GUI_FILES)
             + core_total))


if __name__ == "__main__":
    main()
