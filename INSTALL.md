# Building and installing Astir

Astir builds with autotools. The steps are the usual ones — bootstrap,
configure, make, make install — and the only part that ever gives trouble is
having the right libraries installed first.

Astir installs alongside Xastir and shares nothing with it. If you have Xastir
on this machine you do not need to remove it, and installing Astir will not
touch it. See [FORK.md](FORK.md).

1. [Dependencies](#dependencies)
2. [Get the source](#get-the-source)
3. [Bootstrap](#bootstrap)
4. [Configure](#configure)
5. [Build](#build)
6. [Install](#install)
7. [Running without installing](#running-without-installing)
8. [First run](#first-run)
9. [Where things are kept](#where-things-are-kept)
10. [Troubleshooting](#troubleshooting)

## Dependencies

### Required

You cannot build Astir at all without these:

* **GTK4, version 4.10 or newer**, and its development headers
* a C compiler and the C library headers — `gcc` or `clang`
* `make`
* `autoconf`, `automake`, `libtool`, `pkg-config`
* `git`, if you are building from a clone

GTK4 is not optional and there is no fallback toolkit. Motif and X11 are no
longer used; the old Motif front end is kept under `archive/` for reference and
is not built.

### Strongly recommended

Without these Astir runs, but with very little to look at:

* **shapelib** — vector maps in ESRI shapefile format, which is what the
  bundled Natural Earth basemap is
* **PCRE2** — required by shapelib support; it is what the dbfawk styling rules
  are matched with, so configure will refuse `--with-shapelib` without it
* **GraphicsMagick** — raster map images in many formats
* **libcurl** — downloading maps and other data over the network

### Optional

* **libgeotiff** and **libtiff** — GeoTIFF maps, such as USGS topographic sheets
* **libproj** — projection support, used by the GeoTIFF reader
* **libax25** (`ax25-apps`, `ax25-tools`) — Linux kernel-mode AX.25 ports
* **Berkeley DB** 5 or 18.1 — on-disk caching of downloaded map tiles
* **cJSON** — Nominatim geocoding
* **festival** — text to speech

### Alternatives

* ImageMagick 6 — *not* 7 — can stand in for GraphicsMagick, but GraphicsMagick
  is preferred.
* The original PCRE ("PCRE3") still works but is long past end of life. Use
  PCRE2.

### Installing them

Arch:

```sh
sudo pacman -S base-devel git gtk4 shapelib pcre2 graphicsmagick curl \
               libgeotiff proj
```

Debian or Ubuntu:

```sh
sudo apt install build-essential automake autoconf libtool pkg-config git \
                 libgtk-4-dev libshp-dev libpcre2-dev \
                 libgraphicsmagick1-dev libcurl4-openssl-dev \
                 libgeotiff-dev libproj-dev
```

Fedora:

```sh
sudo dnf install @development-tools git gtk4-devel shapelib-devel \
                 pcre2-devel GraphicsMagick-devel libcurl-devel \
                 libgeotiff-devel proj-devel
```

On distributions that split a library from its headers, you need both — that is
what the `-dev` and `-devel` suffixes are. Package names drift between releases;
if one of the above is not found, search your package manager for the library
name rather than assuming it is unavailable.

## Get the source

```sh
git clone <your-clone-url> astir
cd astir
```

A clone gets you the full history and every branch, which is what you want if
you intend to update later — `git pull` and rebuild.

## Bootstrap

The tree does not ship a `configure` script; it is generated:

```sh
./bootstrap.sh
```

You should see:

```
    5) Removing autom4te.cache directory...
    4) Running aclocal...
    3) Running autoheader...
    2) Running autoconf...
    1) Running automake...
Bootstrap complete.
```

If you do not see `Bootstrap complete`, it failed, and almost always because
one of autoconf, automake or libtool is missing. Read the error and install what
it names.

## Configure

On most Linux systems, no arguments are needed:

```sh
./configure
```

Configure works out what you have installed and prints a summary of what it
found. Read it before going further — this is where you discover that shapelib
was not detected, rather than after building:

```
astir X.Y.Z has been configured to use the following
options and external libraries:

MINIMUM OPTIONS:
  ShapeLib (Vector maps) .................... : yes

RECOMMENDED OPTIONS:
  GraphicsMagick/ImageMagick (Raster maps) .. : yes (GraphicsMagick)
  pcre (Shapefile customization) ............ : yes
  Berkeley DB map caching-Raster map speedups : no
  internet map retrieval .................... : yes
  Nominatim Geocoding ....................... : no

FOR ADDITIONAL FEATURES:
  AX25 (Linux Kernel I/O Drivers) ........... : no
  ...

astir will be installed in /usr/local/bin.
Type 'make' to build Astir (Use 'gmake' instead on some systems).
```

If GTK4 is missing or too old, configure stops there and says so. Nothing else
is fatal.

### Building somewhere other than the source directory

Autotools supports out-of-tree builds, and they are worth using: the source tree
stays untouched, and starting over is `rm -rf` on one directory.

```sh
mkdir ../astir-build && cd ../astir-build
../astir/configure
```

Everything below works the same from a build directory.

### Installing somewhere other than /usr/local

```sh
./configure --prefix="$HOME/.local"
```

### Turning features off

Any optional dependency can be declined even if it is installed:

```sh
./configure --without-shapelib --without-geotiff --without-libproj \
            --without-graphicsmagick --without-libcurl --without-ax25 \
            --without-festival --without-map-cache --without-nominatim
```

`./configure --help` lists every option.

### Headers and libraries in unusual places

Some systems put libgeotiff's headers in a subdirectory:

```sh
./configure CPPFLAGS="-I/usr/include/geotiff"
```

On FreeBSD, and on macOS with Homebrew or MacPorts, third-party packages land
outside the compiler's default search path entirely:

```sh
./configure CPPFLAGS="-I/usr/local/include" LDFLAGS="-L/usr/local/lib"
```

These stack, and can be combined with a compiler choice and flags:

```sh
./configure CPPFLAGS="-I/usr/local/include -I/opt/odd/include" \
            LDFLAGS="-L/usr/local/lib -L/opt/odd/lib" \
            CC=gcc13 CFLAGS="-O2 -g"
```

Berkeley DB is the fiddliest of them, because a system may have two versions at
once and the headers must match the library actually linked:

```sh
./configure --with-bdb-incdir=/usr/local/include/db18 \
            --with-bdb-libdir=/usr/local/lib
```

Astir links the newest Berkeley DB present even if you point it at older
headers, so if you need the older one, name it explicitly with
`LIBS="-ldb-5.3"`.

## Build

```sh
make
```

Warnings are expected; errors are not. During development, `./build.sh` wraps
the same thing for incremental rebuilds.

## Install

```sh
sudo make install
```

That puts `astir` in `/usr/local/bin` and its resources in
`/usr/local/share/astir`, or under whatever `--prefix` you configured.

If you enabled AX.25 and want kernel AX.25 ports, the binary needs to be setuid:

```sh
sudo chmod 4755 /usr/local/bin/astir
```

**Do not run Astir as root.** It has no need of root privileges and every reason
not to have them.

## Running without installing

Astir finds its maps, symbols, help text and styling rules under
`ASTIR_DATA_BASE`, which defaults to the installed data directory. To run
straight out of a build tree, assemble that directory first:

```sh
./tools/devdata.sh
ASTIR_DATA_BASE=$PWD/artifacts/datadir ./src/astir
```

`devdata.sh` symlinks Astir's own `config/`, `symbols/`, `help/` and maps into
`artifacts/datadir`, so editing a rule file in the tree takes effect on the next
run with no copying.

If you have a collection of downloaded maps — TIGER shapefiles, for instance —
point `ASTIR_MAPS` at it and they are linked in too:

```sh
ASTIR_MAPS=~/maps ./tools/devdata.sh
```

`ASTIR_MAPS` may not be another program's install directory, and `devdata.sh`
refuses one. `/usr/share/xastir/maps` looks like it works, because the
shapefiles really are in there, but it drags that program's own resources in
alongside them and Astir then reads them on every run. Copy the maps you want
out to a directory of your own.

`ASTIR_DATA_BASE` pointing into another program's data directory is refused at
startup for the same reason.

## First run

Start it from a terminal or from your desktop menu:

```sh
astir
```

The first launch creates `~/.astir`, selects the bundled offline Natural Earth
basemap, and draws a map of the world. No network is required to get that far.

Set your callsign and home position in `~/.astir/config/astir.cnf`:

```
STATION_CALLSIGN:N0CALL
SCREEN_LAT:20152269
SCREEN_LONG:22192914
```

There is no settings window yet — this port exposes interfaces and map
selection in the interface, and everything else through the config file.

To receive traffic, open **Menu → Connections → Interfaces** (`Ctrl+I`) and add
one. The quickest check that the receive path works, with no radio at all, is an
**APRS-IS internet server** interface pointing at `noam.aprs2.net` port `14580`
with passcode `-1` and a server filter such as `r/34.05/-118.25/150`. A passcode
of `-1` is a receive-only login. See the [README](README.md#first-run).

For a radio, run a software TNC such as Direwolf with an AGWPE port and add a
**Software TNC (AGWPE)** interface pointing at it, usually `localhost:8000`.

Transmit is a checkbox on each interface and is off by default. Nothing in this
port originates traffic yet.

## Where things are kept

Per user, under `~/.astir`:

| | |
| --- | --- |
| `config/` | settings (`astir.cnf`), map index, chosen maps |
| `data/` | station databases and downloaded lookups |
| `logs/` | packet and message logs |
| `tracklogs/` | recorded GPS tracks |
| `gps/` | GPS device scratch space |
| `map_cache/` | downloaded map tiles |
| `tmp/` | working files, cleared between runs |

Set `ASTIR_USER_BASE` to put that directory somewhere other than your home
directory — useful for testing without touching your real settings.

System-wide, under the install prefix:

| | |
| --- | --- |
| `bin/` | `astir`, `astir_callpass`, `astir_testdbfawk`, `astir_udp_client` |
| `share/astir/maps/` | bundled maps, including the Natural Earth basemap |
| `share/astir/symbols/` | symbol resources |
| `share/astir/config/` | dbfawk styling rules and shipped configuration |
| `share/astir/scripts/` | helper scripts |

## Troubleshooting

**`configure` stops with "GTK4 not found".** Install your distribution's GTK4
development package. `pkg-config --modversion gtk4` must report 4.10 or newer.

**`configure` says shapelib support requires PCRE.** Install `libpcre2-dev` (or
`pcre2-devel`), or build without shapefile maps using `--without-shapelib`.

**Astir starts but the map is blank.** Check the terminal output. If it says it
is starting with the default offline map set and nothing draws, shapelib support
was almost certainly not compiled in — re-run configure and read the summary.

**Astir refuses to start, saying it will not use another program's data
directory.** `ASTIR_DATA_BASE` is pointing at something that is not Astir's, most
often `/usr/share/xastir`. Run `./tools/devdata.sh` and point it at
`artifacts/datadir`, or install Astir properly and unset the variable.

**Nothing appears after adding an interface.** Open the interface window again
and look at the port's status line. An APRS-IS server with no filter set will
sometimes give you nothing until one is configured; a filter of
`r/<lat>/<lon>/<km>` around your own position is the normal starting point.
