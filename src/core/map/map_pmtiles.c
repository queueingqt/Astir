/*
 * Vector tile map driver: a PMTiles archive of Mapbox Vector Tiles.
 *
 * The point of this against the raster tile driver next door is that a raster
 * tile is a photograph of a map -- its road casings and place names were drawn
 * by a server at one size, so scaling it blurs them and no amount of vector
 * work on this side can help.  A vector tile carries the geometry and the tags,
 * and everything is drawn here, at the display's own resolution.
 *
 * It reuses three things rather than inventing them:
 *
 *   mvt.c        decodes a tile into features
 *   pmtiles.c    finds a tile in a single offline archive
 *   dbfawk       decides what each feature looks like
 *
 * That last one is the whole reason this is a few hundred lines rather than a
 * project.  A vector tile client normally needs a MapLibre style document and
 * an interpreter for it; Astir has had an equivalent since long before vector
 * tiles existed, and dbfawk_parse_attrs() points it at tile tags.
 *
 * Rules are matched by LAYER NAME, not by a DBF signature: a tile's layers are
 * named ("transportation", "water", "place") and that name is the stable thing
 * to key on.  A rule file declares dbfinfo="mvt:transportation" to claim one.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core/astir.h"
#include "core/map/maps.h"
#include "core/map/mvt.h"
#include "core/map/pmtiles.h"
#include "core/aprs/alert.h"
#include "core/map/awk.h"
#include "core/map/dbfawk.h"
#include "core/map/map_style.h"
#include "core/util/util.h"
#include "core/util/snprintf.h"
#include "core/util/xa_perf.h"
#include "draw/xa_draw.h"

// A degree in Astir units: hundredths of a second.
#define DEG_TO_ASTIR 360000.0

// Refuse to draw an unreasonable number of tiles in one frame.  A viewport
// covers a handful at a sane zoom; a hundred means the zoom choice is wrong,
// and drawing them anyway would hang rather than look bad.
#define MAX_TILES_PER_FRAME 64


/* ---- Web Mercator ------------------------------------------------------- */

static double tile_x_to_lon(double tx, int z)
{
  return tx / (double)(1 << z) * 360.0 - 180.0;
}


static double tile_y_to_lat(double ty, int z)
{
  double n = M_PI - 2.0 * M_PI * ty / (double)(1 << z);

  return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}


static double lon_to_tile_x(double lon, int z)
{
  return (lon + 180.0) / 360.0 * (double)(1 << z);
}


static double lat_to_tile_y(double lat, int z)
{
  double r = lat * M_PI / 180.0;

  return (1.0 - log(tan(r) + 1.0 / cos(r)) / M_PI) / 2.0 * (double)(1 << z);
}


/*
 * Which zoom to read.
 *
 * Vector tiles do not blur when scaled -- they are redrawn -- so this does not
 * need the round-up the raster driver needs to avoid magnifying a photograph.
 * It wants the level whose feature density suits the view, which is the level
 * whose tiles are about screen-sized.
 */
static int choose_zoom(void)
{
  double deg_across = (double)screen_width * (double)scale_x / DEG_TO_ASTIR;
  double tiles_across;
  int z;

  if (deg_across <= 0.0)
  {
    return 0;
  }
  tiles_across = 360.0 / deg_across;
  z = (int)floor(log(tiles_across) / log(2.0) + 0.5);
  if (z < 0) { z = 0; }
  if (z > 20) { z = 20; }
  return z;
}


/* ---- drawing ------------------------------------------------------------ */

/*
 * One feature: style it, project it, draw it.
 *
 * Projection is two steps because the tile and the screen use different models
 * of the world.  Tile coordinates are Web Mercator within a tile; Astir works
 * in hundredths of a second of latitude and longitude, equirectangular.  So
 * every point goes tile -> lon/lat -> Astir -> pixel, and there is no shortcut
 * that skips the middle.
 */
static void draw_feature(const mvt_feature *f, const mvt_layer *l,
                         int z, unsigned int tx, unsigned int ty,
                         dbfawk_sig_info *sig, dbfawk_field_info *fld,
                         xa_surface_id where)
{
  static xa_point pts[MAX_MAP_POINTS];
  map_style st;
  int p, i;
  double extent = l->extent ? (double)l->extent : 4096.0;

  (void)fld;

  // Style first: a feature culled by its rule costs no projection at all, and
  // at a city zoom most features in a tile are culled.
  if (sig == NULL || sig->prog == NULL)
  {
    return;
  }
  dbfawk_parse_attrs(sig->prog, f->nattrs,
                     (const char *const *)f->keys,
                     (const char *const *)f->values);
  st = map_dbfawk_style();

  if (st.display_level > 0 && scale_y > st.display_level)
  {
    return;                      // too far out for this feature
  }
  if (st.min_display_level > 0 && scale_y < st.min_display_level)
  {
    return;                      // too close in
  }

  xa_pen_color(gc, colors[st.color]);
  xa_pen_line(gc, st.lanes > 0 ? st.lanes : 1,
              st.pattern ? XA_LINE_ON_OFF_DASH : XA_LINE_SOLID,
              XA_CAP_ROUND, XA_JOIN_ROUND);

  for (p = 0; p < f->nparts; p++)
  {
    int first = f->parts[p].first;
    int n = f->parts[p].npts;
    int out = 0;

    if (n < 1)
    {
      continue;
    }
    for (i = 0; i < n && out < MAX_MAP_POINTS; i++)
    {
      double gx = (double)tx + (double)f->pts[first + i].x / extent;
      double gy = (double)ty + (double)f->pts[first + i].y / extent;
      double lon = tile_x_to_lon(gx, z);
      double lat = tile_y_to_lat(gy, z);
      unsigned long ax, ay;

      if (!convert_to_astir_coordinates(&ax, &ay, (float)lon, (float)lat))
      {
        continue;                // off the world; skip the vertex
      }
      pts[out].x = l16(((long)ax - NW_corner_longitude) / scale_x);
      pts[out].y = l16(((long)ay - NW_corner_latitude) / scale_y);
      out++;
    }
    if (out < 2)
    {
      // A point feature, or a line that fell off the world.
      if (out == 1 && f->type == MVT_GEOM_POINT)
      {
        xa_draw_point(where, gc, pts[0].x, pts[0].y);
      }
      continue;
    }

    if (f->type == MVT_GEOM_POLYGON && st.filled)
    {
      xa_pen_color(gc, colors[st.fill_color]);
      xa_fill_polygon(where, gc, pts, out, XA_SHAPE_COMPLEX, XA_COORD_ORIGIN);
      xa_pen_color(gc, colors[st.color]);
    }
    xa_draw_lines(where, gc, pts, out, XA_COORD_ORIGIN);
  }
}


void draw_pmtiles_map(char *dir,
                      char *filenm,
                      alert_entry *alert,
                      u_char alert_color,
                      int destination_pixmap,
                      map_draw_flags *mdf)
{
  char path[MAX_FILENAME];
  pmtiles *pm;
  int z, drawn = 0;
  double tx0, tx1, ty0, ty1;
  long ix, iy;

  (void)alert;
  (void)alert_color;
  (void)mdf;

  astir_snprintf(path, sizeof(path), "%s/%s", dir, filenm);
  pm = pmtiles_open(path);
  if (pm == NULL)
  {
    fprintf(stderr, "draw_pmtiles_map: not a PMTiles v3 archive: %s\n", path);
    return;
  }

  if (pm->tile_type != PMTILES_TYPE_MVT)
  {
    // A raster PMTiles is a real thing and this is not the driver for it.
    fprintf(stderr, "draw_pmtiles_map: %s holds raster tiles, not vector\n",
            filenm);
    pmtiles_close(pm);
    return;
  }

  // Indexing: the header carries the bounds, so this costs one read rather
  // than a scan of the archive.
  if (destination_pixmap == INDEX_CHECK_TIMESTAMPS
      || destination_pixmap == INDEX_NO_TIMESTAMPS)
  {
    index_update_ll(filenm, pm->min_lat, pm->max_lat,
                    pm->min_lon, pm->max_lon, 0);
    pmtiles_close(pm);
    return;
  }

  // The styling engine's symbol table is built once and shared; make sure it
  // exists before any rule runs, in case no shapefile has been drawn yet.
  map_dbfawk_init_symtab();

  z = choose_zoom();
  if (z < pm->min_zoom) { z = pm->min_zoom; }
  if (z > pm->max_zoom) { z = pm->max_zoom; }

  // The tile range covering the viewport, in tile coordinates at that zoom.
  {
    double west = (double)NW_corner_longitude / DEG_TO_ASTIR - 180.0;
    double east = (double)SE_corner_longitude / DEG_TO_ASTIR - 180.0;
    double north = 90.0 - (double)NW_corner_latitude / DEG_TO_ASTIR;
    double south = 90.0 - (double)SE_corner_latitude / DEG_TO_ASTIR;

    if (north > 85.05) { north = 85.05; }     // Mercator cannot go to the pole
    if (south < -85.05) { south = -85.05; }

    tx0 = floor(lon_to_tile_x(west, z));
    tx1 = floor(lon_to_tile_x(east, z));
    ty0 = floor(lat_to_tile_y(north, z));
    ty1 = floor(lat_to_tile_y(south, z));
  }

  for (iy = (long)ty0; iy <= (long)ty1 && drawn < MAX_TILES_PER_FRAME; iy++)
  {
    for (ix = (long)tx0; ix <= (long)tx1 && drawn < MAX_TILES_PER_FRAME; ix++)
    {
      unsigned char *raw = NULL;
      size_t rawlen = 0;
      mvt_tile *t;
      int li;

      if (ix < 0 || iy < 0 || ix >= (1L << z) || iy >= (1L << z))
      {
        continue;
      }
      if (!pmtiles_get(pm, z, (unsigned int)ix, (unsigned int)iy,
                       &raw, &rawlen))
      {
        continue;                // sparse archives are normal
      }
      t = mvt_decode(raw, rawlen);
      free(raw);
      if (t == NULL)
      {
        continue;
      }
      drawn++;

      for (li = 0; li < t->nlayers; li++)
      {
        mvt_layer *l = &t->layers[li];
        dbfawk_sig_info *sig;
        char siglabel[128];
        int fi;

        // Rules are claimed by layer name: dbfinfo="mvt:transportation".
        astir_snprintf(siglabel, sizeof(siglabel), "mvt:%s",
                       l->name ? l->name : "");
        sig = dbfawk_find_sig((dbfawk_sig_info *)map_dbfawk_sigs(),
                              siglabel, filenm);
        if (sig == NULL || sig->prog == NULL)
        {
          continue;              // no rule claims this layer, so skip it
        }
        if (map_dbfawk_compile(sig) < 0)
        {
          fprintf(stderr, "draw_pmtiles_map: cannot compile the rule for "
                  "layer %s\n", l->name ? l->name : "(unnamed)");
          continue;
        }
        awk_exec_begin(sig->prog);

        for (fi = 0; fi < l->nfeatures; fi++)
        {
          draw_feature(&l->features[fi], l, z, (unsigned int)ix,
                       (unsigned int)iy, sig, NULL, pixmap);
        }
      }
      mvt_free(t);
    }
  }

  if (debug_level & 16)
  {
    fprintf(stderr, "draw_pmtiles_map: %s zoom %d, %d tiles drawn\n",
            filenm, z, drawn);
  }
  pmtiles_close(pm);
}
