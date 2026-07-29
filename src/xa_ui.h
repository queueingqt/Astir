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

  // Give the front end a chance to process pending input.  The map drawing
  // code calls this every 64 shapes and between map files so that a pan or
  // zoom can interrupt a slow redraw -- see interrupt_drawing_now.  It is the
  // reason core objects referenced the Xt application context at all.
  void (*pump_events)(void);

  // Say that something slow has started, so the front end can show whatever
  // it uses for that.  There is no matching "not busy": the Motif shell
  // clears the cursor from an idle work procedure, and a front end that
  // wanted an explicit end would need a different shape than this.
  //
  // Core callers never chose a widget -- all five sites passed the main
  // window -- so nothing is passed here.  That is what makes it a callback
  // rather than a wrapper.
  void (*busy)(void);

  // Make pending drawing visible now, instead of waiting for the front end to
  // return to its event loop.  Called from the map drivers around slow work --
  // a tile download, a long shapefile -- so the status message and the partly
  // drawn map do not sit invisible until the load finishes.
  //
  // Distinct from status() on purpose: status() runs ~45 times per redraw and
  // has to stay cheap, so it must not force a repaint.  Flushing is the
  // deliberate, occasional case.
  void (*flush)(void);

  // Report something wrong that is not worth aborting for.  One caller: a map
  // file whose point count overflows the fixed vertex buffer, which is then
  // clamped and drawn anyway.
  void (*warn)(const char *text);

  // Release whatever the front end cached in map_index_record.ui_label.
  //
  // The core owns those records -- it builds the map index and frees it -- but
  // only the front end knows what it stored there, so only it can release it.
  // A front end that caches nothing leaves the field NULL and needs no
  // implementation.  Never called with NULL.
  void (*free_label)(void *label);

  // Open a window for composing a message to a station.  Called when a message
  // arrives and popups are enabled -- the core decides that a conversation has
  // started, the front end decides what a conversation looks like.
  //
  // to_call is the callsign, prefixed with '*' for a group message.  Read only;
  // the front end must copy anything it keeps.
  void (*open_message_window)(const char *to_call);

  // Redraw the map now: recomposite the layers, draw the stations, present.
  // Called from core code that has changed what should be on screen and cannot
  // wait for the next natural redraw -- loading CAD objects from file, erasing
  // one, closing a polygon.
  //
  // Not the same as flush(), which makes already-issued drawing visible.  This
  // one re-runs the drawing.
  void (*redraw)(void);
} xa_ui_callbacks;

// Install the front end's implementations.  Passing NULL, or leaving a member
// NULL, makes the corresponding call a no-op rather than a crash.
void xa_ui_set_callbacks(const xa_ui_callbacks *cb);

// Core-side entry point.  Safe to call before any front end has registered.
void xa_ui_status(const char *text);
void xa_ui_pump_events(void);
void xa_ui_busy(void);
void xa_ui_flush(void);
void xa_ui_warn(const char *text);
void xa_ui_free_label(void *label);
void xa_ui_open_message_window(const char *to_call);
void xa_ui_redraw(void);

#endif // XA_UI_H
