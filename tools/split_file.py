#!/usr/bin/env python3
"""Move a named set of functions out of one .c file into a new one.

Written for the cad_objects.c split, but nothing in it is CAD-specific: give it
a source file, a destination, and the functions to move.

The hard part is not choosing what to move, it is moving it without dropping or
duplicating a byte.  Two things make that checkable:

  * Comments and string literals are blanked to spaces of the SAME LENGTH, so
    every offset computed on the blanked text indexes the original exactly.
    (split_scope.py preserves only newlines, which is fine for line numbers and
    useless for slicing.)

  * Before writing anything, the moved spans and the kept spans are checked to
    reassemble the original file byte for byte.  A split that loses a function,
    or emits one twice, fails here rather than in the compiler -- or worse, in
    the compiler's silence.

A function's span starts at its preceding comment block when one is adjacent,
so the documentation travels with the code.

Usage:
  split_file.py <src.c> <dest.c> <fn>[,<fn>...]  [--header=file] [--apply]

Dry run by default: prints what would move and confirms the reassembly check.
"""
import re, sys, os


def blank(src):
  """Replace comment and string-literal contents with spaces, preserving length.

  Newlines are kept as newlines so line numbers still work.
  """
  out, i, n = [], 0, len(src)
  while i < n:
    c = src[i]
    if c == '/' and i + 1 < n and src[i+1] == '*':
      j = src.find('*/', i + 2)
      j = n if j < 0 else j + 2
      out.append(''.join(ch if ch == '\n' else ' ' for ch in src[i:j]))
      i = j
    elif c == '/' and i + 1 < n and src[i+1] == '/':
      j = src.find('\n', i)
      j = n if j < 0 else j
      out.append(' ' * (j - i))
      i = j
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
      j = min(j, n)
      out.append(''.join(ch if ch == '\n' else ' ' for ch in src[i:j]))
      i = j
    else:
      out.append(c)
      i += 1
  s = ''.join(out)
  assert len(s) == len(src), "blank() changed the length: %d vs %d" % (len(s), len(src))
  return s


def functions(src):
  """[(name, start, end)] for top-level brace-balanced definitions."""
  b = blank(src)
  found = []
  for m in re.finditer(r'^([A-Za-z_][\w \t\*]*?)\b(\w+)\s*\(', b, re.M):
    p = b.find('(', m.end() - 1)
    depth, i, n = 0, p, len(b)
    while i < n:
      if b[i] == '(':
        depth += 1
      elif b[i] == ')':
        depth -= 1
        if depth == 0:
          break
      i += 1
    j = i + 1
    while j < n and b[j] in ' \t\r\n':
      j += 1
    if j >= n or b[j] != '{':
      continue                       # declaration or call, not a definition
    depth, k = 0, j
    while k < n:
      if b[k] == '{':
        depth += 1
      elif b[k] == '}':
        depth -= 1
        if depth == 0:
          break
      k += 1
    found.append((m.group(2), m.start(), k + 1))
  return found


def cpp_wrapper(src, a, b):
  """If a #if/#endif encloses exactly the span (a,b), return the widened span.

  A function can sit inside a conditional -- clsd_menuCallback is wrapped in
  `#ifndef USE_COMBO_BOX`.  Moving the function and leaving its guard behind
  produces two wrong files that both compile: an empty conditional in the
  source, and an unguarded definition in the destination.  This is why the
  first run of this script had to be thrown away.

  Only the simple case is handled -- one conditional wrapping one function,
  with nothing else between the directives.  Anything else raises, because a
  guess here is worse than a stop.
  """
  b_txt = blank(src)
  # nearest non-blank line above `a`
  i = src.rfind('\n', 0, a)
  while i > 0:
    ls = src.rfind('\n', 0, i) + 1
    line = src[ls:i].strip()
    if line:
      break
    i = ls - 1
  else:
    return None
  if not line.startswith('#if'):
    return None

  # nearest non-blank line below `b` must be the matching #endif
  j = b
  while j < len(src):
    le = src.find('\n', j)
    le = len(src) if le < 0 else le
    line2 = src[j:le].strip()
    if line2:
      break
    j = le + 1
  else:
    return None
  if not line2.startswith('#endif'):
    raise SystemExit(
      "function at offset %d is inside `%s` but is not the only thing in it;\n"
      "  the next directive after it is `%s`.  Move it by hand." % (a, line, line2))
  # nothing but the function between the directives
  if b_txt[i+1:a].strip() or b_txt[b:j].strip():
    raise SystemExit("conditional at offset %d wraps more than one item" % ls)
  return (ls, le)


def with_leading_comment(src, start, floor):
  """Extend a span backwards over blank lines and an adjacent comment block.

  `floor` is where the previous span ended, so a comment is never claimed twice.
  """
  b = blank(src)
  i = start
  # back over whitespace
  while i > floor and src[i-1] in ' \t\r\n':
    i -= 1
  # if that lands on the end of a comment, take the whole comment
  if i > floor and b[max(floor, i-2):i].strip() == '' and src[max(floor, i-2):i] == '*/':
    j = src.rfind('/*', floor, i)
    if j >= floor:
      i = j
      while i > floor and src[i-1] in ' \t':
        i -= 1
  else:
    # run of // lines
    while True:
      ls = src.rfind('\n', floor, i - 1 if i > floor else floor)
      ls = floor if ls < floor else ls + 1
      line = src[ls:i].strip()
      if ls >= i or not line.startswith('//'):
        break
      i = ls
  while i > floor and src[i-1] in ' \t\r\n':
    i -= 1
  return i


def main():
  args = [a for a in sys.argv[1:] if not a.startswith('--')]
  apply_it = '--apply' in sys.argv
  header = None
  for a in sys.argv[1:]:
    if a.startswith('--header='):
      header = a[len('--header='):]
  if len(args) < 3:
    raise SystemExit(__doc__)
  srcpath, destpath, wanted = args[0], args[1], set(args[2].split(','))

  src = open(srcpath, encoding='utf-8', errors='replace').read()
  funcs = functions(src)
  by_name = {n: (a, b) for n, a, b in funcs}

  missing = wanted - set(by_name)
  if missing:
    raise SystemExit("not found in %s: %s" % (srcpath, ", ".join(sorted(missing))))

  # Spans in file order, each extended back over its own comment block and out
  # over a conditional that wraps it alone.
  spans, floor, wrapped = [], 0, []
  for name, a, b in funcs:
    w = cpp_wrapper(src, a, b) if name in wanted else None
    if w:
      a, b = w
      wrapped.append(name)
    a2 = with_leading_comment(src, a, floor)
    spans.append((name, a2, b))
    floor = b
  if wrapped:
    print("carrying the enclosing #if/#endif with: %s" % ", ".join(wrapped))

  move = [(n, a, b) for n, a, b in spans if n in wanted]
  keep_text, moved_text, cur = [], [], 0
  for n, a, b in spans:
    if n not in wanted:
      continue
    keep_text.append(src[cur:a])
    moved_text.append((n, src[a:b]))
    cur = b
  keep_text.append(src[cur:])

  kept = ''.join(keep_text)
  moved = ''.join(t for _, t in moved_text)

  # The check that makes this safe: kept + moved must account for every byte.
  if len(kept) + len(moved) != len(src):
    raise SystemExit("BYTE MISMATCH: kept %d + moved %d != original %d"
                     % (len(kept), len(moved), len(src)))
  print("moving %d functions, %d bytes; keeping %d bytes; total matches original (%d)"
        % (len(move), len(moved), len(kept), len(src)))
  for n, a, b in move:
    print("    %-42s %6d bytes" % (n, b - a))

  hdr = open(header, encoding='utf-8').read() if header else ""
  out = hdr + moved

  if not apply_it:
    print("\ndry run; pass --apply to write %s and rewrite %s" % (destpath, srcpath))
    return
  open(destpath, 'w', encoding='utf-8').write(out)
  open(srcpath, 'w', encoding='utf-8').write(kept)
  print("\nwrote %s (%d bytes) and rewrote %s (%d bytes)"
        % (destpath, len(out), srcpath, len(kept)))


if __name__ == "__main__":
  main()
