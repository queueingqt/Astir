/*
 * station_draw.h -- drawing stations, trails and tracking.  See station_draw.c.
 *
 * These four were declared in db_gui.h, which meant a core object calling
 * display_station() included a GUI header to do it.  None of them contains a
 * Motif reference and they belong on this side of the boundary.
 *
 * Unlike xa_ui.h, xa_state.h and xa_settings.h, this header is NOT toolkit-free,
 * and it is worth being explicit about why rather than quietly declaring the
 * parameter void *.  The Widget in these signatures is not decoration:
 * display_station() passes it to draw_symbol(), which calls
 * XQueryFont(XtDisplay(w)) to measure text.  Every caller supplies the drawing
 * area and nothing else would work, so the argument is effectively a constant --
 * but it cannot be dropped, or narrowed, until the font calls go through the
 * drawing layer.  That is the same blocker that stops maps.c and
 * draw_symbols.c being split.
 *
 * So: Xt only, no Motif, and a note to come back here when fonts are done.
 */

#ifndef XASTIR_STATION_DRAW_H
#define XASTIR_STATION_DRAW_H

#include <X11/Intrinsic.h>      // Widget; see the note above

#include "database.h"           // DataRow

void display_station(Widget w, DataRow *p_station, int single);
void draw_trail(Widget w, DataRow *fill, int solid);
void track_station(Widget w, char *call_tracked, DataRow *p_station);
void set_map_position(Widget w, long lat, long lon);

#endif // XASTIR_STATION_DRAW_H
