/*
 * Moving the map, for windows that are not the map.
 *
 * The station list wants "centre on this station", and will not be the last to
 * want something like it -- a search result, a message from a station, an
 * alert.  Each of those needs the view moved and the frame redrawn, and none of
 * them should be reaching into the main file's static rescale to do it.
 *
 * Small on purpose.  This is the view's interface to the rest of the front end,
 * not a general window manager: everything here changes where the map is
 * looking and nothing else.
 */
#ifndef ASTIR_GTK4_VIEW_H
#define ASTIR_GTK4_VIEW_H

/*
 * Put this position at the centre of the map and redraw.
 *
 * Coordinates are Astir units, the same ones a DataRow carries, so a caller
 * that has a station has what it needs without converting anything.
 */
void xa_gtk4_centre_on(long latitude, long longitude);

#endif /* ASTIR_GTK4_VIEW_H */
