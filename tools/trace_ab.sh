#!/bin/bash
# Capture a deterministic log of message-window operations.
#
# snapshot_ab.sh compares the map canvas, which is the one thing the Send
# Message windows are not part of.  This drives message traffic through a
# replayed packet log instead and records what the core asks the message
# windows to do -- see src/core/util/xa_trace.h.
#
#   ./tools/trace_ab.sh <out.trace>
#
# SNAP_BIN=<path>   run a different binary (A/B a committed change)
# SNAP_DIR=<dir>    where snap.cnf lives
# TRACE_LOG=<file>  scenario to replay (default tools/trace/messages.log)
#
# The raw trace is written next to <out.trace> as <out.trace>.raw.  <out.trace>
# is the normalised form -- see tools/trace_norm.py for what is collapsed and
# what is masked, and read that before treating a clean diff as a clean result.
set -eu
OUT="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
SC="${SNAP_DIR:-$HERE/snapshot}"
SCENARIO="${TRACE_LOG:-$HERE/trace/messages.log}"
[ -f "$SC/snap.cnf" ] || { echo "no snap.cnf in $SC (set SNAP_DIR)" >&2; exit 1; }
[ -f "$SCENARIO" ]    || { echo "no scenario at $SCENARIO" >&2; exit 1; }
cd /home/aevanger/github/Astir

# A stale $OUT would be diffed against as though this run had produced it.
rm -f "$OUT" "$OUT.raw"

export XAUTHORITY="${XAUTHORITY:-/run/user/1000/xauth_ZUnYLn}" DISPLAY="${DISPLAY:-:0}"
pkill -x astir 2>/dev/null || true
waited=0
while pgrep -x astir >/dev/null; do sleep 1; waited=$((waited+1)); [ $waited -ge 10 ] && pkill -KILL -x astir; [ $waited -ge 20 ] && exit 1; done

cp "$SC/snap.cnf" ~/.astir/config/astir.cnf

# Nothing is cleared between runs on purpose.  msg_data/msg_index are static in
# db.c and are not reloaded at startup, so the message store starts empty; the
# message *log* on disk is append-only user data that never feeds back in.  If
# that ever changes, a stale store would make this trace depend on how many
# times the script has been run, so check it before trusting a clean diff.

RAW="$OUT.raw"
LOG="$(mktemp -t trace_ab.XXXXXX.log)"
BIN="${SNAP_BIN:-./src/astir}"
[ -x "$BIN" ] || { echo "not executable: $BIN" >&2; exit 1; }
echo "running $BIN  (scenario $(basename "$SCENARIO"))"

ASTIR_PERF=1 ASTIR_LOD_PX=1.0 ASTIR_ZOOMOUT=4 \
  ASTIR_REPLAY="$SCENARIO" ASTIR_TRACE="$RAW" \
  "$BIN" > "$LOG" 2>&1 &

# Wait for the replay to consume the whole file.  read_file_line() clears
# read_file at EOF and main.c prints this then, so it is a real condition and
# not a guessed duration.
w=0
until grep -qa '\[replay\] done' "$LOG" 2>/dev/null; do
  pgrep -x astir >/dev/null || { echo "exited early"; tail -5 "$LOG"; exit 1; }
  sleep 5; w=$((w+5))
  [ $w -ge 600 ] && { echo "replay never finished in 600s" >&2; tail -5 "$LOG" >&2; break; }
done

# update_messages() keeps running on its timer after the last packet, so the
# trace is still growing when the replay ends.  Wait for it to stop changing
# before cutting it off, or the tail of the file is decided by when this script
# happened to send the signal.
prev=""; stable=0
for i in $(seq 1 30); do
  cur="$(md5sum "$RAW" 2>/dev/null | cut -d' ' -f1)"
  if [ -n "$cur" ] && [ "$cur" = "$prev" ]; then
    stable=$((stable+1))
    [ $stable -ge 2 ] && break
  else
    stable=0
  fi
  prev="$cur"
  sleep 4
done
[ $stable -ge 2 ] || echo "WARNING: trace still growing when captured" >&2

kill -TERM "$(pgrep -x astir | head -1)" 2>/dev/null || true
t=0
while pgrep -x astir >/dev/null; do
  sleep 2; t=$((t+2))
  [ $t -ge 20 ] && pkill -KILL -x astir 2>/dev/null
  [ $t -ge 40 ] && break
done

[ -s "$RAW" ] || { echo "FAILED: empty trace; do not A/B this run" >&2; exit 1; }
python3 "$HERE/trace_norm.py" "$RAW" > "$OUT"
echo "captured $OUT ($(wc -l < "$RAW") raw records -> $(wc -l < "$OUT") normalised)"
