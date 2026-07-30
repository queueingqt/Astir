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
| core objects still *calling* Motif directly | ? | 2 (`db.o`, `messages.o`) | 2 — unchanged |
| map drivers naming `Widget` | all | all | **0** |
| drawing Xlib call sites outside the backend | — | 7 | 7 |
| harnesses that can see the message windows | 0 | 0 | **1** |
| lines of GTK4 front end written | 0 | 0 | **0** |

This session moved no boundary. It built the thing that makes the next two moves
verifiable, and proved it works — which the previous session's notes identified
as the highest-value next step precisely because four commits had nearly claimed
verification they had not earned.

## The next boundary: two core objects still call Motif

`link_core.py` cannot see this, and it is worth understanding why. It links the
core against the *full* library set, so a Motif call inside a core object
resolves happily from `-lXm` and the link stays clean. Asking `nm` about the
individual objects asks a different question, and the answer is:

| object | needs | what it is |
|---|---|---|
| `db.o` | 7 `XmText*` / `XtFree` | **`update_messages()`** — renders the message window |
| `messages.o` | `XmTextFieldGetString`, `XtDestroyWidget`, `XtFree`, `XtPopup` | 4 window-management functions, and `mw[]` itself |
| `xa_draw_x11.o` | 50 | the backend. Expected. |
| `rotated.o`, `cairo_text.o`, `color.o` | 19 | renderer/platform implementation |

`map_tif.o`'s `XTIFFOpen`/`XTIFFClose` are **libtiff**, not Xlib — the trap
`tools/README.md` warns about, which a first pass here fell into anyway.

So: the core links without the front end, and two core objects still drive Motif
widgets directly. Both statements are true, and the second is the next job.

`Message_Window mw[MAX_MESSAGE_WINDOWS+1]`, an array of Motif widgets, is
**defined in `messages.c`** — a core file. 245 of its uses are in
`messages_gui.c`, where it belongs; 27 are in `messages.c` and 15 in `db.c`.

### Why `update_messages()` was not done, and what it needs

`db.c` is one function from toolkit-free. That function was deliberately left
alone, for a reason that should survive:

- ~~**Nothing here can verify it.**~~ **Fixed this session.** `snapshot_ab.sh`
  captures `pixmap_final`, the map canvas, and the message window is a separate
  Motif dialog that does not appear in it at all. `tools/trace_ab.sh` now covers
  it — see below. This was the blocker and it is gone.
- ~~**`pos` is never reset.**~~ **That was wrong, and it made the job look
  harder than it is.** There are two resets, not one: the commented-out
  `//pos=0;` at the top of the per-window loop, *and* a live `pos = 0;` at
  `db.c:1777`, inside `if(strlen(temp1)>0)` and before the message list is
  built. Every read of `pos` is dominated by that assignment, so despite being
  `static` it behaves exactly as a local. There is no cross-call accumulation to
  reproduce.
- **The obvious move is still the wrong one.** The function cannot simply move
  to `messages_gui.c`: it reads `msg_data`, `msg_index`, `msg_index_end`, which
  are `static` in `db.c` on purpose. Moving it means exporting the message
  store, which trades a layering problem for a worse one.

Two designs, neither attempted:

1. **Narrow view callbacks** in `xa_ui.h` — `msg_window_is_open(i)`,
   `msg_window_callsign(i, out, n)`, `msg_window_clear(i)`,
   `msg_window_append(i, text, hl_from, hl)`, `msg_window_show_end(i)`. Matches
   the existing 16-callback pattern, keeps the message store private, leaves the
   core pushing view updates.
2. **Model/view split** — `db.c` grows an accessor returning the time-sorted
   messages for a callsign, and `update_messages()` moves to `messages_gui.c` and
   renders from that. Cleaner, larger, and the highlighting logic (`acked` 0/2/3,
   reverse video, `is_my_call`) has to come across exactly.

**That harness now exists.** `tools/trace_ab.sh`, built and proven this session
— see "The message windows have a harness now" below. Both designs above are
unblocked.

`messages.c` is the easier half and a reasonable first move: its 4 Motif
functions are all window management, they belong in `messages_gui.c` outright,
and `split_file.py` refuses to write unless the bytes reassemble. The cost the
tool reports is 3 shared file-scope names (`last_check_and_transmit`,
`message_pool`, `send_message_dialog_lock`) and one call to invert
(`check_and_transmit_messages` → `clear_acked_message`).

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

**Read `tools/README.md` for what the scenario does not reach.** Three paths
never fire — `clear_message_windows`, `clear_acked_message`'s scan, and the
`mode=selected` reverse-video branch — all of them outbound-message paths that a
receive-only replay cannot get to. A clean diff says nothing about those three.

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
    necessary. This unblocks 2 and 3.
2. **`messages.c`** — the smallest well-understood piece of real coupling left,
    and now verifiable. Its 4 Motif functions reduce to three operations the
    core actually needs (`is_open`, `callsign`, `raise`) plus a `close_all`,
    which is the same narrow-callback shape as the existing 16 in `xa_ui.h`.
    Do not move `check_popup_window` to `messages_gui.c` as previously
    suggested: most of it is core logic, and only the field read and the
    `XtPopup` belong to the view. Two of the four Motif calls, in
    `clear_acked_message`, are dead code and can simply go.
    **Coverage caveat**: the scenario reaches `check_popup_window` and
    `look_for_open_group_data` but not `clear_message_windows` — check the
    histogram, do not assume.
3. **`db.c`'s `update_messages()`** — now verifiable, and easier than recorded:
    the `static pos` trap does not exist (see above). The trace records
    `msg_clear` / `msg_insert` / `msg_highlight` / `msg_show` with their exact
    arguments, so a faithful move produces an identical diff.
4. **Write a second backend.** `xa_draw.h` is ~40 entry points and the X11 one is
    ~1000 lines. A backend implementing only the drawing and pen calls, with text
    stubbed, is enough to find out whether the interface is actually sufficient —
    which is the open question, and one the call-site count cannot answer.
5. **Get the untested abstractions exercised**: a scenario that renders OSM
    tiles, and one with an active weather alert. Cheap, and `xa_image_*` and
    `xa_bitmap_load` are inspection-only until then. `XASTIR_REPLAY` is now the
    tool for building scenarios like these — it drives Xastir from a packet log
    with no GUI interaction, which is how the weather-alert case gets its alert.

`rotated.c` is the one core file with real Xlib left that is not the backend. It
is not a conversion target — Pango does rotated text natively — so it belongs to
whichever backend comes next, not to this layer.
