#!/bin/bash
# Does label collision actually happen, and does the placer handle it?
#
# tools/label_place_test.sh unit-tests the placer against synthetic boxes.  This
# is the other half: it renders the real replay through the real drawing path
# and asserts that crowding occurred AND was resolved.
#
# It exists because the regression render was quietly proving nothing.  The
# replay used to hold eight stations spread across the Los Angeles basin, so
# every label fit -- "8 of 8 placed" -- and comparing renders against it could
# only ever show that nothing had broken.  It could not show that a crowd was
# handled, because there was never a crowd.  A screenful of overlapping weather
# labels shipped underneath a test that was passing.
#
# No stored image.  A count is reproducible on any machine and survives between
# sessions; a reference PNG is neither.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/.."
ASTIR="${ASTIR:-$ROOT/src/astir}"
REPLAY="$HERE/trace/stations-la.log"

# Its own user directory, so the run cannot read or write anybody's settings --
# and in particular cannot save a position over theirs on the way out.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/.astir/config"
cat > "$TMP/.astir/config/astir.cnf" <<EOF
SCREEN_LAT:20152269
SCREEN_LONG:22192914
SCREEN_ZOOM:1200
STATION_CALLSIGN:N0CALL
EOF

out="$(ASTIR_USER_BASE="$TMP" \
      ASTIR_DATA_BASE="${ASTIR_DATA_BASE:-$ROOT/artifacts/datadir}" \
      ASTIR_DEBUG=16 \
      ASTIR_GTK4_SCALE=1200 \
      ASTIR_GTK4_RENDER_FRAMES=2 \
      ASTIR_REPLAY="$REPLAY" \
      ASTIR_GTK4_RENDER_TO="$TMP/out.png" \
      "$ASTIR" 2>&1 | grep -E '^station labels:' | tail -1)"

if [ -z "$out" ]; then
  echo "FAILED: the render reported no station labels at all"
  exit 1
fi

placed="$(echo "$out"  | sed -E 's/.*: ([0-9]+) of ([0-9]+).*/\1/')"
offered="$(echo "$out" | sed -E 's/.*: ([0-9]+) of ([0-9]+).*/\2/')"
echo "$out"

fail=0

# EVERY label the replay should produce must reach the placer.
#
# The replay holds 21 stations, 8 of them weather.  That is 21 callsigns, plus a
# wind line and a temperature line for each weather station: 21 + 8 + 8 = 37.
#
# An exact number rather than a comfortable margin, because this is the check
# that catches text bypassing the placer entirely -- which is the bug that
# shipped.  Reverting one of the ten label_submit_styled() calls to a direct
# draw drops the count by eight, and a loose threshold would wave that through.
# If the replay gains stations this number goes up and must be updated; that is
# the intended cost of an exact expectation.
if [ "$offered" -lt 37 ]; then
  echo "FAILED: $offered labels offered, expected at least 37"
  echo "        Some station text is being drawn without going through the"
  echo "        placer, or the replay has lost stations."
  fail=1
fi

# Something has to be rejected, or collision is not being exercised.
if [ "$placed" -ge "$offered" ]; then
  echo "FAILED: $placed of $offered placed -- nothing collided, so nothing was tested"
  fail=1
fi

# And something has to be drawn.  A placer that rejects everything would satisfy
# the check above and be useless.
if [ "$placed" -lt 5 ]; then
  echo "FAILED: only $placed labels placed; the map would be effectively unlabelled"
  fail=1
fi

if [ "$fail" -eq 0 ]; then
  echo "LABEL REGRESSION PASSED  ($placed of $offered placed: crowded, and resolved)"
fi
exit "$fail"
