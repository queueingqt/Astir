/*
 * The interface control window.  See the header for why it is its own file.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "core/astir.h"
#include "core/io/interface.h"
#include "core/state/xa_config.h"
#include "core/state/xa_settings.h"
#include "core/util/snprintf.h"
#include "ui/gtk4/xa_gtk4_interfaces.h"

// One window, and the timer that keeps its status column honest.
static GtkWidget *iface_win;
static GtkWidget *iface_list;

static void rebuild_rows(void);


/*
 * What kind of interface, in words a person recognises.
 *
 * The core's own names come out of the language file and are written for the
 * Motif option menu ("Networked AGWPE", "Serial TNC"); these say what you would
 * plug in.  Only the types this front end can currently configure are offered.
 * The others still LOAD and still work if they are already in the config -- the
 * list below decides what can be created, not what can be run.
 */
typedef struct
{
  int         type;
  const char *label;
  const char *hint;
  int         is_network;
  int         default_port;
} iface_kind;

static const iface_kind kinds[] =
{
  {
    DEVICE_NET_AGWPE, "Software TNC (AGWPE)",
    "Direwolf, or anything else speaking AGWPE. This is how a sound-card TNC "
    "on this machine or on the network is reached.",
    1, 8000
  },
  {
    DEVICE_NET_STREAM, "APRS-IS internet server",
    "The APRS-IS network, over the internet. No radio involved: useful for "
    "seeing live traffic, and for proving the receive path works.",
    1, 14580
  },
  {
    DEVICE_SERIAL_KISS_TNC, "Serial KISS TNC",
    "A hardware TNC in KISS mode on a serial port, such as /dev/ttyUSB0.",
    0, 9600
  },
  {
    DEVICE_SERIAL_TNC, "Serial TNC (command mode)",
    "An older TNC driven with command strings rather than KISS framing.",
    0, 9600
  },
};

#define NKINDS ((int)(sizeof(kinds) / sizeof(kinds[0])))


static const iface_kind *kind_of(int type)
{
  int i;

  for (i = 0; i < NKINDS; i++)
  {
    if (kinds[i].type == type)
    {
      return &kinds[i];
    }
  }
  return NULL;
}


/*
 * A name for EVERY device type, including the ones this window cannot create.
 *
 * The two lists are deliberately different sizes.  kinds[] is what can be
 * added; this is what can be shown.  A config carried over from another program
 * -- or written by hand, or by an older version -- can hold a type that is not
 * offered here, and calling it "unknown type" would be this window admitting it
 * had not looked.  The interface still runs, still starts and stops, and still
 * says what it is; it just cannot be edited here yet.
 */
static const char *type_name(int type)
{
  const iface_kind *k = kind_of(type);

  if (k != NULL)
  {
    return k->label;
  }
  switch (type)
  {
    case DEVICE_SERIAL_TNC_HSP_GPS:  return "Serial TNC with GPS (HSP cable)";
    case DEVICE_SERIAL_TNC_AUX_GPS:  return "Serial TNC with GPS (aux port)";
    case DEVICE_SERIAL_GPS:          return "Serial GPS";
    case DEVICE_SERIAL_WX:           return "Serial weather station";
    case DEVICE_AX25_TNC:            return "Kernel AX.25 port";
    case DEVICE_NET_GPSD:            return "GPS via gpsd";
    case DEVICE_NET_WX:              return "Networked weather station";
    case DEVICE_NET_DATABASE:        return "Networked database";
    case DEVICE_SERIAL_MKISS_TNC:    return "Multi-port serial KISS TNC";
    case DEVICE_SQL_DATABASE:        return "SQL database";
    default:                         return "Interface";
  }
}


// Whether the add/edit dialog understands this type well enough to change it.
static int type_is_editable(int type)
{
  return kind_of(type) != NULL;
}


// A device slot that has never been configured.  device_type is the only field
// that says so; the rest of the record is whatever was last in it.
static int slot_in_use(int i)
{
  return devices[i].device_type != DEVICE_NONE;
}


static int first_free_slot(void)
{
  int i;

  for (i = 0; i < MAX_IFACE_DEVICES; i++)
  {
    if (!slot_in_use(i))
    {
      return i;
    }
  }
  return -1;
}


/*
 * A one-line description of where this interface points.
 *
 * Host and port for a network device, device node and speed for a serial one.
 * The same two fields carry both -- sp is "serial port speed/Net port" in the
 * core -- so which one to show depends on the type, not on which is set.
 */
static void describe_target(int i, char *out, size_t n)
{
  const iface_kind *k = kind_of(devices[i].device_type);

  if (k != NULL && k->is_network)
  {
    astir_snprintf(out, n, "%s:%d",
                   devices[i].device_host_name[0] ? devices[i].device_host_name
                                                  : "(no host)",
                   devices[i].sp);
  }
  else
  {
    astir_snprintf(out, n, "%s @ %d",
                   devices[i].device_name[0] ? devices[i].device_name
                                             : "(no device)",
                   devices[i].sp);
  }
}


/* ---- starting and stopping --------------------------------------------- */

/*
 * Redraw the list, but not from inside a button's own click handler.
 *
 * rebuild_rows() destroys every row and builds new ones, and the button being
 * clicked is inside one of them -- destroying the widget GTK is in the middle
 * of dispatching to.  Deferring to an idle callback lets the click finish
 * first.
 *
 * It also fixes what that looked like.  Bringing a port up does not block until
 * it is up: a connect runs on its own thread and can take seconds, longer still
 * when a name resolves to an IPv6 address with no route and the attempt has to
 * time out.  Reading the status back on the next line therefore read it before
 * anything could have happened, and painted "Error" on an interface that went on
 * to connect perfectly well a moment later.  The half-second poll is what
 * reports the outcome; this only refreshes the buttons.
 */
static gboolean rebuild_soon(gpointer unused)
{
  (void)unused;
  if (iface_win != NULL)
  {
    rebuild_rows();
  }
  return G_SOURCE_REMOVE;
}


static void on_start(GtkButton *b, gpointer data)
{
  int port = GPOINTER_TO_INT(data);

  (void)b;
  // A specific port rather than -1: bring up THIS one whatever its "connect on
  // startup" setting says.  Pressing Start is the override.
  startup_all_or_defined_port(port);
  g_idle_add(rebuild_soon, NULL);
}


static void on_stop(GtkButton *b, gpointer data)
{
  int port = GPOINTER_TO_INT(data);

  (void)b;
  shutdown_all_active_or_defined_port(port);
  g_idle_add(rebuild_soon, NULL);
}


static void on_delete(GtkButton *b, gpointer data)
{
  int port = GPOINTER_TO_INT(data);

  (void)b;
  if (get_device_status(port) == DEVICE_UP)
  {
    shutdown_all_active_or_defined_port(port);
  }
  // Clearing the type is what marks the slot free; the record itself is left
  // alone, which is harmless and keeps the fields around if it is re-created.
  devices[port].device_type = DEVICE_NONE;
  devices[port].connect_on_startup = 0;
  save_data();                   // so a deleted interface stays deleted
  g_idle_add(rebuild_soon, NULL);   // not from inside this button's handler
}


/* ---- the add/edit dialog ------------------------------------------------ */

typedef struct
{
  int        port;               /* -1 for a new interface */
  GtkWidget *dialog;
  GtkWidget *kind;               /* GtkDropDown over kinds[] */
  GtkWidget *target;             /* host, or serial device node */
  GtkWidget *port_num;           /* TCP port, or serial speed */
  GtkWidget *comment;
  GtkWidget *passcode;
  GtkWidget *filter;
  GtkWidget *on_startup;
  GtkWidget *transmit;
  GtkWidget *target_label;
  GtkWidget *port_label;
  GtkWidget *hint;
  GtkWidget *tx_note;            /* why transmit is unavailable, when it is */
} iface_dialog;


// APRS-IS calls -1 "receive only" and means it: the server accepts the login
// and then refuses everything sent up it.  Anything else numeric and positive
// is a real passcode.
static int passcode_allows_transmit(const char *pass)
{
  return (pass != NULL) && (pass[0] != '\0') && (atoi(pass) > 0);
}


/*
 * Offer the transmit setting only where it could do something.
 *
 * Two quite different reasons it might not, and they want different words: the
 * kind of device never transmits at all, or it does but this particular login
 * is not allowed to.  Saying "no" without saying which leaves the operator to
 * guess, and the second one is fixable in the box directly above it.
 *
 * Turned OFF as well as disabled.  A tick left set on a setting that cannot
 * apply is exactly the stale flag this is meant to stop being written.
 */
static void refresh_transmit_availability(iface_dialog *d)
{
  guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(d->kind));
  const iface_kind *k;
  const char *why = "";

  if (sel >= (guint)NKINDS)
  {
    return;
  }
  k = &kinds[sel];

  if (!device_type_can_transmit(k->type))
  {
    why = "This kind of interface only ever receives.";
  }
  else if (k->type == DEVICE_NET_STREAM
           && !passcode_allows_transmit(
                gtk_editable_get_text(GTK_EDITABLE(d->passcode))))
  {
    why = "A receive-only login cannot transmit. APRS-IS accepts passcode -1 "
          "and then refuses anything sent up it. Enter the passcode for your "
          "callsign above to enable this \xe2\x80\x94 astir_callpass computes it.";
  }

  gtk_widget_set_sensitive(d->transmit, why[0] == '\0');
  if (why[0] != '\0')
  {
    gtk_check_button_set_active(GTK_CHECK_BUTTON(d->transmit), FALSE);
  }
  gtk_label_set_text(GTK_LABEL(d->tx_note), why);
  gtk_widget_set_visible(d->tx_note, why[0] != '\0');
}


static void on_passcode_changed(GtkEditable *e, gpointer data)
{
  (void)e;
  refresh_transmit_availability(data);
}


// Follow the type selection: the same two entry boxes mean different things for
// a network interface and a serial one, and a box whose label is wrong is worse
// than no box.
static void on_kind_changed(GtkDropDown *dd, GParamSpec *ps, gpointer data)
{
  iface_dialog *d = data;
  guint sel = gtk_drop_down_get_selected(dd);
  const iface_kind *k;
  char buf[16];

  (void)ps;
  if (sel >= (guint)NKINDS)
  {
    return;
  }
  k = &kinds[sel];

  gtk_label_set_text(GTK_LABEL(d->target_label), k->is_network ? "Host"
                                                               : "Device");
  gtk_label_set_text(GTK_LABEL(d->port_label), k->is_network ? "TCP port"
                                                             : "Speed");
  gtk_label_set_text(GTK_LABEL(d->hint), k->hint);

  if (d->port == -1)             // only prefill a NEW interface
  {
    gtk_editable_set_text(GTK_EDITABLE(d->target),
                          k->is_network ? "localhost" : "/dev/ttyUSB0");
    astir_snprintf(buf, sizeof(buf), "%d", k->default_port);
    gtk_editable_set_text(GTK_EDITABLE(d->port_num), buf);
  }

  // A passcode and a server filter are APRS-IS ideas and mean nothing to a TNC.
  gtk_widget_set_visible(gtk_widget_get_parent(d->passcode),
                         k->type == DEVICE_NET_STREAM);
  gtk_widget_set_visible(gtk_widget_get_parent(d->filter),
                         k->type == DEVICE_NET_STREAM);

  refresh_transmit_availability(d);
}


static void on_dialog_save(GtkButton *b, gpointer data)
{
  iface_dialog *d = data;
  guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(d->kind));
  const iface_kind *k;
  int port = d->port;

  (void)b;
  if (sel >= (guint)NKINDS)
  {
    return;
  }
  k = &kinds[sel];

  if (port < 0)
  {
    port = first_free_slot();
    if (port < 0)
    {
      return;                    // all 15 slots taken
    }
  }

  memset(&devices[port], 0, sizeof(devices[port]));
  devices[port].device_type = k->type;

  if (k->is_network)
  {
    astir_snprintf(devices[port].device_host_name,
                   sizeof(devices[port].device_host_name), "%s",
                   gtk_editable_get_text(GTK_EDITABLE(d->target)));
  }
  else
  {
    astir_snprintf(devices[port].device_name,
                   sizeof(devices[port].device_name), "%s",
                   gtk_editable_get_text(GTK_EDITABLE(d->target)));
  }
  devices[port].sp = atoi(gtk_editable_get_text(GTK_EDITABLE(d->port_num)));

  astir_snprintf(devices[port].comment, sizeof(devices[port].comment), "%s",
                 gtk_editable_get_text(GTK_EDITABLE(d->comment)));

  if (k->type == DEVICE_NET_STREAM)
  {
    astir_snprintf(devices[port].device_host_pswd,
                   sizeof(devices[port].device_host_pswd), "%s",
                   gtk_editable_get_text(GTK_EDITABLE(d->passcode)));
    astir_snprintf(devices[port].device_host_filter_string,
                   sizeof(devices[port].device_host_filter_string), "%s",
                   gtk_editable_get_text(GTK_EDITABLE(d->filter)));
  }

  devices[port].connect_on_startup =
    gtk_check_button_get_active(GTK_CHECK_BUTTON(d->on_startup)) ? 1 : 0;
  devices[port].transmit_data =
    gtk_check_button_get_active(GTK_CHECK_BUTTON(d->transmit)) ? 1 : 0;
  devices[port].reconnect = 1;   // a dropped link should come back by itself

  save_data();                   // an interface that is not saved is not an
                                 // interface; it is a session
  gtk_window_destroy(GTK_WINDOW(d->dialog));
  g_idle_add(rebuild_soon, NULL);
}


/*
 * One click to the thing that works without a radio.
 *
 * A fresh install receives nothing until an interface exists, which is correct
 * -- but it means Astir sits there drawing a world map and never doing anything,
 * and nothing on screen says why.  APRS-IS is the honest answer to that: it
 * needs no radio, no licence and no hardware, and it proves the whole receive
 * path in about ten seconds.
 *
 * Receive-only, and not by accident.  Passcode -1 is the documented read-only
 * login: the server sends traffic and will not accept any, so a button that
 * puts a beginner on the network cannot put them on the AIR.  transmit_data is
 * left at zero for the same reason.
 *
 * Filtered to where the operator is, because the alternative is the entire
 * planet's traffic -- tens of thousands of stations, which is not a useful
 * first experience and is rude to the server.  The radius is deliberately
 * generous: 200 km covers anywhere a VHF signal might plausibly have come from,
 * and forgives a position that is only accurate to a grid square.
 */
static void on_add_aprsis(GtkButton *b, gpointer unused)
{
  int    port;
  double lat_deg, lon_deg;

  (void)b; (void)unused;

  port = first_free_slot();
  if (port < 0)
  {
    return;                        // all slots taken
  }

  // Astir units are 1/100 second, with latitude 0 at 90N and longitude 0 at
  // 180W.  APRS-IS wants signed decimal degrees.
  lat_deg =  90.0 - (double)center_latitude  / 360000.0;
  lon_deg = (double)center_longitude / 360000.0 - 180.0;

  memset(&devices[port], 0, sizeof(devices[port]));
  devices[port].device_type = DEVICE_NET_STREAM;
  astir_snprintf(devices[port].device_host_name,
                 sizeof(devices[port].device_host_name), "%s",
                 "noam.aprs2.net");
  devices[port].sp = 14580;
  astir_snprintf(devices[port].device_host_pswd,
                 sizeof(devices[port].device_host_pswd), "%s", "-1");
  astir_snprintf(devices[port].device_host_filter_string,
                 sizeof(devices[port].device_host_filter_string),
                 "r/%.2f/%.2f/200", lat_deg, lon_deg);
  astir_snprintf(devices[port].comment, sizeof(devices[port].comment), "%s",
                 "APRS-IS, receive only");
  devices[port].connect_on_startup = 1;
  devices[port].transmit_data      = 0;   // -1 could not transmit anyway
  devices[port].reconnect          = 1;

  save_data();
  g_idle_add(rebuild_soon, NULL);
}


static void on_dialog_close(GtkButton *b, gpointer data)
{
  iface_dialog *d = data;

  (void)b;
  gtk_window_destroy(GTK_WINDOW(d->dialog));
}


// A label and a widget on one row, returned as the row so the caller can hide
// the pair together.
static GtkWidget *field_row(GtkWidget *grid, int row, const char *label,
                            GtkWidget *w, GtkWidget **label_out)
{
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *l = gtk_label_new(label);

  gtk_widget_set_size_request(l, 110, -1);
  gtk_label_set_xalign(GTK_LABEL(l), 0.0);
  gtk_widget_set_hexpand(w, TRUE);
  gtk_box_append(GTK_BOX(box), l);
  gtk_box_append(GTK_BOX(box), w);
  gtk_grid_attach(GTK_GRID(grid), box, 0, row, 1, 1);
  if (label_out != NULL)
  {
    *label_out = l;
  }
  return box;
}


static void show_dialog(GtkWindow *parent, int port)
{
  iface_dialog *d = g_new0(iface_dialog, 1);
  GtkWidget *box, *grid, *buttons, *save, *cancel;
  GtkStringList *names = gtk_string_list_new(NULL);
  char buf[64];
  int i, sel = 0;

  d->port = port;
  d->dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(d->dialog),
                       port < 0 ? "Add Interface" : "Edit Interface");
  gtk_window_set_transient_for(GTK_WINDOW(d->dialog), parent);
  gtk_window_set_modal(GTK_WINDOW(d->dialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(d->dialog), 460, -1);
  g_object_set_data_full(G_OBJECT(d->dialog), "iface-dialog", d, g_free);

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);
  gtk_window_set_child(GTK_WINDOW(d->dialog), box);

  for (i = 0; i < NKINDS; i++)
  {
    gtk_string_list_append(names, kinds[i].label);
    if (port >= 0 && devices[port].device_type == kinds[i].type)
    {
      sel = i;
    }
  }
  d->kind = gtk_drop_down_new(G_LIST_MODEL(names), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(d->kind), sel);

  grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);

  field_row(grid, 0, "Type", d->kind, NULL);

  d->target = gtk_entry_new();
  field_row(grid, 1, "Host", d->target, &d->target_label);

  d->port_num = gtk_entry_new();
  field_row(grid, 2, "TCP port", d->port_num, &d->port_label);

  d->passcode = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(d->passcode),
                                 "-1 for receive only");
  field_row(grid, 3, "Passcode", d->passcode, NULL);

  d->filter = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(d->filter), "m/100");
  field_row(grid, 4, "Server filter", d->filter, NULL);

  d->comment = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(d->comment), "a name for this port");
  field_row(grid, 5, "Label", d->comment, NULL);

  gtk_box_append(GTK_BOX(box), grid);

  d->hint = gtk_label_new("");
  gtk_label_set_wrap(GTK_LABEL(d->hint), TRUE);
  gtk_label_set_xalign(GTK_LABEL(d->hint), 0.0);
  gtk_widget_add_css_class(d->hint, "dim-label");
  gtk_box_append(GTK_BOX(box), d->hint);

  d->on_startup = gtk_check_button_new_with_label("Connect when Astir starts");
  gtk_box_append(GTK_BOX(box), d->on_startup);

  /*
   * Transmit is off unless it is deliberately turned on, on every new
   * interface.  A default that can put a signal on the air is the wrong
   * default: an unconfigured callsign transmitting is at best rude and in most
   * places not legal, and nothing here can tell whether the operator is
   * licensed for the band the radio is sitting on.
   */
  d->transmit = gtk_check_button_new_with_label(
                  "Allow transmit on this interface");
  gtk_box_append(GTK_BOX(box), d->transmit);

  // Sits under the tick box and says why it is unavailable, when it is.
  d->tx_note = gtk_label_new("");
  gtk_label_set_wrap(GTK_LABEL(d->tx_note), TRUE);
  gtk_label_set_xalign(GTK_LABEL(d->tx_note), 0.0);
  gtk_widget_set_margin_start(d->tx_note, 24);
  gtk_widget_add_css_class(d->tx_note, "dim-label");
  gtk_widget_set_visible(d->tx_note, FALSE);
  gtk_box_append(GTK_BOX(box), d->tx_note);

  if (port >= 0)                 // editing: fill in what is there
  {
    const iface_kind *k = kind_of(devices[port].device_type);

    gtk_editable_set_text(GTK_EDITABLE(d->target),
                          (k != NULL && k->is_network)
                            ? devices[port].device_host_name
                            : devices[port].device_name);
    astir_snprintf(buf, sizeof(buf), "%d", devices[port].sp);
    gtk_editable_set_text(GTK_EDITABLE(d->port_num), buf);
    gtk_editable_set_text(GTK_EDITABLE(d->comment), devices[port].comment);
    gtk_editable_set_text(GTK_EDITABLE(d->passcode),
                          devices[port].device_host_pswd);
    gtk_editable_set_text(GTK_EDITABLE(d->filter),
                          devices[port].device_host_filter_string);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(d->on_startup),
                                devices[port].connect_on_startup != 0);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(d->transmit),
                                devices[port].transmit_data != 0);
  }

  buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(buttons, GTK_ALIGN_END);
  cancel = gtk_button_new_with_label("Cancel");
  save = gtk_button_new_with_label(port < 0 ? "Add" : "Save");
  gtk_widget_add_css_class(save, "suggested-action");
  gtk_box_append(GTK_BOX(buttons), cancel);
  gtk_box_append(GTK_BOX(buttons), save);
  gtk_box_append(GTK_BOX(box), buttons);

  g_signal_connect(cancel, "clicked", G_CALLBACK(on_dialog_close), d);
  g_signal_connect(save, "clicked", G_CALLBACK(on_dialog_save), d);
  g_signal_connect(d->kind, "notify::selected",
                   G_CALLBACK(on_kind_changed), d);
  // Typing a real passcode is what makes transmit available on APRS-IS, so the
  // tick box has to follow the box above it as it is typed.
  g_signal_connect(d->passcode, "changed",
                   G_CALLBACK(on_passcode_changed), d);

  // Run it once so the labels and hint match the selection before it is shown.
  on_kind_changed(GTK_DROP_DOWN(d->kind), NULL, d);

  gtk_window_present(GTK_WINDOW(d->dialog));
}


static void on_add(GtkButton *b, gpointer data)
{
  (void)b;
  show_dialog(GTK_WINDOW(data), -1);
}


static void on_edit(GtkButton *b, gpointer data)
{
  int port = GPOINTER_TO_INT(data);

  (void)b;
  show_dialog(GTK_WINDOW(iface_win), port);
}


/* ---- the list ----------------------------------------------------------- */

static void rebuild_rows(void)
{
  GtkWidget *child;
  int i, shown = 0;

  if (iface_list == NULL)
  {
    return;
  }
  while ((child = gtk_widget_get_first_child(iface_list)) != NULL)
  {
    gtk_list_box_remove(GTK_LIST_BOX(iface_list), child);
  }

  for (i = 0; i < MAX_IFACE_DEVICES; i++)
  {
    GtkWidget *row, *hbox, *vbox, *title, *sub, *dot, *btn;
    char target[160], line[260];
    int status;

    if (!slot_in_use(i))
    {
      continue;
    }
    status = get_device_status(i);
    /*
     * ASTIR_IFACE_TRACE reports what this window believes, once per refresh.
     *
     * Worth keeping: a window that is behind another window is not repainted by
     * the compositor, so a screenshot of it can show a frame minutes old while
     * the data underneath is current.  Chasing that once was enough -- the
     * status looked stuck at Up when it had long since gone to Error, and only
     * this said otherwise.
     */
    if (getenv("ASTIR_IFACE_TRACE") != NULL)
    {
      fprintf(stderr, "[iface] port %d type %d status %d (%s)\n",
              i, devices[i].device_type, status,
              status == DEVICE_UP ? "up"
              : status == DEVICE_ERROR ? "error" : "down");
    }

    row = gtk_list_box_row_new();
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(hbox, 8);
    gtk_widget_set_margin_bottom(hbox, 8);
    gtk_widget_set_margin_start(hbox, 12);
    gtk_widget_set_margin_end(hbox, 12);

    /*
     * Status as a coloured dot AND as a word.
     *
     * The colour is what you read at a glance; the word is what survives being
     * colour-blind, printed, or described to somebody over the air.
     */
    dot = gtk_label_new(status == DEVICE_UP    ? "\xe2\x97\x8f Up"
                        : status == DEVICE_ERROR ? "\xe2\x97\x8f Error"
                        : "\xe2\x97\x8b Down");
    gtk_widget_set_size_request(dot, 66, -1);
    gtk_label_set_xalign(GTK_LABEL(dot), 0.0);
    if (status == DEVICE_UP)
    {
      gtk_widget_add_css_class(dot, "success");
    }
    else if (status == DEVICE_ERROR)
    {
      gtk_widget_add_css_class(dot, "error");
    }
    else
    {
      gtk_widget_add_css_class(dot, "dim-label");
    }

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);

    describe_target(i, target, sizeof(target));
    astir_snprintf(line, sizeof(line), "%s",
                   devices[i].comment[0] ? devices[i].comment
                   : type_name(devices[i].device_type));
    title = gtk_label_new(line);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_widget_add_css_class(title, "heading");
    /*
     * Ellipsize, or the text decides how wide the row is.
     *
     * A label with no ellipsize reports its full text as its minimum width, so
     * "APRS-IS internet server -- noam.aprs2.net:14580, receive only" made the
     * row wider than the window.  The list scrolled sideways to fit it and the
     * buttons on the right went off the edge -- Delete first, because it is
     * last.  A row that cannot be narrowed pushes its own controls out of reach.
     */
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);

    astir_snprintf(line, sizeof(line), "%s \xe2\x80\x94 %s%s",
                   type_name(devices[i].device_type), target,
                   devices[i].transmit_data ? "  \xe2\x80\xa2 transmit enabled"
                                            : "  \xe2\x80\xa2 receive only");
    sub = gtk_label_new(line);
    gtk_label_set_xalign(GTK_LABEL(sub), 0.0);
    gtk_widget_add_css_class(sub, "dim-label");
    gtk_label_set_ellipsize(GTK_LABEL(sub), PANGO_ELLIPSIZE_END);
    // The full text is always reachable, ellipsized or not.
    gtk_widget_set_tooltip_text(sub, line);

    gtk_box_append(GTK_BOX(vbox), title);
    gtk_box_append(GTK_BOX(vbox), sub);

    gtk_box_append(GTK_BOX(hbox), dot);
    gtk_box_append(GTK_BOX(hbox), vbox);

    if (status == DEVICE_UP)
    {
      btn = gtk_button_new_with_label("Stop");
      g_signal_connect(btn, "clicked", G_CALLBACK(on_stop),
                       GINT_TO_POINTER(i));
    }
    else
    {
      btn = gtk_button_new_with_label("Start");
      gtk_widget_add_css_class(btn, "suggested-action");
      g_signal_connect(btn, "clicked", G_CALLBACK(on_start),
                       GINT_TO_POINTER(i));
    }
    gtk_box_append(GTK_BOX(hbox), btn);

    btn = gtk_button_new_with_label("Edit");
    if (type_is_editable(devices[i].device_type))
    {
      g_signal_connect(btn, "clicked", G_CALLBACK(on_edit),
                       GINT_TO_POINTER(i));
    }
    else
    {
      // Greyed rather than hidden, with a reason.  A missing button invites
      // the guess that the interface is broken; a disabled one with a tooltip
      // says which part is not finished, and the interface still runs.
      gtk_widget_set_sensitive(btn, FALSE);
      gtk_widget_set_tooltip_text(btn,
        "This interface type cannot be edited here yet. It still starts and "
        "stops normally; change its settings in ~/.astir/config/astir.cnf.");
    }
    gtk_box_append(GTK_BOX(hbox), btn);

    btn = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_set_tooltip_text(btn, "Delete this interface");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_delete),
                     GINT_TO_POINTER(i));
    gtk_box_append(GTK_BOX(hbox), btn);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), hbox);
    gtk_list_box_append(GTK_LIST_BOX(iface_list), row);
    shown++;
  }

  if (shown == 0)
  {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *l = gtk_label_new("No interfaces configured.\n"
                                 "Add one to connect Astir to a radio, "
                                 "a software TNC, or the APRS-IS network.");
    GtkWidget *quick = gtk_button_new_with_label("Add APRS-IS (receive only)");
    GtkWidget *hint = gtk_label_new(
      "Connects to the internet side of APRS with a receive-only login, "
      "filtered to your area.\nNo radio and no licence needed to listen.");

    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class(l, "dim-label");

    gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);

    gtk_widget_set_halign(quick, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(quick, "suggested-action");
    g_signal_connect(quick, "clicked", G_CALLBACK(on_add_aprsis), NULL);

    gtk_box_append(GTK_BOX(box), l);
    gtk_box_append(GTK_BOX(box), quick);
    gtk_box_append(GTK_BOX(box), hint);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_append(GTK_LIST_BOX(iface_list), row);
  }
}


/*
 * Poll the status column.
 *
 * A port goes up or down on its own thread, without telling anybody: a network
 * interface that loses its server reconnects by itself, and a serial device
 * that is unplugged just stops.  Nothing posts an event, so the only honest way
 * to show live status is to ask.  Twice a second is far below what a person
 * notices as lag and far above what costs anything.
 */
/*
 * The core says a port changed state.  Redraw the list.
 *
 * This replaces a half-second poll.  xa_ui.h has carried an interfaces_changed
 * callback all along -- the core announces from about twenty places in
 * interface.c whenever a port opens, closes or errors -- and this front end
 * never registered for it, so the window asked twice a second instead of being
 * told.  Polling meant rebuilding rows nobody had changed, under the pointer,
 * which is why Start and Stop needed repeated clicks.
 *
 * Told instead of asking, the list redraws exactly when a port changes and at
 * no other time.
 */
void xa_gtk4_interfaces_changed(void)
{
  if (iface_win != NULL)
  {
    // Deferred: the core announces this from inside its own locks, and
    // rebuilding here would run GTK while interface.c holds port_data_lock.
    g_idle_add(rebuild_soon, NULL);
  }
}


static void on_win_destroy(GtkWidget *w, gpointer unused)
{
  (void)w;
  (void)unused;
  iface_win = NULL;
  iface_list = NULL;
}


void xa_gtk4_interfaces_show_add(GtkWindow *parent)
{
  show_dialog(parent, -1);
}


void xa_gtk4_interfaces_show_edit(GtkWindow *parent, int port)
{
  if (port >= 0 && port < MAX_IFACE_DEVICES && slot_in_use(port))
  {
    show_dialog(parent, port);
  }
}


void xa_gtk4_interfaces_show(GtkWindow *parent)
{
  GtkWidget *box, *scroll, *header, *add, *note;

  if (iface_win != NULL)
  {
    gtk_window_present(GTK_WINDOW(iface_win));
    return;
  }

  iface_win = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(iface_win), "Interfaces");
  gtk_window_set_transient_for(GTK_WINDOW(iface_win), parent);
  // Wide enough that the three per-row controls and a readable label coexist
  // without the text having to be cut short at the usual case.
  gtk_window_set_default_size(GTK_WINDOW(iface_win), 760, 420);

  header = gtk_header_bar_new();
  add = gtk_button_new_from_icon_name("list-add-symbolic");
  gtk_widget_set_tooltip_text(add, "Add an interface");
  g_signal_connect(add, "clicked", G_CALLBACK(on_add), iface_win);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(header), add);
  gtk_window_set_titlebar(GTK_WINDOW(iface_win), header);

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(iface_win), box);

  scroll = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroll, TRUE);
  /*
   * Never scroll sideways.  A list of interfaces has a fixed set of controls on
   * the right of every row, and a horizontal scroll is the one thing guaranteed
   * to hide them -- you cannot press Delete if reaching it means noticing a
   * scrollbar that only appears because the text is long.  With the labels
   * ellipsized there is nothing legitimate to scroll to.
   */
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  iface_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(iface_list),
                                  GTK_SELECTION_NONE);
  gtk_widget_add_css_class(iface_list, "boxed-list");
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), iface_list);
  gtk_box_append(GTK_BOX(box), scroll);

  // Say plainly whether anything can transmit, because the per-interface
  // checkbox is not the only thing that decides it.
  note = gtk_label_new(transmit_disable
                       ? "Transmit is disabled globally. Nothing will go on "
                         "the air."
                       : "Transmit follows each interface's own setting.");
  gtk_widget_set_margin_top(note, 8);
  gtk_widget_set_margin_bottom(note, 8);
  gtk_widget_add_css_class(note, "dim-label");
  gtk_box_append(GTK_BOX(box), note);

  g_signal_connect(iface_win, "destroy", G_CALLBACK(on_win_destroy), NULL);

  rebuild_rows();
  gtk_window_present(GTK_WINDOW(iface_win));
}
