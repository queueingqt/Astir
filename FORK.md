# Astir is a fork of Xastir

Astir began as [Xastir](https://xastir.org), and everything good about it that
predates this fork was written by the Xastir Group and by Frank Giannandrea,
who started it in 1999. The copyright notices in the source say so and are left
alone deliberately.

This is a **hard fork**, not a newer version of Xastir. No merge back is
planned in either direction.

## Why it forked

Xastir is built around X11 and Motif, and the two are not separable from it —
the toolkit reached into the APRS parser, the shapefile reader and the map
drivers. The work here extracted a core that links with no front end at all,
put every drawing operation behind one interface, and wrote a second front end
against it in GTK4. That is a different program with the same purpose, and
carrying Xastir's name for it would misrepresent both projects.

The `X` was the first thing to go. X + ASTIR was X11 plus *Amateur Station
Tracking and Information Reporting*; the core now links three backends and
privileges none of them, so the prefix described history rather than software.
ASTIR is the original acronym and still says exactly what this does.

## Astir and Xastir can be installed at the same time

This is a requirement, not an aspiration. A user may have both, and a resource
read from the wrong one is a bug nobody can see. Nothing is shared:

| | Xastir | Astir |
|---|---|---|
| program | `/usr/bin/xastir` | `/usr/bin/astir` |
| helpers | `callpass`, `testdbfawk`, `xastir_udp_client` | `astir_callpass`, `astir_testdbfawk`, `astir_udp_client` |
| data | `/usr/share/xastir` | `/usr/share/astir` |
| settings | `~/.xastir/config/xastir.cnf` | `~/.astir/config/astir.cnf` |
| environment | `XASTIR_*` | `ASTIR_*` |
| APRS tocall | `APX` + version | `APZ` + version |

Verified rather than asserted: staging an install and diffing every path
against the ones the installed `xastir` package owns finds **0 collisions**
across 236 Astir paths and 272 Xastir paths.

`~/.astir` is its own directory, not a reinterpretation of `~/.xastir`. Astir
never reads or writes Xastir's settings; doing so would corrupt a working
install of a program it is not.

### The tocall

`APX` is Xastir's **registered** APRS tocall and goes out in the destination
field of every packet Xastir transmits. A fork emitting `APX` would tell every
station on the air that it is Xastir, which is false and impossible for anyone
receiving it to debug. Astir emits `APZ` + version — the prefix the APRS
specification reserves for experimental and unregistered software, which is
what this is until a tocall is applied for.

Registering a real one is a follow-up, not a code change: ask the APRS tocall
registry for an `APxxxx` and set it in `configure.ac`.

## Tracking upstream

The `upstream` remote is kept so their changes can be read:

    git fetch upstream
    git log upstream/master

It is for **reading only**. `master` in this repository is the commit the fork
started from and is not moved. A merge is not expected to work and is not
attempted — inheriting upstream's file organisation is the specific thing this
fork exists to avoid, and half-tracking it would pull that back in a commit at
a time. A worthwhile upstream fix is cherry-picked deliberately, by a person
who has read it.

## Running from a source tree

Astir's compiled-in data directory is `/usr/share/astir`, which does not exist
until it is installed. To run a build without installing it, assemble Astir's
own data directory from its own tree:

    ./tools/devdata.sh
    ASTIR_DATA_BASE=$PWD/artifacts/datadir ./src/astir-gtk4

Point `ASTIR_MAPS` at a map collection to link it in.

**Do not** set `ASTIR_DATA_BASE=/usr/share/xastir`. It appears to work, and it
is how a broken render can look correct: the shapefiles get styled by upstream's
copy of the dbfawk rules instead of this tree's.
