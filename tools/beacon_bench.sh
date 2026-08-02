#!/bin/bash
# A bench for the beacon schedule: beacon for real, on the air nowhere.
#
# The same bargain msg_bench.sh makes, for the other thing Astir transmits.
# Nothing pretends: the posit is composed, scheduled and written to an
# interface exactly as in production, and the interface is a pty with nothing
# on the other end.  The GPS is a pty too, fed by tools/nmea_feed.py -- a
# receiver standing still, which is the case the schedule gets wrong.
#
#   ./tools/beacon_bench.sh                    # the build in ./src
#   BEACON_BIN=~/.local/bin/astir ./tools/beacon_bench.sh   # something else
#   BEACON_SECONDS=240 ./tools/beacon_bench.sh
#
# It prints every packet keyed, then counts the posits among them.  A station
# that is not moving should emit very few; SB_POSIT_SLOW is thirty minutes, so
# over a bench of a couple of minutes the right answer is one or two.
#
# N0CALL is deliberate, as in msg_bench.sh: not a licensed callsign, so a
# mistake here cannot be taken for a real station.  The real ~/.astir is
# neither read nor written.
set -eu
cd "$(dirname "$0")/.."

BENCH="${BEACON_BENCH_DIR:-/tmp/astir-beacon-bench}"
BIN="${BEACON_BIN:-./src/astir}"
SECONDS_TO_RUN="${BEACON_SECONDS:-180}"
SCENARIO="${BEACON_SCENARIO:-corner}"

# Two ways to drive it, and they answer different questions.
#
#   corner   A car at 20 knots that goes straight, turns ninety degrees once,
#            and goes straight again.  Corner-pegging should fire ONCE, on the
#            corner, and then leave the schedule to POSIT_rate.  Nothing here is
#            random, so the count is a fact rather than a sample -- which makes
#            this the one to regress against.
#
#   parked   The field symptom: a receiver standing still, whose reported course
#            wanders because there is no real direction of travel to report.
#            Closer to what a real station does and correspondingly less tidy.
#
#   forced   The operator pressing "beacon now" with AUTOMATIC beaconing switched
#            off, and no GPS at all.  Not a schedule question: it asks whether the
#            two are separate settings, which they were not.  DISABLE_POSIT_TX:1
#            used to refuse this, so a station that did not want to announce
#            itself every half hour could not send one posit to check its own
#            transmit path.  No GPS on purpose -- a station with no receiver has
#            no clock, so a forced beacon is the ONLY posit it can ever send, and
#            it is the case most likely to be broken without anyone noticing.
case "$SCENARIO" in
  corner)
    FEED_ARGS="--knots 20 --course 0 --drift 0 --turn-at 30 --turn-to 90"
    EXPECT="one corner-pegged posit, then one about every 97s (POSIT_rate at 20kn)"
    USE_GPS=1; POSIT_TX_DISABLE=0; FORCE_BEACON=""
    ;;
  parked)
    FEED_ARGS="--knots 1.69 --course 282 --drift 18"
    EXPECT="one or two: standing still, SB_POSIT_SLOW is 30 minutes"
    USE_GPS=1; POSIT_TX_DISABLE=0; FORCE_BEACON=""
    ;;
  forced)
    FEED_ARGS=""
    EXPECT="exactly 1 -- one press, one posit, with automatic beaconing off"
    USE_GPS=0; POSIT_TX_DISABLE=1; FORCE_BEACON=1
    SECONDS_TO_RUN="${BEACON_SECONDS:-25}"   # one press at 4s; no schedule to wait for
    ;;
  *)
    echo "unknown BEACON_SCENARIO: $SCENARIO (want 'corner', 'parked' or 'forced')" >&2
    exit 2
    ;;
esac

rm -rf "$BENCH"
mkdir -p "$BENCH/home/.astir/config"

cleanup() {
  for v in SOCAT_TNC SOCAT_GPS; do
    pid="${!v:-}"
    [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT

CAP="$BENCH/keyed.txt"
: > "$CAP"

# Two ptys, each wired straight to what it needs and nothing in between.
#
# The capture is socat's own write to the file rather than `cat -v | tee`:
# stdio block-buffers on a pipe, so a bench that keys well under 4 KB shows an
# empty capture and a truncated last line, which reads exactly like a station
# that never transmitted.  It cost one wrong answer already.
socat -u pty,raw,echo=0,link="$BENCH/tnc" OPEN:"$CAP",creat,append &
SOCAT_TNC=$!
if [ "$USE_GPS" = 1 ]; then
  socat -u EXEC:"$PWD/tools/nmea_feed.py --seconds $((SECONDS_TO_RUN + 30)) $FEED_ARGS" \
           pty,raw,echo=0,link="$BENCH/gps" &
  SOCAT_GPS=$!
fi
sleep 1

# SmartBeaconing settings and units are the ones that showed the fault on a
# real station: metric, so 1.69 knots converts to 3 km/h and lands ABOVE a
# low-speed limit of 2 -- which is what puts a parked receiver into the moving
# branch where corner-pegging applies.
cat > "$BENCH/home/.astir/config/astir.cnf" <<EOF
STATION_CALLSIGN:N0CALL
STATION_LAT:3401.734N
STATION_LONG:11821.750W
STATION_GROUP:/
STATION_SYMBOL:l
STATION_MESSAGE_TYPE:=
STATION_COMMENTS:Astir beacon bench
DISABLE_TRANSMIT:0
DISABLE_POSIT_TX:$POSIT_TX_DISABLE
DISPLAY_UNITS_ENGLISH:0
POSIT_RATE:1800
SMART_BEACONING:1
SB_TURN_MIN:20
SB_TURN_SLOPE:25
SB_TURN_TIME:5
SB_POSIT_FAST:60
SB_POSIT_SLOW:30
SB_LOW_SPEED_LIMIT:2
SB_HIGH_SPEED_LIMIT:60
DEVICE0_TYPE:1
DEVICE0_NAME:$BENCH/tnc
DEVICE0_RADIO_PORT:0
DEVICE0_SPEED:9600
DEVICE0_TXMT:1
DEVICE0_ONSTARTUP:1
DEVICE0_RECONN:0
DEVICE0_INTERFACE_COMMENT:bench pty, goes nowhere
EOF

# The GPS is a second interface, and only the scheduled scenarios have one.
# 'forced' deliberately runs without: see the note on it above.
if [ "$USE_GPS" = 1 ]; then
  cat >> "$BENCH/home/.astir/config/astir.cnf" <<EOF
DEVICE1_TYPE:3
DEVICE1_NAME:$BENCH/gps
DEVICE1_SPEED:9600
DEVICE1_TXMT:0
DEVICE1_ONSTARTUP:1
DEVICE1_RECONN:0
DEVICE1_INTERFACE_COMMENT:bench GPS, standing still
EOF
fi

echo "=============================================================="
echo " binary   : $BIN ($($BIN -V 2>&1 | head -1))"
echo " scenario : $SCENARIO -- $FEED_ARGS"
echo " duration : ${SECONDS_TO_RUN}s of 1 Hz fixes"
echo " interface: $BENCH/tnc -- a pty, reaching no radio and no APRS-IS"
echo "=============================================================="

# dbus-run-session, and it is not optional.
#
# Astir is a GtkApplication with a unique id, so a second copy launched while
# one is already running registers as a REMOTE instance, activates the first
# one's window and exits 0 straight away -- but only after the core has already
# started and opened the interfaces.  What that looks like from here is a bench
# that keys twenty-two bytes and stops in the middle of the UNPROTO line, which
# reads exactly like a truncated write.  Uniqueness is arbitrated over the
# session bus, so its own bus makes it its own primary instance.
#
# It also stops the bench from reaching into a station that is on the air:
# without this, running this script pokes the interfaces of the real Astir.
# env, not a bare assignment prefix: bash decides what is an assignment before
# it expands anything, so a ${...:+VAR=1} in that position is run as a command
# and the bench reports zero posits for a reason that has nothing to do with
# beaconing.  env takes them as ordinary arguments and gets it right.
set +e
env ASTIR_DATA_BASE="$PWD/artifacts/datadir" \
    ASTIR_USER_BASE="$BENCH/home" \
    ${FORCE_BEACON:+ASTIR_GTK4_BEACON_NOW=1} \
  timeout --signal=TERM "$SECONDS_TO_RUN" \
    dbus-run-session -- "$BIN" >"$BENCH/astir.log" 2>&1
set -e

sleep 1

echo "----- everything keyed -----"
cat -v "$CAP"
echo "----------------------------"

# Count posits by their APRS position, not by a callsign and not by line.
#
# A serial TNC is told the callsign once with a MYCALL command and then sent
# bare payloads, so nothing in this stream looks like "N0CALL>APZ225:" -- and
# none of it is newline-terminated either, so the whole bench arrives as a
# single line.  Matching the data-type identifier and the degrees-minutes that
# must follow it counts the beacons and skips the MYCALL and UNPROTO lines that
# precede each one.
posits=$(grep -o -E '[=!][0-9]{4}\.[0-9]{2}[NS]' "$CAP" | wc -l)

echo
echo "=============================================================="
echo " posits keyed in ${SECONDS_TO_RUN}s: ${posits}"
echo " expected: ${EXPECT}"
echo "=============================================================="
