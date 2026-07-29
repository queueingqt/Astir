#!/usr/bin/env python3
"""Measure the core/GUI boundary: what a non-Motif core would need from main.o.

A full GUI rewrite has to link the core (map loading, APRS, interfaces, shapes)
without main.c, which is 31k lines of mixed Motif and core logic.  This reports,
per object file, how many symbols it pulls from main.o -- and splits them into
data (globals, the hard part) versus functions (callable, can become a vtable).
"""
import subprocess, sys, os, glob, collections

srcdir = sys.argv[1] if len(sys.argv) > 1 else "."

# Objects that are GUI by nature -- a new front end replaces these outright.
GUI_OBJS = {
    "main.o", "db_gui.o", "interface_gui.o", "list_gui.o", "locate_gui.o",
    "location_gui.o", "popup_gui.o", "objects_gui.o", "bulletin_gui.o",
    "geocoder_gui.o", "messages_gui.o", "track_gui.o", "view_message_gui.o",
    "cad_objects_gui.o", "draw_symbols_gui.o", "maps_gui.o",
    "wx_gui.o", "xa_config_gui.o", "map_gui.o",
}

# Not part of the program: standalone utilities with their own main().
STANDALONE = {"xastir_udp_client.o", "testdbfawk.o", "callpass.o"}

def syms(obj, mode):
    try:
        out = subprocess.run(["nm", mode, obj], capture_output=True, text=True).stdout
    except Exception:
        return {}
    d = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 3:
            d[p[2]] = p[1]
        elif len(p) == 2 and p[0] == "U":
            d[p[1]] = "U"
    return d

main_o = os.path.join(srcdir, "main.o")
if not os.path.exists(main_o):
    raise SystemExit("main.o not found; build first")

# What main.o defines, and whether each is data or code.
defined = {}
out = subprocess.run(["nm", "-g", "--defined-only", main_o],
                     capture_output=True, text=True).stdout
for line in out.splitlines():
    p = line.split()
    if len(p) >= 3:
        defined[p[2]] = p[1]

rows = []
all_needed = collections.Counter()
for obj in sorted(glob.glob(os.path.join(srcdir, "*.o"))):
    base = os.path.basename(obj)
    if base == "main.o":
        continue
    out = subprocess.run(["nm", "-u", obj], capture_output=True, text=True).stdout
    undef = [l.split()[-1] for l in out.splitlines() if l.strip()]
    need = [s for s in undef if s in defined]
    if not need:
        continue
    data = [s for s in need if defined[s] in ("D", "B", "R", "G", "S")]
    code = [s for s in need if defined[s] == "T"]
    rows.append((base, len(need), len(data), len(code), base in GUI_OBJS))
    all_needed.update(need)

rows.sort(key=lambda r: -r[1])
print("%-24s %5s %6s %5s  %s" % ("object", "total", "data", "func", "kind"))
for base, n, d, c, isgui in rows:
    print("%-24s %5d %6d %5d  %s" % (base, n, d, c, "GUI" if isgui else "core"))

core_rows = [r for r in rows if not r[4]]
print("\ncore objects needing main.o : %d" % len(core_rows))

# Count over core objects only.  This used to print len(all_needed), which
# accumulates over every object including the GUI ones, directly under the
# core-object line -- so it read as the core's number and was quoted as one.
core_syms = set()
for obj in sorted(glob.glob(os.path.join(srcdir, "*.o"))):
    base = os.path.basename(obj)
    if base == "main.o" or base in GUI_OBJS or base in STANDALONE:
        continue
    out = subprocess.run(["nm", "-u", obj], capture_output=True, text=True).stdout
    core_syms |= {l.split()[-1] for l in out.splitlines()
                  if l.strip() and l.split()[-1] in defined}
print("distinct main.o symbols core objects need: %d" % len(core_syms))
print("  (all objects, GUI included, need %d -- not the same question)"
      % len(all_needed))

# The globals shared by the most core objects are the ones to move first.
print("\nmost widely shared main.o DATA symbols (core objects only):")
shared = collections.Counter()
for obj in sorted(glob.glob(os.path.join(srcdir, "*.o"))):
    base = os.path.basename(obj)
    if base == "main.o" or base in GUI_OBJS:
        continue
    out = subprocess.run(["nm", "-u", obj], capture_output=True, text=True).stdout
    for l in out.splitlines():
        if not l.strip():
            continue
        s = l.split()[-1]
        if s in defined and defined[s] in ("D", "B", "R", "G", "S"):
            shared[s] += 1
for s, n in shared.most_common(25):
    print("  %-34s used by %2d core objects" % (s, n))
print("\ndistinct main.o data symbols used by core objects: %d" % len(shared))


# ---------------------------------------------------------------------------
# main.o is not the whole front end.
#
# Everything above measures one object.  That was the original question -- main.c
# is 31k lines and the obvious obstacle -- but it is not the question that
# decides whether the core can link on its own, and reporting only that number
# understated the coupling by a factor of four.  A trial link of the core
# objects alone leaves 46 symbols unresolved across 12 GUI objects; main.o
# accounts for 11 of them.
#
# tools/link_core.sh performs that link and is the ground truth.  This is the
# cheap version of the same question.
# ---------------------------------------------------------------------------
gui_defines = {}
for obj in sorted(glob.glob(os.path.join(srcdir, "*.o"))):
    base = os.path.basename(obj)
    if base not in GUI_OBJS:
        continue
    out = subprocess.run(["nm", "--defined-only", obj],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 3 and p[1] in "TDBRGS":
            gui_defines.setdefault(p[2], base)

per_gui = collections.defaultdict(set)
for obj in sorted(glob.glob(os.path.join(srcdir, "*.o"))):
    base = os.path.basename(obj)
    if base in GUI_OBJS or base in STANDALONE:
        continue
    out = subprocess.run(["nm", "-u", obj], capture_output=True, text=True).stdout
    for l in out.splitlines():
        if not l.strip():
            continue
        s = l.split()[-1]
        if s in gui_defines:
            per_gui[gui_defines[s]].add(s)

print("\n=== what the core needs from the WHOLE front end, not just main.o ===")
print("%-24s %5s  %s" % ("defined by", "syms", "symbols"))
total = 0
for o, syms_ in sorted(per_gui.items(), key=lambda kv: -len(kv[1])):
    total += len(syms_)
    listing = ", ".join(sorted(syms_))
    print("  %-22s %5d  %s" % (o, len(syms_),
                               listing if len(listing) <= 96 else listing[:93] + "..."))
print("\ncore objects cannot link without %d symbols from %d GUI objects"
      % (total, len(per_gui)))
