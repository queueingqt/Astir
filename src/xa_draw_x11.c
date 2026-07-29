/*
 * xa_draw_x11.c -- X11/Xlib backend for the drawing abstraction.  See xa_draw.h.
 *
 * Thin by design: each entry point maps onto the Xlib calls the code already
 * made, so converting a call site cannot change what is drawn.  Any pixel
 * difference after a conversion is a bug in that conversion.
 *
 * Everything platform-specific is confined here: which widget is the canvas,
 * which GC is used, and how a drawable is named.  Callers name none of that.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/Intrinsic.h>

#include "xastir.h"
#include "main.h"
#include "xa_draw.h"

// da, gc, screen_width and screen_height come from xastir.h.


xa_surface_id xa_screen_target(void)
{
  Window win;

  if (da == (Widget)NULL)
  {
    return XA_SURFACE_NONE;
  }

  // XtWindow() is None until the widget is realized.
  win = XtWindow(da);
  if (win == None)
  {
    return XA_SURFACE_NONE;
  }
  return (xa_surface_id)win;
}


void xa_canvas_size(int *width, int *height)
{
  if (width)
  {
    *width = (int)screen_width;
  }
  if (height)
  {
    *height = (int)screen_height;
  }
}


void xa_copy_area(xa_surface_id src,
                  xa_surface_id dst,
                  int src_x, int src_y,
                  int width, int height,
                  int dst_x, int dst_y)
{
  if (src == XA_SURFACE_NONE || dst == XA_SURFACE_NONE)
  {
    return;
  }
  if (width <= 0 || height <= 0)
  {
    return;
  }

  (void)XCopyArea(XtDisplay(da),
                  (Drawable)src,
                  (Drawable)dst,
                  gc,
                  src_x,
                  src_y,
                  (unsigned int)width,
                  (unsigned int)height,
                  dst_x,
                  dst_y);
}


void xa_present_full(xa_surface_id src)
{
  xa_surface_id target = xa_screen_target();
  int w, h;

  // The open-coded copies this replaces did not check whether the canvas
  // existed, so an early call issued a request against a window that was not
  // yet created.  There is one place to check now.
  if (target == XA_SURFACE_NONE)
  {
    return;
  }

  xa_canvas_size(&w, &h);
  xa_copy_area(src, target, 0, 0, w, h, 0, 0);
}
