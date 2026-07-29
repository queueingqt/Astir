
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

/* Defines used throughout Xastir, mostly, but not exclusively, in maps. */

#define MAX_FILENAME 2000
#define MAX_CALLSIGN 9       // Objects are up to 9 chars
#define MAX_LONG             12
#define MAX_LAT              11

/* Malloc sanity checking macros used in many files */
#define CHECKMALLOC(m)  if (!m) { fprintf(stderr, "***** Malloc Failed *****\n"); exit(0); }
#define CHECKREALLOC(m)  if (!m) { fprintf(stderr, "***** Realloc Failed *****\n"); exit(0); }

// Latitude and longitude string formats.
#define CONVERT_HP_NORMAL       0
#define CONVERT_HP_NOSP         1
#define CONVERT_LP_NORMAL       2
#define CONVERT_LP_NOSP         3
#define CONVERT_DEC_DEG         4
#define CONVERT_UP_TRK          5
#define CONVERT_DMS_NORMAL      6
#define CONVERT_VHP_NOSP        7
#define CONVERT_DMS_NORMAL_FORMATED      8
#define CONVERT_HP_NORMAL_FORMATED       9

// Plain constants needed to define core state without pulling in main.h, which
// is not X-free.  main.h defines these too, with the same values; the guards
// keep either include order safe.  TRUE/FALSE otherwise arrive via X11.
#ifndef TRUE
  #define TRUE  1
#endif
#ifndef FALSE
  #define FALSE 0
#endif
#ifndef MAX_PHG
  #define MAX_PHG      8
#endif
#ifndef MAX_COMMENT
  #define MAX_COMMENT  80
#endif
#ifndef USE_DDMMMM
  #define USE_DDMMMM   1       // Default coordinate system, most APRS systems
#endif
#ifndef MAX_LINE_SIZE
  #define MAX_LINE_SIZE 512
#endif


typedef struct _selections
{
  int none;
  int mine;
  int tnc;
  int direct;
  int via_digi;
  int net;
  int tactical;
  int old_data;

  int stations;
  int fixed_stations;
  int moving_stations;
  int weather_stations;
  int CWOP_wx_stations;
  int objects;
  int weather_objects;
  int gauge_objects;
  int other_objects;
  int aircraft_objects;
  int vessel_objects;
} Selections;

typedef struct _what_to_display
{
  int callsign;
  int label_all_trackpoints;
  int symbol;
  int symbol_rotate;
  int trail;

  int course;
  int speed;
  int speed_short;
  int altitude;

  int weather;
  int weather_text;
  int temperature_only;
  int wind_barb;

  int aloha_circle;
  int ambiguity;
  int phg;
  int default_phg;
  int phg_of_moving;

  int df_data;
  int df_beamwidth_data;
  int df_bearing_data;
  int dr_data;
  int dr_arc;
  int dr_course;
  int dr_symbol;

  int dist_bearing;
  int last_heard;
} What_to_display;

/* Global variables defined in main.c */


extern char my_callsign[MAX_CALLSIGN+1];
extern char my_lat[MAX_LAT];
extern char my_long[MAX_LONG];
