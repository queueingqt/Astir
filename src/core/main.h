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

#ifndef XASTIR_MAIN_H
#define XASTIR_MAIN_H

#include <sys/types.h>   // uid_t/gid_t, which used to arrive via Xos.h

// No X11 header.  Core files include main.h for its non-GUI declarations, so
// an include here puts Xt in front of them.  The widget-typed declarations
// are in main_gui.h; the drawing objects are in xa_draw.h.

// For mutex debugging with Linux threads only
//#ifdef __linux__
//  #define MUTEX_DEBUG 1
//#endif  // __linux__


// This gets defined if pthreads implementation uses SIGUSR1/SIGUSR2
// signals.  This disables our SIGUSR1 handler which disallows
// creating snapshots on receipt of that signal.  Old kernels (2.0
// and early 2.1) had only 32 signals available so only USR1/USR2
// were available for LinuxThreads use.  Newer kernels have more
// signals available, making USR1/USR2 available to programs again.
// _NSIG is defined in /usr/include/bits/signum.h
//
#ifdef __linux__
  #ifdef _NSIG
    #if (_NSIG <= 32)
      #define OLD_PTHREADS
      //#   warning ***** OLD_PTHREADS DETECTED *****
    #else
      //#   warning ***** NEW_PTHREADS DETECTED *****
    #endif // if (_NSIG...)
  #endif  // _NSIG
#endif  // __linux__


// To enable the "Transmit Raw WX data" button in
// Configure->Defaults dialog.  Warning:  If you enable this, enable
// the button in Configure->Defaults, and are running a weather
// station that puts out raw data that is NOT allowed by the APRS
// spec, you'll be putting out garbage and trashing the channel.
//
//#define TRANSMIT_RAW_WX

// To use predefined object configuration files from within the
// user's base directory e.g. ~/.xastir/config/predefined_SAR.sys
// rather than from the main base directory, enable this definition.
//#define OBJECT_DEF_FILE_USER_BASE

extern int enable_server_port;


extern char altnet_call[MAX_CALLSIGN+1];
extern int altnet;
extern FILE *read_file_ptr;
extern int interrupt_drawing_now;


#define VERSIONFRM  (!altnet?XASTIR_TOCALL:altnet_call) /* Packet version info */


#ifdef __LCLINT__
  #define PACKAGE "xastir"
  #define VERSION "lclint"
  #define VERSIONTXT "xastir lclint debug version"
#else   // __LCLINT__
  #define VERSIONTXT PACKAGE " " VERSION
#endif  // __LCLINT__


#define VERSIONLABEL VERSIONTXT

// NOTE:  This is out of date and not used anymore anyway.
#define VERSIONMESSAGE "XASTIR Version: " VERSION "\n\nAmateur Station Tracking and Information Reporting\nby Frank Giannandrea, KC2GJS was KC0DGE\n   Code added by:\n   Richard Hagemeyer - VE3UNW,  Curt Mills - WE7U,\n   Mike Sims - KA9KIM,   Gerald Stueve - K4INT was KE4NFJ,\n   Mark Grennan - KD5AMB,  Henk de Groot - PE1DNN,\n   Jim Sevilla - KD6VPE,  Jose R. Marte A. - HI8GN,\n   Michael G. Petry - N3NYN,   Lloyd Miller - VE6LFM,\n   Alessandro Frigeri - IK0YUP,\n   Chuck Byam - KG4IJB,   Rolf Bleher - DK7IN,   Ken Koster - N7IPB"

// NOTE:  This is out of date and not used anymore anyway.
#define ABOUTGNUL "XASTIR, Copyright (C) 1999, 2000 Frank Giannandrea\nXASTIR comes with ABSOLUTELY NO WARRANTY;\nThis is free software, and you are welcome\nto redistribute it under certain conditions;\nsee the GNU LICENSE for details.\n"

#define MAX_PHG      8
#define MAX_COMMENT  80



//////////////////////////////////////////////////////////////////////
// These globals and prototypes are from:
// http://lightconsulting.com/~thalakan/process-title-notes.html
// They seems to work fine on Linux, but they only change the "ps"
// listings, not the top listings.  I don't know why yet.

/* Globals */
//extern char **Argv = ((void *)0);
//extern char *__progname, *__progname_full;
//extern char *LastArgv = ((void *)0);

/* Prototypes */
extern void set_proc_title(char *fmt,...);
extern void init_set_proc_title(int argc, char *argv[], char *envp[]);

// New stuff defined by Xastir project:
extern int my_argc;
extern char **my_argv;
extern char **my_envp;
//////////////////////////////////////////////////////////////////////

extern int input_x;
extern int input_y;

#define MAX_RELAY_DIGIPEATER_CALLS 50
extern char relay_digipeater_calls[10*MAX_RELAY_DIGIPEATER_CALLS];
extern int skip_dupe_checking;
extern int serial_char_pacing;  // Inter-character delay in ms.
extern int disable_all_maps;
extern int re_sort_maps;
extern int debug_level;
extern int my_position_valid;
extern int using_gps_position;
extern int transmit_now;
extern char DATABASE_FILE[];
extern char DATABASE_POINTER_FILE[];
extern char DATABASE_POINTER_TEMP[];
extern char ALERT_MAP_DIR[400];
extern char SELECTED_MAP_DIR[400];
extern char SELECTED_MAP_DATA[400];
extern char MAP_INDEX_DATA[400];
extern char AUTO_MAP_DIR[400];
extern char SYMBOLS_DIR[400];
extern char HELP_FILE[];
extern char SOUND_DIR[400];
extern time_t WX_ALERTS_REFRESH_TIME;
extern time_t remove_ID_message_time;
extern int pending_ID_message;
extern time_t gps_time;
extern time_t POSIT_rate;
extern time_t OBJECT_rate;
extern time_t update_DR_rate;

extern time_t posit_last_time;
extern time_t posit_next_time;


extern int pop_up_new_bulletins;
extern int view_zero_distance_bulletins;
extern int warn_about_mouse_modifiers;
extern int predefined_menu_from_file;

extern int draw_labeled_grid_border;  // used to turn on or off border on map

extern int my_trail_diff_color;  // trails for mycall with different ssids have the same or a different color

extern int output_station_type;

extern int emergency_beacon;

// Selections and What_to_display moved to globals.h -- they are plain int
// structs of display/selection filters with nothing GUI about them, and the
// core needs them without main.h.  Their externs live in xa_settings.h.

// both moved to xa_settings.h
// colors[], MAX_TRAIL_COLORS and trail_colors[] moved to xa_draw.h.  colors[]
// was declared both here and in xastir.h; core drawing code uses all three.
extern int current_trail_color;
extern int english_units;
extern int do_dbstatus;
extern int redraw_on_new_data;
extern int redo_list;
extern int operate_as_an_igate;

#ifdef TRANSMIT_RAW_WX
  extern int transmit_raw_wx;
#endif  // TRANSMIT_RAW_WX

extern int transmit_compressed_posit;
extern int transmit_compressed_objects_items;
extern int traffic_utf8_enabled;
extern int snapshots_enabled;
extern int kmlsnapshots_enabled;
extern char user_dir[];
extern char lang_to_use[30];
extern char my_group;
extern char my_symbol;
extern char my_phg[MAX_PHG+1];
extern char my_comment[MAX_COMMENT+1];
extern int map_background_color;
extern int map_color_fill;
extern int letter_style;
extern int icon_outline_style;
extern int wx_alert_style;
extern time_t map_refresh_interval;
extern time_t map_refresh_time;
extern char sound_command[90];
extern pid_t last_sound_pid;
extern int ATV_screen_ID;
extern int festival_speak_ID;
extern int festival_speak_new_station;
extern int festival_speak_proximity_alert;
extern int festival_speak_tracked_proximity_alert;
extern int festival_speak_band_opening;
extern int festival_speak_new_message_alert;
extern int festival_speak_new_message_body;
extern int festival_speak_new_weather_alert;
extern char prox_min[30];
extern char prox_max[30];
extern time_t sec_old;
extern time_t sec_clear;
extern time_t aircraft_sec_clear;
extern int trail_segment_time;
extern int trail_segment_distance;
extern int RINO_download_interval;
extern int dead_reckoning_timeout;
extern char bando_min[30];
extern char bando_max[30];
extern int Display_packet_data_type;
extern int show_only_station_capabilities;
extern int Display_packet_data_mine_only;
extern int menu_x;
extern int menu_y;
extern long my_last_altitude;
extern time_t my_last_altitude_time;
extern int my_last_course;
extern int my_last_speed;   // in knots
extern unsigned igate_msgs_tx;
extern int symbols_loaded;
// gc2, gc_tint, gc_stipple and gc_bigfont moved to xa_draw.h, as xa_pen.  Core
// drawing code uses all four and only ever passes them to xa_draw calls, so
// declaring them here made every such file depend on a front-end header for a
// type it never actually needed.
extern int read_file;
extern float x_screen_distance;
extern time_t max_transmit_time;
extern int transmit_disable;
extern int posit_tx_disable;
extern int object_tx_disable;
extern int map_chooser_expand_dirs;
extern int request_new_image;
extern int disable_all_popups;
extern char temp_tracking_station_call[30];

extern int coordinate_system;
#define USE_DDDDDD      0
#define USE_DDMMMM      1
#define USE_DDMMSS      2
#define USE_UTM         3
#define USE_UTM_SPECIAL 4
#define USE_MGRS        5

// The coordinate-calculator dialog and the Widget-taking callbacks moved to
// main_gui.h.

typedef struct _transparent_color_record
{
  unsigned long trans_color;
  struct _transparent_color_record *next;
} transparent_color_record;

// Takes the pixel value, not an XColor: the body only ever read c.pixel, and
// an X11 struct by value in a core signature is what kept this header on Xt.
extern int check_trans (unsigned long pixel, transparent_color_record *c_trans_color_head);

extern void draw_WMS_map (char *filenm, int destination_pixmap, char *URL, transparent_color_record *c_trans_color_head, int nocache);

extern void locate_gui_init(void);
extern void geocoder_gui_init(void);
extern void location_gui_init(void);
extern void view_message_gui_init(void);
extern void wx_gui_init(void);
// get_x_scale() moved to maps.h -- it is defined in maps.c, takes nothing but
// longs, and a second front end needs it to derive scale_x.

extern void display_zoom_status(void);
extern int center_zoom_override;
extern void statusline(char *status_text,int update);
extern void stderr_and_statusline(char *message);
extern int SayTextInit(void);
extern int SayText(char *text);

// Symbol update stuff


extern void Draw_All_CAD_Objects(void);

// Nominatim geocoding configuration
extern char nominatim_server_url[400];
extern int nominatim_cache_enabled;
extern int nominatim_cache_days;
extern char nominatim_user_email[100];
extern char nominatim_country_default[20];

// unit conversion
extern char un_alt[2+1];
extern char un_dst[2+1];
extern char un_spd[4+1];
extern double cvt_m2len;
extern double cvt_dm2len;
extern double cvt_hm2len;
extern double cvt_kn2len;
extern double cvt_mi2len;

// euid and egid
extern uid_t euid;
extern gid_t egid;

/* JMT - works in FreeBSD */
// Note: weird conditional thing is there just to shut up
// "ignoring return value" warnings from GCC on newer Linux systems
#define DISABLE_SETUID_PRIVILEGE do { \
if (seteuid(getuid())==0){/* all is well*/} \
if (setegid(getgid())==0){/* all is well*/} \
if (debug_level & 4) { fprintf(stderr, "Changing euid to %d and egid to %d\n", (int)getuid(), (int)getgid()); } \
} while(0)
#define ENABLE_SETUID_PRIVILEGE do { \
if (seteuid(euid)==0){/* all is well*/}     \
if (setegid(egid)==0){/* all is well*/} \
if (debug_level & 4) { fprintf(stderr, "Changing euid to %d and egid to %d\n", (int)euid, (int)egid); } \
} while(0)

#ifdef HAVE_LIBSHP
  extern void create_map_from_trail(char *call_sign);
#endif  // HAVE_LIBSHP


#endif /* XASTIR_MAIN_H */


