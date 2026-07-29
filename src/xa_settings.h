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
extern int smart_beaconing;  // Master enable/disable for SmartBeaconing(tm) mode
extern int sb_POSIT_rate;  // Computed SmartBeaconing(tm) posit rate (secs)
extern int sb_last_heading;  // Heading at time of last posit
extern int sb_current_heading;  // Most recent heading parsed from GPS sentence
extern int sb_turn_min;  // Min threshold for corner pegging (degrees)
extern int sb_turn_slope;  // Threshold slope for corner pegging (degrees/mph)
extern int sb_turn_time;  // Time between other beacon & turn beacon (secs)
extern int sb_posit_fast;  // Fast beacon rate (secs)
extern int sb_posit_slow;  // Slow beacon rate (mins)
extern int sb_low_speed_limit;  // Speed below which SmartBeaconing(tm) is disabled &
extern int sb_high_speed_limit;  // Speed above which we'll beacon at the
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
extern char LOGFILE_TNC[400];
extern char LOGFILE_NET[400];
extern char LOGFILE_IGATE[400];
extern char LOGFILE_MESSAGE[400];
extern char LOGFILE_WX[400];
extern char LOGFILE_WX_ALERT[400];
extern char sound_command[90];
extern int sound_play_new_station;
extern char sound_new_station[90];
extern int sound_play_new_message;
extern char sound_new_message[90];
extern int sound_play_prox_message;
extern char sound_prox_message[90];
extern char prox_min[30];
extern char prox_max[30];
extern int sound_play_band_open_message;
extern char sound_band_open_message[90];
extern char bando_min[30];
extern char bando_max[30];
extern int sound_play_wx_alert_message;
extern char sound_wx_alert_message[90];

#endif // XA_SETTINGS_H
