/*
 * xa_settings.h -- user-configurable settings and session state, owned by the core.
 *
 * Moved out of main.c so that core objects needing them do not have to link
 * main.o, and therefore Motif.  Measured motivation: xa_config.o needed 109
 * symbols from main.o, every one of them plain data and not a single GUI type;
 * db.o needed 69, of which only three Widgets and one function were GUI.
 *
 * Names, types and initialisers are unchanged from main.c on purpose, so that
 * relocating them could not alter behaviour.
 *
 * This header must never include an X11, Xt or Motif header.
 */

#ifndef XA_SETTINGS_H
#define XA_SETTINGS_H

#include <time.h>
#include <sys/types.h>   // uid_t, gid_t

// Array bounds (MAX_CALLSIGN, MAX_FILENAME, ...) and a few plain macros.
// globals.h includes nothing at all, so it cannot drag X in.
#include "globals.h"

// Note: several of these are also declared in main.h without their array
// bounds, e.g. `extern char HELP_FILE[];`.  Those declarations are compatible
// but incomplete, so sizeof() against them fails.  Anything using sizeof() on
// one of these must include this header, which carries the real dimensions.

extern int my_trail_diff_color;
extern int serial_char_pacing;  // Inter-char delay in ms for serial ports.
extern char lang_to_use[30];
extern int altnet;
extern char altnet_call[MAX_CALLSIGN+1];
extern int Display_packet_data_type;
extern int show_only_station_capabilities;
extern int Display_packet_data_mine_only;
extern int map_chooser_expand_dirs;
extern int long_lat_grid;  // Switch for Map Lat and Long grid display
extern int draw_labeled_grid_border;  // Toggle labeled border around map.
extern int map_lock_pan_zoom;
extern int auto_maps_skip_raster;
extern int map_labels;  // toggle use of map_labels */
extern int index_maps_on_startup;  // Index maps on startup
extern int transmit_disable;
extern int posit_tx_disable;
extern int object_tx_disable;
extern int enable_server_port;
extern int english_units;
extern double cvt_kn2len;  // from knots
extern double cvt_mi2len;  // from miles
extern int do_dbstatus;
extern int coordinate_system;  // Default, used for most APRS systems
extern int ATV_screen_ID;
extern int pop_up_new_bulletins;
extern int view_zero_distance_bulletins;
extern int warn_about_mouse_modifiers;
extern int redo_list;  // Station List update request
extern int redraw_on_new_data;  // Station redraw request
extern time_t POSIT_rate;  // Posit TX rate timer
extern time_t OBJECT_rate;  // Object/Item TX rate timer
extern time_t update_DR_rate;  // How often to call draw_symbols if DR enabled
extern int net_map_timeout;
extern int trail_segment_time;  // Segment missing if above this time (mins)
extern int trail_segment_distance;  // Segment missing if greater distance
extern int RINO_download_interval;  // Interval at which to download RINO waypoints,
extern int dead_reckoning_timeout;  // 10 minutes;
extern int transmit_compressed_posit;  // transmit location in compressed format?
extern int transmit_compressed_objects_items;  // Same for objects & items
extern time_t posit_last_time;
extern char aprs_station_message_type;  // station message-capable or not
extern int snapshots_enabled;  // toggle to allow creating .png snapshots on a regular basis
extern int kmlsnapshots_enabled;  // toggle to allow creating .kml snapshots on a regular basis
extern int read_file;
extern char my_callsign[MAX_CALLSIGN+1];
extern char my_lat[MAX_LAT];
extern char my_long[MAX_LONG];
extern char my_group;
extern char my_symbol;
extern char my_phg[MAX_PHG+1];
extern char my_comment[MAX_COMMENT+1];
extern int my_last_course;
extern int my_last_speed;
extern long my_last_altitude;
extern time_t my_last_altitude_time;
extern char AUTO_MAP_DIR[400];
extern char ALERT_MAP_DIR[400];
extern char SELECTED_MAP_DIR[400];
extern char SELECTED_MAP_DATA[400];
extern char MAP_INDEX_DATA[400];
extern char SYMBOLS_DIR[400];
extern char HELP_FILE[400];
extern char SOUND_DIR[400];
extern char sound_command[90];
extern char prox_min[30];
extern char prox_max[30];
extern char bando_min[30];
extern char bando_max[30];

// SmartBeaconing(tm) settings and runtime state.  Was eleven separate sb_*
// globals; they are one subsystem and are now one struct.
typedef struct
{
  int enabled;
  int posit_fast;         // secs
  int posit_slow;         // mins
  int low_speed_limit;
  int high_speed_limit;
  int turn_min;           // degrees
  int turn_slope;         // degrees/mph
  int turn_time;          // secs
  // Runtime state rather than configuration:
  int posit_rate;         // computed posit rate (secs)
  int current_heading;
  int last_heading;
} xa_smart_beacon_t;

extern xa_smart_beacon_t xa_sb;

// Sounds were five pairs of parallel globals -- a play-this flag and a
// filename each.  One table indexed by the event removes the duplication and
// makes adding an event a one-line change.
typedef enum
{
  XA_SOUND_NEW_STATION = 0,
  XA_SOUND_NEW_MESSAGE,
  XA_SOUND_PROX,
  XA_SOUND_BAND_OPEN,
  XA_SOUND_WX_ALERT,
  XA_SOUND_COUNT
} xa_sound_event_t;

typedef struct
{
  int  enabled;
  char file[90];
} xa_sound_cfg_t;

extern xa_sound_cfg_t xa_sound[XA_SOUND_COUNT];

// Logging was six pairs of parallel globals -- an enable flag and a path each.
// One table indexed by the log kind removes the duplication; adding a log is a
// one-line change instead of two globals, two externs and two config clauses.
typedef enum
{
  XA_LOG_TNC = 0,
  XA_LOG_NET,
  XA_LOG_IGATE,
  XA_LOG_MESSAGE,
  XA_LOG_WX,
  XA_LOG_WX_ALERT,
  XA_LOG_COUNT
} xa_log_kind_t;

typedef struct
{
  int  enabled;
  char file[400];
} xa_log_cfg_t;

extern xa_log_cfg_t xa_log[XA_LOG_COUNT];

// Second extraction pass: symbols whose definitions use /* */ trailing
// comments, which the first pass's regex silently skipped.
extern uid_t euid;
extern gid_t egid;
extern int my_argc;
extern char **my_argv;
extern char **my_envp;
extern int currently_selected_stations;
extern char dangerous_operation[200];
extern int emergency_beacon;
extern int re_sort_maps;
extern int disable_all_maps;
extern int map_auto_maps;  /* toggle use of auto_maps */
extern int map_color_levels;  /* toggle use of map_color_levels */
extern int map_color_fill;  /* Whether or not to fill in map polygons with solid color */
extern int map_background_color;  /* Background color for maps */
extern int letter_style;  /* Station Letter style */
extern int icon_outline_style;  /* Icon Outline style */
extern int wx_alert_style;  /* WX alert map style */
extern time_t map_refresh_interval;  /* how often to refresh maps, seconds */
extern time_t map_refresh_time;  /* when to refresh maps next, seconds */
extern double cvt_m2len;  // from meter
extern int interrupt_drawing_now;  // Flag used to interrupt map drawing
extern int request_new_image;  // Flag used to request a create_image operation
extern float f_center_longitude;  // Floating point map center longitude, updated by new_image()
extern float f_center_latitude;  // Floating point map center latitude , updated by new_image()
extern char user_dir[1000];  /* user directory file */
extern int current_trail_color;  /* what color to draw station trails with */
extern int wait_to_redraw;  /* wait to redraw until system is up */
extern time_t max_transmit_time;  /* max time between transmits */
extern time_t gps_time;  /* gps delay time */
extern char gprmc_save_string[MAX_LINE_SIZE+1];
extern char gpgga_save_string[MAX_LINE_SIZE+1];
extern int gps_port_save;
extern time_t sec_old;  /* station old after */
extern time_t sec_clear;  /* station cleared after */
extern time_t aircraft_sec_clear;  /* aircraft cleared after */
extern time_t sec_remove;  /* Station removed after */
extern int output_station_type;  /* Broadcast station type */
extern time_t posit_next_time;  /* time at which next posit TX will occur */
extern int transmit_now;  /* set to transmit now (push on moment) */
extern int my_position_valid;  /* Don't send posits if this is zero */
extern int using_gps_position;  /* Set to one if a GPS port is active */
extern int operate_as_an_igate;  /* toggle igate operations for net connections */
extern unsigned igate_msgs_tx;  /* current total of igate messages transmitted */
extern int traffic_utf8_enabled;  /* toggle UTF-8 parse/send for APRS messages */
extern time_t WX_ALERTS_REFRESH_TIME;  /* Minimum WX alert map refresh time in seconds */
extern pid_t last_sound_pid;
extern int disable_all_popups;

extern Selections Select_;
extern What_to_display Display_;

#ifdef HAVE_LIBGEOTIFF
extern int DRG_XOR_colors;
extern int DRG_show_colors[13];
#endif


// Moved out of view_message_gui.c, where these lived only because the dialog
// that edits them does.  Definitions unchanged.
extern int vm_range;
extern int view_message_limit;
extern int Read_messages_packet_data_type;  // 1=tnc_only, 2=net_only, 0=tnc&net
extern int Read_messages_mine_only;


// Moved out of list_gui.c, where these lived only because the dialog
// that edits them does.  Definitions unchanged.
// Incomplete type on purpose: list_gui.h already declares these the same way,
// and the extent (LST_NUM) is a station-list category count that this header
// has no business knowing.  The definition in xa_settings.c carries it.
extern int list_size_h[];  // height of entire list widget
extern int list_size_w[];  // width  of entire list widget


// Moved out of bulletin_gui.c, where these lived only because the dialog
// that edits them does.  Definitions unchanged.
extern int bulletin_range;


// Moved out of track_gui.c, where these lived only because the dialog
// that edits them does.  Definitions unchanged.
extern int track_station_on;  /* used for tracking stations */
extern int track_me;
extern int track_case;  /* used for tracking stations */
extern int track_match;  /* used for tracking stations */
extern char tracking_station_call[30];  /* Tracking station callsign */


// Moved out of locate_gui.c, where these lived only because the dialog
// that edits them does.  Definitions unchanged.
extern char locate_station_call[30];
extern char locate_gnis_filename[200];


// Moved out of objects_gui.c, where these lived only because the dialog
// that edits them does.  Definitions unchanged.
extern char predefined_object_definition_filename[256];
extern int predefined_menu_from_file;


// Moved out of interface_gui.c, where these lived only because the dialog
// that edits them does.  Definitions unchanged.
extern int WX_rain_gauge_type;

#endif // XA_SETTINGS_H
