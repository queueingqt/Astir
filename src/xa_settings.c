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
int smart_beaconing;            // Master enable/disable for SmartBeaconing(tm) mode
int sb_POSIT_rate = 30 * 60;    // Computed SmartBeaconing(tm) posit rate (secs)
int sb_last_heading = -1;       // Heading at time of last posit
int sb_current_heading = -1;    // Most recent heading parsed from GPS sentence
int sb_turn_min = 20;           // Min threshold for corner pegging (degrees)
int sb_turn_slope = 25;         // Threshold slope for corner pegging (degrees/mph)
int sb_turn_time = 5;           // Time between other beacon & turn beacon (secs)
int sb_posit_fast = 90;         // Fast beacon rate (secs)
int sb_posit_slow = 30;         // Slow beacon rate (mins)
int sb_low_speed_limit = 2;     // Speed below which SmartBeaconing(tm) is disabled &
int sb_high_speed_limit = 60;   // Speed above which we'll beacon at the
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
char LOGFILE_TNC[400];
char LOGFILE_NET[400];
char LOGFILE_IGATE[400];
char LOGFILE_MESSAGE[400];
char LOGFILE_WX[400];
char LOGFILE_WX_ALERT[400];
char sound_command[90];
int  sound_play_new_station;
char sound_new_station[90];
int  sound_play_new_message;
char sound_new_message[90];
int  sound_play_prox_message;
char sound_prox_message[90];
char prox_min[30];
char prox_max[30];
int  sound_play_band_open_message;
char sound_band_open_message[90];
char bando_min[30];
char bando_max[30];
int  sound_play_wx_alert_message;
char sound_wx_alert_message[90];
