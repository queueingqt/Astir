/*
 * xa_draw_x11.h -- the X11 backend's own setup interface.
 *
 * Deliberately separate from xa_draw.h.  That header is what drawing code
 * includes and it names no toolkit; this one names Widget, so only the front
 * end includes it, and only to hand the backend the canvas it draws on.
 *
 * The backend used to read main.c's `da` global directly, in some forty places
 * via one static helper.  That made xa_draw_x11.o a core object that referenced
 * a front-end symbol -- one of the two objects still doing so, and the one
 * HANDOFF.md did not account for.  Receiving the canvas instead of reaching for
 * it is also what a replacement backend has to do: a GTK4 backend is handed a
 * GtkDrawingArea by whatever built the window, and cannot know the name of the
 * variable holding it.
 *
 * Call xa_x11_set_canvas() once, as soon as the drawing area exists and before
 * anything draws.  Until then every backend entry point is a no-op that returns
 * an empty value, which is the same behaviour the `da == NULL` checks gave
 * before.
 */

#ifndef XA_DRAW_X11_H
#define XA_DRAW_X11_H

#include <X11/Intrinsic.h>

void xa_x11_set_canvas(Widget canvas);

// The colormap the backend allocates into.  Was in astir.h, which every core
// file includes; only this backend and the two renderer files that sit beside
// it (color.c, cairo_text.c) use it, and all three include X11 anyway.
extern Colormap cmap;

#endif // XA_DRAW_X11_H
