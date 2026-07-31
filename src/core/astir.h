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

/* All of the misc entry points to be included for all packages */

#ifndef _ASTIR_H
#define _ASTIR_H



// INT_TO_XTPOINTER / XTPOINTER_TO_INT moved to astir_gui.h -- they name an
// Xt type, and this header is included by every file in the tree.


// Defines we can use to mark functions and parameters as "unused" to the compiler
#ifdef __GNUC__
  #define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
  #define UNUSED(x) UNUSED_ ## x
#endif

#ifdef __GNUC__
  #define UNUSED_FUNCTION(x) __attribute__((__unused__)) UNUSED_ ## x
#else
  #define UNUSED_FUNCTION(x) UNUSED_ ## x
#endif


#define SERIAL_KISS_RELAY_DIGI


// No X11 header.  This file is included by every core file in the tree, so an
// include here puts Xt in front of the shapefile reader and the APRS parser.
// The widget-typed declarations that needed it are in astir_gui.h; the drawing
// objects are in xa_draw.h, in neutral types.
#include "draw/xa_draw.h"

//#include "core/aprs/database.h"
#include "core/util/util.h"
#include "core/aprs/messages.h"
#include "core/aprs/fcc_data.h"
#include "core/aprs/rac_data.h"


#define MAX_CALLSIGN 9       // Objects are up to 9 chars


// MY_FG_COLOR and friends moved to astir_gui.h: two of them expand to Motif
// resource names.

#ifndef M_PI                      /* if not defined in math.h */
  #define M_PI 3.14159265358979323846
#endif  // M_PI

/* GLOBAL DEFINES */
extern char dangerous_operation[200];

// gc, the pixmaps and colors[] moved to xa_draw.h, in the neutral types the
// call sites already use.  `gc` was declared twice here.
// appshell and resize_dialog() moved to astir_gui.h.

extern int wait_to_redraw;

extern int debug_level;


extern float f_center_longitude;   // Floating point map center longitude
extern float f_center_latitude;    // Floating point map center latitude
extern float f_NW_corner_longitude;// longitude of NW corner
extern float f_NW_corner_latitude; // latitude of NW corner
extern float f_SE_corner_longitude;// longitude of SE corner
extern float f_SE_corner_latitude; // latitude of SE corner

extern long center_longitude;      // Longitude at center of map
extern long center_latitude;       // Latitude at center of map
extern long NW_corner_longitude;   // longitude of NW corner
extern long NW_corner_latitude;    // latitude of NW corner
extern long SE_corner_longitude;   // longitude of SE corner
extern long SE_corner_latitude;    // latitude of SE corner

extern long scale_x;               // x scaling in 1/100 sec per pixel
extern long scale_y;               // y scaling in 1/100 sec per pixel


extern long screen_width;
extern long screen_height;
// screen_x_offset / screen_y_offset moved to astir_gui.h -- Position is an Xt
// type and nothing outside main.c uses them.
extern int long_lat_grid;
//extern Pixmap  pixmap;
//extern Pixmap  pixmap_final;
//extern Pixmap  pixmap_alerts;
extern int map_color_levels;
extern int map_labels;
extern int map_lock_pan_zoom;
extern int map_auto_maps;
extern int auto_maps_skip_raster;
extern time_t sec_remove;
// da, text and app_context moved to astir_gui.h.
extern int redraw_on_new_data;
//extern Widget hidden_shell;
extern int index_maps_on_startup;
#define MAX_LABEL_FONTNAME 256
#define FONT_SYSTEM  0
#define FONT_STATION 1
#define FONT_TINY    2
#define FONT_SMALL   3
#define FONT_MEDIUM  4
#define FONT_LARGE   5
#define FONT_HUGE    6
#define FONT_BORDER  7
#define FONT_ATV_ID  8
#define FONT_MAX     9
#define FONT_DEFAULT FONT_MEDIUM
#define MAX_FONTNAME 256
extern char rotated_label_fontname[FONT_MAX][MAX_LABEL_FONTNAME];

#ifdef HAVE_LIBGEOTIFF
  extern int DRG_XOR_colors;
  extern int DRG_show_colors[13];
#endif  // HAVE_LIBGEOTIFF


extern int net_map_timeout;

// sort_list() and redraw_symbols() moved to astir_gui.h -- both take a Widget.

// cmap moved to xa_draw_x11.h.  Only the backend and the two renderer files
// (color.c, cairo_text.c) use it, and all three include X11 themselves.



/* from messages.c */
extern char  message_counter[5+1];
extern int  auto_reply;
extern char auto_reply_message[100];
extern int  satellite_ack_mode;
extern void clear_outgoing_messages(void);
extern void reset_outgoing_messages(void);
extern void output_message(char *from, char *to, char *message, char *path);
extern void check_and_transmit_messages(time_t time);
// mw[] moved to messages_gui.h -- it is an array of widgets, and this header is
// included by every core file in the tree.
extern void clear_message_windows(void);


/* from lang.c */
extern int load_language_file(char *filename);
extern char *langcode(char *code);
extern char langcode_hotkey(char *code);

/* from location.c */
extern void set_last_position(void);
extern void map_pos_last_position(void);

/* from location_gui.c */
extern char locate_station_call[30];
// Last_location() and Jump_location() moved to astir_gui.h.
extern void map_pos(long mid_y, long mid_x, long sz);
extern char locate_gnis_filename[200];

// This needs to be quite long for some of the weather station
// serial data to get through ok (Peet Bros U2k Complete Record Mode
// for one).
#define MAX_LINE_SIZE 512

// from main.c
extern char gprmc_save_string[MAX_LINE_SIZE+1];
extern char gpgga_save_string[MAX_LINE_SIZE+1];
extern int gps_port_save;

// from map.c
extern double calc_dscale_x(long x, long y);

/* from popup_gui.c */
extern void popup_message_always(char *banner, char *message);
extern void popup_message(char *banner, char *message);
extern void popup_ID_message(char *banner, char *message);


/* from view_messages.c */
extern void all_messages(char from, char *call_sign, char *from_call, char *message);
// view_all_messages() moved to astir_gui.h.


#endif /* ASTIR_H */


