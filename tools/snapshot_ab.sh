#!/bin/bash
# Capture Xastir's own rendered map (pixmap_final) via its snapshot facility.
# Independent of window stacking, which is what made the screenshot A/B useless.
set -eu
OUT="$1"
cd /home/aevanger/github/Xastir
SC=/tmp/claude-1000/-home-aevanger-github-Xastir/6e7addb3-47dc-41db-90c4-91868a2d3959/scratchpad
export XAUTHORITY=/run/user/1000/xauth_ZUnYLn DISPLAY=:0
pkill -x xastir 2>/dev/null || true
waited=0
while pgrep -x xastir >/dev/null; do sleep 1; waited=$((waited+1)); [ $waited -ge 10 ] && pkill -KILL -x xastir; [ $waited -ge 20 ] && exit 1; done
cp "$SC/snap.cnf" ~/.xastir/config/xastir.cnf
rm -f ~/.xastir/tmp/snapshot.xpm
LOG="$SC/snap-run.log"
XASTIR_PERF=1 XASTIR_LOD_PX=1.0 XASTIR_ZOOMOUT=4 ./src/xastir > "$LOG" 2>&1 &
until grep -qa 'holding at final scale' "$LOG" 2>/dev/null; do
  pgrep -x xastir >/dev/null || { echo "exited early"; tail -3 "$LOG"; exit 1; }
  sleep 5
done
# wait for a snapshot to appear (interval is 1 min; the first fires at once)
w=0
until [ -s ~/.xastir/tmp/snapshot.xpm ]; do sleep 5; w=$((w+5)); [ $w -ge 180 ] && { echo "no snapshot in 180s"; break; }; done
if [ -s ~/.xastir/tmp/snapshot.xpm ]; then cp ~/.xastir/tmp/snapshot.xpm "$OUT"; echo "captured $OUT ($(stat -c%s "$OUT") bytes)"; fi
kill -TERM "$(pgrep -x xastir | head -1)" 2>/dev/null || true
until ! pgrep -x xastir >/dev/null; do sleep 2; done
