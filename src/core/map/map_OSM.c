/*
 *
 *
 * Copyright (C) 2000-2026 The Xastir Group
 *
 * This file was contributed by Jerry Dunmire, KA6HLD.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Look at the README for more information on the program.
 *
 * This file derived from map_tiger.c which has the following copyrights:
 *    Copyright (C) 1999,2000  Frank Giannandrea
 *    Copyright (C) 2000-2026 The Xastir Group
 */


#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif  // HAVE_CONFIG_H

#include "core/util/snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>

#include <dirent.h>
#include <netinet/in.h>
#include <Xm/XmAll.h>

#ifdef HAVE_X11_XPM_H
  #include <X11/xpm.h>
  #ifdef HAVE_LIBXPM // if we have both, prefer the extra library
    #undef HAVE_XM_XPMI_H
  #endif // HAVE_LIBXPM
#endif // HAVE_X11_XPM_H

#ifdef HAVE_XM_XPMI_H
  #include <Xm/XpmI.h>
#endif // HAVE_XM_XPMI_H

#include <X11/Xlib.h>

#include <math.h>

#include "core/astir.h"
#include "core/globals.h"
#include <time.h>
#include "core/map/maps.h"
#include "core/io/fetch_remote.h"
#include "core/util/util.h"
#include "core/main.h"
#include "core/state/xa_config.h"

#include "core/map/map_cache.h"

#include "core/map/tile_mgmnt.h"
#include "core/io/dlm.h"
#include "core/map/map_OSM.h"

#ifdef HAVE_MAGICK
  #if HAVE_SYS_TIME_H
    #include <sys/time.h>
  #endif // HAVE_SYS_TIME_H
  #include <time.h>
  // TVR: "stupid ImageMagick"
  // The problem is that magick/api.h includes Magick's config.h file, and that
  // pulls in all the same autoconf-generated defines that we use.
  // plays those games below, but I don't think in the end that they actually
  // make usable macros with our own data in them.
  // Fortunately, we don't need them, so I'll just undef the ones that are
  // causing problems today.  See main.c for fixes that preserve our values.
  #undef PACKAGE
  #undef VERSION
  /* JMT - stupid ImageMagick */
  #define ASTIR_PACKAGE_BUGREPORT PACKAGE_BUGREPORT
  #undef PACKAGE_BUGREPORT
  #define ASTIR_PACKAGE_NAME PACKAGE_NAME
  #undef PACKAGE_NAME
  #define ASTIR_PACKAGE_STRING PACKAGE_STRING
  #undef PACKAGE_STRING
  #define ASTIR_PACKAGE_TARNAME PACKAGE_TARNAME
  #undef PACKAGE_TARNAME
  #define ASTIR_PACKAGE_VERSION PACKAGE_VERSION
  #undef PACKAGE_VERSION
  #ifdef HAVE_MAGICK
    #ifdef HAVE_MAGICKCORE_MAGICKCORE_H
      #include <MagickCore/MagickCore.h>
    #else
      #ifdef HAVE_MAGICK_API_H
        #include <magick/api.h>
      #endif // HAVE_MAGICK_API_H
    #endif //HAVE_MAGICKCORE_MAGICKCORE_H
  #endif //HAVE_MAGICK
  #undef PACKAGE_BUGREPORT
  #define PACKAGE_BUGREPORT ASTIR_PACKAGE_BUGREPORT
  #undef ASTIR_PACKAGE_BUGREPORT
  #undef PACKAGE_NAME
  #define PACKAGE_NAME ASTIR_PACKAGE_NAME
  #undef ASTIR_PACKAGE_NAME
  #undef PACKAGE_STRING
  #define PACKAGE_STRING ASTIR_PACKAGE_STRING
  #undef ASTIR_PACKAGE_STRING
  #undef PACKAGE_TARNAME
  #define PACKAGE_TARNAME ASTIR_PACKAGE_TARNAME
  #undef ASTIR_PACKAGE_TARNAME
  #undef PACKAGE_VERSION
  #define PACKAGE_VERSION ASTIR_PACKAGE_VERSION
  #undef ASTIR_PACKAGE_VERSION

  // This matte color was chosen emphirically to work well with the
  // contours from topOSM.
  #if (QuantumDepth == 8)
    #define MATTE_RED   (0xa7)
    #define MATTE_GREEN (0xa7)
    #define MATTE_BLUE  (0xa7)
    #define MATTE_OPACITY (0x00)
    #define MATTE_COLOR_STRING "xc:#a7a7a7"
    //#define MATTE_COLOR_STRING "xc:#a7a7a700"
    //#define MATTE_OPACITY (0xff)
    //#define MATTE_COLOR_STRING "xc:#a7a7a7ff"
  #elif (QuantumDepth == 16)
    #define MATTE_RED   (0xa700)
    #define MATTE_GREEN (0xa700)
    #define MATTE_BLUE  (0xa700)
    #define MATTE_OPACITY (0x0000)
    #define MATTE_COLOR_STRING "xc:#a700a700a700"
    //#define MATTE_COLOR_STRING "xc:#a700a700a700ffff"
  #else
    #error "QuantumDepth != 16 or 8"
  #endif // QuantumDepth

#endif // HAVE_MAGICK

#include "draw/xa_draw.h"
#include "core/render/draw_symbols.h"   // draw_nice_string

#include "core/xa_ui.h"

// Must be last include file
#include "core/util/leak_detection.h"

#define astirColorsMatch(p,q) (((p).red == (q).red) && ((p).blue == (q).blue) \
        && ((p).green == (q).green))

// osm_scale_x - map Astir scale_x value to an OSM binned value
//
// Note that the terms 'higher' and 'lower' are confusing because a
// smaller Astir scale number is a larger OSM zoom level. OSM zoom level
// 0 would show the whole world in a 256x256 pixel tile, OSM zoom level
// 18 (the max) would require 2^18 tiles to simple wrap the equator.
//
// On the equator, OSM zoom level 0 equates to ~97 miles/pixel
// and OSM zoom level 18 equates to ~2 ft/pixel
//
// direction = -1, zoom in
// direction = 1, zoom out
// direction = 0, nearst level out from the astir scale
//
#define MAX_OSM_ZOOM_LEVEL 18
#define OSM_ZOOM_LEVELS    (MAX_OSM_ZOOM_LEVEL + 1)

static long osm_scale_x(long astir_scale_x)
{

  long osm_level[OSM_ZOOM_LEVELS] = {1, 2, 4, 8, 15, 31, 62, 124, \
                                     247, 494, 989, 1978, 3955, 7910, 15820, 31641,\
                                     63281, 126563, 253125
                                    };
  long osm_scale_x = osm_level[0];
  int i = 0;

  for (i=1; i <= MAX_OSM_ZOOM_LEVEL; i++)
  {
    if (astir_scale_x > osm_level[i])
    {
      continue;
    }
    else
    {
      if (labs(osm_level[i - 1] - astir_scale_x) < labs(osm_level[i] - astir_scale_x))
      {
        osm_scale_x = osm_level[i - 1];
      }
      else
      {
        osm_scale_x = osm_level[i];
      }
      break;
    }
  }

  if (i > MAX_OSM_ZOOM_LEVEL)
  {
    i = MAX_OSM_ZOOM_LEVEL;
    osm_scale_x = osm_level[i];
  }

  return(osm_scale_x);

} // osm_scale_x()


/*
 * adj_to_OSM_level - adjust scale_x and scale_y to approximate an OSM zoom level
 *
 * The OSM zoom level closest to the scale_x value will be chosen and scale_x is modified.
 * The scale_y value (pointed to by the second argument) is scaled proportionaly. Both
 * values pointed to by the arguments are modified.
 */
void adj_to_OSM_level( long *new_scale_x, long *new_scale_y)
{

  long scale;

  scale = osm_scale_x(*new_scale_x);

  // the y scale must also be adjusted.
  *new_scale_y = (int)(((double)(*new_scale_y) * ((double)scale / (double)(*new_scale_x)) + 0.5));
  *new_scale_x = scale;

  return;

} // adj_to_OSM_level()



/*
 * The OpenStreetMap attribution, as text.
 *
 * This was two PNGs: a 133x31 logo strip and a 180x16 picture of the string
 * "CC-BY-SA 2.0 OpenStreetMap", loaded off disk with GraphicsMagick's
 * ReadImage() on every single frame and composited into the corner.
 *
 * Drawing it as text costs nothing per frame, scales with the display, and is
 * the only version that can be translated.  A picture of a sentence is not a
 * thing a program with a text engine should own.
 *
 * It also fixes what the sentence SAID.  OpenStreetMap relicensed its data
 * from CC-BY-SA 2.0 to the Open Database Licence in September 2012, so the
 * old attribution had been claiming the wrong licence for over a decade, and
 * shipping a Creative Commons badge for data that is not under a Creative
 * Commons licence.  The badge is gone with it; the credit OSM asks for is a
 * textual one naming the contributors.
 */
static void draw_osm_attribution(void)
{
  // Declare the credit; the front end draws it.
  //
  // Drawing it here put it in the map layer, and the map layer is what the
  // render scheduler scales and slides to preview a zoom or a drag -- so the
  // credit zoomed with the map, which is exactly what a credit must not do.
  astir_snprintf(map_attribution, sizeof(map_attribution),
                  "%s", "(c) OpenStreetMap contributors  ODbL");
}

/*
 * osm_zoom_level - translate the longitude scale to the nearest OSM zoom level
 *
 * OSM tile scaling is based on the number of tiles needed to wrap the earth at the equator.
 * A tile is 256x256 pixels.
 */
unsigned int osm_zoom_level(long scale_x)
{
  double circumference = 360.0*3600.0*100.0; // Astir Units = 1/100 second.
  double zf;
  int z;

  zf = (log(circumference / (double)scale_x) / log(2.0)) - 8.0;

  /*
   * Round UP, and account for the display density.
   *
   * This used to round to nearest, which means the chosen zoom can be up to
   * half a level below what the view needs, and the tiles are then MAGNIFIED
   * by as much as 1.41x.  A tile is a photograph of a map: its road casings
   * and place names were rasterised at one size by the tile server, and
   * magnifying them is exactly how crisp text becomes unreadable text.  Half
   * the time the map was being blown up rather than shrunk.
   *
   * Rounding up means the tiles are only ever REDUCED, which resamples
   * cleanly.  It costs more tiles per frame -- one zoom level is four times as
   * many -- which is why it was worth being deliberate about rather than
   * quietly correct.
   *
   * The device scale is the same argument again.  On a 2x display every tile
   * is drawn at twice its pixel size, so the level that was right for the
   * logical size is one too shallow; log2 of the scale is exactly the
   * correction.
   */
  {
    int density = xa_device_scale();

    if (density > 1)
    {
      zf += log((double)density) / log(2.0);
    }
  }
  z = (int)ceil(zf - 0.001);      // the epsilon keeps an exact level exact

  // OSM levels run from 0 to 18. Not all levels are available for all views.
  if (z < 0)
  {
    z = 0;
  }
  if (z > 18)
  {
    z = 18;
  }

  return((unsigned int)z);

} // osm_zoom_level()


static xa_keysym OptimizeKey = 0;
static xa_keysym ReportScaleKey = 0;

void init_OSM_values(void)
{
  OptimizeKey = 0;
  ReportScaleKey = 0;
  return;
}

int OSM_optimize_key(xa_keysym key)
{
  return (key == OptimizeKey ? TRUE : FALSE);
}

void set_OSM_optimize_key(xa_keysym key)
{
  OptimizeKey = key;
  return;
}

int OSM_report_scale_key(xa_keysym key)
{
  return (key == ReportScaleKey ? TRUE : FALSE);
}

void set_OSM_report_scale_key(xa_keysym key)
{
  ReportScaleKey = key;
  return;
}

#ifdef HAVE_MAGICK
static void get_OSM_local_file(char * local_filename, char * fileimg)
{
  time_t query_start_time, query_end_time;

#ifdef USE_MAP_CACHE
  int map_cache_return = 1; // Default = cache miss
  char *cache_file_id;
#endif  // USE_MAP_CACHE

  char temp_file_path[MAX_VALUE];

  if (debug_level & 512)
  {
    query_start_time=time(&query_start_time);
  }


#ifdef USE_MAP_CACHE

  if (!map_cache_fetch_disable)
  {

    // Delete old copy from the cache
    if (map_cache_fetch_disable && fileimg[0] != '\0')
    {
      if (map_cache_del(fileimg))
      {
        if (debug_level & 512)
        {
          fprintf(stderr,"Couldn't delete old map from cache\n");
        }
      }
    }

    set_dangerous("map_OSM: map_cache_get");
    map_cache_return = map_cache_get(fileimg,local_filename);
    clear_dangerous();
  }

  if (debug_level & 512)
  {
    fprintf(stderr,"map_cache_return: <%d> bytes returned: %d\n",
            map_cache_return,
            (int) strlen(local_filename));
  }

  if (map_cache_return != 0 )
  {

    set_dangerous("map_OSM: map_cache_fileid");
    cache_file_id = map_cache_fileid();
    astir_snprintf(local_filename,
                    MAX_FILENAME,           // hardcoded to avoid sizeof()
                    "%s/map_%s.%s",
                    get_user_base_dir("map_cache", temp_file_path, sizeof(temp_file_path)),
                    cache_file_id,
                    "png");
    free(cache_file_id);
    clear_dangerous();

#else   // USE_MAP_CACHE

  astir_snprintf(local_filename,
                  MAX_FILENAME,               // hardcoded to avoid sizeof()
                  "%s/map.%s",
                  get_user_base_dir("tmp", temp_file_path, sizeof(temp_file_path)),
                  "png");

#endif  // USE_MAP_CACHE


    // Erase any previously existing local file by the same name.
    // This avoids the problem of having an old map image here and
    // the code trying to display it when the download fails.

    unlink( local_filename );

    if (fetch_remote_file(fileimg, local_filename))
    {
      // Had trouble getting the file.  Abort.
      return;
    }

    // For debugging the MagickError/MagickWarning segfaults.
    //system("cat /dev/null >/var/tmp/astir_hacker_map.png");


#ifdef USE_MAP_CACHE

    set_dangerous("map_OSM: map_cache_put");
    map_cache_put(fileimg,local_filename);
    clear_dangerous();

  } // end if is cached  DHBROWN

#endif // MAP_CACHE


  if (debug_level & 512)
  {
    fprintf (stderr, "Fetch or query took %d seconds\n",
             (int) (time(&query_end_time) - query_start_time));
  }

  // Set permissions on the file so that any user can overwrite it.
  chmod(local_filename, 0666);

} // end get_OSM_local_file
#endif  //HAVE_MAGICK



#ifdef HAVE_MAGICK
static long astirLat2pixelLat(
  long xlat, int osm_zoom )
{
  double lat;  // in radians
  double projection, y;
  long pixelLat;

  lat = convert_lat_l2r(xlat);

  // astir latitude values can exceed +/- 90.0 degrees because
  // the latitude is the extent of the display window. Limit the
  // OSM latitude to less than +/- 90.0 degrees so that the projection
  // calculation does not blow up or return unreasonable values.
  if (lat > ((89.0/180.0) * M_PI))
  {
    lat = ((89.0/180.0) * M_PI);
  }
  else if (lat < ((-89.0/180.0) * M_PI))
  {
    lat = ((-89.0/180.0) * M_PI);
  }


  projection = log(tan(lat) + (1.0 / cos(lat)));
  y = projection / M_PI;
  y = 1.0 - y;
  pixelLat = (long)((y * (double)(1<<(osm_zoom + 8))) / 2.0);
  return(pixelLat);
} // astirLat2pixelLat()
#endif  // HAVE_MAGICK


#ifdef HAVE_MAGICK
static double pixelLat2Lat(long osm_lat, int osm_zoom)
{
  double lat, projection, y;
  y = (double)osm_lat * 2.0 / (double)(1<<(osm_zoom + 8));
  y = 1.0 - y;
  projection = y * M_PI;
  lat = 2.0 * atan(exp(projection)) - (M_PI / 2.0);
  lat = (lat * 180.0 ) / M_PI;
  return(lat);
} // pixelLat2Lat()
#endif  // HAVE_MAGICK


#ifdef HAVE_MAGICK
static long pixelLat2astirLat(long osm_lat, int osm_zoom)
{
  double lat;
  long astirLat;
  lat = pixelLat2Lat(osm_lat, osm_zoom);
  astirLat = (long)((90.0 - lat) * 3600.0 * 100.0);
  return (astirLat);
} // pixelLat2astirLat()
#endif  // HAVE_MAGICK


#ifdef HAVE_MAGICK
static long astirLon2pixelLon(
  long xlon, int osm_zoom)
{
  double lon;
  long pixelLon;
  lon = xlon / (3600.0 * 100.0);
  lon = lon * (1<<(osm_zoom +8));
  lon = lon / 360.0;
  pixelLon = lon;
  return(pixelLon);
} // astirLon2pixelLon()
#endif  // HAVE_MAGICK


#ifdef HAVE_MAGICK
static double pixelLon2Lon(long osm_lon, int osm_zoom)
{
  double lon;
  lon = osm_lon * 360.0 ;
  lon = lon / (1<<(osm_zoom + 8));
  return(lon);
} // pixelLon2Lon()
#endif  // HAVE_MAGICK


#ifdef HAVE_MAGICK
static long pixelLon2astirLon(long osm_lon, int osm_zoom)
{
  long astirLon;
  astirLon = (long)(pixelLon2Lon(osm_lon, osm_zoom) * 3600.0 * 100.0);
  return(astirLon);
} // pixelLon2astirLon()
#endif  // HAVE_MAGICK


#ifdef HAVE_MAGICK

/**********************************************************
 * draw_image() - copy a image onto the display
 **********************************************************/
static void draw_image(
  Image *image,
  ExceptionInfo *except_ptr,
  unsigned offsetx,
  unsigned offsety)
{
  int l;
  XColor my_colors[256];
  PixelPacket *pixel_pack;
  PixelPacket temp_pack;
  IndexPacket *index_pack;
  unsigned image_row;
  unsigned image_col;
  unsigned scr_x, scr_y;             // screen pixel plot positions

  //if (debug_level & 512)
  //    fprintf(stderr,"Color depth is %i \n", (int)image->depth);

  /*
      if (image->colorspace != RGBColorspace) {
          TransformImageColorspace(image, RGBColorspace);
          //fprintf(stderr,"TBD: I don't think we can deal with colorspace != RGB");
          //return;
      }
  */

  // If were are drawing to a low bpp display (typically < 8bpp)
  // try to reduce the number of colors in an image.
  // This may take some time, so it would be best to do ahead of
  // time if it is a static image.
  if (!xa_color_is_direct() && GetNumberColors(image, NULL, except_ptr) > 128)
  {
    if (image->storage_class == PseudoClass)
    {
      CompressImageColormap(image); // Remove duplicate colors
    }

    // Quantize down to 128 will go here...
  }


#if defined(HAVE_GRAPHICSMAGICK)
  pixel_pack = GetImagePixels(image, 0, 0, image->columns, image->rows);
#else
  pixel_pack = GetAuthenticPixels(image, 0, 0, image->columns, image->rows, except_ptr);
#endif
  if (!pixel_pack)
  {
    fprintf(stderr,"pixel_pack == NULL!!!");
    return;
  }

#if defined(HAVE_GRAPHICSMAGICK)
#if (MagickLibVersion < 0x201702)
  index_pack = GetIndexes(image);
#else
  index_pack = AccessMutableIndexes(image);
#endif
#else
  index_pack = GetAuthenticIndexQueue(image);
#endif
  if (image->storage_class == PseudoClass && !index_pack)
  {
    fprintf(stderr,"PseudoClass && index_pack == NULL!!!");
    return;
  }


  if (image->storage_class == PseudoClass && image->colors <= 256)
  {
    for (l = 0; l < (int)image->colors; l++)
    {
      // Need to check how to do this for ANY image, as
      // ImageMagick can read in all sorts of image files
      temp_pack = image->colormap[l];
      //if (debug_level & 512)
      //    fprintf(stderr,"Colormap color is %i  %i  %i \n",
      //           temp_pack.red, temp_pack.green, temp_pack.blue);

      // Here's a tricky bit:  PixelPacket entries are defined as
      // Quantum's.  Quantum is defined in
      // /usr/include/magick/image.h as either an unsigned short
      // or an unsigned char, depending on what "configure"
      // decided when ImageMagick was installed.  We can determine
      // which by looking at MaxRGB or QuantumDepth.
      //
      if (QuantumDepth == 16)    // Defined in /usr/include/magick/image.h
      {
        if (debug_level & 512)
        {
          fprintf(stderr,"Color quantum is [0..65535]\n");
        }
        my_colors[l].red   = temp_pack.red * raster_map_intensity;
        my_colors[l].green = temp_pack.green * raster_map_intensity;
        my_colors[l].blue  = temp_pack.blue * raster_map_intensity;
      }
      else    // QuantumDepth = 8
      {
        if (debug_level & 512)
        {
          fprintf(stderr,"Color quantum is [0..255]\n");
        }
        my_colors[l].red   = (temp_pack.red * 256) * raster_map_intensity;
        my_colors[l].green = (temp_pack.green * 256) * raster_map_intensity;
        my_colors[l].blue  = (temp_pack.blue * 256) * raster_map_intensity;
      }

      // Get the color allocated on < 8bpp displays. pixel color is written to my_colors.pixel
      (void)xa_color_resolve(&my_colors[l].red, &my_colors[l].green, &my_colors[l].blue,
                             &my_colors[l].pixel);

      //if (debug_level & 512)
      //    fprintf(stderr,"Color allocated is %li  %i  %i  %i \n", my_colors[l].pixel,
      //           my_colors[l].red, my_colors[l].blue, my_colors[l].green);
    }
  }

  // loop over image pixel rows
  for (image_row = 0; image_row < image->rows; image_row++)
  {

    xa_ui_pump_events();
    if (interrupt_drawing_now)
    {
      // Update to screen
      xa_present_full(pixmap);
      return;
    }

    scr_y = image_row + offsety;

    // loop over image pixel columns
    for (image_col = 0; image_col < image->columns; image_col++)
    {
      scr_x = image_col + offsetx;
      // now copy a pixel from the image to the screen
      l = image_col + (image_row * image->columns);
      if (image->storage_class == PseudoClass)
      {
        // Make matte transparent
        if (astirColorsMatch(pixel_pack[l],image->matte_color))
        {
          continue;
        }
        xa_pen_color(gc, my_colors[(int)index_pack[l]].pixel);
      }
      else
      {
        // Skip transparent pixels
        if (pixel_pack[l].opacity == TransparentOpacity)
        {
          continue;
        }

        // It is not safe to assume that the red/green/blue
        // elements of pixel_pack of type Quantum are the
        // same as the red/green/blue of an XColor!
        if (QuantumDepth==16)
        {
          my_colors[0].red=pixel_pack[l].red;
          my_colors[0].green=pixel_pack[l].green;
          my_colors[0].blue=pixel_pack[l].blue;
        }
        else   // QuantumDepth=8
        {
          // shift the bits of the 8-bit quantity so that
          // they become the high bigs of my_colors.*
          my_colors[0].red=pixel_pack[l].red*256;
          my_colors[0].green=pixel_pack[l].green*256;
          my_colors[0].blue=pixel_pack[l].blue*256;
        }
        // NOW my_colors has the right r,g,b range for
        // pack_pixel_bits
        xa_color_pack(my_colors[0].red * raster_map_intensity,
                        my_colors[0].green * raster_map_intensity,
                        my_colors[0].blue * raster_map_intensity, &my_colors[0].pixel);
        xa_pen_color(gc, my_colors[0].pixel);
      }
      // write the pixel from the map image to the
      // screen.
      xa_fill_rect(pixmap, gc, scr_x, scr_y, 1, 1);
    } // loop over map pixel columns
  } // loop over map pixel rows

  return;
}  // end draw_image()


/**********************************************************
 * render_OSM_image_pixels() - internal helper: write one decoded image's
 * pixels into a caller-supplied XImage buffer (no ownership, no XPutImage).
 * Called by both draw_OSM_image() (single image) and draw_OSM_tiles()
 * (shared buffer for all tiles) so that the X server round-trips for
 * XGetImage and XPutImage happen exactly once per redraw, not once per tile.
 **********************************************************/
/*
 * Column-to-screen tables for one image, and how wide an image they cover.
 *
 * A tile is 256 wide; this allows for a single large image as well, and falls
 * back to computing per pixel above the limit rather than allocating.
 */
#define OSM_COL_TAB_MAX 8192
static long osm_col_x[OSM_COL_TAB_MAX];
static long osm_col_dx[OSM_COL_TAB_MAX];
static int  osm_col_valid;

/* Stage timings, reported under ASTIR_PERF; see the driver's summary below. */
double astir_osm_t_capture, astir_osm_t_pixels, astir_osm_t_writeback;
long   astir_osm_n_images;

static double astir_osm_now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static long dirty_x0, dirty_x1, dirty_y0, dirty_y1;

/*
 * Decoded tiles, kept in memory.
 *
 * The tiles on screen were read off disk and PNG-decoded on every frame -- 35
 * of them, and the majority of the tile driver's cost, which is itself most of
 * a warm frame.  The file cache below this only avoids the network; nothing
 * avoided the decode.
 *
 * Keyed by path, because the path already encodes zoom, x, y and the tile set,
 * so two tile sets cannot collide and no separate key needs constructing.
 *
 * Bounded, and evicted oldest-first.  Panning brings new tiles in forever, so
 * an unbounded cache is a leak with a long fuse; 128 tiles is about 32 MB at
 * 256x256 RGBA and comfortably more than one screen holds, so a pan back and
 * forth stays entirely in cache.
 */
#define OSM_TILE_CACHE_MAX 128

typedef struct
{
  char path[MAX_FILENAME];
  Image *image;
  unsigned long used;            /* for oldest-first eviction */
} osm_tile_entry;

static osm_tile_entry osm_tile_cache[OSM_TILE_CACHE_MAX];
static int osm_tile_cache_n;
static unsigned long osm_tile_clock;
long astir_osm_tile_hits, astir_osm_tile_misses;

static Image *osm_tile_cache_get(const char *path)
{
  int i;

  for (i = 0; i < osm_tile_cache_n; i++)
  {
    if (strcmp(osm_tile_cache[i].path, path) == 0)
    {
      osm_tile_cache[i].used = ++osm_tile_clock;
      astir_osm_tile_hits++;
      return osm_tile_cache[i].image;
    }
  }
  astir_osm_tile_misses++;
  return NULL;
}


static void osm_tile_cache_put(const char *path, Image *image)
{
  int slot;

  if (image == NULL)
  {
    return;
  }
  if (osm_tile_cache_n < OSM_TILE_CACHE_MAX)
  {
    slot = osm_tile_cache_n++;
  }
  else
  {
    int i;

    slot = 0;
    for (i = 1; i < osm_tile_cache_n; i++)
    {
      if (osm_tile_cache[i].used < osm_tile_cache[slot].used)
      {
        slot = i;
      }
    }
    DestroyImage(osm_tile_cache[slot].image);
  }
  astir_snprintf(osm_tile_cache[slot].path,
                  sizeof(osm_tile_cache[slot].path), "%s", path);
  osm_tile_cache[slot].image = image;
  osm_tile_cache[slot].used = ++osm_tile_clock;
}


/*
 * Forget a tile.  Called when a file turns out to be corrupt and is unlinked,
 * so a redownload is not shadowed by the bad decode.
 */
static void osm_tile_cache_drop(const char *path)
{
  int i;

  for (i = 0; i < osm_tile_cache_n; i++)
  {
    if (strcmp(osm_tile_cache[i].path, path) == 0)
    {
      DestroyImage(osm_tile_cache[i].image);
      osm_tile_cache[i] = osm_tile_cache[--osm_tile_cache_n];
      return;
    }
  }
}

static void render_OSM_image_pixels(
  Image *image,
  ExceptionInfo *except_ptr,
  tiepoint *tpNW,
  tiepoint *tpSE,
  int osm_zl,
  xa_image ximg)   // buffer to write into; XA_IMAGE_NONE falls back to per-pixel
{
  int l;
  XColor my_colors[256];
  PixelPacket *pixel_pack;
  PixelPacket temp_pack;
  IndexPacket *index_pack;
  long map_image_row;
  long map_image_col;
  long map_x_min, map_x_max;      // map boundaries for in screen part of map
  long map_y_min, map_y_max;
  int map_seen = 0;
  int map_act;
  int map_done;

  long scr_x,  scr_y;             // screen pixel plot positions
  long scr_xp, scr_yp;            // previous screen plot positions
  int  scr_dx, scr_dy;            // increments in screen plot positions
  unsigned long fill_pixel = 0;   // pixel value accumulated before writing

  //if (debug_level & 512)
  //    fprintf(stderr,"Color depth is %i \n", (int)image->depth);

  /*
      if (image->colorspace != RGBColorspace) {
          TransformImageColorspace(image, RGBColorspace);
          //fprintf(stderr,"TBD: I don't think we can deal with colorspace != RGB");
          //return;
      }
  */

  // If were are drawing to a low bpp display (typically < 8bpp)
  // try to reduce the number of colors in an image.
  // This may take some time, so it would be best to do ahead of
  // time if it is a static image.
  if (!xa_color_is_direct() && GetNumberColors(image, NULL, except_ptr) > 128)
  {
    if (image->storage_class == PseudoClass)
    {
      CompressImageColormap(image); // Remove duplicate colors
    }

    // Quantize down to 128 will go here...
  }


#if defined(HAVE_GRAPHICSMAGICK)
  pixel_pack = GetImagePixels(image, 0, 0, image->columns, image->rows);
#else
  pixel_pack = GetAuthenticPixels(image, 0, 0, image->columns, image->rows, except_ptr);
#endif
  if (!pixel_pack)
  {
    fprintf(stderr,"pixel_pack == NULL!!!");
    return;
  }

#if defined(HAVE_GRAPHICSMAGICK)
#if (MagickLibVersion < 0x201702)
  index_pack = GetIndexes(image);
#else
  index_pack = AccessMutableIndexes(image);
#endif
#else
  index_pack = GetAuthenticIndexQueue(image);
#endif
  if (image->storage_class == PseudoClass && !index_pack)
  {
    fprintf(stderr,"PseudoClass && index_pack == NULL!!!");
    return;
  }


  if (image->storage_class == PseudoClass && image->colors <= 256)
  {
    for (l = 0; l < (int)image->colors; l++)
    {
      // Need to check how to do this for ANY image, as
      // ImageMagick can read in all sorts of image files
      temp_pack = image->colormap[l];
      //if (debug_level & 512)
      //    fprintf(stderr,"Colormap color is %i  %i  %i \n",
      //           temp_pack.red, temp_pack.green, temp_pack.blue);

      // Here's a tricky bit:  PixelPacket entries are defined as
      // Quantum's.  Quantum is defined in
      // /usr/include/magick/image.h as either an unsigned short
      // or an unsigned char, depending on what "configure"
      // decided when ImageMagick was installed.  We can determine
      // which by looking at MaxRGB or QuantumDepth.
      //
      if (QuantumDepth == 16)    // Defined in /usr/include/magick/image.h
      {
        //if (debug_level & 512)
        //    fprintf(stderr,"Color quantum is [0..65535]\n");
        my_colors[l].red   = temp_pack.red * raster_map_intensity;
        my_colors[l].green = temp_pack.green * raster_map_intensity;
        my_colors[l].blue  = temp_pack.blue * raster_map_intensity;
      }
      else    // QuantumDepth = 8
      {
        //if (debug_level & 512)
        //    fprintf(stderr,"Color quantum is [0..255]\n");
        my_colors[l].red   = (temp_pack.red * 256) * raster_map_intensity;
        my_colors[l].green = (temp_pack.green * 256) * raster_map_intensity;
        my_colors[l].blue  = (temp_pack.blue * 256) * raster_map_intensity;
      }

      // Get the color allocated on < 8bpp displays. pixel color is written to my_colors.pixel
      (void)xa_color_resolve(&my_colors[l].red, &my_colors[l].green, &my_colors[l].blue,
                             &my_colors[l].pixel);

      //if (debug_level & 512)
      //    fprintf(stderr,"Color allocated is %li  %i  %i  %i \n", my_colors[l].pixel,
      //           my_colors[l].red, my_colors[l].blue, my_colors[l].green);
    }
  }

  /*
  * Here are the corners of our viewport, using the Astir
  * coordinate system.  Notice that Y is upside down:
  *
  *   left edge of view = NW_corner_longitude
  *  right edge of view = SE_corner_longitude
  *    top edge of view = NW_corner_latitude
  * bottom edge of view = SE_corner_latitude
  *
  * The corners of our image were calculated and stored
  * above as tiepoints using OSM units (pixels/circle). They are:
  *
  *   left edge of map = tp[0].x_long
  *  right edge of map = tp[1].x_long
  *    top edge of map = tp[0].y_lat
  * bottom edge of map = tp[1].y_lat
  *
  */

  scr_dx = 1;
  scr_dy = 1;

  // calculate map pixel range in y direction that falls into screen area
  map_y_min = map_y_max = 0l;
  for (map_image_row = 0; map_image_row < (long)image->rows; map_image_row++)
  {
    scr_y = (pixelLat2astirLat(map_image_row + tpNW->y_lat, osm_zl) - NW_corner_latitude) / scale_y;
    if (scr_y > 0)
    {
      if (scr_y < screen_height)
      {
        map_y_max = map_image_row;  // update last map pixel in y
      }
      else
      {
        break;  // done, reached bottom screen border
      }
    }
    else                                // pixel is above screen
    {
      map_y_min = map_image_row;     // update first map pixel in y
    }
  }

  // Calculate the position of the map image relative to the screen
  map_x_min = map_x_max = 0l;
  for (map_image_col = 0; map_image_col < (long)image->columns; map_image_col++)
  {
    scr_x = (pixelLon2astirLon(map_image_col + tpNW->x_long, osm_zl) - NW_corner_longitude) / scale_x;
    if (scr_x > 0)
    {
      if (scr_x < screen_width)
      {
        map_x_max = map_image_col;  // update last map pixel in x
      }
      else
      {
        break;  // done, reached right screen border
      }
    }
    else                                // pixel is left from screen
    {
      map_x_min = map_image_col;              // update first map pixel in x
    }
  }

  scr_yp = -1;
  map_done = 0;
  map_act  = 0;
  map_seen = 0;

  /*
   * The dirty rectangle, in screen pixels, or the whole window.
   *
   * This driver writes into a pixel buffer rather than through a pen, so a
   * clip cannot reach it and it has to do its own.  Without this a partial
   * redraw is not merely slow, it is WRONG: the driver captures the screen,
   * paints tiles across all of it, and writes it back, erasing whatever the
   * surface-reuse blit had just put there.  That showed up as the entire road
   * network vanishing from the reused part of the frame.
   */
  {
    if (xa_dirty_active)
    {
      dirty_x0 = (xa_dirty_left  - NW_corner_longitude) / scale_x;
      dirty_x1 = (xa_dirty_right - NW_corner_longitude) / scale_x;
      dirty_y0 = (xa_dirty_top    - NW_corner_latitude) / scale_y;
      dirty_y1 = (xa_dirty_bottom - NW_corner_latitude) / scale_y;
    }
    else
    {
      dirty_x0 = 0;
      dirty_y0 = 0;
      dirty_x1 = screen_width;
      dirty_y1 = screen_height;
    }
  }

  // loop over map pixel rows
  for (map_image_row = map_y_min; (map_image_row <= map_y_max); map_image_row++)
  {
    /*
     * Service the main loop every 64 rows rather than every row.
     *
     * The pump exists so a slow redraw can be interrupted, and it has to run
     * often enough to stay responsive -- not once per row of every tile, which
     * is around nine thousand main-loop services per frame for a job that
     * needs a few dozen.  The shapefile reader settled on 64 for the same
     * reason and recorded why; this matches it.
     */
    if ((map_image_row & 63) == 0)
    {
      xa_ui_pump_events();
    }
    if (interrupt_drawing_now)
    {
      // Caller owns ximg; we just return without flushing —
      // draw_OSM_image() or draw_OSM_tiles() will handle cleanup.
      return;
    }

    scr_y = (pixelLat2astirLat(map_image_row + tpNW->y_lat, osm_zl)
             - NW_corner_latitude) / scale_y;

    // image rows do not match 1:1 with screen rows due to Mercator
    // scaling, so scr_dy will be passed to XFillRectangle to
    // handle that issue.
    // scr_dy is in rows and must be a minimum of 1 row.
    scr_dy = ((  pixelLat2astirLat(map_image_row + 1 + tpNW->y_lat, osm_zl)
                 - NW_corner_latitude) / scale_y) - scr_y;
    if (scr_dy < 1)
    {
      scr_dy = 1;
    }

    // Rows outside the dirty rectangle are already correct on the surface.
    if (scr_y + scr_dy < dirty_y0 || scr_y > dirty_y1)
    {
      continue;
    }

    if (scr_y != scr_yp)                    // don't do a row twice
    {
      scr_yp = scr_y;                     // remember as previous y
      scr_xp = -1;
      // loop over map pixel columns
      map_act = 0;
      for (map_image_col = map_x_min; map_image_col <= map_x_max; map_image_col++)
      {
        scr_x = (  pixelLon2astirLon(map_image_col + tpNW->x_long, osm_zl)
                   - NW_corner_longitude) / scale_x;
        // handle the case when here the horizontal resolution
        // of the image is less than the horizontal resolution
        // displayed. scr_dx is passed to XFillRectangle() below
        // and must be at least 1 column.
        scr_dx = ( (pixelLon2astirLon(map_image_col + 1 + tpNW->x_long, osm_zl)
                    - NW_corner_longitude) / scale_x) - scr_x;
        if (scr_dx < 1)
        {
          scr_dx = 1;
        }
        if (scr_x + scr_dx < dirty_x0 || scr_x > dirty_x1)
        {
          continue;               // already correct from the previous frame
        }

        if (scr_x != scr_xp)        // don't do a pixel twice
        {
          scr_xp = scr_x;         // remember as previous x

          // check map boundaries in y direction
          if (map_image_row >= 0 && map_image_row <= tpSE->img_y)
          {
            map_seen = 1;
            map_act = 1;   // detects blank screen rows (end of map)

            // now copy a pixel from the map image to the screen
            l = map_image_col + map_image_row * image->columns;
            if (image->storage_class == PseudoClass)
            {
              // Make matte transparent by skipping pixels
              if (astirColorsMatch(pixel_pack[l],image->matte_color))
              {
                continue;
              }
              fill_pixel = my_colors[(int)index_pack[l]].pixel;
            }
            else
            {
              // Skip transparent pixels and make matte
              // colored pixels transparent (by skipping)
              if ((pixel_pack[l].opacity == TransparentOpacity)
                  || (astirColorsMatch(pixel_pack[l], image->matte_color)))
              {
                continue;
              }

              // It is not safe to assume that the red/green/blue
              // elements of pixel_pack of type Quantum are the
              // same as the red/green/blue of an XColor!
              if (QuantumDepth==16)
              {
                my_colors[0].red=pixel_pack[l].red;
                my_colors[0].green=pixel_pack[l].green;
                my_colors[0].blue=pixel_pack[l].blue;
              }
              else   // QuantumDepth=8
              {
                // shift the bits of the 8-bit quantity so that
                // they become the high bigs of my_colors.*
                my_colors[0].red=pixel_pack[l].red*256;
                my_colors[0].green=pixel_pack[l].green*256;
                my_colors[0].blue=pixel_pack[l].blue*256;
              }
              // NOW my_colors has the right r,g,b range for
              // pack_pixel_bits
              xa_color_pack(my_colors[0].red * raster_map_intensity,
                              my_colors[0].green * raster_map_intensity,
                              my_colors[0].blue * raster_map_intensity, &my_colors[0].pixel);
              fill_pixel = my_colors[0].pixel;
            }
            // Write pixel(s) to the screen. Use the bulk pixel buffer when
            // available to avoid per-pixel X11 round-trips (major bottleneck
            // on Raspberry Pi and slow/remote displays).
            if (ximg != XA_IMAGE_NONE)
            {
              /*
               * One call per source pixel, not one per destination pixel.
               *
               * This was a nested loop calling xa_image_put_pixel() for every
               * screen pixel -- a function call and four bounds tests each,
               * around 700000 per frame, and the largest single cost in a warm
               * frame.  xa_image_fill_rect() resolves the bounds once and
               * fills the rows, and it does its own clipping, so the guards
               * that were here per pixel are gone with it.
               */
              xa_image_fill_rect(ximg, (int)scr_x, (int)scr_y,
                                 scr_dx, scr_dy, fill_pixel);
            }
            else
            {
              // Fallback: per-pixel X11 calls (slow but always available)
              xa_pen_color(gc, fill_pixel);
              xa_fill_rect(pixmap, gc, scr_x, scr_y, scr_dx, scr_dy);
            }
          } // check map boundaries in y direction
        }  // don't do a screen pixel twice (in the same row)
      } // loop over map pixel columns

      if (map_seen && !map_act)
      {
        map_done = 1;
      }
      (void)map_done; // map_done is never used, but this takes away the compile warning.
    } // don't do a screen row twice.
  } // loop over map pixel rows
}  // end render_OSM_image_pixels()


/**********************************************************
 * draw_OSM_image() - copy a single decoded image to the display.
 * Allocates an XImage seeded from the current pixmap (so areas outside the
 * image bounds are preserved), writes pixels via render_OSM_image_pixels(),
 * then flushes the whole buffer with a single XPutImage.
 **********************************************************/
static void draw_OSM_image(
  Image *image,
  ExceptionInfo *except_ptr,
  tiepoint *tpNW,
  tiepoint *tpSE,
  int osm_zl)
{
  // Seed the buffer from the current pixmap so missing-tile areas show the
  // previously-rendered content (e.g. lower-zoom fallback tiles).
  xa_image ximg = xa_image_capture(pixmap, 0, 0,
                                   (int)screen_width, (int)screen_height);
  if (ximg == XA_IMAGE_NONE)
  {
    // Fallback: zeroed buffer (better than nothing)
    ximg = xa_image_create((int)screen_width, (int)screen_height);
  }

  render_OSM_image_pixels(image, except_ptr, tpNW, tpSE, osm_zl, ximg);

  if (ximg != XA_IMAGE_NONE)
  {
    xa_image_to_surface(pixmap, gc, ximg, 0, 0, 0, 0,
                        (int)screen_width, (int)screen_height);
    xa_image_destroy(ximg);
  }
}  // end draw_OSM_image()


/**********************************************************
 * draw_OSM_tiles() - retrieve enough map tiles to fill the display
 **********************************************************/
// MaxTextExtent is an ImageMagick/GraphicMagick constant
#define MAX_TMPSTRING MaxTextExtent

void draw_OSM_tiles (char *filenm,           // this is the name of the astir map file
                     int destination_pixmap,
                     char *server_url,      // if specified in astir map file
                     char *tileCacheDir,    // if specified in astir map file
                     char *mapName,         // if specified in astir map file
                     char *tileExt)         // if specified in astir map file
{

  char serverURL[MAX_FILENAME];
  char tileRootDir[MAX_FILENAME];
  char map_it[MAX_FILENAME];
  char short_filenm[MAX_FILENAME];
  int osm_zl;
  tileArea_t tiles;
  coord_t corner;
  tiepoint NWcorner;
  tiepoint SEcorner;
  unsigned long tilex, tiley;
  unsigned long tileCnt = 0;
  unsigned long numTiles;
  int interrupted = 0;

  ExceptionInfo exception;
  Image *tile = NULL;
  ImageInfo *tile_info = NULL;
  unsigned int row, col;
  char tmpString[MAX_TMPSTRING];

  char temp_file_path[MAX_VALUE];

  // Check whether we're indexing or drawing the map
  if ( (destination_pixmap == INDEX_CHECK_TIMESTAMPS)
       || (destination_pixmap == INDEX_NO_TIMESTAMPS) )
  {

    // We're indexing only.  Save the extents in the index.
    // Force the extents to the edges of the earth for the
    // index file.
    index_update_astir(filenm, // Filename only
                        64800000l,      // Bottom
                        0l,             // Top
                        0l,             // Left
                        129600000l,     // Right
                        0);             // Default Map Level

    // Update statusline
    astir_snprintf(map_it,
                    sizeof(map_it),
                    langcode ("BBARSTA039"),  // Indexing %s
                    short_filenm);
    xa_ui_status(map_it);       // Indexing

    return; // Done indexing this file
  }

  if (tileCacheDir[0] != '\0')
  {
    if (tileCacheDir[0] == '/')
    {
      astir_snprintf(tileRootDir, sizeof(tileRootDir),
                      "%s", tileCacheDir);
    }
    else
    {
      astir_snprintf(tileRootDir, sizeof(tileRootDir),
                      "%s", get_user_base_dir(tileCacheDir, temp_file_path, sizeof(temp_file_path)));
    }
  }
  else
  {
    astir_snprintf(tileRootDir, sizeof(tileRootDir),
                    "%s", get_user_base_dir("OSMtiles", temp_file_path, sizeof(temp_file_path)));
  }

  if (mapName[0] != '\0')
  {
    astir_snprintf(tmpString, sizeof(tmpString), "/%s", mapName);
    strncat(tileRootDir, tmpString, sizeof(tileRootDir) - 1 - strlen(tileRootDir));
  }

  if (server_url[0] != '\0')
  {
    astir_snprintf(serverURL, sizeof(serverURL),
                    "%s", server_url);
  }
  else
  {
    astir_snprintf(serverURL, sizeof(serverURL),
                    "%s", "http://tile.openstreetmap.org");
  }

  if (server_url[strlen(serverURL) - 1] == '/')
  {
    serverURL[strlen(serverURL) - 1] = '\0';
  }

  // Create a shorter filename for display
  short_filename_for_status(filenm, short_filenm, sizeof(short_filenm));

  GetExceptionInfo(&exception);

  if (debug_level & 512)
  {
    unsigned long lat, lon;
    (void)convert_to_astir_coordinates(&lon, &lat,
                                        f_NW_corner_longitude, f_NW_corner_latitude);
    fprintf(stderr, "NW_corner_longitude = %f, %ld, %ld\n",
            f_NW_corner_longitude, NW_corner_longitude, lon);
    fprintf(stderr, "NW_corner_latitude = %f, %ld, %ld\n",
            f_NW_corner_latitude, NW_corner_latitude, lat);

    (void)convert_to_astir_coordinates(&lon, &lat,
                                        f_SE_corner_longitude, f_SE_corner_latitude);
    fprintf(stderr, "SE_corner_longitude = %f, %ld, %ld\n",
            f_SE_corner_longitude, SE_corner_longitude, lon);
    fprintf(stderr, "SE_corner_latitude = %f, %ld, %ld\n",
            f_SE_corner_latitude, SE_corner_latitude, lat);
  }

  osm_zl = osm_zoom_level(scale_x);
  calcTileArea(f_NW_corner_longitude, f_NW_corner_latitude,
               f_SE_corner_longitude, f_SE_corner_latitude,
               osm_zl, &tiles);

  astir_snprintf(map_it, sizeof(map_it), "%s",
                  langcode ("BBARSTA050")); // Downloading tiles...
  xa_ui_status(map_it);
  xa_ui_flush();

  // make sure all the map directories exist
  mkOSMmapDirs(tileRootDir, tiles.startx, tiles.endx, osm_zl);

  // Check to see how many tiles need to be downloaded
  // A simple calculation doesn't work well here because some
  // (possibly all) of the tiles may exist in the cache.
  numTiles = tilesMissing(tiles.startx, tiles.endx, tiles.starty,
                          tiles.endy, osm_zl, tileRootDir,
                          tileExt[0] != '\0' ? tileExt : "png");

  // get the tiles
  tileCnt = 1;
  for (tilex = tiles.startx; tilex <= tiles.endx; tilex++)
  {
    for (tiley = tiles.starty; tiley <= tiles.endy; tiley++)
    {
      if ((numTiles > 0) & (tileCnt <= numTiles))
      {
        astir_snprintf(map_it, sizeof(map_it), langcode("BBARSTA051"),
                        tileCnt, numTiles);  // Downloading tile %ls of %ls
        xa_ui_status(map_it);
        xa_ui_flush();
      }

      DLM_queue_tile(serverURL, tilex, tiley,
                     osm_zl, tileRootDir, tileExt[0] != '\0' ? tileExt : "png");

    }
  }

  // if the Download Manager is not using threaded (background) mode,
  // we need this to actually do the downloads.
  // In threaded mode, it does nothing
  DLM_do_transfers();
  if (interrupt_drawing_now)
  {
    interrupted = 1;
  }

  if (interrupted != 1)
  {
    // calculate tie points
    NWcorner.img_x = 0;
    NWcorner.img_y = 0;
    NWcorner.x_long = tiles.startx * 256;
    NWcorner.y_lat = tiles.starty * 256;

    if (debug_level & 512)
    {
      fprintf(stderr, "scale = %ld, zoom = %d\n", scale_x, osm_zl);
      fprintf(stderr, "NW corner:\n");
      fprintf(stderr, "  img_x = %d, img_y = %d\n", NWcorner.img_x, NWcorner.img_y);
      fprintf(stderr, "  x_long = %ld, y_lat = %ld\n", NWcorner.x_long, NWcorner.y_lat);
      fprintf(stderr, "req. lon = %f, lat = %f\n", f_NW_corner_longitude,
              f_NW_corner_latitude);
      tile2coord(tiles.startx, tiles.starty, osm_zl, &corner);
      fprintf(stderr, "ret. lon = %f, lat = %f\n", corner.lon, corner.lat);
      fprintf(stderr, "tile x = %li, y = %li\n", tiles.startx, tiles.starty);
    }

    // The NW corner of the next tile is the SE corner of the last tile
    // we fetched. So add one to the end tile numbers before calculating
    // the coordinates.
    SEcorner.img_x = (256 * ((tiles.endx + 1) - tiles.startx)) - 1;
    SEcorner.img_y = (256 * ((tiles.endy + 1) - tiles.starty)) - 1;
    SEcorner.x_long = (tiles.endx + 1) * 256;
    SEcorner.y_lat = (tiles.endy + 1) * 256;

    if (debug_level & 512)
    {
      fprintf(stderr, "SE corner:\n");
      fprintf(stderr, "  img_x = %d, img_y = %d\n", SEcorner.img_x, SEcorner.img_y);
      fprintf(stderr, "  x_long = %ld, y_lat = %ld\n", SEcorner.x_long, SEcorner.y_lat);
      fprintf(stderr, "req. lon = %f, lat = %f\n", f_SE_corner_longitude,
              f_SE_corner_latitude);
      tile2coord(tiles.endx + 1, tiles.endy + 1, osm_zl, &corner);
      fprintf(stderr, "ret. lon = %f, lat = %f\n", corner.lon, corner.lat);
      fprintf(stderr, "tile x = %li, y = %li\n", tiles.endx, tiles.endy);
    }

    /*
     * Create a canvas upon which the tiles will be composited.
    */
    astir_snprintf(map_it, sizeof(map_it), "%s",
                    langcode ("BBARSTA049")); // Reading tiles...
    xa_ui_status(map_it);
    xa_ui_flush();

    tile_info = CloneImageInfo((ImageInfo *)NULL);

    // Allocate a single shared pixel buffer for all tiles.
    // Seeded from the current pixmap so that areas without a tile (missing or
    // not-yet-downloaded) preserve the previously-rendered content, e.g.
    // lower-zoom fallback tiles — the same behaviour as the original canvas.
    // Each tile's pixels are written into this buffer with render_OSM_image_pixels()
    // and the whole screen is handed to the pixmap in one operation at the end.
    double osm_t0 = astir_osm_now();
    xa_image shared_ximg = xa_image_capture(pixmap, 0, 0,
                                           (int)screen_width,
                                           (int)screen_height);
    astir_osm_t_capture += astir_osm_now() - osm_t0;
    if (shared_ximg == XA_IMAGE_NONE)
    {
      shared_ximg = xa_image_create((int)screen_width, (int)screen_height);
    }

    tile_info = CloneImageInfo((ImageInfo *)NULL);

    // Decode and render each tile directly into the shared XImage.
    // One XGetImage + N render passes + one XPutImage = fast.
    for (col = tiles.starty; col <= tiles.endy; col++)
    {
      for (row = tiles.startx; row <= tiles.endx; row++)
      {
          tiepoint tpTileNW, tpTileSE;

          // Per-tile tiepoints: OSM pixel coordinates of this tile
          tpTileNW.img_x = 0;
          tpTileNW.img_y = 0;
          tpTileNW.x_long = row * 256;
          tpTileNW.y_lat  = col * 256;
          tpTileSE.img_x  = 255;
          tpTileSE.img_y  = 255;
          tpTileSE.x_long = (row + 1) * 256;
          tpTileSE.y_lat  = (col + 1) * 256;

          astir_snprintf(tmpString, sizeof(tmpString),
                          "%s/%d/%u/%u.%s", tileRootDir, osm_zl, row, col,
                          tileExt[0] != '\0' ? tileExt : "png");
          strncpy(tile_info->filename, tmpString, MaxTextExtent);

          tile = osm_tile_cache_get(tmpString);
          if (tile != NULL)
          {
            goto have_tile;      // decoded already; skip the read entirely
          }

          tile = ReadImage(tile_info, &exception);

          if (exception.severity != UndefinedException)
          {
            if (exception.severity == FileOpenError)
            {
#if !defined(HAVE_GRAPHICSMAGICK)
              ClearMagickException(&exception);
#endif
            }
            else
            {
              if (debug_level & 512)
              {
                fprintf(stderr, "%s NOT removed.\n", tmpString);
              }
              else
              {
                fprintf(stderr, "Removing %s\n", tmpString);
                osm_tile_cache_drop(tmpString);
                unlink(tmpString);
              }
              CatchException(&exception);
            }
            GetExceptionInfo(&exception);
          }

          osm_tile_cache_put(tmpString, tile);

have_tile:
          if (tile)
          {
            { double pt0 = astir_osm_now();
            render_OSM_image_pixels(tile, &exception, &tpTileNW, &tpTileSE, osm_zl,
                                    shared_ximg);
              astir_osm_t_pixels += astir_osm_now() - pt0; }
            astir_osm_n_images++;
            // Not destroyed: the cache owns it now and it outlives the frame.
          }
        }
      }

    // Hand all tiles to the pixmap in one bulk transfer
    if (shared_ximg != XA_IMAGE_NONE)
    {
      osm_t0 = astir_osm_now();
      xa_image_to_surface(pixmap, gc, shared_ximg, 0, 0, 0, 0,
                          (int)screen_width, (int)screen_height);
      astir_osm_t_writeback += astir_osm_now() - osm_t0;
      xa_image_destroy(shared_ximg);
    }
    if (getenv("ASTIR_PERF") != NULL)
    {
      fprintf(stderr, "[osm] capture %.1f  pixels %.1f  writeback %.1f ms "
              "across %ld images; tile cache %ld hit %ld miss\n",
              astir_osm_t_capture, astir_osm_t_pixels,
              astir_osm_t_writeback, astir_osm_n_images,
              astir_osm_tile_hits, astir_osm_tile_misses);
      astir_osm_tile_hits = astir_osm_tile_misses = 0;
      astir_osm_t_capture = astir_osm_t_pixels = astir_osm_t_writeback = 0;
      astir_osm_n_images = 0;
    }

    draw_osm_attribution();
  }
  else
  {
    // map draw was interrupted
    // Update to screen
    xa_present_full(pixmap);
  }

  /*
   * Release resources
  */
  if (tile_info != NULL)
  {
    DestroyImageInfo(tile_info);
  }
  DestroyExceptionInfo(&exception);
  return;

} // draw_OSM_tiles()


/**********************************************************
 * draw_OSM_map() - retrieve an image that is the size of the display
 **********************************************************/
void draw_OSM_map (char *filenm,
                   int destination_pixmap,
                   char *url,
                   char *style,
                   int UNUSED(nocache) )       // For future implementation of a "refresh cached map" option
{
  char file[MAX_FILENAME];        // Complete path/name of image file
  char short_filenm[MAX_FILENAME];
  FILE *f;                        // Filehandle of image file
  char fileimg[MAX_FILENAME];     // Ascii name of image file, read from GEO file
  char OSMtmp[MAX_FILENAME*2];    // Used for putting together the OSMmap query
  tiepoint tp[2];                 // Calibration points for map

  char local_filename[MAX_FILENAME];

  ExceptionInfo exception;
  Image *image;
  ImageInfo *image_info;
  double left, right, top, bottom;
  double lat_center  = 0;
  double long_center = 0;

  char map_it[MAX_FILENAME];
  char tmpstr[1001];
  int osm_zl = 18;                 // OSM zoom level, at 18, the whole
  // world fits in one 256x256 tile.
  unsigned map_image_width;        // Image width
  unsigned map_image_height;       // Image height
  // TODO: put the max_image_* limits in the .geo/.osm file because it could change on a by-server
  //       basis and the server URL can be specified in the .geo/.osm file.
  unsigned max_image_width = 2000;  // This value is for the default server
  unsigned max_image_height = 2000; // This value is for the default server

  // initialize this
  local_filename[0]='\0';

  // Create a shorter filename for display
  short_filename_for_status(filenm, short_filenm, sizeof(short_filenm));

  astir_snprintf(map_it,
                  sizeof(map_it),
                  langcode ("BBARSTA028"),
                  short_filenm);
  xa_ui_status(map_it);       // Loading ...
  xa_ui_flush();

  // Check whether we're indexing or drawing the map
  if ( (destination_pixmap == INDEX_CHECK_TIMESTAMPS)
       || (destination_pixmap == INDEX_NO_TIMESTAMPS) )
  {

    // We're indexing only.  Save the extents in the index.
    // Force the extents to the edges of the earth for the
    // index file.
    index_update_astir(filenm, // Filename only
                        64800000l,      // Bottom
                        0l,             // Top
                        0l,             // Left
                        129600000l,     // Right
                        0);             // Default Map Level

    // Update statusline
    astir_snprintf(map_it,
                    sizeof(map_it),
                    langcode ("BBARSTA039"),
                    short_filenm);
    xa_ui_status(map_it);       // Loading/Indexing ...

    return; // Done indexing this file
  }

  // calculate the OSM zoom level (osm_zl) that is nearest the astir scale
  osm_zl = osm_zoom_level(scale_x);

  // Calculate the image size to request. The size will be saved as tiepoints
  // for the top-left and bottom-right of the image.
  tp[0].x_long = astirLon2pixelLon(NW_corner_longitude, osm_zl); // OSM pixels
  tp[1].x_long = astirLon2pixelLon(SE_corner_longitude, osm_zl); // OSM pixels
  tp[0].y_lat = astirLat2pixelLat(NW_corner_latitude, osm_zl);  // OSM pixels
  tp[1].y_lat = astirLat2pixelLat(SE_corner_latitude, osm_zl);  // OSM pixels

  map_image_height = tp[1].y_lat - tp[0].y_lat;
  map_image_width = tp[1].x_long - tp[0].x_long;

  // Limit dimensions to the max the server will allow.
  if (map_image_width > max_image_width)
  {
    int tmp = ((map_image_width - map_image_height) / 2) + 1;
    tp[0].x_long += tmp;
    tp[1].x_long -= tmp;
    map_image_width = tp[1].x_long - tp[0].x_long;
  }

  if (map_image_height > max_image_height)
  {
    int tmp = ((map_image_height - max_image_height) / 2) + 1;
    tp[0].y_lat += tmp;
    tp[1].y_lat -= tmp;
    map_image_height = tp[1].y_lat - tp[0].y_lat;
  }

  // Size and coordinates for the tiepoints in pixels
  tp[0].img_x = 0;
  tp[0].img_y = 0;
  tp[1].img_x = map_image_width - 1;
  tp[1].img_y = map_image_height - 1;

  // calculate the center coordinates for the image request
  left = (double)((NW_corner_longitude - 64800000l )/360000.0);   // Lat/long Coordinates, degrees
  top = (double)(-((NW_corner_latitude - 32400000l )/360000.0));  // Lat/long Coordinates, degrees
  right = (double)((SE_corner_longitude - 64800000l)/360000.0);   //Lat/long Coordinates, degrees
  bottom = (double)(-((SE_corner_latitude - 32400000l)/360000.0));//Lat/long Coordinates, degrees

  long_center = (left + right)/2.0l; // degrees

  // The vertical center of the image must be calculated from the OSM image size to
  // compensate for latitude scaling (Mercator). This is particularly important for small image/screen
  // sizes and may not be apparent on large displays.
  lat_center = pixelLat2Lat((map_image_height / 2) + tp[0].y_lat, osm_zl);

  /*
   * Query format to the StaticMap
   * See: http://ojw.dev.openstreetmap.org/StaticMap/?mode=API&
   *
   * http://ojw.dev.openstreetmap.org/StaticMap/?lat=LL.LLLLLL&lon=-LLL.LLLLL&z=15& \
   *     w=WWW&h=HHH&layer=osmarender&mode=Export&att=none&show=1
   */

  if (url[0] != '\0')
  {
    astir_snprintf(OSMtmp, sizeof(OSMtmp), "%s", url);
  }
  else
  {
    astir_snprintf(OSMtmp, sizeof(OSMtmp), "http://ojw.dev.openstreetmap.org/StaticMap/");
  }
  //astir_snprintf(tmpstr, sizeof(tmpstr), "?mode=Export&att=text&show=1&");
  astir_snprintf(tmpstr, sizeof(tmpstr), "?mode=Export&show=1&");
  strncat (OSMtmp, tmpstr, sizeof(OSMtmp) - 1 - strlen(OSMtmp));

  if (style[0] != '\0')
  {
    astir_snprintf(tmpstr, sizeof(tmpstr), "%s", style);
    strncat (OSMtmp, tmpstr, sizeof(OSMtmp) - 1 - strlen(OSMtmp));
  }
  else
  {
    astir_snprintf(tmpstr, sizeof(tmpstr), "layer=osmarender&");
    strncat (OSMtmp, tmpstr, sizeof(OSMtmp) - 1 - strlen(OSMtmp));
  }

  astir_snprintf(tmpstr, sizeof(tmpstr), "&lat=%f\046lon=%f\046", lat_center, long_center);
  strncat (OSMtmp, tmpstr, sizeof(OSMtmp) - 1 - strlen(OSMtmp));

  astir_snprintf(tmpstr, sizeof(tmpstr), "w=%i\046h=%i\046", map_image_width, map_image_height);
  strncat (OSMtmp, tmpstr, sizeof(OSMtmp) - 1 - strlen(OSMtmp));

  astir_snprintf(tmpstr, sizeof(tmpstr), "z=%d", osm_zl);
  strncat (OSMtmp, tmpstr, sizeof(OSMtmp) - 1 - strlen(OSMtmp));

  memcpy(fileimg, OSMtmp, sizeof(fileimg));
  fileimg[sizeof(fileimg)-1] = '\0';  // Terminate string

  if (debug_level & 512)
  {
    fprintf(stderr,"left side is %f\n", left);
    fprintf(stderr,"right side is %f\n", right);
    fprintf(stderr,"top  is %f\n", top);
    fprintf(stderr,"bottom is %f\n", bottom);
    fprintf(stderr,"lat center is %f\n", lat_center);
    fprintf(stderr,"long center is %f\n", long_center);
    fprintf(stderr,"screen width is %li\n", screen_width);
    fprintf(stderr,"screen height is %li\n", screen_height);
    fprintf(stderr,"OSM image width is %i\n", map_image_width);
    fprintf(stderr,"OSM image height is %i\n", map_image_height);
    fprintf(stderr,"scale_y = %li\n", scale_y);
    fprintf(stderr,"scale_x = %li\n", scale_x);
    fprintf(stderr,"OSM zoom level = %i\n", osm_zl);
    fprintf(stderr,"fileimg is %s\n", fileimg);
    fprintf(stderr,"ftp or http file: %s\n", fileimg);
  }

  xa_ui_pump_events();
  if (interrupt_drawing_now)
  {
    // Update to screen
    xa_present_full(pixmap);
    return;
  }

  get_OSM_local_file(local_filename,fileimg);

  // Tell ImageMagick where to find it
  astir_snprintf(file,
                  sizeof(file),
                  "%s",
                  local_filename);

  GetExceptionInfo(&exception);

  image_info=CloneImageInfo((ImageInfo *) NULL);

  astir_snprintf(image_info->filename,
                  sizeof(image_info->filename),
                  "%s",
                  file);

  if (debug_level & 512)
  {
    fprintf(stderr,"Copied %s into image info.\n", file);
    fprintf(stderr,"image_info got: %s\n", image_info->filename);
    fprintf(stderr,"Entered ImageMagick code.\n");
    fprintf(stderr,"Attempting to open: %s\n", image_info->filename);
  }

  // We do a test read first to see if the file exists, so we
  // don't kill Astir in the ReadImage routine.
  f = fopen (image_info->filename, "r");
  if (f == NULL)
  {
    if (debug_level & 512)
    {
      fprintf(stderr,"File could not be read\n");
    }

#ifdef USE_MAP_CACHE

    // clear from cache if bad
    if (map_cache_del(fileimg))
    {
      if (debug_level & 512)
      {
        fprintf(stderr,"Couldn't delete unreadable map from cache\n");
      }
    }
#endif

    if (image_info)
    {
      DestroyImageInfo(image_info);
    }
    DestroyExceptionInfo(&exception);
    return;
  }
  (void)fclose (f);


  image = ReadImage(image_info, &exception);

  if (image == (Image *) NULL)
  {
    MagickWarning(exception.severity, exception.reason, exception.description);
    //fprintf(stderr,"MagickWarning\n");

#ifdef USE_MAP_CACHE
    // clear from cache if bad
    if (map_cache_del(fileimg))
    {
      if (debug_level & 512)
      {
        fprintf(stderr,"Couldn't delete map from cache\n");
      }
    }
#endif

    if (image_info)
    {
      DestroyImageInfo(image_info);
    }
    DestroyExceptionInfo(&exception);
    return;

  }
  else if ( (image->columns != map_image_width)
            || (image->rows != map_image_height))
  {
    fprintf(stderr, "Server returned an image size different than requested!\n");

#ifdef USE_MAP_CACHE
    // clear from cache if bad
    if (map_cache_del(fileimg))
    {
      if (debug_level & 512)
      {
        fprintf(stderr,"Couldn't delete map from cache\n");
      }
    }
#endif
    if (image)
    {
      DestroyImage(image);
    }
    if (image_info)
    {
      DestroyImageInfo(image_info);
    }
    DestroyExceptionInfo(&exception);
    return;
  }

  if (debug_level & 512)
  {
    fprintf(stderr,"Image: %s\n", file);
    fprintf(stderr,"Image size %d %d\n", map_image_width, map_image_height);
    fprintf(stderr,"Unique colors = %ld\n", GetNumberColors(image, NULL, &exception));
    fprintf(stderr,"image matte is %i\n", image->matte);
  } // debug_level & 512

  draw_OSM_image(image, &exception, &(tp[0]), &(tp[1]), osm_zl);
  DestroyImage(image);

  draw_osm_attribution();


  // Clean up
  if (image)
  {
    DestroyImage(image);
  }
  if (image_info)
  {
    DestroyImageInfo(image_info);
  }
  DestroyExceptionInfo(&exception);

}  // end draw_OSM_map()


#endif //HAVE_MAGICK
///////////////////////////////////////////// End of OpenStreetMap code ///////////////////////////////////////



