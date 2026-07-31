/*
 * PMTiles: a whole tile set in one file, with no server.
 *
 * The usual way to serve vector tiles is a tile server and a network request
 * per tile.  For an application people carry to emergencies that is the wrong
 * shape entirely -- a map that needs the internet is a map you do not have
 * when you need it.  A PMTiles archive is a single file holding every tile,
 * with an index at the front, so a region lives on the disk and a tile is a
 * seek and a read.
 *
 * This reads version 3.  The format is a 127-byte header, a root directory, an
 * optional layer of leaf directories, and the tile data; directories are runs
 * of varints, and tiles are addressed by a Hilbert-curve index that keeps
 * neighbouring tiles near each other in the file.
 *
 * What comes out is bytes.  Whether those bytes are MVT, PNG or something else
 * is in the header, and decoding them is somebody else's job -- mvt.c for the
 * vector case.
 */
#ifndef ASTIR_PMTILES_H
#define ASTIR_PMTILES_H

#include <stddef.h>
#include <stdio.h>

// Tile payload types, as the specification numbers them.
#define PMTILES_TYPE_UNKNOWN 0
#define PMTILES_TYPE_MVT     1
#define PMTILES_TYPE_PNG     2
#define PMTILES_TYPE_JPEG    3
#define PMTILES_TYPE_WEBP    4

// Compressions, likewise.  Only NONE and GZIP are handled; brotli and zstd
// would each be another dependency, and no common build uses them for tiles.
#define PMTILES_COMPRESS_UNKNOWN 0
#define PMTILES_COMPRESS_NONE    1
#define PMTILES_COMPRESS_GZIP    2
#define PMTILES_COMPRESS_BROTLI  3
#define PMTILES_COMPRESS_ZSTD    4

typedef struct
{
  FILE *f;

  unsigned long long root_off, root_len;
  unsigned long long leaf_off, leaf_len;
  unsigned long long data_off, data_len;

  int internal_compression;      // how the DIRECTORIES are compressed
  int tile_compression;          // how the TILES are compressed
  int tile_type;
  int min_zoom, max_zoom;

  // Bounds, in degrees.  Worth having: a driver can skip an archive that does
  // not cover the view without reading a single directory.
  double min_lon, min_lat, max_lon, max_lat;
} pmtiles;

/*
 * Open an archive.  NULL if the file is missing or is not a PMTiles v3.
 *
 * A wrong or truncated file is an ordinary failure: these are user-supplied
 * downloads, and the driver has to carry on with the other maps.
 */
pmtiles *pmtiles_open(const char *path);
void pmtiles_close(pmtiles *p);

/*
 * Fetch one tile, decompressed.
 *
 * Returns 1 and fills the data and length on success; the caller frees *data.  Returns
 * 0 when the archive simply has no tile there, which is normal -- a tile set
 * is sparse over the ocean and outside its own region.
 */
int pmtiles_get(pmtiles *p, int z, unsigned int x, unsigned int y,
                unsigned char **data, size_t *len);

/*
 * The Hilbert-curve tile id for a z/x/y, exposed for testing.
 *
 * PMTiles orders tiles along a Hilbert curve rather than row by row, so that
 * tiles near each other on the map are near each other in the file and a
 * viewport reads a short run rather than scattered seeks.
 */
unsigned long long pmtiles_tile_id(int z, unsigned int x, unsigned int y);

#endif /* ASTIR_PMTILES_H */
