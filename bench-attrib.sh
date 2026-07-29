#!/bin/bash
# Run xastir at a known scale and collect the perf breakdown.  No screenshot,
# so it does not care whether the session is locked or the screen has blanked.
#
#   ./bench-attrib.sh <lod_px> <zoomout_steps> <tag> [extra env assignments...]
#
# Example:  ./bench-attrib.sh 1.0 4 attrib XASTIR_FORCE_INDEX=1
set -eu
LOD="$1"; ZOOM="$2"; TAG="$3"; shift 3
cd "$(dirname "$0")"
export XAUTHORITY=/run/user/1000/xauth_ZUnYLn DISPLAY=:0

LOG="bench-${TAG}.log"
pkill -x xastir 2>/dev/null || true
until ! pgrep -x xastir >/dev/null; do sleep 1; done

# Xastir saves zoom/centre on exit, so consecutive runs would each start where
# the previous finished.  Pin the same baseline view every run.
BASE="$(pwd)/ab-baseline-xastir.cnf"
[ -f "$BASE" ] || { echo "missing $BASE"; exit 1; }
cp "$BASE" ~/.xastir/config/xastir.cnf

env XASTIR_PERF=1 XASTIR_LOD_PX="$LOD" XASTIR_ZOOMOUT="$ZOOM" "$@" \
  ./src/xastir > "$LOG" 2>&1 &

# Startup
waited=0
until grep -qa 'Done with WX Alert log files' "$LOG" 2>/dev/null; do
  pgrep -x xastir >/dev/null || { echo "xastir exited early"; tail -5 "$LOG"; exit 1; }
  sleep 3; waited=$((waited+3))
  [ "$waited" -lt 180 ] || { echo "startup timed out"; exit 1; }
done

if [ "$ZOOM" -gt 0 ]; then
  waited=0
  until grep -qa 'holding at final scale' "$LOG" 2>/dev/null; do
    pgrep -x xastir >/dev/null || { echo "xastir exited early"; exit 1; }
    sleep 5; waited=$((waited+5))
    [ "$waited" -lt 300 ] || { echo "never reached final scale"; break; }
  done
fi

# Wait for rendering to quiesce.  grep -c prints 0 AND exits nonzero with no
# match, so guard with `|| true` rather than `|| echo 0` (which yields "0\n0").
prev=-1; waited=0; MAXWAIT=420; n=0
while [ "$waited" -lt "$MAXWAIT" ]; do
  n=$(grep -ac '^\[perf\] create_image' "$LOG" 2>/dev/null || true)
  n=${n:-0}
  if [ "$n" -eq "$prev" ] && [ "$n" -gt 0 ]; then break; fi
  prev="$n"
  sleep 20; waited=$((waited+20))
done
echo "== $TAG (lod=$LOD zoomout=$ZOOM $*) : $n frame(s) =="

kill -TERM "$(pgrep -x xastir | head -1)" 2>/dev/null || true
until ! pgrep -x xastir >/dev/null; do sleep 2; done

grep -a '^\[perf\]' "$LOG" || echo "  (no perf lines)"
echo "--- session summary ---"
sed -n '/=== xastir perf summary ===/,/^===========/p' "$LOG" || true
