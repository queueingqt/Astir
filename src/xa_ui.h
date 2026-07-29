/*
 * xa_ui.h -- the callbacks the core needs from whatever front end is running.
 *
 * Core extraction (Stage 4).  Almost all of the core's coupling to main.c was
 * shared *data*, and that has been moved to xa_state.h and xa_settings.h.  What
 * remains is a handful of places where core code needs to tell the user
 * something, which is a call into the GUI rather than a variable.
 *
 * Rather than let those calls keep main.c on the link line, the front end
 * registers them here.  The Motif build registers its Motif implementations at
 * startup; a GTK4 shell registers its own; a headless tool registers nothing
 * and the calls become no-ops.
 *
 * This header must never include an X11, Xt or Motif header.
 */

#ifndef XA_UI_H
#define XA_UI_H

typedef struct
{
  // Show a short progress or status message.  Called from map loading, the
  // interfaces and the station database -- roughly 45 sites.  May be called
  // very frequently during a redraw, so an implementation should be cheap.
  void (*status)(const char *text);
} xa_ui_callbacks;

// Install the front end's implementations.  Passing NULL, or leaving a member
// NULL, makes the corresponding call a no-op rather than a crash.
void xa_ui_set_callbacks(const xa_ui_callbacks *cb);

// Core-side entry point.  Safe to call before any front end has registered.
void xa_ui_status(const char *text);

#endif // XA_UI_H
