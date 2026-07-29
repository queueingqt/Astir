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
