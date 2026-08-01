# Astir

An APRS client for amateur radio: it listens to a radio or to the internet,
decodes what other stations are transmitting, and draws them on a map.

Astir is a fork of [Xastir](https://xastir.org), rebuilt around a GTK4 front end
and a core that links with no toolkit at all. **It is a separate program, not a
newer version of Xastir.** The two install and run side by side and share no
files, no settings and no data directory. See [FORK.md](FORK.md).

> **Work in progress.** Astir receives, decodes and displays. It cannot yet
> transmit, and several things Xastir does are not built. Read
> [What is not done](#what-is-not-done) before deciding whether it is any use to
> you.

APRS(tm) is a trademark of Bob Bruninga.

## What it does

**Symbols that stay sharp.** All 211 APRS symbols were traced out of their
original pixel art into vector outlines, and drawing is Cairo and Pango
throughout, onto surfaces set to the display's real device scale. A station icon
is as crisp on a HiDPI screen as on a 96dpi one, and zooming does not turn it
into a mosaic.

**Vector maps.** Shapefiles, plus readers for Mapbox Vector Tiles and PMTiles,
all styled through the same dbfawk rules Xastir uses — so existing rule files
keep working. A PMTiles archive is a whole region in one file with no server,
which is the shape that matters for a program someone carries somewhere with no
network. Raster tiles work too, and are held decoded in memory instead of being
re-decoded on every frame.

**Labels that stay readable.** Names are collected while the map is drawn and
placed at the end, sorted by priority and rejected where they would overlap, so
a crowded city shows the important names rather than a solid block of text.
Station callsigns outrank map names, because that is what the program is for.

**Interfaces you set up in the application.** A software TNC over AGWPE
(Direwolf, or anything else speaking it), APRS-IS over the internet, a serial
KISS TNC, or an older command-mode serial TNC — added, started and stopped from
a window, with live status per port. Transmit is a per-interface checkbox and is
off by default.

**Stations you can interrogate.** Click one on the map for its position, course,
speed, altitude, path, comments and weather; or open the station list to search,
sort by distance or by how long ago it was heard, and centre the map on any of
them.

**An offline map on first run.** Astir ships a Natural Earth basemap —
coastlines, countries, lakes and rivers — and selects it the first time it
starts, so it draws a real map on a machine with no network and no downloads.

**Nothing polls.** The main loop sleeps until something happens: an arriving
packet wakes it through a file descriptor, ports and stations announce their own
changes, and the map redraws on the compositor's frame clock. An idle Astir is
genuinely idle.

## Building

[INSTALL.md](INSTALL.md) covers this in detail — out-of-tree builds, configure
options, unusual library locations, and troubleshooting. The short version:

### Dependencies

Astir needs **GTK4 4.10 or newer**. Shapefile support additionally needs PCRE,
which is what the dbfawk rules are matched with.

On Arch:

```sh
sudo pacman -S base-devel gtk4 shapelib pcre2 graphicsmagick curl \
               libgeotiff proj
```

On Debian or Ubuntu:

```sh
sudo apt install build-essential automake autoconf libtool pkg-config \
                 libgtk-4-dev libshp-dev libpcre2-dev \
                 libgraphicsmagick1-dev libcurl4-openssl-dev \
                 libgeotiff-dev libproj-dev
```

Optional, and off unless you ask for them: `libax25-dev` (kernel AX.25 ports),
`festival` (speech), `libcjson-dev` (Nominatim geocoding), Berkeley DB (on-disk
map cache).

Anything you do not want can be turned off at configure time —
`--without-shapelib`, `--without-geotiff`, `--without-libproj`,
`--without-graphicsmagick`, `--without-libcurl`, and so on. `./configure --help`
lists them all.

### Build and install

```sh
./bootstrap.sh          # once, after a fresh clone
./configure
make
sudo make install
```

`./build.sh` wraps the usual incremental rebuild during development.

To install into your home directory instead of system-wide:

```sh
./configure --prefix="$HOME/.local"
make && make install
```

### Running an uninstalled build

Astir looks for its maps, symbols and rules under `ASTIR_DATA_BASE`, which
defaults to the installed data directory. To run straight out of a checkout,
assemble that directory from the tree first:

```sh
./tools/devdata.sh
ASTIR_DATA_BASE=$PWD/artifacts/datadir ./src/astir
```

Pointing `ASTIR_DATA_BASE` at another program's data directory is refused at
startup. A build that quietly rendered using Xastir's copy of the styling rules
would be worse than one that would not start.

## First run

Astir creates `~/.astir` the first time it launches and comes up on the offline
basemap. Your callsign and home position live in `~/.astir/config/astir.cnf`.

To receive anything, you need an interface: **Menu → Connections →
Interfaces**, or `Ctrl+I`.

The simplest thing that works, with no radio at all, is APRS-IS — the internet
side of the APRS network:

| Field | Value |
| --- | --- |
| Type | APRS-IS internet server |
| Host | `noam.aprs2.net` |
| TCP port | `14580` |
| Passcode | `-1` |
| Server filter | `r/34.05/-118.25/150` |

A passcode of `-1` is a receive-only login: the server sends you traffic and
will not accept anything from you. The filter is a latitude, a longitude and a
radius in kilometres — without one you are asking for the whole world.

For a radio, run a software TNC such as Direwolf with an AGWPE port and add a
**Software TNC (AGWPE)** interface pointing at `localhost:8000`.

## Keyboard

| | |
| --- | --- |
| `Ctrl+I` | Interfaces |
| `Ctrl+M` | Choose maps |
| `Ctrl+L` | Station list |
| `+` / `-` | Zoom in / out |
| `F5` | Redraw |

## What is not done

Astir is mid-rewrite, and it is worth being plain about the gaps:

- **Nothing can go on the air.** Beaconing, messaging and digipeating exist in
  the core and are not reachable from the interface. The per-interface transmit
  checkbox arms a path that nothing currently originates traffic on.
- **Messages are not shown.** Received APRS messages are decoded and logged,
  not displayed in a window.
- **There is no settings UI** beyond interfaces and maps. Callsign, position and
  every other preference are edited in `~/.astir/config/astir.cnf`.
- **GPS, weather alerts and the SQL database backends** are in the core and are
  not exposed by this front end.
- **GPS interfaces cannot be created** from the interface window, though
  existing ones in a config file still load and run.
- The Motif front end and the X11 drawing backend are kept under `archive/` for
  reference while the port continues. They are not built.

## Licence

GPL v2, as Xastir is — see [COPYING](COPYING). Astir carries Xastir's history
and its authors' copyrights; see [AUTHORS](AUTHORS).

```
Copyright (C) 1999 Frank Giannandrea
Copyright (C) 2000-2026 The Xastir Group
```

There is no warranty, implied or otherwise. You use this software at your own
risk, and you are responsible for anything you transmit.

The Natural Earth basemap under `NaturalEarthVector/` is public domain. Map data
drawn from OpenStreetMap is © OpenStreetMap contributors, ODbL.

SmartBeaconing(tm) was invented by Tony Arnerich (KD7TA) and Steve Bragg
(KA9MVA) for the HamHUD project, and offered to other authors on the condition
that credit is given.
