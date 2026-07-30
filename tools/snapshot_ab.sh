#!/bin/bash
# Capture Xastir's own rendered map (pixmap_final) via its snapshot facility.
# Independent of window stacking, which is what made the screenshot A/B useless.
set -eu
OUT="$1"
# Where snap.cnf lives.  It was a hardcoded session scratchpad, which is fine
# until that session's directory is cleaned up and the script then silently
# copies nothing, leaving whatever config was already in place -- an A/B against
# an unknown configuration.  Override with SNAP_DIR; the config must exist.
# Resolved before the cd, so it does not depend on where this was invoked from.
SC="$(cd "$(dirname "$0")" && pwd)/snapshot"
SC="${SNAP_DIR:-$SC}"
[ -f "$SC/snap.cnf" ] || { echo "no snap.cnf in $SC (set SNAP_DIR)" >&2; exit 1; }
cd /home/aevanger/github/Xastir
# A stale $OUT is worse than no $OUT: cmp would compare a previous run's capture
# and report a clean A/B.  Remove it, and fail loudly if nothing replaces it.
rm -f "$OUT"
export XAUTHORITY="${XAUTHORITY:-/run/user/1000/xauth_ZUnYLn}" DISPLAY="${DISPLAY:-:0}"
pkill -x xastir 2>/dev/null || true
waited=0
while pgrep -x xastir >/dev/null; do sleep 1; waited=$((waited+1)); [ $waited -ge 10 ] && pkill -KILL -x xastir; [ $waited -ge 20 ] && exit 1; done
# Let the X server reclaim the previous instance's colours before starting the
# next one.  Back-to-back runs otherwise contend for the shared colormap:
# XAllocColor returns approximate matches instead of the exact entries, and the
# capture comes out with a couple of hundred extra near-black shades.  See the
# colour-count check at the end for how that presented.
sleep 5
cp "$SC/snap.cnf" ~/.xastir/config/xastir.cnf
rm -f ~/.xastir/tmp/snapshot.xpm
LOG="$(mktemp -t snapshot_ab.XXXXXX.log)"
# Which binary to run.  Overridable so that two builds can be compared without
# rebuilding between captures -- e.g. against a git worktree at an older commit,
# which is the only way to A/B a change that is already committed.  Whatever
# binary is named must come from a tree configured the same way; diff its
# config.h against this one before believing the result.
BIN="${SNAP_BIN:-./src/xastir}"
[ -x "$BIN" ] || { echo "not executable: $BIN" >&2; exit 1; }
echo "running $BIN"
XASTIR_PERF=1 XASTIR_LOD_PX=1.0 XASTIR_ZOOMOUT=4 "$BIN" > "$LOG" 2>&1 &
until grep -qa 'holding at final scale' "$LOG" 2>/dev/null; do
  pgrep -x xastir >/dev/null || { echo "exited early"; tail -3 "$LOG"; exit 1; }
  sleep 5
done
# Take a snapshot written AFTER the render settled, and prove it settled.
#
# The first snapshot fires the moment snapshots are enabled, which is before the
# maps have finished drawing, so it can catch a half-rendered frame -- features
# simply missing, reading as background.  That is indistinguishable from a real
# rendering regression, and it produced exactly that false positive once: a
# capture 17.6% different from its own baseline, with identical geometry
# counters, blamed on a change that runtime probes then showed was equivalent.
#
# So: discard everything written up to now, then require two consecutive fresh
# snapshots to be byte-identical.  One fresh snapshot only proves it was taken
# after the hold message; two identical ones prove nothing is still being drawn.
snap=~/.xastir/tmp/snapshot.xpm
wait_fresh() {           # $1 = destination; waits for a newly written snapshot
  rm -f "$snap"
  local w=0
  until [ -s "$snap" ]; do
    sleep 5; w=$((w+5))
    [ $w -ge 180 ] && { echo "no snapshot in 180s" >&2; return 1; }
  done
  sleep 2               # let the writer finish the file
  cp "$snap" "$1"
}
TMP_A="$(mktemp -t snap_a.XXXXXX.xpm)"
TMP_B="$(mktemp -t snap_b.XXXXXX.xpm)"
settled=0
for attempt in 1 2 3; do
  wait_fresh "$TMP_A" || break
  wait_fresh "$TMP_B" || break
  if cmp -s "$TMP_A" "$TMP_B"; then settled=1; break; fi
  echo "render not settled yet (attempt $attempt); retrying" >&2
done
if [ "$settled" = 1 ]; then
  cp "$TMP_B" "$OUT"
  echo "captured $OUT ($(stat -c%s "$OUT") bytes, render settled)"
else
  echo "FAILED: render never settled; refusing to emit a capture" >&2
fi
rm -f "$TMP_A" "$TMP_B"
kill -TERM "$(pgrep -x xastir | head -1)" 2>/dev/null || true
# Bounded.  Xastir does not always act on SIGTERM -- one run sat for six minutes
# after the snapshot was already captured -- and an unbounded wait here stalls
# the whole verification loop for a process whose output is already on disk.
t=0
while pgrep -x xastir >/dev/null; do
  sleep 2; t=$((t+2))
  [ $t -ge 20 ] && pkill -KILL -x xastir 2>/dev/null
  [ $t -ge 40 ] && break
done
[ -s "$OUT" ] || { echo "FAILED: no snapshot captured; do not A/B this run" >&2; exit 1; }

# How many colours the frame ended up with.  Always printed, because it is the
# cheapest single number that says whether the capture is trustworthy, and both
# ways this harness has produced a confident wrong answer showed up in it.
#
#   792 -- the render was captured half-drawn (the race fixed above)
#   822 -- correct, for tools/snapshot/snap.cnf
#   960+ -- the colormap was still held by the previous Xastir when this one
#           started, so XAllocColor returned approximations.  Roughly 180
#           near-black and near-grey shades appear that are not in a good
#           capture, and consecutive bad runs differ from each other by a
#           handful, which a correct capture never does.
#
# That third case cost a bisect: a change was blamed for a 142-colour
# difference, reproduced twice, and turned out to be innocent -- the two "clean"
# comparison runs had simply each followed a five-minute rebuild, which is long
# enough for the colormap to settle.  Override with SNAP_EXPECT_COLORS, or set
# it to `any` when a change is *meant* to alter the palette.
expect="${SNAP_EXPECT_COLORS:-822}"
got="$(sed -n '3p' "$OUT" | tr -dc '0-9 ' | awk '{print $3}')"
echo "colours: $got"
if [ "$expect" != "any" ] && [ -n "$got" ] && [ "$got" != "$expect" ]; then
  echo "" >&2
  echo "WARNING: $got colours, expected $expect." >&2
  echo "  Either this change really does alter the palette, or the capture hit" >&2
  echo "  the colormap-contention flake.  Re-run it once with a minute's gap" >&2
  echo "  before believing a difference -- and do not start a bisect until a" >&2
  echo "  spaced-out re-run reproduces it." >&2
  echo "  SNAP_EXPECT_COLORS=$got to accept this as the new baseline." >&2
  echo "" >&2
fi
