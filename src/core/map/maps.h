/*
 *
 * ASTIR, Amateur Station Tracking and Information Reporting
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

#ifndef __ASTIR_MAPS_H
#define __ASTIR_MAPS_H

// (was: Xt only, for the Widget and XtPointer in the declarations below)
// was here for one field of map_index_record, which is now opaque.  Of the 21
// files that include this header, 19 include a Motif header themselves and are
// unaffected; log_utils.c and tile_mgmnt.c reached Motif only through here, and
// now reach none of it.
// The Widget-taking declarations moved to maps_gui.h.

#define MAX_OUTBOUND 900
#define MAX_MAP_POINTS 500000

#define DRAW_TO_PIXMAP          0
#define DRAW_TO_PIXMAP_FINAL    1
#define DRAW_TO_PIXMAP_ALERTS   2
#define INDEX_CHECK_TIMESTAMPS  9998
#define INDEX_NO_TIMESTAMPS     9999


/* memory structs */

typedef struct
{
  unsigned char vector_start_color;
  unsigned char object_behavior;
  unsigned long longitude;
  unsigned long latitude;
} map_vectors;

typedef struct
{
  unsigned long longitude;
  unsigned long latitude;
  unsigned int mag;
  char label_text[33];
  unsigned char text_color_quad;
} text_label;

typedef struct
{
  unsigned long longitude;
  unsigned long latitude;
  unsigned int mag;
  unsigned char symbol;
  unsigned char aprs_symbol;
  unsigned char text_color;
  char label_text[30];
} symbol_label;

typedef struct _map_index_record
{
  char filename[MAX_FILENAME];
  // A label for this record, in whatever form the front end wants to keep it,
  // memoised so the map chooser does not rebuild one per row per open.  The
  // core only ever sets it to NULL and hands it back through
  // xa_ui_free_label(); it never looks inside.  Opaque rather than XmString
  // because that was the only Motif type in this header, and a core data
  // structure should not have a field whose type comes from the toolkit.
  void *ui_label;
  unsigned long bottom;
  unsigned long top;
  unsigned long left;
  unsigned long right;
  int accessed;
  int max_zoom;       // Specify maximum zoom at which this layer is drawn.
  int min_zoom;       // Specify minimum zoom at which this layer is drawn.
  int map_layer;      // Specify which layer to draw the map on.
  int draw_filled;    // Specify whether to fill polygons when drawing.
  // 0 = Global No-Fill (Vector)
  // 1 = Global Fill
  // 2 = Auto (dbfawk controls it if present)
  int usgs_drg;       // Specify whether the map has USGS DRG colormap
  // and should have color configuration applied
  // 0 = No
  // 1 = Yes
  // 2 = Auto (detect from TIFFTAG_IMAGEDESCRIPTION)
  int selected;       // Specifies if map is currently selected
  int temp_select;    // Temporary selection used in map properties dialog
  int auto_maps;      // Specifies if map included in automaps function
  struct _map_index_record *next;
} map_index_record;
extern map_index_record *map_index_head;

typedef struct
{
  int img_x;
  int img_y;
  unsigned long x_long;
  unsigned long y_lat;
} tiepoint;

void draw_point(
                unsigned long x1,
                unsigned long y1,
                xa_pen gc,
                xa_surface_id which_pixmap,
                int skip_duplicates);

void draw_point_ll(float y1,
                   float x1,
                   xa_pen gc,
                   xa_surface_id which_pixmap,
                   int skip_duplicates);

void draw_vector(
                 unsigned long x1,
                 unsigned long y1,
                 unsigned long x2,
                 unsigned long y2,
                 xa_pen gc,
                 xa_surface_id which_pixmap,
                 int skip_duplicates);

void draw_vector_ll(
                    float y1,
                    float x1,
                    float y2,
                    float x2,
                    xa_pen gc,
                    xa_surface_id which_pixmap,
                    int skip_duplicates);

char *get_map_ext (char *filename);
char *get_map_dir (char *fullpath);
void load_auto_maps(char *dir);
void load_maps(void);
void fill_in_new_alert_entries(void);
void load_alert_maps(char *dir);
void  index_update_astir(char *filename, unsigned long bottom, unsigned long top, unsigned long left, unsigned long right, int default_map_layer);
void  index_update_ll(char *filename, double bottom, double top, double left, double right, int default_map_layer);
extern void get_horizontal_datum(char *datum, int sizeof_datum);
void draw_grid (void);
void Snapshot(void);
extern int index_retrieve(char *filename, unsigned long *bottom,
                          unsigned long *top, unsigned long *left, unsigned long *right,
                          int *max_zoom, int *min_zoom, int *map_layer, int *draw_filled,
                          int *usgs_drg, int *automaps);
extern void index_restore_from_file(void);
extern void index_save_to_file(void);
extern void map_indexer(int parameter);
extern void get_viewport_lat_lon(double *xmin,
                                 double *ymin,
                                 double *xmax,
                                 double *ymax);
extern int map_visible (unsigned long bottom_map_boundary,
                        unsigned long top_map_boundary,
                        unsigned long left_map_boundary,
                        unsigned long right_map_boundary);
extern int map_visible_lat_lon (double f_bottom_map_boundary,
                                double f_top_map_boundary,
                                double f_left_map_boundary,
                                double f_right_map_boundary);
extern int map_inside_viewport_lat_lon(double map_min_y,
                                       double map_max_y,
                                       double map_min_x,
                                       double map_max_x);
extern void draw_label_text (int x, int y, int label_length, int color, char *label_text);
extern void draw_rotated_label_text (int rotation, int x, int y, int label_length, int color, char *label_text, int fontsize);
extern int get_rotated_label_text_length_pixels(char *label_text, int fontsize);
extern void draw_centered_label_text (int rotation, int x, int y, int label_length, int color, char *label_text, int fontsize);
extern void Snapshot(void);
extern void clean_string(char *input);
extern int print_rotated;
extern int print_auto_rotation;
extern int print_auto_scale;
extern int print_in_monochrome;
extern int print_invert;
extern char printer_program[MAX_FILENAME+1];
extern char previewer_program[MAX_FILENAME+1];




extern void maps_init(void);
enum map_onscreen_enum {MAP_NOT_VIS=0,MAP_IS_VIS,MAP_NOT_INDEXED};
extern enum map_onscreen_enum map_onscreen(long left, long right, long top, long bottom, int checkpercentage);
extern enum map_onscreen_enum map_onscreen_index(char *filename);
extern time_t last_snapshot;
extern time_t last_kmlsnapshot;
extern int snapshot_interval;

extern int grid_size;

#if !defined(NO_GRAPHICS)
  #if defined(HAVE_MAGICK)
    extern float imagemagick_gamma_adjust;
  #endif    // HAVE_MAGICK
#endif  // NO_GRAPHICS

extern float raster_map_intensity;


extern void map_plot (long max_x, long max_y, long x_long_cord, long y_lat_cord, unsigned char color, long object_behavior, int destination_pixmap, int draw_filled);

// A struct to pass down in to map driver functions so they can have
// driver-specific flags.  Most drivers won't care about any (or even all)
// of the flags, but this way we can just pass a single pointer rather than
// adding new arguments to the generic interface each time we want new flags
typedef struct
{
  int draw_filled;
  int usgs_drg;
} map_draw_flags;

// Derive the x scale from the y scale and the position, so that a distance is
// the same number of pixels in both directions.  Was declared in main.h, which
// is the Motif front end's header; it is defined here in maps.c and a second
// front end needs it.
extern long get_x_scale(long x, long y, long ysc);

/*
 * The credit the currently drawn maps require, or an empty string.
 *
 * Set by whichever map driver needs one -- only the OSM tile driver does --
 * and cleared at the start of every map pass, so it reflects what is on screen
 * rather than what once was.
 *
 * The front end draws it, not the map code, and that is the point.  Drawn into
 * the map layer it scaled and slid with the map: the render scheduler shows the
 * previous frame transformed while the new one is composed, so anything in the
 * canvas moves with a drag and grows with a zoom.  A credit is chrome.  It
 * belongs on top of the frame, in fixed position, at a fixed size.
 */
#define MAP_ATTRIBUTION_MAX 128
extern char map_attribution[MAP_ATTRIBUTION_MAX];

/*
 * A dirty rectangle, in Astir coordinates, or inactive.
 *
 * When a pan moves the view, most of the new frame is the old frame shifted;
 * only an L-shaped strip along two edges is new.  Redrawing the whole map to
 * produce mostly what was already there is the largest avoidable cost in a
 * gesture.
 *
 * This is honoured by map_visible_lat_lon(), which every shapefile shape
 * already passes through, and by the raster tile driver, which clamps its
 * pixel loop to it.  Anything drawn outside is discarded, so a driver that
 * ignores it is correct but slow rather than wrong.
 *
 * The view corners stay at the FULL view throughout: screen positions are
 * derived from them, so narrowing them would move everything that did get
 * drawn.  This is a separate rectangle for that reason.
 */
extern int  xa_dirty_active;
extern long xa_dirty_left, xa_dirty_right;    /* longitude, Astir units */
extern long xa_dirty_top, xa_dirty_bottom;    /* latitude, Astir units */

void xa_dirty_set(long left, long right, long top, long bottom);
void xa_dirty_clear(void);

/*
 * Put the configured map background into the palette slot the front end clears
 * with, and return it.
 *
 * MAP_BGCOLOR has been read from the config, written back to it and offered in
 * the preferences all along, but the code that turned the number into a colour
 * lived in the Motif main() and did not come across.  The setting has been
 * inert since: every map has been drawn on gray73 whatever the user chose,
 * which happens to be what 0 means, so it looked like it worked.
 *
 * Call it before clearing the map layer.  It is cheap and the setting can
 * change between frames.
 */
xa_color map_background_apply(void);

/*
 * Which maps are shown, as a property of the index rather than a file.
 *
 * selected_maps.sys is a list of paths, and load_maps() has always read it
 * straight through and drawn as it went -- so nothing ever knew which of the
 * indexed maps were on, only which ones to draw next.  That is enough to render
 * and not enough to offer a choice, which is why choosing a map has meant
 * editing the file by hand.
 *
 * map_selection_load() marks the index from the file; map_selection_save()
 * writes the file from the index.  The core keeps the format, a front end keeps
 * the presentation, and neither needs to know the other's business.
 */
void map_selection_load(void);
int  map_selection_save(void);

#endif /* __ASTIR_MAPS_H */


