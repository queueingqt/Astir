#!/bin/bash
# Profile astir's steady-state rendering with perf.
#
#   ./bench-perf.sh <lod_px> <zoomout_steps> <seconds>
#
# perf is attached only AFTER the view has settled at the final scale, so the
# profile covers steady-state redraws rather than startup and index building.
set -eu
LOD="$1"; ZOOM="$2"; SECS="$3"
cd "$(dirname "$0")/.."
export XAUTHORITY=/run/user/1000/xauth_ZUnYLn DISPLAY=:0

LOG="bench-perf.log"
# Bounded: astir does not reliably die on SIGTERM.  See bench-attrib.sh.
pkill -x astir 2>/dev/null || true
waited=0
while pgrep -x astir >/dev/null; do
  sleep 1; waited=$((waited+1))
  [ "$waited" -eq 10 ] && pkill -KILL -x astir 2>/dev/null || true
  if [ "$waited" -ge 20 ]; then
    echo "a previous astir will not die (pid $(pgrep -x astir | head -1)); aborting" >&2
    exit 1
  fi
done

cp "$(pwd)/ab-baseline-astir.cnf" ~/.astir/config/astir.cnf

ASTIR_PERF=1 ASTIR_LOD_PX="$LOD" ASTIR_ZOOMOUT="$ZOOM" \
  ./src/astir > "$LOG" 2>&1 &

waited=0
until grep -qa 'Done with WX Alert log files' "$LOG" 2>/dev/null; do
  pgrep -x astir >/dev/null || { echo "exited early"; tail -5 "$LOG"; exit 1; }
  sleep 3; waited=$((waited+3))
  [ "$waited" -lt 180 ] || { echo "startup timed out"; exit 1; }
done

if [ "$ZOOM" -gt 0 ]; then
  waited=0
  until grep -qa 'holding at final scale' "$LOG" 2>/dev/null; do
    pgrep -x astir >/dev/null || { echo "exited early"; exit 1; }
    sleep 5; waited=$((waited+5))
    [ "$waited" -lt 300 ] || { echo "never reached final scale"; break; }
  done
fi

PID="$(pgrep -x astir | head -1)"
echo "attaching perf to pid $PID for ${SECS}s"
perf record -g --call-graph=fp -F 299 -o perf.data -p "$PID" -- sleep "$SECS" \
  >/dev/null 2>&1 || echo "perf record returned $?"

kill -TERM "$PID" 2>/dev/null || true
until ! pgrep -x astir >/dev/null; do sleep 2; done

echo "=== frames captured during profile ==="
grep -a '^\[perf\] create_image' "$LOG" | tail -6
