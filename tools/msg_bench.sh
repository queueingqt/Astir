#!/bin/bash
# A bench for the message sidebar: send for real, on the air nowhere.
#
# Astir has no "pretend to send" mode and should not grow one.  A flag that
# skips the transmit is a flag that can be believed when it is not set, and the
# failure is putting traffic on the air under a callsign that did not expect
# to.  So this does the opposite: everything is real -- the message is queued,
# formatted and written to an interface exactly as in production -- and the
# interface is a pty with nothing on the other end but `cat`.
#
# What you get is the actual bytes Astir would have keyed, printed in a
# terminal, and a sidebar you can type into.
#
#   ./tools/msg_bench.sh
#
# Ctrl-C to stop.  Everything lives in a scratch directory; the real ~/.astir
# is not read or written.
set -eu
cd "$(dirname "$0")/.."

BENCH="${1:-/tmp/astir-msg-bench}"
mkdir -p "$BENCH/home/.astir/config"

cleanup() {
  [ -n "${SOCAT_PID:-}" ] && kill "$SOCAT_PID" 2>/dev/null || true
  [ -n "${CAT_PID:-}"   ] && kill "$CAT_PID"   2>/dev/null || true
}
trap cleanup EXIT

# Two ends of one pty.  Astir opens the first; the second is watched here.
socat pty,raw,echo=0,link="$BENCH/tnc" pty,raw,echo=0,link="$BENCH/watch" &
SOCAT_PID=$!
sleep 1

# N0CALL is deliberate: it is not a licensed callsign, so a mistake here cannot
# be mistaken for a real station.  The whole point is that it reaches no radio.
cat > "$BENCH/home/.astir/config/astir.cnf" <<EOF
STATION_CALLSIGN:N0CALL
STATION_LAT:4736.372N
STATION_LONG:12219.926W
STATION_GROUP:/
STATION_SYMBOL:x
STATION_COMMENTS:Astir message bench
DISABLE_TRANSMIT:0
DEVICE0_TYPE:1
DEVICE0_NAME:$BENCH/tnc
DEVICE0_RADIO_PORT:0
DEVICE0_SPEED:9600
DEVICE0_TXMT:1
DEVICE0_ONSTARTUP:1
DEVICE0_RECONN:0
DEVICE0_INTERFACE_COMMENT:bench pty, goes nowhere
EOF

# Some traffic to reply to, if no log was supplied.
REPLAY="${ASTIR_BENCH_REPLAY:-$BENCH/replay.log}"
if [ ! -f "$REPLAY" ]; then
  cat > "$REPLAY" <<'EOF'
K6ABC>APRS,TCPIP*:!4738.00N/12218.00W>Bench station
W7XYZ>APRS,TCPIP*:!4736.50N/12220.00W>Bench station
K6ABC>APRS,TCPIP*::N0CALL   :Are you receiving me?{001
K6ABC>APRS,TCPIP*::N0CALL   :Second one, so there is a thread{002
W7XYZ>APRS,TCPIP*::N0CALL   :Different correspondent{007
EOF
fi

echo "=============================================================="
echo " Anything Astir transmits appears below.  Nothing reaches a"
echo " radio or APRS-IS: the interface is $BENCH/tnc,"
echo " a pty with no other end but this terminal."
echo "=============================================================="
cat -v "$BENCH/watch" &
CAT_PID=$!

ASTIR_DATA_BASE="$PWD/artifacts/datadir" \
ASTIR_USER_BASE="$BENCH/home" \
ASTIR_REPLAY="$REPLAY" \
ASTIR_GTK4_SHOW_MESSAGES=1 \
./src/astir
