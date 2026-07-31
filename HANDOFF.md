# Core extraction — state, and where to pick up

Written at the end of a session so the next one can resume without
re-deriving anything. Branch `perf-and-gui`. Everything below is committed.

## Where it stands

The goal is a core that links without the Motif front end, so a replacement
front end can be written. The measure is `tools/link_core.py`, which actually
links the core objects with no front end and reports what fails.

**It still reports `LINKED CLEANLY`**, now across 64 core objects.
`core_boundary.py` agrees and `nm` finds no core object needing anything from a
GUI object.

| | two sessions ago | last session | now |
|---|---|---|---|
| symbols the core cannot link without | 46, across 12 objects | 1, in main.o | **0** |
| objects referencing a front-end *symbol* | 12 | 2 | **0** |
| core objects still *calling* Motif directly | ? | 2 (`db.o`, `messages.o`) | **0** |
| core `.c` files including a Motif header | ? | 2 | **0** |
| map drivers naming `Widget` | all | all | **0** |
| drawing Xlib call sites outside the backend | — | 7 | 7 |
| harnesses that can see the message windows | 0 | 0 | **1** |
| core `.c` files pulling in **no** X header at all | 0 | 0 | **48 of 59** |
| core headers rooting the X include tree | many | many | **0** |
| X symbols the core needs, against a **non-X backend** | untested | untested | **18, in 3 renderer files** |
| backends implementing `xa_draw.h` | 1 (X11) | 1 (X11) | **3** (X11, null, GTK4) |
| lines of GTK4 front end written | 0 | 0 | **~470, and it renders** |

Two things happened. First the harness the previous notes asked for, because the
remaining work was unverifiable without it. Then the work itself: `mw[]` and the
whole of the Motif coupling in `db.c` and `messages.c` moved behind eight
callbacks, and `nm` now reports **zero** `Xm*`/`Xt*` symbols needed by either
object. Both were verified against the harness rather than by compiling.

## That boundary is crossed: no core object calls Motif

`link_core.py` cannot see this on its own, and it is worth understanding why. It
links the core against the *full* library set, so a Motif call inside a core
object resolves happily from `-lXm` and the link stays clean. Asking `nm` about
the individual objects asks a different question. It used to answer:

| object | needed | what it was |
|---|---|---|
| `db.o` | 7 `XmText*` / `XtFree` | `update_messages()` — renders the message window |
| `messages.o` | `XmTextFieldGetString`, `XtDestroyWidget`, `XtFree`, `XtPopup` | 4 window-management functions, and `mw[]` itself |

Both are now **zero**, and `nm -u` on either object finds no `Xm*` or `Xt*` name
at all. `messages.c` includes no X header; neither does `db.c`. What remains is
the backend and the files a backend replaces:

| object | needs | what it is |
|---|---|---|
| `xa_draw_x11.o` | 50 | the backend. Expected. |
| `rotated.o`, `cairo_text.o`, `color.o` | 19 | renderer/platform implementation |

`map_tif.o`'s `XTIFFOpen`/`XTIFFClose` are **libtiff**, not Xlib — the trap
`tools/README.md` warns about, which a first pass here fell into anyway.

### How it was done

`Message_Window mw[MAX_MESSAGE_WINDOWS+1]`, an array of Motif widgets, was
**defined in `messages.c`** — a core file — and declared in `astir.h`, which
every core file includes. It now lives in `messages_gui.c`, declared in a new
`messages_gui.h` that only the front end includes. That move is what forced the
rest, and it is also what makes `link_core.py` a real test of it: `mw` is now
defined in an object the core link omits, so any core file still naming it fails
to link rather than failing silently.

The core turned out to need eight operations, added to `xa_ui.h` alongside the
existing sixteen:

| callback | replaces |
|---|---|
| `msg_window_is_open` | `mw[i].send_message_dialog != NULL` |
| `msg_window_is_group` | `mw[i].message_group` |
| `msg_window_callsign` | `XmTextFieldGetString` + copy + `XtFree` |
| `msg_window_raise` | `XtPopup` |
| `msg_window_close_all` | the whole of `clear_message_windows()` |
| `msg_window_clear` | `XmTextReplace(..., "")` |
| `msg_window_append` | `XmTextInsert` + `XmTextSetHighlight` |
| `msg_window_show` | `XmTextShowPosition` |

Three details that had to be right, and would have been easy to get wrong:

- **`msg_window_callsign` returns a value.** The original distinguished "this
  window has no callsign field" from "the field is empty" and took different
  paths — `new_message_data--` happens for the first but not the second. The
  return preserves that.
- **`msg_window_append` returns a value.** In the original, `pos +=
  strlen(temp2)` sat *inside* `if (mw[..].send_message_text != NULL)`, so a
  window with no transcript did not advance the position. The caller now guards
  on the return for the same reason.
- **The highlight decision stayed in the core.** Whether a line is reverse video
  depends on `acked` and `is_my_call`, which read the message store. Only the
  drawing moved; the core passes a boolean.

`update_messages()` was not moved to `messages_gui.c`, and should not be: it
reads `msg_data`, `msg_index`, `msg_index_end`, which are `static` in `db.c` on
purpose. Moving it means exporting the message store, which trades a layering
problem for a worse one. Pushing view updates through callbacks keeps the store
private.

`static XmTextPosition pos` became `static long pos` — `XmTextPosition` *is*
`long` (`Xm/Xm.h:1152`), so that is the same variable with the toolkit's name
taken off it.

**The recorded `static pos` trap did not exist, and it is worth saying why it
looked like one.** The previous notes said `pos` is never reset, pointing at the
commented-out `//pos=0;` at the top of the per-window loop. There is a second
reset: a live `pos = 0;` further down, inside `if(strlen(temp1)>0)` and before
the message list is built. Every read of `pos` is dominated by that assignment,
so despite the `static` it behaves exactly as a local, and there was no
cross-call accumulation to reproduce. One commented-out line four hundred lines
from the live one was enough to make a mechanical change look risky for two
sessions. Grep for *all* the assignments before believing a note like that.

### One thing was deleted rather than converted

`clear_acked_message()` held a loop that read the callsign out of every open
window, uppercased it into a local, and dropped it. The only statement that used
the result — `XtSetSensitive(mw[ii].button_ok,TRUE)`, "clear the send button" —
has been commented out for as long as the history goes back; the local had one
writer and no reader. Two of the four Motif calls in `messages.o` were held up
by nothing at all. Deleting it also retired two now-unused locals.

This is the one change in the set that the harness does **not** cover — the
scenario never reaches `clear_acked_message`, which the operation histogram says
plainly. It rests on reading, not on measurement.

### What this cost, and what it did not

Verified three ways, all of which had to pass before it was committed:

- `link_core.py` — `LINKED CLEANLY`, 64 core objects, with `mw` now defined in
  an omitted object.
- `trace_ab.sh` — **byte-identical** to the pre-change capture. Same 239 raw
  records, same 89 normalised, same histogram. Every message-window operation
  the scenario reaches produced the same arguments through the callbacks as it
  did through the direct Motif calls.
- `snapshot_ab.sh` — byte-identical to the baseline.

### The core headers are off X11

`astir.h` opened with `#include <X11/Intrinsic.h>` and is included by every
core file, so the shapefile reader and the APRS parser got Xt whether they
wanted it or not. `main.h` did the same. Both are gone, along with the same
problem in `interface.h`, `draw_symbols.h`, `cad_objects.h`, `maps.h`, `wx.h`,
`station_draw.h`, `dlm.h` and `map_OSM.h`.

**48 of the 59 core `.c` files now compile with no X header reachable at all**,
up from none.

Three kinds of thing were in those headers, and they needed three different
answers:

- **Drawing objects and surfaces** → `xa_draw.h`, in the neutral types. The
  five GCs, seven pixmaps, `colors[]` and `trail_colors[]`; every `Pixmap where`
  parameter in `draw_symbols.h`/`maps.h` became `xa_surface_id`, and `GC gc`
  became `xa_pen`. Not a widening: `Pixmap` and `Pixel` *are* `unsigned long`,
  and `GC` converts implicitly to `xa_pen`'s `void *`. Core drawing code already
  treated all of them this way.
- **Widget-typed declarations** → six new `_gui.h` headers (`astir_gui.h`,
  `main_gui.h`, `interface_gui.h`, `draw_symbols_gui.h`, `cad_objects_gui.h`,
  `maps_gui.h`, `wx_gui.h`). None had a core caller.
- **Things that were only pretending to need X** — `check_trans()` took an
  `XColor` by value but read only `.pixel`, so it takes the pixel now;
  `Draw_All_CAD_Objects()` carried a `Widget` it never used;
  `station_draw.h`'s include had a comment explaining it could not be removed
  until the font calls went through the drawing layer, which had already
  happened two sessions earlier; `dlm.h` included `<X11/X.h>` for a `KeySym` it
  does not name.

**The surprise was `<X11/Xos.h>`.** Ten core files stopped compiling when their
X include went, and not one of them needed X: they needed `<string.h>`. Xt had
been supplying `strlen`, `strcmp`, `memcpy` and friends transitively for
decades. GCC names the header it wants in its diagnostic, which made the sweep
mechanical -- remove the include, read the `note: include ‘<string.h>’`, add it,
recompile.

`build.sh` checks `astir.h` and `interface.h` for X-freedom on every build.

**11 core files still reach X**, and they are the honest remainder rather than
oversight: `draw_symbols.c` names `Display`, `map_shp.c` uses `XPoint` in its
vertex buffers, `xa_config.c` has `Widget` in a signature, and the raster map
drivers (`map_dos.c`, `map_gnis.c`, `map_pop.c`, `map_tif.c`) use `XColor` and
the XPM path. Those are conversions, not relocations, and several belong to
whichever backend comes next.

`db.c` also still includes `db_gui.h` and `objects_gui.h` -- GUI headers pulled
in by a core file. That is a layering question, not a header one, and it is why
`db.c` is in the 11.

## The message windows have a harness now

`tools/trace_ab.sh`. The code under test says what it is doing, the same packets
are replayed through both builds, and the records are diffed. It does not depend
on the change being visible, which is why it reaches what the pixel harness
cannot.

A record looks like this, and the whole point is that it survives an interface
change — the same values have to come out whether the call is `XmTextInsert` or
a front-end callback:

```
msg_render_begin win=0
msg_clear win=0
msg_callsign win=0 call="N7XYZ-1"
msg_insert win=0 pos=0 text="NN/NN NN:NN N7XYZ-1  >First message from N7XYZ\n"
msg_highlight win=0 from=22 to=47 mode=normal
msg_show win=0 pos=47
msg_render_end win=0
```

Three pieces: `src/xa_trace.[ch]` (inert unless `ASTIR_TRACE` names a file),
trace points at the message-window operations in `db.c` and `messages.c`, and
`ASTIR_REPLAY=<file>` in `main.c`, which sets `read_file_ptr`/`read_file`
exactly as `File > Open Log File` does so a scenario can run without a human.

**It was proven three ways before being used for anything**, because a harness
that cannot tell A from B reports success either way:

| | result |
|---|---|
| deterministic | two runs, byte-identical normalised traces, identical raw counts (239) |
| sensitive | `offset = 22` → `23` in `update_messages()` moved 28 diff lines, all `msg_highlight from=`, nothing else |
| necessary | the *same* defective binary gave a `snapshot_ab.sh` capture byte-identical to the baseline |

That third row is the one that matters. The pixel harness does not merely miss
this defect by luck: `pixmap_final` is the map canvas, the Send Message windows
are separate top-level dialogs, and the pixel scenario replays no packets so no
window is ever opened in it.

Two things had to be normalised rather than wished away, both in
`tools/trace_norm.py`, which prints everything it collapsed:

- `update_messages()` rebuilds the whole window on a timer as well as on
  arrival, so identical blocks repeat an arbitrary number of times.
- Each line embeds `packet_time`, the wall clock at reception, so two runs a
  minute apart differ. The `NN/NN NN:NN` field is masked, which leaves the
  timestamp *values* uncovered but not its format or width.

**Read `tools/README.md` for what the scenario does not reach.** Three records
never appear: `msg_destroy`, `msg_scan_call fn=clear_acked_message`, and
`msg_highlight mode=selected`. Two of the three need an *outgoing* message,
which a receive-only replay cannot produce. The third is narrower than it looks:
`clear_message_windows()` *is* called, once, at startup — it simply finds every
window already closed, so the loop and the null-out run and the
`XtDestroyWidget` inside them does not. A clean diff says nothing about those
three branches.

One of them matters less than it looks: **`clear_acked_message`'s Motif loop is
dead code.** It reads the callsign out of every open window, uppercases it, and
discards it; the only statement that used the result is commented out. Those are
two of the four Motif calls that make `messages.o` non-portable, and they are
holding up nothing.

## The GTK4 front end: how to run it, and what it does

**Running uninstalled needs `ASTIR_DATA_BASE`.** The data directory compiled in
is now `/usr/share/astir`, and nothing is installed there -- the dev tree has
always borrowed the packaged install's data. Without it the shapefiles still
draw, but with default styling instead of their dbfawk rules, which looks like
a rendering regression and is not one:

    ASTIR_DATA_BASE=/usr/share/xastir ./src/astir-gtk4

    ./build.sh                 # the core objects, as usual
    ./tools/build_gtk4.sh      # links them + xa_draw_gtk4.o, no Motif, no X
    ./src/astir-gtk4

Environment hooks, all scripted-run conveniences, all inert unset:

| | |
|---|---|
| `ASTIR_GTK4_RENDER_TO=<f.png>` | render one frame and exit. **Use this as a gate before launching anything interactively** |
| `ASTIR_GTK4_SCALE=<n>` | start at a given `scale_y`; 1200 is a Los Angeles view |
| `ASTIR_REPLAY=<log>` | ingest packets with no interface; `tools/trace/stations-la.log` has eight stations around that view |
| `ASTIR_GTK4_SHOW_MENU=1` | pop the menu open, for screenshots |
| `ASTIR_GTK4_TRACE_ZOOM=1` | log every zoom step and every render |
| `ASTIR_DEBUG=<n>` | set `debug_level`; 16 traces map loading |

**Works**: OSM tiles, TIGER shapefiles, the lat/lon grid, station symbols and
labels, the range-scale bar, the status line, pan by drag, scroll and button
zoom, a GMenu with working toggles. On Wayland under KWin.

**Does not**: dialogs, interfaces, message windows, station list, configuration
UI, weather alerts. It does not write the config back on exit, deliberately --
`~/.astir/config/astir.cnf` is what the Motif pixel baseline depends on.

### Known broken: grey boxes behind station icons

`draw_symbol()` blits an icon through a per-pixel coverage mask.
`xa_draw_gtk4.c`'s `begin()` clips to the mask's **bounding rectangle** only,
so every symbol sits on an opaque square.

The obvious fix -- push a Cairo group in `begin()`, pop it through
`cairo_mask_surface()` in a matching `finish()` -- was written, and reverted.
It is correct and it is catastrophic: **a pen keeps its clip mask until
something clears it**, so once a symbol set one, every subsequent draw call paid
a full group allocation and mask composite. All 747 shapefile draws. The app
pegged a core and never showed a window.

The X11 backend never had this problem because a server-side clip mask is free
to leave set. That cost difference is invisible in the interface.

**Do it by masking only `xa_copy_area()`**, which is what symbol blitting
actually uses, or by clearing the pen's mask after the symbol draw. Gate it on
`ASTIR_GTK4_RENDER_TO` before it goes near a window.

### The render scheduler, and three bugs in it

Rendering is deferred and coalesced: handlers move the position and call
`render_soon()`, which arms a 150 ms timer; `render_now()` does one render.
Gestures meanwhile transform the frame already on screen -- `view_dx`/`view_dy`
slide it, `view_scale` scales it about the window centre -- so a drag tracks the
pointer and a scroll responds instantly, blurry until the real frame lands.

Three bugs found here in one sitting, all worth remembering because none is
visible in a headless render:

1. **Rendering inline from the handler.** `xa_render()` is 0.5-1 s. Called
   directly it blocks the main loop, so `gtk_widget_queue_draw()` cannot be
   serviced and the canvas keeps showing the old frame *until some later event
   lets the loop run*. That presents as "grey areas render if I click after",
   which sounds like a drawing bug and is a scheduling one.
2. **`xa_render()` is re-entrant.** The core calls `xa_ui_pump_events()` every
   64 shapes so a slow redraw can be interrupted, and this front end services
   the main loop there -- so scroll events run *inside* a render. Resetting
   `view_scale` to 1 afterwards discarded every step that arrived while it ran,
   which presented as zoom "snapping back to one level out". Divide out only
   what the finished frame accounts for.
3. **A returned-early timer id.** The re-entrancy guard returned before clearing
   `render_timer`, so `render_soon()` believed a render was pending forever and
   **nothing ever rendered again**. One skipped frame stopped the map
   permanently. Clear the timer first, unconditionally.

`dy` from a scroll event is not comparable across devices -- a wheel notch may
report 1, a high-resolution wheel or touchpad tens -- so the per-event zoom is
clamped to +/-1 and applied as `pow(1.15, step)`.

Zoom-out stops once the whole world fits (`32400000 / screen_height`, 180
degrees of latitude). Astir's 500000 is not a view limit, only the largest
value the config can store.

### Station symbols are raster, by design

The APRS symbol set, 20x20 pixmaps from `symbols.dat` via
`load_pixmap_symbol_file()`, drawn at fixed pixel size regardless of zoom. They
are not vectors and do not scale with the map. Pixelation *during* a zoom
gesture is the preview transform scaling the whole composed frame; it resolves
when the render lands.

## There is a GTK4 front end

`src/gtk4/xa_gtk4_main.c`, built by `tools/build_gtk4.sh`: 60 core objects plus
the GTK4 backend, no Motif, every X library struck off the link line. One object
set, two binaries -- and not a line of the core compiled differently for either.

Modern GTK4 rather than a port of main.c's shape: GtkApplicationWindow, header
bar, GtkDrawingArea, GAction with accelerators, `GtkGestureDrag` for panning and
`GtkEventControllerScroll` for zoom. Panning and zooming are arithmetic on
`center_longitude`/`scale_y`, which is core state, because that is the only
thing either front end can do.

**It is not a replacement for the Motif front end** -- that is ~69,000 lines
across 16 files plus main.c's 30,800. No dialogs, no menus, no interfaces, no
message windows, no station list, no configuration UI. What is here is the
spine.

### It draws maps

OSM tiles, the TIGER shapefile overlay and the lat/lon grid, with the status
line live in the header bar. It renders at the config's own zoom with no override; `ASTIR_GTK4_SCALE`
remains only as a scripted-render convenience.
`ASTIR_GTK4_RENDER_TO=<file.png>` renders one frame and exits.

    [perf] gtk4_render 544.8 ms | shp_read 119.1 shp_draw 26.1 dbfawk 280.0 |
           maps 2 shapes_read 122678 vertices 145799 draw_calls 576

**The bug was one missing assignment.** `map_onscreen_index()` culls every map
against `NW_corner_longitude`/`SE_corner_*`, and those are computed by
`create_image()` in main.c -- not by anything in the core. Left at zero, every
map reports `MAP_NOT_VIS`, `load_maps()` draws nothing, and there is no error
anywhere, because "not visible" is a perfectly normal answer. Three maps were
found, three drivers selected, nothing drawn.

It took Astir's own `debug_level & 16` tracing to find: the trace prints the
map path *after* the visibility test, so the geo map printed its path and the
two shapefiles did not. That asymmetry was the whole clue.

**scale_x is derived, not chosen.** `get_x_scale()` computes it from `scale_y`
and the position so a mile is the same number of pixels both ways, and it
carries two guards: it returns `scale_y` unchanged near the poles and above
`scale_y` 50000. The first version here multiplied `scale_y` by
`calc_dscale_x()`, which is neither that formula nor the right shape --
`calc_dscale_x` is metres per Astir unit and the ratio wanted is `sc_y/sc_x`.
Below 50000 that gave a wildly wrong x scale; at or above it the guard hid the
error completely, which is why the config's own zoom looked fine and zooming in
did not. It is re-derived on a pan too, since moving north or south changes it.

`get_x_scale()` moved from `main.h` to `maps.h` on the way: it is defined in
`maps.c`, takes nothing but longs, and a second front end needs it.

Maps not drawing at some zooms is **not** a bug: `maps.c:4213` gates every map
on `scale_y <= max_zoom`, per map, from the index. At `scale_y` 8000 the TIGER
roads file is outside its range and `shp_transform`/`shp_draw` never run at
all; at 2000 they do. That is the core's own layer logic and both front ends get
it.

Menus are a `GMenu` model behind a hamburger `GtkMenuButton` -- View (zoom,
redraw), Maps (grid, labels, filled, as stateful toggles synced to the config
values), and About. One model, described once.

### What it does and does not draw (superseded above)

`ASTIR_GTK4_RENDER_TO=<file.png>` renders one frame and exits. The output has
the OSM driver's attribution logo and CC-BY-SA badge on the correct
`colors[0xfd]` background, so the path from core through `xa_draw.h` through
Cairo to a PNG is live.

**No map content yet.** Measured with the project's own counters, not guessed:

    [perf] gtk4_render 50.4 ms | map_one 13.4 map_onscreen 0.0 |
    cumulative counts:
      (empty)

One `map_one` where there should be three, and `shapes_read`/`vertices`/
`draw_calls` all zero. The two TIGER shapefiles are on disk and named in
`selected_maps.sys`; they are not being reached. `index_restore_from_file()` is
called and the selected-maps file is found and read, so the thread to pull is
what else `load_maps()` needs that main.c sets up and this does not -- most
likely map-directory resolution or a map-index population step still living in
main.c. That is the next job and it is a narrow one.

### Startup the core needs and does not do itself

Three things main.c did that a second front end has to repeat, each of which
failed loudly and unhelpfully when missing:

- `user_dir` from `getpwuid()`. `get_user_base_dir()` reads it and nothing in
  the core fills it in, so every path came out as `/.astir/...`.
- `InitializeMagick()` and `curl_global_init()`. Missing the first shows up as
  an assertion inside GraphicsMagick the first time a raster map loads.
- The colour palette. `colors[]` is indexed by number by core drawing code, so
  the 102 entries and 32 trail colours were extracted from main.c's
  `GetPixelByName()` calls into `src/gtk4/xa_gtk4_palette.c` and resolved
  through `xa_color_by_name()`. Modern mechanism, identical values.

These are worth a note because they are the real shape of "what a front end
owes the core", and none of them is visible in any header.

## There is a GTK4 backend

`src/xa_draw_gtk4.c` -- Cairo for drawing, Pango for text, GTK4 for the canvas.
All 57 entry points, no X11, no Motif.

Three claims, each with its own evidence, and they are not the same claim:

| | how |
|---|---|
| the header is toolkit-neutral | compiles against gtk4 with **no X include path**; zero X headers reached, zero X symbols undefined |
| a non-X toolkit can satisfy the interface | `tools/link_null.py src --backend=gtk4` -- the core links needing **0** X symbols |
| the calls actually draw | `tools/gtk4_smoke.sh` -- 31 assertions, renders a PNG |

**It has never drawn a frame of Astir**, and cannot until a front end exists.
`main.c` is still 30,000 lines of Motif. Nothing here has been compared against
the X11 backend pixel for pixel.

The smoke test is what makes the third row worth anything. "It links" says
nothing about whether pixels come out, so `tools/gtk4_smoke.c` drives the
backend directly -- headless, since Cairo image surfaces and Pango need no
display -- and checks sampled pixel values at known coordinates plus a
distinct-colour count, so a blank frame fails rather than passing quietly.

It paid for itself immediately: `xa_region_subtract()` copied shape structs
wholesale, taking the source list's `next` pointer with the payload and splicing
the destination list into the region being read. Both region assertions failed
and everything else passed. A compiler cannot see that, a link cannot see it,
and it is easy to miss in review.

Four places where the mapping from X11 is approximate, all marked `APPROXIMATE`
in the source and all worth reading before trusting output: `XA_FUNC_XOR`
becomes `CAIRO_OPERATOR_DIFFERENCE` (same draw-twice-restores property,
different intermediate colour); font metrics come from Pango's approximations
rather than the true widest and narrowest glyph; colours are RGB rather than
colormap indices, so the palette paths in the raster drivers never run; and
`.xbm` is parsed directly because GdkPixbuf will not read it.

## The interface question is answered

`xa_draw.h` is a real abstraction, not an X11 interface wearing a hat. That was
the open question and no call-site count could settle it: an interface only ever
compiled inside X11 looks portable from in there.

`src/xa_draw_null.c` settles it by being a second implementation — all 58 entry
points, no toolkit, surfaces and pens as plain malloc'd records, text metrics a
fixed-width approximation. It compiles with **no X include path at all**, which
alone proves the header is clean.

`tools/link_null.py` does the rest: every core object, `xa_draw_null.o` in place
of `xa_draw_x11.o`, every X library struck off the link line. Result: **18 X
symbols, across 3 objects** — `rotated.o`, `color.o`, `cairo_text.o`. Every
other core object links against a backend that has never seen X11.

The three are exactly the files the earlier notes already flagged as "a backend
replaces these outright", so this is confirmation rather than news — but it is
now measured, and it is a number that can be watched.

Two traps in building that tool, both of which fired and both worth remembering
because they are the same shape as everything else that has gone wrong here:
putting the libraries before the objects on the link line made every library
look unsatisfied (160 phantom missing symbols from ImageMagick and shapelib);
and `-fno-lto`, added to get per-object attribution, made the linker unable to
read the LTO objects at all, so it reported **two** undefined symbols and looked
like a triumph. Attribution comes from `nm` now.

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

So the *Xlib* seam is in the right place: what is left is either the backend or
files a backend replaces outright. That is a different question from the Motif
coupling above — `audit_x11.py` checks names against `<X11/Xlib.h>` and
`<X11/Xutil.h>`, so `XmTextReplace` and `XtPopup` are not in these numbers at
all. `db.o` and `messages.o` look clean here and are not.

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

## Starting a new session: do this first

**There is no baseline snapshot in the tree.** The golden capture this work was
verified against lived in a session scratch directory and is gone. Do not skip
re-establishing one — comparing against a stale or absent baseline is how this
branch lost most of a session.

```bash
./build.sh                                  # incremental; -j3 on purpose, see build.sh
python3 tools/link_core.py src              # expect: LINKED CLEANLY
./tools/snapshot_ab.sh /tmp/base1.xpm       # ~3-6 min each
./tools/snapshot_ab.sh /tmp/base2.xpm
cmp /tmp/base1.xpm /tmp/base2.xpm           # MUST match, or the harness cannot tell A from B
```

The second capture is not optional. It is the check that would have caught the
capture race immediately, and it costs one run. A correct baseline here is
**822 colours** — `head -3 /tmp/base1.xpm | tail -1` should read
`"640 425 822 2"`. If it says 792, the render was captured half-drawn and the
harness has regressed.

Then `./tools/bench-attrib.sh 1.0 4 base` should give `shapes_read 7444`,
`vertices 219817`, `draw_calls 3458`.

If the work touches the message windows, take a trace baseline the same way and
for the same reason — two runs, and they must match:

```bash
./tools/trace_ab.sh /tmp/tbase1.trace     # ~4 min each
./tools/trace_ab.sh /tmp/tbase2.trace
diff /tmp/tbase1.trace /tmp/tbase2.trace  # MUST be empty
```

A correct baseline here is **239 raw records normalising to 89**, and the stderr
histogram should show `msg_insert 49` and `msg_scan_call 17`. Read that
histogram rather than trusting the diff: it is the only statement of what the
scenario actually reached.

Do not run Astir while building — `build.sh` explains why (a saturated compile
plus a GUI app hung the GPU on this machine and corrupted five object files).

**The branch is local-only.** `perf-and-gui` has no upstream tracking branch and
is 11 commits ahead of nothing. `origin` is the fork, `upstream` is Xastir/Xastir.
Nothing here has been pushed.

## How to verify a change (read this first)

Three harnesses, and picking the wrong one gives a confident wrong answer.

- **`tools/snapshot_ab.sh <out.xpm>`** — pixels, via Astir's own snapshot
  facility, so window stacking is irrelevant. Required for anything touching
  text, colour, images or drawing. `SNAP_BIN=<path>` runs a different binary,
  which is the only way to A/B a change that is already committed (build a
  worktree at the older commit — and diff its `config.h` against this tree's
  before believing the result).
- **`./tools/bench-attrib.sh 1.0 4 <tag>`** — counters. Identical output means
  identical geometry: `shapes_read 7444`, `vertices 219817`, `draw_calls 3458`
  cumulative at `lod=1.0 zoomout=4`. **Blind to text and colour.**
- **`tools/trace_ab.sh <out.trace>`** — message-window operations, with
  arguments. Required for anything touching `update_messages()` or the window
  management in `messages.c`, because the other two are blind to all of it —
  measured, not assumed: a planted defect there left the pixel capture
  byte-identical. **Blind to everything else**; its stderr histogram says which
  paths the scenario actually reached, and three do not run at all.

Do **not** use `ab-shot.sh` for A/B. It screenshots the whole screen.

### The pixel harness is flaky, and it fakes a regression

Found the hard way this session, after it produced a clean, reproducible,
completely false regression that triggered a bisect.

Astir allocates its palette with `XAllocColor` from the shared colormap. Start
it seconds after the previous instance exited and the server may still hold
those entries, so allocation returns *approximations*. The frame renders,
settles, passes the two-identical-snapshots check, and carries about 180 extra
near-black shades. 822 colours becomes 964.

It survived a revert-and-restore, because every "good" run happened to follow a
five-minute rebuild and every "bad" one followed another Astir within seconds.

**The tell was in the numbers all along: the bad runs were 964 and 967.** Two
runs of one build disagreeing is not a regression, it is a broken measurement.
The same rule that caught the half-drawn-frame race, at the other end of the run.

`snapshot_ab.sh` now sleeps 5 s after killing the previous instance and always
prints the colour count, warning when it is not 822. `SNAP_EXPECT_COLORS=any`
when a change is meant to alter the palette.

**Do not bisect a pixel difference until a spaced-out re-run reproduces it.** A
bisect on a flaky measurement will confidently name whichever change happened to
be in the tree when the flake fired — here it named an innocent one, and the
`.text` sections of every object were byte-identical across it.

### The harness was broken until this session, and silently

`snapshot_ab.sh` deleted the snapshot before starting Astir and took the first
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

All in `tools/`, all documented in `tools/README.md`.

| tool | use |
|---|---|
| `link_core.py` | **ground truth** for the boundary — actually links the core |
| `core_boundary.py` | cheap nm version of the same question |
| `audit_x11.py` | Xlib call sites. Counts `#ifdef`'d-out code as live — see above |
| `split_scope.py` | where the GUI/core seam runs inside one file, transitively |
| `split_file.py` | performs a split; refuses unless the bytes reassemble |
| `drop_first_arg.py` | removes a leading argument from calls, not definitions |
| `extract_settings.py` | relocates plain-data definitions between files |
| `snapshot_ab.sh` | pixel-level A/B. `SNAP_BIN=` to run another binary |
| `snapshot/snap.cnf` | the exact configuration the pixel A/B runs under |
| `trace_ab.sh` | operation-level A/B for the **message windows**, which the pixel harness cannot see |
| `trace_norm.py` | normalises a raw trace; its stderr is the coverage report — read it |
| `trace/messages.log` | the replayed scenario |

## Where to pick up

In rough order of value per unit of risk:

1. ~~**Build a harness that can see something other than the map canvas.**~~
    **Done.** `tools/trace_ab.sh`, proven deterministic, sensitive, and
    necessary.
2. ~~**`messages.c`**~~ and ~~**`db.c`'s `update_messages()`**~~ — **both done**,
    in one move, because `mw[]` could not leave `messages.c` until `db.c` had
    stopped naming it. Eight callbacks; trace byte-identical.
3. ~~**Get `astir.h` off `X11/Intrinsic.h`.**~~ **Done** — 28 of 65 core `.c`
    files now pull in no X header. **Two roots remain**, and they are the next
    job: `main.h:27` includes `<X11/Intrinsic.h>` and core files include
    `main.h` for non-GUI declarations; and `db.c` includes `db_gui.h`, a Motif
    header, which is a layering question rather than a header one. Same
    technique both times — move the widget-typed declarations to a `_gui.h`,
    let the compiler find the callers, and add the header to `build.sh`'s
    neutrality check so it cannot come back.
4. ~~**Write a second backend.**~~ **Done twice** -- a null one to prove the
    header is neutral, and a real GTK4 one. See above.
    **Next**: the front end. That is the only thing left between this and a
    running GTK4 Astir, and it is by far the largest piece -- `main.c` is
    ~30,800 lines and the front end ~69,000 across 16 files. The drawing half of
    the problem is solved and proven; the dialogs, menus and event loop are not
    started. A first target would be a GtkApplicationWindow with a
    GtkDrawingArea whose draw function paints `xa_gtk4_canvas_surface()`, plus
    the 24 `xa_ui.h` callbacks -- enough to see a map.

5. **Get the untested message-window paths exercised.** One of three done, by
    deletion: `clear_acked_message`'s scan is gone with its dead loop. Two
    remain. `msg_destroy` needs GUI interaction. `mode=selected` was covered for
    one commit and the coverage was **withdrawn** — the only way found to reach
    it makes the scenario's sort order non-deterministic, and a flaky baseline
    is worse than an uncovered branch. See `tools/README.md`. Still open for
    `xa_image_*` (needs OSM
    tiles) and `xa_bitmap_load` (needs an active alert); `ASTIR_REPLAY` is now
    the tool for building all of them, since it drives Astir from a packet log
    with no GUI interaction.

`rotated.c` is the one core file with real Xlib left that is not the backend. It
is not a conversion target — Pango does rotated text natively — so it belongs to
whichever backend comes next, not to this layer.
