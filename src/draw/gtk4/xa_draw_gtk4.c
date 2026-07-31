/*
 * xa_draw_gtk4.c -- a GTK4 backend for xa_draw.h.
 *
 * Cairo for drawing, Pango for text, GTK4 for the canvas.  No X11, no Motif.
 *
 * WHAT THIS IS AND IS NOT
 *
 * It is a complete implementation of the interface: every entry point in
 * xa_draw.h is here, it compiles against gtk4/cairo/pango with no X headers,
 * and the core links against it with the X libraries struck off.
 *
 * It has never drawn a frame of Astir.  There is no GTK4 front end to drive
 * it -- main.c is still 30,000 lines of Motif -- so nothing here has been
 * compared against the X11 backend pixel for pixel.  tools/gtk4_smoke.c
 * exercises the drawing calls end to end and writes a PNG, which proves they
 * work rather than merely link; that is the whole of what has been verified.
 * Anywhere the mapping from X11 semantics is approximate is marked APPROXIMATE
 * below, and those are the places to look first when the output is wrong.
 *
 * THE MODEL
 *
 * X11 hands out server-side Drawables and GCs.  Cairo has neither: a surface is
 * client-side pixels and drawing state lives on a short-lived cairo_t.  So:
 *
 *   xa_surface_id   an index into a table of cairo_surface_t *
 *   xa_pen          a struct holding what a GC held; a cairo_t is made,
 *                   configured from it, used and destroyed per call
 *   xa_color        0x00RRGGBB, not an index -- there is no colormap
 *   xa_font         a PangoFontDescription *
 *   xa_image        a client-side ARGB32 buffer
 *   xa_region       a list of rectangles and polygons, applied as a clip path
 *
 * Making a cairo_t per drawing call is the obvious cost.  It is deliberate for
 * a first implementation: cairo_t carries a state stack, and caching one per
 * surface means every entry point has to leave that stack exactly as it found
 * it or state leaks between unrelated calls.  Correct first.  If this ever
 * draws real frames, cache a cairo_t per surface and save/restore around each
 * operation -- xa_image_put_pixel() is the one that must not be slow, and it
 * touches no cairo_t at all.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <gtk/gtk.h>
#include <cairo.h>
#include <pango/pangocairo.h>

#include "draw/xa_draw.h"

/* ---- surfaces ---------------------------------------------------------- */

// Handles, not pointers: xa_surface_id is an integer type by design, so that a
// backend can decide what it indexes.  Slot 0 is XA_SURFACE_NONE, slot 1 is
// the canvas.
#define GTK4_MAX_SURFACES 512
#define GTK4_CANVAS_ID    ((xa_surface_id)1)

typedef struct
{
  cairo_surface_t *surf;
  int width, height;
  int is_bitmap;                // A1/A8 rather than RGB24
} gtk4_surface;

static gtk4_surface gtk4_surfaces[GTK4_MAX_SURFACES];

static GtkWidget *gtk4_canvas = NULL;
static int gtk4_canvas_w = 0, gtk4_canvas_h = 0;

/*
 * Device pixels per logical pixel.
 *
 * Every coordinate that crosses xa_draw.h is logical: the core sizes its world
 * from screen_width/screen_height and has no business knowing what a monitor
 * does with them.  Resolution independence therefore lives entirely here --
 * a canvas-depth surface is allocated at scale x its logical size and told its
 * device scale, so Cairo renders strokes, glyphs and curves at the real pixel
 * grid while the caller keeps drawing in logical units.
 *
 * Without this the whole frame is composed at 1x and stretched by GTK on the
 * way to the screen, which pixelates everything the app draws -- map strokes,
 * the lat/lon grid, Pango labels, range rings -- not just the raster icons.
 *
 * Bitmaps (A8 stipples and clip masks) stay at 1:1 deliberately: they carry
 * their own resolution, an .xbm being 1 bit per device pixel by definition,
 * and scaling them would blur a pattern that is meant to be crisp.
 */
static int gtk4_device_scale = 1;

static gtk4_surface *surf_of(xa_surface_id s)
{
  if (s == XA_SURFACE_NONE || s >= GTK4_MAX_SURFACES)
  {
    return NULL;
  }
  return gtk4_surfaces[s].surf ? &gtk4_surfaces[s] : NULL;
}

static xa_surface_id surf_new(cairo_surface_t *cs, int w, int h, int bitmap)
{
  int i;

  if (cs == NULL)
  {
    return XA_SURFACE_NONE;
  }
  for (i = GTK4_CANVAS_ID + 1; i < GTK4_MAX_SURFACES; i++)
  {
    if (gtk4_surfaces[i].surf == NULL)
    {
      gtk4_surfaces[i].surf = cs;
      gtk4_surfaces[i].width = w;
      gtk4_surfaces[i].height = h;
      gtk4_surfaces[i].is_bitmap = bitmap;
      return (xa_surface_id)i;
    }
  }
  cairo_surface_destroy(cs);
  return XA_SURFACE_NONE;
}

/*
 * A colour surface of `width` x `height` *logical* pixels, backed by however
 * many device pixels that currently means.  cairo_surface_set_device_scale()
 * is what makes the difference invisible to the caller: it puts the scale in
 * the surface's own matrix, so a cairo_t created on it takes logical
 * coordinates and rasterises at device resolution.
 */
static cairo_surface_t *canvas_surface_create(int width, int height)
{
  cairo_surface_t *cs =
    cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                               width  * gtk4_device_scale,
                               height * gtk4_device_scale);

  cairo_surface_set_device_scale(cs, (double)gtk4_device_scale,
                                 (double)gtk4_device_scale);
  return cs;
}

/*
 * Hand the backend its canvas.  The counterpart of xa_x11_set_canvas():
 * the front end owns the widget, the backend owns everything drawn into it.
 *
 * The canvas is an offscreen image surface, not the widget's own.  GTK4 has no
 * persistent window pixels to draw into -- a widget renders from a snapshot
 * callback -- so the canvas Astir presents to has to be one we keep, and the
 * widget draws it. xa_gtk4_canvas_surface() is what a GtkDrawingArea draw
 * function paints.
 */
void xa_gtk4_set_canvas(GtkWidget *canvas, int width, int height)
{
  gtk4_canvas = canvas;
  gtk4_canvas_w = width;
  gtk4_canvas_h = height;

  if (gtk4_surfaces[GTK4_CANVAS_ID].surf != NULL)
  {
    cairo_surface_destroy(gtk4_surfaces[GTK4_CANVAS_ID].surf);
  }
  gtk4_surfaces[GTK4_CANVAS_ID].surf = canvas_surface_create(width, height);
  gtk4_surfaces[GTK4_CANVAS_ID].width = width;
  gtk4_surfaces[GTK4_CANVAS_ID].height = height;
  gtk4_surfaces[GTK4_CANVAS_ID].is_bitmap = 0;
}


/*
 * Tell the backend how many device pixels a logical pixel is worth.
 *
 * Takes effect on surfaces created after it, so the front end sets it before
 * rebuilding the canvas and the layer pixmaps -- which it already does on every
 * resize, because those are sized to the canvas.  Returns whether it changed,
 * so a scale-factor notification can skip a rebuild that would do nothing.
 */
int xa_gtk4_set_device_scale(int scale)
{
  if (scale < 1)
  {
    scale = 1;
  }
  if (scale == gtk4_device_scale)
  {
    return 0;
  }
  gtk4_device_scale = scale;
  return 1;
}


int xa_gtk4_device_scale(void)
{
  return gtk4_device_scale;
}


// The toolkit-neutral spelling, for core code choosing the resolution of a
// raster asset.  Same number; xa_draw.h is what the core includes.
int xa_device_scale(void)
{
  return gtk4_device_scale;
}

/*
 * The Cairo surface behind a handle.
 *
 * For the front end's compositor, which paints several layers with different
 * transforms and so needs each one rather than just the canvas.
 */
cairo_surface_t *xa_gtk4_surface_of(xa_surface_id s)
{
  gtk4_surface *g = surf_of(s);

  return g ? g->surf : NULL;
}


// What the widget's draw function should paint.  NULL before set_canvas().
cairo_surface_t *xa_gtk4_canvas_surface(void)
{
  return gtk4_surfaces[GTK4_CANVAS_ID].surf;
}


xa_surface_id xa_screen_target(void)
{
  return gtk4_surfaces[GTK4_CANVAS_ID].surf ? GTK4_CANVAS_ID : XA_SURFACE_NONE;
}


void xa_canvas_size(int *width, int *height)
{
  if (width)
  {
    *width = gtk4_canvas_w;
  }
  if (height)
  {
    *height = gtk4_canvas_h;
  }
}


xa_surface_id xa_surface_create(int width, int height, int depth)
{
  if (width <= 0 || height <= 0)
  {
    return XA_SURFACE_NONE;
  }
  // A8 rather than A1 for bitmaps: the contract only ever uses them as stipples
  // and clip masks, where Cairo wants coverage, and A1 rows are awkward to
  // fill by hand for no benefit here.  Bitmaps are not device-scaled; see the
  // gtk4_device_scale comment for why.
  if (depth == XA_DEPTH_BITMAP)
  {
    return surf_new(cairo_image_surface_create(CAIRO_FORMAT_A8, width, height),
                    width, height, 1);
  }
  if (depth == XA_DEPTH_ALPHA)
  {
    // ARGB32 and device-scaled like the canvas, because an overlay is composited
    // onto the canvas one-to-one and has to carry the same resolution.
    cairo_surface_t *cs =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                 width  * gtk4_device_scale,
                                 height * gtk4_device_scale);

    cairo_surface_set_device_scale(cs, (double)gtk4_device_scale,
                                   (double)gtk4_device_scale);
    return surf_new(cs, width, height, 0);
  }
  // Colour surfaces are layers Astir composes the frame from -- pixmap,
  // pixmap_alerts, pixmap_final -- and are copied to the canvas whole, so they
  // have to carry the same resolution or the copy throws it away again.
  return surf_new(canvas_surface_create(width, height), width, height, 0);
}


void xa_surface_clear(xa_surface_id s)
{
  gtk4_surface *g = surf_of(s);
  cairo_t *cr;

  if (g == NULL)
  {
    return;
  }
  cr = cairo_create(g->surf);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_destroy(cr);
}


void xa_surface_destroy(xa_surface_id s)
{
  gtk4_surface *g = surf_of(s);

  if (g == NULL || s == GTK4_CANVAS_ID)
  {
    return;                      // never destroy the canvas through this
  }
  cairo_surface_destroy(g->surf);
  g->surf = NULL;
}


xa_surface_id xa_bitmap_from_data(const char *bits, int width, int height)
{
  cairo_surface_t *cs;
  unsigned char *data;
  int stride, x, y;

  if (bits == NULL || width <= 0 || height <= 0)
  {
    return XA_SURFACE_NONE;
  }
  cs = cairo_image_surface_create(CAIRO_FORMAT_A8, width, height);
  if (cairo_surface_status(cs) != CAIRO_STATUS_SUCCESS)
  {
    cairo_surface_destroy(cs);
    return XA_SURFACE_NONE;
  }
  data = cairo_image_surface_get_data(cs);
  stride = cairo_image_surface_get_stride(cs);

  // X bitmap order: rows padded to whole bytes, least significant bit leftmost.
  for (y = 0; y < height; y++)
  {
    const unsigned char *row = (const unsigned char *)bits + (size_t)y * ((width + 7) / 8);
    for (x = 0; x < width; x++)
    {
      data[(size_t)y * stride + x] = (row[x >> 3] & (1 << (x & 7))) ? 0xff : 0x00;
    }
  }
  cairo_surface_mark_dirty(cs);
  return surf_new(cs, width, height, 1);
}


/*
 * Load a 1-bit bitmap from a file.
 *
 * APPROXIMATE: the X11 backend reads .xbm, which is a C source fragment.
 * GdkPixbuf does not read it, so this parses the format directly -- it is a
 * width, a height and a comma-separated list of byte literals, and every file
 * Astir ships is one.  Anything GdkPixbuf *can* read is tried first, so a PNG
 * works too, which the X11 backend cannot do.
 */
xa_surface_id xa_bitmap_load(const char *path, int *width, int *height)
{
  GdkPixbuf *pb;
  FILE *f;
  char line[512];
  int w = 0, h = 0;
  unsigned char *bits = NULL;
  size_t nbytes = 0, got = 0;
  xa_surface_id out = XA_SURFACE_NONE;

  if (path == NULL)
  {
    return XA_SURFACE_NONE;
  }

  pb = gdk_pixbuf_new_from_file(path, NULL);
  if (pb != NULL)
  {
    int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb);
    cairo_surface_t *cs = cairo_image_surface_create(CAIRO_FORMAT_A8, pw, ph);
    unsigned char *dst = cairo_image_surface_get_data(cs);
    int stride = cairo_image_surface_get_stride(cs);
    const guchar *src = gdk_pixbuf_get_pixels(pb);
    int srcstride = gdk_pixbuf_get_rowstride(pb);
    int nch = gdk_pixbuf_get_n_channels(pb), x, y;

    for (y = 0; y < ph; y++)
      for (x = 0; x < pw; x++)
      {
        const guchar *p = src + (size_t)y * srcstride + (size_t)x * nch;
        // Any non-black pixel is set, matching how an .xbm reads.
        dst[(size_t)y * stride + x] = (p[0] || p[1] || p[2]) ? 0xff : 0x00;
      }
    cairo_surface_mark_dirty(cs);
    g_object_unref(pb);
    if (width)  { *width  = pw; }
    if (height) { *height = ph; }
    return surf_new(cs, pw, ph, 1);
  }

  f = fopen(path, "r");
  if (f == NULL)
  {
    return XA_SURFACE_NONE;
  }
  while (fgets(line, sizeof(line), f) != NULL)
  {
    char *p = strstr(line, "_width");
    if (p) { w = atoi(strrchr(line, ' ') ? strrchr(line, ' ') + 1 : "0"); continue; }
    p = strstr(line, "_height");
    if (p) { h = atoi(strrchr(line, ' ') ? strrchr(line, ' ') + 1 : "0"); continue; }
    if (strchr(line, '{') != NULL)
    {
      break;                     // the byte list starts here
    }
  }
  if (w > 0 && h > 0)
  {
    nbytes = (size_t)((w + 7) / 8) * (size_t)h;
    bits = (unsigned char *)calloc(1, nbytes ? nbytes : 1);
  }
  if (bits != NULL)
  {
    int v;
    // Read hex byte literals wherever they appear from here on.
    while (got < nbytes && fscanf(f, " 0x%x%*[ ,\n\r\t]", &v) == 1)
    {
      bits[got++] = (unsigned char)v;
    }
    if (got == nbytes)
    {
      out = xa_bitmap_from_data((const char *)bits, w, h);
      if (out != XA_SURFACE_NONE)
      {
        if (width)  { *width  = w; }
        if (height) { *height = h; }
      }
    }
    free(bits);
  }
  fclose(f);
  return out;
}


/* ---- colours ----------------------------------------------------------- */

/*
 * 0x00RRGGBB.  There is no colormap and no visual to ask, so allocation always
 * succeeds and packing is the identity -- which is why xa_color_is_direct() is
 * unconditionally true here and the palette-display path in the raster drivers
 * simply never runs.
 */
#define GTK4_R(c) ((int)(((c) >> 16) & 0xff))
#define GTK4_G(c) ((int)(((c) >>  8) & 0xff))
#define GTK4_B(c) ((int)( (c)        & 0xff))

static xa_color gtk4_pack8(int r, int g, int b)
{
  return ((xa_color)(r & 0xff) << 16) | ((xa_color)(g & 0xff) << 8)
         | (xa_color)(b & 0xff);
}


void xa_color_rgb(xa_color c, unsigned short *r, unsigned short *g,
                  unsigned short *b)
{
  // 8 bits per channel out to the 0-65535 range X reports, by replication so
  // that 0xff maps to 0xffff rather than 0xff00.
  if (r) { *r = (unsigned short)(GTK4_R(c) * 257); }
  if (g) { *g = (unsigned short)(GTK4_G(c) * 257); }
  if (b) { *b = (unsigned short)(GTK4_B(c) * 257); }
}


xa_color xa_color_by_name(const char *name)
{
  GdkRGBA rgba;

  if (name == NULL || !gdk_rgba_parse(&rgba, name))
  {
    return 0;                    // black, as the X11 backend does on failure
  }
  return gtk4_pack8((int)(rgba.red   * 255.0 + 0.5),
                    (int)(rgba.green * 255.0 + 0.5),
                    (int)(rgba.blue  * 255.0 + 0.5));
}


int xa_color_resolve(unsigned short *r, unsigned short *g, unsigned short *b,
                     xa_color *pixel)
{
  if (r == NULL || g == NULL || b == NULL || pixel == NULL)
  {
    return 0;
  }
  // Always succeeds, and hands back exactly what was asked for: there is no
  // palette to fall short of.  The contract says callers read the channels back
  // and use them, so they are left as given rather than quantised.
  *pixel = gtk4_pack8(*r >> 8, *g >> 8, *b >> 8);
  return 1;
}


int xa_color_is_direct(void)
{
  return 1;
}


void xa_color_pack(unsigned short r, unsigned short g, unsigned short b,
                   xa_color *pixel)
{
  if (pixel != NULL)
  {
    *pixel = gtk4_pack8(r >> 8, g >> 8, b >> 8);
  }
}


/* ---- pens -------------------------------------------------------------- */

#define GTK4_MAX_DASHES 8

typedef struct
{
  xa_color fg, bg;
  int line_width, line_style, cap, join;
  double dashes[GTK4_MAX_DASHES];
  int ndashes;
  double dash_offset;
  int fill_style;
  xa_surface_id stipple;
  double fill_density;           /* used when stippled with no bitmap */
  int ts_x, ts_y;
  int func;
  xa_surface_id clip_mask;
  int clip_x, clip_y;
  xa_region clip_region;
  PangoFontDescription *font;
} gtk4_pen;


xa_pen xa_pen_create(xa_surface_id for_surface)
{
  gtk4_pen *p = (gtk4_pen *)calloc(1, sizeof(gtk4_pen));

  (void)for_surface;             // Cairo state is not bound to a surface
  if (p == NULL)
  {
    return NULL;
  }
  p->fg = 0x00ffffff;
  p->bg = 0;
  p->line_width = 1;
  p->line_style = XA_LINE_SOLID;
  p->cap = XA_CAP_BUTT;
  p->join = XA_JOIN_MITER;
  p->fill_style = XA_FILL_SOLID;
  p->func = XA_FUNC_COPY;
  p->stipple = XA_SURFACE_NONE;
  // Opaque unless somebody asks otherwise, so a pen that is switched to
  // stippled without setting a density still paints something.
  p->fill_density = 1.0;
  p->clip_mask = XA_SURFACE_NONE;
  return (xa_pen)p;
}


void xa_pen_destroy(xa_pen pen)
{
  gtk4_pen *p = (gtk4_pen *)pen;

  if (p != NULL)
  {
    if (p->font)
    {
      pango_font_description_free(p->font);
    }
    free(p);
  }
}


void xa_pen_color(xa_pen pen, xa_color c)
{
  if (pen) { ((gtk4_pen *)pen)->fg = c; }
}

void xa_pen_bg(xa_pen pen, xa_color c)
{
  if (pen) { ((gtk4_pen *)pen)->bg = c; }
}

void xa_pen_line(xa_pen pen, int width, int line_style, int cap, int join)
{
  gtk4_pen *p = (gtk4_pen *)pen;

  if (p == NULL) { return; }
  p->line_width = width;
  p->line_style = line_style;
  p->cap = cap;
  p->join = join;
}

void xa_pen_dashes(xa_pen pen, int dash_offset, const char *dash_list, int n)
{
  gtk4_pen *p = (gtk4_pen *)pen;
  int i;

  if (p == NULL || dash_list == NULL || n <= 0) { return; }
  if (n > GTK4_MAX_DASHES) { n = GTK4_MAX_DASHES; }
  for (i = 0; i < n; i++)
  {
    // X dash lengths are unsigned bytes; a zero-length dash is meaningless to
    // Cairo and makes it error the context, so clamp to 1.
    unsigned char d = (unsigned char)dash_list[i];
    p->dashes[i] = d ? (double)d : 1.0;
  }
  p->ndashes = n;
  p->dash_offset = (double)dash_offset;
}

void xa_pen_fill_style(xa_pen pen, int fill_style)
{
  if (pen) { ((gtk4_pen *)pen)->fill_style = fill_style; }
}

void xa_pen_stipple(xa_pen pen, xa_surface_id bitmap)
{
  if (pen) { ((gtk4_pen *)pen)->stipple = bitmap; }
}

void xa_pen_fill_density(xa_pen pen, double density)
{
  gtk4_pen *p = (gtk4_pen *)pen;

  if (p == NULL)
  {
    return;
  }
  if (density < 0.0) { density = 0.0; }
  if (density > 1.0) { density = 1.0; }
  p->fill_density = density;
}

void xa_pen_ts_origin(xa_pen pen, int x, int y)
{
  gtk4_pen *p = (gtk4_pen *)pen;

  if (p) { p->ts_x = x; p->ts_y = y; }
}

void xa_pen_function(xa_pen pen, int func)
{
  if (pen) { ((gtk4_pen *)pen)->func = func; }
}

void xa_pen_clip_mask(xa_pen pen, xa_surface_id mask)
{
  if (pen) { ((gtk4_pen *)pen)->clip_mask = mask; }
}

void xa_pen_clip_origin(xa_pen pen, int x, int y)
{
  gtk4_pen *p = (gtk4_pen *)pen;

  if (p) { p->clip_x = x; p->clip_y = y; }
}

void xa_pen_copy_fill(xa_pen dst, xa_pen src)
{
  gtk4_pen *d = (gtk4_pen *)dst, *s = (gtk4_pen *)src;

  if (d && s)
  {
    d->fill_style = s->fill_style;
    d->stipple = s->stipple;
    d->fill_density = s->fill_density;
  }
}


/* ---- regions ----------------------------------------------------------- */

/*
 * Cairo has cairo_region_t, and it is the wrong tool: it holds rectangles, and
 * the one consumer here builds a rectangle with polygon-shaped holes cut out of
 * it (map_shp.c's "swiss-cheese rectangle", for polygons with holes).
 *
 * So a region is a list of shapes with a sign.  Applying one builds a single
 * Cairo path and clips with the even-odd rule, which is what turns the
 * subtracted shapes into holes.  That is both simpler and more accurate than
 * rasterising to rectangles.
 */
typedef struct gtk4_shape
{
  struct gtk4_shape *next;
  int is_rect;
  int x, y, w, h;                // rectangle
  xa_point *pts;                 // or polygon
  int npts;
  int subtract;
} gtk4_shape;

typedef struct
{
  gtk4_shape *head;
} gtk4_region;


xa_region xa_region_create(void)
{
  return (xa_region)calloc(1, sizeof(gtk4_region));
}


static void shape_free(gtk4_shape *s)
{
  while (s != NULL)
  {
    gtk4_shape *n = s->next;
    free(s->pts);
    free(s);
    s = n;
  }
}


void xa_region_destroy(xa_region r)
{
  gtk4_region *g = (gtk4_region *)r;

  if (g != NULL)
  {
    shape_free(g->head);
    free(g);
  }
}


static gtk4_shape *shape_push(gtk4_region *g)
{
  gtk4_shape *s = (gtk4_shape *)calloc(1, sizeof(gtk4_shape));

  if (s == NULL) { return NULL; }
  s->next = g->head;
  g->head = s;
  return s;
}


xa_region xa_region_from_polygon(xa_point *points, int npoints, int rule)
{
  gtk4_region *g;
  gtk4_shape *s;

  (void)rule;                    // even-odd is applied when the clip is built
  if (points == NULL || npoints <= 0) { return XA_REGION_NONE; }
  g = (gtk4_region *)xa_region_create();
  if (g == NULL) { return XA_REGION_NONE; }
  s = shape_push(g);
  if (s == NULL) { xa_region_destroy((xa_region)g); return XA_REGION_NONE; }
  s->pts = (xa_point *)malloc(sizeof(xa_point) * (size_t)npoints);
  if (s->pts == NULL) { xa_region_destroy(g); return XA_REGION_NONE; }
  memcpy(s->pts, points, sizeof(xa_point) * (size_t)npoints);
  s->npts = npoints;
  return (xa_region)g;
}


void xa_region_add_rect(xa_region r, int x, int y, int width, int height)
{
  gtk4_region *g = (gtk4_region *)r;
  gtk4_shape *s;

  if (g == NULL) { return; }
  s = shape_push(g);
  if (s == NULL) { return; }
  s->is_rect = 1;
  s->x = x; s->y = y; s->w = width; s->h = height;
}


static void copy_shapes(gtk4_region *dst, gtk4_region *src, int subtract)
{
  gtk4_shape *s;

  for (s = src ? src->head : NULL; s != NULL; s = s->next)
  {
    gtk4_shape *n = shape_push(dst);
    gtk4_shape *keep;

    if (n == NULL) { return; }
    keep = n->next;
    *n = *s;
    n->next = keep;
    n->subtract = subtract;
    if (s->pts != NULL && s->npts > 0)
    {
      n->pts = (xa_point *)malloc(sizeof(xa_point) * (size_t)s->npts);
      if (n->pts != NULL)
      {
        memcpy(n->pts, s->pts, sizeof(xa_point) * (size_t)s->npts);
      }
      else
      {
        n->npts = 0;
      }
    }
  }
}


void xa_region_subtract(xa_region a, xa_region b, xa_region dst)
{
  gtk4_region *ga = (gtk4_region *)a, *gb = (gtk4_region *)b,
              *gd = (gtk4_region *)dst;

  if (gd == NULL) { return; }
  shape_free(gd->head);
  gd->head = NULL;

  // dst = a, then b's shapes marked subtractive.  Copied rather than aliased:
  // the caller owns a and b and destroys them independently.
  //
  // The struct copy has to keep the *destination* list's next pointer.  Copying
  // it wholesale takes the source's next as well, which points into the list
  // being read from -- dst then walks into a and never terminates where it
  // should.  The smoke test caught exactly that.
  copy_shapes(gd, ga, 0);
  copy_shapes(gd, gb, 1);
}


/*
 * Deep-copy a region into the pen, the way XSetRegion() does.
 *
 * This used to store the caller's pointer, and that is a use-after-free
 * waiting for the right zoom level.  map_shp.c does exactly what Xlib asks
 * for:
 *
 *     xa_pen_clip_region(gc_temp, region[i]);
 *     xa_region_destroy(region[i]);          // immediately
 *
 * which is correct against X -- XSetRegion copies the region into the GC, so
 * the client's copy is the caller's to free straight afterwards.  Aliasing it
 * instead left the pen pointing at freed memory, and the next fill through
 * that pen walked a dangling list.  It survived at city zoom because the code
 * path that sets a region is the one that clips a filled polygon to a
 * shapefile's extent, and at a world zoom the Natural Earth lakes take it:
 * SIGSEGV in apply_region_clip().
 *
 * The whole class of bug is an X idiom that stops being true when the backend
 * changes, and the interface said nothing about ownership either way.  It does
 * now, and the copy makes both readings safe.
 */
void xa_pen_clip_region(xa_pen pen, xa_region r)
{
  gtk4_pen *p = (gtk4_pen *)pen;
  const gtk4_region *src = (const gtk4_region *)r;
  gtk4_region *copy;
  const gtk4_shape *s;
  gtk4_shape **tail;

  if (p == NULL)
  {
    return;
  }

  // Drop whatever the pen was holding; it owns its copy.
  if (p->clip_region != NULL)
  {
    shape_free(((gtk4_region *)p->clip_region)->head);
    free(p->clip_region);
    p->clip_region = NULL;
  }
  if (src == NULL)
  {
    return;
  }

  copy = (gtk4_region *)calloc(1, sizeof(gtk4_region));
  if (copy == NULL)
  {
    return;
  }
  tail = &copy->head;
  for (s = src->head; s != NULL; s = s->next)
  {
    gtk4_shape *d = (gtk4_shape *)calloc(1, sizeof(gtk4_shape));

    if (d == NULL)
    {
      break;
    }
    *d = *s;
    d->next = NULL;
    if (s->pts != NULL && s->npts > 0)
    {
      d->pts = (xa_point *)malloc((size_t)s->npts * sizeof(xa_point));
      if (d->pts == NULL)
      {
        free(d);
        break;
      }
      memcpy(d->pts, s->pts, (size_t)s->npts * sizeof(xa_point));
    }
    *tail = d;
    tail = &d->next;
  }
  p->clip_region = (xa_region)copy;
}


/* ---- turning a pen into a cairo_t -------------------------------------- */

static void set_source_color(cairo_t *cr, xa_color c)
{
  cairo_set_source_rgb(cr, GTK4_R(c) / 255.0, GTK4_G(c) / 255.0,
                       GTK4_B(c) / 255.0);
}


static void apply_region_clip(cairo_t *cr, gtk4_region *g)
{
  gtk4_shape *s;
  int any = 0;

  for (s = g->head; s != NULL; s = s->next)
  {
    if (s->is_rect)
    {
      cairo_rectangle(cr, s->x, s->y, s->w, s->h);
    }
    else if (s->npts > 0)
    {
      int i;
      cairo_move_to(cr, s->pts[0].x, s->pts[0].y);
      for (i = 1; i < s->npts; i++)
      {
        cairo_line_to(cr, s->pts[i].x, s->pts[i].y);
      }
      cairo_close_path(cr);
    }
    any = 1;
  }
  if (any)
  {
    // Even-odd is what makes an enclosed subtracted shape a hole.
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_clip(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
  }
}


/*
 * Begin an operation on `dst` with `pen`.  Returns NULL if there is nothing to
 * draw on, in which case the caller must return without drawing.
 */
static cairo_t *begin(xa_surface_id dst, xa_pen pen)
{
  gtk4_surface *g = surf_of(dst);
  gtk4_pen *p = (gtk4_pen *)pen;
  cairo_t *cr;

  if (g == NULL)
  {
    return NULL;
  }
  cr = cairo_create(g->surf);
  if (cairo_status(cr) != CAIRO_STATUS_SUCCESS)
  {
    cairo_destroy(cr);
    return NULL;
  }
  if (p == NULL)
  {
    return cr;
  }

  if (p->clip_mask != XA_SURFACE_NONE)
  {
    gtk4_surface *m = surf_of(p->clip_mask);
    if (m != NULL)
    {
      // A clip mask is a coverage bitmap positioned at the clip origin.  Cairo
      // has no "clip to a mask", so it becomes a mask applied at paint time --
      // handled by the fill helpers below, which push a group.  Here it only
      // bounds the drawing to the mask's rectangle, which is the part Cairo can
      // express as a clip.
      cairo_rectangle(cr, p->clip_x, p->clip_y, m->width, m->height);
      cairo_clip(cr);
    }
  }
  if (p->clip_region != NULL)
  {
    apply_region_clip(cr, (gtk4_region *)p->clip_region);
  }

  // APPROXIMATE.  X's GXxor is a bitwise operation on pixel *values*; Cairo has
  // no equivalent because it composites colours, not indices.  DIFFERENCE is
  // the nearest thing and shares the property Astir actually relies on --
  // drawing the same thing twice restores the original.  The intermediate
  // colour is not the same.  Astir uses XOR for rubber-band selection boxes
  // and the CAD polygon in progress.
  cairo_set_operator(cr, (p->func == XA_FUNC_XOR)
                     ? CAIRO_OPERATOR_DIFFERENCE : CAIRO_OPERATOR_OVER);

  cairo_set_line_width(cr, p->line_width > 0 ? p->line_width : 1);
  cairo_set_line_cap(cr,
                     p->cap == XA_CAP_ROUND ? CAIRO_LINE_CAP_ROUND :
                     p->cap == XA_CAP_PROJECTING ? CAIRO_LINE_CAP_SQUARE :
                     CAIRO_LINE_CAP_BUTT);
  cairo_set_line_join(cr,
                      p->join == XA_JOIN_ROUND ? CAIRO_LINE_JOIN_ROUND :
                      p->join == XA_JOIN_BEVEL ? CAIRO_LINE_JOIN_BEVEL :
                      CAIRO_LINE_JOIN_MITER);
  if (p->line_style != XA_LINE_SOLID && p->ndashes > 0)
  {
    cairo_set_dash(cr, p->dashes, p->ndashes, p->dash_offset);
  }
  set_source_color(cr, p->fg);
  return cr;
}


/*
 * Set the source for a fill, honouring the stipple.
 *
 * XA_FILL_STIPPLED paints the foreground only where the stipple bit is set;
 * XA_FILL_OPAQUE_STIPPLED paints the background elsewhere as well.  Both become
 * a repeating mask pattern in Cairo, which is a closer match than X's own
 * model.
 *
 * A stippled pen with no bitmap is not an error, and it must not be silently
 * skipped: the caller has already turned the path into a clip by this point, so
 * returning without painting loses the fill entirely and shows as a map with no
 * land on it.  It means "a translucent wash", and it is drawn as one.
 */
static void fill_source(cairo_t *cr, gtk4_pen *p)
{
  gtk4_surface *st;

  if (p == NULL || p->fill_style == XA_FILL_SOLID)
  {
    return;                      // begin() already set the solid colour
  }
  st = surf_of(p->stipple);
  if (st == NULL)
  {
    if (p->fill_density > 0.0)
    {
      set_source_color(cr, p->fg);
      cairo_paint_with_alpha(cr, p->fill_density);
    }
    return;
  }
  if (p->fill_style == XA_FILL_OPAQUE_STIPPLED)
  {
    cairo_save(cr);
    set_source_color(cr, p->bg);
    cairo_paint(cr);             // bounded by the clip already in place
    cairo_restore(cr);
  }
  {
    cairo_pattern_t *pat = cairo_pattern_create_for_surface(st->surf);
    cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
    cairo_matrix_t m;
    cairo_matrix_init_translate(&m, -p->ts_x, -p->ts_y);
    cairo_pattern_set_matrix(pat, &m);
    // The stipple is coverage; the colour comes from the pen.
    cairo_push_group(cr);
    set_source_color(cr, p->fg);
    cairo_paint(cr);
    cairo_pop_group_to_source(cr);
    cairo_mask(cr, pat);
    cairo_pattern_destroy(pat);
  }
}


/* ---- drawing ----------------------------------------------------------- */

/*
 * Where a path has to sit for a stroke to land on whole pixels.
 *
 * X names pixels; Cairo centres a stroke on the path.  A width-1 stroke down
 * x = 7.0 therefore covers x = 6.5 to 7.5 -- half of pixel 6 and half of pixel
 * 7, which is a two-pixel grey smear where X drew one crisp line.  Offsetting
 * the path by half a pixel fixes that.
 *
 * It fixes it FOR ODD WIDTHS ONLY, and the previous version applied it to
 * every width.  A width-2 stroke down x = 7.5 covers 6.5 to 8.5: pixel 7 fully,
 * pixels 6 and 8 half each -- two pixels of ink spread over three columns.
 * That is what made the lat/lon grid look blurred, because draw_grid() asks for
 * width 2.  An even width wants the path ON the boundary, not offset from it.
 *
 * Measured on the grid before and after: (185,183,233) (127,127,233)
 * (185,183,233) across three columns became two columns of one solid colour.
 */
static double px_off(gtk4_pen *p)
{
  int w = (p != NULL && p->line_width > 0) ? p->line_width : 1;

  return (w & 1) ? 0.5 : 0.0;
}

#define PX(v) ((double)(v) + off)

void xa_draw_line(xa_surface_id dst, xa_pen pen, int x1, int y1, int x2, int y2)
{
  cairo_t *cr = begin(dst, pen);
  double off = px_off((gtk4_pen *)pen);

  if (cr == NULL) { return; }
  cairo_move_to(cr, PX(x1), PX(y1));
  cairo_line_to(cr, PX(x2), PX(y2));
  cairo_stroke(cr);
  cairo_destroy(cr);
}


static void path_points(cairo_t *cr, xa_point *points, int npoints,
                        int coord_mode, int close, double off)
{
  int i;
  double cx, cy;

  if (npoints <= 0) { return; }
  cx = points[0].x; cy = points[0].y;
  cairo_move_to(cr, PX(cx), PX(cy));
  for (i = 1; i < npoints; i++)
  {
    if (coord_mode == XA_COORD_PREVIOUS)
    {
      cx += points[i].x; cy += points[i].y;
    }
    else
    {
      cx = points[i].x; cy = points[i].y;
    }
    cairo_line_to(cr, PX(cx), PX(cy));
  }
  if (close) { cairo_close_path(cr); }
}


void xa_draw_lines(xa_surface_id dst, xa_pen pen, xa_point *points,
                   int npoints, int coord_mode)
{
  cairo_t *cr = begin(dst, pen);
  double off = px_off((gtk4_pen *)pen);

  if (cr == NULL) { return; }
  path_points(cr, points, npoints, coord_mode, 0, off);
  cairo_stroke(cr);
  cairo_destroy(cr);
}


void xa_draw_point(xa_surface_id dst, xa_pen pen, int x, int y)
{
  cairo_t *cr = begin(dst, pen);

  if (cr == NULL) { return; }
  cairo_rectangle(cr, x, y, 1, 1);
  cairo_fill(cr);
  cairo_destroy(cr);
}


void xa_draw_rect(xa_surface_id dst, xa_pen pen, int x, int y,
                  int width, int height)
{
  cairo_t *cr = begin(dst, pen);
  double off = px_off((gtk4_pen *)pen);

  if (cr == NULL) { return; }
  // X draws a rectangle width+1 by height+1 pixels; matched here so a converted
  // call site does not shrink by a pixel.
  cairo_rectangle(cr, PX(x), PX(y), width, height);
  cairo_stroke(cr);
  cairo_destroy(cr);
}


void xa_fill_rect(xa_surface_id dst, xa_pen pen, int x, int y,
                  int width, int height)
{
  cairo_t *cr = begin(dst, pen);
  gtk4_pen *p = (gtk4_pen *)pen;

  if (cr == NULL) { return; }
  cairo_rectangle(cr, x, y, width, height);
  cairo_clip(cr);
  if (p && p->fill_style != XA_FILL_SOLID)
  {
    fill_source(cr, p);
  }
  else
  {
    cairo_paint(cr);
  }
  cairo_destroy(cr);
}


void xa_fill_polygon(xa_surface_id dst, xa_pen pen, xa_point *points,
                     int npoints, int shape, int coord_mode)
{
  cairo_t *cr = begin(dst, pen);
  gtk4_pen *p = (gtk4_pen *)pen;
  // A fill has no stroke width, so it wants the pixel boundary, never a
  // half-pixel offset: an offset fill is a shape moved half a pixel.
  double off = 0.0;

  (void)shape;                   // a hint about convexity; Cairo needs none
  if (cr == NULL) { return; }
  path_points(cr, points, npoints, coord_mode, 1, off);
  if (p && p->fill_style != XA_FILL_SOLID)
  {
    cairo_clip(cr);
    fill_source(cr, p);
  }
  else
  {
    cairo_fill(cr);
  }
  cairo_destroy(cr);
}


/*
 * Arcs.  X angles are in 64ths of a degree, measured counter-clockwise from
 * three o'clock; Cairo angles are radians clockwise from three o'clock because
 * y grows downward.  Hence the negation.
 */
static void arc_path(cairo_t *cr, int x, int y, int width, int height,
                     int angle1, int angle2)
{
  double cx = x + width / 2.0, cy = y + height / 2.0;
  double a1 = -(angle1 / 64.0) * M_PI / 180.0;
  double a2 = -((angle1 + angle2) / 64.0) * M_PI / 180.0;

  cairo_save(cr);
  cairo_translate(cr, cx, cy);
  if (width != 0 && height != 0)
  {
    cairo_scale(cr, width / 2.0, height / 2.0);
  }
  if (angle2 >= 0)
  {
    cairo_arc_negative(cr, 0, 0, 1.0, a1, a2);
  }
  else
  {
    cairo_arc(cr, 0, 0, 1.0, a1, a2);
  }
  cairo_restore(cr);
}


/*
 * Fill closed rings by the nonzero winding rule, at `alpha`.
 *
 * The symbol outlines land here.  No clip mask is involved, which is the whole
 * point: the pixmap path drew an icon by blitting a 20x20 rectangle through a
 * 1-bit coverage mask, and this backend could only honour the mask's bounding
 * rectangle, so every station sat on an opaque grey square.  A filled outline
 * has no rectangle to leak.
 */
void xa_fill_rings(xa_surface_id dst, xa_pen pen,
                   const xa_pointf *pts, const int *ring_sizes, int nrings,
                   double alpha)
{
  cairo_t *cr;
  int i, k, at = 0;

  if (pts == NULL || ring_sizes == NULL || nrings <= 0)
  {
    return;
  }
  cr = begin(dst, pen);
  if (cr == NULL)
  {
    return;
  }

  // Nonzero is Cairo's default, but say so: the tracer relies on it to put
  // holes in, and a later change to even-odd would fill them in silently.
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);

  for (i = 0; i < nrings; i++)
  {
    if (ring_sizes[i] < 3)
    {
      at += ring_sizes[i];
      continue;
    }
    cairo_move_to(cr, pts[at].x, pts[at].y);
    for (k = 1; k < ring_sizes[i]; k++)
    {
      cairo_line_to(cr, pts[at + k].x, pts[at + k].y);
    }
    cairo_close_path(cr);
    at += ring_sizes[i];
  }

  if (alpha >= 1.0)
  {
    cairo_fill(cr);
  }
  else
  {
    // Clip to the path and paint through alpha, rather than setting a
    // translucent source and filling: overlapping rings within one glyph would
    // otherwise composite against each other and show their seams.
    cairo_clip(cr);
    cairo_paint_with_alpha(cr, alpha < 0.0 ? 0.0 : alpha);
  }
  cairo_destroy(cr);
}


void xa_draw_arc(xa_surface_id dst, xa_pen pen, int x, int y,
                 int width, int height, int angle1, int angle2)
{
  cairo_t *cr = begin(dst, pen);

  if (cr == NULL) { return; }
  arc_path(cr, x, y, width, height, angle1, angle2);
  cairo_stroke(cr);
  cairo_destroy(cr);
}


void xa_fill_arc(xa_surface_id dst, xa_pen pen, int x, int y,
                 int width, int height, int angle1, int angle2)
{
  cairo_t *cr = begin(dst, pen);

  if (cr == NULL) { return; }
  cairo_move_to(cr, x + width / 2.0, y + height / 2.0);
  arc_path(cr, x, y, width, height, angle1, angle2);
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_destroy(cr);
}


void xa_copy_area(xa_surface_id src, xa_surface_id dst, xa_pen pen,
                  int src_x, int src_y, int width, int height,
                  int dst_x, int dst_y)
{
  gtk4_surface *s = surf_of(src);
  cairo_t *cr;

  if (s == NULL) { return; }
  cr = begin(dst, pen);
  if (cr == NULL) { return; }
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, s->surf, dst_x - src_x, dst_y - src_y);
  cairo_rectangle(cr, dst_x, dst_y, width, height);
  cairo_fill(cr);
  cairo_destroy(cr);
}


void xa_present_full(xa_surface_id src)
{
  gtk4_surface *s = surf_of(src);
  gtk4_surface *c = surf_of(GTK4_CANVAS_ID);
  cairo_t *cr;

  if (s == NULL || c == NULL) { return; }
  if (s->surf != c->surf)
  {
    cr = cairo_create(c->surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, s->surf, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
  }
  // Ask the widget to repaint from the canvas surface.  Under X this was a
  // server-side copy; here presentation is the toolkit's job.
  if (gtk4_canvas != NULL)
  {
    gtk_widget_queue_draw(gtk4_canvas);
  }
}


/* ---- pixel buffers ----------------------------------------------------- */

typedef struct
{
  int width, height;
  xa_color *px;
} gtk4_image;


xa_image xa_image_create(int width, int height)
{
  gtk4_image *im;

  if (width <= 0 || height <= 0) { return XA_IMAGE_NONE; }
  im = (gtk4_image *)calloc(1, sizeof(gtk4_image));
  if (im == NULL) { return XA_IMAGE_NONE; }
  im->width = width;
  im->height = height;
  im->px = (xa_color *)calloc((size_t)width * (size_t)height, sizeof(xa_color));
  if (im->px == NULL) { free(im); return XA_IMAGE_NONE; }
  return (xa_image)im;
}


void xa_image_destroy(xa_image img)
{
  gtk4_image *im = (gtk4_image *)img;

  if (im != NULL) { free(im->px); free(im); }
}


/*
 * An xa_image is a buffer of *logical* pixels -- the OSM tile path is the only
 * caller, and it works one screen pixel at a time.  A device-scaled surface has
 * more pixels than that and a stride to match, so reading its memory directly
 * with logical coordinates would sample the top-left corner of the frame and
 * call it the whole frame.  At scale 1 the two are the same thing and the
 * direct loop is kept; above it, Cairo does the resampling.
 */
static cairo_surface_t *logical_copy_of(gtk4_surface *g, int x, int y,
                                        int width, int height)
{
  cairo_surface_t *tmp =
    cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
  cairo_t *cr = cairo_create(tmp);

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, g->surf, -x, -y);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(tmp);
  return tmp;
}


xa_image xa_image_capture(xa_surface_id src, int x, int y,
                          int width, int height)
{
  gtk4_surface *g = surf_of(src);
  gtk4_image *im;
  cairo_surface_t *from;
  unsigned char *data;
  int stride, ix, iy, ox, oy, limit_w, limit_h;

  if (g == NULL) { return XA_IMAGE_NONE; }
  im = (gtk4_image *)xa_image_create(width, height);
  if (im == NULL) { return XA_IMAGE_NONE; }

  if (gtk4_device_scale == 1)
  {
    from = g->surf;                        // read the surface itself
    ox = x;
    oy = y;
    limit_w = g->width;
    limit_h = g->height;
    cairo_surface_flush(from);
  }
  else
  {
    from = logical_copy_of(g, x, y, width, height);   // already the right window
    ox = 0;
    oy = 0;
    limit_w = width;
    limit_h = height;
  }

  data = cairo_image_surface_get_data(from);
  stride = cairo_image_surface_get_stride(from);
  if (data == NULL)                        // not an image surface: leave blank
  {
    if (from != g->surf) { cairo_surface_destroy(from); }
    return (xa_image)im;
  }

  for (iy = 0; iy < height; iy++)
  {
    int sy = oy + iy;
    if (sy < 0 || sy >= limit_h) { continue; }
    for (ix = 0; ix < width; ix++)
    {
      int sx = ox + ix;
      if (sx < 0 || sx >= limit_w) { continue; }
      im->px[(size_t)iy * width + ix] =
        ((uint32_t *)(data + (size_t)sy * stride))[sx] & 0x00ffffff;
    }
  }
  if (from != g->surf) { cairo_surface_destroy(from); }
  return (xa_image)im;
}


/*
 * Load an image file into a buffer.
 *
 * The X11 backend reads XPM and only in a build without ImageMagick.  GdkPixbuf
 * reads XPM and much else, so this is strictly more capable -- and, like its
 * counterpart, untested, because nothing in this tree calls it.
 */
xa_image xa_image_load(const char *path, int *width, int *height)
{
  GdkPixbuf *pb;
  gtk4_image *im;
  int w, h, nch, rs, x, y;
  const guchar *src;

  if (path == NULL) { return XA_IMAGE_NONE; }
  pb = gdk_pixbuf_new_from_file(path, NULL);
  if (pb == NULL) { return XA_IMAGE_NONE; }

  w = gdk_pixbuf_get_width(pb);
  h = gdk_pixbuf_get_height(pb);
  im = (gtk4_image *)xa_image_create(w, h);
  if (im == NULL) { g_object_unref(pb); return XA_IMAGE_NONE; }

  src = gdk_pixbuf_get_pixels(pb);
  rs = gdk_pixbuf_get_rowstride(pb);
  nch = gdk_pixbuf_get_n_channels(pb);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
    {
      const guchar *p = src + (size_t)y * rs + (size_t)x * nch;
      im->px[(size_t)y * w + x] = gtk4_pack8(p[0], p[1], p[2]);
    }
  g_object_unref(pb);
  if (width)  { *width  = w; }
  if (height) { *height = h; }
  return (xa_image)im;
}


void xa_image_put_pixel(xa_image img, int x, int y, xa_color c)
{
  gtk4_image *im = (gtk4_image *)img;

  // No round trip, by construction: this is a store into a local array.  The
  // header calls that out as the one place where per-pixel cost matters.
  if (im == NULL || x < 0 || y < 0 || x >= im->width || y >= im->height)
  {
    return;
  }
  im->px[(size_t)y * im->width + x] = c;
}


void xa_image_fill_rect(xa_image img, int x, int y, int w, int h, xa_color c)
{
  gtk4_image *im = (gtk4_image *)img;
  int yy;

  if (im == NULL || w <= 0 || h <= 0)
  {
    return;
  }

  // Clip once, then fill rows with no further checking.  This is the whole
  // point of the call: the per-pixel version re-tested every bound for every
  // pixel of every block.
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > im->width)  { w = im->width  - x; }
  if (y + h > im->height) { h = im->height - y; }
  if (w <= 0 || h <= 0)
  {
    return;
  }

  for (yy = 0; yy < h; yy++)
  {
    xa_color *row = im->px + (size_t)(y + yy) * im->width + x;
    int xx;

    for (xx = 0; xx < w; xx++)
    {
      row[xx] = c;
    }
  }
}


xa_color xa_image_get_pixel(xa_image img, int x, int y)
{
  gtk4_image *im = (gtk4_image *)img;

  if (im == NULL || x < 0 || y < 0 || x >= im->width || y >= im->height)
  {
    return 0;
  }
  return im->px[(size_t)y * im->width + x];
}


void xa_image_to_surface(xa_surface_id dst, xa_pen pen, xa_image img,
                         int src_x, int src_y, int dst_x, int dst_y,
                         int width, int height)
{
  gtk4_surface *g = surf_of(dst);
  gtk4_image *im = (gtk4_image *)img;
  unsigned char *data;
  int stride, ix, iy;

  (void)pen;                     // a raw pixel put ignores pen state, as in X
  if (g == NULL || im == NULL) { return; }

  // Above scale 1 the destination has more pixels than the buffer has, so the
  // buffer becomes a source surface and Cairo places it in logical coordinates.
  // The image is raster either way -- an OSM tile magnified to device
  // resolution is still the tile -- but it lands in the right place and covers
  // the whole area, which the direct write below would not.
  if (gtk4_device_scale != 1)
  {
    cairo_surface_t *tmp =
      cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    unsigned char *td = cairo_image_surface_get_data(tmp);
    int tstride = cairo_image_surface_get_stride(tmp);
    cairo_t *cr;

    if (td == NULL) { cairo_surface_destroy(tmp); return; }
    for (iy = 0; iy < height; iy++)
    {
      int sy = src_y + iy;
      if (sy < 0 || sy >= im->height) { continue; }
      for (ix = 0; ix < width; ix++)
      {
        int sx = src_x + ix;
        if (sx < 0 || sx >= im->width) { continue; }
        ((uint32_t *)(td + (size_t)iy * tstride))[ix] =
          0xff000000u | (uint32_t)im->px[(size_t)sy * im->width + sx];
      }
    }
    cairo_surface_mark_dirty(tmp);

    cr = cairo_create(g->surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, tmp, dst_x, dst_y);
    cairo_rectangle(cr, dst_x, dst_y, width, height);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(tmp);
    return;
  }

  cairo_surface_flush(g->surf);
  data = cairo_image_surface_get_data(g->surf);
  stride = cairo_image_surface_get_stride(g->surf);
  if (data == NULL) { return; }

  for (iy = 0; iy < height; iy++)
  {
    int sy = src_y + iy, dy = dst_y + iy;
    if (sy < 0 || sy >= im->height || dy < 0 || dy >= g->height) { continue; }
    for (ix = 0; ix < width; ix++)
    {
      int sx = src_x + ix, dx = dst_x + ix;
      if (sx < 0 || sx >= im->width || dx < 0 || dx >= g->width) { continue; }
      ((uint32_t *)(data + (size_t)dy * stride))[dx] =
        0xff000000u | (uint32_t)im->px[(size_t)sy * im->width + sx];
    }
  }
  cairo_surface_mark_dirty(g->surf);
}


/* ---- fonts and text ---------------------------------------------------- */

static PangoFontMap *gtk4_fontmap(void)
{
  static PangoFontMap *fm = NULL;

  if (fm == NULL)
  {
    fm = pango_cairo_font_map_get_default();
  }
  return fm;
}


/*
 * Turn a font name into a Pango description.
 *
 * Astir names fonts three ways and this has to take all of them:
 *   an XLFD          -adobe-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1
 *   a Cairo spec     "Helvetica:size=12"
 *   a Pango string   "Helvetica 12"
 *
 * The XLFD parse takes the family from field 2, the pixel size from field 7,
 * bold from the weight field and italic from the slant field.  A "*" size falls
 * back to the point-size field, then to 12 -- which is what the fixed-size XLFDs
 * in the config ask for anyway.
 */
static PangoFontDescription *desc_from_name(const char *name)
{
  PangoFontDescription *d;

  if (name == NULL || *name == '\0')
  {
    return pango_font_description_from_string("Sans 12");
  }

  if (name[0] == '-')
  {
    char family[128] = "Sans";
    char fields[16][64];
    int nf = 0, i = 0, size = 0;
    const char *p = name;

    // Split on '-'; the leading '-' means field 0 is empty.
    while (*p && nf < 16)
    {
      int k = 0;
      if (*p == '-') { p++; }
      while (*p && *p != '-' && k < 63) { fields[nf][k++] = *p++; }
      fields[nf][k] = '\0';
      nf++;
    }
    // 0 foundry, 1 family, 2 weight, 3 slant, ... 6 pixelsize, 7 pointsize
    if (nf > 1 && fields[1][0] && fields[1][0] != '*')
    {
      snprintf(family, sizeof(family), "%s", fields[1]);
    }
    if (nf > 6 && fields[6][0] && fields[6][0] != '*')
    {
      size = atoi(fields[6]);
    }
    if (size <= 0 && nf > 7 && fields[7][0] && fields[7][0] != '*')
    {
      size = atoi(fields[7]) / 10;
    }
    if (size <= 0) { size = 12; }

    d = pango_font_description_new();
    pango_font_description_set_family(d, family);
    pango_font_description_set_absolute_size(d, size * PANGO_SCALE);
    if (nf > 2 && (!strcmp(fields[2], "bold") || !strcmp(fields[2], "demibold")))
    {
      pango_font_description_set_weight(d, PANGO_WEIGHT_BOLD);
    }
    if (nf > 3 && (fields[3][0] == 'i' || fields[3][0] == 'o'))
    {
      pango_font_description_set_style(d, PANGO_STYLE_ITALIC);
    }
    (void)i;
    return d;
  }

  if (strstr(name, ":size=") != NULL)
  {
    char buf[256];
    const char *colon = strchr(name, ':');
    int size = atoi(strstr(name, ":size=") + 6);
    size_t flen = (size_t)(colon - name);

    if (flen >= sizeof(buf)) { flen = sizeof(buf) - 1; }
    memcpy(buf, name, flen);
    buf[flen] = '\0';
    d = pango_font_description_new();
    pango_font_description_set_family(d, buf);
    pango_font_description_set_absolute_size(d, (size > 0 ? size : 12) * PANGO_SCALE);
    return d;
  }

  return pango_font_description_from_string(name);
}


xa_font xa_font_load(const char *name)
{
  return (xa_font)desc_from_name(name);
}


void xa_font_free(xa_font f)
{
  if (f != XA_FONT_NONE)
  {
    pango_font_description_free((PangoFontDescription *)f);
  }
}


// A context to measure against.  Measurement must work before any surface
// exists, so this owns a 1x1 surface rather than borrowing the canvas.
static PangoContext *measure_ctx(void)
{
  static PangoContext *ctx = NULL;

  if (ctx == NULL)
  {
    ctx = pango_font_map_create_context(gtk4_fontmap());
  }
  return ctx;
}


static void metrics_of(PangoFontDescription *d, xa_font_metrics *m)
{
  PangoFontMetrics *pm;

  if (m == NULL) { return; }
  m->max_width = m->min_width = m->ascent = m->descent = 0;
  if (d == NULL) { return; }

  pm = pango_context_get_metrics(measure_ctx(), d, NULL);
  if (pm == NULL) { return; }
  m->ascent  = pango_font_metrics_get_ascent(pm)  / PANGO_SCALE;
  m->descent = pango_font_metrics_get_descent(pm) / PANGO_SCALE;
  // APPROXIMATE.  X reports the widest and narrowest glyph in the font; Pango
  // offers the approximate character width and the digit width.  Call sites
  // compute (3*max + min)/4 as a stand-in for the average advance, so feeding
  // both from Pango's approximations keeps that arithmetic meaningful even
  // though the two extremes are not the true ones.
  m->max_width = pango_font_metrics_get_approximate_char_width(pm) / PANGO_SCALE;
  m->min_width = pango_font_metrics_get_approximate_digit_width(pm) / PANGO_SCALE;
  if (m->max_width < m->min_width)
  {
    int t = m->max_width; m->max_width = m->min_width; m->min_width = t;
  }
  pango_font_metrics_unref(pm);
}


void xa_font_metrics_get(xa_font f, xa_font_metrics *m)
{
  metrics_of((PangoFontDescription *)f, m);
}


static void extents_of(PangoFontDescription *d, const char *text, int length,
                       int *width, int *ascent, int *descent)
{
  PangoLayout *lay;
  PangoRectangle ink;

  if (width)   { *width   = 0; }
  if (ascent)  { *ascent  = 0; }
  if (descent) { *descent = 0; }
  if (d == NULL || text == NULL || length <= 0) { return; }

  lay = pango_layout_new(measure_ctx());
  pango_layout_set_font_description(lay, d);
  pango_layout_set_text(lay, text, length);
  pango_layout_get_pixel_extents(lay, &ink, NULL);
  if (width)   { *width   = ink.width; }
  if (ascent)  { *ascent  = -ink.y; }
  if (descent) { *descent = ink.height + ink.y; }
  g_object_unref(lay);
}


void xa_font_text_extents(xa_font f, const char *text, int length,
                          int *width, int *ascent, int *descent)
{
  extents_of((PangoFontDescription *)f, text, length, width, ascent, descent);
}


int xa_font_text_width(xa_font f, const char *text, int length)
{
  int w;

  extents_of((PangoFontDescription *)f, text, length, &w, NULL, NULL);
  return w;
}


void xa_pen_font(xa_pen pen, xa_font f)
{
  gtk4_pen *p = (gtk4_pen *)pen;

  if (p == NULL) { return; }
  if (p->font) { pango_font_description_free(p->font); }
  p->font = (f != XA_FONT_NONE)
            ? pango_font_description_copy((PangoFontDescription *)f) : NULL;
}


void xa_pen_font_metrics(xa_pen pen, xa_font_metrics *m)
{
  gtk4_pen *p = (gtk4_pen *)pen;

  // Answered from the pen's own state -- no round trip and nothing to free,
  // which is the reason the header gives for this call existing.
  metrics_of(p ? p->font : NULL, m);
}


int xa_pen_text_width(xa_pen pen, const char *text, int length)
{
  gtk4_pen *p = (gtk4_pen *)pen;
  int w;

  extents_of(p ? p->font : NULL, text, length, &w, NULL, NULL);
  return w;
}


void xa_draw_string(xa_surface_id dst, xa_pen pen, int x, int y,
                    const char *text, int length)
{
  cairo_t *cr;
  gtk4_pen *p = (gtk4_pen *)pen;
  PangoLayout *lay;
  int baseline;

  if (p == NULL || p->font == NULL || text == NULL || length <= 0) { return; }
  cr = begin(dst, pen);
  if (cr == NULL) { return; }

  lay = pango_cairo_create_layout(cr);
  pango_layout_set_font_description(lay, p->font);
  pango_layout_set_text(lay, text, length);
  // X's y is the baseline; Pango lays out from the top of the line.
  baseline = pango_layout_get_baseline(lay) / PANGO_SCALE;
  cairo_move_to(cr, x, y - baseline);
  pango_cairo_show_layout(cr, lay);
  g_object_unref(lay);
  cairo_destroy(cr);
}


// Where the anchor sits relative to the text box, per XA_ALIGN_*.
static void align_offsets(int align, int w, int h, double *dx, double *dy)
{
  int col = (align == XA_ALIGN_NONE) ? 0 : (align - 1) % 3;
  int row = (align == XA_ALIGN_NONE) ? 0 : (align - 1) / 3;

  *dx = (col == 1) ? -w / 2.0 : (col == 2) ? -(double)w : 0.0;
  *dy = (row == 1) ? -h / 2.0 : (row == 2) ? -(double)h : 0.0;
}


static void draw_rotated(cairo_t *cr, PangoFontDescription *d, int x, int y,
                         float degrees, int align, const char *text,
                         xa_color fg, int outline, xa_color outline_color)
{
  PangoLayout *lay;
  int w, h;
  double dx, dy;

  lay = pango_cairo_create_layout(cr);
  pango_layout_set_font_description(lay, d);
  pango_layout_set_text(lay, text, -1);
  pango_layout_get_pixel_size(lay, &w, &h);
  align_offsets(align, w, h, &dx, &dy);

  cairo_save(cr);
  cairo_translate(cr, x, y);
  // Screen y grows downward, so a positive angle is clockwise on screen; the
  // X11 path measures the same way, hence the negation.
  cairo_rotate(cr, -degrees * M_PI / 180.0);
  cairo_translate(cr, dx, dy);

  if (outline)
  {
    int ox, oy;
    set_source_color(cr, outline_color);
    for (oy = -1; oy <= 1; oy++)
      for (ox = -1; ox <= 1; ox++)
      {
        if (ox == 0 && oy == 0) { continue; }
        cairo_move_to(cr, ox, oy);
        pango_cairo_show_layout(cr, lay);
      }
  }
  set_source_color(cr, fg);
  cairo_move_to(cr, 0, 0);
  pango_cairo_show_layout(cr, lay);
  cairo_restore(cr);
  g_object_unref(lay);
}


void xa_draw_text_rotated(xa_surface_id dst, xa_pen pen, xa_font f,
                          int x, int y, float degrees, int align,
                          const char *text)
{
  cairo_t *cr;
  gtk4_pen *p = (gtk4_pen *)pen;

  if (text == NULL || f == XA_FONT_NONE) { return; }
  cr = begin(dst, pen);
  if (cr == NULL) { return; }
  // This is where rotated.c's whole existence goes: Pango rotates natively, so
  // there is nothing to port.
  draw_rotated(cr, (PangoFontDescription *)f, x, y, degrees, align, text,
               p ? p->fg : 0x00ffffff, 0, 0);
  cairo_destroy(cr);
}


void xa_draw_text_styled(xa_surface_id dst, int x, int y, float degrees,
                         const char *text, const char *fontspec,
                         xa_color fg, int outline, xa_color outline_color,
                         int align)
{
  cairo_t *cr;
  PangoFontDescription *d;

  if (text == NULL) { return; }
  cr = begin(dst, NULL);
  if (cr == NULL) { return; }
  d = desc_from_name(fontspec);
  draw_rotated(cr, d, x, y, degrees, align, text, fg, outline, outline_color);
  pango_font_description_free(d);
  cairo_destroy(cr);
}


int xa_text_width(const char *text, const char *fontspec)
{
  PangoFontDescription *d = desc_from_name(fontspec);
  int w;

  extents_of(d, text, text ? (int)strlen(text) : 0, &w, NULL, NULL);
  pango_font_description_free(d);
  return w;
}


int xa_text_height(const char *fontspec)
{
  PangoFontDescription *d = desc_from_name(fontspec);
  xa_font_metrics m;

  metrics_of(d, &m);
  pango_font_description_free(d);
  return m.ascent + m.descent;
}


/* ---- the shared drawing objects ---------------------------------------- */

/*
 * Same as the X11 backend: a backend owns these, not just the entry points.
 * The front end fills them in at startup; nothing here creates them, because
 * their sizes depend on the canvas.
 */
xa_pen gc, gc2, gc_tint, gc_stipple, gc_bigfont;

xa_surface_id pixmap, pixmap_final, pixmap_alerts;
xa_surface_id pixmap_wx_stipple;

xa_color colors[256];
xa_color trail_colors[MAX_TRAIL_COLORS];
