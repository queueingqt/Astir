/*
 * Mapbox Vector Tiles: decoding one tile into features Astir can draw.
 *
 * A vector tile carries GEOMETRY and ATTRIBUTES rather than pixels: road
 * centrelines, polygon rings, and the tags that say what each one is.  That is
 * the difference that matters here -- a raster tile's place names were
 * rasterised by a server at one size and blur when scaled, and a vector tile's
 * are strings this program renders at the display's own resolution.
 *
 * WHY THIS IS HAND WRITTEN
 *
 * MVT is protobuf, and the obvious move is to link protobuf-c.  The schema is
 * four messages and about a dozen fields, all of which are varints, strings or
 * packed varint arrays, so the decoder is smaller than the build-system change
 * needed to carry another dependency -- and this program has spent a long time
 * removing dependencies rather than adding them.
 *
 * WHAT IT DOES NOT DO
 *
 * No styling.  Attributes come out as key/value strings and go to dbfawk, which
 * has been Astir's style engine for shapefiles for twenty years and already
 * knows how to turn a tag into a colour, a width and a zoom range.  Vector
 * tiles usually arrive with a MapLibre style JSON and an interpreter for it;
 * this reuses what is here instead, because the expensive part of a vector tile
 * client is the style engine and Astir already has one.
 */
#ifndef ASTIR_MVT_H
#define ASTIR_MVT_H

#include <stddef.h>

// Geometry types, as the MVT specification numbers them.
#define MVT_GEOM_UNKNOWN 0
#define MVT_GEOM_POINT   1
#define MVT_GEOM_LINE    2
#define MVT_GEOM_POLYGON 3

// One ring or line: a run of points inside mvt_feature's point array.  A
// polygon's first ring is its outer boundary and the rest are holes, which is
// the same convention the traced symbols use.
typedef struct
{
  int first;
  int npts;
} mvt_part;

// A point in TILE coordinates: 0..extent across the tile, y downward.  Turning
// those into longitude and latitude needs the tile's z/x/y, which the caller
// has and this does not.
typedef struct
{
  int x, y;
} mvt_point;

typedef struct
{
  int type;                      // MVT_GEOM_*
  mvt_point *pts;
  int npts;
  mvt_part *parts;
  int nparts;
  // Attributes, as parallel arrays of strings.  Values are rendered to text
  // whatever their wire type, because that is what dbfawk matches on.
  char **keys;
  char **values;
  int nattrs;
} mvt_feature;

typedef struct
{
  char *name;
  unsigned int extent;           // tile coordinate span, 4096 by convention
  mvt_feature *features;
  int nfeatures;

  /*
   * The layer's key and value tables.
   *
   * A feature's tags are indices into these, and the strings are BORROWED by
   * the features rather than copied -- a tile has a few dozen distinct keys
   * and thousands of features, so copying every tag per feature is exactly the
   * per-feature allocation a map draw cannot afford.  They live on the layer
   * because they must outlive every feature in it.
   */
  char **keys_table;
  int nkeys_table;
  char **values_table;
  int nvalues_table;
} mvt_layer;

typedef struct
{
  mvt_layer *layers;
  int nlayers;
} mvt_tile;

/*
 * Decode one tile.  `data` is the raw protobuf, already decompressed.
 *
 * Returns NULL if the buffer is not a tile this can read.  A malformed tile is
 * an expected input, not an assertion: tiles come off disk or a network and
 * being handed rubbish must not take the program down.
 */
mvt_tile *mvt_decode(const unsigned char *data, size_t len);
void mvt_free(mvt_tile *t);

// The value of an attribute by key, or NULL.  For the driver's dbfawk bridge.
const char *mvt_attr(const mvt_feature *f, const char *key);

#endif /* ASTIR_MVT_H */
