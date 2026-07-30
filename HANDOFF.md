# Core extraction — state, and where to pick up

Written at the end of a session so the next one can resume without
re-deriving anything. Branch `perf-and-gui`. Everything below is committed.

## Where it stands

The goal is a core that links without the Motif front end, so a replacement
front end can be written. The measure is `tools/link_core.py`, which actually
links the 63 core objects with no front end and reports what fails.

**It now reports `LINKED CLEANLY`.** `core_boundary.py` agrees and `nm` finds no
core object needing anything from a GUI object.

| | two sessions ago | last session | now |
|---|---|---|---|
| symbols the core cannot link without | 46, across 12 objects | 1, in main.o | **0** |
| objects referencing a front-end symbol | 12 | 2 | **0** |
| map drivers naming `Widget` | all | all | **0** |
| drawing Xlib call sites outside the backend | — | — | 7 |
| lines of GTK4 front end written | 0 | 0 | **0** |

Last session's count of "1 symbol" was misleading: `link_core.py` counts distinct
symbols, and *two* objects needed `da` — `maps.o` as recorded, and
`xa_draw_x11.o`, which read main.c's global directly in about forty places. Both
are fixed.

## What is left, and where

`audit_x11.py` now reports 34 drawing call sites in **3** files and 49 other
Xlib call sites in core files. Both numbers are concentrated in code that is
itself platform implementation rather than application logic:

| file | what it is | why Xlib is there |
|---|---|---|
| `xa_draw_x11.c` | **the backend** | 27 drawing + most of the rest. Expected: this is the file a GTK4 backend replaces. |
| `rotated.c` | rotated-text renderer, predates Xft | 5 drawing + ~15 other. Replaced wholesale by Pango, not converted. |
| `cairo_text.c` | the Cairo text path | 2 |
| `color.c` | visual detection, `pack_pixel_bits` | 4 |
| `main.c` | front end | 2 |

So the seam is in the right place now. The remaining work is not "convert more
call sites"; it is "write a second backend".

## What this does NOT mean

**The interface has not changed, and no line of a replacement front end exists.**
`main.c` is ~30,800 lines and the front end is ~69,000 lines across 16 files.
Nothing in this session altered a pixel — every commit is verified not to.

The abstractions still have exactly one consumer each. "The core is portable" is
a claim `link_core.py` tests one specific way: that the core objects compile and
link with no front-end object present. That is now true, and it is all that is
true. A GTK4 backend is what would test the rest.

Two abstractions are **not covered by any test**:

- `xa_image_*` — the OSM path never executes in the A/B scenario. Measured, not
  assumed: counters on all six entry points report zero across a full render,
  even though `Online/OSM_tiled_mapnik.geo` is one of the three selected maps.
- `xa_bitmap_load` — the weather-alert block runs zero times. Needs an active
  alert.
- `xa_image_load` and `map_geo.c`'s XPM branch are `#ifdef`'d out entirely,
  because `HAVE_MAGICK` is defined. They compile; nothing runs them.
  `audit_x11.py` counts call sites without knowing they are unreachable here.

## How to verify a change (read this first)

Two harnesses, and picking the wrong one gives a confident wrong answer.

- **`tools/snapshot_ab.sh <out.xpm>`** — pixels, via Xastir's own snapshot
  facility, so window stacking is irrelevant. Required for anything touching
  text, colour, images or drawing. `SNAP_BIN=<path>` runs a different binary,
  which is the only way to A/B a change that is already committed (build a
  worktree at the older commit — and diff its `config.h` against this tree's
  before believing the result).
- **`./bench-attrib.sh 1.0 4 <tag>`** — counters. Identical output means
  identical geometry: `shapes_read 7444`, `vertices 219817`, `draw_calls 3458`
  cumulative at `lod=1.0 zoomout=4`. **Blind to text and colour.**

Do **not** use `ab-shot.sh` for A/B. It screenshots the whole screen.

### The harness was broken until this session, and silently

`snapshot_ab.sh` deleted the snapshot before starting Xastir and took the first
one that appeared. Snapshots fire the moment they are enabled — before the maps
finish drawing — so the captured frame could be **partial**, with features simply
absent, reading as background grey.

It cost most of a session. A correct change appeared to alter 17.6% of the frame,
in exactly the shape of a broken clip region. Two things made it convincing: the
geometry counters were identical, which reads as "same draw calls, pixels
suppressed"; and it *reproduced*, because the race resolved the same way every
time. Three baseline captures were byte-identical — byte-identical **partial
renders**. The settled frame has 822 colours; every capture the harness had ever
produced had 792.

The fix: discard everything written before the render settles, then require two
consecutive fresh snapshots to be byte-identical before emitting one. If it never
settles, no file is written and the script fails.

**The lesson generalises: "reproducible" is not "correct".** A deterministic
measurement of the wrong thing is the most expensive kind, because it survives
every retry. When a result and the code disagree, probe the runtime values before
believing either.

## Two techniques that worked

1. **Measure coverage, don't assume it.** Counters in the code under test, run
   once, answer "did the A/B exercise this at all?" That is what showed
   `get_hole_clipping_context` runs 83 times per 300 shapefile draws (covered)
   while every image entry point runs zero times (not covered). Without it, four
   commits would have claimed verification they had not earned.

2. **Enumerate the set, then let the compiler backstop the sweep.** The
   40-function Widget strip — 67 signatures, 99 call sites, 15 files — was
   uneventful because every call site's first argument was extracted with
   balanced-paren parsing (comments and strings stripped) *before* editing,
   giving three distinct values: `w` ×90, `NULL` ×2, `da` ×1. The rewrite only
   drops an argument that is one of those, and reports what it declines to touch.
   C then refuses to compile a call with the wrong argument count, which caught
   the two things a name-based sweep cannot see: leftover `(void)w;` statements,
   and `map_driver_ptr->func(w, ...)`, called through a function pointer.

   The previous hand attempt at this cascaded into unrelated call sites and left
   two files unrecoverable.

## Tools

All in `tools/` except three at the root, all documented in `tools/README.md`.

| tool | use |
|---|---|
| `link_core.py` | **ground truth** for the boundary — actually links the core |
| `../core_boundary.py` | cheap nm version of the same question |
| `../audit_x11.py` | Xlib call sites. Counts `#ifdef`'d-out code as live — see above |
| `split_scope.py` | where the GUI/core seam runs inside one file, transitively |
| `split_file.py` | performs a split; refuses unless the bytes reassemble |
| `drop_first_arg.py` | removes a leading argument from calls, not definitions |
| `extract_settings.py` | relocates plain-data definitions between files |
| `snapshot_ab.sh` | pixel-level A/B. `SNAP_BIN=` to run another binary |
| `snapshot/snap.cnf` | the exact configuration the pixel A/B runs under |

## Where to pick up

The boundary work is done; the next step is the first thing that tests it.

1. **Write a second backend.** `xa_draw.h` is ~40 entry points and the X11 one is
    ~1000 lines. A backend that only implements the drawing and pen calls, with
    text stubbed, is enough to find out whether the interface is actually
    sufficient — which is the open question, not the call-site count.
2. **Or get the untested paths under test first**, since they are cheap: a
    scenario with OSM tiles rendering, and one with an active weather alert.
    `xa_image_*` and `xa_bitmap_load` are inspection-only today.
3. `rotated.c` is the one core file with real Xlib left that is not the backend.
    It is not a conversion target — Pango does rotated text natively — so it
    belongs to whichever backend comes next, not to this layer.
