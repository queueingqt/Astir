/*
 * xa_settings.c -- definitions for xa_settings.h.  Moved verbatim from main.c.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>

#include "xa_settings.h"

int my_trail_diff_color = 0;
int serial_char_pacing;  // Inter-char delay in ms for serial ports.
char lang_to_use[30];
int  altnet;
char altnet_call[MAX_CALLSIGN+1];
int Display_packet_data_type;
int show_only_station_capabilities = 0;
int Display_packet_data_mine_only = 0;
int map_chooser_expand_dirs = 0;
int long_lat_grid;              // Switch for Map Lat and Long grid display
int draw_labeled_grid_border = FALSE;   // Toggle labeled border around map.
int map_lock_pan_zoom = 0;
int auto_maps_skip_raster;
int map_labels;                 // toggle use of map_labels */
int index_maps_on_startup;      // Index maps on startup
int transmit_disable;
int posit_tx_disable;
int object_tx_disable;
int enable_server_port = 0;
int english_units;
double cvt_kn2len;  // from knots
double cvt_mi2len;  // from miles
int do_dbstatus;
int coordinate_system = USE_DDMMMM; // Default, used for most APRS systems
int ATV_screen_ID;
int pop_up_new_bulletins = 0;
int view_zero_distance_bulletins = 0;
int warn_about_mouse_modifiers = 1;
int redo_list;                  // Station List update request
int redraw_on_new_data;         // Station redraw request
time_t POSIT_rate;              // Posit TX rate timer
time_t OBJECT_rate;             // Object/Item TX rate timer
time_t update_DR_rate;          // How often to call draw_symbols if DR enabled
int net_map_timeout = 120;
int trail_segment_time;         // Segment missing if above this time (mins)
int trail_segment_distance;     // Segment missing if greater distance
int RINO_download_interval;     // Interval at which to download RINO waypoints,
int dead_reckoning_timeout = 60 * 10;   // 10 minutes;
int transmit_compressed_posit;  // transmit location in compressed format?
int transmit_compressed_objects_items;  // Same for objects & items
time_t posit_last_time;
char aprs_station_message_type = '='; // station message-capable or not
int snapshots_enabled = 0;      // toggle to allow creating .png snapshots on a regular basis
int kmlsnapshots_enabled = 0;   // toggle to allow creating .kml snapshots on a regular basis
int read_file;
char my_callsign[MAX_CALLSIGN+1];
char my_lat[MAX_LAT];
char my_long[MAX_LONG];
char my_group;
char my_symbol;
char my_phg[MAX_PHG+1];
char my_comment[MAX_COMMENT+1];
int  my_last_course;
int  my_last_speed;
long my_last_altitude;
time_t my_last_altitude_time;
char AUTO_MAP_DIR[400];
char ALERT_MAP_DIR[400];
char SELECTED_MAP_DIR[400];
char SELECTED_MAP_DATA[400];
char MAP_INDEX_DATA[400];
char SYMBOLS_DIR[400];
char HELP_FILE[400];
char SOUND_DIR[400];
char sound_command[90];
char prox_min[30];
char prox_max[30];
char bando_min[30];
char bando_max[30];

// SmartBeaconing(tm): eleven parallel sb_* scalars collapsed into one struct.
// The first eight are configuration; the last three are runtime state, kept
// together because they belong to the same subsystem.
xa_smart_beacon_t xa_sb =
{
  .enabled          = 0,
  .posit_fast       = 90,       // secs
  .posit_slow       = 30,       // mins
  .low_speed_limit  = 2,        // below this, SmartBeaconing is disabled
  .high_speed_limit = 60,
  .turn_min         = 20,       // degrees
  .turn_slope       = 25,       // degrees/mph
  .turn_time        = 5,        // secs between other beacon and turn beacon
  .posit_rate       = 30 * 60,  // computed posit rate (secs)
  .current_heading  = -1,
  .last_heading     = -1,
};

// Five {play?, filename} pairs collapsed into one table indexed by event.
xa_sound_cfg_t xa_sound[XA_SOUND_COUNT];

// Six {enable flag, path} pairs collapsed into one table.  Paths are filled in
// from the config file, defaulting to logs/<kind>.log.
xa_log_cfg_t xa_log[XA_LOG_COUNT];

uid_t euid;
gid_t egid;
int   my_argc;
char **my_argv;
char **my_envp;
int currently_selected_stations      = 0;
char dangerous_operation[200];
int emergency_beacon = 0;
int re_sort_maps = 1;
int disable_all_maps = 0;
int map_auto_maps;              /* toggle use of auto_maps */
int map_color_levels;           /* toggle use of map_color_levels */
int map_color_fill;             /* Whether or not to fill in map polygons with solid color */
int map_background_color;       /* Background color for maps */
int letter_style;               /* Station Letter style */
int icon_outline_style;         /* Icon Outline style */
int wx_alert_style;             /* WX alert map style */
time_t map_refresh_interval = 0; /* how often to refresh maps, seconds */
time_t map_refresh_time = 0;     /* when to refresh maps next, seconds */
double cvt_m2len;   // from meter
int interrupt_drawing_now = 0;  // Flag used to interrupt map drawing
int request_new_image = 0;      // Flag used to request a create_image operation
float f_center_longitude;    // Floating point map center longitude, updated by new_image()
float f_center_latitude;     // Floating point map center latitude , updated by new_image()
char user_dir[1000];            /* user directory file */
int current_trail_color;        /* what color to draw station trails with */
int wait_to_redraw;             /* wait to redraw until system is up */
time_t max_transmit_time;       /* max time between transmits */
time_t gps_time;                /* gps delay time */
char gprmc_save_string[MAX_LINE_SIZE+1];
char gpgga_save_string[MAX_LINE_SIZE+1];
int gps_port_save;
time_t sec_old;                 /* station old after */
time_t sec_clear;               /* station cleared after */
time_t aircraft_sec_clear;      /* aircraft cleared after */
time_t sec_remove;              /* Station removed after */
int output_station_type;        /* Broadcast station type */
time_t posit_next_time;         /* time at which next posit TX will occur */
int transmit_now;               /* set to transmit now (push on moment) */
int my_position_valid = 1;      /* Don't send posits if this is zero */
int using_gps_position = 0;     /* Set to one if a GPS port is active */
int operate_as_an_igate;        /* toggle igate operations for net connections */
unsigned igate_msgs_tx;         /* current total of igate messages transmitted */
int traffic_utf8_enabled = 1;   /* toggle UTF-8 parse/send for APRS messages */
time_t WX_ALERTS_REFRESH_TIME;  /* Minimum WX alert map refresh time in seconds */
pid_t last_sound_pid;
int disable_all_popups = 0;

Selections Select_ = { 0, // none
                       1, // mine
                       1, // tnc
                       1, // direct
                       1, // via_digi
                       1, // net
                       0, // tactical
                       1, // old_data

                       1, // stations
                       1, // fixed_stations
                       1, // moving_stations
                       1, // weather_stations
                       1, // CWOP_wx_stations
                       1, // objects
                       1, // weather_objects
                       1, // gauge_objects
                       1, // other_objects
                       1, // aircraft_objects
                       1, // vessel_objects
                     };

What_to_display Display_ = { 1, // callsign
                             1, // label_all_trackpoints
                             1, // symbol
                             1, // symbol_rotate
                             1, // trail

                             1, // course
                             1, // speed
                             1, // speed_short
                             1, // altitude

                             1, // weather
                             1, // weather_text
                             1, // temperature_only
                             1, // wind_barb

                             1, // aloha_circle
                             1, // ambiguity
                             1, // phg
                             1, // default_phg
                             1, // phg_of_moving

                             1, // df_data
                             1, // df_beamwidth_data
                             1, // df_bearing_data
                             1, // dr_data
                             1, // dr_arc
                             1, // dr_course
                             1, // dr_symbol

                             1, // dist_bearing
                             1, // last_heard
                           };

#ifdef HAVE_LIBGEOTIFF
// USGS DRG colour toggles.  Plain ints; they were the last symbols xa_config.o
// needed from main.o.
int DRG_XOR_colors = 0;
int DRG_show_colors[13];
#endif  // HAVE_LIBGEOTIFF
