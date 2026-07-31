/*
 * xa_draw_null.c -- a second backend for xa_draw.h, implemented with no
 * toolkit at all.
 *
 * This exists to answer one question that no call-site count can: **is
 * xa_draw.h actually sufficient, and actually toolkit-neutral?**  The X11
 * backend cannot answer it, because an interface shaped around X11 looks fine
 * from inside X11.  A second implementation either compiles against the same
 * header with no X headers present, or it does not -- and if it does not, the
 * failure names precisely what leaked.
 *
 * It draws nothing.  Surfaces, pens, images and regions are plain malloc'd
 * records; drawing calls are counted and, with ASTIR_TRACE set, recorded.
 * That is enough for the question being asked, and it also makes this usable
 * as a harness in its own right: a core linked against this backend reports
 * what it *asked* to have drawn without needing a display.
 *
 * Text metrics are a fixed-width approximation.  Nothing here can measure a
 * real font, so xa_font_text_width() returns length * NULL_ADVANCE.  Anything
 * whose layout depends on real metrics lays out differently against this
 * backend -- that is honest rather than a bug, and it is the one part of the
 * interface a null backend cannot exercise faithfully.
 *
 * Built but not linked into astir; see tools/link_null.py, which links the
 * core against this instead of xa_draw_x11.o and with no X libraries.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "draw/xa_draw.h"
#include "core/util/xa_trace.h"

// A fixed-width font, because there is no font here to ask.
#define NULL_ADVANCE 6
#define NULL_ASCENT  10
#define NULL_DESCENT 3

static int null_canvas_w = 640;
static int null_canvas_h = 425;
static char null_font_token;

/*
 * The shared drawing objects xa_draw.h declares.  A backend owns these, not
 * just the entry points -- they are as much part of the contract as any call,
 * and a backend that defines only the functions leaves ~450 undefined
 * references behind.  Nothing here dereferences them; they exist so that core
 * code which passes `gc` and `pixmap` around has something to pass.
 */
xa_pen gc, gc2, gc_tint, gc_stipple, gc_bigfont;

xa_surface_id pixmap, pixmap_final, pixmap_alerts;
xa_surface_id pixmap_50pct_stipple, pixmap_25pct_stipple;
xa_surface_id pixmap_13pct_stipple, pixmap_wx_stipple;

xa_color colors[256];
xa_color trail_colors[MAX_TRAIL_COLORS];

static long null_calls;

static void null_record(const char *op)
{
  null_calls++;
  xa_trace("draw %s", op);
}

typedef enum { NULL_PEN, NULL_REGION, NULL_IMAGE, NULL_SURFACE } null_kind;

typedef struct
{
  null_kind kind;
  int width, height;
  xa_color *pixels;     // images only
} null_obj;

static void *null_alloc(null_kind k)
{
  null_obj *o = (null_obj *)calloc(1, sizeof(null_obj));
  if (o != NULL)
  {
    o->kind = k;
  }
  return o;
}

static void null_free(void *p)
{
  null_obj *o = (null_obj *)p;
  if (o != NULL)
  {
    free(o->pixels);
    free(o);
  }
}

// Surfaces are handles, not pointers, so they get an id table rather than a
// cast -- xa_surface_id is deliberately not a pointer type.
#define NULL_MAX_SURFACES 256
static struct { int used, w, h; } null_surfaces[NULL_MAX_SURFACES];

static xa_surface_id null_surface_new(int w, int h)
{
  int i;
  for (i = 2; i < NULL_MAX_SURFACES; i++)   // 1 is the notional screen
  {
    if (!null_surfaces[i].used)
    {
      null_surfaces[i].used = 1;
      null_surfaces[i].w = w;
      null_surfaces[i].h = h;
      return (xa_surface_id)i;
    }
  }
  return XA_SURFACE_NONE;
}

static void null_surface_drop(xa_surface_id s)
{
  if (s > 1 && s < NULL_MAX_SURFACES)
  {
    null_surfaces[s].used = 0;
  }
}

static xa_image null_image_new(int w, int h)
{
  null_obj *o = (null_obj *)null_alloc(NULL_IMAGE);
  if (o == NULL)
  {
    return NULL;
  }
  o->width = w;
  o->height = h;
  if (w > 0 && h > 0)
  {
    o->pixels = (xa_color *)calloc((size_t)w * (size_t)h, sizeof(xa_color));
  }
  return (xa_image)o;
}

static xa_color null_image_pixel(xa_image img, int x, int y)
{
  null_obj *o = (null_obj *)img;
  if (o == NULL || o->pixels == NULL
      || x < 0 || y < 0 || x >= o->width || y >= o->height)
  {
    return 0;
  }
  return o->pixels[(size_t)y * (size_t)o->width + (size_t)x];
}

static void null_image_set(xa_image img, int x, int y, xa_color c)
{
  null_obj *o = (null_obj *)img;
  if (o == NULL || o->pixels == NULL
      || x < 0 || y < 0 || x >= o->width || y >= o->height)
  {
    return;
  }
  o->pixels[(size_t)y * (size_t)o->width + (size_t)x] = c;
}

static int null_text_width(const char *text, int length)
{
  if (text == NULL || length <= 0)
  {
    return 0;
  }
  return length * NULL_ADVANCE;
}

// Colours are an index space in the X11 backend.  Here a name hashes to a
// stable value, which is all any caller may assume of an xa_color.
static xa_color null_color_hash(const char *name)
{
  xa_color h = 5381;
  if (name == NULL)
  {
    return 0;
  }
  while (*name)
  {
    h = ((h << 5) + h) + (unsigned char)*name++;
  }
  return h & 0xffffff;
}

int xa_color_is_direct(void)
{
  null_record("xa_color_is_direct");
  return 1;               // no colormap here, so packing always works
}


void xa_color_pack(unsigned short r, unsigned short g, unsigned short b,
                   xa_color *pixel)
{
  null_record("xa_color_pack");
  if (pixel == NULL)
  {
    return;
  }
  // Always writes: xa_color_is_direct() is 1 here, so the "leave it alone"
  // case cannot arise.  Same 8-bits-per-channel packing xa_color_rgb()
  // unpacks, so a value through both comes back unchanged.
  *pixel = (((xa_color)(r >> 8) & 0xff) << 16)
           | (((xa_color)(g >> 8) & 0xff) << 8)
           | ((xa_color)(b >> 8) & 0xff);
}


// How much the core asked to have drawn.  For a harness, not for the core.
long xa_null_call_count(void)
{
  return null_calls;
}

xa_surface_id xa_screen_target(void)
{
  null_record("xa_screen_target");
  return (xa_surface_id)1;   // one notional screen
}


void xa_canvas_size(int *width, int *height)
{
  null_record("xa_canvas_size");
  if (width)  { *width  = null_canvas_w; }
  if (height) { *height = null_canvas_h; }
}


xa_font xa_font_load(const char *name)
{
  null_record("xa_font_load");
  (void)name;
  return (xa_font)&null_font_token;
}


void xa_font_free(xa_font f)
{
  (void)f;
  null_record("xa_font_free");
}


void xa_font_metrics_get(xa_font f, xa_font_metrics *m)
{
  null_record("xa_font_metrics_get");
  (void)f;
  if (m == NULL) { return; }
  m->max_width = NULL_ADVANCE;
  m->min_width = NULL_ADVANCE;
  m->ascent = NULL_ASCENT;
  m->descent = NULL_DESCENT;
}


void xa_font_text_extents(xa_font f, const char *text, int length, int *width, int *ascent, int *descent)
{
  null_record("xa_font_text_extents");
  (void)f;
  if (width)   { *width   = null_text_width(text, length); }
  if (ascent)  { *ascent  = NULL_ASCENT; }
  if (descent) { *descent = NULL_DESCENT; }
}


int xa_font_text_width(xa_font f, const char *text, int length)
{
  null_record("xa_font_text_width");
  (void)f;
  return null_text_width(text, length);
}


void xa_pen_font_metrics(xa_pen pen, xa_font_metrics *m)
{
  null_record("xa_pen_font_metrics");
  (void)pen;
  if (m == NULL) { return; }
  m->max_width = NULL_ADVANCE;
  m->min_width = NULL_ADVANCE;
  m->ascent = NULL_ASCENT;
  m->descent = NULL_DESCENT;
}


int xa_pen_text_width(xa_pen pen, const char *text, int length)
{
  null_record("xa_pen_text_width");
  (void)pen;
  return null_text_width(text, length);
}


void xa_pen_font(xa_pen pen, xa_font f)
{
  (void)pen;
  (void)f;
  null_record("xa_pen_font");
}


void xa_draw_text_rotated(xa_surface_id dst, xa_pen pen, xa_font f, int x, int y, float degrees, int align, const char *text)
{
  (void)dst;
  (void)pen;
  (void)f;
  (void)x;
  (void)y;
  (void)degrees;
  (void)align;
  (void)text;
  null_record("xa_draw_text_rotated");
}


void xa_draw_text_styled(xa_surface_id dst, int x, int y, float degrees, const char *text, const char *fontspec, xa_color fg, int outline, xa_color outline_color, int align)
{
  (void)dst;
  (void)x;
  (void)y;
  (void)degrees;
  (void)text;
  (void)fontspec;
  (void)fg;
  (void)outline;
  (void)outline_color;
  (void)align;
  null_record("xa_draw_text_styled");
}


int xa_text_width(const char *text, const char *fontspec)
{
  null_record("xa_text_width");
  (void)fontspec;
  return null_text_width(text, text ? (int)strlen(text) : 0);
}


int xa_text_height(const char *fontspec)
{
  null_record("xa_text_height");
  (void)fontspec;
  return NULL_ASCENT + NULL_DESCENT;
}


void xa_color_rgb(xa_color c, unsigned short *r, unsigned short *g, unsigned short *b)
{
  null_record("xa_color_rgb");
  // The inverse of null_color_hash()'s packing, so a round trip is stable.
  if (r) { *r = (unsigned short)(((c >> 16) & 0xff) * 257); }
  if (g) { *g = (unsigned short)(((c >>  8) & 0xff) * 257); }
  if (b) { *b = (unsigned short)(( c        & 0xff) * 257); }
}


xa_color xa_color_by_name(const char *name)
{
  null_record("xa_color_by_name");
  return null_color_hash(name);
}


int xa_color_resolve(unsigned short *r, unsigned short *g, unsigned short *b, xa_color *pixel)
{
  null_record("xa_color_resolve");
  // Always succeeds: there is no colormap to run out of.
  if (pixel)
  {
    *pixel = (((xa_color)(r ? *r : 0) >> 8) << 16)
             | (((xa_color)(g ? *g : 0) >> 8) << 8)
             | ((xa_color)(b ? *b : 0) >> 8);
  }
  return 1;
}


xa_surface_id xa_surface_create(int width, int height, int depth)
{
  null_record("xa_surface_create");
  (void)depth;
  return null_surface_new(width, height);
}


void xa_surface_destroy(xa_surface_id s)
{
  null_record("xa_surface_destroy");
  null_surface_drop(s);
}


xa_surface_id xa_bitmap_from_data(const char *bits, int width, int height)
{
  null_record("xa_bitmap_from_data");
  (void)bits;
  return null_surface_new(width, height);
}


xa_surface_id xa_bitmap_load(const char *path, int *width, int *height)
{
  null_record("xa_bitmap_load");
  // No file formats here.  A caller that needs a real bitmap gets nothing
  // and must cope, which is exactly what it must do when a file is missing.
  (void)path;
  if (width)  { *width  = 0; }
  if (height) { *height = 0; }
  return XA_SURFACE_NONE;
}


xa_image xa_image_capture(xa_surface_id src, int x, int y, int width, int height)
{
  null_record("xa_image_capture");
  (void)src; (void)x; (void)y;
  return null_image_new(width, height);
}


xa_image xa_image_create(int width, int height)
{
  null_record("xa_image_create");
  return null_image_new(width, height);
}


xa_image xa_image_load(const char *path, int *width, int *height)
{
  null_record("xa_image_load");
  (void)path;
  if (width) { *width = 0; }
  if (height) { *height = 0; }
  return NULL;
}


void xa_image_destroy(xa_image img)
{
  null_record("xa_image_destroy");
  null_free(img);
}


void xa_image_put_pixel(xa_image img, int x, int y, xa_color c)
{
  null_record("xa_image_put_pixel");
  null_image_set(img, x, y, c);
}


xa_color xa_image_get_pixel(xa_image img, int x, int y)
{
  null_record("xa_image_get_pixel");
  return null_image_pixel(img, x, y);
}


void xa_image_to_surface(xa_surface_id dst, xa_pen pen, xa_image img, int src_x, int src_y, int dst_x, int dst_y, int width, int height)
{
  (void)dst;
  (void)pen;
  (void)img;
  (void)src_x;
  (void)src_y;
  (void)dst_x;
  (void)dst_y;
  (void)width;
  (void)height;
  null_record("xa_image_to_surface");
}


xa_region xa_region_create(void)
{
  null_record("xa_region_create");
  return null_alloc(NULL_REGION);
}


void xa_region_destroy(xa_region r)
{
  null_record("xa_region_destroy");
  null_free(r);
}


xa_region xa_region_from_polygon(xa_point *points, int npoints, int rule)
{
  null_record("xa_region_from_polygon");
  (void)points;
  (void)npoints;
  (void)rule;
  return null_alloc(NULL_REGION);
}


void xa_region_add_rect(xa_region r, int x, int y, int width, int height)
{
  (void)r;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  null_record("xa_region_add_rect");
}


void xa_region_subtract(xa_region a, xa_region b, xa_region dst)
{
  (void)a;
  (void)b;
  (void)dst;
  null_record("xa_region_subtract");
}


xa_pen xa_pen_create(xa_surface_id for_surface)
{
  null_record("xa_pen_create");
  (void)for_surface;
  return null_alloc(NULL_PEN);
}


void xa_pen_destroy(xa_pen pen)
{
  null_record("xa_pen_destroy");
  null_free(pen);
}


void xa_copy_area(xa_surface_id src, xa_surface_id dst, xa_pen pen, int src_x, int src_y, int width, int height, int dst_x, int dst_y)
{
  (void)src;
  (void)dst;
  (void)pen;
  (void)src_x;
  (void)src_y;
  (void)width;
  (void)height;
  (void)dst_x;
  (void)dst_y;
  null_record("xa_copy_area");
}


void xa_present_full(xa_surface_id src)
{
  (void)src;
  null_record("xa_present_full");
}


void xa_pen_color(xa_pen pen, xa_color c)
{
  (void)pen;
  (void)c;
  null_record("xa_pen_color");
}


void xa_pen_bg(xa_pen pen, xa_color c)
{
  (void)pen;
  (void)c;
  null_record("xa_pen_bg");
}


void xa_pen_line(xa_pen pen, int width, int line_style, int cap, int join)
{
  (void)pen;
  (void)width;
  (void)line_style;
  (void)cap;
  (void)join;
  null_record("xa_pen_line");
}


void xa_pen_dashes(xa_pen pen, int dash_offset, const char *dash_list, int n)
{
  (void)pen;
  (void)dash_offset;
  (void)dash_list;
  (void)n;
  null_record("xa_pen_dashes");
}


void xa_pen_fill_style(xa_pen pen, int fill_style)
{
  (void)pen;
  (void)fill_style;
  null_record("xa_pen_fill_style");
}


void xa_pen_stipple(xa_pen pen, xa_surface_id bitmap)
{
  (void)pen;
  (void)bitmap;
  null_record("xa_pen_stipple");
}


void xa_pen_ts_origin(xa_pen pen, int x, int y)
{
  (void)pen;
  (void)x;
  (void)y;
  null_record("xa_pen_ts_origin");
}


void xa_pen_function(xa_pen pen, int func)
{
  (void)pen;
  (void)func;
  null_record("xa_pen_function");
}


void xa_pen_clip_mask(xa_pen pen, xa_surface_id mask)
{
  (void)pen;
  (void)mask;
  null_record("xa_pen_clip_mask");
}


void xa_pen_clip_origin(xa_pen pen, int x, int y)
{
  (void)pen;
  (void)x;
  (void)y;
  null_record("xa_pen_clip_origin");
}


void xa_pen_clip_region(xa_pen pen, xa_region r)
{
  (void)pen;
  (void)r;
  null_record("xa_pen_clip_region");
}


void xa_pen_copy_fill(xa_pen dst, xa_pen src)
{
  (void)dst;
  (void)src;
  null_record("xa_pen_copy_fill");
}


void xa_draw_line(xa_surface_id dst, xa_pen pen, int x1, int y1, int x2, int y2)
{
  (void)dst;
  (void)pen;
  (void)x1;
  (void)y1;
  (void)x2;
  (void)y2;
  null_record("xa_draw_line");
}


void xa_draw_lines(xa_surface_id dst, xa_pen pen, xa_point *points, int npoints, int coord_mode)
{
  (void)dst;
  (void)pen;
  (void)points;
  (void)npoints;
  (void)coord_mode;
  null_record("xa_draw_lines");
}


void xa_draw_point(xa_surface_id dst, xa_pen pen, int x, int y)
{
  (void)dst;
  (void)pen;
  (void)x;
  (void)y;
  null_record("xa_draw_point");
}


void xa_draw_rect(xa_surface_id dst, xa_pen pen, int x, int y, int width, int height)
{
  (void)dst;
  (void)pen;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  null_record("xa_draw_rect");
}


void xa_fill_rect(xa_surface_id dst, xa_pen pen, int x, int y, int width, int height)
{
  (void)dst;
  (void)pen;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  null_record("xa_fill_rect");
}


void xa_fill_polygon(xa_surface_id dst, xa_pen pen, xa_point *points, int npoints, int shape, int coord_mode)
{
  (void)dst;
  (void)pen;
  (void)points;
  (void)npoints;
  (void)shape;
  (void)coord_mode;
  null_record("xa_fill_polygon");
}


void xa_draw_arc(xa_surface_id dst, xa_pen pen, int x, int y, int width, int height, int angle1, int angle2)
{
  (void)dst;
  (void)pen;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)angle1;
  (void)angle2;
  null_record("xa_draw_arc");
}


void xa_fill_arc(xa_surface_id dst, xa_pen pen, int x, int y, int width, int height, int angle1, int angle2)
{
  (void)dst;
  (void)pen;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)angle1;
  (void)angle2;
  null_record("xa_fill_arc");
}


void xa_draw_string(xa_surface_id dst, xa_pen pen, int x, int y, const char *text, int length)
{
  (void)dst;
  (void)pen;
  (void)x;
  (void)y;
  (void)text;
  (void)length;
  null_record("xa_draw_string");
}

void xa_fill_rings(xa_surface_id dst, xa_pen pen,
                   const xa_pointf *pts, const int *ring_sizes, int nrings,
                   double alpha)
{
  (void)dst; (void)pen; (void)pts; (void)ring_sizes; (void)nrings; (void)alpha;
}

int xa_device_scale(void)
{
  return 1;
}
