#!/usr/bin/env python3
"""Find the GUI/core seam inside one .c file, function by function.

The remaining main.o dependencies are not calls that can be inverted into the
callback table -- they are Motif dialogs sitting in files classified as core.
Splitting those files is the next step, and the question that decides whether a
split is cheap or expensive is not "how many GUI functions", it is "how much do
the GUI functions and the core functions share".

So this reports three things per file:

  * which top-level functions touch Motif/Xt at all,
  * which file-scope statics each group uses, and
  * the statics used by BOTH -- the actual cost of the split, since every one
    of those has to become a shared declaration or move with one side.

Usage: split_scope.py <src-dir> <file.c> [file.c...]
"""
import re, sys, os, collections

GUI_RE = re.compile(
  r'\b(Xm[A-Z]\w*|Xt[A-Z]\w*|XmString\w*|Widget|XtPointer|XtAppContext|'
  r'XmFontList|xmDialogShellWidgetClass|xm\w+WidgetClass|XmN\w+|XmC\w+)\b')

# A file-scope definition: no leading whitespace, ends in ; or = or {.
DEF_RE = re.compile(r'^(?:static\s+)?[A-Za-z_][\w\s\*\[\]]*?\b(\w+)\s*(?:\[[^\]]*\])?\s*(?:=|;)',
                    re.M)


def strip_comments(src):
  """Remove comments, keeping newlines so line numbers survive."""
  out, i, n = [], 0, len(src)
  while i < n:
    c = src[i]
    if c == '/' and i + 1 < n and src[i + 1] == '*':
      j = src.find('*/', i + 2)
      seg = src[i:n if j < 0 else j + 2]
      out.append('\n' * seg.count('\n'))
      i = n if j < 0 else j + 2
    elif c == '/' and i + 1 < n and src[i + 1] == '/':
      j = src.find('\n', i)
      i = n if j < 0 else j
    else:
      out.append(c)
      i += 1
  return ''.join(out)


def top_level_functions(src):
  """Yield (name, start, end) for each brace-balanced top-level function body.

  A function definition here is a line starting in column 0 that contains a
  '(' and whose matching ')' is followed by '{'.  Declarations end in ';' and
  are skipped.
  """
  funcs = []
  for m in re.finditer(r'^([A-Za-z_][\w \t\*]*?)\b(\w+)\s*\(', src, re.M):
    open_paren = src.find('(', m.end() - 1)
    depth, i, n = 0, open_paren, len(src)
    while i < n:
      if src[i] == '(':
        depth += 1
      elif src[i] == ')':
        depth -= 1
        if depth == 0:
          break
      i += 1
    j = i + 1
    while j < n and src[j] in ' \t\r\n':
      j += 1
    if j >= n or src[j] != '{':
      continue                      # a declaration or a call, not a definition
    depth, k = 0, j
    while k < n:
      if src[k] == '{':
        depth += 1
      elif src[k] == '}':
        depth -= 1
        if depth == 0:
          break
      k += 1
    funcs.append((m.group(2), m.start(), k + 1))
  return funcs


def file_statics(src, funcs):
  """File-scope identifiers: definitions outside any function body."""
  spans = [(s, e) for _, s, e in funcs]
  names = set()
  for m in DEF_RE.finditer(src):
    if any(s <= m.start() < e for s, e in spans):
      continue
    names.add(m.group(1))
  return names


def report(path):
  raw = open(path, encoding='utf-8', errors='replace').read()
  src = strip_comments(raw)
  funcs = top_level_functions(src)
  statics = file_statics(src, funcs)

  gui, core = [], []
  gui_uses, core_uses = collections.Counter(), collections.Counter()
  for name, s, e in funcs:
    whole = src[s:e]
    # Count Motif references in the BODY only.  A function whose sole GUI
    # contact is `Widget w` in its parameter list is not GUI code -- it is core
    # code carrying a vestigial parameter, and lumping the two together makes
    # every drawing function in the file look like it belongs to the front end.
    brace = whole.find('{')
    body = whole[brace:] if brace >= 0 else whole
    hits = len(GUI_RE.findall(body))
    sig_only = hits == 0 and GUI_RE.search(whole[:brace] if brace >= 0 else '')
    used = {v for v in statics if re.search(r'\b%s\b' % re.escape(v), body)}
    if hits:
      gui.append((name, hits, e - s))
      gui_uses.update(used)
    else:
      core.append((name, e - s, bool(sig_only)))
      core_uses.update(used)

  shared = sorted(set(gui_uses) & set(core_uses))
  gui_bytes = sum(b for _, _, b in gui)
  core_bytes = sum(b for _, b, _ in core)
  vestigial = [n for n, _, sig in core if sig]

  print("=" * 70)
  print("%s  --  %d functions, %d GUI / %d core, %.0f%% of the code is GUI"
        % (os.path.basename(path), len(funcs), len(gui), len(core),
           100.0 * gui_bytes / max(1, gui_bytes + core_bytes)))
  print("=" * 70)
  print("  GUI functions -- Motif in the body (%d):" % len(gui))
  for name, hits, b in sorted(gui, key=lambda t: -t[1]):
    print("    %-42s %4d Motif refs, %5d bytes" % (name, hits, b))
  print("\n  core, but carrying a Widget only in the signature (%d):"
        % len(vestigial))
  print("    " + (", ".join(sorted(vestigial)) if vestigial else "(none)"))
  print("\n  shared file-scope names (%d) -- the cost of the split:" % len(shared))
  if shared:
    for v in shared:
      print("    %-40s  gui:%-3d core:%d" % (v, gui_uses[v], core_uses[v]))
  else:
    print("    (none -- the two halves touch no common file-scope state)")
  print()


if __name__ == "__main__":
  if len(sys.argv) < 3:
    raise SystemExit("usage: split_scope.py <src-dir> <file.c> [file.c...]")
  srcdir = sys.argv[1]
  for f in sys.argv[2:]:
    report(os.path.join(srcdir, f))
