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

#include "util.h"         // get_timestamp
#include "xa_settings.h"  // disable_all_popups
#include "xa_ui.h"

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
