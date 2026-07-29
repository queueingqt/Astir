/*
 * xa_draw_x11.c -- X11/Xlib backend for the drawing abstraction.  See xa_draw.h.
 *
 * Thin by design: each entry point maps onto the Xlib calls the code already
 * made, so converting a call site cannot change what is drawn.  Any pixel
 * difference after a conversion is a bug in that conversion.
 *
 * Everything platform-specific is confined here: which widget is the canvas,
 * which display connection is used, and how a drawable is named.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/Intrinsic.h>

#include "xastir.h"
#include "main.h"
#include "xa_draw.h"

// da, gc, screen_width and screen_height come from xastir.h.

// The style constants in xa_draw.h are declared to carry the same numeric
// values as X11's, so the backend needs no translation table.  That is an
// assumption about a third-party header, so check it at compile time rather
// than trusting it -- a mismatch would silently change how things are drawn.
#define XA_CHECK(cond) typedef char xa_assert_##__LINE__[(cond) ? 1 : -1]
XA_CHECK(XA_LINE_SOLID == LineSolid);
XA_CHECK(XA_LINE_ON_OFF_DASH == LineOnOffDash);
XA_CHECK(XA_LINE_DOUBLE_DASH == LineDoubleDash);
XA_CHECK(XA_CAP_NOT_LAST == CapNotLast);
XA_CHECK(XA_CAP_BUTT == CapButt);
XA_CHECK(XA_CAP_ROUND == CapRound);
XA_CHECK(XA_CAP_PROJECTING == CapProjecting);
XA_CHECK(XA_JOIN_MITER == JoinMiter);
XA_CHECK(XA_JOIN_ROUND == JoinRound);
XA_CHECK(XA_JOIN_BEVEL == JoinBevel);
XA_CHECK(XA_FILL_SOLID == FillSolid);
XA_CHECK(XA_FILL_TILED == FillTiled);
XA_CHECK(XA_FILL_STIPPLED == FillStippled);
XA_CHECK(XA_FILL_OPAQUE_STIPPLED == FillOpaqueStippled);
XA_CHECK(XA_COORD_ORIGIN == CoordModeOrigin);
XA_CHECK(XA_COORD_PREVIOUS == CoordModePrevious);
XA_CHECK(XA_SHAPE_COMPLEX == Complex);
XA_CHECK(XA_SHAPE_NONCONVEX == Nonconvex);
XA_CHECK(XA_SHAPE_CONVEX == Convex);
XA_CHECK(XA_FUNC_COPY == GXcopy);
XA_CHECK(XA_FUNC_XOR == GXxor);
// xa_point must be layout-compatible with XPoint, since point arrays are
// passed straight through rather than copied.
XA_CHECK(sizeof(xa_point) == sizeof(XPoint));


// The display connection.  Every call used XtDisplay() on some widget; they
// are all the same connection, so resolve it in one place.
static Display *xa_dpy(void)
{
  return (da != (Widget)NULL) ? XtDisplay(da) : NULL;
}


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


xa_surface_id xa_surface_create(int width, int height, int depth)
{
  Display *dpy = xa_dpy();

  if (dpy == NULL || width <= 0 || height <= 0)
  {
    return XA_SURFACE_NONE;
  }
  if (depth <= 0)
  {
    depth = DefaultDepthOfScreen(XtScreen(da));
  }
  return (xa_surface_id)XCreatePixmap(dpy,
                                      RootWindowOfScreen(XtScreen(da)),
                                      (unsigned int)width,
                                      (unsigned int)height,
                                      (unsigned int)depth);
}


void xa_surface_destroy(xa_surface_id s)
{
  Display *dpy = xa_dpy();

  if (dpy != NULL && s != XA_SURFACE_NONE)
  {
    (void)XFreePixmap(dpy, (Pixmap)s);
  }
}


void xa_copy_area(xa_surface_id src,
                  xa_surface_id dst,
                  xa_pen pen,
                  int src_x, int src_y,
                  int width, int height,
                  int dst_x, int dst_y)
{
  Display *dpy = xa_dpy();

  if (dpy == NULL || src == XA_SURFACE_NONE || dst == XA_SURFACE_NONE)
  {
    return;
  }
  if (width <= 0 || height <= 0)
  {
    return;
  }
  if (pen == NULL)
  {
    pen = (xa_pen)gc;          // the shared pen, as the open-coded copies used
  }

  (void)XCopyArea(dpy, (Drawable)src, (Drawable)dst, (GC)pen,
                  src_x, src_y,
                  (unsigned int)width, (unsigned int)height,
                  dst_x, dst_y);
}


void xa_present_full(xa_surface_id src)
{
  xa_surface_id target = xa_screen_target();
  int w, h;

  // The open-coded copies this replaced did not check whether the canvas
  // existed, so an early call issued a request against a window that was not
  // yet created.  There is one place to check now.
  if (target == XA_SURFACE_NONE)
  {
    return;
  }

  xa_canvas_size(&w, &h);
  xa_copy_area(src, target, (xa_pen)gc, 0, 0, w, h, 0, 0);
}


/* ---- pen state -------------------------------------------------------- */

void xa_pen_color(xa_pen pen, xa_color c)
{
  Display *dpy = xa_dpy();
  if (dpy && pen)
  {
    (void)XSetForeground(dpy, (GC)pen, (unsigned long)c);
  }
}

void xa_pen_bg(xa_pen pen, xa_color c)
{
  Display *dpy = xa_dpy();
  if (dpy && pen)
  {
    (void)XSetBackground(dpy, (GC)pen, (unsigned long)c);
  }
}

void xa_pen_line(xa_pen pen, int width, int line_style, int cap, int join)
{
  Display *dpy = xa_dpy();
  if (dpy && pen)
  {
    (void)XSetLineAttributes(dpy, (GC)pen, (unsigned int)width,
                             line_style, cap, join);
  }
}

void xa_pen_dashes(xa_pen pen, int dash_offset, const char *dash_list, int n)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && dash_list && n > 0)
  {
    (void)XSetDashes(dpy, (GC)pen, dash_offset, (char *)dash_list, n);
  }
}

void xa_pen_fill_style(xa_pen pen, int fill_style)
{
  Display *dpy = xa_dpy();
  if (dpy && pen)
  {
    (void)XSetFillStyle(dpy, (GC)pen, fill_style);
  }
}

void xa_pen_stipple(xa_pen pen, xa_surface_id bitmap)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && bitmap != XA_SURFACE_NONE)
  {
    (void)XSetStipple(dpy, (GC)pen, (Pixmap)bitmap);
  }
}

void xa_pen_ts_origin(xa_pen pen, int x, int y)
{
  Display *dpy = xa_dpy();
  if (dpy && pen)
  {
    (void)XSetTSOrigin(dpy, (GC)pen, x, y);
  }
}

void xa_pen_function(xa_pen pen, int func)
{
  Display *dpy = xa_dpy();
  if (dpy && pen)
  {
    (void)XSetFunction(dpy, (GC)pen, func);
  }
}

void xa_pen_clip_mask(xa_pen pen, xa_surface_id mask)
{
  Display *dpy = xa_dpy();
  if (dpy && pen)
  {
    // XA_SURFACE_NONE is None, which is exactly "no clipping" to Xlib.
    (void)XSetClipMask(dpy, (GC)pen, (Pixmap)mask);
  }
}


/* ---- drawing ---------------------------------------------------------- */

void xa_draw_line(xa_surface_id dst, xa_pen pen,
                  int x1, int y1, int x2, int y2)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && dst != XA_SURFACE_NONE)
  {
    (void)XDrawLine(dpy, (Drawable)dst, (GC)pen, x1, y1, x2, y2);
  }
}

void xa_draw_lines(xa_surface_id dst, xa_pen pen,
                   xa_point *points, int npoints, int coord_mode)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && dst != XA_SURFACE_NONE && points && npoints > 0)
  {
    (void)XDrawLines(dpy, (Drawable)dst, (GC)pen,
                     (XPoint *)points, npoints, coord_mode);
  }
}

void xa_draw_point(xa_surface_id dst, xa_pen pen, int x, int y)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && dst != XA_SURFACE_NONE)
  {
    (void)XDrawPoint(dpy, (Drawable)dst, (GC)pen, x, y);
  }
}

void xa_draw_rect(xa_surface_id dst, xa_pen pen,
                  int x, int y, int width, int height)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && dst != XA_SURFACE_NONE)
  {
    (void)XDrawRectangle(dpy, (Drawable)dst, (GC)pen, x, y,
                         (unsigned int)width, (unsigned int)height);
  }
}

void xa_fill_rect(xa_surface_id dst, xa_pen pen,
                  int x, int y, int width, int height)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && dst != XA_SURFACE_NONE)
  {
    (void)XFillRectangle(dpy, (Drawable)dst, (GC)pen, x, y,
                         (unsigned int)width, (unsigned int)height);
  }
}

void xa_fill_polygon(xa_surface_id dst, xa_pen pen,
                     xa_point *points, int npoints, int shape, int coord_mode)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && dst != XA_SURFACE_NONE && points && npoints > 0)
  {
    (void)XFillPolygon(dpy, (Drawable)dst, (GC)pen,
                       (XPoint *)points, npoints, shape, coord_mode);
  }
}

void xa_draw_arc(xa_surface_id dst, xa_pen pen,
                 int x, int y, int width, int height, int angle1, int angle2)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && dst != XA_SURFACE_NONE)
  {
    (void)XDrawArc(dpy, (Drawable)dst, (GC)pen, x, y,
                   (unsigned int)width, (unsigned int)height, angle1, angle2);
  }
}

void xa_fill_arc(xa_surface_id dst, xa_pen pen,
                 int x, int y, int width, int height, int angle1, int angle2)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && dst != XA_SURFACE_NONE)
  {
    (void)XFillArc(dpy, (Drawable)dst, (GC)pen, x, y,
                   (unsigned int)width, (unsigned int)height, angle1, angle2);
  }
}

void xa_draw_string(xa_surface_id dst, xa_pen pen,
                    int x, int y, const char *text, int length)
{
  Display *dpy = xa_dpy();
  if (dpy && pen && dst != XA_SURFACE_NONE && text && length > 0)
  {
    (void)XDrawString(dpy, (Drawable)dst, (GC)pen, x, y, text, length);
  }
}


xa_surface_id xa_bitmap_from_data(const char *bits, int width, int height)
{
  Display *dpy = xa_dpy();

  if (dpy == NULL || bits == NULL || width <= 0 || height <= 0)
  {
    return XA_SURFACE_NONE;
  }
  return (xa_surface_id)XCreateBitmapFromData(dpy,
                                              RootWindowOfScreen(XtScreen(da)),
                                              (char *)bits,
                                              (unsigned int)width,
                                              (unsigned int)height);
}


xa_pen xa_pen_create(xa_surface_id for_surface)
{
  Display *dpy = xa_dpy();
  Drawable d;

  if (dpy == NULL)
  {
    return NULL;
  }
  // A GC is tied to a drawable's depth, not its identity, so the canvas root
  // serves for anything canvas-depth.
  d = (for_surface != XA_SURFACE_NONE)
      ? (Drawable)for_surface
      : (Drawable)RootWindowOfScreen(XtScreen(da));
  return (xa_pen)XCreateGC(dpy, d, 0, NULL);
}


void xa_pen_destroy(xa_pen pen)
{
  Display *dpy = xa_dpy();

  if (dpy != NULL && pen != NULL)
  {
    (void)XFreeGC(dpy, (GC)pen);
  }
}
