/*
 * xa_draw.h -- drawing abstraction layer (Stage 2 of the modernization plan).
 *
 * Purpose: funnel every drawing operation through one interface so the backend
 * can be swapped without touching call sites.  Today the only backend is
 * xa_draw_x11.c, a thin mapping onto the existing Xlib calls and GCs, and the
 * output must stay pixel-identical.
 *
 * The replacement backend is deliberately NOT named here.  GTK4 does not draw
 * through Cairo -- that was GTK3; GTK4 renders GSK node trees through Vulkan or
 * GL, with Cairo only as a fallback path -- so "which renderer" is an open
 * decision, not a settled one.  This layer exists precisely so that decision
 * can be made and remade against measurements without editing 452 call sites.
 *
 * This header deliberately includes no X11 headers.  Code that draws should be
 * able to compile without knowing which backend is in use.
 *
 * Measured scope of the conversion (the C sources, with comments and string
 * literals excluded): 452 call sites, 17 files, 28 Xlib primitives.  Half of
 * those sites are GC state changes rather than drawing, which is why the
 * eventual interface is context-based -- Cairo is a state machine too.
 */

#ifndef XA_DRAW_H
#define XA_DRAW_H

/*
 * An opaque handle to something that can be drawn on or copied from.
 *
 * It is an X11 Drawable (XID) today, which is why the underlying type is an
 * unsigned long: it lets callers pass the existing `pixmap`, `pixmap_final`
 * and `pixmap_alerts` globals unchanged during the transition.  Callers must
 * not interpret the value or do arithmetic on it, so that a later backend can
 * redefine it as an index or a pointer to its own surface type.
 */
typedef unsigned long xa_surface_id;

// A surface handle that refers to nothing.
#define XA_SURFACE_NONE ((xa_surface_id)0)


/*
 * The on-screen surface that finished frames are presented to, and the current
 * size of that canvas.  These are queried rather than assumed so that no call
 * site has to name the drawing area, and so a backend that presents somewhere
 * else (a GTK4 widget, an offscreen compositor buffer) only has to change here.
 *
 * xa_screen_target() returns XA_SURFACE_NONE before the canvas exists.
 */
xa_surface_id xa_screen_target(void);
void          xa_canvas_size(int *width, int *height);


/*
 * Copy a rectangle between surfaces.  The general primitive; everything else
 * that moves pixels is expressed in terms of it.
 *
 * A copy that is wholly or partly outside either surface is the caller's
 * responsibility, exactly as with the Xlib call this replaces.
 */
void xa_copy_area(xa_surface_id src,
                  xa_surface_id dst,
                  int src_x, int src_y,
                  int width, int height,
                  int dst_x, int dst_y);


/*
 * Present a full-screen offscreen surface to the visible canvas.
 *
 * This replaces 37 hand-written copies of the same ten-line XCopyArea block --
 * two thirds of every XCopyArea in the tree.  They were byte-for-byte
 * identical apart from the source surface: always the map drawing area, always
 * the shared GC, always the whole canvas at offset 0,0.
 *
 * Xastir calls it to push a partly-drawn map to the screen when rendering is
 * interrupted, and to show a finished frame.  Under GTK4/Wayland this becomes
 * buffer presentation, so keeping it as one named operation is what makes that
 * substitution possible.
 *
 * Does nothing if the canvas does not exist yet, rather than issuing a request
 * against a window that has not been created.
 */
void xa_present_full(xa_surface_id src);

#endif // XA_DRAW_H
