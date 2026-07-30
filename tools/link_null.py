#!/usr/bin/env python3
"""Link the core against the *null* backend, with no X libraries at all.

link_core.py asks "can the core link without the front end", and the answer has
been yes for a while.  This asks the harder question the notes kept deferring:
**can the core link without X?**  That is what a GTK4 port actually needs, and
no call-site count can answer it -- a header that only ever gets compiled inside
X11 looks portable from in there.

The method is the same one that made link_core.py trustworthy: do the thing and
see.  Take every core object, swap xa_draw_x11.o for xa_draw_null.o, drop every
X library from the link line, and report what is left undefined, attributed to
the object that needs it.

The link is expected to FAIL.  What matters is the list, because that list is
exactly the remaining work -- and unlike a grep it cannot be fooled by a name
that merely starts with X (XTIFFOpen is libtiff) or miss one that does not.

Usage: link_null.py [srcdir]     (default src/, must be built)
"""
import subprocess, sys, os, re, glob, collections, tempfile

GUI_SUFFIX = "_gui.o"
STANDALONE = {"xastir_udp_client.o", "testdbfawk.o", "callpass.o"}
# The X11 backend, all four files of it.  xa_draw_x11.c is the obvious one;
# the other three are the same thing wearing different names:
#
#   rotated.c     rotated text, predating Xft.  Pango does this natively, so a
#                 second backend replaces it rather than porting it.
#   color.c       visual detection and colour allocation.
#   cairo_text.c  the Cairo text path.
#
# Calling them "core" was a filing error, not a finding: nm says no core object
# references any symbol any of them defines.  Verified, not assumed -- see
# tools/README.md.
X11_BACKEND = {"xa_draw_x11.o", "rotated.o", "color.o", "cairo_text.o"}

X_LIB = re.compile(r'^-l(X[a-zA-Z0-9]*|Xm|Xt|ICE|SM|Xext|Xpm|xcb.*)$')


def link_command(srcdir):
  """The real link line for xastir, from make, so this tracks ./configure."""
  mk = subprocess.run(["make", "-n", "-W", "main.c", "xastir"], cwd=srcdir,
                      capture_output=True, text=True).stdout
  for line in mk.splitlines():
    if " -o xastir " not in line:
      continue
    for part in line.split(";"):
      part = part.strip()
      if part.startswith(("gcc", "cc ", "clang")) and " -o xastir " in part:
        return part
  return None


def main():
  srcdir = sys.argv[1] if len(sys.argv) > 1 else "src"
  if not os.path.exists(os.path.join(srcdir, "main.o")):
    raise SystemExit("%s not built" % srcdir)

  cmd = link_command(srcdir)
  if not cmd:
    raise SystemExit("could not find the link command; run make first")

  toks = cmd.split()
  objs = sorted(os.path.basename(p) for p in glob.glob(os.path.join(srcdir, "*.o")))
  core = [o for o in objs
          if not o.endswith(GUI_SUFFIX) and o not in STANDALONE
          and o != "main.o" and o not in X11_BACKEND
          and o != "xa_draw_null.o"]   # appended explicitly below

  # Compiled here rather than by make, because it must never end up in the
  # xastir binary: it defines the same symbols as the X11 backend.  Compiled
  # with no X include path on purpose -- if it ever needs one, xa_draw.h has
  # sprung a leak and this is where that shows up first.
  nullobj = os.path.join(srcdir, "xa_draw_null.o")
  r = subprocess.run(["gcc", "-I" + srcdir,
                      "-I" + os.path.dirname(os.path.abspath(srcdir)),
                      "-O2", "-Wall", "-c", os.path.join(srcdir, "xa_draw_null.c"),
                      "-o", nullobj], capture_output=True, text=True)
  if r.returncode != 0:
    raise SystemExit("the null backend does not compile -- xa_draw.h is not "
                     "toolkit-neutral:\n" + r.stderr)

  # Rebuild the command IN ORDER.  Libraries only resolve against objects that
  # precede them on the link line, so hoisting the flags to the front makes
  # every library look unsatisfied and the report becomes noise about
  # ImageMagick and shapelib.  That happened; hence this comment.
  #
  # LTO stays ON.  Turning it off to get per-object attribution looks like the
  # obvious move and is a trap: these objects are LTO objects, so without the
  # plugin the linker cannot read them at all, reports two undefined symbols
  # instead of a hundred and sixty, and the run looks like a triumph.  That
  # happened.  Attribution comes from nm below instead.
  dropped, rebuilt, placed = [], [toks[0]], False
  for t in toks[1:]:
    if t.endswith(".o"):
      if not placed:
        rebuilt.append("@OBJS@")
        placed = True
      continue
    if X_LIB.match(t):
      dropped.append(t)
      continue
    rebuilt.append(t)
  if not placed:
    raise SystemExit("no objects found on the link line")

  print("core objects : %d  (+ xa_draw_null.o, without %s)"
        % (len(core), " ".join(sorted(X11_BACKEND))))
  print("X libs dropped: %s" % (" ".join(sorted(set(dropped))) or "(none found!)"))

  with tempfile.TemporaryDirectory() as td:
    stub = os.path.join(td, "stub.c")
    open(stub, "w").write("int main(void){return 0;}\n")
    # Force every core-defined symbol live, or the linker discards what nothing
    # references and the link succeeds while proving nothing.  Same trap
    # link_core.py documents.
    live = set()
    for o in core:
      nm = subprocess.run(["nm", "-g", "--defined-only", os.path.join(srcdir, o)],
                          capture_output=True, text=True).stdout
      for l in nm.splitlines():
        p = l.split()
        if len(p) >= 3 and p[1] in "TDBRG":
          live.add(p[2])
    rsp = os.path.join(td, "objs.rsp")
    open(rsp, "w").write("\n".join(core) + "\nxa_draw_null.o\n")
    out = os.path.join(td, "a.out")
    args = []
    for t in rebuilt:
      if t == "@OBJS@":
        args += ["@" + rsp, stub]
      elif t == "xastir":
        args.append(out)
      else:
        args.append(t)
    args += ["-Wl,-u%s" % sym for sym in sorted(live)]
    # Run from srcdir: the link line carries -Lrtree and other relative paths,
    # which resolve to nothing from anywhere else -- and the link then fails
    # for a reason that has nothing to do with X.
    r = subprocess.run(args, capture_output=True, text=True, cwd=srcdir)

    # Ask the binary what it ended up needing, while it still exists.  A
    # successful link is weaker evidence than it looks: striking -lX11 off the
    # command line does not make libX11 unavailable, because GraphicsMagick
    # lists it in DT_NEEDED and the linker will happily resolve against it.
    xdeps = []
    if r.returncode == 0 and os.path.exists(out):
      ldd = subprocess.run(["ldd", out], capture_output=True, text=True).stdout
      xdeps = sorted(set(re.findall(r'\b(lib(?:X[A-Za-z0-9]*|ICE|SM)\.so[^\s]*)', ldd)))

  # Whatever the linker said, ask the objects directly.  A successful link is
  # not the only useful answer and it is not the one available today: the first
  # thing that fails is GraphicsMagick, which is itself linked against libX11,
  # so Xastir's own X usage never gets a chance to be reported.
  #
  # So: take the symbols each core object leaves undefined, and intersect them
  # with what the X libraries actually export.  That is the same method
  # audit_x11.py uses for headers, and for the same reason -- a name starting
  # with X is not evidence (XTIFFOpen is libtiff), and a name that does not is
  # not evidence of the opposite.
  xsyms = set()
  libs = []
  for pat in ("libX11.so*", "libXt.so*", "libXm.so*", "libXext.so*",
              "libXpm.so*", "libICE.so*", "libSM.so*"):
    for d in ("/usr/lib", "/usr/lib64", "/usr/lib/x86_64-linux-gnu"):
      libs += glob.glob(os.path.join(d, pat))
  for lib in libs:
    out = subprocess.run(["nm", "-D", "--defined-only", lib],
                         capture_output=True, text=True).stdout
    xsyms.update(re.findall(r'^\S+\s+[TWiD]\s+(\S+)', out, re.M))
  if not xsyms:
    raise SystemExit("could not read any X library's symbols -- cannot tell an "
                     "X symbol from any other, so refusing to report a number")

  need = collections.defaultdict(set)
  for o in core:
    nm = subprocess.run(["nm", "-u", os.path.join(srcdir, o)],
                        capture_output=True, text=True).stdout
    for sym in re.findall(r'^\s+U\s+(\S+)', nm, re.M):
      if sym in xsyms:
        need[o].add(sym)

  if r.returncode == 0:
    if xdeps:
      print("\nLinked -- but NOT proof of anything on its own: the binary still")
      print("pulls in %d X libraries transitively (%s ...)."
            % (len(xdeps), ", ".join(xdeps[:3])))
      print("They arrive through GraphicsMagick's DT_NEEDED, not through Xastir.")
      print("The number below is the real result: it comes from nm, not the link.")
    else:
      print("\nLINKED, and the binary has no X library in its dependencies at all.")
  else:
    # The first *error*, not the first ld line -- relocation warnings come
    # first and are not why it failed.
    errs = [l for l in r.stderr.splitlines()
            if "undefined reference" in l or ("error" in l and "warning" not in l)]
    print("\nlink failed. blockers:")
    for l in errs[:4]:
      print("   " + l.strip()[:110])
    if len(errs) > 4:
      print("   ... and %d more" % (len(errs) - 4))

  total = len(set().union(*need.values())) if need else 0
  print("\nX symbols Xastir's own core objects still need: %d, across %d objects\n"
        % (total, len(need)))
  for o in sorted(need, key=lambda k: (-len(need[k]), k)):
    syms = sorted(need[o])
    print("  %-18s %3d  %s" % (o, len(syms), " ".join(syms[:5])
                               + (" ..." if len(syms) > 5 else "")))
  if not need:
    print("  (none -- every remaining blocker is a third-party library)")


if __name__ == "__main__":
  main()
