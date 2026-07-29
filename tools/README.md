# Modernization tools

Measurement and migration scripts for the Wayland/GTK4 port.  All take the
source directory as an argument; none hardcode a path.  The measurement ones
are worth re-running, the migration ones are kept because the remaining work
(image/font calls, a GTK4 backend, the rest of the UI callbacks) is more of the
same.

Run the measurement scripts against a **built** tree — several read `src/*.o`.

| script | what it answers |
|---|---|
| `../audit_x11.py src` | How many real Xlib call sites remain, by file and primitive. Strips comments and string literals, which a plain grep does not. |
| `../core_boundary.py src` | What each object still needs from `main.o`, split into data and functions. The gating measurement for core extraction. |
| `classify_syms.py src <obj.o>...` | For one object, *which* symbols it needs and whether they are GUI-typed (Widget, GC, Pixmap...) or plain data. Turns a bare count into a decision. |
| `find_dupes.py [minlines]` | Repeated code blocks touching drawing or the interrupt idiom — how the duplicate `XCopyArea` and settings families were found. |
| `convert_draw.py src <file.c>... [--apply]` | Rewrites Xlib drawing calls to `xa_draw`. Parses calls with balanced parens (they span many lines) and skips comments/strings. Dry run by default. |
| `extract_settings.py src <outbase> [--apply]` | Relocates plain-data definitions out of `main.c` into a core file, reading the target symbol list on stdin. Moves definitions verbatim so no call site changes. |
| `split_scope.py src <file.c>...` | Where the GUI/core seam runs inside one file: which functions have Motif in the **body**, which merely carry a `Widget` in the signature, and which file-scope names both halves touch — that last list is what a split actually costs. |

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
