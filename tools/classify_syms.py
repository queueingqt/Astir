#!/usr/bin/env python3
"""Classify what a given object needs from main.o, by the C type of each symbol.

"109 symbols" is not actionable on its own.  What matters is whether they are
GUI-typed (Widget, Pixel, GC, Pixmap, Colormap, XmFontList -- real Motif/X
entanglement) or plain data (int, long, char, float, time_t -- settings and
state that can simply move).
"""
import subprocess, sys, os, re, collections

if len(sys.argv) < 3:
    raise SystemExit("usage: classify_syms.py <src-dir> <obj> [obj...]")
srcdir, objs = sys.argv[1], sys.argv[2:]

main_o = os.path.join(srcdir, "main.o")
main_c = os.path.join(srcdir, "main.c")

defined = {}
for line in subprocess.run(["nm", "-g", "--defined-only", main_o],
                           capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) >= 3:
        defined[p[2]] = p[1]

src = open(main_c, encoding="utf-8", errors="replace").read()
src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
src = "\n".join(re.sub(r'//.*', '', l) for l in src.split("\n"))

# Map symbol -> declared C type, by finding its file-scope definition.
ctype = {}
for m in re.finditer(r'^([A-Za-z_][\w \t\*]*?)\s*\**\s*([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*(=|;|,)',
                     src, re.M):
    t, name = m.group(1).strip(), m.group(2)
    if name not in ctype:
        ctype[name] = t

GUI_TYPES = ("Widget", "Pixel", "GC", "Pixmap", "Colormap", "XmFontList",
             "XmString", "Position", "Dimension", "Display", "Window",
             "Pixel_Format", "XtAppContext", "Atom", "Cursor", "XImage",
             "XtIntervalId", "XFontStruct", "Visual")

def kind(sym):
    t = ctype.get(sym)
    if t is None:
        return "unknown", None
    for g in GUI_TYPES:
        if re.search(r'\b' + g + r'\b', t):
            return "GUI", t
    return "plain", t

for obj in objs:
    path = os.path.join(srcdir, obj)
    undef = [l.split()[-1] for l in
             subprocess.run(["nm", "-u", path], capture_output=True,
                            text=True).stdout.splitlines() if l.strip()]
    need = [s for s in undef if s in defined]
    data = [s for s in need if defined[s] in ("D", "B", "R", "G", "S")]
    func = [s for s in need if defined[s] == "T"]

    buckets = collections.defaultdict(list)
    for s in data:
        k, t = kind(s)
        buckets[k].append((s, t))

    print("=" * 68)
    print("%s : %d symbols from main.o (%d data, %d functions)"
          % (obj, len(need), len(data), len(func)))
    print("=" * 68)
    for k in ("GUI", "plain", "unknown"):
        if not buckets[k]:
            continue
        print("\n  %s data (%d):" % (k.upper(), len(buckets[k])))
        for s, t in sorted(buckets[k]):
            print("    %-34s %s" % (s, t or "?"))
    if func:
        print("\n  FUNCTIONS (%d): %s" % (len(func), ", ".join(sorted(func))))
    print()
