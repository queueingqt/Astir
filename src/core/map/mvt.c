/*
 * Mapbox Vector Tile decoder.  See mvt.h for why this is hand written.
 *
 * The protobuf wire format needed here is small: a field is a varint tag whose
 * low three bits are the wire type and whose upper bits are the field number,
 * followed by a varint, a 64-bit value, a length-delimited run of bytes, or a
 * 32-bit value.  Nothing in the MVT schema uses groups, and only the geometry
 * and the tag list use packed repeated fields.
 *
 * Every read is bounds checked against the end of the buffer.  Tiles arrive
 * from disk or a network and a malformed one has to be an ordinary failure --
 * the decoder returns NULL and the driver skips the tile.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/map/mvt.h"
#include "core/util/snprintf.h"

/* ---- protobuf wire format ---------------------------------------------- */

#define WIRE_VARINT 0
#define WIRE_64BIT  1
#define WIRE_BYTES  2
#define WIRE_32BIT  5

typedef struct
{
  const unsigned char *p;
  const unsigned char *end;
  int bad;                       // sticky: set once, every later read is a nop
} rd;


static unsigned long long rd_varint(rd *r)
{
  unsigned long long v = 0;
  int shift = 0;

  while (!r->bad && r->p < r->end)
  {
    unsigned char b = *r->p++;

    // Ten groups of seven bits is 70, past the 64 a varint can hold.  A longer
    // one is corrupt rather than merely large.
    if (shift > 63)
    {
      r->bad = 1;
      return 0;
    }
    v |= (unsigned long long)(b & 0x7f) << shift;
    if ((b & 0x80) == 0)
    {
      return v;
    }
    shift += 7;
  }
  r->bad = 1;
  return 0;
}


// Zigzag: MVT encodes geometry deltas as signed values in unsigned varints.
static int rd_zigzag(unsigned long long v)
{
  return (int)((v >> 1) ^ (~(v & 1) + 1));
}


static int rd_bytes(rd *r, const unsigned char **out, size_t *outlen)
{
  unsigned long long n = rd_varint(r);

  if (r->bad || n > (unsigned long long)(r->end - r->p))
  {
    r->bad = 1;
    return 0;
  }
  *out = r->p;
  *outlen = (size_t)n;
  r->p += n;
  return 1;
}


// Step over a field whose contents we do not need.
static void rd_skip(rd *r, int wire)
{
  const unsigned char *b;
  size_t n;

  switch (wire)
  {
    case WIRE_VARINT:
      (void)rd_varint(r);
      break;
    case WIRE_64BIT:
      if (r->end - r->p < 8) { r->bad = 1; } else { r->p += 8; }
      break;
    case WIRE_BYTES:
      (void)rd_bytes(r, &b, &n);
      break;
    case WIRE_32BIT:
      if (r->end - r->p < 4) { r->bad = 1; } else { r->p += 4; }
      break;
    default:
      r->bad = 1;
      break;
  }
}


static char *dup_str(const unsigned char *p, size_t n)
{
  char *s = (char *)malloc(n + 1);

  if (s != NULL)
  {
    memcpy(s, p, n);
    s[n] = '\0';
  }
  return s;
}


/* ---- the MVT schema ----------------------------------------------------- */

/*
 * A value is a one-field message whose field number says its type.  dbfawk
 * matches on text, so each of them is rendered to a string here rather than
 * carried as a union the caller would have to switch on.
 */
static char *decode_value(const unsigned char *p, size_t len)
{
  rd r;
  char buf[64];

  r.p = p;
  r.end = p + len;
  r.bad = 0;

  while (!r.bad && r.p < r.end)
  {
    unsigned long long tag = rd_varint(&r);
    int field = (int)(tag >> 3), wire = (int)(tag & 7);
    const unsigned char *b;
    size_t n;

    switch (field)
    {
      case 1:                    // string
        if (wire != WIRE_BYTES || !rd_bytes(&r, &b, &n)) { r.bad = 1; break; }
        return dup_str(b, n);

      case 2:                    // float
        if (wire != WIRE_32BIT || r.end - r.p < 4) { r.bad = 1; break; }
        {
          float f;
          memcpy(&f, r.p, 4);
          r.p += 4;
          astir_snprintf(buf, sizeof(buf), "%g", (double)f);
          return dup_str((const unsigned char *)buf, strlen(buf));
        }

      case 3:                    // double
        if (wire != WIRE_64BIT || r.end - r.p < 8) { r.bad = 1; break; }
        {
          double d;
          memcpy(&d, r.p, 8);
          r.p += 8;
          astir_snprintf(buf, sizeof(buf), "%g", d);
          return dup_str((const unsigned char *)buf, strlen(buf));
        }

      case 4:                    // int64
      case 5:                    // uint64
        {
          unsigned long long v = rd_varint(&r);
          astir_snprintf(buf, sizeof(buf), "%llu", v);
          return dup_str((const unsigned char *)buf, strlen(buf));
        }

      case 6:                    // sint64, zigzag
        {
          unsigned long long v = rd_varint(&r);
          astir_snprintf(buf, sizeof(buf), "%d", rd_zigzag(v));
          return dup_str((const unsigned char *)buf, strlen(buf));
        }

      case 7:                    // bool
        {
          unsigned long long v = rd_varint(&r);
          return dup_str((const unsigned char *)(v ? "1" : "0"), 1);
        }

      default:
        rd_skip(&r, wire);
        break;
    }
  }
  return NULL;
}


/*
 * Geometry is a packed varint array of commands and coordinates.
 *
 * The low three bits of a command are the operation and the rest is a repeat
 * count: MoveTo(1) starts a new part, LineTo(2) extends it, ClosePath(7) shuts
 * a ring without consuming coordinates.  Coordinates are zigzag deltas from
 * the previous point, starting at the tile origin.
 */
static int decode_geometry(rd *r, size_t len, mvt_feature *f)
{
  const unsigned char *end = r->p + len;
  int x = 0, y = 0;
  int cap_pts = 0, cap_parts = 0;

  while (!r->bad && r->p < end)
  {
    unsigned long long cmdint = rd_varint(r);
    int cmd = (int)(cmdint & 7);
    int count = (int)(cmdint >> 3);
    int i;

    if (cmd == 7)                // ClosePath: no coordinates follow
    {
      continue;
    }
    if (cmd != 1 && cmd != 2)
    {
      r->bad = 1;
      break;
    }

    for (i = 0; i < count && !r->bad && r->p < end; i++)
    {
      x += rd_zigzag(rd_varint(r));
      y += rd_zigzag(rd_varint(r));

      if (f->npts >= cap_pts)
      {
        int want = cap_pts ? cap_pts * 2 : 64;
        mvt_point *np = (mvt_point *)realloc(f->pts, (size_t)want * sizeof(*np));

        if (np == NULL) { r->bad = 1; break; }
        f->pts = np;
        cap_pts = want;
      }

      // A MoveTo begins a part; a LineTo extends the one in progress.  A
      // LineTo with no part open is malformed rather than something to guess
      // at, so it opens one and the tile still draws.
      if (cmd == 1 || f->nparts == 0)
      {
        if (f->nparts >= cap_parts)
        {
          int want = cap_parts ? cap_parts * 2 : 8;
          mvt_part *npart = (mvt_part *)realloc(f->parts,
                                                (size_t)want * sizeof(*npart));

          if (npart == NULL) { r->bad = 1; break; }
          f->parts = npart;
          cap_parts = want;
        }
        f->parts[f->nparts].first = f->npts;
        f->parts[f->nparts].npts = 0;
        f->nparts++;
      }

      f->pts[f->npts].x = x;
      f->pts[f->npts].y = y;
      f->npts++;
      f->parts[f->nparts - 1].npts++;
    }
  }
  r->p = end;
  return !r->bad;
}


static int decode_feature(const unsigned char *p, size_t len, mvt_feature *f,
                          char **keys, int nkeys, char **vals, int nvals)
{
  rd r;
  const unsigned char *tags = NULL;
  size_t tagslen = 0;
  const unsigned char *geom = NULL;
  size_t geomlen = 0;

  memset(f, 0, sizeof(*f));
  r.p = p;
  r.end = p + len;
  r.bad = 0;

  while (!r.bad && r.p < r.end)
  {
    unsigned long long tag = rd_varint(&r);
    int field = (int)(tag >> 3), wire = (int)(tag & 7);

    switch (field)
    {
      case 2:                    // tags: packed pairs of indices
        if (wire != WIRE_BYTES || !rd_bytes(&r, &tags, &tagslen)) { r.bad = 1; }
        break;
      case 3:                    // geometry type
        f->type = (int)rd_varint(&r);
        break;
      case 4:                    // geometry
        if (wire != WIRE_BYTES || !rd_bytes(&r, &geom, &geomlen)) { r.bad = 1; }
        break;
      default:
        rd_skip(&r, wire);
        break;
    }
  }

  if (!r.bad && tags != NULL)
  {
    rd t;
    int cap = 0;

    t.p = tags;
    t.end = tags + tagslen;
    t.bad = 0;
    while (!t.bad && t.p < t.end)
    {
      unsigned long long ki = rd_varint(&t);
      unsigned long long vi = rd_varint(&t);

      if (t.bad || ki >= (unsigned long long)nkeys
          || vi >= (unsigned long long)nvals)
      {
        break;                   // an index outside the tile's own tables
      }
      if (f->nattrs >= cap)
      {
        int want = cap ? cap * 2 : 8;
        char **nk = (char **)realloc(f->keys, (size_t)want * sizeof(char *));
        char **nv = (char **)realloc(f->values, (size_t)want * sizeof(char *));

        if (nk != NULL) { f->keys = nk; }
        if (nv != NULL) { f->values = nv; }
        if (nk == NULL || nv == NULL) { break; }
        cap = want;
      }
      // Borrowed from the layer's tables, not owned: the layer outlives every
      // feature in it, and copying every tag of every feature is the kind of
      // per-feature allocation a map draw cannot afford.
      f->keys[f->nattrs] = keys[ki];
      f->values[f->nattrs] = vals[vi];
      f->nattrs++;
    }
  }

  if (!r.bad && geom != NULL)
  {
    rd g;

    g.p = geom;
    g.end = geom + geomlen;
    g.bad = 0;
    decode_geometry(&g, geomlen, f);
  }
  return !r.bad;
}


static int decode_layer(const unsigned char *p, size_t len, mvt_layer *l)
{
  rd r;
  char **keys = NULL, **vals = NULL;
  int nkeys = 0, nvals = 0, ckeys = 0, cvals = 0;
  const unsigned char **feats = NULL;
  size_t *featlens = NULL;
  int nfeats = 0, cfeats = 0;
  int i, ok = 1;

  memset(l, 0, sizeof(*l));
  l->extent = 4096;              // the specification's default
  r.p = p;
  r.end = p + len;
  r.bad = 0;

  // Two passes in one: collect the key and value tables and remember where the
  // features are.  A feature's tags index those tables, and the specification
  // does not promise the tables come first.
  while (!r.bad && r.p < r.end)
  {
    unsigned long long tag = rd_varint(&r);
    int field = (int)(tag >> 3), wire = (int)(tag & 7);
    const unsigned char *b;
    size_t n;

    switch (field)
    {
      case 1:                    // name
        if (wire != WIRE_BYTES || !rd_bytes(&r, &b, &n)) { r.bad = 1; break; }
        free(l->name);
        l->name = dup_str(b, n);
        break;

      case 2:                    // feature
        if (wire != WIRE_BYTES || !rd_bytes(&r, &b, &n)) { r.bad = 1; break; }
        if (nfeats >= cfeats)
        {
          int want = cfeats ? cfeats * 2 : 64;
          const unsigned char **nf = (const unsigned char **)
                                     realloc(feats, (size_t)want * sizeof(*nf));
          size_t *nl = (size_t *)realloc(featlens, (size_t)want * sizeof(*nl));

          if (nf != NULL) { feats = nf; }
          if (nl != NULL) { featlens = nl; }
          if (nf == NULL || nl == NULL) { r.bad = 1; break; }
          cfeats = want;
        }
        feats[nfeats] = b;
        featlens[nfeats] = n;
        nfeats++;
        break;

      case 3:                    // keys
        if (wire != WIRE_BYTES || !rd_bytes(&r, &b, &n)) { r.bad = 1; break; }
        if (nkeys >= ckeys)
        {
          int want = ckeys ? ckeys * 2 : 32;
          char **nk = (char **)realloc(keys, (size_t)want * sizeof(char *));

          if (nk == NULL) { r.bad = 1; break; }
          keys = nk;
          ckeys = want;
        }
        keys[nkeys++] = dup_str(b, n);
        break;

      case 4:                    // values
        if (wire != WIRE_BYTES || !rd_bytes(&r, &b, &n)) { r.bad = 1; break; }
        if (nvals >= cvals)
        {
          int want = cvals ? cvals * 2 : 32;
          char **nv = (char **)realloc(vals, (size_t)want * sizeof(char *));

          if (nv == NULL) { r.bad = 1; break; }
          vals = nv;
          cvals = want;
        }
        vals[nvals++] = decode_value(b, n);
        break;

      case 5:                    // extent
        l->extent = (unsigned int)rd_varint(&r);
        break;

      default:
        rd_skip(&r, wire);
        break;
    }
  }

  if (!r.bad && nfeats > 0)
  {
    l->features = (mvt_feature *)calloc((size_t)nfeats, sizeof(mvt_feature));
    if (l->features == NULL)
    {
      ok = 0;
    }
    else
    {
      for (i = 0; i < nfeats; i++)
      {
        if (decode_feature(feats[i], featlens[i], &l->features[l->nfeatures],
                           keys, nkeys, vals, nvals))
        {
          l->nfeatures++;
        }
      }
    }
  }

  // The tables belong to the layer: the features borrow into them, so they
  // outlive every feature and are freed with the layer.
  l->extent = l->extent ? l->extent : 4096;
  l->keys_table = keys;
  l->nkeys_table = nkeys;
  l->values_table = vals;
  l->nvalues_table = nvals;

  free(featlens);
  free(feats);
  return ok && !r.bad;
}


mvt_tile *mvt_decode(const unsigned char *data, size_t len)
{
  mvt_tile *t;
  rd r;
  int cap = 0;

  if (data == NULL || len == 0)
  {
    return NULL;
  }
  t = (mvt_tile *)calloc(1, sizeof(mvt_tile));
  if (t == NULL)
  {
    return NULL;
  }
  r.p = data;
  r.end = data + len;
  r.bad = 0;

  while (!r.bad && r.p < r.end)
  {
    unsigned long long tag = rd_varint(&r);
    int field = (int)(tag >> 3), wire = (int)(tag & 7);
    const unsigned char *b;
    size_t n;

    if (field == 3 && wire == WIRE_BYTES)   // layer
    {
      if (!rd_bytes(&r, &b, &n))
      {
        break;
      }
      if (t->nlayers >= cap)
      {
        int want = cap ? cap * 2 : 8;
        mvt_layer *nl = (mvt_layer *)realloc(t->layers,
                                             (size_t)want * sizeof(mvt_layer));

        if (nl == NULL) { break; }
        t->layers = nl;
        cap = want;
      }
      if (decode_layer(b, n, &t->layers[t->nlayers]))
      {
        t->nlayers++;
      }
    }
    else
    {
      rd_skip(&r, wire);
    }
  }

  if (t->nlayers == 0)
  {
    mvt_free(t);
    return NULL;
  }
  return t;
}


void mvt_free(mvt_tile *t)
{
  int i, j, k;

  if (t == NULL)
  {
    return;
  }
  for (i = 0; i < t->nlayers; i++)
  {
    mvt_layer *l = &t->layers[i];

    for (j = 0; j < l->nfeatures; j++)
    {
      free(l->features[j].pts);
      free(l->features[j].parts);
      // keys and values are borrowed from the layer's tables
      free(l->features[j].keys);
      free(l->features[j].values);
    }
    free(l->features);
    for (k = 0; k < l->nkeys_table; k++)   { free(l->keys_table[k]); }
    for (k = 0; k < l->nvalues_table; k++) { free(l->values_table[k]); }
    free(l->keys_table);
    free(l->values_table);
    free(l->name);
  }
  free(t->layers);
  free(t);
}


const char *mvt_attr(const mvt_feature *f, const char *key)
{
  int i;

  if (f == NULL || key == NULL)
  {
    return NULL;
  }
  for (i = 0; i < f->nattrs; i++)
  {
    if (f->keys[i] != NULL && strcmp(f->keys[i], key) == 0)
    {
      return f->values[i];
    }
  }
  return NULL;
}
