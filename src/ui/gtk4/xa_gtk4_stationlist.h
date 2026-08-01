/*
 * Every station heard, as a list.
 *
 * The map answers "what is near here".  It cannot answer "where is K6ABC", and
 * hunting a callsign across a map with two hundred symbols on it is not a
 * search.  This is the other half: the same data, ordered, searchable, and
 * clickable through to the same station window.
 *
 * Nothing in the Motif build is being followed here.  Its station list was a
 * text widget rebuilt wholesale on a timer, which is exactly the shape this
 * front end has spent the day removing.
 */
#ifndef ASTIR_GTK4_STATIONLIST_H
#define ASTIR_GTK4_STATIONLIST_H

#include <gtk/gtk.h>

// Show the station list, or present the one already open.
void xa_gtk4_stationlist_show(GtkWindow *parent);

/*
 * A station was heard or changed.  Wired to the same xa_ui station_changed
 * callback the info window uses.
 *
 * Adds a row if the station is new, updates it in place if it is not, and asks
 * GTK to re-sort.  Never rebuilds: with a few hundred stations arriving several
 * times a second, a rebuild would destroy every row under the pointer
 * continuously.
 */
void xa_gtk4_stationlist_changed(const char *call_sign);

#endif /* ASTIR_GTK4_STATIONLIST_H */
