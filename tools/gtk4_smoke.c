/*
 * gtk4_smoke.c -- does the GTK4 backend actually draw, or merely link?
 *
 * A backend that compiles and links proves the interface is satisfiable.  It
 * does not prove a single pixel comes out, and "it links" is exactly the kind
 * of claim this project has learned not to accept.  There is no GTK4 front end
 * to drive the real thing, so this drives the backend directly: it makes a
 * surface, draws through xa_draw.h, and writes a PNG.
 *
 * It then checks the result rather than trusting it -- counts distinct colours
 * and samples specific pixels, so an all-black image fails instead of passing
 * quietly.  Headless: Cairo image surfaces and Pango need no display.
 *
 * Built and run by tools/gtk4_smoke.sh.  Not part of the astir build.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cairo.h>
#include <glib.h>

#include "draw/xa_draw.h"

// Provided by the backend; declared here so this file needs no gtk header.
void             xa_gtk4_set_canvas(void *canvas, int width, int height);
cairo_surface_t *xa_gtk4_canvas_surface(void);

#define W 400
#define H 300

static int failures = 0;

static void check(const char *what, int ok)
{
  printf("  %-46s %s\n", what, ok ? "ok" : "FAILED");
  if (!ok)
  {
    failures++;
  }
}

static unsigned pixel_at(cairo_surface_t *s, int x, int y)
{
  unsigned char *d = cairo_image_surface_get_data(s);
  int stride = cairo_image_surface_get_stride(s);

  cairo_surface_flush(s);
  if (d == NULL)
  {
    return 0;
  }
  return ((unsigned *)(d + (size_t)y * stride))[x] & 0x00ffffff;
}

/*
 * Count the backend's own "repaired" warnings, and let everything else through.
 *
 * The point is to tell the backend's message apart from Pango's: one means the
 * text was fixed and drawn, the other means it was refused and lost.
 */
static int saw_repair_warning;

static void count_repair_warnings(const gchar *domain, GLogLevelFlags level,
                                  const gchar *message, gpointer user_data)
{
  if (message != NULL && strstr(message, "was repaired for display") != NULL)
  {
    saw_repair_warning++;
    return;                      /* expected here; do not print it */
  }
  g_log_default_handler(domain, level, message, user_data);
}


int main(void)
{
  xa_surface_id canvas, off, stipple;
  xa_pen pen;
  xa_font font;
  xa_font_metrics fm;
  xa_image img;
  xa_color red, blue, green;
  xa_point poly[4];
  cairo_surface_t *cs;
  int w, h, i, distinct = 0;
  unsigned seen[64];
  int nseen = 0;

  // The canvas is created without a widget: no display, no main loop.
  xa_gtk4_set_canvas(NULL, W, H);
  canvas = xa_screen_target();
  check("xa_screen_target() gives a canvas", canvas != XA_SURFACE_NONE);

  xa_canvas_size(&w, &h);
  check("xa_canvas_size() reports what was set", w == W && h == H);

  red   = xa_color_by_name("red");
  blue  = xa_color_by_name("blue");
  green = xa_color_by_name("#00ff00");
  check("xa_color_by_name(\"red\")", red == 0xff0000);
  check("xa_color_by_name(\"blue\")", blue == 0x0000ff);
  check("xa_color_by_name(\"#00ff00\")", green == 0x00ff00);

  {
    unsigned short r = 0, g = 0, b = 0;
    xa_color_rgb(red, &r, &g, &b);
    check("xa_color_rgb() round trip", r == 0xffff && g == 0 && b == 0);
  }
  {
    unsigned short r = 0x8000, g = 0x4000, b = 0x2000;
    xa_color p = 0;
    check("xa_color_resolve() succeeds", xa_color_resolve(&r, &g, &b, &p) != 0);
    check("xa_color_resolve() packs", p == 0x804020);
  }

  pen = xa_pen_create(canvas);
  check("xa_pen_create()", pen != NULL);

  // A background, so "did anything draw" is not confused with "it was blank".
  xa_pen_color(pen, 0x202020);
  xa_fill_rect(canvas, pen, 0, 0, W, H);
  check("background filled", pixel_at(xa_gtk4_canvas_surface(), 5, 5) == 0x202020);

  xa_pen_color(pen, red);
  xa_pen_line(pen, 3, XA_LINE_SOLID, XA_CAP_ROUND, XA_JOIN_ROUND);
  xa_draw_line(canvas, pen, 20, 20, 380, 60);
  check("xa_draw_line() marked the surface",
        pixel_at(xa_gtk4_canvas_surface(), 20, 20) != 0x202020);

  xa_pen_color(pen, blue);
  xa_fill_rect(canvas, pen, 30, 90, 80, 40);
  check("xa_fill_rect() is the pen colour",
        pixel_at(xa_gtk4_canvas_surface(), 60, 100) == 0x0000ff);

  xa_pen_color(pen, green);
  xa_pen_line(pen, 1, XA_LINE_SOLID, XA_CAP_BUTT, XA_JOIN_MITER);
  xa_draw_rect(canvas, pen, 130, 90, 80, 40);

  poly[0].x = 240; poly[0].y = 90;
  poly[1].x = 320; poly[1].y = 90;
  poly[2].x = 340; poly[2].y = 130;
  poly[3].x = 260; poly[3].y = 130;
  xa_pen_color(pen, 0xffaa00);
  xa_fill_polygon(canvas, pen, poly, 4, XA_SHAPE_CONVEX, XA_COORD_ORIGIN);
  check("xa_fill_polygon() filled its interior",
        pixel_at(xa_gtk4_canvas_surface(), 290, 110) == 0xffaa00);

  xa_pen_color(pen, 0x00ffff);
  xa_fill_arc(canvas, pen, 30, 150, 60, 60, 0, 360 * 64);
  check("xa_fill_arc() filled the centre",
        pixel_at(xa_gtk4_canvas_surface(), 60, 180) == 0x00ffff);

  xa_pen_line(pen, 2, XA_LINE_ON_OFF_DASH, XA_CAP_BUTT, XA_JOIN_MITER);
  xa_pen_dashes(pen, 0, "\x04\x04", 2);
  xa_pen_color(pen, 0xff00ff);
  xa_draw_line(canvas, pen, 110, 160, 380, 160);
  xa_pen_line(pen, 1, XA_LINE_SOLID, XA_CAP_BUTT, XA_JOIN_MITER);

  // Text, which is the half of the interface Pango replaces wholesale.
  font = xa_font_load("-adobe-helvetica-medium-r-normal--14-*-*-*-*-*-iso8859-1");
  check("xa_font_load() parses an XLFD", font != XA_FONT_NONE);
  xa_font_metrics_get(font, &fm);
  check("font metrics are plausible",
        fm.ascent > 0 && fm.descent >= 0 && fm.max_width > 0);
  check("xa_font_text_width() grows with the string",
        xa_font_text_width(font, "iiii", 4) < xa_font_text_width(font, "WWWW", 4));

  xa_pen_font(pen, font);
  xa_pen_color(pen, 0xffffff);
  xa_draw_string(canvas, pen, 30, 240, "Astir GTK4 backend", 19);
  check("xa_pen_text_width() answers from pen state",
        xa_pen_text_width(pen, "Astir", 6) > 0);

  xa_draw_text_styled(canvas, 200, 250, 30.0f, "rotated 30", "Sans:size=13",
                      0xffff00, 1, 0x000000, XA_ALIGN_MLEFT);
  check("xa_text_width() by fontspec", xa_text_width("mmmm", "Sans:size=13") > 0);
  check("xa_text_height() by fontspec", xa_text_height("Sans:size=13") > 0);

  /*
   * Text that is not UTF-8 must be repaired, not dropped.
   *
   * Pango requires UTF-8 and, given anything else, lays out NOTHING -- so a
   * string with one bad byte silently vanishes from the screen.  That is how a
   * wind speed beside every weather station and a heading beside every moving
   * station disappeared, and it reads as missing data rather than as a bad
   * byte, which is why it survived so long.
   *
   * Tested by the warning, not by the width.  Measuring proves nothing here:
   * pango_layout_get_pixel_extents() returns the SAME number whether the text
   * was accepted or rejected, so a width assertion passes with the repair
   * removed -- which is a test that cannot fail, and this file had three of
   * them for about ten minutes.
   *
   * The backend says "was repaired for display" when it fixes something.  With
   * the repair taken out, Pango says "Invalid UTF-8 string passed to
   * pango_layout_set_text()" instead, so the two cases are told apart by which
   * warning arrives.
   */
  {
    const char *latin1_degree = "270\xb0 at 12";      /* Latin-1 degree sign */
    const char *truncated_utf8 = "temp \xc2";         /* a lead byte, alone */

    saw_repair_warning = 0;
    g_log_set_default_handler(count_repair_warnings, NULL);

    (void)xa_text_width(latin1_degree, "Sans:size=13");
    (void)xa_text_width(truncated_utf8, "Sans:size=13");

    g_log_set_default_handler(g_log_default_handler, NULL);

    check("bad UTF-8 is repaired rather than dropped", saw_repair_warning >= 2);
    check("repaired text still measures",
          xa_text_width(latin1_degree, "Sans:size=13") > 0);
  }
  // A stipple, exercising the fill-style path.
  {
    static const char bits[] = { 0x05, 0x0a, 0x05, 0x0a };
    stipple = xa_bitmap_from_data(bits, 4, 4);
    check("xa_bitmap_from_data()", stipple != XA_SURFACE_NONE);
    xa_pen_fill_style(pen, XA_FILL_STIPPLED);
    xa_pen_stipple(pen, stipple);
    xa_pen_color(pen, 0xffffff);
    xa_fill_rect(canvas, pen, 250, 170, 120, 50);
    xa_pen_fill_style(pen, XA_FILL_SOLID);
  }

  // Offscreen surface, copy and present -- the frame-composition path.
  off = xa_surface_create(60, 60, XA_DEPTH_CANVAS);
  check("xa_surface_create()", off != XA_SURFACE_NONE);
  {
    xa_pen p2 = xa_pen_create(off);
    xa_pen_color(p2, 0xff8080);
    xa_fill_rect(off, p2, 0, 0, 60, 60);
    xa_pen_destroy(p2);
  }
  xa_copy_area(off, canvas, pen, 0, 0, 60, 60, 330, 220);
  check("xa_copy_area() moved the pixels",
        pixel_at(xa_gtk4_canvas_surface(), 350, 240) == 0xff8080);

  // Pixel buffers: capture, poke, put back.
  img = xa_image_capture(canvas, 0, 0, 40, 40);
  check("xa_image_capture()", img != XA_IMAGE_NONE);
  check("captured pixel matches the surface",
        xa_image_get_pixel(img, 5, 5) == 0x202020);
  for (i = 0; i < 40; i++)
  {
    xa_image_put_pixel(img, i, i, 0xffffff);
  }
  xa_image_to_surface(canvas, pen, img, 0, 0, 0, 0, 40, 40);
  check("xa_image_to_surface() wrote the diagonal",
        pixel_at(xa_gtk4_canvas_surface(), 20, 20) == 0xffffff);
  xa_image_destroy(img);

  // Regions: a rectangle with a triangular hole, then fill through it.
  {
    xa_region base = xa_region_create();
    xa_point tri[3];
    xa_region hole, diff;
    xa_pen cp;

    xa_region_add_rect(base, 150, 200, 90, 70);
    tri[0].x = 170; tri[0].y = 215;
    tri[1].x = 220; tri[1].y = 215;
    tri[2].x = 195; tri[2].y = 260;
    hole = xa_region_from_polygon(tri, 3, XA_EVEN_ODD);
    diff = xa_region_create();
    xa_region_subtract(base, hole, diff);

    cp = xa_pen_create(canvas);
    xa_pen_color(cp, 0x40c040);
    xa_pen_clip_region(cp, diff);
    xa_fill_rect(canvas, cp, 150, 200, 90, 70);
    xa_pen_destroy(cp);

    check("region clip painted outside the hole",
          pixel_at(xa_gtk4_canvas_surface(), 155, 205) == 0x40c040);
    check("region clip left the hole unpainted",
          pixel_at(xa_gtk4_canvas_surface(), 195, 235) != 0x40c040);
    xa_region_destroy(base);
    xa_region_destroy(hole);
    xa_region_destroy(diff);
  }

  xa_present_full(canvas);

  // How much actually got drawn.  A backend that silently no-ops would come
  // out here as one or two colours.
  cs = xa_gtk4_canvas_surface();
  for (i = 0; i < W * H; i += 7)
  {
    unsigned p = pixel_at(cs, i % W, (i / W) % H);
    int j, found = 0;
    for (j = 0; j < nseen; j++)
    {
      if (seen[j] == p) { found = 1; break; }
    }
    if (!found && nseen < 64)
    {
      seen[nseen++] = p;
      distinct++;
    }
  }
  printf("  %-46s %d\n", "distinct colours sampled", distinct);
  check("the frame has real content (>= 8 colours)", distinct >= 8);

  cairo_surface_write_to_png(cs, "/tmp/astir_gtk4_smoke.png");
  check("PNG written",
        cairo_surface_status(cs) == CAIRO_STATUS_SUCCESS);

  xa_font_free(font);
  xa_pen_destroy(pen);
  xa_surface_destroy(off);
  xa_surface_destroy(stipple);

  printf("\n%s  (%d failure%s)\n", failures ? "SMOKE TEST FAILED" : "SMOKE TEST PASSED",
         failures, failures == 1 ? "" : "s");
  printf("wrote /tmp/astir_gtk4_smoke.png\n");
  return failures ? 1 : 0;
}
