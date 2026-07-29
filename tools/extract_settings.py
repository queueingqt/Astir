#!/usr/bin/env python3
"""Relocate plain-data settings/state definitions from main.c into a core file.

Same technique as the xa_state step: move the DEFINITION, keep the name, type
and initialiser identical, so no call site changes and behaviour cannot shift.

Only handles definitions that are unambiguous: file-scope (column 0), a single
declarator, not inside any #if.  Anything else is reported and left for a human,
because a wrong move here is a silent behaviour change.

Usage: extract_settings.py <src-dir> <out-basename> [--from=file.c] [--append]
                           [--apply]
Reads the target symbol list on stdin, one per line.

--from  the file to take definitions out of (default main.c).  main.c was the
        only source when this was written, because it was the only one anybody
        had measured.  A trial link later showed the core also needs 35 symbols
        from eleven *_gui.c files, most of them plain settings that are only in
        a GUI file because that is where the dialog editing them lives.
--append  add to an existing out-base.c/.h instead of creating them.
"""
import subprocess, sys, os, re

args = [a for a in sys.argv[1:] if not a.startswith("--")]
if len(args) < 2:
    raise SystemExit("usage: extract_settings.py <src-dir> <out-base> "
                     "[--from=f.c] [--append] [--apply]")
srcdir, outbase = args[0], args[1]
APPLY = "--apply" in sys.argv
APPEND = "--append" in sys.argv
srcname = "main.c"
for a in sys.argv[1:]:
    if a.startswith("--from="):
        srcname = a[len("--from="):]

targets = [l.strip() for l in sys.stdin if l.strip()]
main_c = os.path.join(srcdir, srcname)
raw = open(main_c, encoding="utf-8").read()
lines = raw.split("\n")

# Mark lines inside preprocessor conditionals -- moving those needs the guard too.
depth, guarded = 0, [False] * len(lines)
for i, l in enumerate(lines):
    s = l.strip()
    if re.match(r'#\s*(if|ifdef|ifndef)\b', s):
        depth += 1
    guarded[i] = depth > 0
    if re.match(r'#\s*endif\b', s):
        depth = max(0, depth - 1)

# A file-scope, single-declarator definition starting at column 0.
# A file-scope, single-declarator definition starting at column 0.  The
# trailing-comment group must accept BOTH styles: an earlier version allowed
# only //, which silently skipped 34 targets that use /* ... */ instead.
DEF = re.compile(
    r'^(?P<type>[A-Za-z_][A-Za-z_0-9 \t]*?)\s+(?P<stars>\**)\s*'
    r'(?P<name>[A-Za-z_]\w*)\s*(?P<arr>(\[[^\]]*\])*)\s*'
    r'(?P<init>=\s*[^;]+)?;[ \t]*(?P<cmt>//.*|/\*.*)?$')

found, skipped = {}, {}
for i, l in enumerate(lines):
    m = DEF.match(l)
    if not m:
        continue
    name = m.group("name")
    if name not in targets:
        continue
    if m.group("type").strip() in ("return", "typedef", "extern", "static"):
        continue
    if guarded[i]:
        skipped[name] = (i + 1, "inside #if guard")
        continue
    if name in found:
        skipped[name] = (i + 1, "defined more than once")
        continue
    found[name] = (i, l)

missing = [t for t in targets if t not in found and t not in skipped]

print("targets              : %d" % len(targets))
print("movable definitions  : %d" % len(found))
print("skipped (guarded/dup): %d" % len(skipped))
print("not found at col 0   : %d" % len(missing))
if skipped:
    print("\nskipped:")
    for n, (ln, why) in sorted(skipped.items()):
        print("  %-34s %s:%-6d %s" % (n, srcname, ln, why))
if missing:
    print("\nnot found as a simple file-scope definition (leave for a human):")
    for n in sorted(missing):
        print("  %s" % n)

if not APPLY:
    print("\n(dry run -- pass --apply to write)")
    sys.exit(0)

# Write the new core file, and blank the moved lines in main.c.
decls, defs = [], []
for name in sorted(found, key=lambda n: found[n][0]):
    i, l = found[name]
    defs.append(l)
    m = DEF.match(l)
    decl = "extern %s %s%s%s;" % (m.group("type").strip(), m.group("stars"),
                                  name, m.group("arr") or "")
    cmt = m.group("cmt")
    decls.append(decl + ("  " + cmt if cmt else ""))

guard = os.path.basename(outbase).upper().replace(".", "_") + "_H"

if APPEND:
    hpath = os.path.join(srcdir, outbase + ".h")
    cpath = os.path.join(srcdir, outbase + ".c")
    htxt = open(hpath, encoding="utf-8").read()
    endif = htxt.rfind("#endif")
    if endif < 0:
        raise SystemExit("%s has no #endif to insert before" % hpath)
    block = ("\n// Moved out of %s, where these lived only because the dialog\n"
             "// that edits them does.  Definitions unchanged.\n" % srcname
             + "\n".join(decls) + "\n")
    open(hpath, "w", encoding="utf-8").write(htxt[:endif] + block + "\n"
                                             + htxt[endif:])
    open(cpath, "a", encoding="utf-8").write(
        "\n\n// Moved verbatim from %s.\n" % srcname + "\n".join(defs) + "\n")
    for name, (i, l) in found.items():
        lines[i] = None
    open(main_c, "w", encoding="utf-8").write(
        "\n".join(l for l in lines if l is not None))
    print("\nappended %d definitions to %s.c/.h; removed them from %s"
          % (len(found), outbase, srcname))
    sys.exit(0)

with open(os.path.join(srcdir, outbase + ".h"), "w", encoding="utf-8") as f:
    f.write("""/*
 * %s.h -- user-configurable settings and session state, owned by the core.
 *
 * Moved out of main.c so that core objects needing them do not have to link
 * main.o, and therefore Motif.  Measured motivation: xa_config.o needed 109
 * symbols from main.o, every one of them plain data and not a single GUI type;
 * db.o needed 69, of which only three Widgets and one function were GUI.
 *
 * Names, types and initialisers are unchanged from main.c on purpose, so that
 * relocating them could not alter behaviour.
 *
 * This header must never include an X11, Xt or Motif header.
 */

#ifndef %s
#define %s

""" % (os.path.basename(outbase), guard, guard))
    f.write("\n".join(decls))
    f.write("\n\n#endif // %s\n" % guard)

with open(os.path.join(srcdir, outbase + ".c"), "w", encoding="utf-8") as f:
    f.write("""/*
 * %s.c -- definitions for %s.h.  Moved verbatim from main.c.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>

#include "%s.h"

""" % (os.path.basename(outbase), os.path.basename(outbase),
       os.path.basename(outbase)))
    f.write("\n".join(defs))
    f.write("\n")

for name, (i, l) in found.items():
    lines[i] = None
newmain = "\n".join(l for l in lines if l is not None)
with open(main_c, "w", encoding="utf-8") as f:
    f.write(newmain)

print("\nwrote %s.c / %s.h with %d definitions; removed them from main.c"
      % (outbase, outbase, len(found)))
