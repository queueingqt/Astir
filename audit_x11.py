#!/usr/bin/env python3
"""Count real Xlib drawing call sites in src/*.c, ignoring comments and strings.

The plan's original audit was a plain grep, which counted mentions inside
comments and silently skipped three ISO-8859 files entirely.  Strip C comments
and string/char literals first so the number reflects code that must actually
be converted.
"""
import re, sys, glob, os, collections

FNS = [
    "XSetForeground", "XSetBackground", "XCopyArea", "XSetLineAttributes",
    "XCreatePixmap", "XDrawLine", "XDrawLines", "XSetFillStyle", "XFillPolygon",
    "XFreePixmap", "XFillRectangle", "XDrawString", "XDrawRectangle",
    "XCreateGC", "XFreeGC", "XSetClipMask", "XSetClipRectangles", "XSetStipple",
    "XSetTSOrigin", "XSetFunction", "XSetDashes", "XDrawArc", "XFillArc",
    "XDrawPoint", "XDrawSegments", "XCreateBitmapFromData", "XPutImage",
    "XGetImage", "XSetFont", "XDrawImageString", "XDrawPoints",
    "XFillRectangles", "XSetLineWidth", "XSetPlaneMask", "XSetSubwindowMode",
]

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

srcdir = sys.argv[1] if len(sys.argv) > 1 else "."
pat = re.compile(r'\b(' + '|'.join(FNS) + r')\s*\(')

per_file = collections.Counter()
per_fn = collections.Counter()
for path in sorted(glob.glob(os.path.join(srcdir, "*.c"))):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        code = strip(f.read())
    hits = pat.findall(code)
    if hits:
        per_file[os.path.basename(path)] = len(hits)
        per_fn.update(hits)

print("=== real call sites per file (comments/strings excluded) ===")
for f, n in sorted(per_file.items(), key=lambda kv: -kv[1]):
    print("  %-22s %4d" % (f, n))
print("\n=== per primitive ===")
for fn, n in per_fn.most_common():
    print("  %-24s %4d" % (fn, n))
print("\ntotal call sites : %d" % sum(per_file.values()))
print("files            : %d" % len(per_file))
print("distinct calls   : %d" % len(per_fn))
