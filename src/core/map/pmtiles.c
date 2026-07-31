/*
 * PMTiles v3 reader.  See pmtiles.h for why a single-file archive is the right
 * shape for this application.
 *
 * Layout: a 127-byte header, then a root directory, then optionally a block of
 * leaf directories, then the tile data.  A directory is a run of varints --
 * count, then delta-encoded tile ids, then run lengths, then lengths, then
 * offsets -- and an entry with run_length 0 points at a leaf directory rather
 * than a tile, which is how one archive addresses more tiles than fit in one
 * directory.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "core/map/pmtiles.h"

#define PM_HEADER_LEN 127

/* ---- little-endian header fields ---------------------------------------- */

static unsigned long long le64(const unsigned char *p)
{
  unsigned long long v = 0;
  int i;

  for (i = 7; i >= 0; i--)
  {
    v = (v << 8) | p[i];
  }
  return v;
}


static int le32s(const unsigned char *p)
{
  unsigned int v = (unsigned int)p[0] | ((unsigned int)p[1] << 8)
                   | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);

  return (int)v;
}


/* ---- Hilbert curve ------------------------------------------------------ */

/*
 * Tile id: every zoom level below z, then this tile's position along the
 * Hilbert curve at z.
 *
 * The count of tiles at all levels above z is (4^z - 1)/3, which is why the
 * accumulator loop below is not a formula -- it avoids overflowing at high
 * zoom for the sake of one multiplication.
 */
unsigned long long pmtiles_tile_id(int z, unsigned int x, unsigned int y)
{
  unsigned long long acc = 0;
  unsigned long long d = 0;
  unsigned int rx, ry, s;
  unsigned int tx = x, ty = y;
  int i;

  for (i = 0; i < z; i++)
  {
    acc += (1ULL << i) * (1ULL << i);      // 4^i
  }

  for (s = 1U << (z - (z > 0 ? 1 : 0)); z > 0 && s > 0; s /= 2)
  {
    rx = (tx & s) > 0 ? 1 : 0;
    ry = (ty & s) > 0 ? 1 : 0;
    d += (unsigned long long)s * s * ((3 * rx) ^ ry);

    // Rotate the quadrant so the curve stays continuous.
    if (ry == 0)
    {
      if (rx == 1)
      {
        tx = s - 1 - tx;
        ty = s - 1 - ty;
      }
      {
        unsigned int t = tx;
        tx = ty;
        ty = t;
      }
    }
  }
  return acc + d;
}


/* ---- varints ------------------------------------------------------------ */

typedef struct
{
  const unsigned char *p, *end;
  int bad;
} vrd;


static unsigned long long v_get(vrd *r)
{
  unsigned long long v = 0;
  int shift = 0;

  while (!r->bad && r->p < r->end)
  {
    unsigned char b = *r->p++;

    if (shift > 63) { r->bad = 1; return 0; }
    v |= (unsigned long long)(b & 0x7f) << shift;
    if (!(b & 0x80)) { return v; }
    shift += 7;
  }
  r->bad = 1;
  return 0;
}


/* ---- gzip --------------------------------------------------------------- */

/*
 * Inflate a gzip member.
 *
 * Tiles and directories are usually gzipped, and zlib is already linked, so
 * this costs nothing to support.  The output size is not known in advance, so
 * the buffer grows; tiles are tens of kilobytes, and a bound is imposed anyway
 * because the length comes from a file that may be hostile.
 */
#define PM_MAX_INFLATE (64u * 1024u * 1024u)

static int gunzip(const unsigned char *in, size_t inlen,
                  unsigned char **out, size_t *outlen)
{
  z_stream zs;
  size_t cap = inlen * 4 + 1024;
  unsigned char *buf = (unsigned char *)malloc(cap);
  int rc;

  if (buf == NULL) { return 0; }
  memset(&zs, 0, sizeof(zs));
  zs.next_in = (Bytef *)in;
  zs.avail_in = (uInt)inlen;

  if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)   // 16 = expect a gzip header
  {
    free(buf);
    return 0;
  }

  zs.next_out = buf;
  zs.avail_out = (uInt)cap;

  for (;;)
  {
    rc = inflate(&zs, Z_NO_FLUSH);
    if (rc == Z_STREAM_END)
    {
      break;
    }
    if (rc != Z_OK)
    {
      inflateEnd(&zs);
      free(buf);
      return 0;
    }
    if (zs.avail_out == 0)
    {
      unsigned char *nb;
      size_t used = cap;

      if (cap > PM_MAX_INFLATE / 2)
      {
        inflateEnd(&zs);
        free(buf);
        return 0;                // refuse an absurd expansion ratio
      }
      cap *= 2;
      nb = (unsigned char *)realloc(buf, cap);
      if (nb == NULL) { inflateEnd(&zs); free(buf); return 0; }
      buf = nb;
      zs.next_out = buf + used;
      zs.avail_out = (uInt)(cap - used);
    }
  }

  *outlen = cap - zs.avail_out;
  inflateEnd(&zs);
  *out = buf;
  return 1;
}


static int decompress(int how, const unsigned char *in, size_t inlen,
                      unsigned char **out, size_t *outlen)
{
  if (how == PMTILES_COMPRESS_NONE || how == PMTILES_COMPRESS_UNKNOWN)
  {
    unsigned char *c = (unsigned char *)malloc(inlen ? inlen : 1);

    if (c == NULL) { return 0; }
    memcpy(c, in, inlen);
    *out = c;
    *outlen = inlen;
    return 1;
  }
  if (how == PMTILES_COMPRESS_GZIP)
  {
    return gunzip(in, inlen, out, outlen);
  }
  return 0;                      // brotli and zstd would each be a dependency
}


/* ---- reading ------------------------------------------------------------ */

static unsigned char *read_at(FILE *f, unsigned long long off,
                              unsigned long long len)
{
  unsigned char *b;

  if (len == 0 || len > PM_MAX_INFLATE)
  {
    return NULL;
  }
  b = (unsigned char *)malloc((size_t)len);
  if (b == NULL) { return NULL; }
  if (fseek(f, (long)off, SEEK_SET) != 0
      || fread(b, 1, (size_t)len, f) != (size_t)len)
  {
    free(b);
    return NULL;
  }
  return b;
}


pmtiles *pmtiles_open(const char *path)
{
  unsigned char h[PM_HEADER_LEN];
  pmtiles *p;
  FILE *f;

  if (path == NULL)
  {
    return NULL;
  }
  f = fopen(path, "rb");
  if (f == NULL)
  {
    return NULL;
  }
  if (fread(h, 1, sizeof(h), f) != sizeof(h)
      || memcmp(h, "PMTiles", 7) != 0 || h[7] != 3)
  {
    fclose(f);
    return NULL;                 // not a v3 archive
  }

  p = (pmtiles *)calloc(1, sizeof(*p));
  if (p == NULL) { fclose(f); return NULL; }

  p->f = f;
  p->root_off = le64(h + 8);
  p->root_len = le64(h + 16);
  p->leaf_off = le64(h + 40);
  p->leaf_len = le64(h + 48);
  p->data_off = le64(h + 56);
  p->data_len = le64(h + 64);
  p->internal_compression = h[97];
  p->tile_compression = h[98];
  p->tile_type = h[99];
  p->min_zoom = h[100];
  p->max_zoom = h[101];
  p->min_lon = le32s(h + 102) / 1e7;
  p->min_lat = le32s(h + 106) / 1e7;
  p->max_lon = le32s(h + 110) / 1e7;
  p->max_lat = le32s(h + 114) / 1e7;

  return p;
}


void pmtiles_close(pmtiles *p)
{
  if (p != NULL)
  {
    if (p->f != NULL) { fclose(p->f); }
    free(p);
  }
}


/*
 * Search one directory for a tile id.
 *
 * Returns 1 with the offset and length set when the entry is a tile, 2 when it is a leaf
 * directory to search next, and 0 when the id is not in this directory.
 */
static int dir_find(const unsigned char *d, size_t dlen,
                    unsigned long long want,
                    unsigned long long *off, unsigned long long *len)
{
  vrd r;
  unsigned long long n, i;
  unsigned long long *ids, *runs, *lens, *offs;
  int found = 0;

  r.p = d;
  r.end = d + dlen;
  r.bad = 0;
  n = v_get(&r);
  if (r.bad || n == 0 || n > 100000000ULL)
  {
    return 0;
  }

  ids  = (unsigned long long *)malloc((size_t)n * sizeof(*ids));
  runs = (unsigned long long *)malloc((size_t)n * sizeof(*runs));
  lens = (unsigned long long *)malloc((size_t)n * sizeof(*lens));
  offs = (unsigned long long *)malloc((size_t)n * sizeof(*offs));
  if (ids == NULL || runs == NULL || lens == NULL || offs == NULL)
  {
    free(ids); free(runs); free(lens); free(offs);
    return 0;
  }

  // Tile ids are stored as deltas, so they only make sense accumulated.
  {
    unsigned long long acc = 0;

    for (i = 0; i < n && !r.bad; i++)
    {
      acc += v_get(&r);
      ids[i] = acc;
    }
  }
  for (i = 0; i < n && !r.bad; i++) { runs[i] = v_get(&r); }
  for (i = 0; i < n && !r.bad; i++) { lens[i] = v_get(&r); }

  // An offset of zero means "immediately after the previous entry", which is
  // how a clustered archive avoids storing an offset per tile.
  for (i = 0; i < n && !r.bad; i++)
  {
    unsigned long long v = v_get(&r);

    if (v == 0 && i > 0)
    {
      offs[i] = offs[i - 1] + lens[i - 1];
    }
    else
    {
      offs[i] = v - 1;
    }
  }

  if (!r.bad)
  {
    for (i = 0; i < n; i++)
    {
      // run_length 0 marks a leaf directory covering ids from here onward;
      // otherwise the entry covers run_length consecutive ids.
      if (runs[i] == 0)
      {
        if (want >= ids[i] && (i + 1 >= n || want < ids[i + 1]))
        {
          *off = offs[i];
          *len = lens[i];
          found = 2;
          break;
        }
      }
      else if (want >= ids[i] && want < ids[i] + runs[i])
      {
        *off = offs[i];
        *len = lens[i];
        found = 1;
        break;
      }
    }
  }

  free(ids); free(runs); free(lens); free(offs);
  return found;
}


int pmtiles_get(pmtiles *p, int z, unsigned int x, unsigned int y,
                unsigned char **data, size_t *len)
{
  unsigned long long want;
  unsigned long long off = p ? p->root_off : 0;
  unsigned long long dlen = p ? p->root_len : 0;
  int depth;

  if (p == NULL || data == NULL || len == NULL || z < 0 || z > 30)
  {
    return 0;
  }
  if (z < p->min_zoom || z > p->max_zoom)
  {
    return 0;                    // outside what this archive holds
  }
  want = pmtiles_tile_id(z, x, y);

  // Root, then leaves.  The specification allows one level of leaves; the
  // bound is here so a malformed archive cannot loop forever.
  for (depth = 0; depth < 4; depth++)
  {
    unsigned char *raw = read_at(p->f, off, dlen);
    unsigned char *dir = NULL;
    size_t dirlen = 0;
    unsigned long long e_off = 0, e_len = 0;
    int r;

    if (raw == NULL) { return 0; }
    if (!decompress(p->internal_compression, raw, (size_t)dlen, &dir, &dirlen))
    {
      free(raw);
      return 0;
    }
    free(raw);

    r = dir_find(dir, dirlen, want, &e_off, &e_len);
    free(dir);

    if (r == 0)
    {
      return 0;                  // no such tile; normal for a sparse set
    }
    if (r == 1)
    {
      unsigned char *tile = read_at(p->f, p->data_off + e_off, e_len);

      if (tile == NULL) { return 0; }
      if (!decompress(p->tile_compression, tile, (size_t)e_len, data, len))
      {
        free(tile);
        return 0;
      }
      free(tile);
      return 1;
    }
    // r == 2: descend into the leaf directory
    off = p->leaf_off + e_off;
    dlen = e_len;
  }
  return 0;
}
