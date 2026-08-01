/*
 * The map chooser.  See the header for why it exists.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "core/astir.h"
#include "core/map/maps.h"
#include "core/state/xa_config.h"
#include "core/xa_ui.h"
#include "core/util/snprintf.h"
#include "ui/gtk4/xa_gtk4_maps.h"

static GtkWidget *maps_win;
static GtkWidget *maps_list;
static GtkWidget *maps_count;
static GtkWidget *maps_filter;

// What the user has ticked, not yet saved.  Applied to the index on Apply.
static void refresh_count(void);


/*
 * The directory part of an indexed path, or "" for one at the top.
 *
 * Grouping is by directory because that is how map collections arrive -- a
 * TIGER download is one directory of six hundred counties -- and a flat list of
 * six hundred is not a chooser, it is a wall.
 */
static void dir_of(const char *path, char *out, size_t n)
{
  const char *slash = strrchr(path, '/');

  if (slash == NULL)
  {
    astir_snprintf(out, n, "%s", "");
    return;
  }
  if ((size_t)(slash - path) < n)
  {
    memcpy(out, path, (size_t)(slash - path));
    out[slash - path] = '\0';
  }
  else
  {
    astir_snprintf(out, n, "%s", "");
  }
}


static const char *base_of(const char *path)
{
  const char *slash = strrchr(path, '/');

  return (slash != NULL) ? slash + 1 : path;
}


// A directory entry in the index, which is not a map.
static int is_directory_entry(const map_index_record *r)
{
  size_t len = strlen(r->filename);

  return (len == 0) || (r->filename[len - 1] == '/');
}


static void on_toggle(GtkCheckButton *b, gpointer data)
{
  map_index_record *r = data;

  r->selected = gtk_check_button_get_active(b) ? 1 : 0;
  refresh_count();
}


static void refresh_count(void)
{
  map_index_record *r;
  int sel = 0, total = 0;
  char buf[128];

  if (maps_count == NULL)
  {
    return;
  }
  for (r = map_index_head; r != NULL; r = r->next)
  {
    if (is_directory_entry(r))
    {
      continue;
    }
    total++;
    if (r->selected)
    {
      sel++;
    }
  }
  astir_snprintf(buf, sizeof(buf), "%d of %d maps selected", sel, total);
  gtk_label_set_text(GTK_LABEL(maps_count), buf);
}


/*
 * Build the rows.
 *
 * Done once when the window opens and again only when the filter changes --
 * both moments the user caused.  Nothing here runs on its own, so no row is
 * ever destroyed under the pointer.
 */
static void rebuild(void)
{
  map_index_record *r;
  GtkWidget *child;
  char cur_dir[MAX_FILENAME];
  char this_dir[MAX_FILENAME];
  const char *filter;
  int shown = 0;

  if (maps_list == NULL)
  {
    return;
  }
  while ((child = gtk_widget_get_first_child(maps_list)) != NULL)
  {
    gtk_list_box_remove(GTK_LIST_BOX(maps_list), child);
  }

  filter = (maps_filter != NULL)
             ? gtk_editable_get_text(GTK_EDITABLE(maps_filter)) : "";
  cur_dir[0] = '\0';

  for (r = map_index_head; r != NULL; r = r->next)
  {
    GtkWidget *row, *box, *chk;

    if (is_directory_entry(r))
    {
      continue;                  // shown as a heading when its files appear
    }
    if (filter != NULL && filter[0] != '\0'
        && strcasestr(r->filename, filter) == NULL)
    {
      continue;
    }

    // A heading whenever the directory changes.  The index is already ordered,
    // so this needs no sort of its own.
    dir_of(r->filename, this_dir, sizeof(this_dir));
    if (strcmp(this_dir, cur_dir) != 0)
    {
      GtkWidget *hrow = gtk_list_box_row_new();
      GtkWidget *h = gtk_label_new(this_dir[0] ? this_dir : "(top level)");

      gtk_label_set_xalign(GTK_LABEL(h), 0.0);
      gtk_widget_add_css_class(h, "heading");
      gtk_widget_set_margin_top(h, 10);
      gtk_widget_set_margin_bottom(h, 4);
      gtk_widget_set_margin_start(h, 10);
      gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(hrow), h);
      gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(hrow), FALSE);
      gtk_list_box_append(GTK_LIST_BOX(maps_list), hrow);
      astir_snprintf(cur_dir, sizeof(cur_dir), "%s", this_dir);
    }

    row = gtk_list_box_row_new();
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 2);
    gtk_widget_set_margin_bottom(box, 2);

    chk = gtk_check_button_new_with_label(base_of(r->filename));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk), r->selected != 0);
    // The record itself, not a copy of its name: the index outlives this
    // window, and a toggle should change the thing the map code reads.
    g_signal_connect(chk, "toggled", G_CALLBACK(on_toggle), r);
    gtk_widget_set_hexpand(chk, TRUE);
    {
      GtkWidget *lbl = gtk_widget_get_first_child(chk);

      // Long county filenames must not set the window width.
      while (lbl != NULL && !GTK_IS_LABEL(lbl))
      {
        lbl = gtk_widget_get_next_sibling(lbl);
      }
      if (lbl != NULL)
      {
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
      }
    }
    gtk_box_append(GTK_BOX(box), chk);

    // Layer, because drawing order is the one property that decides what you
    // can see when two maps cover the same ground.
    {
      char lay[32];
      GtkWidget *l;

      astir_snprintf(lay, sizeof(lay), "layer %d", r->map_layer);
      l = gtk_label_new(lay);
      gtk_widget_add_css_class(l, "dim-label");
      gtk_widget_set_tooltip_text(l, "Drawing order: lower layers draw first, "
                                     "so higher layers appear on top.");
      gtk_box_append(GTK_BOX(box), l);
    }

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(maps_list), row);
    shown++;
  }

  if (shown == 0)
  {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *l = gtk_label_new(
      (filter != NULL && filter[0] != '\0')
        ? "No map matches that."
        : "No maps indexed.\nCheck the map directory, then Maps > Rebuild Map "
          "Index.");

    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class(l, "dim-label");
    gtk_widget_set_margin_top(l, 28);
    gtk_widget_set_margin_bottom(l, 28);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), l);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_append(GTK_LIST_BOX(maps_list), row);
  }

  refresh_count();
}


static void on_filter_changed(GtkEditable *e, gpointer unused)
{
  (void)e;
  (void)unused;
  rebuild();
}


static void on_apply(GtkButton *b, gpointer unused)
{
  int n;

  (void)b;
  (void)unused;

  n = map_selection_save();
  if (n < 0)
  {
    xa_ui_message(XA_MSG_ERROR, NULL, "Could not save the map selection");
    return;
  }

  // Draw it.  The selection is not a preference to be noticed later; the point
  // of pressing Apply is to see the result.
  xa_ui_message(XA_MSG_INFO, NULL, "Map selection saved");
  xa_ui_redraw();
}


static void on_none(GtkButton *b, gpointer unused)
{
  map_index_record *r;

  (void)b;
  (void)unused;
  for (r = map_index_head; r != NULL; r = r->next)
  {
    r->selected = 0;
  }
  rebuild();
}


static void on_win_destroy(GtkWidget *w, gpointer unused)
{
  (void)w;
  (void)unused;
  maps_win = NULL;
  maps_list = NULL;
  maps_count = NULL;
  maps_filter = NULL;
}


void xa_gtk4_maps_show(GtkWindow *parent)
{
  GtkWidget *box, *scroll, *header, *bar, *apply, *none;

  if (maps_win != NULL)
  {
    gtk_window_present(GTK_WINDOW(maps_win));
    return;
  }

  // Take the current selection from the file, so the ticks match what is being
  // drawn rather than whatever was left in the index.
  map_selection_load();

  maps_win = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(maps_win), "Maps");
  gtk_window_set_transient_for(GTK_WINDOW(maps_win), parent);
  gtk_window_set_default_size(GTK_WINDOW(maps_win), 620, 560);

  header = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(maps_win), header);

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(maps_win), box);

  maps_filter = gtk_search_entry_new();
  gtk_widget_set_margin_top(maps_filter, 8);
  gtk_widget_set_margin_bottom(maps_filter, 8);
  gtk_widget_set_margin_start(maps_filter, 8);
  gtk_widget_set_margin_end(maps_filter, 8);
  gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(maps_filter),
                                        "Filter by name");
  g_signal_connect(maps_filter, "search-changed",
                   G_CALLBACK(on_filter_changed), NULL);
  gtk_box_append(GTK_BOX(box), maps_filter);

  scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);
  maps_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(maps_list),
                                  GTK_SELECTION_NONE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), maps_list);
  gtk_box_append(GTK_BOX(box), scroll);

  bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top(bar, 8);
  gtk_widget_set_margin_bottom(bar, 8);
  gtk_widget_set_margin_start(bar, 8);
  gtk_widget_set_margin_end(bar, 8);

  maps_count = gtk_label_new("");
  gtk_widget_add_css_class(maps_count, "dim-label");
  gtk_widget_set_hexpand(maps_count, TRUE);
  gtk_label_set_xalign(GTK_LABEL(maps_count), 0.0);
  gtk_box_append(GTK_BOX(bar), maps_count);

  none = gtk_button_new_with_label("Select None");
  g_signal_connect(none, "clicked", G_CALLBACK(on_none), NULL);
  gtk_box_append(GTK_BOX(bar), none);

  apply = gtk_button_new_with_label("Apply");
  gtk_widget_add_css_class(apply, "suggested-action");
  g_signal_connect(apply, "clicked", G_CALLBACK(on_apply), NULL);
  gtk_box_append(GTK_BOX(bar), apply);

  gtk_box_append(GTK_BOX(box), bar);

  g_signal_connect(maps_win, "destroy", G_CALLBACK(on_win_destroy), NULL);

  rebuild();
  gtk_window_present(GTK_WINDOW(maps_win));
}
