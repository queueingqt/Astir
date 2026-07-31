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

/*
 * What kind of thing a message is.
 *
 * Every message the core sends says what it is, so a front end can decide what
 * to do with it instead of guessing from the text.  Guessing is what this
 * replaces: the status history used to recover a callsign by taking the first
 * word and looking it up in the station list, which worked for stations and
 * could not work at all for anything else -- an interface failing, a database
 * refusing a connection -- so those simply were not kept.
 *
 * Classes are about WHAT HAPPENED, not about how it should look.  Where it is
 * shown, how long for, and whether it is worth remembering are the front end's
 * decisions, and different front ends may reasonably differ.
 */
typedef enum
{
  // Work in progress: loading a map, indexing a shapefile, fetching a tile.
  // True while it is on screen and meaningless afterwards.  The overwhelming
  // majority of messages, and the reason a history that kept everything was
  // useless.
  XA_MSG_PROGRESS = 0,

  // A station was heard, or changed.  `subject` is its callsign.
  XA_MSG_STATION,

  // An interface opened, closed, or changed state.  `subject` is its name if
  // there is one.
  XA_MSG_INTERFACE,

  // Something failed.  Worth keeping whether or not anybody was looking.
  XA_MSG_ERROR,

  // Something worth saying that is not progress, not a failure, and not about
  // one station: a file finished loading, a cache was cleared.
  XA_MSG_INFO
} xa_msg_class;


typedef struct
{
  // Show a short progress or status message.  Called from map loading, the
  // interfaces and the station database -- roughly 45 sites.  May be called
  // very frequently during a redraw, so an implementation should be cheap.
  void (*status)(const char *text);

  /*
   * The same thing, classified.
   *
   * `subject` is what the message is ABOUT -- a callsign, an interface name --
   * or NULL.  Having it here is the point: a front end that wants to offer
   * "open that station" should not have to parse it back out of a sentence
   * assembled for a status bar.
   *
   * A front end may implement this, or `status`, or both.  xa_ui_message()
   * calls whichever exists, so a front end that only wants the text still gets
   * every message and one that wants the class gets that too.
   */
  void (*message)(xa_msg_class cls, const char *subject, const char *text);

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

  /* ---- things happened that a view of them should know about ---------- */

  // Show an error or notice to the user.  If no front end is registered these
  // fall back to timestamped stderr, which is exactly what a build without
  // HAVE_ERROR_POPUPS already does -- so a headless run keeps its diagnostics
  // instead of dropping them.
  //
  // popup_always is for messages the front end should not suppress by its own
  // policy.  Both respect disable_all_popups, which is a core setting and is
  // checked before dispatch.
  void (*popup)(const char *banner, const char *message);
  void (*popup_always)(const char *banner, const char *message);

  // The interface table changed -- a port opened, closed, or changed state.
  // Anything displaying devices[] should re-read it.  Called from ~20 places
  // in interface.c.
  void (*interfaces_changed)(void);

  // New decoded weather data is available.
  void (*wx_data_changed)(void);

  /*
   * A station's record changed: it was heard, moved, or gained a comment.
   *
   * The counterpart to interfaces_changed, and added for the same reason.  A
   * front end showing one station's details otherwise has to poll, and a window
   * that polls is a window that rebuilds itself under the pointer -- text
   * cannot be selected, and anything mid-click is destroyed.  Being told
   * instead means the display changes exactly when the data does and at no
   * other time.
   *
   * The callsign identifies which; a front end showing something else can
   * return immediately.  Called on the thread that decoded the packet, which is
   * the front end's own tick, so a toolkit may be touched from here.
   */
  void (*station_changed)(const char *call_sign);

  // A bulletin arrived.  Passed through as received; the front end copies what
  // it keeps.
  void (*bulletin_added)(const char *call_sign, const char *from_call,
                         const char *data, const char *seq,
                         char type, char from);

  // A message was sent or received and should go in whatever log the front end
  // keeps.  `from` distinguishes the direction, as in the original.
  void (*message_logged)(char from, const char *call_sign,
                         const char *from_call, const char *message);

  // Open the locate-station window.  emergency non-zero for the emergency
  // variant, which is the only way the core ever calls it -- an EMERGENCY
  // beacon arriving is the trigger.
  void (*locate_station)(int emergency);

  /* ---- and one question in the other direction --------------------------- */

  // What path should a message to this station take?  The answer lives in the
  // open send-message windows, so only the front end can answer it.  Writes at
  // most path_size bytes and leaves path empty if it does not know.
  //
  // The only callback here that reads front-end state rather than telling it
  // something.  A front end with no message windows can leave it NULL, and the
  // core falls back to its default path exactly as it does today when no window
  // matches the callsign.
  void (*send_message_path)(const char *callsign, char *path, int path_size);

  /* ---- the Send Message windows ------------------------------------------ */

  // The core drives these windows -- it decides a conversation exists, which
  // messages belong in it and in what order -- but it must not know they are
  // made of Motif text widgets.  These are the whole of what it needs.
  //
  // Windows are addressed by index, 0..MAX_MESSAGE_WINDOWS-1, because that is
  // how the core already tracks them.  A front end that keeps its windows some
  // other way still has to map an index to one.
  //
  // Every one of these is a no-op (or a zero answer) when unregistered, so a
  // headless build simply has no message windows.

  // Is window i open?  The core uses this both to skip closed windows and to
  // find a free slot for a new one.
  int (*msg_window_is_open)(int i);

  // Was window i opened for a *group* conversation?  Group windows show every
  // message to the group, not just the ones to and from us.
  int (*msg_window_is_group)(int i);

  // The callsign window i is holding a conversation with.  Writes at most n
  // bytes.  Returns zero if the window has no callsign field at all, which the
  // core distinguishes from an empty one -- the two took different paths in
  // the original and still do.
  int (*msg_window_callsign)(int i, char *out, int n);

  // Raise window i.
  void (*msg_window_raise)(int i);

  // Close every open message window, releasing whatever they hold.
  void (*msg_window_close_all)(void);

  /* ---- and rendering a conversation into one ----------------------------- */

  // The core rebuilds a window's whole contents on every update: clear, then
  // one append per message in time order, then show.  It is not a diffing
  // interface because the original was not one either.

  // Empty window i's transcript.
  void (*msg_window_clear)(int i);

  // Append `text` at `pos` in window i's transcript, and highlight the range
  // [hl_from, hl_to) -- reverse video when `hl_selected`, plain otherwise.
  //
  // The core passes absolute positions rather than letting the front end track
  // them, because it is the core that knows a message line's prefix should not
  // be highlighted.  Returns zero if there is no transcript to append to, in
  // which case the core does not advance `pos` -- as the original did not.
  int (*msg_window_append)(int i, long pos, const char *text,
                           long hl_from, long hl_to, int hl_selected);

  // Scroll window i so that `pos` is visible.
  void (*msg_window_show)(int i, long pos);
} xa_ui_callbacks;

// Install the front end's implementations.  Passing NULL, or leaving a member
// NULL, makes the corresponding call a no-op rather than a crash.
void xa_ui_set_callbacks(const xa_ui_callbacks *cb);

// Core-side entry point.  Safe to call before any front end has registered.
void xa_ui_status(const char *text);

/*
 * Send a classified message.  xa_ui_status(text) is exactly
 * xa_ui_message(XA_MSG_PROGRESS, NULL, text) -- progress is what an
 * unclassified message turns out to be, in every case that has been looked at.
 */
void xa_ui_message(xa_msg_class cls, const char *subject, const char *text);
void xa_ui_pump_events(void);
void xa_ui_busy(void);
void xa_ui_flush(void);
void xa_ui_warn(const char *text);
void xa_ui_free_label(void *label);
void xa_ui_open_message_window(const char *to_call);
void xa_ui_redraw(void);
void xa_ui_popup(const char *banner, const char *message);
void xa_ui_popup_always(const char *banner, const char *message);
void xa_ui_interfaces_changed(void);
void xa_ui_wx_data_changed(void);
void xa_ui_station_changed(const char *call_sign);
void xa_ui_bulletin_added(const char *call_sign, const char *from_call,
                          const char *data, const char *seq,
                          char type, char from);
void xa_ui_message_logged(char from, const char *call_sign,
                          const char *from_call, const char *message);
void xa_ui_locate_station(int emergency);
void xa_ui_send_message_path(const char *callsign, char *path, int path_size);

int  xa_ui_msg_window_is_open(int i);
int  xa_ui_msg_window_is_group(int i);
int  xa_ui_msg_window_callsign(int i, char *out, int n);
void xa_ui_msg_window_raise(int i);
void xa_ui_msg_window_close_all(void);
void xa_ui_msg_window_clear(int i);
int  xa_ui_msg_window_append(int i, long pos, const char *text,
                             long hl_from, long hl_to, int hl_selected);
void xa_ui_msg_window_show(int i, long pos);

#endif // XA_UI_H
