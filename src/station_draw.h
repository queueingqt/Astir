/*
 * station_draw.h -- drawing stations, trails and tracking.  See station_draw.c.
 *
 * These four were declared in db_gui.h, which meant a core object calling
 * display_station() included a GUI header to do it.  None of them contains a
 * Motif reference and they belong on this side of the boundary.
 *
 * It used to carry <X11/Intrinsic.h> for a Widget in these signatures, with a
 * note saying it could not be dropped until the font calls went through the
 * drawing layer.  Both have since happened -- the Widget strip removed the
 * parameter and xa_draw.h owns the font calls -- so the include went with them.
 * The note outlived the reason for it by two sessions.
 */

#ifndef XASTIR_STATION_DRAW_H
#define XASTIR_STATION_DRAW_H

#include "database.h"           // DataRow

void display_station(DataRow *p_station, int single);
void draw_trail(DataRow *fill, int solid);
void track_station(char *call_tracked, DataRow *p_station);
void set_map_position(long lat, long lon);

/*
 * Drawing every station, and the ruler.
 *
 * These were in db_gui.c and each took a Widget it never used -- display_file()
 * is 241 lines and names `w` only in its own signature.  They are the station
 * half of composing a frame, they contain no toolkit call at all, and a second
 * front end needs them, so they belong on this side.
 */
void display_file(void);
void draw_ruler(void);
void draw_ruler_text(char *text, long ofs);
void draw_test_line(long x, long y, long dx, long dy, long ofs);
void draw_range_scale(void);

#endif // XASTIR_STATION_DRAW_H
