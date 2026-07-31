/*
 * The interface control window: what Astir is connected to, and whether it is
 * actually up.
 *
 * Everything underneath this has been in the core all along -- devices[] holds
 * the configuration, add_device() opens a port, startup_all_or_defined_port()
 * brings one up and get_device_status() says whether it worked.  What was
 * missing was any way for a person to reach any of it, which made a program
 * that talks to radios unable to talk to a radio.
 *
 * Kept in its own file rather than added to xa_gtk4_main.c because it is a
 * window with its own state and its own refresh, and the main file is already
 * the map, the gestures, the menus and the render scheduler.
 */
#ifndef ASTIR_GTK4_INTERFACES_H
#define ASTIR_GTK4_INTERFACES_H

#include <gtk/gtk.h>

/*
 * Show the interface control window, or present the existing one.
 *
 * One window, not one per invocation: it polls device status on a timer, and
 * two of them would be two timers reporting the same thing.
 */
void xa_gtk4_interfaces_show(GtkWindow *parent);

/*
 * Open the add-interface dialog directly, for a screenshot.
 *
 * This session has no input automation, so a dialog reached by pressing a
 * button cannot be got on screen to be looked at unless the application opens
 * it -- the same reason the menu popover has a hook.  Not reachable from the
 * interface: nothing calls it but the ASTIR_GTK4_SHOW_INTERFACES=add hook.
 */
void xa_gtk4_interfaces_show_add(GtkWindow *parent);

/*
 * A port opened, closed or changed state.  Wired to xa_ui's interfaces_changed
 * callback, which the core has been announcing all along with nothing
 * listening.  Replaces the half-second poll this window used to run on.
 */
void xa_gtk4_interfaces_changed(void);

#endif /* ASTIR_GTK4_INTERFACES_H */
