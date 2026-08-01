/*
 * Callsign, position, symbol and comment: this station's own identity.
 *
 * See xa_gtk4_mystation.h for why one window does both first run and settings.
 *
 * The position field takes a Maidenhead grid square as well as a latitude and
 * longitude, and grid square is offered first on purpose.  A licensed operator
 * knows theirs -- it is on QSL cards, in every contest exchange and in every
 * logging program -- and almost nobody knows their latitude to four decimal
 * places without looking it up.  Six characters put you within about 3 km,
 * which is finer than a map centre, a distance readout or a server filter can
 * use.  The two fields update each other so they are never shown disagreeing.
 *
 * Nothing here can be got wrong in a way that costs anything: everything is
 * editable again from the menu, and the window can be dismissed without
 * answering.  It is a prompt, not a gate.
 */

#include <gtk/gtk.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/astir.h"
#include "core/globals.h"
#include "core/main.h"          // my_group, my_symbol, my_comment
#include "core/state/xa_state.h"
#include "core/state/xa_config.h"
#include "core/util/util.h"
#include "core/util/snprintf.h"
#include "core/render/symbols_vector.h"
#include "draw/xa_draw.h"
#include "ui/gtk4/xa_gtk4_mystation.h"
#include "ui/gtk4/xa_gtk4_interfaces.h"

// Implemented by the core renderer and the GTK4 backend respectively.  Drawing
// the preview through the same path the map uses is the whole point: a preview
// with its own copy of the geometry would be free to disagree with the map, and
// the one place that must not happen is the picture of what you look like.
const astir_sym_glyph *astir_symbol_glyph(char table, char symbol);
void astir_symbol_draw_glyph(const astir_sym_glyph *g, xa_surface_id where,
                             xa_pen pen, long x, long y, double size,
                             char orient, double alpha);
cairo_surface_t *xa_gtk4_surface_of(xa_surface_id s);


/*
 * The symbols an operator is most likely to be.
 *
 * Not the whole APRS set -- there are 211 of those and a list that long is a
 * worse way to find "house" than typing it.  These are the ones people
 * actually set for themselves, and anything else is two characters away in the
 * field below.
 */
typedef struct
{
  char        group;             // symbol table: '/' primary, '\\' alternate
  char        symbol;
  const char *label;
} sym_choice;

static const sym_choice symbols[] =
{
  { '/', '-', "Home station"        },
  { '/', '>', "Car"                 },
  { '/', 'k', "Truck"               },
  { '/', 'v', "Van"                 },
  { '/', 'j', "Jeep"                },
  { '/', '<', "Motorcycle"          },
  { '/', 'b', "Bicycle"             },
  { '/', '[', "Person on foot"      },
  { '/', 'Y', "Boat"                },
  { '/', '\'', "Aircraft"           },
  { '/', '_', "Weather station"     },
  { '/', '#', "Digipeater"          },
  { '/', '&', "Gateway (igate)"     },
  { '/', 'r', "Repeater"            },
  { '\\', 'k', "Portable / EOC"     },
};

#define NSYMBOLS ((int)(sizeof(symbols) / sizeof(symbols[0])))

/*
 * One more entry than there are symbols, for everything else.
 *
 * Without it the list cannot describe a station whose symbol is not one of the
 * common ones -- and the default is exactly that: Astir starts at "/x", which
 * is the marker for "not configured".  Selecting nothing does not help,
 * because a GtkDropDown with no selection still draws its first row, so the
 * window claimed "Home station" while the code beside it read "/x".  A setting
 * that displays as something other than its value is worse than one that
 * displays as unfamiliar.
 */
#define SYM_OTHER NSYMBOLS


/* ---- drawing a symbol into a widget ------------------------------------ */

/*
 * A swatch showing one APRS symbol, as the map would draw it.
 *
 * The symbol is rendered on a pale background rather than on the window's, and
 * not for decoration: a great many APRS symbols are black line art on
 * transparency, and on a dark theme those come out as an empty square.  The map
 * draws them over land, so a land-coloured swatch is both what the operator
 * will actually see and the only choice that works in either theme.
 */
#define SWATCH  22.0

typedef struct
{
  char table;
  char symbol;
} swatch_sym;


static void swatch_draw(GtkDrawingArea *area, cairo_t *cr,
                        int width, int height, gpointer data)
{
  swatch_sym            *s = data;
  const astir_sym_glyph *g;
  xa_surface_id          surf;
  xa_pen                 pen;
  cairo_surface_t       *cs;
  double                 x = (width - SWATCH) / 2.0;
  double                 y = (height - SWATCH) / 2.0;

  (void)area;

  // The land colour the default basemap fills with, so the swatch reads as a
  // piece of map rather than as a button.
  cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
  cairo_rectangle(cr, x, y, SWATCH, SWATCH);
  cairo_fill(cr);

  g = astir_symbol_glyph(s->table, s->symbol);
  if (g == NULL)
  {
    // No such symbol.  Say so with a stroke rather than leaving a blank square
    // that reads as "loading".
    cairo_set_source_rgb(cr, 0.6, 0.2, 0.2);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, x + 4, y + 4);
    cairo_line_to(cr, x + SWATCH - 4, y + SWATCH - 4);
    cairo_move_to(cr, x + SWATCH - 4, y + 4);
    cairo_line_to(cr, x + 4, y + SWATCH - 4);
    cairo_stroke(cr);
    return;
  }

  /*
   * XA_DEPTH_ALPHA, and it has to be.
   *
   * The depths are an enum -- CANVAS 0, BITMAP 1, ALPHA 2 -- not a bit count.
   * Asking for "32" lands in the default branch and returns an opaque RGB24
   * surface, and an APRS symbol is line art that covers a fraction of its box:
   * composited from a surface with no alpha it brings the whole square with it.
   * A symbol that is mostly transparent needs a surface that can be.
   */
  surf = xa_surface_create((int)SWATCH, (int)SWATCH, XA_DEPTH_ALPHA);
  if (surf == XA_SURFACE_NONE)
  {
    return;
  }
  xa_surface_clear(surf);          // transparent, not whatever malloc left
  pen = xa_pen_create(surf);
  astir_symbol_draw_glyph(g, surf, pen, 0, 0, SWATCH, ' ', 1.0);

  cs = xa_gtk4_surface_of(surf);
  if (cs != NULL)
  {
    cairo_set_source_surface(cr, cs, x, y);
    cairo_paint(cr);
  }
  xa_pen_destroy(pen);
  xa_surface_destroy(surf);
}


static GtkWidget *swatch_new(char table, char symbol)
{
  GtkWidget  *area = gtk_drawing_area_new();
  swatch_sym *s    = g_new0(swatch_sym, 1);

  s->table  = table;
  s->symbol = symbol;
  gtk_widget_set_size_request(area, (int)SWATCH + 6, (int)SWATCH + 6);
  gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), swatch_draw,
                                 s, g_free);
  return area;
}


static void swatch_set(GtkWidget *area, char table, char symbol)
{
  swatch_sym *s = g_new0(swatch_sym, 1);

  s->table  = table;
  s->symbol = symbol;
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), swatch_draw,
                                 s, g_free);
  gtk_widget_queue_draw(area);
}


/* ---- the drop-down rows ------------------------------------------------ */

static void row_setup(GtkSignalListItemFactory *f, GtkListItem *item,
                      gpointer u)
{
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

  (void)f; (void)u;
  gtk_box_append(GTK_BOX(box), swatch_new('/', '-'));
  gtk_box_append(GTK_BOX(box), gtk_label_new(""));
  gtk_list_item_set_child(item, box);
}


static void row_bind(GtkSignalListItemFactory *f, GtkListItem *item,
                     gpointer u)
{
  GtkWidget *box   = gtk_list_item_get_child(item);
  GtkWidget *sw    = gtk_widget_get_first_child(box);
  GtkWidget *label = gtk_widget_get_next_sibling(sw);
  guint      pos   = gtk_list_item_get_position(item);

  (void)f; (void)u;
  if (pos < (guint)NSYMBOLS)
  {
    swatch_set(sw, symbols[pos].group, symbols[pos].symbol);
    gtk_widget_set_visible(sw, TRUE);
    gtk_label_set_text(GTK_LABEL(label), symbols[pos].label);
  }
  else
  {
    // "Other" has no symbol of its own to show.
    gtk_widget_set_visible(sw, FALSE);
    gtk_label_set_text(GTK_LABEL(label), "Other \xe2\x80\x94 set the code below");
  }
}


typedef struct
{
  GtkWidget *dialog;
  GtkWidget *call;
  GtkWidget *grid;               // Maidenhead locator
  GtkWidget *lat;
  GtkWidget *lon;
  GtkWidget *where;              // live description of the resolved position
  GtkWidget *sym_pick;           // the common-symbols drop-down
  GtkWidget *sym_raw;            // the two characters, for anything else
  GtkWidget *sym_swatch;         // what those two characters look like
  GtkWidget *comment;
  int        first_run;
  int        updating;           // guard against paired fields fighting
} mystation;


int xa_gtk4_mystation_needed(void)
{
  // NOCALL is what xa_config.c falls back to when the setting is absent.
  if (my_callsign[0] == '\0' || strcmp(my_callsign, "NOCALL") == 0)
  {
    return 1;
  }
  // 0000.000N / 00000.000W is the "no position" default it writes.
  if (strncmp(my_lat, "0000.000", 8) == 0
      && strncmp(my_long, "00000.000", 9) == 0)
  {
    return 1;
  }
  return 0;
}


// Astir units to signed decimal degrees, which is what a person reads.
static void to_degrees(long lat, long lon, double *lat_deg, double *lon_deg)
{
  *lat_deg =  90.0 - (double)lat / 360000.0;
  *lon_deg = (double)lon / 360000.0 - 180.0;
}


static void describe(mystation *m, long lat, long lon)
{
  double lat_deg, lon_deg;
  char   buf[128];

  to_degrees(lat, lon, &lat_deg, &lon_deg);
  astir_snprintf(buf, sizeof(buf), "%.4f%c  %.4f%c   (grid %s)",
                 lat_deg < 0 ? -lat_deg : lat_deg, lat_deg < 0 ? 'S' : 'N',
                 lon_deg < 0 ? -lon_deg : lon_deg, lon_deg < 0 ? 'W' : 'E',
                 sec_to_loc(lon, lat));
  gtk_label_set_text(GTK_LABEL(m->where), buf);
}


// Typing a grid square fills in the latitude and longitude.
static void on_grid_changed(GtkEditable *e, gpointer data)
{
  mystation  *m = data;
  const char *txt = gtk_editable_get_text(e);
  long        lat, lon;
  char        buf[32];
  double      lat_deg, lon_deg;

  if (m->updating)
  {
    return;
  }
  if (!loc_to_sec(txt, &lon, &lat))
  {
    if (txt[0] != '\0')
    {
      gtk_label_set_text(GTK_LABEL(m->where),
                         "Not a grid square \xe2\x80\x94 four or six "
                         "characters, like CN87 or CN87us.");
    }
    return;
  }

  to_degrees(lat, lon, &lat_deg, &lon_deg);
  m->updating = 1;
  astir_snprintf(buf, sizeof(buf), "%.4f", lat_deg);
  gtk_editable_set_text(GTK_EDITABLE(m->lat), buf);
  astir_snprintf(buf, sizeof(buf), "%.4f", lon_deg);
  gtk_editable_set_text(GTK_EDITABLE(m->lon), buf);
  m->updating = 0;

  describe(m, lat, lon);
}


// ...and typing coordinates updates the grid square, for the same reason.
static void on_coord_changed(GtkEditable *e, gpointer data)
{
  mystation *m = data;
  double     lat_deg, lon_deg;
  long       lat, lon;

  (void)e;
  if (m->updating)
  {
    return;
  }
  lat_deg = atof(gtk_editable_get_text(GTK_EDITABLE(m->lat)));
  lon_deg = atof(gtk_editable_get_text(GTK_EDITABLE(m->lon)));

  if (lat_deg < -90.0 || lat_deg > 90.0 || lon_deg < -180.0 || lon_deg > 180.0)
  {
    gtk_label_set_text(GTK_LABEL(m->where),
                       "Latitude is -90 to 90, longitude -180 to 180.");
    return;
  }

  lat = (long)((90.0 - lat_deg) * 360000.0);
  lon = (long)((lon_deg + 180.0) * 360000.0);

  m->updating = 1;
  gtk_editable_set_text(GTK_EDITABLE(m->grid), sec_to_loc(lon, lat));
  m->updating = 0;

  describe(m, lat, lon);
}


// The drop-down and the two raw characters are two views of one setting, and
// are kept in step the same way the position fields are.
static void on_sym_pick(GtkDropDown *dd, GParamSpec *ps, gpointer data)
{
  mystation *m = data;
  guint      sel = gtk_drop_down_get_selected(dd);
  char       buf[3];

  (void)ps;
  // "Other" describes the code field rather than setting it, so picking it
  // changes nothing -- the two characters below are already the answer.
  if (m->updating || sel >= (guint)NSYMBOLS)
  {
    return;
  }
  buf[0] = symbols[sel].group;
  buf[1] = symbols[sel].symbol;
  buf[2] = '\0';

  m->updating = 1;
  gtk_editable_set_text(GTK_EDITABLE(m->sym_raw), buf);
  m->updating = 0;
}


static void on_sym_raw(GtkEditable *e, gpointer data)
{
  mystation  *m = data;
  const char *txt = gtk_editable_get_text(e);
  int         i;

  if (strlen(txt) != 2)
  {
    return;
  }
  // The preview follows the code even while the list is being updated: it
  // shows what was typed, and that is true either way.
  swatch_set(m->sym_swatch, txt[0], txt[1]);

  if (m->updating)
  {
    return;
  }
  m->updating = 1;
  for (i = 0; i < NSYMBOLS; i++)
  {
    if (symbols[i].group == txt[0] && symbols[i].symbol == txt[1])
    {
      gtk_drop_down_set_selected(GTK_DROP_DOWN(m->sym_pick), (guint)i);
      break;
    }
  }
  if (i == NSYMBOLS)
  {
    // Not one of the common ones.  Say so, rather than let the list keep
    // showing whichever entry it happened to be on.
    gtk_drop_down_set_selected(GTK_DROP_DOWN(m->sym_pick), (guint)SYM_OTHER);
  }
  m->updating = 0;
}


/*
 * Take what was typed, if it is usable.
 *
 * A blank or unparseable field leaves the existing setting alone rather than
 * writing a zero over it.  Somebody who opens this window and closes it again
 * must not end up worse off than before they did.
 */
static void apply(mystation *m)
{
  const char *call = gtk_editable_get_text(GTK_EDITABLE(m->call));
  const char *sym  = gtk_editable_get_text(GTK_EDITABLE(m->sym_raw));
  const char *cmt  = gtk_editable_get_text(GTK_EDITABLE(m->comment));
  double      lat_deg, lon_deg;

  if (call != NULL && call[0] != '\0')
  {
    astir_snprintf(my_callsign, sizeof(my_callsign), "%s", call);
    (void)to_upper(my_callsign);
  }

  lat_deg = atof(gtk_editable_get_text(GTK_EDITABLE(m->lat)));
  lon_deg = atof(gtk_editable_get_text(GTK_EDITABLE(m->lon)));
  if (lat_deg >= -90.0 && lat_deg <= 90.0
      && lon_deg >= -180.0 && lon_deg <= 180.0
      && !(lat_deg == 0.0 && lon_deg == 0.0))
  {
    long lat = (long)((90.0 - lat_deg) * 360000.0);
    long lon = (long)((lon_deg + 180.0) * 360000.0);

    /*
     * The station's position and the map's position are different settings and
     * both have to be set.
     *
     * my_lat/my_long is where this station IS: it is what distance and bearing
     * to every other station are measured from, what a server filter is built
     * around, and what would be beaconed.  center_latitude/center_longitude is
     * merely where the map is looking.  Setting only the second -- which is
     * what this window did in its first draft -- leaves the program computing
     * every distance from the Gulf of Guinea while showing you your home town,
     * which is the kind of wrong that looks right.
     *
     * The map only follows on first run.  Afterwards, moving your station is
     * not a request to be teleported away from wherever you were looking.
     */
    convert_lat_l2s(lat, my_lat,  sizeof(my_lat),  CONVERT_HP_NOSP);
    convert_lon_l2s(lon, my_long, sizeof(my_long), CONVERT_HP_NOSP);

    if (m->first_run)
    {
      center_latitude  = lat;
      center_longitude = lon;
    }
  }

  if (sym != NULL && strlen(sym) == 2)
  {
    my_group  = sym[0];
    my_symbol = sym[1];
  }

  if (cmt != NULL)
  {
    astir_snprintf(my_comment, sizeof(my_comment), "%s", cmt);
  }

  save_data();
}


static void on_save(GtkButton *b, gpointer data)
{
  mystation *m = data;

  (void)b;
  apply(m);
  gtk_window_destroy(GTK_WINDOW(m->dialog));
}


// Save, then open the interface window, because the next thing that has to
// happen is not obvious from anywhere else in the program.
static void on_save_and_interfaces(GtkButton *b, gpointer data)
{
  mystation *m = data;
  GtkWindow *parent = gtk_window_get_transient_for(GTK_WINDOW(m->dialog));

  (void)b;
  apply(m);
  gtk_window_destroy(GTK_WINDOW(m->dialog));
  xa_gtk4_interfaces_show(parent);
}


static void on_cancel(GtkButton *b, gpointer data)
{
  mystation *m = data;

  (void)b;
  gtk_window_destroy(GTK_WINDOW(m->dialog));
}


static void on_destroy(GtkWidget *w, gpointer data)
{
  (void)w;
  g_free(data);
}


static GtkWidget *labelled(const char *text, GtkWidget *entry, int width)
{
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *l   = gtk_label_new(text);

  gtk_widget_set_size_request(l, 120, -1);
  gtk_widget_set_halign(l, GTK_ALIGN_END);
  if (GTK_IS_EDITABLE(entry))
  {
    gtk_editable_set_width_chars(GTK_EDITABLE(entry), width);
  }
  gtk_box_append(GTK_BOX(row), l);
  gtk_box_append(GTK_BOX(row), entry);
  return row;
}


static GtkWidget *dim_note(const char *text)
{
  GtkWidget *l = gtk_label_new(text);

  gtk_label_set_wrap(GTK_LABEL(l), TRUE);
  gtk_label_set_xalign(GTK_LABEL(l), 0.0);
  gtk_widget_add_css_class(l, "dim-label");
  return l;
}


void xa_gtk4_mystation_show(GtkWindow *parent, int first_run)
{
  mystation  *m = g_new0(mystation, 1);
  GtkWidget  *box, *buttons, *b_save, *b_ifaces, *b_cancel, *sep, *row;
  const char *names[NSYMBOLS + 2];   // + "Other", + the NULL terminator
  char        buf[32];
  int         i;

  m->first_run = first_run;

  m->dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(m->dialog),
                       first_run ? "Welcome to Astir" : "My Station");
  gtk_window_set_transient_for(GTK_WINDOW(m->dialog), parent);
  gtk_window_set_modal(GTK_WINDOW(m->dialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(m->dialog), 540, -1);
  g_signal_connect(m->dialog, "destroy", G_CALLBACK(on_destroy), m);

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 18);
  gtk_widget_set_margin_bottom(box, 18);
  gtk_widget_set_margin_start(box, 18);
  gtk_widget_set_margin_end(box, 18);

  if (first_run)
  {
    GtkWidget *intro = gtk_label_new(
      "Astir needs two things before it can be much use: your callsign, and "
      "roughly where you are. Your position is what every distance and bearing "
      "to another station is measured from.");
    gtk_label_set_wrap(GTK_LABEL(intro), TRUE);
    gtk_label_set_xalign(GTK_LABEL(intro), 0.0);
    gtk_box_append(GTK_BOX(box), intro);
  }

  /* ---- callsign ---- */
  m->call = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(m->call), "N0CALL");
  if (my_callsign[0] != '\0' && strcmp(my_callsign, "NOCALL") != 0)
  {
    gtk_editable_set_text(GTK_EDITABLE(m->call), my_callsign);
  }
  gtk_box_append(GTK_BOX(box), labelled("Callsign", m->call, 12));

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(box), sep);

  /* ---- position ---- */
  m->grid = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(m->grid), "CN87us");
  gtk_box_append(GTK_BOX(box), labelled("Grid square", m->grid, 10));

  gtk_box_append(GTK_BOX(box), dim_note(
    "Your Maidenhead locator \xe2\x80\x94 four or six characters. If you do "
    "not know it, enter the latitude and longitude instead and it will be "
    "filled in."));

  m->lat = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(m->lat), "47.6062");
  gtk_box_append(GTK_BOX(box), labelled("Latitude", m->lat, 12));

  m->lon = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(m->lon), "-122.3321");
  gtk_box_append(GTK_BOX(box), labelled("Longitude", m->lon, 12));

  m->where = gtk_label_new("Decimal degrees; south and west are negative.");
  gtk_label_set_xalign(GTK_LABEL(m->where), 0.0);
  gtk_widget_add_css_class(m->where, "dim-label");
  gtk_box_append(GTK_BOX(box), m->where);

  g_signal_connect(m->grid, "changed", G_CALLBACK(on_grid_changed), m);
  g_signal_connect(m->lat,  "changed", G_CALLBACK(on_coord_changed), m);
  g_signal_connect(m->lon,  "changed", G_CALLBACK(on_coord_changed), m);

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(box), sep);

  /* ---- symbol ---- */
  for (i = 0; i < NSYMBOLS; i++)
  {
    names[i] = symbols[i].label;
  }
  names[SYM_OTHER]     = "Other \xe2\x80\x94 set the code below";
  names[SYM_OTHER + 1] = NULL;

  m->sym_pick = gtk_drop_down_new_from_strings(names);

  // Each row draws its own symbol.  A list of names alone asks the operator to
  // already know what "Portable / EOC" looks like, which is the thing they came
  // here to find out.
  {
    GtkListItemFactory *fac = gtk_signal_list_item_factory_new();

    g_signal_connect(fac, "setup", G_CALLBACK(row_setup), NULL);
    g_signal_connect(fac, "bind",  G_CALLBACK(row_bind),  NULL);
    gtk_drop_down_set_factory(GTK_DROP_DOWN(m->sym_pick), fac);
    g_object_unref(fac);
  }
  gtk_box_append(GTK_BOX(box), labelled("Symbol", m->sym_pick, 0));

  // The code field gets its own preview, because it accepts all 211 symbols
  // and the list only names fifteen.
  m->sym_raw = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(m->sym_raw), 2);
  m->sym_swatch = swatch_new('/', 'x');
  row = labelled("Symbol code", m->sym_raw, 4);
  gtk_box_append(GTK_BOX(row), m->sym_swatch);
  gtk_box_append(GTK_BOX(box), row);
  gtk_box_append(GTK_BOX(box), dim_note(
    "Table and code, two characters \xe2\x80\x94 \"/-\" is a house. Any APRS "
    "symbol can be typed here; the list above covers the common ones."));

  // Seed from the current setting, which also selects the matching list entry.
  buf[0] = my_group  != '\0' ? my_group  : '/';
  buf[1] = my_symbol != '\0' ? my_symbol : '-';
  buf[2] = '\0';
  gtk_editable_set_text(GTK_EDITABLE(m->sym_raw), buf);

  g_signal_connect(m->sym_pick, "notify::selected",
                   G_CALLBACK(on_sym_pick), m);
  g_signal_connect(m->sym_raw, "changed", G_CALLBACK(on_sym_raw), m);
  on_sym_raw(GTK_EDITABLE(m->sym_raw), m);   // select the list entry now

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(box), sep);

  /* ---- comment ---- */
  m->comment = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(m->comment), MAX_COMMENT);
  // Generic on purpose.  This used to read "Astir - Seattle, WA", which is a
  // real place that is not the operator's, and a placeholder naming somewhere
  // specific reads as sample data left in by mistake -- doubly so on the
  // first-run window, where every other field is genuinely filled in.
  gtk_entry_set_placeholder_text(GTK_ENTRY(m->comment),
                                 "sent with every beacon; may be left empty");
  gtk_editable_set_text(GTK_EDITABLE(m->comment), my_comment);
  row = labelled("Comment", m->comment, 28);
  gtk_widget_set_hexpand(m->comment, TRUE);
  gtk_box_append(GTK_BOX(box), row);

  /* ---- what happens next ---- */
  if (first_run)
  {
    GtkWidget *note;

    sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);

    // Setting a position does not make traffic arrive, and there is nothing
    // else in the program that would tell them so.
    note = gtk_label_new(
      "Astir receives nothing until an interface is set up. Connect it to a "
      "radio or a software TNC \xe2\x80\x94 or, with no radio at all, to the "
      "APRS-IS network over the internet, which takes one click and is "
      "receive-only.");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.0);
    gtk_box_append(GTK_BOX(box), note);
  }

  buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(buttons, GTK_ALIGN_END);

  b_cancel = gtk_button_new_with_label(first_run ? "Not now" : "Cancel");
  b_save   = gtk_button_new_with_label("Save");
  g_signal_connect(b_cancel, "clicked", G_CALLBACK(on_cancel), m);
  g_signal_connect(b_save,   "clicked", G_CALLBACK(on_save), m);
  gtk_box_append(GTK_BOX(buttons), b_cancel);
  gtk_box_append(GTK_BOX(buttons), b_save);

  if (first_run)
  {
    b_ifaces = gtk_button_new_with_label(
                 "Save and set up interfaces\xe2\x80\xa6");
    gtk_widget_add_css_class(b_ifaces, "suggested-action");
    g_signal_connect(b_ifaces, "clicked",
                     G_CALLBACK(on_save_and_interfaces), m);
    gtk_box_append(GTK_BOX(buttons), b_ifaces);
  }
  else
  {
    gtk_widget_add_css_class(b_save, "suggested-action");
  }

  gtk_box_append(GTK_BOX(box), buttons);

  /*
   * Type into the grid field for us, so the conversion can be checked.
   *
   * loc_to_sec() is unit tested; what is not, and cannot be without input
   * automation, is that this window's "changed" handlers are connected to the
   * right widgets and fill the other fields in.  Setting the text here goes
   * through exactly the same signal a keystroke would, so a screenshot of the
   * result tests the wiring rather than the arithmetic.
   */
  {
    const char *seed = getenv("ASTIR_GTK4_MYSTATION_GRID");

    if (seed != NULL)
    {
      gtk_editable_set_text(GTK_EDITABLE(m->grid), seed);

      /*
       * ...and press Save, if asked to.
       *
       * This exists because the first draft of apply() wrote the map centre
       * and not the station position -- the program would then have shown the
       * operator their home town while measuring every distance and bearing
       * from the Gulf of Guinea.  It read correctly and displayed correctly;
       * only the file it wrote was wrong.  Nothing on screen could have caught
       * that, so the check has to be "type a position, save, and read the
       * config back", which needs a way to press the button.
       */
      if (getenv("ASTIR_GTK4_MYSTATION_SAVE") != NULL)
      {
        apply(m);
      }
    }
    else if (!first_run)
    {
      // Show where the station already is, rather than an empty form.
      long lat = convert_lat_s2l(my_lat);
      long lon = convert_lon_s2l(my_long);

      if (!(lat == 0 && lon == 0))
      {
        char d[32];

        m->updating = 1;
        gtk_editable_set_text(GTK_EDITABLE(m->grid), sec_to_loc(lon, lat));
        astir_snprintf(d, sizeof(d), "%.4f", 90.0 - (double)lat / 360000.0);
        gtk_editable_set_text(GTK_EDITABLE(m->lat), d);
        astir_snprintf(d, sizeof(d), "%.4f",
                       (double)lon / 360000.0 - 180.0);
        gtk_editable_set_text(GTK_EDITABLE(m->lon), d);
        m->updating = 0;
        describe(m, lat, lon);
      }
    }
  }

  gtk_window_set_child(GTK_WINDOW(m->dialog), box);
  gtk_window_present(GTK_WINDOW(m->dialog));
}
