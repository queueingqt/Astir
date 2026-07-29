# Core extraction — state, and where to pick up

Written at the end of a session so the next one can resume without
re-deriving anything. Branch `perf-and-gui`. Everything below is committed.

## Where it stands

The goal is a core that links without the Motif front end, so a replacement
front end can be written. The measure is `tools/link_core.py`, which actually
links the 62 core objects with no front end and reports what fails.

| | start of session | now |
|---|---|---|
| symbols the core cannot link without | 46, across 12 objects | **1, in main.o** |
| front-end callbacks (`xa_ui.h`) | 2 | 16 |
| `maps.c` GUI by volume | 55% | 0% |
| `draw_symbols.c` GUI by volume | 14% | 0% |
| `cad_objects.c` GUI by volume | 74% | 0% |
| lines of GTK4 front end written | 0 | 0 |

Eleven of the twelve GUI objects no longer supply anything to the core:
`popup_gui`, `bulletin_gui`, `view_message_gui`, `wx_gui`, `interface_gui`,
`list_gui`, `locate_gui`, `messages_gui`, `db_gui`, `objects_gui`,
`cad_objects_gui`.

Four files were split, all verified rendering-identical:
`cad_objects.c`/`cad_objects_gui.c`, `draw_symbols.c`/`draw_symbols_gui.c`,
`maps.c`/`maps_gui.c`, and `db_gui.c` → new `station_draw.c`.

## The one remaining symbol

`da`, the drawing area widget, at `src/maps.c:6952`:

```c
void fill_in_new_alert_entries(void)   // maps.c:6890
{
    ...
    map_search (da, alert_scan, temp, &alert_count, ...);
```

**It is blocked on the image and colour calls, not on anything structural.**
`map_search` threads its widget to `draw_map`, which threads it to the map
drivers, and four of those genuinely read it:

| file | why it needs the Widget |
|---|---|
| `map_WMS.c` | `XtDisplay(w)` |
| `map_geo.c` | `XtDisplay(w)` — `XGetImage`, `XPutImage`, `XAllocColor` |
| `map_shp.c` | `XtDisplay(w)` — region/clip calls |
| `map_tif.c` | `XtDisplay(w)` — `XAllocColor` |

Two dead ends already checked, so don't retry them:

- **Pushing the reference up to the front end does not work.**
  `fill_in_new_alert_entries()` is called from `db.c` and `log_utils.c`, both
  core, so giving it a Widget parameter moves the reference to another core file.
- **Stripping the Widget from the map-loading chain does not work yet**, because
  the drivers above read it.

So the next step is to abstract images and colours into `xa_draw.h`, the same way
fonts were, then the chain strips and the count reaches zero. `audit_x11.py`
reports 68 non-drawing Xlib calls left in core files; the relevant clusters are
18 image calls (`XDestroyImage`, `XCreateImage`, `XGetPixel`, `XPutPixel`,
`XGetSubImage`), 15 colour calls (`XAllocColor`, `XQueryColor`, `XGetVisualInfo`)
and 13 region/clip calls in `map_shp.c`.

## Then what — the honest picture

Reaching zero means the core *links* without Motif. It does not mean the
interface changes. **No line of a replacement front end exists.** `main.c` is
still ~30,700 lines of mixed Motif and application logic, and the front end as a
whole is ~68,000 lines across 16 files with ~1,376 widget creations. Nothing in
this session altered a single pixel — every commit is verified not to.

The abstractions are also still unvalidated by anything but Motif. The 16-entry
callback table and `xa_draw.h` have exactly one consumer each. "The core is
portable" is a claim the measurement scripts make, not a demonstrated fact, and
this session found the scripts wrong three times.

## How to verify a change (this matters)

Two harnesses, and picking the wrong one gives a confident wrong answer.

- **`./bench-attrib.sh 1.0 4 <tag>`** — counters. Identical output means
  identical geometry: `shapes_read 7444`, `vertices 219817`, `draw_calls 3458`
  cumulative at `lod=1.0 zoomout=4`. **Blind to text and colour** — a font
  change moves every label while all three stay identical.
- **`tools/snapshot_ab.sh`** — pixels. Uses Xastir's own snapshot facility
  (`SNAPSHOTS_ENABLED:1` writes `pixmap_final` to `~/.xastir/tmp/snapshot.xpm`),
  so it does not care what window is on top. Run against each build and `cmp`.
  Required for anything touching text, colour or drawing.

Do **not** use `ab-shot.sh` for A/B comparison. It screenshots the whole screen,
so it captures whatever window happens to be in front — one attempt this session
returned a blank Xastir window on one side and a browser on the other, and the
comparison reported a 7% difference. `xwd`/`xwininfo`/`xdotool` are not installed.

`tools/README.md` has the full set of cautions, including three ways to get a
confidently wrong measurement that all happened this session.

## Tools

All in `tools/`, all take the source dir as an argument, all documented in
`tools/README.md`.

| tool | use |
|---|---|
| `link_core.py` | **ground truth** for the boundary — actually links the core |
| `../core_boundary.py` | cheap nm version of the same question |
| `../audit_x11.py` | Xlib call sites, two numbers: Stage 2 scope, and everything else |
| `split_scope.py` | where the GUI/core seam runs inside one file, transitively |
| `split_file.py` | performs a split; refuses unless the bytes reassemble |
| `drop_first_arg.py` | removes a leading argument from calls, not definitions |
| `extract_settings.py` | relocates plain-data definitions between files |
| `snapshot_ab.sh` | pixel-level A/B |

## Two process rules earned the hard way this session

1. **Compute the set before editing it.** An attempt at the last change tried to
   strip 24 functions — everything `split_scope.py` listed — and cascaded into
   unrelated call sites until two files were unrecoverable and had to be
   reverted. The transitive closure from the three entry points that mattered is
   11 functions, and that pass was uneventful.
2. **When a sweep is wrong, revert everything it touched and redo.** Twice this
   session a script produced code that compiled and was still wrong. Patching
   its output would have left the rest of the damage in place.
