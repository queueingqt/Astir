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
