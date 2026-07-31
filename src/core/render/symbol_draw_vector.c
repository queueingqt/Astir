/*
 * Draw an APRS symbol from its outline.
 *
 * This replaces the pixmap blit that symbol() used since 1999: a 20x20 pixmap
 * per symbol, copied to the screen through a 1-bit coverage mask.  That fixed
 * a station icon at 20 device pixels, which was the right call when a pixmap
 * was a server-side resource and there was no such thing as a HiDPI display.
 *
 * Three things fall out of drawing the outline instead, and none of them is a
 * separate feature:
 *
 *   the grey boxes go away.  The mask could only be honoured as its bounding
 *   rectangle by the Cairo backend, so every station sat on an opaque square.
 *   A filled outline has no rectangle to leak.
 *
 *   ghosting becomes alpha.  The old ghost was a checkerboard mask that blanked
 *   every second pixel, because a 1-bit mask was the only transparency an X
 *   pixmap had.
 *
 *   the icon renders at the display's resolution, at whatever size is asked
 *   for, instead of at 20 pixels and then magnified.
 *
 * The four orientations are reproduced exactly as the pixmap path built them,
 * and they are not four rotations.  insert_symbol() built 'u' by rotating a
 * quarter turn, but 'r' by MIRRORING and 'd' by reflecting about the
 * anti-diagonal -- which is the right visual choice for a vehicle drawn facing
 * left, because a mirrored car still has its wheels underneath it and a rotated
 * one does not.  The transforms below are those, written as matrices.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <string.h>

#include "core/astir.h"
#include "draw/xa_draw.h"
#include "core/render/draw_symbols.h"
#include "core/render/symbols_vector.h"

// Symbol outlines live in a 0..20 box, the size the artwork was drawn at.
#define SYM_SPAN 20.0

// The largest number of points in any one glyph, so a draw needs no allocation.
// The generator reports the real maximum; this is comfortably above it and is
// checked at the point of use rather than trusted.
#define SYM_MAX_PTS 1024
#define SYM_MAX_RINGS 128


/*
 * Where a point of the source artwork lands, for one of the four orientations.
 *
 * Continuous coordinates, not pixel indices: a source pixel at column u covers
 * [u, u+1], so a mirror sends it to [SPAN-u-1, SPAN-u], and the transform of
 * the coordinate is SPAN-u rather than 19-u.
 */
static void orient_point(char orient, double u, double v, double *ox, double *oy)
{
  switch (orient)
  {
    case 'u':                    // quarter turn clockwise
      *ox = SYM_SPAN - v;
      *oy = u;
      break;

    case 'r':                    // mirrored, not rotated -- see the file comment
      *ox = SYM_SPAN - u;
      *oy = v;
      break;

    case 'd':                    // reflected about the anti-diagonal
      *ox = SYM_SPAN - v;
      *oy = SYM_SPAN - u;
      break;

    case ' ':
    case 'l':
    default:                     // the artwork as drawn, facing left
      *ox = u;
      *oy = v;
      break;
  }
}


/*
 * Find a glyph by table and symbol character.
 *
 * Linear over 211 entries.  The pixmap path kept a five-entry cache in front
 * of the same search and it is not obviously needed here, but the search is
 * the same shape, so if this ever shows up in a profile the same cache fits.
 */
const astir_sym_glyph *astir_symbol_glyph(char table, char symbol)
{
  int i;

  for (i = 0; i < astir_sym_glyph_count; i++)
  {
    if (astir_sym_glyphs[i].table == table
        && astir_sym_glyphs[i].symbol == symbol)
    {
      return &astir_sym_glyphs[i];
    }
  }
  return NULL;
}


/*
 * Draw one glyph into `where`, with its top-left corner at (x, y).
 *
 * `size` is the width and height in pixels; 20 reproduces the old geometry.
 * `alpha` below 1 ghosts it.
 */
void astir_symbol_draw_glyph(const astir_sym_glyph *g, xa_surface_id where,
                             xa_pen pen, long x, long y, double size,
                             char orient, double alpha)
{
  xa_pointf pts[SYM_MAX_PTS];
  int ring_sizes[SYM_MAX_RINGS];
  double scale;
  int s;

  if (g == NULL || size <= 0.0)
  {
    return;
  }
  scale = size / SYM_SPAN;

  for (s = 0; s < g->nshapes; s++)
  {
    const astir_sym_shape *sh = &astir_sym_shapes[g->first_shape + s];
    int nrings = 0, npts = 0, r;

    for (r = 0; r < sh->nrings; r++)
    {
      const astir_sym_ring *ring = &astir_sym_rings[sh->first_ring + r];
      int k;

      if (nrings >= SYM_MAX_RINGS || npts + ring->npts > SYM_MAX_PTS)
      {
        break;                   // refuse to overrun; see SYM_MAX_PTS
      }
      for (k = 0; k < ring->npts; k++)
      {
        double u = (double)astir_sym_pts[(ring->first_pt + k) * 2];
        double v = (double)astir_sym_pts[(ring->first_pt + k) * 2 + 1];
        double ox, oy;

        orient_point(orient, u, v, &ox, &oy);
        pts[npts].x = (double)x + ox * scale;
        pts[npts].y = (double)y + oy * scale;
        npts++;
      }
      ring_sizes[nrings++] = ring->npts;
    }

    if (nrings == 0)
    {
      continue;
    }
    xa_pen_color(pen, colors[sh->color]);
    xa_fill_rings(where, pen, pts, ring_sizes, nrings, alpha);
  }
}
