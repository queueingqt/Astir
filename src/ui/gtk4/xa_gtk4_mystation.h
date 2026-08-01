/*
 * Who this station is: callsign, position, symbol and comment.
 *
 * The same window serves two jobs, because they ask for the same things.  On a
 * fresh install it appears by itself, because nothing else would: Astir would
 * otherwise come up as NOCALL at 0N 0E -- a point in the Gulf of Guinea that is
 * nobody's home and, worse, is the position every distance, every bearing and
 * every server filter would then be built around.  The program looks like it
 * works and is quietly useless.
 *
 * After that it lives on the menu, because these are settings and settings get
 * changed.  Astir has no general configuration window yet; this is the first
 * piece of one, and the piece that has to exist for the rest of the program to
 * mean anything.
 */
#ifndef ASTIR_GTK4_MYSTATION_H
#define ASTIR_GTK4_MYSTATION_H

#include <gtk/gtk.h>

/*
 * Is the configuration still the untouched default?
 *
 * True when the callsign has never been set, or the station has no position.
 * Deliberately not a "have I run before" flag in the config file: a flag can be
 * true while the settings it was supposed to guard are still empty, and then
 * the prompt never appears again for the one person who needed it.  Asking the
 * settings themselves cannot drift out of step with the settings themselves.
 */
int xa_gtk4_mystation_needed(void);

/*
 * Show the window.  Modal on parent; saves the config when dismissed.
 *
 * first_run adds the explanation and the button through to the interface
 * window -- a new operator has no reason to know that setting a position does
 * not, by itself, make any traffic arrive.
 */
void xa_gtk4_mystation_show(GtkWindow *parent, int first_run);

#endif /* ASTIR_GTK4_MYSTATION_H */
