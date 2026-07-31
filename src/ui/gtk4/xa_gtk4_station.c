/*
 * The station info window, and the history behind the status toast.
 * See the header for why this is written rather than ported.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <gtk/gtk.h>

#include "core/astir.h"
#include "core/aprs/database.h"
#include "core/aprs/db_funcs.h"
#include "core/util/snprintf.h"
#include "core/util/util.h"
#include "ui/gtk4/xa_gtk4_station.h"

/* ---- the history behind the toast --------------------------------------- */

/*
 * A short ring of what the status line has said.
 *
 * The toast shows one line for four seconds and then it is gone, which is fine
 * for "still loading" and useless for "a station you care about was heard while
 * you were looking somewhere else".  Keeping the last few, with the callsign
 * they were about, turns a message that vanished into one you can still open.
 *
 * Short on purpose.  This is a peripheral view of recent activity, not a log --
 * the log is a log, and the station list is the station list.
 */
#define HISTORY_MAX 40

typedef struct
{
  char text[128];
  char callsign[MAX_CALLSIGN + 1];
} history_entry;

static history_entry history[HISTORY_MAX];
static int history_n;            /* how many are filled, up to HISTORY_MAX */
static int history_head;         /* where the next one goes */


void xa_gtk4_station_note(const char *text, const char *callsign)
{
  history_entry *e;

  if (text == NULL || text[0] == '\0')
  {
    return;
  }

  // Collapse an immediate repeat.  A progress line that ticks ("Loading x",
  // "Loading x") would otherwise fill the ring with one event.
  if (history_n > 0)
  {
    int prev = (history_head + HISTORY_MAX - 1) % HISTORY_MAX;

    if (strcmp(history[prev].text, text) == 0)
    {
      return;
    }
  }

  e = &history[history_head];
  astir_snprintf(e->text, sizeof(e->text), "%s", text);
  astir_snprintf(e->callsign, sizeof(e->callsign), "%s",
                 callsign != NULL ? callsign : "");

  history_head = (history_head + 1) % HISTORY_MAX;
  if (history_n < HISTORY_MAX)
  {
    history_n++;
  }
}


int xa_gtk4_station_history(const char **text, const char **callsign, int max)
{
  int i, n = 0;

  for (i = 0; i < history_n && n < max; i++)
  {
    // Walk backwards from the newest.
    int idx = (history_head + HISTORY_MAX - 1 - i) % HISTORY_MAX;

    text[n] = history[idx].text;
    callsign[n] = history[idx].callsign[0] ? history[idx].callsign : NULL;
    n++;
  }
  return n;
}


/* ---- the station window -------------------------------------------------- */

static GtkWidget *info_win;
static GtkWidget *info_grid;
static guint      info_timer;
static char       info_call[MAX_CALLSIGN + 1];

static void info_fill(void);


static void on_info_destroy(GtkWidget *w, gpointer unused)
{
  (void)w;
  (void)unused;
  info_win = NULL;
  info_grid = NULL;
  info_call[0] = '\0';
  if (info_timer != 0)
  {
    g_source_remove(info_timer);
    info_timer = 0;
  }
}


// "4 minutes ago", because an absolute time makes you do the subtraction.
static void ago(time_t then, char *out, size_t n)
{
  long secs;

  if (then == 0)
  {
    astir_snprintf(out, n, "never");
    return;
  }
  secs = (long)(sec_now() - then);
  if (secs < 0)   { secs = 0; }
  if (secs < 60)  { astir_snprintf(out, n, "%lds ago", secs); return; }
  if (secs < 3600){ astir_snprintf(out, n, "%ldm ago", secs / 60); return; }
  if (secs < 86400)
  {
    astir_snprintf(out, n, "%ldh %ldm ago", secs / 3600, (secs % 3600) / 60);
    return;
  }
  astir_snprintf(out, n, "%ldd ago", secs / 86400);
}


static void add_row(int *row, const char *name, const char *value)
{
  GtkWidget *l, *v;

  if (value == NULL || value[0] == '\0')
  {
    return;                      // an empty field is not worth a line
  }

  l = gtk_label_new(name);
  gtk_label_set_xalign(GTK_LABEL(l), 0.0);
  gtk_widget_set_valign(l, GTK_ALIGN_START);
  gtk_widget_add_css_class(l, "dim-label");
  gtk_widget_set_size_request(l, 110, -1);

  v = gtk_label_new(value);
  gtk_label_set_xalign(GTK_LABEL(v), 0.0);
  gtk_label_set_wrap(GTK_LABEL(v), TRUE);
  gtk_label_set_selectable(GTK_LABEL(v), TRUE);   // a callsign wants copying
  gtk_widget_set_hexpand(v, TRUE);

  gtk_grid_attach(GTK_GRID(info_grid), l, 0, *row, 1, 1);
  gtk_grid_attach(GTK_GRID(info_grid), v, 1, *row, 1, 1);
  (*row)++;
}


/*
 * Rebuild the contents from the station database.
 *
 * Re-looked-up every time rather than held: the station timeout can expire and
 * free a DataRow while its window is open, and a window holding that pointer
 * would be reading freed memory a few seconds later.
 */
static void info_fill(void)
{
  DataRow *p = NULL;
  GtkWidget *child;
  char buf[512], tmp[128];
  int row = 0;

  if (info_grid == NULL)
  {
    return;
  }

  while ((child = gtk_widget_get_first_child(info_grid)) != NULL)
  {
    gtk_grid_remove(GTK_GRID(info_grid), child);
  }

  if (!search_station_name(&p, info_call, 1) || p == NULL)
  {
    add_row(&row, "Station", info_call);
    add_row(&row, "", "No longer in the station list -- it has expired.");
    return;
  }

  add_row(&row, "Callsign", p->call_sign);
  if (p->tactical_call_sign != NULL && p->tactical_call_sign[0] != '\0')
  {
    add_row(&row, "Tactical", p->tactical_call_sign);
  }
  if (p->origin[0] != '\0')
  {
    // An object or item is transmitted BY somebody, and which somebody matters.
    add_row(&row, "Reported by", p->origin);
  }

  convert_lat_l2s(p->coord_lat, tmp, sizeof(tmp), CONVERT_HP_NOSP);
  add_row(&row, "Latitude", tmp);
  convert_lon_l2s(p->coord_lon, tmp, sizeof(tmp), CONVERT_HP_NOSP);
  add_row(&row, "Longitude", tmp);

  ago(p->sec_heard, tmp, sizeof(tmp));
  add_row(&row, "Last heard", tmp);
  if (p->direct_heard != 0)
  {
    ago(p->direct_heard, tmp, sizeof(tmp));
    add_row(&row, "Heard direct", tmp);
  }

  if (p->course[0] != '\0' || p->speed[0] != '\0')
  {
    astir_snprintf(buf, sizeof(buf), "%s%s%s",
                   p->course[0] ? p->course : "",
                   (p->course[0] && p->speed[0]) ? " deg at " : "",
                   p->speed[0] ? p->speed : "");
    add_row(&row, "Course/speed", buf);
  }
  add_row(&row, "Altitude", p->altitude);
  add_row(&row, "Power/gain", p->power_gain);

  astir_snprintf(buf, sizeof(buf), "%u", p->num_packets);
  add_row(&row, "Packets", buf);

  if (p->node_path_ptr != NULL)
  {
    add_row(&row, "Path", p->node_path_ptr);
  }

  // Comments and status, newest first, as one block.  A station that keeps
  // changing its comment has a history worth seeing at a glance.
  {
    CommentRow *c;
    int n = 0;

    buf[0] = '\0';
    for (c = p->comment_data; c != NULL && n < 6; c = c->next, n++)
    {
      if (c->text_ptr == NULL || c->text_ptr[0] == '\0')
      {
        continue;
      }
      if (buf[0] != '\0')
      {
        strncat(buf, "\n", sizeof(buf) - 1 - strlen(buf));
      }
      strncat(buf, c->text_ptr, sizeof(buf) - 1 - strlen(buf));
    }
    add_row(&row, "Comment", buf);

    buf[0] = '\0';
    n = 0;
    for (c = p->status_data; c != NULL && n < 4; c = c->next, n++)
    {
      if (c->text_ptr == NULL || c->text_ptr[0] == '\0')
      {
        continue;
      }
      if (buf[0] != '\0')
      {
        strncat(buf, "\n", sizeof(buf) - 1 - strlen(buf));
      }
      strncat(buf, c->text_ptr, sizeof(buf) - 1 - strlen(buf));
    }
    add_row(&row, "Status", buf);
  }
}


// Refresh while open: a moving station's position and last-heard time go stale
// within seconds, and a window showing a stale position is worse than none.
static gboolean info_refresh(gpointer unused)
{
  (void)unused;
  if (info_win == NULL)
  {
    info_timer = 0;
    return G_SOURCE_REMOVE;
  }
  info_fill();
  return G_SOURCE_CONTINUE;
}


void xa_gtk4_station_show(GtkWindow *parent, const char *callsign)
{
  GtkWidget *scroll, *header;

  if (callsign == NULL || callsign[0] == '\0')
  {
    return;
  }
  astir_snprintf(info_call, sizeof(info_call), "%s", callsign);

  if (info_win != NULL)
  {
    gtk_window_set_title(GTK_WINDOW(info_win), info_call);
    info_fill();
    gtk_window_present(GTK_WINDOW(info_win));
    return;
  }

  info_win = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(info_win), info_call);
  gtk_window_set_transient_for(GTK_WINDOW(info_win), parent);
  gtk_window_set_default_size(GTK_WINDOW(info_win), 460, 480);

  header = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(info_win), header);

  scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  info_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(info_grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(info_grid), 12);
  gtk_widget_set_margin_top(info_grid, 16);
  gtk_widget_set_margin_bottom(info_grid, 16);
  gtk_widget_set_margin_start(info_grid, 16);
  gtk_widget_set_margin_end(info_grid, 16);

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), info_grid);
  gtk_window_set_child(GTK_WINDOW(info_win), scroll);

  g_signal_connect(info_win, "destroy", G_CALLBACK(on_info_destroy), NULL);

  info_fill();
  info_timer = g_timeout_add_seconds(2, info_refresh, NULL);
  gtk_window_present(GTK_WINDOW(info_win));
}
