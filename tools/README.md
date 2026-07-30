# Modernization tools

Measurement and migration scripts for the Wayland/GTK4 port.  All take the
source directory as an argument; none hardcode a path.  The measurement ones
are worth re-running, the migration ones are kept because the remaining work
(image/font calls, a GTK4 backend, the rest of the UI callbacks) is more of the
same.

Run the measurement scripts against a **built** tree — several read `src/*.o`.

| script | what it answers |
|---|---|
| `../audit_x11.py src` | Xlib call sites, by file and primitive. Strips comments and string literals, which a plain grep does not. Reports **two** numbers: the Stage 2 drawing scope, and everything else Xlib (fonts, colours, images, regions, cursors). Read the second one before claiming the tree is nearly toolkit-independent. |
| `link_core.py [src]` | **Ground truth.** Actually links the core objects with no front end and reports what fails, attributed to the object that would have to provide it. Slower than the nm scripts and the only one that cannot be wrong about the answer. |
| `../core_boundary.py src` | What each object needs from `main.o` *and* from the rest of the front end, split into data and functions. The cheap version of `link_core.py`. |
| `classify_syms.py src <obj.o>...` | For one object, *which* symbols it needs and whether they are GUI-typed (Widget, GC, Pixmap...) or plain data. Turns a bare count into a decision. |
| `find_dupes.py [minlines]` | Repeated code blocks touching drawing or the interrupt idiom — how the duplicate `XCopyArea` and settings families were found. |
| `convert_draw.py src <file.c>... [--apply]` | Rewrites Xlib drawing calls to `xa_draw`. Parses calls with balanced parens (they span many lines) and skips comments/strings. Dry run by default. |
| `extract_settings.py src <outbase> [--apply]` | Relocates plain-data definitions out of `main.c` into a core file, reading the target symbol list on stdin. Moves definitions verbatim so no call site changes. |
| `split_file.py <src.c> <dest.c> <fn,fn,...> [--header=f] [--apply]` | Performs the split: moves the named functions, with their doc comments and any `#if` that wraps one, into a new file. Dry run by default. Refuses to write unless the moved and kept spans reassemble the original byte for byte. |
| `drop_first_arg.py <file.c> <fn,fn,...>` | Removes a leading argument from *calls*, not definitions, parsing the argument list with balanced parens. Skips definitions (`{` after the closing paren) and declarations (only a type before the name). See the caution below before using it on signatures that have already been edited. |
| `snapshot_ab.sh <out.xpm>` | Pixel-level A/B, via Xastir's own snapshot facility. `SNAP_BIN=` runs another binary. Requires `snapshot/snap.cnf`, which is committed so the comparison configuration is fixed rather than whatever happened to be in `~/.xastir`. |
| `split_scope.py [--gui=fn,fn] src <file.c>...` | Where the GUI/core seam runs inside one file: which functions have Motif in the **body**, which are pulled in transitively by *calling* one that does, which merely carry a `Widget` in the signature, and which file-scope names both halves touch. `--gui=` adds known-GUI names defined in other files (`redraw_symbols`, `pos_dialog`, `resize_dialog`). |

## The file splits were blocked on fonts, not on dialogs

Resolved, but the shape of it is worth keeping. `split_scope.py` with
call-coupling on reported:

| file | GUI then | now | what had pulled the core half across |
|---|---|---|---|
| `cad_objects.c` | 74% | **split** | dialogs and `redraw_symbols` |
| `maps.c` | 55% | 20% | eleven crossings, every one text measurement |
| `draw_symbols.c` | — | 14% | `draw_symbol`, via `XQueryFont` / `XSetClipOrigin` |

In `maps.c` the entire grid subsystem — `draw_grid`,
`draw_complete_lat_lon_grid`, both UTM grid functions,
`actually_draw_utm_minor_grid`, the biggest core functions in the file —
reached Motif through nothing but `get_border_width`,
`get_rotated_label_text_length_pixels` and `draw_rotated_label_text_common`,
which called `XLoadQueryFont`, `XTextExtents` and `XFreeFont`. No dialog was
anywhere in that chain. Splitting the file first would have put the grid code on
the GUI side because it measures text.

Fonts now go through `xa_draw.h`, both files have zero crossings, and the two
splits are unblocked. The general lesson: run the transitive check before
choosing which file to cut, because the first-order answer named the wrong
blocker.

## Reading the Xlib numbers

`audit_x11.py` reports two numbers that answer different questions — "how much
of the Stage 2 conversion is done" and "how much Xlib is left". Quote either one
only with its scope attached.

As of the map-driver work they are 34 drawing call sites in **3** files and 49
other Xlib call sites in core files, and both are now concentrated in code that
is platform implementation rather than application logic: `xa_draw_x11.c` (the
backend — expected, this is the file a second backend replaces), `rotated.c`,
`cairo_text.c` and `color.c`. The map drivers are at zero. So neither number is
a to-do list any more; the remaining work is writing a second backend, not
converting more call sites.

**It counts code that is not compiled.** `map_geo.c`'s XPM branch is inside
`#else` of `#ifdef HAVE_MAGICK`, and `HAVE_MAGICK` is defined here, so those
call sites are unreachable in this configuration while still appearing in the
totals. Check the `#ifdef` nesting before treating a cluster as live — the same
caution as the object-file scripts below, for the same reason.

A name starting with `X` is not evidence it is Xlib: `XTIFFClose` is libtiff,
`XRotDrawAlignedString` is `rotated.c`'s own API, and `XA_CHECK` is a local
macro in `xa_draw_x11.c`. Those three alone would have added 41 to the count.
The script checks each name against `<X11/Xlib.h>` and `<X11/Xutil.h>` instead
of guessing, and says so rather than reporting zero if those headers are absent.

## Verifying a change that alters pixels

The counter checks in `bench-attrib.sh` cannot see text. They count shapes,
vertices and draw calls, all of which stay identical while a font change moves
every label by a pixel. For anything touching text or colour, compare the
rendered map itself.

`snapshot_ab.sh <out.xpm>` does that, using Xastir's own snapshot facility:
`SNAPSHOTS_ENABLED:1` makes it write `pixmap_final` to
`~/.xastir/tmp/snapshot.xpm`, which is the finished frame with no window
involved. Run it against each build and `cmp` the two files.

`SNAP_BIN=<path>` runs a binary from somewhere else instead of `./src/xastir`.
Without it there is no way to A/B a change that is already committed; with it,
build a `git worktree` at the older commit and point at that. Diff the
worktree's `config.h` against this tree's before believing the result — a
differently configured baseline is a differently rendered one.

### The fourth way to get a worthless result: capture the frame too early

This one is worse than the three below, because it is *reproducible*.

Snapshots fire the moment they are enabled, which is before the maps have
finished drawing. The script used to delete the snapshot before starting Xastir
and take the first one that appeared, so the captured frame could be partial —
features simply missing, reading as background grey. That is indistinguishable
from a broken clip region.

It cost most of a session. A correct change appeared to alter 17.6% of the frame.
The geometry counters were identical, which reads as "same draw calls issued,
pixels suppressed". And three separate baseline captures were byte-identical, so
the harness looked trustworthy — they were byte-identical *partial renders*. The
settled frame has 822 colours; every capture the harness had produced until then
had 792, including the ones four commits had already been verified against.

The script now discards everything written before the render settles and requires
two consecutive fresh snapshots to be byte-identical before emitting one. If it
never settles it writes nothing and exits non-zero, rather than leaving the
previous run's file for `cmp` to find and pass.

**"Reproducible" is not "correct".** A deterministic measurement of the wrong
thing survives every retry, which is exactly what makes it expensive. When a
measurement and the code disagree, probe the runtime values — the pen state, the
window id, the actual arguments — before believing either one.

### Measure coverage, or the A/B is verifying nothing

An identical A/B only means something if the changed code ran. Put counters in
the code under test and run once. That is what showed
`get_hole_clipping_context()` runs 83 times per 300 shapefile draws (covered),
while every `xa_image_*` entry point runs zero times (not covered) even though an
OSM map is selected, and the weather-alert block runs zero times too.

Cheap, and it is the difference between "verified" and "verified something else".

Three other approaches were tried first and all three produced worthless
results rather than failing:

- **`ab-shot.sh` captures the whole screen** (`spectacle -b`), so it only works
  if Xastir is the visible unobscured window. One side came back as a blank grey
  Xastir window, the other as the browser that happened to be on top of it, and
  the comparison dutifully reported a 7% difference. `bench-attrib.sh`'s header
  already warns it was written to avoid exactly this.
- **Cropping the screenshot** to the Xastir window does not help, because the
  window content was wrong, not the framing.
- **`xwd` / `xwininfo` / `xdotool` are not installed here**, so capturing a
  specific window by id was not available either.

`import -window` exists but needs a window id from one of the missing tools.

## Link it before believing it

`core_boundary.py` was quoted for eight commits as the measure of how close the
core is to standing alone. Linking it proved two things wrong at once:

- **It measured `main.o` only.** That was the original question — `main.c` is
  31k lines and the obvious obstacle — but "can the core link" is a question
  about the *whole* front end. The real answer is 46 symbols across 12 objects;
  `main.o` accounts for 11. Twelve `*_gui.o` files were never in the query.
- **It printed a GUI-inclusive count under a core-only heading.** `core objects
  needing main.o : 4` followed by `distinct main.o symbols they need: 38`, where
  the 38 accumulated over every object in the tree. "They" read as the four.
  The core-only figure is 11.

Both are fixed, and the nm answer now matches the linker exactly. Prefer
`link_core.py` when the answer matters.

Two ways to get a confidently wrong result from the trial link, both of which
happened before the numbers above were believed:

- **The session shell is fish, which does not word-split on expansion.** An
  object list passed as `$OBJS` arrives as a single filename, the link dies
  with "File name too long", and a grep for `undefined reference` finds nothing
  — reporting a clean link. Use a response file.
- **`-flto` plus a stub `main()` that calls nothing discards the entire
  program.** The link then succeeds, proving the exact opposite of what it
  appears to. Force every core-defined symbol live with `-Wl,-u` first.

## What the object-file measurements cannot see

`core_boundary.py` and `classify_syms.py` read `nm` output, so they report what
**this configuration** links — not what the source couples to.

- **Code behind an `#ifdef` that is off is invisible.** `db_gis.c` and
  `map_cache.c` sit behind `HAVE_POSTGIS`/`HAVE_MYSQL` and `USE_MAP_CACHE`.
  With those off their objects are 17k and 4k of nearly nothing, and
  `core_boundary.py` called both clean while ten `statusline()` calls sat in
  the source. A build configured with those features re-links `main.o` into
  both. Before calling a count zero, confirm it with a source scan — reuse
  `audit_x11.py`'s `strip()` so comments and string literals do not inflate it.
- **A configuration you cannot build is a change you cannot verify.** Neither
  libdb nor libpq is installed here, so edits to those regions are unverified
  by anything stronger than being identical to edits made elsewhere. Say so
  rather than letting the commit imply otherwise.
- **`objdump -d` on these objects shows nothing.** The build uses `-flto=auto`,
  so `.o` files carry `.gnu.lto_*` sections and a zero-length `.text`.
  Comparing two builds by disassembly silently "passes" because both sides are
  empty. `nm` still works — the symbol table is real — which is why the
  boundary scripts are unaffected. To show that a cosmetic edit did not change
  behaviour, re-run the benchmark; do not diff the objects.

## Hard-won cautions for anything that rewrites call sites

Both bugs below produced code that *compiled*, which is why they are worth
repeating rather than rediscovering:

- **Strip comments from each argument before flattening a call onto one line.**
  A trailing `//` otherwise swallows every argument after it, e.g.
  `xa_pen_dashes(gc_tint, 0, // dash offset dash, // dash list[] 2);`
- **Watch the end index.** Replacing up to `i - 1` when `i` is already past the
  closing paren re-emits it: `xa_ui_status(st));`

- **A brace in a string literal will silently move every function boundary
  after it.** `split_scope.py` stripped comments but not string or char
  literals, and `db.c` line 993 is `'}'` — the APRS message-acknowledgement
  character. One unbalanced brace, and the tool reported `alert_data_add` as
  spanning lines 1554-17480 with 42673 bytes and 12 Motif references. It is 102
  lines long and contains no Motif; what it had swallowed was
  `update_messages()`, the file's actual GUI function.

  Nothing about the output looked wrong — a big function full of Motif in a big
  file is exactly what you expect to find. Fixed by blanking literals as well as
  comments, after which `db.c` reports the useful answer: 2 functions contain
  Motif, not 6, and 9 more are pulled in transitively by calling them.

  This is the same tool the README credits with finding that fonts, not dialogs,
  were blocking the file splits. Any file with a `'{'` or `'}'` literal in it got
  a wrong answer from it until now.

- **A function can be wrapped in a preprocessor conditional.** `split_file.py`
  moved `clsd_menuCallback` out of `cad_objects.c` and left its
  `#ifndef USE_COMBO_BOX` behind: an empty conditional in the source file, and
  an unguarded definition in the destination. Both files compiled. The tell
  was that the function became a global symbol in a build where the guard
  should have excluded it — worth checking `nm` against the source function
  list after any move, not just that the tree builds.

- **Enumerate the values before rewriting them, and let the compiler backstop
  the sweep.** The 40-function Widget strip — 67 signatures, 99 call sites, 15
  files — was uneventful for two reasons. Every call site's first argument was
  extracted with balanced-paren parsing and comments/strings stripped *first*,
  which showed exactly three distinct values (`w` ×90, `NULL` ×2, `da` ×1); the
  rewrite then dropped only an argument that was one of those three, and printed
  anything it declined to touch. And C will not compile a call with the wrong
  argument count, which caught the two things no name-based sweep can see:
  leftover `(void)w;` statements, and `map_driver_ptr->func(w, ...)`, called
  through a function pointer and so having no name to match. The table's own
  type had to lose its `Widget` too.

  Note the ordering hazard: once the `Widget` is gone from the definitions,
  `drop_first_arg.py` run blindly would drop the first *real* parameter from any
  definition its skip heuristics miss. Match on the argument's value, not just
  the function's name.

When a script that edits hundreds of sites turns out to be wrong, revert every
file it touched and redo — do not patch its output. Some of the corruption it
produced will compile. This applied to the split above: the fix went into the
script and the whole split was re-run, rather than the two stray directives
being deleted by hand.

Also: a tool reporting "I can't handle these N cases" is itself a claim worth
checking. `extract_settings.py` once reported 34 symbols as immovable; the real
cause was that its trailing-comment pattern accepted `//` but not `/* */`.
