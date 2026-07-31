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


/*
 * Record an event.  Progress is not an event.
 *
 * Everything the core puts on the status line came through here at first, and
 * that filled the ring with "Loading CA_Los_Angeles_06037_roads.shp" -- several
 * per render, forever -- pushing out the stations, which are the only entries
 * anybody can click.  A history of what the program was busy with is not a
 * history of what happened.
 *
 * So a message is kept only if it names a station.  That is a strict rule and a
 * deliberate one: it makes this "stations heard recently", which is exactly
 * what the popover is for, and progress still shows live in the toast where it
 * is useful.
 */
void xa_gtk4_station_note(const char *text, const char *callsign)
{
  history_entry *e;
  int i;

  if (text == NULL || text[0] == '\0')
  {
    return;
  }
  if (callsign == NULL || callsign[0] == '\0')
  {
    return;                      // progress, not an event
  }

  /*
   * One entry per station, moved to the front when heard again.
   *
   * A station transmitting every thirty seconds would otherwise take the whole
   * ring to itself within the hour, and forty lines all saying the same
   * callsign is not a history either.
   */
  for (i = 0; i < history_n; i++)
  {
    int idx = (history_head + HISTORY_MAX - 1 - i) % HISTORY_MAX;

    if (strcasecmp(history[idx].callsign, callsign) == 0)
    {
      // Shuffle the newer entries back over it, then append afresh.
      int k;

      for (k = i; k > 0; k--)
      {
        int dst = (history_head + HISTORY_MAX - 1 - k) % HISTORY_MAX;
        int src = (history_head + HISTORY_MAX - k) % HISTORY_MAX;

        history[dst] = history[src];
      }
      history_head = (history_head + HISTORY_MAX - 1) % HISTORY_MAX;
      history_n--;
      break;
    }
  }

  e = &history[history_head];
  astir_snprintf(e->callsign, sizeof(e->callsign), "%s", callsign);

  /*
   * Tidy the text for a list rather than a status line.
   *
   * The core pads the callsign out to a fixed width so successive status lines
   * align in place; in a stacked list that padding is just a gap.  And a
   * position update carries no message at all, which left rows showing a bare
   * callsign and nothing else -- true, but it reads like something failed to
   * load.
   */
  {
    const char *rest = text + strlen(callsign);
    char tidy[sizeof(e->text)];
    size_t n = 0;
    int gap = 0;

    while (*rest == ' ' || *rest == '\t')
    {
      rest++;
    }
    while (*rest != '\0' && n < sizeof(tidy) - 1)
    {
      if (*rest == ' ' || *rest == '\t')
      {
        gap = 1;                 // collapse a run of spaces to one
      }
      else
      {
        if (gap && n > 0) { tidy[n++] = ' '; }
        gap = 0;
        tidy[n++] = *rest;
      }
      rest++;
    }
    tidy[n] = '\0';

    astir_snprintf(e->text, sizeof(e->text), "%s  %s", callsign,
                   n > 0 ? tidy : "heard");
  }

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
static char       info_call[MAX_CALLSIGN + 1];

static void info_fill(void);

#define FIELD_MAX 20

static struct
{
  const char *name;
  GtkWidget  *label;
  GtkWidget  *value;
} fields[FIELD_MAX];
static int field_n;



static void on_info_destroy(GtkWidget *w, gpointer unused)
{
  (void)w;
  (void)unused;
  info_win = NULL;
  info_grid = NULL;
  field_n = 0;                   // the widgets went with the window
  info_call[0] = '\0';
}


/*
 * When, as a wall-clock time.
 *
 * Not "9s ago", which was the first version and which cannot be right for more
 * than a second: a relative time has to be redrawn continuously to stay true,
 * and redrawing continuously is the polling this window was supposed to stop
 * doing.  Solving it with a five-second timer was solving the symptom -- the
 * window still woke up asking a question nothing had prompted.
 *
 * A timestamp is correct the moment it is written and stays correct forever, so
 * it needs no clock at all.  The date appears only when it is not today, which
 * is the case where its absence would mislead.
 */
static void when(time_t t, char *out, size_t n)
{
  struct tm tm_then, tm_now;
  time_t now = sec_now();

  if (t == 0)
  {
    astir_snprintf(out, n, "never");
    return;
  }

  tm_then = *localtime(&t);
  tm_now  = *localtime(&now);

  if (tm_then.tm_year == tm_now.tm_year && tm_then.tm_yday == tm_now.tm_yday)
  {
    strftime(out, n, "%H:%M:%S", &tm_then);
  }
  else
  {
    strftime(out, n, "%Y-%m-%d %H:%M:%S", &tm_then);
  }
}


/*
 * One field, created once and then updated in place.
 *
 * The first version of this destroyed every child and built new ones on each
 * refresh.  That is how a Motif dialog was redrawn and it is wrong here for
 * reasons that are visible rather than theoretical: selected text is thrown
 * away mid-selection, the scroll position jumps, and a widget under the pointer
 * is destroyed while GTK is dispatching to it.  It is the same fault that made
 * the interface list eat clicks.
 *
 * So the rows are built once, keyed by field name, and afterwards only their
 * text changes.  A field that becomes empty hides its row rather than removing
 * it, so nothing is ever destroyed while the window is open.
 */
static void set_field(const char *name, const char *value)
{
  int i;
  GtkWidget *l, *v;

  for (i = 0; i < field_n; i++)
  {
    if (strcmp(fields[i].name, name) == 0)
    {
      break;
    }
  }

  if (i == field_n)                      // first time this field is seen
  {
    if (field_n >= FIELD_MAX || info_grid == NULL)
    {
      return;
    }
    l = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    gtk_widget_set_valign(l, GTK_ALIGN_START);
    gtk_widget_add_css_class(l, "dim-label");
    gtk_widget_set_size_request(l, 110, -1);

    v = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(v), 0.0);
    gtk_label_set_wrap(GTK_LABEL(v), TRUE);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);   // a callsign wants copying
    gtk_widget_set_hexpand(v, TRUE);

    gtk_grid_attach(GTK_GRID(info_grid), l, 0, field_n, 1, 1);
    gtk_grid_attach(GTK_GRID(info_grid), v, 1, field_n, 1, 1);

    fields[field_n].name = name;
    fields[field_n].label = l;
    fields[field_n].value = v;
    field_n++;
  }

  l = fields[i].label;
  v = fields[i].value;

  if (value == NULL || value[0] == '\0')
  {
    gtk_widget_set_visible(l, FALSE);
    gtk_widget_set_visible(v, FALSE);
    return;
  }

  // Only touch the text if it actually differs: setting a label to what it
  // already says still drops any selection inside it.
  if (strcmp(gtk_label_get_text(GTK_LABEL(v)), value) != 0)
  {
    gtk_label_set_text(GTK_LABEL(v), value);
  }
  gtk_widget_set_visible(l, TRUE);
  gtk_widget_set_visible(v, TRUE);
}


/*
 * Refresh the contents from the station database.
 *
 * Re-looked-up every time rather than held: the station timeout can expire and
 * free a DataRow while its window is open, and a window holding that pointer
 * would be reading freed memory a few seconds later.  Nothing is destroyed --
 * see set_field.
 */
static void info_fill(void)
{
  DataRow *p = NULL;
  char buf[512], tmp[128];

  if (info_grid == NULL)
  {
    return;
  }

  if (!search_station_name(&p, info_call, 1) || p == NULL)
  {
    set_field("Callsign", info_call);
    set_field("Status", "No longer in the station list -- it has expired.");
    return;
  }

  set_field("Callsign", p->call_sign);
  if (p->tactical_call_sign != NULL && p->tactical_call_sign[0] != '\0')
  {
    set_field("Tactical", p->tactical_call_sign);
  }
  if (p->origin[0] != '\0')
  {
    // An object or item is transmitted BY somebody, and which somebody matters.
    set_field("Reported by", p->origin);
  }

  convert_lat_l2s(p->coord_lat, tmp, sizeof(tmp), CONVERT_HP_NOSP);
  set_field("Latitude", tmp);
  convert_lon_l2s(p->coord_lon, tmp, sizeof(tmp), CONVERT_HP_NOSP);
  set_field("Longitude", tmp);

  when(p->sec_heard, tmp, sizeof(tmp));
  set_field("Last heard", tmp);
  if (p->direct_heard != 0)
  {
    when(p->direct_heard, tmp, sizeof(tmp));
    set_field("Heard direct", tmp);
  }

  if (p->course[0] != '\0' || p->speed[0] != '\0')
  {
    astir_snprintf(buf, sizeof(buf), "%s%s%s",
                   p->course[0] ? p->course : "",
                   (p->course[0] && p->speed[0]) ? " deg at " : "",
                   p->speed[0] ? p->speed : "");
    set_field("Course/speed", buf);
  }
  set_field("Altitude", p->altitude);
  set_field("Power/gain", p->power_gain);

  astir_snprintf(buf, sizeof(buf), "%u", p->num_packets);
  set_field("Packets", buf);

  if (p->node_path_ptr != NULL)
  {
    set_field("Path", p->node_path_ptr);
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
    set_field("Comment", buf);

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
    set_field("Status", buf);
  }
}


/*
 * The core heard this station.  Refresh, if it is the one on display.
 *
 * This is what the window runs on, in place of a timer.  It fires exactly when
 * a packet for this station is decoded, which is the only moment its details
 * can have changed.
 */
void xa_gtk4_station_changed(const char *call_sign)
{
  if (info_win == NULL || call_sign == NULL || info_call[0] == '\0')
  {
    return;
  }
  if (strcasecmp(call_sign, info_call) == 0)
  {
    info_fill();
  }
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
  gtk_window_present(GTK_WINDOW(info_win));
}
