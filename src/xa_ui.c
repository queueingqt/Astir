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

static xa_ui_callbacks ui = { NULL };


void xa_ui_set_callbacks(const xa_ui_callbacks *cb)
{
  if (cb == NULL)
  {
    ui.status = NULL;
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
