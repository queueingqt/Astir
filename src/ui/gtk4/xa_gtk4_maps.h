/*
 * Choosing which maps are drawn.
 *
 * The indexer walks the map directory and records every map it finds, with its
 * bounds and its layer.  Nothing has ever shown that index, so the only way to
 * change which maps appear has been to edit ~/.astir/config/selected_maps.sys
 * by hand and know that the order of the lines is the drawing order.  For an
 * application whose window is a map, that is the largest remaining gap.
 */
#ifndef ASTIR_GTK4_MAPS_H
#define ASTIR_GTK4_MAPS_H

#include <gtk/gtk.h>

/*
 * Show the map chooser, or present the one already open.
 *
 * One window: it holds pending changes that have not been saved, and a second
 * copy would be a second set of pending changes against the same index.
 */
void xa_gtk4_maps_show(GtkWindow *parent);

#endif /* ASTIR_GTK4_MAPS_H */
