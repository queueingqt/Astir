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
| core `.c` files pulling in **no** X header at all | 0 | 0 | **28 of 65** |
| core headers rooting the X include tree | 3 | 3 | 1 (`main.h`) + two `_gui.h` included by core |
| lines of GTK4 front end written | 0 | 0 | **0** |

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
**defined in `messages.c`** — a core file — and declared in `xastir.h`, which
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

### xastir.h is off X11; main.h is the next root

`xastir.h` opened with `#include <X11/Intrinsic.h>` and is included by every
core file in the tree, so the shapefile reader and the APRS parser got Xt
whether they wanted it or not. That line is gone.

What was in there split three ways:

- **Drawing objects** — `gc`, `gc2`, `gc_tint`, `gc_stipple`, `gc_bigfont`, the
  seven pixmaps and `colors[]` — moved to `xa_draw.h` in the neutral types.
  This is not a widening: `Pixmap` and `Pixel` *are* `unsigned long`, matching
  `xa_surface_id` and `xa_color`, and `GC` converts implicitly to `xa_pen`'s
  `void *`, which is why `xa_pen` was made `void *` in the first place. Core
  drawing code already treated them this way — no core file ever passed one to
  an Xlib call. The backend's definitions were changed to match, so declaration
  and definition agree exactly rather than merely being layout-compatible.
- **Widget-typed declarations** — `appshell`, `da`, `text`, `app_context`,
  `screen_x_offset/y_offset`, `resize_dialog()`, `sort_list()`,
  `redraw_symbols()`, `Last_location()`, `Jump_location()`,
  `view_all_messages()`, `INT_TO_XTPOINTER` and the `MY_*_COLOR` macros — moved
  to a new `xastir_gui.h`. None had a core caller.
- **`cmap`** moved to `xa_draw_x11.h`. Only the backend and the two renderer
  files beside it use it, and all three include X11 themselves.

`interface.h` had to follow: it declared four Widget-taking functions from
`interface_gui.c` and is included by core files, `interface.c` among them. It
compiled only because something else had already pulled in Xt — `db_gui.c`
includes `db_gis.h` before `xastir.h` and so had not, which is how it surfaced.
Those four are in `interface_gui.h` now.

`build.sh` checks `xastir.h` and `interface.h` for X-freedom on every build, so
one convenient `#include` cannot put it back unnoticed.

**What this did and did not achieve.** 28 of the 65 core `.c` files now compile
without pulling in a single X header. The other 37 still do, and `db.c`'s count
is unchanged at 106, because there are two more roots:

| root | what it is |
|---|---|
| `main.h:27` `#include <X11/Intrinsic.h>` | the front-end header, but core files include it for non-GUI declarations. 45 X-type mentions of its own. |
| `db_gui.h`, `objects_gui.h` | GUI headers *included by core files* — `db.c` includes `db_gui.h`, which pulls all of Motif |

So the honest statement is that one of three roots is gone and the mechanism is
proven; "the core builds on a machine with no X headers" is still false. `main.h`
is the same job again at a larger scale, and the two `_gui.h` includes in `db.c`
are a layering question rather than a header one.

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

Three pieces: `src/xa_trace.[ch]` (inert unless `XASTIR_TRACE` names a file),
trace points at the message-window operations in `db.c` and `messages.c`, and
`XASTIR_REPLAY=<file>` in `main.c`, which sets `read_file_ptr`/`read_file`
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

Then `./bench-attrib.sh 1.0 4 base` should give `shapes_read 7444`,
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

Do not run Xastir while building — `build.sh` explains why (a saturated compile
plus a GUI app hung the GPU on this machine and corrupted five object files).

**The branch is local-only.** `perf-and-gui` has no upstream tracking branch and
is 11 commits ahead of nothing. `origin` is the fork, `upstream` is Xastir/Xastir.
Nothing here has been pushed.

## How to verify a change (read this first)

Three harnesses, and picking the wrong one gives a confident wrong answer.

- **`tools/snapshot_ab.sh <out.xpm>`** — pixels, via Xastir's own snapshot
  facility, so window stacking is irrelevant. Required for anything touching
  text, colour, images or drawing. `SNAP_BIN=<path>` runs a different binary,
  which is the only way to A/B a change that is already committed (build a
  worktree at the older commit — and diff its `config.h` against this tree's
  before believing the result).
- **`./bench-attrib.sh 1.0 4 <tag>`** — counters. Identical output means
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

Xastir allocates its palette with `XAllocColor` from the shared colormap. Start
it seconds after the previous instance exited and the server may still hold
those entries, so allocation returns *approximations*. The frame renders,
settles, passes the two-identical-snapshots check, and carries about 180 extra
near-black shades. 822 colours becomes 964.

It survived a revert-and-restore, because every "good" run happened to follow a
five-minute rebuild and every "bad" one followed another Xastir within seconds.

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
3. ~~**Get `xastir.h` off `X11/Intrinsic.h`.**~~ **Done** — 28 of 65 core `.c`
    files now pull in no X header. **Two roots remain**, and they are the next
    job: `main.h:27` includes `<X11/Intrinsic.h>` and core files include
    `main.h` for non-GUI declarations; and `db.c` includes `db_gui.h`, a Motif
    header, which is a layering question rather than a header one. Same
    technique both times — move the widget-typed declarations to a `_gui.h`,
    let the compiler find the callers, and add the header to `build.sh`'s
    neutrality check so it cannot come back.
4. **Write a second backend.** `xa_draw.h` is ~40 entry points and the X11 one is
    ~1000 lines. A backend implementing only the drawing and pen calls, with text
    stubbed, is enough to find out whether the interface is actually sufficient —
    which is the open question, and one the call-site count cannot answer.
5. **Get the untested paths exercised.** Three message-window records still
    never appear — `msg_destroy`, `clear_acked_message`'s scan, and
    `mode=selected` — and two of them need an *outgoing* message. A send-capable
    scenario would close both at once and is the cheapest coverage available;
    the third needs a window open when the windows are cleared. Same for
    `xa_image_*` (needs OSM
    tiles) and `xa_bitmap_load` (needs an active alert); `XASTIR_REPLAY` is now
    the tool for building all of them, since it drives Xastir from a packet log
    with no GUI interaction.

`rotated.c` is the one core file with real Xlib left that is not the backend. It
is not a conversion target — Pango does rotated text natively — so it belongs to
whichever backend comes next, not to this layer.
