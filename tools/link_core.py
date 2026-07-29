#!/usr/bin/env python3
"""Try to link the core objects on their own, with no front end.  Ground truth.

core_boundary.py answers this question with nm, which is fast and was wrong
twice: it looked only at main.o, and it printed a GUI-inclusive symbol count
directly under the core-object count.  This asks the linker instead.

The link is expected to FAIL.  What matters is the list of symbols it fails on:
that is exactly what a new front end has to provide, and nothing else is.

Two ways to get a meaningless answer here, both of which happened:

  * Passing the object list through a shell variable.  The session shell is
    fish, which does not word-split on expansion, so the whole list arrives as
    one filename and the link dies with "File name too long" -- while a grep
    for "undefined reference" finds nothing and reports a clean link.  The
    object list goes in a response file now.

  * Linking against a stub main() that calls nothing.  With -flto the linker
    discards everything unreachable, so the link succeeds trivially and proves
    the opposite of what it appears to.  Every symbol the core objects define
    is forced live with -Wl,-u before linking.

Usage: link_core.py [srcdir]     (default src/, must be built)
"""
import subprocess, sys, os, re, glob, tempfile, collections

GUI_SUFFIX = "_gui.o"
STANDALONE = {"xastir_udp_client.o", "testdbfawk.o", "callpass.o"}


def link_command(srcdir):
  """The real link line for xastir, from make, so this tracks ./configure."""
  # -W pretends main.c changed, so make prints the relink without touching
  # anything.  Without it make says "up to date" and prints no link line at all.
  mk = subprocess.run(["make", "-n", "-W", "main.c", "xastir"], cwd=srcdir,
                      capture_output=True, text=True).stdout
  for line in mk.splitlines():
    if " -o xastir " not in line:
      continue
    # Automake's silent rules prefix the real command with an echo, e.g.
    #   echo "  GEN     " xastir;gcc -fopenmp ... -o xastir ...
    # so the compiler is not necessarily at the start of the line.
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
    raise SystemExit("could not find the link command; run make first "
                     "(it only prints it when xastir is out of date)")

  toks = cmd.split()
  objs, libs, flags, i = [], [], [], 1
  while i < len(toks):
    t = toks[i]
    if t == "-o":
      i += 2
      continue
    (objs if t.endswith(".o") else libs if (t.startswith("-l") or t.startswith("-L"))
     else flags).append(t)
    i += 1

  core = [o for o in objs
          if os.path.basename(o) != "main.o"
          and not os.path.basename(o).endswith(GUI_SUFFIX)
          and os.path.basename(o) not in STANDALONE]
  dropped = [o for o in objs if o not in core]
  print("core objects : %d" % len(core))
  print("omitted      : %s" % " ".join(sorted(os.path.basename(o) for o in dropped)))

  tmp = tempfile.mkdtemp(prefix="linkcore.")
  stub = os.path.join(tmp, "stub_main.c")
  with open(stub, "w") as f:
    f.write("/* stands in for main.c so the core can be linked alone */\n"
            "int main(void) { return 0; }\n")
  subprocess.run(["gcc", "-c", "-o", os.path.join(tmp, "stub_main.o"), stub],
                 check=True)

  # Force-keep every symbol the core defines, or LTO discards the lot.
  nm = subprocess.run(["nm", "--defined-only"] + core, cwd=srcdir,
                      capture_output=True, text=True).stdout
  keep = sorted({p[2] for p in (l.split() for l in nm.splitlines())
                 if len(p) >= 3 and p[1] in "TDBRG"})
  print("symbols forced live: %d" % len(keep))

  rsp = os.path.join(tmp, "objs.rsp")
  with open(rsp, "w") as f:
    f.write(" ".join(core) + " " + os.path.join(tmp, "stub_main.o") + " ")
    f.write(" ".join("-Wl,-u,%s" % s for s in keep))

  out = os.path.join(tmp, "coretest")
  r = subprocess.run(["gcc"] + flags + ["-o", out, "@" + rsp] + libs,
                     cwd=srcdir, capture_output=True, text=True)
  undef = sorted(set(re.findall(r"undefined reference to `([^']+)'",
                                r.stderr + r.stdout)))

  if r.returncode == 0 and not undef:
    print("\nLINKED CLEANLY -- the core needs nothing from the front end.")
    return
  if not undef:
    print("\nlink failed for a reason that is not undefined symbols:")
    print(r.stderr[:2000])
    raise SystemExit(2)

  # Attribute each one, so the output says who has to provide it.
  owner = {}
  for o in dropped:
    d = subprocess.run(["nm", "--defined-only", o], cwd=srcdir,
                       capture_output=True, text=True).stdout
    for p in (l.split() for l in d.splitlines()):
      if len(p) >= 3 and p[1] in "TDBRGS":
        owner.setdefault(p[2], os.path.basename(o))
  by = collections.defaultdict(list)
  for s in undef:
    by[owner.get(s, "(unattributed)")].append(s)

  print("\n%d symbols unresolved, across %d front-end objects:\n"
        % (len(undef), len(by)))
  for o, ss in sorted(by.items(), key=lambda kv: -len(kv[1])):
    print("  %-22s %2d  %s" % (o, len(ss), ", ".join(sorted(ss))))
  print("\nThat list is the front end's contract with the core.")


if __name__ == "__main__":
  main()
