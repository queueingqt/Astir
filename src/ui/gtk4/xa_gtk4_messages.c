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
 * windows by index and something has to map an index to a thing.
 *
 * A slot holds no transcript.  It used to hold a GtkTextBuffer that the core
 * filled through msg_window_append(), and that could not survive wanting a chat
 * layout: the core hands over one preformatted line -- date, time, callsign and
 * message run together, laid out for a Motif text widget -- and a line already
 * laid out one way cannot be laid out another.  Own messages on the right, with
 * the heading above the words rather than in front of them, needs the pieces
 * separately, so the transcript is built from the message store instead, the
 * same store the correspondent list is built from.
 *
 * The core still decides which conversations exist and which messages are in
 * one.  It no longer decides what they look like.
 */
typedef struct
{
  int             in_use;
  int             is_group;
  char            call[MAX_CALLSIGN+1];
  time_t          touched;       /* when this slot was last selected */
} conversation;

static conversation conv[MAX_MESSAGE_WINDOWS];

static GtkWidget *msg_pane;      /* the whole sidebar */
static GtkWidget *msg_stack;     /* list <-> one conversation */
static GtkWidget *msg_list;      /* correspondents */
static GtkWidget *msg_scroll;    /* what holds that list */
static GtkWidget *msg_thread;    /* the messages of one conversation */
static GtkWidget *msg_thread_sw; /* what scrolls them */
static GtkWidget *msg_title;     /* whose conversation that is */
static GtkWidget *msg_empty;     /* shown instead of the list when it is empty */
static GtkWidget *msg_entry;     /* the reply box under the transcript */
static GtkWidget *msg_count;     /* how much room is left in it */
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

  conv[chosen].in_use = 1;
  conv[chosen].is_group = CONVERSATION_IS_WHOLE_CHANNEL;
  conv[chosen].touched = sec_now();
  astir_snprintf(conv[chosen].call, sizeof(conv[chosen].call), "%s", call);
  return chosen;
}


/* ---- one conversation, read out of the store ------------------------------ */

/*
 * The messages of the conversation being shown.
 *
 * Gathered by the same means as the correspondent list and for the same reason:
 * the store is the one copy of the traffic, and a transcript kept alongside it
 * would be a second copy to get out of step.  Each message is kept in pieces --
 * who, when, what, and how it went -- because that is what a chat layout needs
 * and what the core's rendered line cannot be taken apart into.
 */
typedef struct
{
  char   who[MAX_CALLSIGN+1];    /* whoever sent it */
  char   text[MAX_MESSAGE_LENGTH+1];
  time_t when;
  int    mine;                   /* this station sent it */
  char   acked;
  int    tries;
  time_t interval;
  char   raw[512];               /* the record behind it, for the raw view */
} thread_line;

/*
 * Whether the raw record is shown under each message.
 *
 * Off by default and remembered while the sidebar lives, because it is a
 * debugging view: the reason to want it is a message that did not behave, and
 * that is answered by the sequence number, the ack state and how it was heard
 * -- none of which belong in a conversation being read.
 */
static int msg_raw;

static thread_line *thread_lines;
static int thread_n;
static int thread_max;

// Whose conversation thread_collect() is gathering.  Same reason scan_list is
// a file static: mscan_file() carries no user data.
static char thread_call[MAX_CALLSIGN+1];


static void thread_collect(Message *m)
{
  thread_line *t;

  if (m->type != MESSAGE_MESSAGE)
  {
    return;
  }
  // Every message with this callsign at either end -- the whole channel, as
  // CONVERSATION_IS_WHOLE_CHANNEL says and as the list preview assumes.
  if (strcasecmp(m->from_call_sign, thread_call) != 0
      && strcasecmp(m->call_sign, thread_call) != 0)
  {
    return;
  }

  if (thread_n == thread_max)
  {
    thread_max = thread_max ? thread_max * 2 : 32;
    thread_lines = g_realloc(thread_lines,
                             (gsize)thread_max * sizeof(thread_line));
  }
  t = &thread_lines[thread_n++];
  memset(t, 0, sizeof(*t));
  astir_snprintf(t->who, sizeof(t->who), "%s", m->from_call_sign);
  astir_snprintf(t->text, sizeof(t->text), "%s", m->message_line);
  t->when = m->sec_heard;
  t->mine = is_my_call(m->from_call_sign, 1);
  t->acked = m->acked;
  t->tries = m->tries;
  t->interval = m->interval;

  /*
   * The record as the store holds it, built here while the Message is in hand.
   *
   * Every field is printed as it is rather than interpreted -- `acked` as its
   * number, `type` as its letter -- because the point of a raw view is to see
   * what is actually there.  Anything translated into friendlier words would
   * be the friendly view again, one line further down.
   *
   * The message itself is part of the record and is quoted, so that leading or
   * trailing spaces -- which are invisible in the read view and are exactly
   * the sort of thing a raw view is opened to find -- can be seen.
   */
  /*
   * Labelled, because the two halves are not the same thing said twice.
   *
   * The top is the packet.  Below it is the record Astir's decoder made OF
   * that packet, and `msg=` is its idea of the text -- which agrees with what
   * is visible in the packet whenever nothing is wrong, and stops agreeing
   * exactly when something is: a truncation, a trailing space, a byte that did
   * not survive.  That disagreement is most of the reason to open this view,
   * so the field stays and is labelled instead of being dropped for looking
   * redundant.
   *
   * "rx" and "tx" because they are not the same claim.  An rx line is the
   * bytes that arrived.  A tx line is the frame Astir handed to its own
   * decoder on the way out -- what goes on the air -- and not literally what
   * went down the wire to a TNC, which gets the info field only and supplies
   * the "N0CALL>APZ225,WIDE2-2:" header itself from MYCALL and UNPROTO.
   */
  astir_snprintf(t->raw, sizeof(t->raw),
                 "%s  %s\n\n"
                 "decoded\n"
                 "  from=%s to=%s seq=%s type=%c\n"
                 "  acked=%d tries=%d interval=%lds\n"
                 "  heard=%s via=%c tnc=%c pos=%d sec=%ld\n"
                 "  msg=\"%s\"",
                 (m->data_via == 'L') ? "tx" : "rx",
                 (m->raw_packet[0] != '\0') ? m->raw_packet
                                            : "(no packet recorded)",
                 m->from_call_sign,
                 m->call_sign,
                 (m->seq[0] != '\0') ? m->seq : "-",
                 (m->type != '\0') ? m->type : '?',
                 (int)m->acked,
                 m->tries,
                 (long)m->interval,
                 (m->packet_time[0] != '\0') ? m->packet_time : "-",
                 (m->data_via != '\0') ? m->data_via : '?',
                 // A character, not a count: it holds 'T'/'N', and printing it
                 // as a number showed "tnc=78" for what is plainly an 'N'.
                 (m->heard_via_tnc != '\0') ? m->heard_via_tnc : '?',
                 (int)m->position_known,
                 (long)m->sec_heard,
                 m->message_line);
}


static int by_when(const void *a, const void *b)
{
  const thread_line *ta = a;
  const thread_line *tb = b;

  if (ta->when < tb->when) { return -1; }       /* oldest first, as a chat is */
  if (ta->when > tb->when) { return 1; }
  return 0;
}


/*
 * What became of a message of ours, in as few words as will do.
 *
 * Only for our own: a message somebody else sent has already arrived by
 * definition, and saying so on every one of them would be noise.  Empty means
 * there is nothing worth saying.
 */
static void delivery_state(const thread_line *t, char *out, size_t n)
{
  out[0] = '\0';
  if (!t->mine)
  {
    return;
  }
  switch (t->acked)
  {
    case 0:
      // Sent, no ack yet.  Astir does not retry, so this is where it stays
      // until the ack arrives or the operator sends it again.
      astir_snprintf(out, n, "%s", "sent, no ack");
      break;
    case 2:  astir_snprintf(out, n, "%s", "timed out"); break;
    case 3:  astir_snprintf(out, n, "%s", "cancelled"); break;
    case 4:  astir_snprintf(out, n, "%s", "rejected");  break;
    default: astir_snprintf(out, n, "%s", "delivered"); break;
  }
}


static GtkWidget *make_bubble(const thread_line *t)
{
  GtkWidget *bubble = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
  GtkWidget *info, *body;
  struct tm *lt;
  char heading[128];
  char stamp[16];
  char state[32];

  /*
   * The heading on its own line, above the words.
   *
   * The core put them on one line, in fixed columns, which is a table -- and a
   * table of two-word messages spends most of a narrow sidebar on the same
   * callsign repeated down the left.  Above it, the heading can be small and
   * dim and the message can have the width.
   */
  /*
   * Raw replaces what the message says; it does not move it.
   *
   * The words become the packet and the decoded fields, because that is the
   * point of the view.  The SIDE stays: which end of a conversation a message
   * came from is not decoration, it is the fastest thing to read in a
   * transcript, and a raw view that stacks both ends down one edge is harder
   * to follow than the thing it replaced, not more faithful.  The tint goes
   * with it for the same reason.
   */
  if (msg_raw)
  {
    GtkWidget *raw = gtk_label_new(t->raw);

    gtk_label_set_xalign(GTK_LABEL(raw), 0.0);
    gtk_label_set_wrap(GTK_LABEL(raw), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(raw), PANGO_WRAP_WORD_CHAR);
    // Narrower than the pane on purpose: a block that fills the width is on
    // no side at all, and then there is nothing to read the alignment from.
    gtk_label_set_max_width_chars(GTK_LABEL(raw), 42);
    gtk_label_set_selectable(GTK_LABEL(raw), TRUE);
    gtk_widget_add_css_class(raw, "monospace");
    gtk_widget_add_css_class(raw, "astir-raw");
    gtk_widget_add_css_class(raw, "astir-bubble");
    gtk_widget_add_css_class(raw, t->mine ? "astir-bubble-mine"
                                          : "astir-bubble-theirs");
    gtk_widget_set_halign(raw, t->mine ? GTK_ALIGN_END : GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(bubble), raw);

    gtk_widget_set_halign(bubble, t->mine ? GTK_ALIGN_END : GTK_ALIGN_START);
    gtk_widget_set_margin_start(bubble, 8);
    gtk_widget_set_margin_end(bubble, 8);
    gtk_widget_set_margin_top(bubble, 4);
    gtk_widget_set_margin_bottom(bubble, 4);
    return bubble;
  }

  lt = localtime(&t->when);
  if (lt != NULL)
  {
    strftime(stamp, sizeof(stamp), "%H:%M", lt);
  }
  else
  {
    stamp[0] = '\0';
  }

  delivery_state(t, state, sizeof(state));
  if (state[0] != '\0')
  {
    astir_snprintf(heading, sizeof(heading), "%s \xc2\xb7 %s", stamp, state);
  }
  else
  {
    astir_snprintf(heading, sizeof(heading), "%s \xc2\xb7 %s", stamp, t->who);
  }

  info = gtk_label_new(heading);
  gtk_label_set_xalign(GTK_LABEL(info), t->mine ? 1.0 : 0.0);
  gtk_widget_add_css_class(info, "dim-label");
  gtk_widget_add_css_class(info, "astir-bubble-info");
  gtk_box_append(GTK_BOX(bubble), info);

  body = gtk_label_new(t->text);
  gtk_label_set_xalign(GTK_LABEL(body), 0.0);
  gtk_label_set_wrap(GTK_LABEL(body), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(body), PANGO_WRAP_WORD_CHAR);
  // A bubble that spans the whole sidebar is not a bubble; the side it is on
  // is only legible while there is space left on the other.
  gtk_label_set_max_width_chars(GTK_LABEL(body), 28);
  gtk_label_set_selectable(GTK_LABEL(body), TRUE);
  gtk_widget_add_css_class(body, "astir-bubble");
  gtk_widget_add_css_class(body, t->mine ? "astir-bubble-mine"
                                         : "astir-bubble-theirs");
  gtk_widget_set_halign(body, t->mine ? GTK_ALIGN_END : GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(bubble), body);

  // Ours to the right, theirs to the left, which is the whole convention.
  gtk_widget_set_halign(bubble, t->mine ? GTK_ALIGN_END : GTK_ALIGN_START);
  gtk_widget_set_margin_start(bubble, 8);
  gtk_widget_set_margin_end(bubble, 8);
  gtk_widget_set_margin_top(bubble, 3);
  gtk_widget_set_margin_bottom(bubble, 3);
  return bubble;
}


// Put the newest message in view.  Deferred to an idle: the bubbles have just
// been added and have no height yet, so scrolling now would scroll to where the
// bottom used to be.
static gboolean scroll_to_newest(gpointer unused)
{
  GtkAdjustment *v;

  (void)unused;
  if (msg_thread_sw == NULL)
  {
    return G_SOURCE_REMOVE;
  }
  v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(msg_thread_sw));
  if (v != NULL)
  {
    gtk_adjustment_set_value(v, gtk_adjustment_get_upper(v)
                                - gtk_adjustment_get_page_size(v));
  }
  return G_SOURCE_REMOVE;
}


static void render_thread(void)
{
  GtkWidget *child;
  conversation *c = slot(msg_shown);
  int i;

  if (msg_thread == NULL)
  {
    return;
  }

  while ((child = gtk_widget_get_first_child(msg_thread)) != NULL)
  {
    gtk_box_remove(GTK_BOX(msg_thread), child);
  }
  if (c == NULL)
  {
    return;
  }

  astir_snprintf(thread_call, sizeof(thread_call), "%s", c->call);
  thread_n = 0;
  mscan_file('\0', thread_collect);
  qsort(thread_lines, (size_t)thread_n, sizeof(thread_line), by_when);

  for (i = 0; i < thread_n; i++)
  {
    gtk_box_append(GTK_BOX(msg_thread), make_bubble(&thread_lines[i]));
  }

  g_idle_add(scroll_to_newest, NULL);
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

  if (c == NULL || msg_thread == NULL)
  {
    return;
  }

  msg_shown = i;
  c->touched = sec_now();
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

  render_thread();
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

  /*
   * Refused rather than trimmed.
   *
   * The entry caps at 67 CHARACTERS and this buffer holds 67 BYTES, so a
   * message with anything above ASCII in it can pass the first and not fit the
   * second -- and astir_snprintf() would then cut it at 67 bytes, which lands
   * mid-character and puts a mangled tail on the air.  Better to say so.
   */
  if (strlen(text) > MAX_MESSAGE_OUTPUT_LENGTH)
  {
    char msg[160];

    astir_snprintf(msg, sizeof(msg),
                   "An APRS message carries at most %d bytes and this one is "
                   "%d.\n\nCharacters outside plain ASCII take more than one "
                   "byte each, so it is longer than it looks.",
                   MAX_MESSAGE_OUTPUT_LENGTH, (int)strlen(text));
    xa_ui_popup("This message was not sent", msg);
    return;
  }

  astir_snprintf(body, sizeof(body), "%s", text);

  /*
   * The lock is taken around output_message() because output_message expects
   * to be holding it.
   *
   * Partway through, it releases this lock and retakes it around its call to
   * msg_data_add() -- see the comment there.  Called without the lock held,
   * that release unlocks something never locked (which the mutex layer warns
   * about and ignores) and the retake then leaves it locked for good.  The
   * Motif Send button held it; this one has to as well.
   */
  begin_critical_section(&send_message_dialog_lock,
                         "xa_gtk4_messages.c:send_now");
  output_message(my_callsign, call, body, "");
  end_critical_section(&send_message_dialog_lock,
                       "xa_gtk4_messages.c:send_now");

  kick_outgoing_timer(call);
  check_and_transmit_messages(sec_now());

  // It is in the store now, so both views can be told to re-read it.
  render_thread();
  refresh_list();
}


/*
 * What an APRS message is allowed to contain.
 *
 * APRS101 section 14: the message text is printable ASCII, less '|' and '~',
 * which are reserved for TNC use, and '{', which delimits the message ID that
 * output_message() appends.  A '{' typed into the middle of a message would
 * make the tail of it look like a sequence number to the station receiving it.
 *
 * Anything outside that range is not merely unusual, it is a different
 * protocol: it may not survive the TNC, and on a receiver that follows the
 * spec it is read as something else entirely.  Which answers the question
 * about emoji -- no.  One is three or four bytes of UTF-8, not one of them
 * ASCII, so it would spend a twentieth of the message and arrive as mojibake.
 */
static int aprs_message_char_ok(unsigned char c)
{
  if (c < 0x20 || c > 0x7E)
  {
    return 0;                    // a control code, or not ASCII at all
  }
  return (c != '|' && c != '~' && c != '{');
}


/*
 * Refuse the characters that cannot be sent, as they are typed.
 *
 * At the point of entry rather than at the point of sending, because the two
 * give very different answers to the same mistake: a filter here means the box
 * shows exactly what will go out, and a check at send time means writing a
 * whole message and being told at the end that it cannot go.  It also covers
 * pasting, which is how most of what this rejects would arrive.
 */
static void on_insert_text(GtkEditable *editable, const char *text, int length,
                           int *position, gpointer u)
{
  GString *keep;
  int i, n;

  n = (length < 0) ? (int)strlen(text) : length;
  keep = g_string_new(NULL);
  for (i = 0; i < n; i++)
  {
    if (aprs_message_char_ok((unsigned char)text[i]))
    {
      g_string_append_c(keep, text[i]);
    }
  }

  // Nothing was dropped: let the default handler do the insert as usual.
  if ((int)keep->len != n)
  {
    // Something was.  Insert what survived, and stop the signal so the
    // rejected bytes never reach the entry at all.
    g_signal_handlers_block_by_func(editable, (gpointer)on_insert_text, u);
    if (keep->len > 0)
    {
      gtk_editable_insert_text(editable, keep->str, (int)keep->len, position);
    }
    g_signal_handlers_unblock_by_func(editable, (gpointer)on_insert_text, u);
    g_signal_stop_emission_by_name(editable, "insert-text");
  }
  g_string_free(keep, TRUE);
}


/*
 * Count what is left, in BYTES.
 *
 * Not characters, though the entry's own cap is in characters: APRS counts
 * bytes, output_message() splits on strlen(), and send_now() copies into a
 * 67-byte buffer.  A message of accented or non-Latin characters therefore
 * runs out of room well before it runs out of the 67 characters GTK would
 * allow, and counting characters would promise room that is not there.
 */
static void on_text_changed(GtkEditable *e, gpointer u)
{
  const char *text = gtk_editable_get_text(e);
  int left;
  char buf[16];

  (void)u;
  if (msg_count == NULL)
  {
    return;
  }
  left = MAX_MESSAGE_OUTPUT_LENGTH - (int)strlen((text != NULL) ? text : "");

  astir_snprintf(buf, sizeof(buf), "%d", left);
  gtk_label_set_text(GTK_LABEL(msg_count), buf);

  // Nothing subtle at the boundary: at or past it the next character will not
  // fit, and send_now() will refuse rather than cut a character in half.
  if (left <= 0)
  {
    gtk_widget_remove_css_class(msg_count, "dim-label");
    gtk_widget_add_css_class(msg_count, "astir-count-full");
  }
  else
  {
    gtk_widget_remove_css_class(msg_count, "astir-count-full");
    gtk_widget_add_css_class(msg_count, "dim-label");
  }
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


static void on_toggle_raw(GtkToggleButton *b, gpointer u)
{
  (void)u;
  msg_raw = gtk_toggle_button_get_active(b) ? 1 : 0;
  render_thread();
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

  // And the conversation on screen, which is read from the same store and so
  // is stale for exactly as long as it is not re-read.
  if (slot(msg_shown) != NULL)
  {
    render_thread();
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
    memset(&conv[i], 0, sizeof(conv[i]));
  }
  msg_shown = -1;
  render_thread();               // nothing is open, so it empties
  if (msg_stack != NULL)
  {
    gtk_stack_set_visible_child_name(GTK_STACK(msg_stack), "list");
  }
}


/*
 * The core's clear/append/show are no longer implemented, deliberately.
 *
 * They exist to let update_messages() render a conversation into a text
 * widget, one preformatted line at a time.  The transcript is now built from
 * the store instead -- see the conversation struct for why a chat layout
 * cannot be made out of a line that is already laid out -- so there is nothing
 * for them to render into.  Leaving them unregistered makes each call a no-op
 * at the xa_ui boundary, which is exactly right: the core may keep calling
 * them, and nothing happens.
 *
 * is_open, is_group, callsign, raise and close_all stay.  Those are not about
 * drawing: the core uses them to find a free window for a new conversation and
 * to decide which messages belong in one, and it is still the core that
 * decides both.
 */


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

  // The record behind each message: sequence number, ack state, how it was
  // heard.  A toggle rather than a separate window, so it can be turned on
  // while looking at the message that prompted the question.
  {
    GtkWidget *raw = gtk_toggle_button_new();

    gtk_button_set_icon_name(GTK_BUTTON(raw), "dialog-information-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(raw), FALSE);
    gtk_widget_set_tooltip_text(raw, "Show the raw message record");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(raw), msg_raw != 0);
    g_signal_connect(raw, "toggled", G_CALLBACK(on_toggle_raw), NULL);
    gtk_box_append(GTK_BOX(bar), raw);
  }

  station = gtk_button_new_from_icon_name("find-location-symbolic");
  gtk_button_set_has_frame(GTK_BUTTON(station), FALSE);
  gtk_widget_set_tooltip_text(station, "Open this station");
  g_signal_connect(station, "clicked", G_CALLBACK(on_open_station), NULL);
  gtk_box_append(GTK_BOX(bar), station);

  gtk_box_append(GTK_BOX(page), bar);

  scroll = msg_thread_sw = gtk_scrolled_window_new();
  // Never sideways.  Bubbles wrap, so a horizontal scrollbar could only ever
  // be a way to lose text off the edge.
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 240);
  gtk_widget_set_vexpand(scroll, TRUE);

  // One widget per message rather than one text widget for all of them, which
  // is what lets a message sit on its own side of the sidebar.
  msg_thread = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_margin_top(msg_thread, 4);
  gtk_widget_set_margin_bottom(msg_thread, 4);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), msg_thread);
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
    g_signal_connect(msg_entry, "changed", G_CALLBACK(on_text_changed), NULL);
    // Printable ASCII only, less the three the protocol reserves.  Typed or
    // pasted, anything else never reaches the box.
    g_signal_connect(msg_entry, "insert-text",
                     G_CALLBACK(on_insert_text), NULL);
    gtk_box_append(GTK_BOX(row), msg_entry);

    /*
     * What is left, counted down.
     *
     * Without it the entry simply stops accepting characters at the limit,
     * which reads as a broken keyboard rather than as a full message.  The
     * count says which it is before you get there.
     */
    msg_count = gtk_label_new("");
    gtk_widget_add_css_class(msg_count, "dim-label");
    gtk_label_set_width_chars(GTK_LABEL(msg_count), 3);
    gtk_box_append(GTK_BOX(row), msg_count);

    send = gtk_button_new_from_icon_name("document-send-symbolic");
    gtk_widget_set_tooltip_text(send, "Send this message");
    g_signal_connect(send, "clicked", G_CALLBACK(on_send), NULL);
    gtk_box_append(GTK_BOX(row), send);

    gtk_box_append(GTK_BOX(page), row);

    // So the count reads 67 before a key is pressed, rather than being blank
    // until the first one.
    on_text_changed(GTK_EDITABLE(msg_entry), NULL);
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
    "}"
    /*
     * The bubbles.
     *
     * Both are alpha over whatever is behind them rather than fixed colours,
     * so the sidebar follows a light or dark theme instead of insisting on
     * the one it was written under.
     */
    ".astir-bubble {"
    "  border-radius: 12px;"
    "  padding: 6px 10px;"
    "}"
    ".astir-bubble-mine {"
    "  background-color: alpha(#3584e4, 0.30);"
    "}"
    ".astir-bubble-theirs {"
    "  background-color: alpha(currentColor, 0.10);"
    "}"
    ".astir-bubble-info {"
    "  font-size: 0.78em;"
    "  padding: 0px 4px;"
    "}"
    ".astir-raw {"
    "  font-size: 0.72em;"
    "  padding: 1px 4px;"
    "}"
    ".astir-count-full {"
    "  color: #e01b24;"
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

  // ASTIR_GTK4_MESSAGE_RAW starts with the raw records showing.  Same reason as
  // the other hooks: a toggle in a window nothing can click cannot otherwise be
  // got into its other state to look at.
  msg_raw = (getenv("ASTIR_GTK4_MESSAGE_RAW") != NULL);

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
