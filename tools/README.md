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
| `../core_boundary.py src` | What each object still needs from `main.o`, split into data and functions. The gating measurement for core extraction. |
| `classify_syms.py src <obj.o>...` | For one object, *which* symbols it needs and whether they are GUI-typed (Widget, GC, Pixmap...) or plain data. Turns a bare count into a decision. |
| `find_dupes.py [minlines]` | Repeated code blocks touching drawing or the interrupt idiom — how the duplicate `XCopyArea` and settings families were found. |
| `convert_draw.py src <file.c>... [--apply]` | Rewrites Xlib drawing calls to `xa_draw`. Parses calls with balanced parens (they span many lines) and skips comments/strings. Dry run by default. |
| `extract_settings.py src <outbase> [--apply]` | Relocates plain-data definitions out of `main.c` into a core file, reading the target symbol list on stdin. Moves definitions verbatim so no call site changes. |
| `split_scope.py [--gui=fn,fn] src <file.c>...` | Where the GUI/core seam runs inside one file: which functions have Motif in the **body**, which are pulled in transitively by *calling* one that does, which merely carry a `Widget` in the signature, and which file-scope names both halves touch. `--gui=` adds known-GUI names defined in other files (`redraw_symbols`, `pos_dialog`, `resize_dialog`). |

## The file splits are blocked on fonts, not on dialogs

Worth knowing before starting one. Running `split_scope.py` with call-coupling
turned on says:

| file | GUI | what pulls the core half across |
|---|---|---|
| `cad_objects.c` | 74% | dialogs and `redraw_symbols` — splittable now |
| `draw_symbols.c` | — | `draw_symbol`, via `XQueryFont` / `XSetClipOrigin` |
| `maps.c` | 55% | eleven crossings, all text measurement |

In `maps.c` the entire grid subsystem — `draw_grid`,
`draw_complete_lat_lon_grid`, `draw_major_utm_mgrs_grid`,
`draw_minor_utm_mgrs_grid`, `actually_draw_utm_minor_grid`, the biggest core
functions in the file — reaches Motif through nothing but
`get_border_width`, `get_rotated_label_text_length_pixels` and
`draw_rotated_label_text_common`, which call `XLoadQueryFont`, `XTextExtents`
and `XFreeFont`. No dialog is involved. Split the file today and the grid code
lands on the GUI side for no reason but that it measures text.

So the order is: abstract fonts, then split `maps.c` and `draw_symbols.c`.
`cad_objects.c` does not depend on that and can go first.

## Reading the Xlib numbers

`audit_x11.py` reports 35 remaining drawing call sites and 122 Xlib call sites
reachable from core code. Both are true and they answer different questions —
the first is "how much of the Stage 2 conversion is done", the second is "how
much Xlib is left". Quote the first one only with its scope attached.

The 122 is not a to-do list of 122 conversions. Roughly a quarter of it is in
`rotated.c` and `color.c`, which a GTK4 backend replaces outright rather than
converts — Pango does rotated text natively, and colour allocation is a
backend concern. The number to act on is the Xlib in the map drivers and
`draw_symbols.c`.

A name starting with `X` is not evidence it is Xlib: `XTIFFClose` is libtiff,
`XRotDrawAlignedString` is `rotated.c`'s own API, and `XA_CHECK` is a local
macro in `xa_draw_x11.c`. Those three alone would have added 41 to the count.
The script checks each name against `<X11/Xlib.h>` and `<X11/Xutil.h>` instead
of guessing, and says so rather than reporting zero if those headers are absent.

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

When a script that edits hundreds of sites turns out to be wrong, revert every
file it touched and redo — do not patch its output. Some of the corruption it
produced will compile.

Also: a tool reporting "I can't handle these N cases" is itself a claim worth
checking. `extract_settings.py` once reported 34 symbols as immovable; the real
cause was that its trailing-comment pattern accepted `//` but not `/* */`.
