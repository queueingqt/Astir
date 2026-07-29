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
