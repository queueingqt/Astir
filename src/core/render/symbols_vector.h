/*
 * The APRS symbols as outlines.  Generated; see symbols_vector.c.
 *
 * Each glyph is a list of shapes, each shape a colour plus a list of rings,
 * each ring a run of points in the symbol's 0..20 space.  Rings wind so that
 * the nonzero fill rule puts holes in the right places without the caller
 * having to know which ring is a hole.
 */
#ifndef ASTIR_SYMBOLS_VECTOR_H
#define ASTIR_SYMBOLS_VECTOR_H

typedef struct { unsigned short first_pt; unsigned char npts; } astir_sym_ring;
typedef struct { unsigned char color; unsigned short first_ring; unsigned char nrings; } astir_sym_shape;
typedef struct
{
  char table, symbol;
  unsigned char rotatable;      /* drawn facing left; may be rotated by course */
  unsigned short first_shape;
  unsigned char nshapes;
} astir_sym_glyph;

extern const unsigned char  astir_sym_pts[];
extern const astir_sym_ring astir_sym_rings[];
extern const astir_sym_shape astir_sym_shapes[];
extern const astir_sym_glyph astir_sym_glyphs[];
extern const int astir_sym_glyph_count;

#endif /* ASTIR_SYMBOLS_VECTOR_H */
