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
#include "color.h"    // Pixel_Format, NOT_TRUE_NOR_DIRECT
#include "rotated.h"  // XRotDrawAlignedString and the alignment constants
#include "snprintf.h" // xastir_snprintf, for the font-name cache
#include "xa_draw.h"
#include "xa_draw_x11.h"

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
 * Declarations stay in xastir.h/main.h so no call site changed.
 */
GC gc=0;                // Used for drawing maps
GC gc2=0;               // Used for drawing symbols
GC gc_tint=0;           // Used for tinting maps & symbols
GC gc_stipple=0;        // Used for drawing symbols
GC gc_bigfont=0;
Pixmap  pixmap;
Pixmap  pixmap_alerts;
Pixmap  pixmap_final;
Pixmap  pixmap_50pct_stipple; // 50% pixels used for position ambiguity, DF circle, etc.
Pixmap  pixmap_25pct_stipple; // 25% pixels used for large position ambiguity
Pixmap  pixmap_13pct_stipple; // 12.5% pixels used for larger position ambiguity
Pixmap  pixmap_wx_stipple;  // Used for weather alerts

// Colour resources: the allocated palette, the trail palette, the colormap
// and the visual class.  Renderer state, for the same reason as the GCs.
Pixel colors[256];              /* screen colors */
Pixel trail_colors[MAX_TRAIL_COLORS]; /* station trail colors, duh */
Pixel_Format visual_type = NOT_TRUE_NOR_DIRECT;
Colormap cmap;                  /* current colormap */
