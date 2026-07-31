/*
 * Draw symbols through the real code path and write them out, so what the
 * program actually renders can be compared against the artwork it came from.
 *
 * The map render answers "did anything change", which is not the same question
 * as "is it right".  Removing the grey boxes made every icon look different by
 * construction, and a symbol whose light-coloured regions had silently stopped
 * being drawn would look plausible in a screenshot -- the box that used to sit
 * behind it was grey too.
 *
 * Built by tools/symbol_render_check.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include "draw/xa_draw.h"
#include "core/render/symbols_vector.h"

void xa_gtk4_set_canvas(GtkWidget *canvas, int width, int height);
cairo_surface_t *xa_gtk4_canvas_surface(void);
int xa_gtk4_set_device_scale(int scale);

const astir_sym_glyph *astir_symbol_glyph(char table, char symbol);
void astir_symbol_draw_glyph(const astir_sym_glyph *g, xa_surface_id where,
                             xa_pen pen, long x, long y, double size,
                             char orient, double alpha);

// colors[] is defined by the backend; this only fills it, with the same RGB
// values the generator wrote into the SVGs, so a mismatch here is real.
static void set_palette(void)
{
  struct { int i; unsigned int rgb; } t[] = {
    {0x51, 0x000000}, {0x4d, 0xFFFFFF}, {0x43, 0xCCCCCC}, {0x4a, 0xEE0000},
    {0x48, 0x00BFFF}, {0x4c, 0x0000CD}, {0x4b, 0x00CD00}, {0x47, 0x00008B},
    {0x40, 0xFFFF00}, {0x50, 0x454545}, {0x49, 0x006400}, {0x4e, 0x878787},
    {0x41, 0xCD6500}, {0x4f, 0x5A5A5A}, {0x46, 0xCD3333}, {0x42, 0xA020F0},
    {0x45, 0xFF4040}, {0x44, 0xCD0000}, {0x52, 0x32CD32},
  };
  size_t k;

  for (k = 0; k < sizeof(t) / sizeof(t[0]); k++)
  {
    colors[t[k].i] = t[k].rgb;
  }
}

int main(int argc, char **argv)
{
  const char *out = argc > 2 ? argv[2] : "/tmp/symcheck.png";
  int size = argc > 3 ? atoi(argv[3]) : 20;
  char table, symbol;
  const astir_sym_glyph *g;
  xa_surface_id canvas;
  xa_pen pen;
  cairo_surface_t *s;
  cairo_t *cr;

  if (argc < 2 || strlen(argv[1]) < 2)
  {
    fprintf(stderr, "usage: %s <table><symbol> [out.png] [size]\n", argv[0]);
    return 2;
  }
  table = argv[1][0];
  symbol = argv[1][1];

  gtk_init();
  set_palette();
  xa_gtk4_set_device_scale(1);
  xa_gtk4_set_canvas(NULL, size, size);
  canvas = xa_screen_target();

  // Transparent background, so "was this pixel drawn" is answerable.  The
  // canvas is RGB24, so clear it to magenta instead: nothing in the palette is
  // magenta, so any magenta left is a pixel the symbol did not cover.
  s = xa_gtk4_canvas_surface();
  cr = cairo_create(s);
  cairo_set_source_rgb(cr, 1.0, 0.0, 1.0);
  cairo_paint(cr);
  cairo_destroy(cr);

  g = astir_symbol_glyph(table, symbol);
  if (g == NULL)
  {
    fprintf(stderr, "no glyph for %c%c\n", table, symbol);
    return 1;
  }
  pen = xa_pen_create(canvas);
  astir_symbol_draw_glyph(g, canvas, pen, 0, 0, (double)size, ' ', 1.0);

  if (cairo_surface_write_to_png(s, out) != CAIRO_STATUS_SUCCESS)
  {
    fprintf(stderr, "could not write %s\n", out);
    return 1;
  }
  printf("wrote %s (%c%c at %dpx, %d shapes)\n", out, table, symbol, size,
         g->nshapes);
  return 0;
}
