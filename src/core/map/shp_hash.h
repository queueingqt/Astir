
// Copyright (C) 2000-2026 The Xastir Group


#ifndef __ASTIR_SHP_HASH_H
#define __ASTIR_SHP_HASH_H


#ifdef HAVE_SHAPEFIL_H
  #include <shapefil.h>
#else
  #ifdef HAVE_LIBSHP_SHAPEFIL_H
    #include <libshp/shapefil.h>
  #else
    #error HAVE_LIBSHP defined but no corresponding include defined
  #endif  // HAVE_LIBSHP_SHAPEFIL_H
#endif  // HAVE_SHAPEFIL_H

/*
 * The style dbfawk decided for one shape.
 *
 * Cached because a shape's attributes do not change between frames, so neither
 * does its style -- and working it out was half of every warm frame.  The rule
 * output is cached, not the decision to draw: display_level is compared
 * against the current scale afterwards, so the same cached entry is correct at
 * every zoom.
 */
typedef struct
{
  short valid;
  short color, lanes, filled, pattern;
  int display_level, min_display_level, label_level;
  short fill_style, fill_color, fill_stipple;
  short label_color, font_size;
  char name[64];
  char key[64];
  char sym[4];
} shp_style;

typedef struct _shpinfo
{
  char *filename;
  struct Node* root;
  time_t creation;
  time_t last_access;
  int num_accesses;

  /*
   * One style per record, filled lazily.
   *
   * It lives here rather than in a table of its own because it has exactly the
   * lifetime of the R-tree beside it: both describe one shapefile, both are
   * invalidated by the same things, and both are freed when the file leaves
   * the hash.  A separate cache would need its own invalidation and would
   * eventually disagree with this one.
   */
  shp_style *styles;
  int nstyles;
} shpinfo;

void init_shp_hash(int clobber);
void add_shp_to_hash(char *filename,SHPHandle sHP);
void build_rtree(struct Node **root, SHPHandle sHP, const char *shp_path);
void destroy_shp_hash(void);
void empty_shpinfo(shpinfo *si);
void destroy_shpinfo(shpinfo *si);
void purge_shp_hash(time_t secs_now);
shpinfo *get_shp_from_hash(char *filename);

#endif // __ASTIR_SHP_HASH_H
