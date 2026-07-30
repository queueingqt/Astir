/*
 * xa_ui.c -- front-end callback dispatch.  See xa_ui.h.
 *
 * Deliberately trivial and free of any toolkit reference, so that core objects
 * calling xa_ui_status() link against this rather than against main.c.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>
#include <stdio.h>

#include "core/util/util.h"         // get_timestamp
#include "core/state/xa_settings.h"  // disable_all_popups
#include "core/xa_ui.h"

// { 0 } rather than a member-per-line list, here and below, so that adding a
// callback cannot leave one of them behind holding a stale pointer.
static xa_ui_callbacks ui = { 0 };


void xa_ui_set_callbacks(const xa_ui_callbacks *cb)
{
  if (cb == NULL)
  {
    static const xa_ui_callbacks none = { 0 };
    ui = none;
    return;
  }
  ui = *cb;
}


void xa_ui_status(const char *text)
{
  // No front end registered (a headless run, or very early startup) is normal,
  // not an error.
  if (ui.status != NULL && text != NULL)
  {
    ui.status(text);
  }
}


void xa_ui_pump_events(void)
{
  if (ui.pump_events != NULL)
  {
    ui.pump_events();
  }
}


void xa_ui_busy(void)
{
  if (ui.busy != NULL)
  {
    ui.busy();
  }
}


void xa_ui_flush(void)
{
  if (ui.flush != NULL)
  {
    ui.flush();
  }
}


void xa_ui_warn(const char *text)
{
  if (ui.warn != NULL && text != NULL)
  {
    ui.warn(text);
  }
}


void xa_ui_free_label(void *label)
{
  if (ui.free_label != NULL && label != NULL)
  {
    ui.free_label(label);
  }
}


void xa_ui_open_message_window(const char *to_call)
{
  if (ui.open_message_window != NULL && to_call != NULL)
  {
    ui.open_message_window(to_call);
  }
}


void xa_ui_redraw(void)
{
  if (ui.redraw != NULL)
  {
    ui.redraw();
  }
}


// The fallback when nothing is registered.  Not new behaviour: this is what
// popup_message() does in any build without HAVE_ERROR_POPUPS, which is the
// default.  Having it here means a headless run still reports its errors.
static void popup_to_stderr(const char *banner, const char *message)
{
  char timestring[110];

  get_timestamp(timestring);
  fprintf(stderr, "%s:\n\t%s  %s\n\n", timestring, banner, message);
}


void xa_ui_popup(const char *banner, const char *message)
{
  if (banner == NULL || message == NULL || disable_all_popups)
  {
    return;
  }
  if (ui.popup != NULL)
  {
    ui.popup(banner, message);
  }
  else
  {
    popup_to_stderr(banner, message);
  }
}


void xa_ui_popup_always(const char *banner, const char *message)
{
  if (banner == NULL || message == NULL || disable_all_popups)
  {
    return;
  }
  if (ui.popup_always != NULL)
  {
    ui.popup_always(banner, message);
  }
  else
  {
    popup_to_stderr(banner, message);
  }
}


void xa_ui_interfaces_changed(void)
{
  if (ui.interfaces_changed != NULL)
  {
    ui.interfaces_changed();
  }
}


void xa_ui_wx_data_changed(void)
{
  if (ui.wx_data_changed != NULL)
  {
    ui.wx_data_changed();
  }
}


void xa_ui_bulletin_added(const char *call_sign, const char *from_call,
                          const char *data, const char *seq,
                          char type, char from)
{
  if (ui.bulletin_added != NULL)
  {
    ui.bulletin_added(call_sign, from_call, data, seq, type, from);
  }
}


void xa_ui_message_logged(char from, const char *call_sign,
                          const char *from_call, const char *message)
{
  if (ui.message_logged != NULL)
  {
    ui.message_logged(from, call_sign, from_call, message);
  }
}


void xa_ui_locate_station(int emergency)
{
  if (ui.locate_station != NULL)
  {
    ui.locate_station(emergency);
  }
}


void xa_ui_send_message_path(const char *callsign, char *path, int path_size)
{
  if (path == NULL || path_size <= 0)
  {
    return;
  }
  // Empty means "no answer", which is what the front end also returns when no
  // open window matches.  Set it first so an unregistered front end and an
  // unmatched callsign look the same to the caller.
  path[0] = '\0';
  if (ui.send_message_path != NULL && callsign != NULL)
  {
    ui.send_message_path(callsign, path, path_size);
  }
}


// The Send Message windows.  With no front end registered every window is
// closed, which makes the core's loops over them do nothing -- the same shape a
// headless run already has for the popups.

int xa_ui_msg_window_is_open(int i)
{
  return (ui.msg_window_is_open != NULL) ? ui.msg_window_is_open(i) : 0;
}


int xa_ui_msg_window_is_group(int i)
{
  return (ui.msg_window_is_group != NULL) ? ui.msg_window_is_group(i) : 0;
}


int xa_ui_msg_window_callsign(int i, char *out, int n)
{
  if (out == NULL || n <= 0)
  {
    return 0;
  }
  // Cleared first so an unregistered front end and a window with no callsign
  // field look the same to the caller, as in xa_ui_send_message_path().
  out[0] = '\0';
  return (ui.msg_window_callsign != NULL) ? ui.msg_window_callsign(i, out, n) : 0;
}


void xa_ui_msg_window_raise(int i)
{
  if (ui.msg_window_raise != NULL)
  {
    ui.msg_window_raise(i);
  }
}


void xa_ui_msg_window_close_all(void)
{
  if (ui.msg_window_close_all != NULL)
  {
    ui.msg_window_close_all();
  }
}


void xa_ui_msg_window_clear(int i)
{
  if (ui.msg_window_clear != NULL)
  {
    ui.msg_window_clear(i);
  }
}


int xa_ui_msg_window_append(int i, long pos, const char *text,
                            long hl_from, long hl_to, int hl_selected)
{
  if (ui.msg_window_append == NULL || text == NULL)
  {
    return 0;
  }
  return ui.msg_window_append(i, pos, text, hl_from, hl_to, hl_selected);
}


void xa_ui_msg_window_show(int i, long pos)
{
  if (ui.msg_window_show != NULL)
  {
    ui.msg_window_show(i, pos);
  }
}
