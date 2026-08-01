#!/usr/bin/env python3
"""
A GPS that is standing still, which is the hard case for SmartBeaconing(tm).

A receiver that is not moving still reports a course, and that course wanders:
with no real direction of travel the fix-to-fix noise is all there is to derive
one from.  It does not jump about at random either -- it drifts, because
consecutive fixes share most of their error.  That drift is the whole point of
this feeder.  Corner-pegging asks how far the heading has turned SINCE THE LAST
BEACON, and a drifting course answers that question very differently depending
on which heading it is measured from:

  - from a heading fixed once at startup, the drift walks away and STAYS away,
    so every fix looks like a corner and the station beacons continuously;
  - from the heading at the last beacon, the drift has to cover the threshold
    again from where it now is, which takes a while.

So the same stream of sentences separates a scheduler that re-reads its
reference from one that does not.  Speeds are just above the low-speed limit on
purpose: below it corner-pegging is switched off entirely and the bug is hidden
rather than fixed.

Deterministic -- a fixed seed, and no wall clock in the sentences except an
optional start time -- because a bench whose baseline moves cannot show that
anything changed.

  ./tools/nmea_feed.py > /dev/pts/N
"""

import argparse
import math
import random
import sys
import time


def checksum(body):
    """NMEA checksum: XOR of everything between the '$' and the '*'."""
    c = 0
    for ch in body:
        c ^= ord(ch)
    return "%02X" % c


def sentence(body):
    return "$%s*%s\r\n" % (body, checksum(body))


def gprmc(hhmmss, lat, ns, lon, ew, knots, course, ddmmyy):
    return sentence(
        "GPRMC,%s,A,%s,%s,%s,%s,%.2f,%.2f,%s,,,A"
        % (hhmmss, lat, ns, lon, ew, knots, course, ddmmyy)
    )


def gpgga(hhmmss, lat, ns, lon, ew):
    return sentence(
        "GPGGA,%s,%s,%s,%s,%s,1,04,3.3,32.0,M,-33.8,M,,0000"
        % (hhmmss, lat, ns, lon, ew)
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--seconds", type=int, default=180,
                   help="how many fixes to emit, one per second")
    p.add_argument("--lat", default="3401.7348")
    p.add_argument("--ns", default="N")
    p.add_argument("--lon", default="11821.7783")
    p.add_argument("--ew", default="W")
    p.add_argument("--knots", type=float, default=1.69,
                   help="speed; the default is GPS noise, not travel, but is "
                        "still above a low-speed limit of 2 km/h")
    p.add_argument("--course", type=float, default=282.0,
                   help="the heading to start drifting from")
    p.add_argument("--drift", type=float, default=18.0,
                   help="standard deviation of the per-fix course change")
    p.add_argument("--turn-at", type=int, default=None, metavar="SECONDS",
                   help="hold the starting course until here, then turn once "
                        "and hold the new one.  With --drift 0 this is the "
                        "scenario that isolates the reference heading: a "
                        "corner is ONE corner, and a scheduler measuring from "
                        "the last beacon falls quiet after it while one "
                        "measuring from a heading fixed at startup goes on "
                        "seeing the same corner for as long as the new course "
                        "is held")
    p.add_argument("--turn-to", type=float, default=90.0, metavar="DEGREES",
                   help="how far --turn-at turns, in degrees")
    p.add_argument("--seed", type=int, default=20260801)
    p.add_argument("--rate", type=float, default=1.0,
                   help="seconds between fixes; the real thing is 1 Hz")
    args = p.parse_args()

    rng = random.Random(args.seed)
    course = args.course

    for i in range(args.seconds):
        # A random WALK, not a random draw: each fix moves the course a little
        # from where the last one left it.  This is what a stationary receiver
        # actually does, and it is what makes the reference heading matter.
        if args.drift:
            course = (course + rng.gauss(0.0, args.drift)) % 360.0

        # One corner, taken at a single fix and then held.
        if args.turn_at is not None and i >= args.turn_at:
            course = (args.course + args.turn_to) % 360.0

        # Speed wobbles around the given value for the same reason the course
        # does.  Never negative.
        knots = max(0.0, args.knots + rng.gauss(0.0, 0.25))

        # A clock that starts at 00:00:00 and counts the fixes, so two runs of
        # this feeder emit byte-identical streams.
        hhmmss = "%02d%02d%02d.000" % (i // 3600, (i // 60) % 60, i % 60)

        sys.stdout.write(gpgga(hhmmss, args.lat, args.ns, args.lon, args.ew))
        sys.stdout.write(
            gprmc(hhmmss, args.lat, args.ns, args.lon, args.ew,
                  knots, course, "010826")
        )
        sys.stdout.flush()
        time.sleep(args.rate)


if __name__ == "__main__":
    main()
