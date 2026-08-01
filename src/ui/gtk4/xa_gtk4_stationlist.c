/*
 * The station list.  See the header for why it is not a port of the Motif one.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "core/astir.h"
#include "core/aprs/database.h"
#include "core/aprs/db_funcs.h"
#include "core/map/maps.h"
#include "core/state/xa_settings.h"
#include "core/util/snprintf.h"
#include "core/util/util.h"
#include "core/xa_ui.h"
#include "ui/gtk4/xa_gtk4_station.h"
#include "ui/gtk4/xa_gtk4_view.h"
#include "ui/gtk4/xa_gtk4_stationlist.h"

static GtkWidget *sl_win;
static GtkWidget *sl_list;
static GtkWidget *sl_search;
static GtkWidget *sl_count;
static GtkWindow *sl_parent;

/*
 * How the list is ordered.
 *
 * Held here rather than read off a widget, because the sort function is called
 * by GTK for every comparison and reaching into a dropdown per comparison would
 * be work per pair rather than per change.
 */
typedef enum
{
  SORT_HEARD = 0,                /* most recently heard first */
  SORT_CALL,                     /* alphabetical */
  SORT_DISTANCE                  /* nearest first */
} sort_key;

static sort_key sl_sort = SORT_HEARD;


/* ---- reading a station ---------------------------------------------------- */

/*
 * Everything a row shows, gathered in one place.
 *
 * Looked up by callsign each time rather than held: the station timeout can
 * free a DataRow while its row is on screen, and a row holding that pointer
 * would be reading freed memory the moment the sort function touched it.  The
 * sort function runs on every comparison, so that would not be a rare crash.
 */
typedef struct
{
  int    found;
  time_t heard;
  double distance;               /* miles or km, per english_units */
  char   course[8];
} station_facts;

static void read_station(const char *call, station_facts *out)
{
  DataRow *p = NULL;

  memset(out, 0, sizeof(*out));
  out->distance = -1.0;

  if (call == NULL || !search_station_name(&p, (char *)call, 1) || p == NULL)
  {
    return;
  }
  out->found = 1;
  out->heard = p->sec_heard;

  // Only if we know where we are.  An unset position would otherwise put every
  // station the same implausible distance away and sort by nothing.
  if (my_lat[0] != '\0' && my_long[0] != '\0')
  {
    /*
     * calc_distance_course() returns NAUTICAL miles -- r_d*180*60/M_PI is arc
     * minutes, and an arc minute of great circle is a nautical mile.  The unit
     * is nowhere in its name or its declaration, which is exactly the kind of
     * thing that gets multiplied by the wrong constant, so: nautical in,
     * whatever the user asked for out.
     */
    out->distance = calc_distance_course(convert_lat_s2l(my_lat),
                                         convert_lon_s2l(my_long),
                                         p->coord_lat, p->coord_lon,
                                         out->course, sizeof(out->course));
    out->distance *= 1.15078;                 // nautical miles to statute
    if (!english_units)
    {
      out->distance *= cvt_mi2len;            // and to whatever un_dst says
    }
  }
}


// "3m", "2h 10m", "4d" -- short, because this is a column and not a sentence.
static void short_age(time_t then, char *out, size_t n)
{
  long secs;

  if (then == 0)
  {
    astir_snprintf(out, n, "-");
    return;
  }
  secs = (long)(sec_now() - then);
  if (secs < 0)    { secs = 0; }
  if (secs < 60)   { astir_snprintf(out, n, "%lds", secs); return; }
  if (secs < 3600) { astir_snprintf(out, n, "%ldm", secs / 60); return; }
  if (secs < 86400)
  {
    astir_snprintf(out, n, "%ldh %ldm", secs / 3600, (secs % 3600) / 60);
    return;
  }
  astir_snprintf(out, n, "%ldd", secs / 86400);
}


/* ---- rows ----------------------------------------------------------------- */

// A row's three labels, so they can be written without being rebuilt.
#define ROW_CALL  "sl-call"
#define ROW_DIST  "sl-dist"
#define ROW_AGE   "sl-age"


static void row_update(GtkWidget *row)
{
  const char *call = g_object_get_data(G_OBJECT(row), "callsign");
  GtkWidget *l;
  station_facts f;
  char buf[64];

  read_station(call, &f);

  l = g_object_get_data(G_OBJECT(row), ROW_DIST);
  if (l != NULL)
  {
    if (f.distance >= 0.0)
    {
      astir_snprintf(buf, sizeof(buf), "%.1f %s %s", f.distance,
                     un_dst[0] ? un_dst : (english_units ? "mi" : "km"), f.course);
    }
    else
    {
      astir_snprintf(buf, sizeof(buf), "%s", "");
    }
    if (strcmp(gtk_label_get_text(GTK_LABEL(l)), buf) != 0)
    {
      gtk_label_set_text(GTK_LABEL(l), buf);
    }
  }

  l = g_object_get_data(G_OBJECT(row), ROW_AGE);
  if (l != NULL)
  {
    short_age(f.heard, buf, sizeof(buf));
    if (strcmp(gtk_label_get_text(GTK_LABEL(l)), buf) != 0)
    {
      gtk_label_set_text(GTK_LABEL(l), buf);
    }
  }

  // A station that has expired keeps its row until the list is reopened, dimmed
  // rather than removed.  Removing it would move everything under the pointer
  // at the moment somebody was reaching for it.
  if (!f.found)
  {
    gtk_widget_add_css_class(row, "dim-label");
  }
}


static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer u)
{
  const char *call;

  (void)box;
  (void)u;
  call = g_object_get_data(G_OBJECT(row), "callsign");
  if (call != NULL)
  {
    xa_gtk4_station_show(sl_parent, call);
  }
}


// Centre the map on this station.
static void on_centre(GtkButton *b, gpointer unused)
{
  const char *call = g_object_get_data(G_OBJECT(b), "callsign");
  DataRow *p = NULL;

  (void)unused;
  if (call == NULL || !search_station_name(&p, (char *)call, 1) || p == NULL)
  {
    return;
  }
  xa_gtk4_centre_on(p->coord_lat, p->coord_lon);
}


static GtkWidget *make_row(const char *call)
{
  GtkWidget *row = gtk_list_box_row_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *lc, *ld, *la, *btn;

  gtk_widget_set_margin_start(box, 10);
  gtk_widget_set_margin_end(box, 6);
  gtk_widget_set_margin_top(box, 4);
  gtk_widget_set_margin_bottom(box, 4);

  lc = gtk_label_new(call);
  gtk_label_set_xalign(GTK_LABEL(lc), 0.0);
  gtk_widget_set_size_request(lc, 110, -1);
  gtk_widget_add_css_class(lc, "heading");
  gtk_box_append(GTK_BOX(box), lc);

  ld = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(ld), 0.0);
  gtk_widget_set_size_request(ld, 110, -1);
  gtk_widget_add_css_class(ld, "dim-label");
  gtk_box_append(GTK_BOX(box), ld);

  la = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(la), 1.0);
  gtk_widget_set_size_request(la, 70, -1);
  gtk_widget_set_hexpand(la, TRUE);
  gtk_widget_add_css_class(la, "dim-label");
  gtk_box_append(GTK_BOX(box), la);

  btn = gtk_button_new_from_icon_name("find-location-symbolic");
  gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
  gtk_widget_set_tooltip_text(btn, "Centre the map here");
  g_object_set_data_full(G_OBJECT(btn), "callsign", g_strdup(call), g_free);
  g_signal_connect(btn, "clicked", G_CALLBACK(on_centre), NULL);
  gtk_box_append(GTK_BOX(box), btn);

  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

  g_object_set_data_full(G_OBJECT(row), "callsign", g_strdup(call), g_free);
  g_object_set_data(G_OBJECT(row), ROW_CALL, lc);
  g_object_set_data(G_OBJECT(row), ROW_DIST, ld);
  g_object_set_data(G_OBJECT(row), ROW_AGE, la);

  row_update(row);
  return row;
}


static GtkWidget *find_row(const char *call)
{
  GtkWidget *child;

  if (sl_list == NULL || call == NULL)
  {
    return NULL;
  }
  for (child = gtk_widget_get_first_child(sl_list);
       child != NULL;
       child = gtk_widget_get_next_sibling(child))
  {
    const char *c = g_object_get_data(G_OBJECT(child), "callsign");

    if (c != NULL && strcasecmp(c, call) == 0)
    {
      return child;
    }
  }
  return NULL;
}


/* ---- sorting and filtering, done by GTK ----------------------------------- */

/*
 * GTK owns the order and the visibility, not this file.
 *
 * gtk_list_box_set_sort_func and set_filter_func reorder and hide the rows that
 * already exist.  The alternative -- sorting a list and rebuilding it -- would
 * destroy and recreate every row on every packet, which with a busy feed means
 * continuously, and is how a list ends up ignoring clicks.
 */
static int sort_rows(GtkListBoxRow *a, GtkListBoxRow *b, gpointer u)
{
  const char *ca = g_object_get_data(G_OBJECT(a), "callsign");
  const char *cb = g_object_get_data(G_OBJECT(b), "callsign");
  station_facts fa, fb;

  (void)u;
  if (ca == NULL || cb == NULL)
  {
    return 0;
  }

  switch (sl_sort)
  {
    case SORT_CALL:
      return strcasecmp(ca, cb);

    case SORT_DISTANCE:
      read_station(ca, &fa);
      read_station(cb, &fb);
      // A station with no distance sorts last rather than first: "unknown" is
      // not "here".
      if (fa.distance < 0.0 && fb.distance < 0.0) { return strcasecmp(ca, cb); }
      if (fa.distance < 0.0) { return 1; }
      if (fb.distance < 0.0) { return -1; }
      if (fa.distance < fb.distance) { return -1; }
      if (fa.distance > fb.distance) { return 1; }
      return strcasecmp(ca, cb);

    case SORT_HEARD:
    default:
      read_station(ca, &fa);
      read_station(cb, &fb);
      if (fa.heard > fb.heard) { return -1; }   /* newest first */
      if (fa.heard < fb.heard) { return 1; }
      return strcasecmp(ca, cb);
  }
}


static gboolean filter_row(GtkListBoxRow *row, gpointer u)
{
  const char *call = g_object_get_data(G_OBJECT(row), "callsign");
  const char *text;

  (void)u;
  if (sl_search == NULL || call == NULL)
  {
    return TRUE;
  }
  text = gtk_editable_get_text(GTK_EDITABLE(sl_search));
  if (text == NULL || text[0] == '\0')
  {
    return TRUE;
  }
  return (strcasestr(call, text) != NULL);
}


static void refresh_count(void)
{
  GtkWidget *child;
  int n = 0;
  char buf[64];

  if (sl_count == NULL || sl_list == NULL)
  {
    return;
  }
  for (child = gtk_widget_get_first_child(sl_list);
       child != NULL;
       child = gtk_widget_get_next_sibling(child))
  {
    n++;
  }
  astir_snprintf(buf, sizeof(buf), "%d station%s", n, (n == 1) ? "" : "s");
  gtk_label_set_text(GTK_LABEL(sl_count), buf);
}


void xa_gtk4_stationlist_changed(const char *call_sign)
{
  GtkWidget *row;

  if (sl_list == NULL || call_sign == NULL || call_sign[0] == '\0')
  {
    return;                      // nobody is looking
  }

  row = find_row(call_sign);
  if (row == NULL)
  {
    gtk_list_box_append(GTK_LIST_BOX(sl_list), make_row(call_sign));
    refresh_count();
  }
  else
  {
    row_update(row);
  }

  // Ask GTK to reconsider the order.  It moves the existing rows; nothing is
  // destroyed and nothing under the pointer goes anywhere it was not going to.
  gtk_list_box_invalidate_sort(GTK_LIST_BOX(sl_list));
}


static void on_search_changed(GtkEditable *e, gpointer u)
{
  (void)e;
  (void)u;
  if (sl_list != NULL)
  {
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(sl_list));
  }
}


static void on_sort_changed(GtkDropDown *dd, GParamSpec *ps, gpointer u)
{
  (void)ps;
  (void)u;
  sl_sort = (sort_key)gtk_drop_down_get_selected(dd);
  if (sl_list != NULL)
  {
    gtk_list_box_invalidate_sort(GTK_LIST_BOX(sl_list));
  }
}


static void on_win_destroy(GtkWidget *w, gpointer u)
{
  (void)w;
  (void)u;
  sl_win = NULL;
  sl_list = NULL;
  sl_search = NULL;
  sl_count = NULL;
  sl_parent = NULL;
}


void xa_gtk4_stationlist_show(GtkWindow *parent)
{
  GtkWidget *box, *scroll, *header, *bar, *sort;
  GtkStringList *keys;
  DataRow *p;

  if (sl_win != NULL)
  {
    gtk_window_present(GTK_WINDOW(sl_win));
    return;
  }
  sl_parent = parent;

  sl_win = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(sl_win), "Stations");
  gtk_window_set_transient_for(GTK_WINDOW(sl_win), parent);
  gtk_window_set_default_size(GTK_WINDOW(sl_win), 520, 620);

  header = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(sl_win), header);

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(sl_win), box);

  bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top(bar, 8);
  gtk_widget_set_margin_bottom(bar, 8);
  gtk_widget_set_margin_start(bar, 8);
  gtk_widget_set_margin_end(bar, 8);

  sl_search = gtk_search_entry_new();
  gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(sl_search),
                                        "Find a callsign");
  gtk_widget_set_hexpand(sl_search, TRUE);
  g_signal_connect(sl_search, "search-changed",
                   G_CALLBACK(on_search_changed), NULL);
  gtk_box_append(GTK_BOX(bar), sl_search);

  keys = gtk_string_list_new(NULL);
  gtk_string_list_append(keys, "Last heard");
  gtk_string_list_append(keys, "Callsign");
  gtk_string_list_append(keys, "Distance");
  sort = gtk_drop_down_new(G_LIST_MODEL(keys), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(sort), (guint)sl_sort);
  g_signal_connect(sort, "notify::selected", G_CALLBACK(on_sort_changed), NULL);
  gtk_box_append(GTK_BOX(bar), sort);

  gtk_box_append(GTK_BOX(box), bar);

  scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);

  sl_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(sl_list), GTK_SELECTION_SINGLE);
  gtk_list_box_set_sort_func(GTK_LIST_BOX(sl_list), sort_rows, NULL, NULL);
  gtk_list_box_set_filter_func(GTK_LIST_BOX(sl_list), filter_row, NULL, NULL);
  g_signal_connect(sl_list, "row-activated",
                   G_CALLBACK(on_row_activated), NULL);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), sl_list);
  gtk_box_append(GTK_BOX(box), scroll);

  sl_count = gtk_label_new("");
  gtk_widget_add_css_class(sl_count, "dim-label");
  gtk_widget_set_margin_top(sl_count, 6);
  gtk_widget_set_margin_bottom(sl_count, 6);
  gtk_box_append(GTK_BOX(box), sl_count);

  // Everything heard so far.  After this the list is maintained one station at
  // a time, from station_changed.
  for (p = n_first; p != NULL; p = p->n_next)
  {
    if (p->call_sign[0] != '\0')
    {
      gtk_list_box_append(GTK_LIST_BOX(sl_list), make_row(p->call_sign));
    }
  }
  refresh_count();

  g_signal_connect(sl_win, "destroy", G_CALLBACK(on_win_destroy), NULL);
  gtk_window_present(GTK_WINDOW(sl_win));
}
