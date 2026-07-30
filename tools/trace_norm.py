#!/usr/bin/env python3
"""Normalise an xa_trace log so two runs of the same build compare equal.

Reads the raw trace on argv[1], writes the normalised form to stdout, and puts
a summary of everything it removed or masked on stderr.  Nothing is dropped
silently: a normaliser that quietly deletes the difference you were looking for
is worse than no harness, because the diff comes back clean.

Two things in the raw trace vary between runs of the same binary.

1.  How many times a window was re-rendered.

    update_messages() clears the window and rebuilds it from the message store
    on every call, and it is called on a timer as well as on message arrival.
    So the same window state is emitted over and over, and the count depends on
    how long the run took.  What is deterministic is the *sequence of distinct
    states* the window passed through, so consecutive identical blocks for the
    same window collapse to one.

    This is lossy in one specific way: it cannot see a change that only alters
    how often a window is redrawn.  That is deliberate -- the redraw count is
    not deterministic in the first place, so no harness here can check it.

2.  The clock in the rendered message line.

    db.c composes each line as "MM/DD HH:MM callsign>text" from the message's
    packet_time, which is the wall clock at reception (db.c: "Create a
    timestamp from the current time").  Two runs a minute apart differ.  The
    field is masked to NN/NN NN:NN.

    So the timestamp *values* are not covered.  The format still is: the mask
    only matches that exact shape, a change to it stops matching and shows up
    as a difference.  The character count is covered too, because the highlight
    offsets in the following records are absolute positions computed from it.
"""
import sys, re, collections

# Only inside the quoted text of an insert, and only the exact leading shape
# db.c writes.  Anchored so it cannot eat a timestamp that is part of a message
# body, which would hide a real change.
TIMESTAMP = re.compile(r'(text=")(\d\d/\d\d \d\d:\d\d)')

BEGIN = re.compile(r'^msg_render_begin win=(\d+)$')
END   = re.compile(r'^msg_render_end win=(\d+)$')


def main():
  if len(sys.argv) < 2:
    raise SystemExit("usage: trace_norm.py <raw.trace>")

  raw = open(sys.argv[1], encoding='utf-8', errors='replace').read().splitlines()

  masked = 0
  lines = []
  for ln in raw:
    ln, n = TIMESTAMP.subn(r'\1NN/NN NN:NN', ln)
    masked += n
    lines.append(ln)

  out = []
  last_block = {}         # win -> the last block emitted for that window
  dropped_blocks = collections.Counter()
  kept_blocks = collections.Counter()
  unterminated = 0

  i = 0
  n = len(lines)
  while i < n:
    m = BEGIN.match(lines[i])
    if not m:
      out.append(lines[i])
      i += 1
      continue

    win = m.group(1)
    # Collect through the matching end.  Records for other windows can appear
    # in between only if two threads render at once; keep them in the block
    # rather than reordering, so an interleaving change stays visible.
    j = i
    block = []
    while j < n:
      block.append(lines[j])
      e = END.match(lines[j])
      if e and e.group(1) == win:
        break
      j += 1

    if j >= n:
      # Ran off the end: the process was stopped mid-render.  Emit it and say
      # so -- silently dropping the tail would make a truncated run look clean.
      unterminated += 1
      out.extend(block)
      break

    key = "\n".join(block)
    if last_block.get(win) == key:
      dropped_blocks[win] += 1
    else:
      last_block[win] = key
      kept_blocks[win] += 1
      out.extend(block)
    i = j + 1

  sys.stdout.write("\n".join(out) + ("\n" if out else ""))

  ops = collections.Counter(ln.split(None, 1)[0] for ln in raw if ln.strip())
  print("trace_norm: %d raw records -> %d" % (len(raw), len(out)), file=sys.stderr)
  print("  timestamps masked : %d" % masked, file=sys.stderr)
  print("  render blocks     : kept %d, dropped as consecutive duplicates %d"
        % (sum(kept_blocks.values()), sum(dropped_blocks.values())), file=sys.stderr)
  for w in sorted(kept_blocks | dropped_blocks):
    print("      win %s: kept %d, dropped %d"
          % (w, kept_blocks[w], dropped_blocks[w]), file=sys.stderr)
  if unterminated:
    print("  WARNING: %d unterminated render block(s) -- run was cut short"
          % unterminated, file=sys.stderr)
  # Coverage, measured rather than assumed.  An operation with no records was
  # not exercised by this scenario, and a change to it is unverified whatever
  # the diff says.
  print("  operations seen:", file=sys.stderr)
  for op, c in sorted(ops.items()):
    print("      %-14s %d" % (op, c), file=sys.stderr)


if __name__ == "__main__":
  main()
