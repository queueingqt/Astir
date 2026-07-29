#!/usr/bin/env python3
"""Route Xlib drawing calls through xa_draw.

Parses each call with balanced-paren matching rather than a line regex, because
these calls are routinely spread over ten lines.  Skips comments and string
literals.  X style constants become the XA_* equivalents, which carry identical
numeric values (asserted at compile time in xa_draw_x11.c).

usage: convert_draw.py <src-dir> <file.c> [file.c ...] [--apply]
"""
import re, sys, os

args = [a for a in sys.argv[1:] if not a.startswith("--")]
APPLY = "--apply" in sys.argv
SRC, FILES = args[0], args[1:]

# name -> (new name, arg indices to keep, extra casts by new-arg position)
MAP = {
    "XSetForeground":     ("xa_pen_color",      [1, 2], {}),
    "XSetBackground":     ("xa_pen_bg",         [1, 2], {}),
    "XSetLineAttributes": ("xa_pen_line",       [1, 2, 3, 4, 5], {}),
    "XSetDashes":         ("xa_pen_dashes",     [1, 2, 3, 4], {}),
    "XSetFillStyle":      ("xa_pen_fill_style", [1, 2], {}),
    "XSetStipple":        ("xa_pen_stipple",    [1, 2], {}),
    "XSetTSOrigin":       ("xa_pen_ts_origin",  [1, 2, 3], {}),
    "XSetFunction":       ("xa_pen_function",   [1, 2], {}),
    "XSetClipMask":       ("xa_pen_clip_mask",  [1, 2], {}),
    "XDrawLine":          ("xa_draw_line",      [1, 2, 3, 4, 5, 6], {}),
    "XDrawLines":         ("xa_draw_lines",     [1, 2, 3, 4, 5], {2: "(xa_point *)"}),
    "XDrawPoint":         ("xa_draw_point",     [1, 2, 3, 4], {}),
    "XDrawRectangle":     ("xa_draw_rect",      [1, 2, 3, 4, 5, 6], {}),
    "XFillRectangle":     ("xa_fill_rect",      [1, 2, 3, 4, 5, 6], {}),
    "XFillPolygon":       ("xa_fill_polygon",   [1, 2, 3, 4, 5, 6], {2: "(xa_point *)"}),
    "XDrawArc":           ("xa_draw_arc",       [1, 2, 3, 4, 5, 6, 7, 8], {}),
    "XFillArc":           ("xa_fill_arc",       [1, 2, 3, 4, 5, 6, 7, 8], {}),
    "XDrawString":        ("xa_draw_string",    [1, 2, 3, 4, 5, 6], {}),
    "XFreePixmap":        ("xa_surface_destroy", [1], {}),
    # XCreatePixmap(dpy, drawable, w, h, depth): the drawable only supplies a
    # screen and depth, both of which the backend resolves itself.
    "XCreatePixmap":      ("xa_surface_create",  [2, 3, 4], {}),
    # XCreateGC(dpy, drawable, mask, values): Xastir always passes mask 0.
    "XCreateGC":          ("xa_pen_create",      [1], {}),
    "XFreeGC":            ("xa_pen_destroy",     [1], {}),
    # XCopyArea(dpy, src, dst, gc, sx, sy, w, h, dx, dy)
    "XCopyArea":          ("xa_copy_area",       [1, 2, 3, 4, 5, 6, 7, 8, 9], {}),
    "XCreateBitmapFromData": ("xa_bitmap_from_data", [2, 3, 4], {}),
}

CONSTS = {
    "LineSolid": "XA_LINE_SOLID", "LineOnOffDash": "XA_LINE_ON_OFF_DASH",
    "LineDoubleDash": "XA_LINE_DOUBLE_DASH",
    "CapNotLast": "XA_CAP_NOT_LAST", "CapButt": "XA_CAP_BUTT",
    "CapRound": "XA_CAP_ROUND", "CapProjecting": "XA_CAP_PROJECTING",
    "JoinMiter": "XA_JOIN_MITER", "JoinRound": "XA_JOIN_ROUND",
    "JoinBevel": "XA_JOIN_BEVEL",
    "FillSolid": "XA_FILL_SOLID", "FillTiled": "XA_FILL_TILED",
    "FillStippled": "XA_FILL_STIPPLED",
    "FillOpaqueStippled": "XA_FILL_OPAQUE_STIPPLED",
    "CoordModeOrigin": "XA_COORD_ORIGIN",
    "CoordModePrevious": "XA_COORD_PREVIOUS",
    "Complex": "XA_SHAPE_COMPLEX", "Nonconvex": "XA_SHAPE_NONCONVEX",
    "Convex": "XA_SHAPE_CONVEX",
    "GXcopy": "XA_FUNC_COPY", "GXxor": "XA_FUNC_XOR",
}
DEPTH_RE = re.compile(r'^DefaultDepthOfScreen\s*\(.*\)$')
CONST_RE = re.compile(r'\b(' + "|".join(CONSTS) + r')\b')


def skip_spans(src):
    """Comment and string-literal ranges."""
    spans, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2); j = n if j < 0 else j + 2
            spans.append((i, j)); i = j
        elif c == '/' and i + 1 < n and src[i + 1] == '/':
            j = src.find('\n', i); j = n if j < 0 else j
            spans.append((i, j)); i = j
        elif c in '"\'':
            q, j = c, i + 1
            while j < n:
                if src[j] == '\\':
                    j += 2; continue
                if src[j] == q:
                    j += 1; break
                j += 1
            spans.append((i, j)); i = j
        else:
            i += 1
    return spans


def in_spans(pos, spans):
    for a, b in spans:
        if a <= pos < b:
            return True
        if a > pos:
            break
    return False


def split_args(s):
    """Split a top-level argument list on commas."""
    out, depth, cur, i, n = [], 0, [], 0, len(s)
    while i < n:
        c = s[i]
        if c in '([':
            depth += 1
        elif c in ')]':
            depth -= 1
        elif c in '"\'':
            q, j = c, i + 1
            while j < n:
                if s[j] == '\\':
                    j += 2; continue
                if s[j] == q:
                    j += 1; break
                j += 1
            cur.append(s[i:j]); i = j; continue
        if c == ',' and depth == 0:
            out.append("".join(cur).strip()); cur = []
        else:
            cur.append(c)
        i += 1
    if "".join(cur).strip():
        out.append("".join(cur).strip())
    return out


NAME_RE = re.compile(r'\b(' + "|".join(MAP) + r')\s*\(')

total = 0
for fname in FILES:
    path = os.path.join(SRC, fname)
    src = open(path, encoding="utf-8").read()
    spans = skip_spans(src)
    out, last, n_here = [], 0, 0
    pos = 0
    while True:
        m = NAME_RE.search(src, pos)
        if not m:
            break
        if in_spans(m.start(), spans):
            pos = m.end(); continue
        name = m.group(1)
        # find matching close paren
        depth, i = 1, m.end()
        while i < len(src) and depth:
            if src[i] == '(':
                depth += 1
            elif src[i] == ')':
                depth -= 1
            i += 1
        inner = src[m.end():i - 1]
        arglist = split_args(inner)
        newname, keep, casts = MAP[name]
        if max(keep) >= len(arglist):
            pos = m.end(); continue        # unexpected arity, leave alone
        newargs = []
        for k, idx in enumerate(keep):
            a = arglist[idx]
            # Arguments frequently carry trailing comments.  The replacement is
            # emitted on ONE line, so a surviving // comment would swallow every
            # argument after it -- strip comments before flattening.
            a = re.sub(r'/\*.*?\*/', ' ', a, flags=re.S)
            a = re.sub(r'//[^\n]*', ' ', a)
            a = " ".join(a.split())
            a = CONST_RE.sub(lambda mm: CONSTS[mm.group(1)], a)
            if DEPTH_RE.match(a):
                a = "XA_DEPTH_CANVAS"
            if not a:
                a = None
                break
            if k in casts:
                a = casts[k] + a
            newargs.append(a)
        if len(newargs) != len(keep):
            pos = m.end(); continue        # an argument vanished; leave alone
        # swallow a preceding (void) cast
        start = m.start()
        pre = src[:start].rstrip()
        if pre.endswith("(void)"):
            start = len(pre) - len("(void)")
        # keep the statement on one line; the originals were split over many
        repl = "%s(%s)" % (newname, ", ".join(newargs))
        out.append(src[last:start]); out.append(repl)
        last = i - 1 + 1 if src[i - 1] == ')' else i
        last = i
        pos = i
        n_here += 1
    out.append(src[last:])
    if n_here:
        newsrc = "".join(out)
        if APPLY:
            open(path, "w", encoding="utf-8").write(newsrc)
        print("%-18s %3d converted" % (fname, n_here))
        total += n_here

print("total converted: %d%s" % (total, "" if APPLY else "   (dry run)"))
