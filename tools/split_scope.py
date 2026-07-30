#!/usr/bin/env python3
"""Find the GUI/core seam inside one .c file, function by function.

The remaining main.o dependencies are not calls that can be inverted into the
callback table -- they are Motif dialogs sitting in files classified as core.
Splitting those files is the next step, and the question that decides whether a
split is cheap or expensive is not "how many GUI functions", it is "how much do
the GUI functions and the core functions share".

Motif in the body is only the first-order answer.  A function with no Motif in
it that *calls* one of the dialog builders is coupled to the front end just as
surely, and a split has to either move it too or invert the call.  Missing that
makes a file look cleaner than it is: cad_objects.c looked like a tidy cut
until three of its "core" functions turned out to call dialogs directly.

So the classification runs to a fixpoint -- a function is GUI if it touches
Motif, or if it calls a function that is GUI -- and the calls that crossed the
boundary are reported, because those are the decisions a split forces.

Reports per file:

  * GUI functions, split into direct (Motif in the body) and pulled in by a call
  * core functions carrying a Widget only in the signature
  * the calls from core into GUI -- each is a move-or-invert decision
  * file-scope names both halves touch -- the rest of the split's cost

Usage: split_scope.py <src-dir> <file.c> [file.c...]

Names defined elsewhere are not seen.  Pass extra known-GUI function names with
--gui=name1,name2 to include calls out of the file (redraw_symbols, pos_dialog
and resize_dialog live in main.c, for example).
"""
import re, sys, os, collections

GUI_RE = re.compile(
  r'\b(Xm[A-Z]\w*|Xt[A-Z]\w*|XmString\w*|Widget|XtPointer|XtAppContext|'
  r'XmFontList|xmDialogShellWidgetClass|xm\w+WidgetClass|XmN\w+|XmC\w+)\b')

# A file-scope definition: no leading whitespace, ends in ; or = or {.
DEF_RE = re.compile(r'^(?:static\s+)?[A-Za-z_][\w\s\*\[\]]*?\b(\w+)\s*(?:\[[^\]]*\])?\s*(?:=|;)',
                    re.M)


def strip_comments(src):
  """Remove comments AND string/char literals, keeping newlines.

  The literals matter, and leaving them in made this tool confidently wrong.
  Function boundaries here are found by counting braces, and db.c line 993 is
  the char literal '}' -- the APRS message-acknowledgement character.  That one
  unbalanced brace shifted every function boundary after it, so alert_data_add
  was reported as spanning lines 1554-17480 and holding 42673 bytes and 12 Motif
  references.  It is 102 lines long and contains no Motif at all; what it had
  swallowed was update_messages(), the actual GUI function in the file.

  A tool used to decide where to cut a file has to survive a brace in a string.
  """
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
    elif c == '"' or c == "'":
      # Replace the literal with an empty one of the same kind, so that code
      # like `if (c == '}')` still parses as an expression but contributes no
      # brace.  Newlines inside (continued string literals) are preserved.
      quote, j = c, i + 1
      while j < n:
        if src[j] == '\\':
          j += 2
          continue
        if src[j] == quote:
          j += 1
          break
        j += 1
      seg = src[i:j]
      out.append(quote + quote + '\n' * seg.count('\n'))
      i = j
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


def report(path, extra_gui=()):
  raw = open(path, encoding='utf-8', errors='replace').read()
  src = strip_comments(raw)
  funcs = top_level_functions(src)
  statics = file_statics(src, funcs)

  info = {}
  for name, s, e in funcs:
    whole = src[s:e]
    # Count Motif references in the BODY only.  A function whose sole GUI
    # contact is `Widget w` in its parameter list is not GUI code -- it is core
    # code carrying a vestigial parameter, and lumping the two together makes
    # every drawing function in the file look like it belongs to the front end.
    brace = whole.find('{')
    body = whole[brace:] if brace >= 0 else whole
    info[name] = {
      'hits': len(GUI_RE.findall(body)),
      'sig': bool(GUI_RE.search(whole[:brace] if brace >= 0 else '')),
      'size': e - s,
      'body': body,
      'statics': {v for v in statics if re.search(r'\b%s\b' % re.escape(v), body)},
      'calls': set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', body)),
    }

  # Fixpoint: GUI by Motif, then GUI by calling something already GUI.
  direct = {n for n, d in info.items() if d['hits']}
  gui_set = set(direct) | set(extra_gui)
  crossings = []
  while True:
    grew = False
    for n, d in info.items():
      if n in gui_set:
        continue
      reached = sorted(d['calls'] & gui_set)
      if reached:
        gui_set.add(n)
        crossings.append((n, reached))
        grew = True
    if not grew:
      break
  gui_set -= set(extra_gui)          # those are not defined in this file

  gui, core = [], []
  gui_uses, core_uses = collections.Counter(), collections.Counter()
  for name, d in info.items():
    if name in gui_set:
      gui.append((name, d['hits'], d['size']))
      gui_uses.update(d['statics'])
    else:
      core.append((name, d['size'], d['sig']))
      core_uses.update(d['statics'])

  shared = sorted(set(gui_uses) & set(core_uses))
  gui_bytes = sum(b for _, _, b in gui)
  core_bytes = sum(b for _, b, _ in core)
  vestigial = [n for n, _, sig in core if sig]

  print("=" * 70)
  print("%s  --  %d functions, %d GUI / %d core, %.0f%% of the code is GUI"
        % (os.path.basename(path), len(funcs), len(gui), len(core),
           100.0 * gui_bytes / max(1, gui_bytes + core_bytes)))
  print("=" * 70)
  print("  GUI functions (%d) -- Motif refs, or 0 if pulled in by a call:"
        % len(gui))
  for name, hits, b in sorted(gui, key=lambda t: -t[1]):
    print("    %-42s %4d Motif refs, %5d bytes" % (name, hits, b))
  print("\n  core, but carrying a Widget only in the signature (%d):"
        % len(vestigial))
  print("    " + (", ".join(sorted(vestigial)) if vestigial else "(none)"))
  print("\n  calls out of core into GUI (%d) -- move it or invert it:"
        % len(crossings))
  if crossings:
    for name, reached in sorted(crossings):
      print("    %-38s -> %s" % (name, ", ".join(reached)))
  else:
    print("    (none)")
  print("\n  shared file-scope names (%d) -- the cost of the split:" % len(shared))
  if shared:
    for v in shared:
      print("    %-40s  gui:%-3d core:%d" % (v, gui_uses[v], core_uses[v]))
  else:
    print("    (none -- the two halves touch no common file-scope state)")
  print()


if __name__ == "__main__":
  args = [a for a in sys.argv[1:] if not a.startswith("--")]
  extra = []
  for a in sys.argv[1:]:
    if a.startswith("--gui="):
      extra = [x for x in a[len("--gui="):].split(",") if x]
  if len(args) < 2:
    raise SystemExit("usage: split_scope.py [--gui=fn,fn] <src-dir> <file.c>...")
  srcdir = args[0]
  for f in args[1:]:
    report(os.path.join(srcdir, f), extra)
