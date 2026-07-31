# Archived code — reference only, not built

Nothing in here is compiled, installed, or tested. It is kept because it is the
only complete implementation of things Astir has not built yet, and reading it
is faster than reading upstream.

It leaves when GTK4 reaches feature parity, not before.

## `motif/` — the Motif front end

17 files. The complete Xastir user interface: dialogs, interface configuration,
message windows, the station list, the configuration UI, weather alerts. Astir's
GTK4 front end has none of that yet, so this is the reference for what each of
those screens has to do and which core calls it has to make.

## `draw-x11/` — the X11 drawing backend

`xa_draw_x11.c` plus the three files that were part of it: `rotated.c`
(xvertext, rotated text), `color.c`, and `cairo_text.c` (Cairo-on-Xlib text).
A second complete implementation of `xa_draw.h`, which is what proved the
interface was sufficient rather than shaped around one toolkit.

`src/draw/null/` still ships and is still built by `tools/link_null.py`, so the
"can the core link against a backend that is not the real one" test survives
this archive.

## Why these are not deleted

Deleting them costs nothing that git does not hold, and that is exactly the
argument for keeping them where they can be opened. During the port the
question is repeatedly "what did the old one do here" — for a dialog's field
validation, for the order operations happen in, for a default nobody wrote
down. A `git show` of a deleted file answers that too, but only if you already
know which file and which commit to ask for.

## What breaks if you try to build them

The includes still resolve (they were rewritten with the rest of the tree, and
point at `core/...` paths relative to `src/`), but nothing else does. The Motif
front end calls the front-end callbacks in `core/xa_ui.h`, which the GTK4 front
end now implements; two implementations cannot link together. `configure` no
longer looks for X11, Xt or Motif at all.
