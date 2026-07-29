/*
 *
 * XASTIR, Amateur Station Tracking and Information Reporting
 * Copyright (C) 1999,2000  Frank Giannandrea
 * Copyright (C) 2000-2026 The Xastir Group
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
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif  // HAVE_CONFIG_H

#ifdef HAVE_LIBSHP

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#if HAVE_SYS_TIME_H
  #include <sys/time.h>
#endif // HAVE_SYS_TIME_H
#include <time.h>

#ifdef HAVE_SHAPEFIL_H
  #include <shapefil.h>
#else
  #ifdef HAVE_LIBSHP_SHAPEFIL_H
    #include <libshp/shapefil.h>
  #else
    #error HAVE_LIBSHP defined but no corresponding include defined
  #endif  // HAVE_LIBSHP_SHAPEFIL_H
#endif  // HAVE_SHAPEFIL_H


#include <rtree/index.h>

#include "xastir.h"
#include "globals.h"
#include "util.h"
#include "hashtable.h"
#include "hashtable_itr.h"
/// THIS ONLY FOR DEBUGGING!
//#include "hashtable_private.h"
#include "shp_hash.h"
#include "xa_perf.h"
#include "snprintf.h"

// Must be last include file
#include "leak_detection.h"



#define PURGE_PERIOD 3600     // One hour, hard coded for now.
// This should be in a slider in the timing
// configuration instead.

//#define PURGE_PERIOD 120  //  debugging

static struct hashtable *shp_hash=NULL;
static time_t purge_time;

#define SHP_HASH_SIZE 65535


unsigned int shape_hash_from_key(void *key)
{
  char *str=(char *)key;
  unsigned int shphash=5381;
  int c;
  int i=0;
  while (str[i]!='\0')
  {
    c=str[i++];
    shphash = ((shphash << 5) + shphash)^c;
  }

  return (shphash);
}





int shape_keys_equal(void *key1, void *key2)
{

  if (strncmp((char *)key1,(char *)key2,strlen((char *)key1))==0)
  {
    return(1);
  }
  else
  {
    return(0);
  }
}





void init_shp_hash(int clobber)
{
  // make sure we don't leak
  if (shp_hash)
  {
    if (clobber)
    {
      hashtable_destroy(shp_hash, 1);
      shp_hash=create_hashtable(SHP_HASH_SIZE,
                                shape_hash_from_key,
                                shape_keys_equal);
    }
  }
  else
  {
    shp_hash=create_hashtable(SHP_HASH_SIZE,
                              shape_hash_from_key,
                              shape_keys_equal);
  }

  // Now set the static timer value to the next time we need to run the purge
  // routine
  purge_time = sec_now() + PURGE_PERIOD;
}





// destructor for a single shapeinfo structure
void destroy_shpinfo(shpinfo *si)
{
  if (si)
  {
    empty_shpinfo(si);
    free(si);
  }
}





// free the pointers in a shapinfo object
void empty_shpinfo(shpinfo *si)
{
  if (si)
  {
    if (si->root)
    {
      Xastir_RTreeDestroyNode(si->root);
      si->root=NULL;
    }

    // The hashtable functions free the
    // key, which is in our case the filename.  So since we're only going
    // to empty the shpinfo when we're removing from the hashtable, we
    // must not free the filename in si->filename ourselves.
  }
}





void destroy_shp_hash(void)
{
  struct hashtable_itr *iterator=NULL;
  shpinfo *si;
  int ret;

  if (shp_hash)
  {
    // walk through the hashtable, free any pointers in the values
    // that aren't null, or we'll leak like a sieve.

    // the hashtable functions always attempt to dereference iterator,
    // and don't check if you give it a null, but will return null if
    // there's nothing in the table.  Grrrr.
    iterator=hashtable_iterator(shp_hash);
    do
    {
      ret=0;
      if (iterator)
      {
        si = hashtable_iterator_value(iterator);
        if (si)
        {
          empty_shpinfo(si);
        }
        ret=hashtable_iterator_advance(iterator);
      }
    }
    while (ret);
    hashtable_destroy(shp_hash, 1);  // destroy the hashtable, freeing
    // what's left of the entries
    shp_hash=NULL;
    if (iterator)
    {
      free(iterator);
    }
  }
}





void add_shp_to_hash(char *filename, SHPHandle sHP)
{

  // This function does NOT check whether there already is something in
  // the hashtable that matches.
  // Check that before calling this routine.

  shpinfo *temp;
  int filenm_len;

  filenm_len=strlen(filename);
  if (!shp_hash)    // no table to add to
  {
    init_shp_hash(1); // so create one
  }
  temp = (shpinfo *)malloc(sizeof(shpinfo));
  CHECKMALLOC(temp);
  // leave room for terminator
  temp->filename = (char *) malloc(sizeof(char)*(filenm_len+1));
  CHECKMALLOC(temp->filename);

  strncpy(temp->filename,filename,filenm_len+1);
  temp->filename[filenm_len]='\0';  // just to be safe
//    xastir_snprintf(temp->filename,sizeof(shpinfo),"%s",filename);

  temp->root = Xastir_RTreeNewIndex();
  temp->creation = sec_now();
  temp->last_access = temp->creation;

  build_rtree(&(temp->root),sHP,filename);

  if (!hashtable_insert(shp_hash,temp->filename,temp))
  {
    fprintf(stderr,"Insert failed on shapefile hash --- fatal\n");
    free(temp->filename);
    free(temp);
    exit(1);
  }
}





shpinfo *get_shp_from_hash(char *filename)
{
  shpinfo *result;
  if (!shp_hash)    // no table to search
  {
    init_shp_hash(1); // so create one
    return NULL;
  }

  result=hashtable_search(shp_hash,filename);

  // If there is one, we have now accessed it, so bump the last access time
  if (result)
  {
    result->last_access = sec_now();
  }

  return (result);

}





//CAREFUL:  note how adding things to the tree can change the root
// Must not ever use cache a value of the root pointer if there's any
// chance that the tree needs to be expanded!

// Read one record's bounding box straight out of the .shp file.
//
// Building the index only needs each shape's extent, but SHPReadObject()
// parses and allocates the full geometry to provide it.  Measured 2026-07-28:
// index construction was 4419 ms of a 5794 ms frame with a warm cache, and far
// worse cold.  A shapefile record is
//
//     8 bytes  record header (number, content length -- big endian)
//     4 bytes  shape type (little endian)
//    32 bytes  Xmin, Ymin, Xmax, Ymax (little endian doubles) -- arc/polygon
//
// so the extent costs 36 bytes per record instead of the whole geometry.
// Point records carry a single x,y pair instead of a box.
//
// Returns 1 on success.  Any inconsistency returns 0 and the caller falls back
// to the original SHPReadObject() loop.
static int xa_read_record_bbox(FILE *f, unsigned int byte_offset,
                               int expect_recnum, double *bnds)
{
  unsigned char hdr[8];
  int32_t stype;
  int recnum;

  if (fseek(f, (long)byte_offset, SEEK_SET) != 0)
  {
    return 0;
  }
  if (fread(hdr, 1, 8, f) != 8)
  {
    return 0;
  }
  // Record header is big endian; content that follows is little endian.
  recnum = (hdr[0] << 24) | (hdr[1] << 16) | (hdr[2] << 8) | hdr[3];
  if (recnum != expect_recnum)
  {
    return 0;   // offsets are not what we think they are; caller falls back
  }
  if (fread(&stype, 4, 1, f) != 1)
  {
    return 0;
  }

  switch (stype)
  {
    case SHPT_NULL:
      return 0;

    case SHPT_POINT:
    case SHPT_POINTZ:
    case SHPT_POINTM:
    {
      double xy[2];
      if (fread(xy, 8, 2, f) != 2)
      {
        return 0;
      }
      bnds[0] = bnds[2] = xy[0];
      bnds[1] = bnds[3] = xy[1];
      return 1;
    }

    default:
    {
      double b[4];   // Xmin, Ymin, Xmax, Ymax
      if (fread(b, 8, 4, f) != 4)
      {
        return 0;
      }
      bnds[0] = b[0];
      bnds[1] = b[1];
      bnds[2] = b[2];
      bnds[3] = b[3];
      return 1;
    }
  }
}


// Load record byte offsets from the .shx companion file.  Offsets there are
// big-endian and expressed in 16-bit words, so they are doubled.  Reading the
// index ourselves avoids depending on shapelib's internal representation,
// which is what the first attempt at this got wrong.
static unsigned int *xa_load_shx_offsets(const char *shp_path, int nEntities)
{
  char *shx_path;
  FILE *fx;
  unsigned char *raw;
  unsigned int *offs;
  size_t len, i;

  len = strlen(shp_path);
  if (len < 4)
  {
    return NULL;
  }
  shx_path = (char *)malloc(len + 1);
  if (shx_path == NULL)
  {
    return NULL;
  }
  memcpy(shx_path, shp_path, len + 1);
  // preserve the case convention already used by the filename
  if (shx_path[len-1] == 'P')
  {
    shx_path[len-3] = 'S';
    shx_path[len-2] = 'H';
    shx_path[len-1] = 'X';
  }
  else
  {
    shx_path[len-3] = 's';
    shx_path[len-2] = 'h';
    shx_path[len-1] = 'x';
  }

  fx = fopen(shx_path, "rb");
  free(shx_path);
  if (fx == NULL)
  {
    return NULL;
  }
  if (fseek(fx, 100, SEEK_SET) != 0)
  {
    fclose(fx);
    return NULL;
  }
  raw = (unsigned char *)malloc((size_t)nEntities * 8);
  offs = (unsigned int *)malloc((size_t)nEntities * sizeof(unsigned int));
  if (raw == NULL || offs == NULL)
  {
    free(raw);
    free(offs);
    fclose(fx);
    return NULL;
  }
  if (fread(raw, 8, (size_t)nEntities, fx) != (size_t)nEntities)
  {
    free(raw);
    free(offs);
    fclose(fx);
    return NULL;
  }
  fclose(fx);

  for (i = 0; i < (size_t)nEntities; i++)
  {
    unsigned int w = ((unsigned int)raw[i*8]   << 24)
                     | ((unsigned int)raw[i*8+1] << 16)
                     | ((unsigned int)raw[i*8+2] << 8)
                     |  (unsigned int)raw[i*8+3];
    offs[i] = w * 2;   // words -> bytes
  }
  free(raw);
  return offs;
}


void build_rtree (struct Node **root, SHPHandle sHP, const char *shp_path)
{
  xa_perf_begin(XA_ZONE_RTREE_BUILD);
  int nEntities;
  intptr_t i;
  SHPObject    *psCShape;
  struct Rect bbox_shape;
  FILE *fbox = NULL;
  unsigned int *shx_offs = NULL;
  int use_fast = 0;
  SHPGetInfo(sHP, &nEntities, NULL, NULL, NULL);

  // Prefer reading extents straight out of the .shp records.  Verify that
  // assumption on the first few records; if the layout is not what we expect,
  // fall back to SHPReadObject() rather than build a wrong index.
  if (shp_path != NULL && nEntities > 0
      && (shx_offs = xa_load_shx_offsets(shp_path, nEntities)) != NULL)
  {
    fbox = fopen(shp_path, "rb");
    if (fbox != NULL)
    {
      int checked;
      use_fast = 1;
      for (checked = 0; checked < 3 && checked < nEntities; checked++)
      {
        double b[4];
        if (!xa_read_record_bbox(fbox, shx_offs[checked], checked + 1, b))
        {
          use_fast = 0;
          break;
        }
        psCShape = SHPReadObject(sHP, checked);
        if (psCShape == NULL)
        {
          use_fast = 0;
          break;
        }
        if (fabs(b[0] - psCShape->dfXMin) > 1e-9
            || fabs(b[1] - psCShape->dfYMin) > 1e-9
            || fabs(b[2] - psCShape->dfXMax) > 1e-9
            || fabs(b[3] - psCShape->dfYMax) > 1e-9)
        {
          use_fast = 0;
        }
        SHPDestroyObject(psCShape);
        if (!use_fast)
        {
          break;
        }
      }
      if (!use_fast)
      {
        fclose(fbox);
        fbox = NULL;
      }
    }
  }

  for( i = 0; i < nEntities; i++ )
  {
    double bb[4];
    int have_bbox = 0;

    if (use_fast)
    {
      have_bbox = xa_read_record_bbox(fbox, shx_offs[i], (int)i + 1, bb);
    }

    if (have_bbox)
    {
      bbox_shape.boundary[0]=(RectReal) bb[0];
      bbox_shape.boundary[1]=(RectReal) bb[1];
      bbox_shape.boundary[2]=(RectReal) bb[2];
      bbox_shape.boundary[3]=(RectReal) bb[3];
    }
    else
    {
      psCShape = SHPReadObject ( sHP, i );
      if (psCShape == NULL)
      {
        continue;
      }
      bbox_shape.boundary[0]=(RectReal) psCShape->dfXMin;
      bbox_shape.boundary[1]=(RectReal) psCShape->dfYMin;
      bbox_shape.boundary[2]=(RectReal) psCShape->dfXMax;
      bbox_shape.boundary[3]=(RectReal) psCShape->dfYMax;
      SHPDestroyObject ( psCShape );
    }

    {
      // Only insert the rect if it will not fail the assertion in
      // Xastir_RTreeInsertRect --- this will cause us to ignore any shapes that
      // have invalid bboxes (or that return invalid bboxes from shapelib
      // for whatever reason
      if (bbox_shape.boundary[0] <= bbox_shape.boundary[2] &&
          bbox_shape.boundary[1] <= bbox_shape.boundary[3])
      {
        if (!getenv("XASTIR_RTREE_NOINSERT"))
        {
          Xastir_RTreeInsertRect(&bbox_shape, (void *)(i+1), root, 0);
        }
      }
    }
  }

  if (fbox != NULL)
  {
    fclose(fbox);
  }
  free(shx_offs);
  xa_perf_end(XA_ZONE_RTREE_BUILD);
}





void purge_shp_hash(time_t secs_now)
{
  struct hashtable_itr *iterator=NULL;
  shpinfo *si;
  int ret;


  if (secs_now > purge_time)    // Time to purge
  {

    purge_time += PURGE_PERIOD;

    if (shp_hash)
    {
      // walk through the hash table and kill entries that are old

      iterator=hashtable_iterator(shp_hash);
      do
      {
        ret=0;
        if (iterator)    // must check this, because could be null
        {
          // if the iterator malloc failed
          si=hashtable_iterator_value(iterator);

          if (si)
          {
            if (secs_now > si->last_access+PURGE_PERIOD)
            {
              // this is stale, hasn't been accessed in a while
              ret=hashtable_iterator_remove(iterator);

              // Important that we NOT do the
              // destroy first, because we've used
              // the filename pointer field of the
              // structure as the key, and the
              // remove function will free that.  If
              // we clobber the struct first, we
              // invite segfaults
              destroy_shpinfo(si);

            }
            else
            {
              ret=hashtable_iterator_advance(iterator);
            }
          }
        }
      }
      while (ret);
      // we're now done with the iterator.  Free it to stop us from
      // leaking!
      if (iterator)
      {
        free(iterator);
      }
    }
  }
}

#endif  // HAVE_LIBSHP


// To get rid of "-pedantic" compiler warning:
int NON_EMPTY_SOURCE_FILE;


