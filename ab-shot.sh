#!/bin/bash
# Capture a screenshot of xastir at a known scale, with a given LOD threshold.
#
#   ./ab-shot.sh <lod_px> <zoomout_steps> <outfile>
#
# lod_px 0 disables culling entirely, reproducing stock behaviour, so the same
# binary can produce both sides of an A/B comparison.
set -eu
LOD="$1"; ZOOM="$2"; OUT="$3"
cd "$(dirname "$0")"
export XAUTHORITY=/run/user/1000/xauth_ZUnYLn DISPLAY=:0

TAG="lod${LOD}-z${ZOOM}"
pkill -x xastir 2>/dev/null || true
sleep 2

# Xastir saves the current zoom/centre on exit, so consecutive runs would each
# start where the previous one finished -- the two halves of an A/B would then
# be at different scales.  Pin a baseline config and restore it every run.
BASE="$(pwd)/ab-baseline-xastir.cnf"
if [ ! -f "$BASE" ]; then
  cp ~/.xastir/config/xastir.cnf "$BASE"
  echo "saved baseline view -> $BASE"
fi
cp "$BASE" ~/.xastir/config/xastir.cnf

XASTIR_PERF=1 XASTIR_LOD_PX="$LOD" XASTIR_ZOOMOUT="$ZOOM" \
  ./src/xastir > "shot-${TAG}.log" 2>&1 &

until grep -qa 'Done with WX Alert log files' "shot-${TAG}.log" 2>/dev/null; do
  pgrep -x xastir >/dev/null || { echo "xastir exited early"; exit 1; }
  sleep 3
done

if [ "$ZOOM" -gt 0 ]; then
  until grep -qa 'holding at final scale' "shot-${TAG}.log" 2>/dev/null; do
    pgrep -x xastir >/dev/null || { echo "xastir exited early"; exit 1; }
    sleep 5
  done
fi
# Wait for rendering to quiesce rather than guessing a fixed delay.  At state
# zoom auto-maps pulls in 63 county files and a single frame can take many
# seconds; a fixed sleep captured a half-drawn black canvas.  Each completed
# frame prints one "[perf] create_image" line, so wait until no new line has
# appeared for 20 s.
prev=-1
waited=0
MAXWAIT=300
while [ "$waited" -lt "$MAXWAIT" ]; do
  # grep -c prints 0 AND exits nonzero when there is no match, so a
  # "|| echo 0" fallback yields the two-line string "0\n0" and breaks [ -eq ].
  n=$(grep -ac '^\[perf\] create_image' "shot-${TAG}.log" 2>/dev/null || true)
  n=${n:-0}
  if [ "$n" -eq "$prev" ] && [ "$n" -gt 0 ]; then break; fi
  prev="$n"
  sleep 20
  waited=$((waited+20))
done
if [ "$n" -eq 0 ]; then
  echo "  WARNING: no frame completed in ${MAXWAIT}s -- capturing anyway."
  echo "  (create_image has several early returns that emit no perf line, so"
  echo "   zero frames does not necessarily mean nothing was drawn.)"
else
  echo "  render quiesced after $n frame(s)"
fi

timeout 60 spectacle -b -n -o "$OUT" >/dev/null 2>&1
echo "captured $OUT (LOD=$LOD zoomout=$ZOOM)"
grep -a '^\[perf\] create_image' "shot-${TAG}.log" | tail -1

kill -TERM "$(pgrep -x xastir | head -1)" 2>/dev/null || true
until ! pgrep -x xastir >/dev/null; do sleep 2; done
