/*
 * Check the MVT decoder against tiles built here to a known answer.
 *
 * The encoder below is written from the specification independently of the
 * decoder, so agreement means both match the spec rather than that one matches
 * the other.  A decoder tested only against its own encoder agrees with itself
 * about anything, including being wrong.
 *
 * Built and run by tools/mvt_test.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/map/mvt.h"

static int failures;
static int checks;

static void ok(int cond, const char *what)
{
  checks++;
  if (!cond)
  {
    failures++;
    printf("  FAIL  %s\n", what);
  }
}

/* ---- a small protobuf writer, from the spec ----------------------------- */

typedef struct { unsigned char b[8192]; size_t n; } buf;

static void put_varint(buf *b, unsigned long long v)
{
  do
  {
    unsigned char byte = v & 0x7f;
    v >>= 7;
    if (v) { byte |= 0x80; }
    b->b[b->n++] = byte;
  }
  while (v);
}

static void put_tag(buf *b, int field, int wire)
{
  put_varint(b, ((unsigned long long)field << 3) | (unsigned)wire);
}

static void put_bytes(buf *b, int field, const void *p, size_t n)
{
  put_tag(b, field, 2);
  put_varint(b, n);
  memcpy(b->b + b->n, p, n);
  b->n += n;
}

static void put_str(buf *b, int field, const char *s)
{
  put_bytes(b, field, s, strlen(s));
}

static unsigned long long zz(int v)
{
  // Cast before shifting: shifting a negative int left is undefined, which
  // UBSan says out loud even though every compiler in practice does the
  // obvious thing.
  return ((unsigned long long)((unsigned int)v << 1))
         ^ (unsigned long long)(unsigned int)(v >> 31);
}

int main(void)
{
  buf value, feat, poly, layer, tile;
  mvt_tile *t;

  /* A value message holding the string "motorway". */
  value.n = 0;
  put_str(&value, 1, "motorway");

  /*
   * One line feature: tags {key 0 -> value 0}, geometry type LINESTRING,
   * geometry MoveTo(10,10) then LineTo(20,30) and LineTo(-5,0) as deltas.
   */
  feat.n = 0;
  {
    buf tags, geom;

    tags.n = 0;
    put_varint(&tags, 0);        // key index
    put_varint(&tags, 0);        // value index
    put_bytes(&feat, 2, tags.b, tags.n);

    put_tag(&feat, 3, 0);
    put_varint(&feat, MVT_GEOM_LINE);

    /*
     * A command integer is (count << 3) | command, NOT (command << 3) | count.
     *
     * This test had them the wrong way round and passed anyway, because the
     * two cases it used were MoveTo with count 1 and LineTo with count 2 --
     * (1<<3)|1 and (2<<3)|2 are the same number either way.  The error only
     * surfaced on a polygon, where LineTo carries four points and the two
     * orderings finally differ.  The third command below exists so that
     * coincidence cannot come back.
     */
    geom.n = 0;
    put_varint(&geom, (1 << 3) | 1);      // MoveTo, count 1
    put_varint(&geom, zz(10));
    put_varint(&geom, zz(10));
    put_varint(&geom, (2 << 3) | 2);      // LineTo, count 2
    put_varint(&geom, zz(20));
    put_varint(&geom, zz(30));
    put_varint(&geom, zz(-5));
    put_varint(&geom, zz(0));
    put_bytes(&feat, 4, geom.b, geom.n);
  }

  /*
   * A polygon: MoveTo 1, LineTo 4, ClosePath.  LineTo with a count of four is
   * the case where (count << 3) | cmd and (cmd << 3) | count stop agreeing,
   * which is the case the line above cannot test.
   */
  poly.n = 0;
  {
    buf tags, geom;

    tags.n = 0;
    put_varint(&tags, 0);
    put_varint(&tags, 0);
    put_bytes(&poly, 2, tags.b, tags.n);

    put_tag(&poly, 3, 0);
    put_varint(&poly, MVT_GEOM_POLYGON);

    geom.n = 0;
    put_varint(&geom, (1 << 3) | 1);      // MoveTo, count 1
    put_varint(&geom, zz(0));
    put_varint(&geom, zz(0));
    put_varint(&geom, (4 << 3) | 2);      // LineTo, count 4
    put_varint(&geom, zz(100)); put_varint(&geom, zz(0));
    put_varint(&geom, zz(0));   put_varint(&geom, zz(100));
    put_varint(&geom, zz(-100));put_varint(&geom, zz(0));
    put_varint(&geom, zz(0));   put_varint(&geom, zz(-100));
    put_varint(&geom, (1 << 3) | 7);      // ClosePath
    put_bytes(&poly, 4, geom.b, geom.n);
  }

  layer.n = 0;
  put_str(&layer, 1, "roads");
  put_bytes(&layer, 2, feat.b, feat.n);
  put_bytes(&layer, 2, poly.b, poly.n);
  put_str(&layer, 3, "highway");
  put_bytes(&layer, 4, value.b, value.n);
  put_tag(&layer, 5, 0);
  put_varint(&layer, 4096);

  tile.n = 0;
  put_bytes(&tile, 3, layer.b, layer.n);

  printf("decoding a %zu byte tile\n", tile.n);
  t = mvt_decode(tile.b, tile.n);
  ok(t != NULL, "tile decodes");

  if (t != NULL)
  {
    ok(t->nlayers == 1, "one layer");
    if (t->nlayers == 1)
    {
      mvt_layer *l = &t->layers[0];

      ok(l->name && strcmp(l->name, "roads") == 0, "layer name is roads");
      ok(l->extent == 4096, "extent is 4096");
      ok(l->nfeatures == 2, "two features");

      if (l->nfeatures == 2)
      {
        mvt_feature *f = &l->features[0];
        mvt_feature *g = &l->features[1];
        const char *hw = mvt_attr(f, "highway");

        ok(f->type == MVT_GEOM_LINE, "geometry type is line");
        ok(f->npts == 3, "three points");
        ok(f->nparts == 1, "one part");

        // Deltas accumulate: (10,10) then +(20,30) then +(-5,0).
        if (f->npts == 3)
        {
          ok(f->pts[0].x == 10 && f->pts[0].y == 10, "first point (10,10)");
          ok(f->pts[1].x == 30 && f->pts[1].y == 40, "second point (30,40)");
          ok(f->pts[2].x == 25 && f->pts[2].y == 40, "third point (25,40)");
        }
        ok(hw != NULL && strcmp(hw, "motorway") == 0,
           "attribute highway=motorway");
        ok(mvt_attr(f, "nosuchkey") == NULL, "missing attribute is NULL");

        // The polygon: five points, one ring, closed.
        ok(g->type == MVT_GEOM_POLYGON, "second feature is a polygon");
        ok(g->nparts == 1, "polygon has one ring");
        ok(g->npts == 5, "polygon ring has five points");
        if (g->npts == 5)
        {
          ok(g->pts[0].x == 0   && g->pts[0].y == 0,   "ring starts at (0,0)");
          ok(g->pts[1].x == 100 && g->pts[1].y == 0,   "ring point 2 (100,0)");
          ok(g->pts[2].x == 100 && g->pts[2].y == 100, "ring point 3 (100,100)");
          ok(g->pts[3].x == 0   && g->pts[3].y == 100, "ring point 4 (0,100)");
          ok(g->pts[4].x == 0   && g->pts[4].y == 0,   "ring closes at (0,0)");
        }
      }
    }
    mvt_free(t);
  }

  /* Malformed input must fail rather than crash: it comes off a network. */
  ok(mvt_decode(NULL, 0) == NULL, "NULL input rejected");
  ok(mvt_decode((const unsigned char *)"", 0) == NULL, "empty input rejected");
  /*
   * Fuzz it.  "Did not crash on sixty-four bytes" is not a claim worth making
   * about a parser that reads from a network; this walks a deterministic
   * pseudo-random corpus and every truncation of the valid tile, which is the
   * shape real corruption takes.
   */
  {
    unsigned long seed = 12345;
    unsigned char junk[512];
    int round;
    size_t i;

    for (round = 0; round < 2000; round++)
    {
      size_t n = (size_t)(seed % sizeof(junk));

      for (i = 0; i < n; i++)
      {
        seed = seed * 1103515245UL + 12345UL;
        junk[i] = (unsigned char)(seed >> 16);
      }
      mvt_free(mvt_decode(junk, n));
      seed = seed * 1103515245UL + 12345UL;
    }
    ok(1, "2000 random buffers decode or fail without crashing");

    for (i = 0; i <= tile.n; i++)
    {
      mvt_free(mvt_decode(tile.b, i));      // every truncation of a real tile
    }
    ok(1, "every truncation of a valid tile is handled");
  }
  {
    // A truncated tile: the layer length says more than the buffer holds.
    buf trunc;

    trunc.n = 0;
    put_tag(&trunc, 3, 2);
    put_varint(&trunc, 9999);
    ok(mvt_decode(trunc.b, trunc.n) == NULL, "truncated layer rejected");
  }

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
