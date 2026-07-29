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

/*
 * station_draw.c -- drawing stations, trails and tracking, from the database.
 *
 * Moved out of db_gui.c, where these four were the last thing the core needed
 * from that file.  They were never GUI code: 23k of iteration over DataRow
 * records emitting drawing calls, with exactly two toolkit references between
 * them -- XtWindow(da) to pick the screen as the drawing target, and
 * XQueryColor to find the brightness of a trail colour so the position dots
 * could contrast with it.  Those are now xa_screen_target() and
 * xa_color_rgb().
 *
 * set_map_position came along because the trio calls it and leaving it behind
 * would have kept the dependency alive.  A comment in location.c still says
 * "see also set_map_position() in db.c", so this is closer to where it started
 * than where it was found.
 *
 * The functions are unchanged apart from those two calls.  They keep their
 * Widget first parameter, which every caller satisfies with `da` and none of
 * them now reads -- removing it touches call sites in six files and is a
 * separate change.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif  // HAVE_CONFIG_H

#include "snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#include <X11/Intrinsic.h>

#include "xastir.h"
#include "globals.h"
#include "main.h"
#include "db_funcs.h"
#include "draw_symbols.h"
#include "maps.h"
#include "alert.h"
#include "util.h"
#include "color.h"
#include "xa_config.h"
#include "xa_settings.h"
#include "dr_utils.h"
#include "object_utils.h"
#include "ambiguity_utils.h"
#include "tactical_call_utils.h"

#include "xa_draw.h"

#include "station_draw.h"

#include "xa_ui.h"


#include "station_draw.h"

// Must be last include file
#include "leak_detection.h"



/*
 *  Change map position if necessary while tracking a station
 *      we call it with defined station call and position
 */
void track_station(char * UNUSED(call_tracked), DataRow *p_station)
{
  long x_ofs, y_ofs;
  long new_lat, new_lon;

  if ( is_tracked_station(p_station->call_sign) )     // We want to track this station
  {
    new_lat = p_station->coord_lat;                 // center map to station position as default
    new_lon = p_station->coord_lon;
    x_ofs = new_lon - center_longitude;            // current offset from screen center
    y_ofs = new_lat - center_latitude;
    if ((labs(x_ofs) > (screen_width*scale_x/3)) || (labs(y_ofs) > (screen_height*scale_y/3)))
    {
      // only redraw map if near border (margin 1/6 of screen at each side)
      if (labs(y_ofs) < (screen_height*scale_y/2))
      {
        new_lat  += y_ofs/2;  // give more space in driving direction
      }
      if (labs(x_ofs) < (screen_width*scale_x/2))
      {
        new_lon += x_ofs/2;
      }

      set_map_position(new_lat, new_lon);      // center map to new position

    }
    search_tracked_station(&p_station);
  }
}

/*
 *  Draw trail on screen.  If solid=1, draw type LineSolid, else
 *  draw type LineOnOffDash.
 *
 *  If label_all_trackpoints=1, add the callsign next to each
 *  trackpoint.  We may modify this and just add the callsign at the
 *  start/end of each new track segment.
 *
 */
void draw_trail(DataRow *fill, int solid)
{
  char short_dashed[2]  = {(char)1,(char)5};
  char medium_dashed[2] = {(char)5,(char)5};
  unsigned long lat0, lon0, lat1, lon1;        // trail segment points
  int col_trail, col_dot;
  unsigned short rgb_r, rgb_g, rgb_b;
  long brightness;
  char flag1;
  TrackRow *ptr;


  if (!ok_to_draw_station(fill))
  {
    return;
  }

  // Expire old trackpoints first.  We use the
  // remove-station-from-display time as the expire time for
  // trackpoints.  This can be set from the Configure->Defaults
  // dialog.
  expire_trail_points(fill, sec_clear);

  ptr = fill->newest_trackpoint;

  // Trail should have at least two points
  if ( (ptr != NULL) && (ptr->prev != NULL) )
  {
    int skip_dupes = 0; // Don't skip points first time through

    if (debug_level & 256)
    {
      fprintf(stderr,"draw_trail called for %s with %s.\n",
              fill->call_sign, (solid? "Solid" : "Non-Solid"));
    }

    col_trail = trail_colors[fill->trail_color];

    // define color of position dots in trail
    xa_color_rgb(col_trail, &rgb_r, &rgb_g, &rgb_b);

    brightness = (long)(0.3*rgb_r + 0.55*rgb_g + 0.15*rgb_b);
    if (brightness > 32000l)
    {
      col_dot = trail_colors[0x05];   // black dot on light trails
    }
    else
    {
      col_dot = trail_colors[0x06];   // white dot on dark trail
    }

    if (solid)
      // Used to be "JoinMiter" and "CapButt" below
    {
      xa_pen_line(gc, 3, XA_LINE_SOLID, XA_CAP_ROUND, XA_JOIN_ROUND);
    }
    else
    {
      // Another choice is LineDoubleDash
      xa_pen_line(gc, 3, XA_LINE_ON_OFF_DASH, XA_CAP_ROUND, XA_JOIN_ROUND);
      xa_pen_dashes(gc, 0, short_dashed, 2);
    }

    // Traverse linked list of trail points from newest to
    // oldest
    while ( (ptr != NULL) && (ptr->prev != NULL) )
    {
      lon0 = ptr->trail_long_pos;         // Trail segment start
      lat0 = ptr->trail_lat_pos;
      lon1 = ptr->prev->trail_long_pos;   // Trail segment end
      lat1 = ptr->prev->trail_lat_pos;
      flag1 = ptr->flag; // Are we at the start of a new trail?

      if ((flag1 & TR_NEWTRK) == '\0')
      {
        int lon0_screen, lat0_screen, lon1_screen, lat1_screen;

        // draw trail segment
        //
        xa_pen_color(gc, col_trail);
        draw_vector(
                    lon0,
                    lat0,
                    lon1,
                    lat1,
                    gc,
                    pixmap_final,
                    skip_dupes);

        // draw position point itself
        //
        xa_pen_color(gc, col_dot);
        draw_point(
                   lon0,
                   lat0,
                   gc,
                   pixmap_final,
                   skip_dupes);

        // Draw the callsign to go with the point if
        // label_all_trackpoints=1
        //
        if (Display_.callsign && Display_.label_all_trackpoints)
        {

          // Convert to screen coordinates
          lon0_screen = (lon0 - NW_corner_longitude) / scale_x;
          lat0_screen = (lat0 - NW_corner_latitude) / scale_y;

          // Convert to screen coordinates.
          lon1_screen = (lon1 - NW_corner_longitude) / scale_x;
          lat1_screen = (lat1 - NW_corner_latitude)  / scale_y;

          // The last position already gets its callsign
          // string drawn, plus that gets shifted based on
          // other parameters.  Draw both points of all
          // line segments except that one.  This will
          // result in strings getting drawn twice at
          // times, but they overlay on top of each other
          // so no big deal.
          //
          if (ptr != fill->newest_trackpoint)
          {

            draw_nice_string(
                             pixmap_final,
                             letter_style,
                             lon0_screen+10,
                             lat0_screen,
                             fill->call_sign,
                             0x08,
                             0x0f,
                             strlen(fill->call_sign));

            // If not same screen position as last drawn
            if (lon0_screen != lon1_screen
                && lat0_screen != lat1_screen)
            {

              draw_nice_string(
                               pixmap_final,
                               letter_style,
                               lon1_screen+10,
                               lat1_screen,
                               fill->call_sign,
                               0x08,
                               0x0f,
                               strlen(fill->call_sign));
            }
          }
        }
      }
      ptr = ptr->prev;
      skip_dupes = 1;
    }
    xa_pen_dashes(gc, 0, medium_dashed, 2);
  }
  else if (debug_level & 256)
  {
    fprintf(stderr,"Trail for %s does not contain 2 or more points.\n",
            fill->call_sign);
  }
}


// display_station
//
// single is 1 if the calling station wants to update only a
// single station.  If updating multiple stations in a row, then
// "single" will be passed to us as a zero.
//
// If current course/speed/altitude are absent, we check the last
// track point to try to snag those numbers.
//
void display_station(DataRow *p_station, int single)
{
  char temp_altitude[20];
  char temp_course[20];
  char temp_speed[20];
  char dr_speed[20];
  char temp_call[MAX_TACTICAL_CALL+1];
  char wx_tm[50];
  char temp_wx_temp[30];
  char temp_wx_wind[40];
  char temp_my_distance[20];
  char temp_my_course[20];
  char temp1_my_course[20];
  char temp2_my_gauge_data[50];
  time_t temp_sec_heard;
  int temp_show_last_heard;
  long l_lon, l_lat;
  char orient;
  float value;
  char tmp[7+1];
  int speed_ok = 0;
  int course_ok = 0;
  int wx_ghost = 0;
  Pixmap drawing_target;
  WeatherRow *weather = p_station->weather_data;
  time_t secs_now = sec_now();
  int ambiguity_flag;
  long ambiguity_coord_lon, ambiguity_coord_lat;
  size_t temp_len;


  if (debug_level & 128)
  {
    fprintf(stderr,"Display station (%s) called for Single=%d.\n", p_station->call_sign, single);
  }

  if (!ok_to_draw_station(p_station))
  {
    return;
  }

  // Set up call string for display
  if (Display_.callsign)
  {
    if (p_station->tactical_call_sign
        && p_station->tactical_call_sign[0] != '\0')
    {
      // Display tactical callsign instead if it has one
      // defined.
      xastir_snprintf(temp_call,
                      sizeof(temp_call),
                      "%s",
                      p_station->tactical_call_sign);
    }
    else
    {
      // Display normal callsign.
      xastir_snprintf(temp_call,
                      sizeof(temp_call),
                      "%s",
                      p_station->call_sign);
    }
  }
  else
  {
    temp_call[0] = '\0';
  }

  // Set up altitude string for display
  temp_altitude[0] = '\0';

  if (Display_.altitude)
  {
    // Check whether we have altitude in the current data
    if (strlen(p_station->altitude)>0)
    {
      // Found it in the current data
      xastir_snprintf(temp_altitude, sizeof(temp_altitude), "%.0f%s",
                      atof(p_station->altitude) * cvt_m2len, un_alt);
    }

    // Else check whether the previous position had altitude.
    // Note that newest_trackpoint if it exists should be the
    // same as the current data, so we have to go back one
    // further trackpoint.
    else if ( (p_station->newest_trackpoint != NULL)
              && (p_station->newest_trackpoint->prev != NULL) )
    {
      if ( p_station->newest_trackpoint->prev->altitude > -99999l)
      {
        // Found it in the tracklog
        xastir_snprintf(temp_altitude, sizeof(temp_altitude), "%.0f%s",
                        (float)(p_station->newest_trackpoint->prev->altitude * cvt_dm2len),
                        un_alt);

//                fprintf(stderr,"Trail data              with altitude: %s : %s\n",
//                    p_station->call_sign,
//                    temp_altitude);
      }
      else
      {
        //fprintf(stderr,"Trail data w/o altitude                %s\n",
        //    p_station->call_sign);
      }
    }
  }

  // Set up speed and course strings for display
  temp_speed[0] = '\0';
  dr_speed[0] = '\0';
  temp_course[0] = '\0';

  if (Display_.speed || Display_.dr_data)
  {
    // don't display 'fixed' stations speed and course.
    // Check whether we have speed in the current data and it's
    // >= 0.
    if ( (strlen(p_station->speed)>0) && (atof(p_station->speed) >= 0) )
    {
      speed_ok++;
      xastir_snprintf(tmp,
                      sizeof(tmp),
                      "%s",
                      un_spd);
      if (Display_.speed_short)
      {
        tmp[0] = '\0';  // without unit
      }

      xastir_snprintf(temp_speed, sizeof(temp_speed), "%.0f%s",
                      atof(p_station->speed)*cvt_kn2len,tmp);
    }
    // Else check whether the previous position had speed
    // Note that newest_trackpoint if it exists should be the
    // same as the current data, so we have to go back one
    // further trackpoint.
    else if ( (p_station->newest_trackpoint != NULL)
              && (p_station->newest_trackpoint->prev != NULL) )
    {

      xastir_snprintf(tmp,
                      sizeof(tmp),
                      "%s",
                      un_spd);

      if (Display_.speed_short)
      {
        tmp[0] = '\0';  // without unit
      }

      if ( p_station->newest_trackpoint->prev->speed > 0)
      {
        speed_ok++;

        xastir_snprintf(temp_speed, sizeof(temp_speed), "%.0f%s",
                        p_station->newest_trackpoint->prev->speed * cvt_hm2len,
                        tmp);
      }
    }
  }

  if (Display_.course || Display_.dr_data)
  {
    // Check whether we have course in the current data
    if ( (strlen(p_station->course)>0) && (atof(p_station->course) > 0) )
    {
      course_ok++;
      xastir_snprintf(temp_course, sizeof(temp_course), "%.0f\xB0",
                      atof(p_station->course));
    }
    // Else check whether the previous position had a course
    // Note that newest_trackpoint if it exists should be the
    // same as the current data, so we have to go back one
    // further trackpoint.
    else if ( (p_station->newest_trackpoint != NULL)
              && (p_station->newest_trackpoint->prev != NULL) )
    {
      if( p_station->newest_trackpoint->prev->course > 0 )
      {
        course_ok++;
        xastir_snprintf(temp_course, sizeof(temp_course), "%.0f\xB0",
                        (float)p_station->newest_trackpoint->prev->course);
      }
    }
  }

  // Save the speed into the dr string for later
  xastir_snprintf(dr_speed,
                  sizeof(dr_speed),
                  "%s",
                  temp_speed);

  if (!speed_ok  || !Display_.speed)
  {
    temp_speed[0] = '\0';
  }

  if (!course_ok || !Display_.course)
  {
    temp_course[0] = '\0';
  }

  // Set up distance and bearing strings for display
  temp_my_distance[0] = '\0';
  temp_my_course[0] = '\0';

  if (Display_.dist_bearing && strcmp(p_station->call_sign,my_callsign) != 0)
  {
    l_lat = convert_lat_s2l(my_lat);
    l_lon = convert_lon_s2l(my_long);

    // Get distance in nautical miles, convert to current measurement standard
    value = cvt_kn2len * calc_distance_course(l_lat,l_lon,
            p_station->coord_lat,p_station->coord_lon,temp1_my_course,sizeof(temp1_my_course));

    if (value < 5.0)
    {
      sprintf(temp_my_distance,"%0.1f%s",value,un_dst);
    }
    else
    {
      sprintf(temp_my_distance,"%0.0f%s",value,un_dst);
    }

    xastir_snprintf(temp_my_course, sizeof(temp_my_course), "%.0f\xB0",
                    atof(temp1_my_course));
  }

  // Set up weather strings for display
  temp_wx_temp[0] = '\0';
  temp_wx_wind[0] = '\0';

  if (weather != NULL)
  {
    // wx_ghost = 1 if the weather data is too old to display
    wx_ghost = (int)(((sec_old + weather->wx_sec_time)) < secs_now);
  }

  if (Display_.weather
      && Display_.weather_text
      && weather != NULL      // We have weather data
      && !wx_ghost)           // Weather is current, display it
  {

    if (strlen(weather->wx_temp) > 0)
    {
      xastir_snprintf(tmp,
                      sizeof(tmp),
                      "T:");
      if (Display_.temperature_only)
      {
        tmp[0] = '\0';
      }

      if (english_units)
        xastir_snprintf(temp_wx_temp, sizeof(temp_wx_temp), "%s%.0f\xB0%s",
                        tmp, atof(weather->wx_temp),"F ");
      else
        xastir_snprintf(temp_wx_temp, sizeof(temp_wx_temp), "%s%.0f\xB0%s",
                        tmp,((atof(weather->wx_temp)-32.0)*5.0)/9.0,"C ");
    }

    if (!Display_.temperature_only)
    {
      if (strlen(weather->wx_hum) > 0)
      {
        xastir_snprintf(wx_tm, sizeof(wx_tm), "H:%.0f%%", atof(weather->wx_hum));
        strncat(temp_wx_temp,
                wx_tm,
                sizeof(temp_wx_temp) - 1 - strlen(temp_wx_temp));
      }

      if (strlen(weather->wx_speed) > 0)
      {
        xastir_snprintf(temp_wx_wind, sizeof(temp_wx_wind), "S:%.0f%s ",
                        atof(weather->wx_speed)*cvt_mi2len,un_spd);
      }

      if (strlen(weather->wx_gust) > 0)
      {
        xastir_snprintf(wx_tm, sizeof(wx_tm), "G:%.0f%s ",
                        atof(weather->wx_gust)*cvt_mi2len,un_spd);
        strncat(temp_wx_wind,
                wx_tm,
                sizeof(temp_wx_wind) - 1 - strlen(temp_wx_wind));
      }

      if (strlen(weather->wx_course) > 0)
      {
        xastir_snprintf(wx_tm, sizeof(wx_tm), "C:%.0f\xB0", atof(weather->wx_course));
        strncat(temp_wx_wind,
                wx_tm,
                sizeof(temp_wx_wind) - 1 - strlen(temp_wx_wind));
      }

      temp_len = strlen(temp_wx_wind);
      if ((temp_len > 0) && (temp_wx_wind[temp_len-1] == ' '))
      {
        temp_wx_wind[temp_len-1] = '\0';  // delete blank at EOL
      }
    }

    temp_len = strlen(temp_wx_temp);
    if ((temp_len > 0) && (temp_wx_temp[strlen(temp_wx_temp)-1] == ' '))
    {
      temp_wx_temp[temp_len-1] = '\0';  // delete blank at EOL
    }
  }


  (void)remove_trailing_asterisk(p_station->call_sign);  // DK7IN: is this needed here?

  if (Display_.symbol_rotate)
  {
    orient = symbol_orient(p_station->course);  // rotate symbol
  }
  else
  {
    orient = ' ';
  }

  // Prevents my own call from "ghosting"?
  //    temp_sec_heard = (strcmp(p_station->call_sign, my_callsign) == 0) ? secs_now: p_station->sec_heard;
  temp_sec_heard = (is_my_station(p_station)) ? secs_now : p_station->sec_heard;

  // Check whether it's a locally-owned object/item
//    if ( (is_my_call(p_station->origin,1))          // If station is owned by me (including SSID)
//            && ( (p_station->flag & ST_OBJECT)      // And it's an object
//              || (p_station->flag & ST_ITEM) ) ) {  // or an item
//    if ( is_my_object_item(p_station) ) {
//        temp_sec_heard = secs_now; // We don't want our own objects/items to "ghost"
//    }

  // Show last heard times only for others stations and their
  // objects/items.
  //    temp_show_last_heard = (strcmp(p_station->call_sign, my_callsign) == 0) ? 0 : Display_.last_heard;
  temp_show_last_heard = (is_my_station(p_station)) ? 0 : Display_.last_heard;



  //------------------------------------------------------------------------------------------

  // If we're only planning on updating a single station at this time, we go
  // through the drawing calls twice, the first time drawing directly onto
  // the screen.
  if (!pending_ID_message && single)
  {
    drawing_target = xa_screen_target();
  }
  else
  {
    drawing_target = pixmap_final;
  }

  //_do_the_drawing:

  // Check whether it's a locally-owned object/item
//    if ( (is_my_call(p_station->origin,1))                  // If station is owned by me (including SSID)
//            && ( (p_station->flag & ST_OBJECT)       // And it's an object
//              || (p_station->flag & ST_ITEM  ) ) ) { // or an item
//    if ( is_my_object_item(p_station) ) {
//        temp_sec_heard = secs_now; // We don't want our own objects/items to "ghost"
  // This isn't quite right since if it's a moving object, passing an incorrect
  // sec_heard should give the wrong results.
//    }

  ambiguity_flag = 0; // Default

  if (Display_.ambiguity && p_station->pos_amb)
  {
    ambiguity_flag = 1;
    draw_ambiguity(p_station->coord_lon,
                   p_station->coord_lat,
                   p_station->pos_amb,
                   &ambiguity_coord_lon, // New longitude may get passed back to us
                   &ambiguity_coord_lat, // New latitude may get passed back to us
                   temp_sec_heard,
                   drawing_target);
  }

  // Check for DF'ing data, draw DF circles if present and enabled
  if (Display_.df_data && strlen(p_station->signal_gain) == 7)    // There's an SHGD defined
  {
    //fprintf(stderr,"SHGD:%s\n",p_station->signal_gain);
    draw_DF_circle( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                    (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                    p_station->signal_gain,
                    temp_sec_heard,
                    drawing_target);
  }

  // Check for DF'ing beam heading/NRQ data
  if (Display_.df_data && (strlen(p_station->bearing) == 3) && (strlen(p_station->NRQ) == 3))
  {
    //fprintf(stderr,"Bearing: %s\n",p_station->signal_gain,NRQ);
    if (p_station->df_color == -1)
    {
      p_station->df_color = rand() % MAX_TRAIL_COLORS;
    }

    draw_bearing( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                  (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                  p_station->course,
                  p_station->bearing,
                  p_station->NRQ,
                  trail_colors[p_station->df_color],
                  Display_.df_beamwidth_data, Display_.df_bearing_data,
                  temp_sec_heard,
                  drawing_target);
  }

  // Check whether to draw dead-reckoning data by KJ5O
  if (Display_.dr_data
      && ( (p_station->flag & ST_MOVING)
           //        && (p_station->newest_trackpoint!=0
           && course_ok
           && speed_ok
           && scale_y < 8000
           && atof(dr_speed) > 0) )
  {

    // Does it make sense to try to do dead-reckoning on an
    // object that has position ambiguity enabled?  I don't
    // think so!
    //
    if ( ! ambiguity_flag && ( (secs_now-temp_sec_heard) < dead_reckoning_timeout) )
    {

      draw_deadreckoning_features(p_station,
                                  drawing_target);
    }
  }

  if (p_station->aprs_symbol.area_object.type != AREA_NONE)
  {
    draw_area( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
               (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
               p_station->aprs_symbol.area_object.type,
               p_station->aprs_symbol.area_object.color,
               p_station->aprs_symbol.area_object.sqrt_lat_off,
               p_station->aprs_symbol.area_object.sqrt_lon_off,
               p_station->aprs_symbol.area_object.corridor_width,
               temp_sec_heard,
               drawing_target);
  }


  // Draw additional stuff if this is the tracked station
  if (is_tracked_station(p_station->call_sign))
  {
    //WE7U
    draw_pod_circle( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                     (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                     0.0020 * scale_y,
                     colors[0x0e],   // Yellow
                     drawing_target,
                     temp_sec_heard);
    draw_pod_circle( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                     (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                     0.0023 * scale_y,
                     colors[0x44],   // Red
                     drawing_target,
                     temp_sec_heard);
    draw_pod_circle( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                     (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                     0.0026 * scale_y,
                     colors[0x61],   // Blue
                     drawing_target,
                     temp_sec_heard);
  }


  // Draw additional stuff if this is a storm and the weather data
  // is not too old to display.
  if ( (weather != NULL) && weather->wx_storm && !wx_ghost )
  {
    char temp[4];


    //fprintf(stderr,"Plotting a storm symbol:%s:%s:%s:\n",
    //    weather->wx_hurricane_radius,
    //    weather->wx_trop_storm_radius,
    //    weather->wx_whole_gale_radius);

    // Still need to draw the circles in different colors for the
    // different ranges.  Might be nice to tint it as well.

    xastir_snprintf(temp,
                    sizeof(temp),
                    "%s",
                    weather->wx_hurricane_radius);

    if ( (temp[0] != '\0') && (strncmp(temp,"000",3) != 0) )
    {

      draw_pod_circle( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                       (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                       atof(temp) * 1.15078, // nautical miles to miles
                       colors[0x44],   // Red
                       drawing_target,
                       temp_sec_heard);
    }

    xastir_snprintf(temp,
                    sizeof(temp),
                    "%s",
                    weather->wx_trop_storm_radius);

    if ( (temp[0] != '\0') && (strncmp(temp,"000",3) != 0) )
    {
      draw_pod_circle( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                       (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                       atof(temp) * 1.15078, // nautical miles to miles
                       colors[0x0e],   // Yellow
                       drawing_target,
                       temp_sec_heard);
    }

    xastir_snprintf(temp,
                    sizeof(temp),
                    "%s",
                    weather->wx_whole_gale_radius);

    if ( (temp[0] != '\0') && (strncmp(temp,"000",3) != 0) )
    {
      draw_pod_circle( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                       (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                       atof(temp) * 1.15078, // nautical miles to miles
                       colors[0x0a],   // Green
                       drawing_target,
                       temp_sec_heard);
    }
  }


  // Draw wind barb if selected and we have wind, but not a severe
  // storm (wind barbs just confuse the matter).
  if (Display_.weather && Display_.wind_barb
      && weather != NULL && atoi(weather->wx_speed) >= 5
      && !weather->wx_storm
      && !wx_ghost )
  {
    draw_wind_barb( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                    (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                    weather->wx_speed,
                    weather->wx_course,
                    temp_sec_heard,
                    drawing_target);
  }


  // WE7U
  //
  // Draw truncation/rounding rectangles plus error ellipses.
  //
  //
  // We need to keep track of ellipse northing/easting radii plus
  // rectangle northing/easting offsets.  If both sets are present
  // we'll need to draw the summation of both geometric figures.
  // Check that the math works at/near the poles.  We may need to keep
  // track of truncation/rounding rectangles separately if some
  // devices or software use one method, some the other.
  //
  if (!ambiguity_flag)
  {

    // Check whether we're at a close enough zoom level to have
    // the ellipses/rectangles be visible, else skip drawing for
    // efficiency.
    //
    //fprintf(stderr,"scale_y: %ld\t", scale_y);
    if (scale_y < 17)   // 60' figures are good out to about zoom 16
    {

      // Here we may have to check what type of device is being used (if
      // possible to determine) to decide whether to draw a truncation/
      // rounding rectangles or GPS error ellipses.  Truncation rectangles
      // have the symbol at one corner, rounding have it in the middle.
      // Based on the precision inherent in the packet we wish to draw a
      // GPS error ellipse instead, the decision point is when the packet
      // precision is adequate to show ~6 meters.
      //
      // OpenTracker APRS:  Truncation, rectangle
      // OpenTracker Base91:Truncation, ellipse
      // OpenTracker OpenTrac: Truncation, ellipse
      // TinyTrak APRS:     Truncation, rectangle
      // TinyTrak NMEA:     Truncation, ellipse/rectangle based on precision
      // TinyTrak Mic-E:    Truncation, rectangle
      // GPGGA:             Truncation, ellipse/rectangle based on precision/HDOP/Augmentation
      // GPRMC:             Truncation, ellipse/rectangle based on precision
      // GPGLL:             Truncation, ellipse/rectangle based on precision
      // Xastir APRS:       Truncation, rectangle
      // Xastir Base91:     Truncation, ellipse
      // UI-View APRS:      ??, rectangle
      // UI-View Base91:    ??, ellipse
      // APRS+SA APRS:      ??, rectangle
      // APRS+SA Base91:    ??, ellipse
      // PocketAPRS:        ??, rectangle
      // SmartAPRS:         ??, rectangle
      // HamHUD:            Truncation, ??
      // HamHUD GPRMC:      Truncation, ellipse/rectangle based on precision
      // Linksys NSLU2:     ??, rectangle
      // AGW Tracker:       ??, ??
      // APRSPoint:         ??, rectangle
      // APRSce:            ??, rectangle
      // APRSdos APRS:      ??, rectangle
      // APRSdos Base91:    ??, ellipse
      // BalloonTrack:      ??, ??
      // DMapper:           ??, ??
      // JavAPRS APRS:      ??, rectangle
      // JavAPRS Base91:    ??, ellipse
      // WinAPRS APRS:      ??, rectangle
      // WinAPRS Base91:    ??, ellipse
      // MacAPRS APRS:      ??, rectangle
      // MacAPRS Base91:    ??, ellipse
      // MacAPRSOSX APRS:   ??, rectangle
      // MacAPRSOSX Base91: ??, ellipse
      // X-APRS APRS:       ??, rectangle
      // X-APRS Base91:     ??, ellipse
      // OziAPRS:           ??, rectangle
      // NetAPRS:           ??, rectangle
      // APRS SCS:          ??, ??
      // RadioMobile:       ??, rectangle
      // KPC-3:             ??, rectangle
      // MicroTNC:          ??, rectangle
      // TigerTrak:         ??, rectangle
      // PicoPacket:        ??, rectangle
      // MIM:               ??, rectangle
      // Mic-Encoder:       ??, rectangle
      // Pic-Encoder:       ??, rectangle
      // Generic Mic-E:     ??, rectangle
      // D7A/D7E:           ??, rectangle
      // D700A:             ??, rectangle
      // Alinco DR-135:     ??, rectangle
      // Alinco DR-620:     ??, rectangle
      // Alinco DR-635:     ??, rectangle
      // Other:             ??, ??


      // Initial try at drawing the error_ellipse_radius
      // circles around the posit.  error_ellipse_radius is in
      // centimeters.  Convert from cm to miles for
      // draw_pod_circle().
      //
      /*
                  draw_pod_circle( p_station->coord_lon,
                                   p_station->coord_lat,
                                   p_station->error_ellipse_radius / 100000.0 * 0.62137, // cm to mi
                                   colors[0x0f],  // White
                                   drawing_target,
                                   temp_sec_heard);
      */
      draw_precision_rectangle( p_station->coord_lon,
                                p_station->coord_lat,
                                p_station->error_ellipse_radius, // centimeters (not implemented yet)
                                p_station->lat_precision, // 100ths of seconds latitude
                                p_station->lon_precision, // 100ths of seconds longitude
                                colors[0x0f],  // White
                                drawing_target);


      // Perhaps draw vectors from the symbol out to the borders of these
      // odd figures?  Draw an outline without vectors to the symbol?
      // Have the color match the track color assigned to that station so
      // the geometric figures can be kept separate from nearby stations?
      //
      // draw_truncation_rectangle + error_ellipse (symbol at corner)
      // draw_rounding_rectangle + error_ellipse (symbol in middle)

    }
  }

  // Zero out the variable in case we don't use it below.
  temp2_my_gauge_data[0] = '\0';

  // If an H2O object, create a timestamp + last comment variable
  // (which should contain gage-height and/or water-flow numbers)
  // for use in the draw_symbol() function below.
  if (p_station->aprs_symbol.aprs_type == '/'
      && p_station->aprs_symbol.aprs_symbol == 'w'
      && (   p_station->flag & ST_OBJECT    // And it's an object
             || p_station->flag & ST_ITEM) )   // or an item
  {

    // NOTE:  Also check whether it was sent by the Firenet GAGE
    // script??  "GAGE-*"

    // NOTE:  Check most recent comment time against
    // p_station->sec_heard.  If they don't match, don't display the
    // comment.  This will make sure that older comment data doesn't get
    // displayed which can be quite misleading for stream gauges.

    // Check whether we have any comment data at all.  If so,
    // the first one will be the most recent comment and the one
    // we wish to display.
    if (p_station->comment_data != NULL)
    {
      CommentRow *ptr;
//            time_t sec;
//            struct tm *time;


      ptr = p_station->comment_data;

      // Check most recent comment's sec_heard time against
      // the station record's sec_heard time.  If they don't
      // match, don't display the comment.  This will make
      // sure that older comment data doesn't get displayed
      // which can be quite misleading for stream gauges.
      if (p_station->sec_heard == ptr->sec_heard)
      {

        // Note that text_ptr can be an empty string.
        // That's ok.

        // Also print the sec_heard timestamp so we know
        // when this particular gauge data was received
        // (Very important!).
//                sec = ptr->sec_heard;
//                time = localtime(&sec);

        xastir_snprintf(temp2_my_gauge_data,
                        sizeof(temp2_my_gauge_data),
                        "%s",
//                    "%02d/%02d %02d:%02d %s",
//                    time->tm_mon + 1,
//                    time->tm_mday,
//                    time->tm_hour,
//                    time->tm_min,
                        ptr->text_ptr);
//fprintf(stderr, "%s\n", temp2_my_gauge_data);
      }
    }
  }

  draw_symbol(
              p_station->aprs_symbol.aprs_type,
              p_station->aprs_symbol.aprs_symbol,
              p_station->aprs_symbol.special_overlay,
              (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
              (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
              temp_call,
              temp_altitude,
              temp_course,    // ??
              temp_speed,     // ??
              temp_my_distance,
              temp_my_course,
              // Display only if wx temp is current
              (wx_ghost) ? "" : temp_wx_temp,
              // Display only if wind speed is current
              (wx_ghost) ? "" : temp_wx_wind,
              temp_sec_heard,
              temp_show_last_heard,
              drawing_target,
              orient,
              p_station->aprs_symbol.area_object.type,
              p_station->signpost,
              temp2_my_gauge_data,
              1); // Increment "currently_selected_stations"

  // If it's a Waypoint symbol, draw a line from it to the
  // transmitting station.
  if (p_station->aprs_symbol.aprs_type == '\\'
      && p_station->aprs_symbol.aprs_symbol == '/')
  {

    draw_WP_line(p_station,
                 ambiguity_flag,
                 ambiguity_coord_lon,
                 ambiguity_coord_lat,
                 drawing_target);
  }

  // Draw other points associated with the station, if any.
  // KG4NBB
  if (debug_level & 128)
  {
    fprintf(stderr,"  Number of multipoints = %d\n",p_station->num_multipoints);
  }
  if (p_station->num_multipoints != 0)
  {
    draw_multipoints( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                      (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                      p_station->num_multipoints,
                      p_station->multipoint_data->multipoints,
                      p_station->type, p_station->style,
                      temp_sec_heard,
                      drawing_target);
  }

  temp_sec_heard = p_station->sec_heard;    // DK7IN: ???

  if (Display_.phg
      && (!(p_station->flag & ST_MOVING) || Display_.phg_of_moving))
  {

    // Check for Map View "eyeball" symbol
    if ( strncmp(p_station->power_gain,"RNG",3) == 0
         && p_station->aprs_symbol.aprs_type == '/'
         && p_station->aprs_symbol.aprs_symbol == 'E' )
    {
      // Map View "eyeball" symbol.  Don't draw the RNG ring
      // for it.
    }
    else if (strlen(p_station->power_gain) == 7)
    {
      // Station has PHG or RNG defined
      //
      draw_phg_rng( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                    (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                    p_station->power_gain,
                    temp_sec_heard,
                    drawing_target);
    }
    else if (Display_.default_phg && !(p_station->flag & (ST_OBJECT | ST_ITEM)))
    {
      // No PHG defined and not an object/item.  Display a PHG
      // of 3130 as default as specified in the spec:  9W, 3dB
      // omni at 20 feet = 6.2 mile PHG radius.
      //
      draw_phg_rng( (ambiguity_flag) ? ambiguity_coord_lon : p_station->coord_lon,
                    (ambiguity_flag) ? ambiguity_coord_lat : p_station->coord_lat,
                    "PHG3130",
                    temp_sec_heard,
                    drawing_target);
    }
  }


  // Draw minimum proximity circle?
  if (p_station->probability_min[0] != '\0')
  {
    double range = atof(p_station->probability_min);

    // Draw red circle
    draw_pod_circle(p_station->coord_lon,
                    p_station->coord_lat,
                    range,
                    colors[0x44],
                    drawing_target,
                    temp_sec_heard);
  }

  // Draw maximum proximity circle?
  if (p_station->probability_max[0] != '\0')
  {
    double range = atof(p_station->probability_max);

    // Draw red circle
    draw_pod_circle(p_station->coord_lon,
                    p_station->coord_lat,
                    range,
                    colors[0x44],
                    drawing_target,
                    temp_sec_heard);
  }

  // DEBUG STUFF
  //            draw_pod_circle(x_long, y_lat, 1.5, colors[0x44], where);
  //            draw_pod_circle(x_long, y_lat, 3.0, colors[0x44], where);


  // Now if we just did the single drawing, we want to go back and draw
  // the same things onto pixmap_final so that when we do update from it
  // to the screen all of the stuff will be there.
//    if (drawing_target == XtWindow(da)) {
//        drawing_target = pixmap_final;
//        goto _do_the_drawing;
//    }
}

/*
 *  Center map to new position
 */
void set_map_position(long lat, long lon)
{
  // see also map_pos() in location.c

  // Set interrupt_drawing_now because conditions have changed
  // (new map center).
  interrupt_drawing_now++;

  set_last_position();
  center_latitude  = lat;
  center_longitude = lon;
  setup_in_view();  // flag all stations in new screen view

  // Request that a new image be created.  Calls create_image,
  // XCopyArea, and display_zoom_status.
  request_new_image++;

  //    if (create_image(w)) {
  //        (void)XCopyArea(XtDisplay(w),pixmap_final,XtWindow(w),gc,0,0,(unsigned int)screen_width,(unsigned int)screen_height,0,0);
  //    }
}