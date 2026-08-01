/*
 * The message inbox.  See the header for why it is a sidebar and not a set of
 * windows.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <gtk/gtk.h>

#include "core/astir.h"
#include "core/aprs/database.h"
#include "core/aprs/db_funcs.h"
#include "core/aprs/messages.h"
#include "core/io/interface.h"
#include "core/state/xa_settings.h"
#include "core/util/snprintf.h"
#include "core/util/util.h"
#include "core/xa_ui.h"
#include "ui/gtk4/xa_gtk4_messages.h"
#include "ui/gtk4/xa_gtk4_station.h"

/* ---- conversations -------------------------------------------------------- */

/*
 * A conversation slot.
 *
 * One per index in the core's message-window space, because the core addresses
 * windows by index and something has to map an index to a thing.  Every slot
 * that is in use keeps its transcript current whether or not it is the one on
 * screen -- that is the point of giving each its own buffer rather than sharing
 * one and rebuilding it on every switch.  A GtkTextBuffer nothing displays
 * costs its text and nothing else.
 */
typedef struct
{
  int             in_use;
  int             is_group;
  char            call[MAX_CALLSIGN+1];
  GtkTextBuffer  *buf;
  time_t          touched;       /* when this slot was last selected */
} conversation;

static conversation conv[MAX_MESSAGE_WINDOWS];

static GtkWidget *msg_pane;      /* the whole sidebar */
static GtkWidget *msg_stack;     /* list <-> one conversation */
static GtkWidget *msg_list;      /* correspondents */
static GtkWidget *msg_scroll;    /* what holds that list */
static GtkWidget *msg_view;      /* the transcript on screen */
static GtkWidget *msg_title;     /* whose conversation that is */
static GtkWidget *msg_empty;     /* shown instead of the list when it is empty */
static GtkWidget *msg_entry;     /* the reply box under the transcript */
static GtkWidget *msg_newcall;   /* who a new message is to */
static GtkWindow *msg_parent;

// Which slot the transcript view is pointed at, or -1 for none.
static int msg_shown = -1;

// Somebody wants to know when the unread count moves.  See the header.
static void (*unread_notify)(void);

/*
 * Every bounds check in this file, in one place.
 *
 * The core calls in by index from eight different places, each of which used to
 * repeat the same three-clause guard, and a guard repeated eight times is a
 * guard that will eventually be repeated wrong.  NULL means "there is no such
 * conversation", which is exactly what every one of those callers has to do
 * something about anyway.
 */
static conversation *slot(int i)
{
  if (i < 0 || i >= MAX_MESSAGE_WINDOWS || !conv[i].in_use)
  {
    return NULL;
  }
  return &conv[i];
}


/* ---- who has been read ---------------------------------------------------- */

/*
 * When each correspondent's traffic was last read.
 *
 * Kept here rather than in a slot because a conversation is unread precisely
 * when it has never been opened, and a slot only exists once it has been.
 * Keyed by callsign; the value is the sec_now() at which its transcript was
 * last on screen.
 */
static GHashTable *last_read;    /* char* -> time_t* */


static time_t read_mark(const char *call)
{
  time_t *t;

  if (last_read == NULL || call == NULL)
  {
    return 0;
  }
  t = g_hash_table_lookup(last_read, call);
  return (t != NULL) ? *t : (time_t)0;
}


static void set_read_mark(const char *call, time_t when)
{
  time_t *t;

  if (call == NULL || call[0] == '\0')
  {
    return;
  }
  if (last_read == NULL)
  {
    last_read = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  }
  t = g_new(time_t, 1);
  *t = when;
  g_hash_table_replace(last_read, g_strdup(call), t);
}


/*
 * A conversation is every message with that callsign at either end.
 *
 * The core's "group" flag means "render all of it, not just the half involving
 * us", and every conversation here is opened with it set.  That is a decision,
 * not an oversight:
 *
 * A thread and the list row above it have to agree.  The row previews the
 * newest message in the conversation, so if the thread filtered to this
 * station's half of it the row would advertise a line the thread then refused
 * to show -- and the message would be nowhere, because a conversation is filed
 * under the far end and an overheard one has no other place to go.  Filtering
 * loses traffic; not filtering does not.
 *
 * The cost is that a station Astir has really exchanged messages with also
 * shows what it said to everybody else.  For a receive-only program that is
 * close to the point: this is a log of a callsign's message traffic, which is
 * what "history per callsign" means.  Should Astir ever transmit, a real QSO
 * would want its own half separated out, and this is the line to revisit.
 */
#define CONVERSATION_IS_WHOLE_CHANNEL 1


/* ---- reading the message store -------------------------------------------- */

/*
 * Who Astir has exchanged messages with, gathered from the message store.
 *
 * This is a scan of msg_data rather than a list maintained as messages arrive,
 * because the store is what the transcripts are built from and a second copy of
 * it would be a second thing to get wrong.  It runs once per message logged,
 * not on a timer.
 */
typedef struct
{
  char   call[MAX_CALLSIGN+1];
  time_t latest;                 /* sec_heard of the newest message */
  char   last_line[MAX_MESSAGE_LENGTH+1];
  int    unread;                 /* arrivals since it was last read */
} correspondent;

/*
 * mscan_file() takes a bare function pointer with no user data, so the
 * accumulator has to sit here.  Safe because everything in this file runs on
 * the GTK main loop, which is also the thread the core decodes packets on.
 */
static correspondent *scan_list;
static int scan_n;
static int scan_max;
static int unread_total;


static correspondent *scan_find(const char *call)
{
  int i;

  for (i = 0; i < scan_n; i++)
  {
    if (strcasecmp(scan_list[i].call, call) == 0)
    {
      return &scan_list[i];
    }
  }
  if (scan_n == scan_max)
  {
    scan_max = scan_max ? scan_max * 2 : 16;
    scan_list = g_realloc(scan_list, (gsize)scan_max * sizeof(correspondent));
  }
  memset(&scan_list[scan_n], 0, sizeof(correspondent));
  astir_snprintf(scan_list[scan_n].call, sizeof(scan_list[scan_n].call),
                 "%s", call);
  return &scan_list[scan_n++];
}


static void scan_one(Message *m)
{
  const char *other;
  correspondent *c;
  int from_me;

  // Bulletins and NWS traffic are broadcasts, not conversations: they have no
  // second end to file them under.  They belong in a view of their own and are
  // left out of this one rather than being filed under whoever sent them.
  if (m->type != MESSAGE_MESSAGE)
  {
    return;
  }

  from_me = is_my_call(m->from_call_sign, 1);
  other = from_me ? m->call_sign : m->from_call_sign;
  if (other[0] == '\0')
  {
    return;
  }

  c = scan_find(other);
  if (m->sec_heard >= c->latest)
  {
    c->latest = m->sec_heard;
    astir_snprintf(c->last_line, sizeof(c->last_line), "%s", m->message_line);
  }

  // Only arrivals count as unread.  Something this station sent has by
  // definition been seen by whoever sent it.
  if (!from_me && m->sec_heard > read_mark(other))
  {
    c->unread++;
  }
}


static int by_latest(const void *a, const void *b)
{
  const correspondent *ca = a;
  const correspondent *cb = b;

  if (ca->latest > cb->latest) { return -1; }   /* newest conversation first */
  if (ca->latest < cb->latest) { return 1; }
  return strcasecmp(ca->call, cb->call);
}


/*
 * Re-read the store into scan_list, newest conversation first, and total up
 * what has not been read.
 *
 * Separate from redrawing the list because the two are wanted at different
 * moments: opening a conversation changes the unread count without changing a
 * single row, and it happens on the page where no rows are visible.
 */
static void rescan(void)
{
  int i;

  scan_n = 0;
  mscan_file('\0', scan_one);
  qsort(scan_list, (size_t)scan_n, sizeof(correspondent), by_latest);

  unread_total = 0;
  for (i = 0; i < scan_n; i++)
  {
    unread_total += scan_list[i].unread;
  }

  if (unread_notify != NULL)
  {
    unread_notify();
  }
}


int xa_gtk4_messages_unread(void)
{
  return unread_total;
}


void xa_gtk4_messages_set_unread_notify(void (*fn)(void))
{
  unread_notify = fn;
}


/* ---- slots ---------------------------------------------------------------- */

/*
 * Find or make the slot for this callsign.
 *
 * Never fails for want of a slot: with all of them in use the one gone longest
 * without being looked at is taken, which for a sidebar showing one
 * conversation at a time is a cache, not a limit.
 */
static int slot_for(const char *call)
{
  int i, chosen = -1;
  time_t oldest = 0;

  if (call == NULL || call[0] == '\0')
  {
    return -1;
  }

  for (i = 0; i < MAX_MESSAGE_WINDOWS; i++)
  {
    if (conv[i].in_use && strcasecmp(conv[i].call, call) == 0)
    {
      return i;
    }
  }

  for (i = 0; i < MAX_MESSAGE_WINDOWS; i++)
  {
    if (!conv[i].in_use)
    {
      chosen = i;
      break;
    }
    // Not the one on screen, however long ago it was opened.
    if (i != msg_shown && (chosen < 0 || conv[i].touched < oldest))
    {
      chosen = i;
      oldest = conv[i].touched;
    }
  }
  if (chosen < 0)
  {
    return -1;
  }

  if (conv[chosen].buf != NULL)
  {
    g_object_unref(conv[chosen].buf);
  }

  conv[chosen].in_use = 1;
  conv[chosen].is_group = CONVERSATION_IS_WHOLE_CHANNEL;
  conv[chosen].touched = sec_now();
  astir_snprintf(conv[chosen].call, sizeof(conv[chosen].call), "%s", call);

  conv[chosen].buf = gtk_text_buffer_new(NULL);
  /*
   * A message of ours that has not been acked yet.
   *
   * The core asks for reverse video, which is what a Motif text widget had to
   * use to make something stand out.  The thing being said is "this has not
   * arrived yet", and dimmed italics say that where reverse video says only
   * "look here".
   */
  gtk_text_buffer_create_tag(conv[chosen].buf, "pending",
                             "style", PANGO_STYLE_ITALIC,
                             "foreground", "#888888",
                             NULL);
  return chosen;
}


/* ---- the correspondent list ----------------------------------------------- */

// "3m", "2h", "4d" -- this is a column, not a sentence.
static void short_age(time_t then, char *out, size_t n)
{
  long secs;

  if (then == 0)
  {
    astir_snprintf(out, n, "%s", "");
    return;
  }
  secs = (long)(sec_now() - then);
  if (secs < 0)     { secs = 0; }
  if (secs < 60)    { astir_snprintf(out, n, "%lds", secs); return; }
  if (secs < 3600)  { astir_snprintf(out, n, "%ldm", secs / 60); return; }
  if (secs < 86400) { astir_snprintf(out, n, "%ldh", secs / 3600); return; }
  astir_snprintf(out, n, "%ldd", secs / 86400);
}


static GtkWidget *make_row(const correspondent *c)
{
  GtkWidget *row = gtk_list_box_row_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *call, *age, *last;
  char buf[64];

  gtk_widget_set_margin_start(box, 10);
  gtk_widget_set_margin_end(box, 10);
  gtk_widget_set_margin_top(box, 6);
  gtk_widget_set_margin_bottom(box, 6);

  call = gtk_label_new(c->call);
  gtk_label_set_xalign(GTK_LABEL(call), 0.0);
  gtk_widget_set_hexpand(call, TRUE);
  gtk_widget_add_css_class(call, "heading");
  gtk_box_append(GTK_BOX(top), call);

  if (c->unread > 0)
  {
    GtkWidget *badge;

    astir_snprintf(buf, sizeof(buf), "%d", c->unread);
    badge = gtk_label_new(buf);
    gtk_widget_add_css_class(badge, "astir-badge");
    gtk_box_append(GTK_BOX(top), badge);
  }

  short_age(c->latest, buf, sizeof(buf));
  age = gtk_label_new(buf);
  gtk_label_set_xalign(GTK_LABEL(age), 1.0);
  gtk_widget_add_css_class(age, "dim-label");
  gtk_box_append(GTK_BOX(top), age);

  gtk_box_append(GTK_BOX(box), top);

  // The newest line, so the list says what the traffic was and not merely that
  // there was some.
  last = gtk_label_new(c->last_line);
  gtk_label_set_xalign(GTK_LABEL(last), 0.0);
  gtk_label_set_ellipsize(GTK_LABEL(last), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode(GTK_LABEL(last), TRUE);
  gtk_widget_add_css_class(last, "dim-label");
  gtk_box_append(GTK_BOX(box), last);

  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
  g_object_set_data_full(G_OBJECT(row), "callsign", g_strdup(c->call), g_free);
  return row;
}


/*
 * Rebuild the correspondent list.
 *
 * This one does rebuild rather than update in place, unlike the station list.
 * The two are not the same problem: stations arrive several times a second and
 * their rows are a live target for the pointer, whereas a message is a rare
 * event and there are a handful of correspondents.  Rebuilding a dozen rows
 * when a message lands costs nothing and needs no row bookkeeping to go wrong.
 */
static void refresh_list(void)
{
  GtkWidget *child;
  int i;

  if (msg_list == NULL)
  {
    return;                      // the sidebar was never built
  }

  rescan();

  while ((child = gtk_widget_get_first_child(msg_list)) != NULL)
  {
    gtk_list_box_remove(GTK_LIST_BOX(msg_list), child);
  }
  for (i = 0; i < scan_n; i++)
  {
    gtk_list_box_append(GTK_LIST_BOX(msg_list), make_row(&scan_list[i]));
  }

  gtk_widget_set_visible(msg_empty, scan_n == 0);
  gtk_widget_set_visible(msg_scroll, scan_n != 0);
}


/* ---- the transcript ------------------------------------------------------- */

static void show_slot(int i)
{
  conversation *c = slot(i);

  if (c == NULL || msg_view == NULL)
  {
    return;
  }

  msg_shown = i;
  c->touched = sec_now();
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(msg_view), c->buf);
  gtk_label_set_text(GTK_LABEL(msg_title), c->call);

  // Opening it is reading it.  Marked from now rather than from the newest
  // message, so anything landing while it is open counts as read too.
  set_read_mark(c->call, sec_now());
  rescan();                      // the unread count moved; no row did

  gtk_stack_set_visible_child_name(GTK_STACK(msg_stack), "thread");

  // Say who the reply is to, on the box it is typed into, so a conversation
  // opened from somewhere else cannot be replied to under the wrong callsign.
  if (msg_entry != NULL)
  {
    char hint[MAX_CALLSIGN + 16];

    astir_snprintf(hint, sizeof(hint), "Message %s", c->call);
    gtk_entry_set_placeholder_text(GTK_ENTRY(msg_entry), hint);
    gtk_widget_grab_focus(msg_entry);
  }

  // Ask the core to fill it.  It rebuilds every open window, this one included,
  // and does not otherwise run for traffic this station is not part of.
  update_messages(1);
}


/* ---- sending ------------------------------------------------------------- */

/*
 * Why this message cannot go out, or NULL if it can.
 *
 * Asked BEFORE anything is queued, because the transmit path has no way to
 * report back.  output_my_data() walks the ports, writes to the ones that are
 * up and willing, and returns void -- so a message sent with no interface, or
 * with transmit disabled, is accepted in silence and simply never leaves.  The
 * operator would be looking at their own words in the transcript with no way
 * to tell they had gone nowhere.
 *
 * Checking first is what replaces that.  Every one of these is a condition the
 * operator can see and fix, so each says which.
 */
static const char *why_not_sendable(const char *to)
{
  int i;

  if (to == NULL || to[0] == '\0')
  {
    return "No callsign to send to.";
  }

  if (my_callsign[0] == '\0' || strcasecmp(my_callsign, "NOCALL") == 0)
  {
    return "This station has no callsign of its own yet.\n\n"
           "Set one in Settings \xe2\x86\x92 My Station.  Astir will not "
           "transmit as NOCALL: an unidentified packet on the air is somebody "
           "else's problem to chase down, and in most countries it is illegal.";
  }

  if (transmit_disable)
  {
    return "Transmit is disabled for this station.\n\n"
           "DISABLE_TRANSMIT is set in the configuration.  Nothing will go out "
           "on any interface until that is cleared.";
  }

  for (i = 0; i < MAX_IFACE_DEVICES; i++)
  {
    if (devices[i].device_type != DEVICE_NONE
        && port_data[i].status == DEVICE_UP
        && devices[i].transmit_data)
    {
      return NULL;                 // something is up and willing to send
    }
  }

  return "No interface is up that can transmit.\n\n"
         "Open Connections \xe2\x86\x92 Interfaces and bring up a device with "
         "transmit enabled.  A receive-only APRS-IS connection -- the one-click "
         "kind, which logs in with passcode -1 -- cannot send, by design.";
}


/*
 * Send one message, now.
 *
 * Nothing is scheduled and nothing polls.  output_message() queues it and
 * stamps it due immediately; kick_outgoing_timer() clears the once-a-second
 * guard inside check_and_transmit_messages(), without which a second message
 * sent in the same second as the first would be silently held back; and then
 * the transmit runs on this button press, in this call.
 *
 * What is NOT done is retrying.  The core can retry with a backoff up to
 * MAX_TRIES, and driving that needs something to act at a future moment, which
 * is the one thing an event cannot do.  So a message goes out once and stands
 * in the transcript as unacked -- dimmed and italic -- until an ack arrives and
 * clears it, which is itself an event.  Sending again is the operator pressing
 * send again, deliberately, at a moment they chose.
 */
static void send_now(const char *to, const char *text)
{
  const char *why;
  char call[MAX_CALLSIGN+1];
  char body[MAX_MESSAGE_OUTPUT_LENGTH+1];

  if (text == NULL || text[0] == '\0')
  {
    return;                        // an empty send is not an error, it is a
  }                                // finger slip

  astir_snprintf(call, sizeof(call), "%s", (to != NULL) ? to : "");
  (void)remove_trailing_spaces(call);
  (void)to_upper(call);

  why = why_not_sendable(call);
  if (why != NULL)
  {
    xa_ui_popup("This message was not sent", why);
    return;
  }

  astir_snprintf(body, sizeof(body), "%s", text);

  output_message(my_callsign, call, body, "");
  kick_outgoing_timer(call);
  check_and_transmit_messages(sec_now());

  // It is in the store now, so both views can be told to re-read it.
  update_messages(1);
  refresh_list();
}


static void on_send(GtkWidget *w, gpointer u)
{
  conversation *c = slot(msg_shown);
  const char *text;

  (void)w;
  (void)u;
  if (c == NULL || msg_entry == NULL)
  {
    return;
  }
  text = gtk_editable_get_text(GTK_EDITABLE(msg_entry));
  if (text == NULL || text[0] == '\0')
  {
    return;
  }

  send_now(c->call, text);

  // Cleared whether or not it went: send_now() has already said why not, and
  // leaving the text to be pressed again would send a second copy of anything
  // that did go out.
  gtk_editable_set_text(GTK_EDITABLE(msg_entry), "");
}


// "New message": ask who to, then open that conversation and compose in it.
// One send path rather than two, so a new message and a reply cannot diverge.
static void on_new_message(GtkButton *b, gpointer u)
{
  (void)b;
  (void)u;
  if (msg_newcall == NULL)
  {
    return;
  }
  gtk_widget_set_visible(msg_newcall, TRUE);
  gtk_widget_grab_focus(msg_newcall);
}


static void on_newcall_activate(GtkEntry *e, gpointer u)
{
  char call[MAX_CALLSIGN+1];
  const char *typed = gtk_editable_get_text(GTK_EDITABLE(e));
  int i;

  (void)u;
  if (typed == NULL || typed[0] == '\0')
  {
    gtk_widget_set_visible(GTK_WIDGET(e), FALSE);
    return;
  }

  astir_snprintf(call, sizeof(call), "%s", typed);
  (void)remove_trailing_spaces(call);
  (void)to_upper(call);

  gtk_editable_set_text(GTK_EDITABLE(e), "");
  gtk_widget_set_visible(GTK_WIDGET(e), FALSE);

  // Opened even though there is not a word of traffic with them yet: the
  // conversation is where the compose box lives, and it is about to have one.
  i = slot_for(call);
  if (i >= 0)
  {
    show_slot(i);
  }
}


static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer u)
{
  const char *call;
  int i;

  (void)box;
  (void)u;
  call = g_object_get_data(G_OBJECT(row), "callsign");
  if (call == NULL)
  {
    return;
  }
  i = slot_for(call);
  if (i >= 0)
  {
    show_slot(i);
  }
}


static void on_back(GtkButton *b, gpointer u)
{
  (void)b;
  (void)u;
  msg_shown = -1;
  gtk_stack_set_visible_child_name(GTK_STACK(msg_stack), "list");
  // Reading the conversation changed the unread counts.  The list is about to
  // be looked at again, so this is the moment it has to say so.
  refresh_list();
}


// Through to the station window for whoever is on the other end.
static void on_open_station(GtkButton *b, gpointer u)
{
  conversation *c = slot(msg_shown);

  (void)b;
  (void)u;
  if (c != NULL && msg_parent != NULL)
  {
    xa_gtk4_station_show(msg_parent, c->call);
  }
}


/* ---- what the core calls -------------------------------------------------- */

void xa_gtk4_messages_logged(char from, const char *call_sign,
                             const char *from_call, const char *message)
{
  // Nothing here reads the message itself: it is already in the store, which is
  // what both the list and the transcripts are built from.  This is a
  // notification that the store changed, and the arguments are the core's way
  // of saying so rather than data this file needs.
  (void)from;
  (void)call_sign;
  (void)from_call;
  (void)message;

  refresh_list();

  /*
   * And the transcript, if one is open.
   *
   * The core refreshes its message windows by itself only when the message was
   * addressed to or sent by this station -- see the is_my_call() guard around
   * update_messages() in decode_message().  A conversation between two other
   * stations is most of what a receive-only program sees and gets no such
   * refresh, so it is asked for here.
   */
  if (slot(msg_shown) != NULL)
  {
    update_messages(1);
  }
}


void xa_gtk4_messages_open_window(const char *to_call)
{
  const char *call = to_call;

  if (call == NULL)
  {
    return;
  }
  if (call[0] == '*')            // the core's mark for a group conversation
  {
    call++;
  }

  // Made, not shown.  The core has decided a conversation exists; the unread
  // count on the toggle is how this front end says so, because taking the map's
  // space for something that just arrived is not the same as being asked for it.
  if (slot_for(call) >= 0)
  {
    refresh_list();
  }
}


int xa_gtk4_msg_window_is_open(int i)
{
  return slot(i) != NULL;
}


int xa_gtk4_msg_window_is_group(int i)
{
  conversation *c = slot(i);

  return (c != NULL) && c->is_group;
}


int xa_gtk4_msg_window_callsign(int i, char *out, int n)
{
  conversation *c = slot(i);

  if (c == NULL || out == NULL)
  {
    return 0;                    // no callsign field at all, which the core
  }                              // distinguishes from an empty one
  astir_snprintf(out, (size_t)n, "%s", c->call);
  return 1;
}


void xa_gtk4_msg_window_raise(int i)
{
  if (slot(i) != NULL)
  {
    xa_gtk4_messages_set_visible(1);
    show_slot(i);
  }
}


void xa_gtk4_msg_window_close_all(void)
{
  int i;

  for (i = 0; i < MAX_MESSAGE_WINDOWS; i++)
  {
    if (conv[i].buf != NULL)
    {
      g_object_unref(conv[i].buf);
    }
    memset(&conv[i], 0, sizeof(conv[i]));
  }
  msg_shown = -1;
  if (msg_stack != NULL)
  {
    gtk_stack_set_visible_child_name(GTK_STACK(msg_stack), "list");
  }
}


void xa_gtk4_msg_window_clear(int i)
{
  conversation *c = slot(i);
  GtkTextIter a, b;

  if (c == NULL)
  {
    return;
  }
  gtk_text_buffer_get_bounds(c->buf, &a, &b);
  gtk_text_buffer_delete(c->buf, &a, &b);
}


/*
 * Append one message line to a conversation.
 *
 * The core passes absolute positions -- it has been counting bytes since the
 * clear -- and this appends at the end instead, which is the same place: the
 * core clears and then appends in order, so the end of the buffer is always
 * `pos`.  Working from the end rather than from the number matters because a
 * GtkTextBuffer counts characters where the core counts bytes, and the two
 * agree only until a message arrives with something above ASCII in it.
 *
 * The highlight range is turned into an offset within THIS line, which is
 * where that difference cannot bite: hl_from - pos and hl_to - pos are byte
 * offsets into `text` itself, and `text` is right here to measure.
 */
int xa_gtk4_msg_window_append(int i, long pos, const char *text,
                              long hl_from, long hl_to, int hl_selected)
{
  conversation *c = slot(i);
  GtkTextIter end;
  long start_offset, lo, hi;
  size_t len;

  if (c == NULL || text == NULL)
  {
    return 0;                    // no transcript: the core must not advance pos
  }

  len = strlen(text);
  gtk_text_buffer_get_end_iter(c->buf, &end);
  start_offset = gtk_text_iter_get_offset(&end);
  gtk_text_buffer_insert(c->buf, &end, text, -1);

  if (!hl_selected)
  {
    return 1;
  }

  lo = hl_from - pos;
  hi = hl_to - pos;
  if (lo < 0)         { lo = 0; }
  if (hi > (long)len) { hi = (long)len; }
  if (lo >= hi)
  {
    return 1;
  }

  /*
   * Byte offsets to character offsets, and only when the line really is UTF-8.
   *
   * It may not be: the core runs utf8_to_latin1_inplace() over a message when
   * traffic_utf8_enabled is set, which leaves bytes that are not valid UTF-8 at
   * all.  GTK inserts those as replacement characters, so the count shifts and
   * the offsets no longer point where the core meant.  Highlighting is dropped
   * in that case rather than applied to the wrong span -- the text is still
   * there and still readable, which is the part that matters.
   */
  if (g_utf8_validate(text, (gssize)len, NULL))
  {
    GtkTextIter a, b;

    lo = g_utf8_pointer_to_offset(text, text + lo);
    hi = g_utf8_pointer_to_offset(text, text + hi);
    gtk_text_buffer_get_iter_at_offset(c->buf, &a, (int)(start_offset + lo));
    gtk_text_buffer_get_iter_at_offset(c->buf, &b, (int)(start_offset + hi));
    gtk_text_buffer_apply_tag_by_name(c->buf, "pending", &a, &b);
  }
  return 1;
}


void xa_gtk4_msg_window_show(int i, long pos)
{
  conversation *c = slot(i);
  GtkTextIter end;

  (void)pos;
  if (c == NULL)
  {
    return;
  }
  // The newest message, which is what `pos` is by the time the core asks: it
  // has just finished appending in time order.
  gtk_text_buffer_get_end_iter(c->buf, &end);
  gtk_text_buffer_place_cursor(c->buf, &end);
  if (i == msg_shown && msg_view != NULL)
  {
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(msg_view), &end,
                                 0.0, FALSE, 0.0, 0.0);
  }
}


/* ---- the sidebar ---------------------------------------------------------- */

void xa_gtk4_messages_set_visible(int visible)
{
  if (msg_pane != NULL)
  {
    gtk_widget_set_visible(msg_pane, visible ? TRUE : FALSE);
  }
}


int xa_gtk4_messages_get_visible(void)
{
  return (msg_pane != NULL) && gtk_widget_get_visible(msg_pane);
}


void xa_gtk4_messages_compose(const char *call, const char *text)
{
  xa_gtk4_messages_show_call(call);
  if (msg_entry == NULL || text == NULL)
  {
    return;
  }
  gtk_editable_set_text(GTK_EDITABLE(msg_entry), text);
  on_send(NULL, NULL);
}


void xa_gtk4_messages_show_call(const char *call)
{
  int i = slot_for(call);

  if (i >= 0)
  {
    xa_gtk4_messages_set_visible(1);
    show_slot(i);
  }
}


// The page listing everyone Astir has traffic with.
static GtkWidget *build_list_page(void)
{
  GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  /* ---- starting one that does not exist yet ---- */
  {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *newbtn = gtk_button_new_from_icon_name("mail-message-new-symbolic");

    gtk_widget_set_margin_start(bar, 6);
    gtk_widget_set_margin_end(bar, 6);
    gtk_widget_set_margin_top(bar, 6);
    gtk_widget_set_margin_bottom(bar, 2);

    gtk_button_set_has_frame(GTK_BUTTON(newbtn), FALSE);
    gtk_widget_set_tooltip_text(newbtn, "Message a station");
    g_signal_connect(newbtn, "clicked", G_CALLBACK(on_new_message), NULL);
    gtk_box_append(GTK_BOX(bar), newbtn);

    // Hidden until asked for.  A callsign field sitting open above the list
    // would be the most prominent thing in a sidebar whose job is reading.
    msg_newcall = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(msg_newcall), "Callsign");
    gtk_entry_set_max_length(GTK_ENTRY(msg_newcall), MAX_CALLSIGN);
    gtk_widget_set_hexpand(msg_newcall, TRUE);
    gtk_widget_set_visible(msg_newcall, FALSE);
    g_signal_connect(msg_newcall, "activate",
                     G_CALLBACK(on_newcall_activate), NULL);
    gtk_box_append(GTK_BOX(bar), msg_newcall);

    gtk_box_append(GTK_BOX(page), bar);
  }

  msg_scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(msg_scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  // A floor under the width, because without one GTK measured the scrollbar
  // inside an allocation of nothing and complained that a slider cannot be
  // -2 wide.  It is also what stops the divider being dragged onto the list.
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(msg_scroll), 240);
  gtk_widget_set_vexpand(msg_scroll, TRUE);

  msg_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(msg_list), GTK_SELECTION_SINGLE);
  g_signal_connect(msg_list, "row-activated",
                   G_CALLBACK(on_row_activated), NULL);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(msg_scroll), msg_list);
  gtk_box_append(GTK_BOX(page), msg_scroll);

  /*
   * Something to say when there is nothing.
   *
   * An empty sidebar is indistinguishable from a broken one, and messages are
   * rare enough that empty is the ordinary state on a quiet band.
   */
  msg_empty = gtk_label_new("No messages yet.\n\n"
                            "Messages addressed to this station, and traffic "
                            "between others that Astir hears, appear here.");
  gtk_label_set_wrap(GTK_LABEL(msg_empty), TRUE);
  gtk_label_set_justify(GTK_LABEL(msg_empty), GTK_JUSTIFY_CENTER);
  gtk_widget_set_valign(msg_empty, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(msg_empty, TRUE);
  gtk_widget_set_margin_start(msg_empty, 18);
  gtk_widget_set_margin_end(msg_empty, 18);
  gtk_widget_add_css_class(msg_empty, "dim-label");
  gtk_box_append(GTK_BOX(page), msg_empty);

  return page;
}


// The page showing one conversation.
static GtkWidget *build_thread_page(void)
{
  GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *scroll, *back, *station;

  gtk_widget_set_margin_start(bar, 6);
  gtk_widget_set_margin_end(bar, 6);
  gtk_widget_set_margin_top(bar, 6);
  gtk_widget_set_margin_bottom(bar, 6);

  back = gtk_button_new_from_icon_name("go-previous-symbolic");
  gtk_button_set_has_frame(GTK_BUTTON(back), FALSE);
  gtk_widget_set_tooltip_text(back, "Back to all conversations");
  g_signal_connect(back, "clicked", G_CALLBACK(on_back), NULL);
  gtk_box_append(GTK_BOX(bar), back);

  msg_title = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(msg_title), 0.0);
  gtk_widget_set_hexpand(msg_title, TRUE);
  gtk_widget_add_css_class(msg_title, "heading");
  gtk_box_append(GTK_BOX(bar), msg_title);

  station = gtk_button_new_from_icon_name("find-location-symbolic");
  gtk_button_set_has_frame(GTK_BUTTON(station), FALSE);
  gtk_widget_set_tooltip_text(station, "Open this station");
  g_signal_connect(station, "clicked", G_CALLBACK(on_open_station), NULL);
  gtk_box_append(GTK_BOX(bar), station);

  gtk_box_append(GTK_BOX(page), bar);

  scroll = gtk_scrolled_window_new();
  // Never sideways.  The transcript wraps, so a horizontal scrollbar would
  // only ever be a way to lose text off the edge.
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 240);
  gtk_widget_set_vexpand(scroll, TRUE);

  /*
   * Monospace, and wrapped.
   *
   * The core formats a transcript line as a fixed-width prefix -- date, time,
   * callsign, retry count -- and then the message, which is a table and reads
   * as one only while the columns line up.  This first kept the columns and
   * scrolled sideways for the rest, and that was wrong: GTK's scrollbars are
   * overlays that stay hidden until the pointer is over them, so there was
   * nothing on screen to say the line continued.  A message simply stopped at
   * the edge of the sidebar, mid-word, and looked like text escaping its box.
   *
   * Text that cannot be seen is worse than a column that does not line up, so
   * it wraps.  The prefix still aligns down the left of every message, because
   * every prefix is the same width; only an over-long message breaks, and the
   * hanging indent is what says the second line is a continuation and not a
   * new message.
   */
  msg_view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(msg_view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(msg_view), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(msg_view), GTK_WRAP_WORD_CHAR);
  // Hanging indent: every line is indented, and the first line of each message
  // is pulled back out to the margin again.
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(msg_view), 8 + 18);
  gtk_text_view_set_indent(GTK_TEXT_VIEW(msg_view), -18);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(msg_view), 8);
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(msg_view), 4);
  gtk_widget_add_css_class(msg_view, "monospace");
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), msg_view);
  gtk_box_append(GTK_BOX(page), scroll);

  /* ---- the compose box ---- */
  {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *send;

    gtk_widget_set_margin_start(row, 6);
    gtk_widget_set_margin_end(row, 6);
    gtk_widget_set_margin_top(row, 4);
    gtk_widget_set_margin_bottom(row, 6);

    msg_entry = gtk_entry_new();
    /*
     * One message, one packet.
     *
     * output_message() will happily split anything longer into as many APRS
     * messages as it takes, each with its own sequence number and its own ack
     * to wait for.  Stopping at the protocol's own limit means what is typed
     * is what goes on the air, and a long paste cannot turn into a burst of
     * six packets that somebody else's channel has to carry.
     */
    gtk_entry_set_max_length(GTK_ENTRY(msg_entry), MAX_MESSAGE_OUTPUT_LENGTH);
    gtk_widget_set_hexpand(msg_entry, TRUE);
    g_signal_connect(msg_entry, "activate", G_CALLBACK(on_send), NULL);
    gtk_box_append(GTK_BOX(row), msg_entry);

    send = gtk_button_new_from_icon_name("document-send-symbolic");
    gtk_widget_set_tooltip_text(send, "Send this message");
    g_signal_connect(send, "clicked", G_CALLBACK(on_send), NULL);
    gtk_box_append(GTK_BOX(row), send);

    gtk_box_append(GTK_BOX(page), row);
  }

  return page;
}


static void install_css(void)
{
  GtkCssProvider *css = gtk_css_provider_new();

  gtk_css_provider_load_from_string(css,
    ".astir-badge {"
    "  background-color: alpha(currentColor, 0.15);"
    "  border-radius: 9px;"
    "  padding: 0px 7px;"
    "  font-size: 0.8em;"
    "  font-weight: bold;"
    "}");
  gtk_style_context_add_provider_for_display(
    gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);
}


GtkWidget *xa_gtk4_messages_pane(GtkWindow *parent)
{
  msg_parent = parent;

  msg_pane = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request(msg_pane, 320, -1);

  msg_stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(msg_stack),
                                GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
  gtk_widget_set_vexpand(msg_stack, TRUE);
  gtk_box_append(GTK_BOX(msg_pane), msg_stack);

  gtk_stack_add_named(GTK_STACK(msg_stack), build_list_page(), "list");
  gtk_stack_add_named(GTK_STACK(msg_stack), build_thread_page(), "thread");
  gtk_stack_set_visible_child_name(GTK_STACK(msg_stack), "list");

  install_css();

  // Whatever was already in the store before the window existed -- a replay, or
  // a session that has been running while the sidebar was closed.
  refresh_list();

  // Collapsed until asked for.  The map is what Astir is for.
  gtk_widget_set_visible(msg_pane, FALSE);
  return msg_pane;
}
