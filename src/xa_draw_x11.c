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

#include <stdlib.h>   // calloc(), for the pixel-buffer fallback

#include <X11/Intrinsic.h>

#ifdef HAVE_X11_XPM_H
  #include <X11/xpm.h>   // XpmReadFileToImage, for xa_image_load()
#endif

#include "xastir.h"
#include "main.h"
#include "color.h"    // Pixel_Format, NOT_TRUE_NOR_DIRECT
#include "rotated.h"  // XRotDrawAlignedString and the alignment constants
#include "snprintf.h" // xastir_snprintf, for the font-name cache
#include "xa_draw.h"
#include "xa_draw_x11.h"
#include "xa_trace.h"

#ifdef HAVE_CAIRO
  #include "cairo_text.h"
#endif

// gc, screen_width and screen_height come from xastir.h.  The canvas widget does
// not: the front end hands it over through xa_x11_set_canvas(), so that this
// file defines the whole of its dependency on the toolkit rather than sharing a
// global with main.c.  See xa_draw_x11.h.
static Widget xa_canvas = (Widget)NULL;

void xa_x11_set_canvas(Widget canvas)
{
  xa_canvas = canvas;
}

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
  return (xa_canvas != (Widget)NULL) ? XtDisplay(xa_canvas) : NULL;
}


// The screen, for the three calls that want a depth or a root window rather
// than a connection.  Same reason as xa_dpy(): resolve it in one place.
static Screen *xa_scr(void)
{
  return (xa_canvas != (Widget)NULL) ? XtScreen(xa_canvas) : NULL;
}


xa_surface_id xa_screen_target(void)
{
  Window win;

  if (xa_canvas == (Widget)NULL)
  {
    return XA_SURFACE_NONE;
  }

  // XtWindow() is None until the widget is realized.
  win = XtWindow(xa_canvas);
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


/* ---- text and fonts --------------------------------------------------- */

XA_CHECK(XA_ALIGN_NONE == NONE);
XA_CHECK(XA_ALIGN_TLEFT == TLEFT);
XA_CHECK(XA_ALIGN_TCENTRE == TCENTRE);
XA_CHECK(XA_ALIGN_TRIGHT == TRIGHT);
XA_CHECK(XA_ALIGN_MLEFT == MLEFT);
XA_CHECK(XA_ALIGN_MCENTRE == MCENTRE);
XA_CHECK(XA_ALIGN_MRIGHT == MRIGHT);
XA_CHECK(XA_ALIGN_BLEFT == BLEFT);
XA_CHECK(XA_ALIGN_BCENTRE == BCENTRE);
XA_CHECK(XA_ALIGN_BRIGHT == BRIGHT);


xa_font xa_font_load(const char *name)
{
  Display *dpy = xa_dpy();

  if (dpy == NULL || name == NULL)
  {
    return XA_FONT_NONE;
  }
  return (xa_font)XLoadQueryFont(dpy, name);
}


void xa_font_free(xa_font f)
{
  Display *dpy = xa_dpy();

  if (dpy != NULL && f != XA_FONT_NONE)
  {
    (void)XFreeFont(dpy, (XFontStruct *)f);
  }
}


static void metrics_from(const XFontStruct *fs, xa_font_metrics *m)
{
  if (m == NULL)
  {
    return;
  }
  if (fs == NULL)
  {
    m->max_width = m->min_width = m->ascent = m->descent = 0;
    return;
  }
  m->max_width = fs->max_bounds.width;
  m->min_width = fs->min_bounds.width;
  m->ascent    = fs->max_bounds.ascent;
  m->descent   = fs->max_bounds.descent;
}


void xa_font_metrics_get(xa_font f, xa_font_metrics *m)
{
  metrics_from((const XFontStruct *)f, m);
}


void xa_font_text_extents(xa_font f, const char *text, int length,
                          int *width, int *ascent, int *descent)
{
  int dir, asc, desc;
  XCharStruct overall;

  if (width)
  {
    *width = 0;
  }
  if (ascent)
  {
    *ascent = 0;
  }
  if (descent)
  {
    *descent = 0;
  }
  if (f == XA_FONT_NONE || text == NULL || length <= 0)
  {
    return;
  }
  XTextExtents((XFontStruct *)f, text, length, &dir, &asc, &desc, &overall);
  if (width)
  {
    *width = overall.width;
  }
  if (ascent)
  {
    *ascent = overall.ascent;
  }
  if (descent)
  {
    *descent = overall.descent;
  }
}


int xa_font_text_width(xa_font f, const char *text, int length)
{
  int w;

  xa_font_text_extents(f, text, length, &w, NULL, NULL);
  return w;
}


/*
 * The font a pen is set to.  XQueryFont() allocates, so every caller has to
 * free -- and two of the three original sites returned without doing so on one
 * path.  Confining that to here means the leak cannot come back.
 */
static XFontStruct *pen_font(xa_pen pen)
{
  Display *dpy = xa_dpy();

  if (dpy == NULL || pen == NULL)
  {
    return NULL;
  }
  return XQueryFont(dpy, XGContextFromGC((GC)pen));
}


void xa_pen_font_metrics(xa_pen pen, xa_font_metrics *m)
{
  Display *dpy = xa_dpy();
  XFontStruct *fs = pen_font(pen);

  metrics_from(fs, m);
  if (fs != NULL && dpy != NULL)
  {
    XFreeFontInfo(NULL, fs, 1);
  }
}


int xa_pen_text_width(xa_pen pen, const char *text, int length)
{
  Display *dpy = xa_dpy();
  XFontStruct *fs = pen_font(pen);
  int w = 0;

  if (fs != NULL)
  {
    if (text != NULL && length > 0)
    {
      int dir, asc, desc;
      XCharStruct overall;
      XTextExtents(fs, text, length, &dir, &asc, &desc, &overall);
      w = overall.width;
    }
    if (dpy != NULL)
    {
      XFreeFontInfo(NULL, fs, 1);
    }
  }
  return w;
}


void xa_pen_font(xa_pen pen, xa_font f)
{
  Display *dpy = xa_dpy();

  if (dpy && pen && f != XA_FONT_NONE)
  {
    (void)XSetFont(dpy, (GC)pen, ((XFontStruct *)f)->fid);
  }
}


void xa_draw_text_rotated(xa_surface_id dst, xa_pen pen, xa_font f,
                          int x, int y, float degrees, int align,
                          const char *text)
{
  Display *dpy = xa_dpy();

  if (dpy == NULL || pen == NULL || dst == XA_SURFACE_NONE
      || f == XA_FONT_NONE || text == NULL)
  {
    return;
  }
  (void)XRotDrawAlignedString(dpy, (XFontStruct *)f, degrees,
                              (Drawable)dst, (GC)pen, x, y,
                              (char *)text, align);
}


/*
 * Font-spec text.  Two implementations live behind these three entry points,
 * chosen by HAVE_CAIRO, and no caller sees which.
 *
 * The non-Cairo half needs a loaded font for a name, so it keeps a small cache
 * here.  That cache used to be rotated_label_font[] in maps.c together with a
 * parallel array of the names last loaded into it, so that changing the
 * configured font could invalidate the entry -- roughly thirty lines of
 * bookkeeping in a file that draws maps.  Keying on the name directly does the
 * same job, and font resources belong to the renderer.
 */
#ifndef HAVE_CAIRO

#define XA_FONT_CACHE 12

static struct
{
  char  name[256];
  xa_font font;
} xa_font_cache[XA_FONT_CACHE];

static xa_font font_for(const char *spec)
{
  int i, free_slot = -1;

  if (spec == NULL || spec[0] == '\0')
  {
    return XA_FONT_NONE;
  }
  for (i = 0; i < XA_FONT_CACHE; i++)
  {
    if (xa_font_cache[i].font == XA_FONT_NONE)
    {
      if (free_slot < 0)
      {
        free_slot = i;
      }
      continue;
    }
    if (strcmp(xa_font_cache[i].name, spec) == 0)
    {
      return xa_font_cache[i].font;
    }
  }

  {
    xa_font f = xa_font_load(spec);
    if (f == XA_FONT_NONE)
    {
      return XA_FONT_NONE;
    }
    // Full cache: use the font but do not keep it, rather than evicting an entry
    // that something may still be drawing with.  With nine configurable label
    // fonts and twelve slots this does not happen in practice.
    if (free_slot >= 0)
    {
      xastir_snprintf(xa_font_cache[free_slot].name,
                      sizeof(xa_font_cache[free_slot].name), "%s", spec);
      xa_font_cache[free_slot].font = f;
    }
    return f;
  }
}

#endif  // !HAVE_CAIRO


void xa_draw_text_styled(xa_surface_id dst, int x, int y, float degrees,
                         const char *text, const char *fontspec,
                         xa_color fg, int outline, xa_color outline_color,
                         int align)
{
  Display *dpy = xa_dpy();

  if (dpy == NULL || dst == XA_SURFACE_NONE || text == NULL || fontspec == NULL)
  {
    return;
  }

#ifdef HAVE_CAIRO
  xastir_cairo_draw_text(dpy, (Pixmap)dst, x, y, degrees, text, fontspec,
                         (unsigned long)fg, outline,
                         (unsigned long)outline_color, align);
#else
  {
    xa_font f = font_for(fontspec);

    if (f == XA_FONT_NONE)
    {
      return;
    }
    // The xvertext path has no outline mode; the callers that want one already
    // draw the text repeatedly at offsets, which is what this reproduces.
    if (outline)
    {
      int dx, dy;
      xa_pen_color((xa_pen)gc, outline_color);
      for (dx = -1; dx < 2; dx++)
      {
        for (dy = -1; dy < 2; dy++)
        {
          xa_draw_text_rotated(dst, (xa_pen)gc, f, x + dx, y + dy,
                               degrees, align, text);
        }
      }
    }
    xa_pen_color((xa_pen)gc, fg);
    xa_draw_text_rotated(dst, (xa_pen)gc, f, x, y, degrees, align, text);
  }
#endif
}


int xa_text_width(const char *text, const char *fontspec)
{
  if (text == NULL || fontspec == NULL)
  {
    return 0;
  }
#ifdef HAVE_CAIRO
  return xastir_cairo_text_width(text, fontspec);
#else
  return xa_font_text_width(font_for(fontspec), text, (int)strlen(text));
#endif
}


int xa_text_height(const char *fontspec)
{
  if (fontspec == NULL)
  {
    return 0;
  }
#ifdef HAVE_CAIRO
  return xastir_cairo_text_height(fontspec);
#else
  {
    xa_font_metrics m;
    xa_font_metrics_get(font_for(fontspec), &m);
    return m.ascent + m.descent;
  }
#endif
}


xa_color xa_color_by_name(const char *name)
{
  // GetPixelByName() takes a Widget only to reach XtDisplay().  The canvas
  // serves, and it is the widget every caller was passing anyway.
  if (name == NULL || xa_canvas == (Widget)NULL)
  {
    return (xa_color)0;
  }
  return (xa_color)GetPixelByName(xa_canvas, (char *)name);
}


void xa_color_rgb(xa_color c, unsigned short *r, unsigned short *g,
                  unsigned short *b)
{
  Display *dpy = xa_dpy();
  XColor probe;

  if (r)
  {
    *r = 0;
  }
  if (g)
  {
    *g = 0;
  }
  if (b)
  {
    *b = 0;
  }
  if (dpy == NULL)
  {
    return;
  }

  probe.pixel = (unsigned long)c;
  (void)XQueryColor(dpy, cmap, &probe);
  if (r)
  {
    *r = probe.red;
  }
  if (g)
  {
    *g = probe.green;
  }
  if (b)
  {
    *b = probe.blue;
  }
}


int xa_color_resolve(unsigned short *r, unsigned short *g, unsigned short *b,
                     xa_color *pixel)
{
  if (r == NULL || g == NULL || b == NULL || pixel == NULL)
  {
    return 0;
  }

  /*
   * A true-colour or direct-colour visual has no colormap to allocate from;
   * the channels go straight into the visual's bit fields.  pack_pixel_bits()
   * cannot fail and does not disturb the channels.
   */
  if (visual_type != NOT_TRUE_NOR_DIRECT)
  {
    unsigned long p = (unsigned long)*pixel;

    pack_pixel_bits(*r, *g, *b, &p);
    *pixel = (xa_color)p;
    return 1;
  }

  /*
   * A palette visual.  XAllocColor() rewrites the XColor with the colour it
   * actually granted, which is why the channels are copied back -- callers read
   * them.  It is asked through a temporary rather than the caller's storage so
   * that a failure leaves everything untouched, instead of half-written.
   *
   * Note this path is unexercised on this machine: the display is true colour,
   * so every map driver takes the branch above.  It is a transcription of what
   * the call sites did, not something that has been observed running.
   */
  {
    Display *dpy = xa_dpy();
    XColor want;

    if (dpy == NULL)
    {
      return 0;
    }
    want.red   = *r;
    want.green = *g;
    want.blue  = *b;
    want.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(dpy, cmap, &want))
    {
      return 0;
    }
    *r     = want.red;
    *g     = want.green;
    *b     = want.blue;
    *pixel = (xa_color)want.pixel;
    return 1;
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
    depth = DefaultDepthOfScreen(xa_scr());
  }
  return (xa_surface_id)XCreatePixmap(dpy,
                                      RootWindowOfScreen(xa_scr()),
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

void xa_pen_clip_origin(xa_pen pen, int x, int y)
{
  Display *dpy = xa_dpy();
  if (dpy && pen)
  {
    (void)XSetClipOrigin(dpy, (GC)pen, x, y);
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
                                              RootWindowOfScreen(xa_scr()),
                                              (char *)bits,
                                              (unsigned int)width,
                                              (unsigned int)height);
}


xa_surface_id xa_bitmap_load(const char *path, int *width, int *height)
{
  Display *dpy = xa_dpy();
  Pixmap bm = None;
  unsigned int w = 0, h = 0;
  int hot_x, hot_y;   // XReadBitmapFile insists on these; no caller wants them.

  if (dpy == NULL || path == NULL)
  {
    return XA_SURFACE_NONE;
  }
  if (XReadBitmapFile(dpy, RootWindowOfScreen(xa_scr()), path,
                      &w, &h, &bm, &hot_x, &hot_y) != BitmapSuccess)
  {
    return XA_SURFACE_NONE;
  }
  if (width)
  {
    *width = (int)w;
  }
  if (height)
  {
    *height = (int)h;
  }
  return (xa_surface_id)bm;
}


/* ---- pixel buffers ---------------------------------------------------- */

xa_image xa_image_capture(xa_surface_id src, int x, int y,
                          int width, int height)
{
  Display *dpy = xa_dpy();

  if (dpy == NULL || src == XA_SURFACE_NONE || width <= 0 || height <= 0)
  {
    return XA_IMAGE_NONE;
  }
  return (xa_image)XGetImage(dpy, (Drawable)src, x, y,
                             (unsigned int)width, (unsigned int)height,
                             AllPlanes, ZPixmap);
}


xa_image xa_image_create(int width, int height)
{
  Display *dpy = xa_dpy();
  XImage *img;

  if (dpy == NULL || width <= 0 || height <= 0)
  {
    return XA_IMAGE_NONE;
  }
  img = XCreateImage(dpy,
                     DefaultVisualOfScreen(xa_scr()),
                     DefaultDepthOfScreen(xa_scr()),
                     ZPixmap, 0, NULL,
                     (unsigned int)width, (unsigned int)height, 32, 0);
  if (img == NULL)
  {
    return XA_IMAGE_NONE;
  }
  // XCreateImage does not allocate the pixel storage; it only works out the
  // layout, which is why bytes_per_line is read back rather than computed.
  img->data = calloc((size_t)img->bytes_per_line * (size_t)height, 1);
  if (img->data == NULL)
  {
    XDestroyImage(img);
    return XA_IMAGE_NONE;
  }
  return (xa_image)img;
}


xa_image xa_image_load(const char *path, int *width, int *height)
{
#ifdef HAVE_X11_XPM_H
  Display *dpy = xa_dpy();
  XImage *img = NULL;
  XpmAttributes atb;

  if (dpy == NULL || path == NULL)
  {
    return XA_IMAGE_NONE;
  }
  atb.valuemask = 0;
  if (XpmReadFileToImage(dpy, (char *)path, &img, NULL, &atb) != XpmSuccess)
  {
    // XpmReadFileToImage can allocate before failing.
    if (img != NULL)
    {
      XDestroyImage(img);
    }
    return XA_IMAGE_NONE;
  }
  if (width)
  {
    *width = (int)atb.width;
  }
  if (height)
  {
    *height = (int)atb.height;
  }
  return (xa_image)img;
#else
  (void)path;
  (void)width;
  (void)height;
  return XA_IMAGE_NONE;
#endif
}


void xa_image_destroy(xa_image img)
{
  if (img != XA_IMAGE_NONE)
  {
    // Frees the pixel storage too, including the calloc above -- XDestroyImage
    // releases whatever is in ->data.
    XDestroyImage((XImage *)img);
  }
}


void xa_image_put_pixel(xa_image img, int x, int y, xa_color c)
{
  if (img != XA_IMAGE_NONE)
  {
    (void)XPutPixel((XImage *)img, x, y, (unsigned long)c);
  }
}


xa_color xa_image_get_pixel(xa_image img, int x, int y)
{
  if (img == XA_IMAGE_NONE)
  {
    return (xa_color)0;
  }
  return (xa_color)XGetPixel((XImage *)img, x, y);
}


void xa_image_to_surface(xa_surface_id dst, xa_pen pen, xa_image img,
                         int src_x, int src_y, int dst_x, int dst_y,
                         int width, int height)
{
  Display *dpy = xa_dpy();

  if (dpy == NULL || dst == XA_SURFACE_NONE || pen == NULL
      || img == XA_IMAGE_NONE || width <= 0 || height <= 0)
  {
    return;
  }
  (void)XPutImage(dpy, (Drawable)dst, (GC)pen, (XImage *)img,
                  src_x, src_y, dst_x, dst_y,
                  (unsigned int)width, (unsigned int)height);
}


/* ---- regions ---------------------------------------------------------- */

XA_CHECK(XA_EVEN_ODD == EvenOddRule);
XA_CHECK(XA_WINDING == WindingRule);


xa_region xa_region_create(void)
{
  return (xa_region)XCreateRegion();
}


void xa_region_destroy(xa_region r)
{
  if (r != XA_REGION_NONE)
  {
    (void)XDestroyRegion((Region)r);
  }
}


xa_region xa_region_from_polygon(xa_point *points, int npoints, int rule)
{
  if (points == NULL || npoints < 3)
  {
    return XA_REGION_NONE;
  }
  // xa_point is layout-compatible with XPoint; asserted at the top of this file.
  return (xa_region)XPolygonRegion((XPoint *)points, npoints, rule);
}


void xa_region_add_rect(xa_region r, int x, int y, int width, int height)
{
  XRectangle rect;

  if (r == XA_REGION_NONE)
  {
    return;
  }
  // Truncation to 16 bits is X's, and the caller already knows: map_shp.c
  // carries a TODO about exceeding these values when zoomed well in.
  rect.x      = (short)x;
  rect.y      = (short)y;
  rect.width  = (unsigned short)width;
  rect.height = (unsigned short)height;
  (void)XUnionRectWithRegion(&rect, (Region)r, (Region)r);
}


void xa_region_subtract(xa_region a, xa_region b, xa_region dst)
{
  if (a != XA_REGION_NONE && b != XA_REGION_NONE && dst != XA_REGION_NONE)
  {
    (void)XSubtractRegion((Region)a, (Region)b, (Region)dst);
  }
}


void xa_pen_clip_region(xa_pen pen, xa_region r)
{
  Display *dpy = xa_dpy();

  if (dpy != NULL && pen != NULL && r != XA_REGION_NONE)
  {
    (void)XSetRegion(dpy, (GC)pen, (Region)r);
  }
}


void xa_pen_copy_fill(xa_pen dst, xa_pen src)
{
  Display *dpy = xa_dpy();

  if (dpy != NULL && dst != NULL && src != NULL)
  {
    (void)XCopyGC(dpy, (GC)src, (GCFillStyle | GCStipple), (GC)dst);
  }
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
      : (Drawable)RootWindowOfScreen(xa_scr());
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

/* ---- backend-owned drawing resources ---------------------------------- */
/*
 * These were defined in main.c, which meant every core object that draws had
 * to link the Motif GUI to get them.  They are the renderer's resources, so
 * they live with the renderer; a different backend defines its own.
 * Declared in xa_draw.h now, in the neutral types -- they used to be declared
 * in xastir.h and main.h as Pixmap/GC/Pixel, which is what put <X11/Intrinsic.h>
 * in front of every core file in the tree.  Defined here in those same neutral
 * types so the declaration and the definition agree exactly rather than merely
 * being layout-compatible.
 *
 * Pixmap and Pixel *are* unsigned long, so nothing changes for them.  GC is a
 * pointer and xa_pen is void *, so the five below are now void * -- which C
 * converts to GC implicitly at every Xlib call in this file, no cast needed.
 * That implicit conversion is the reason xa_pen was made void * rather than an
 * opaque struct pointer in the first place.
 */
xa_pen gc=0;                // Used for drawing maps
xa_pen gc2=0;               // Used for drawing symbols
xa_pen gc_tint=0;           // Used for tinting maps & symbols
xa_pen gc_stipple=0;        // Used for drawing symbols
xa_pen gc_bigfont=0;
xa_surface_id  pixmap;
xa_surface_id  pixmap_alerts;
xa_surface_id  pixmap_final;
xa_surface_id  pixmap_50pct_stipple; // 50% pixels used for position ambiguity, DF circle, etc.
xa_surface_id  pixmap_25pct_stipple; // 25% pixels used for large position ambiguity
xa_surface_id  pixmap_13pct_stipple; // 12.5% pixels used for larger position ambiguity
xa_surface_id  pixmap_wx_stipple;  // Used for weather alerts

// Colour resources: the allocated palette, the trail palette, the colormap
// and the visual class.  Renderer state, for the same reason as the GCs.
xa_color colors[256];           /* screen colors */
xa_color trail_colors[MAX_TRAIL_COLORS]; /* station trail colors, duh */
Pixel_Format visual_type = NOT_TRUE_NOR_DIRECT;
Colormap cmap;                  /* current colormap */


/*
 * The visual's colour layout.  color.c already works this out at startup and
 * packs the bits; this only puts a toolkit-neutral name on the two questions
 * the map drivers ask, so that they stop naming Pixel_Format and
 * pack_pixel_bits directly.
 */
int xa_color_is_direct(void)
{
  return (visual_type != NOT_TRUE_NOR_DIRECT);
}


void xa_color_pack(unsigned short r, unsigned short g, unsigned short b,
                   xa_color *pixel)
{
  pack_pixel_bits(r, g, b, (unsigned long *)pixel);
}
