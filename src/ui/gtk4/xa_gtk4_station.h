/*
 * What Astir knows about one station.
 *
 * Clicking a station on the map did nothing at all, which for a program whose
 * whole purpose is showing where stations are is a conspicuous hole: the map
 * could tell you something existed and then refuse to say anything about it.
 *
 * The Motif build answered this with Station_info() in db_gui.c.  That file is
 * archived and did not come across, so this is written for GTK4 rather than
 * ported -- but it shows the same things, because they are the things a
 * DataRow holds.
 */
#ifndef ASTIR_GTK4_STATION_H
#define ASTIR_GTK4_STATION_H

#include <gtk/gtk.h>

#include "core/aprs/database.h"

/*
 * Show what is known about a station, or present the window already showing it.
 *
 * Looked up by callsign rather than held as a pointer: a DataRow can be expired
 * and freed by the station timeout while its window is still open, and a window
 * holding the pointer would then be reading freed memory.  The callsign is
 * stable, so the window re-finds the row each time it refreshes and closes
 * itself if the station has gone.
 */
void xa_gtk4_station_show(GtkWindow *parent, const char *callsign);

/*
 * Remember that something happened involving a station, for the status toast's
 * history.  `callsign` may be NULL for a message that is not about one.
 */
void xa_gtk4_station_note(const char *text, const char *callsign);

/*
 * The core heard a station: refresh the window if it is the one on display.
 *
 * Wired to xa_ui's station_changed callback.  This is what the window runs on
 * instead of a timer -- it fires exactly when the data changes and at no other
 * time, so nothing is redrawn under the pointer for no reason.
 */
void xa_gtk4_station_changed(const char *call_sign);

// The history, newest first.  Returns how many entries were written.
int xa_gtk4_station_history(const char **text, const char **callsign, int max);

#endif /* ASTIR_GTK4_STATION_H */
