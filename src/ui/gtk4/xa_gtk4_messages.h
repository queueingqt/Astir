/*
 * Messages, as an inbox docked beside the map.
 *
 * Received messages have been decoded and logged since long before this front
 * end existed, and never shown: the Motif build put each conversation in its
 * own transient window, and nothing here implemented the callbacks that drive
 * them, so they simply were not drawn.
 *
 * This is not a port of those windows.  A conversation is not a dialog -- it
 * has history, it arrives while you are looking at something else, and there is
 * usually more than one of them -- so it is a sidebar: a list of correspondents
 * that stays where it is put, one transcript at a time, collapsed out of the
 * way when the map is what matters.
 *
 * The core still drives it.  Every one of the msg_window_* callbacks in
 * xa_ui.h is implemented here, so update_messages() clears and rebuilds a
 * conversation exactly as it always did, and this file decides only what that
 * looks like.  Which conversations exist, what belongs in one and in what
 * order remain the core's answers, not this file's.
 */
#ifndef ASTIR_GTK4_MESSAGES_H
#define ASTIR_GTK4_MESSAGES_H

#include <gtk/gtk.h>

/*
 * Build the sidebar.  Returns the widget to pack beside the map; the caller
 * owns it, as it owns everything else in the window.
 *
 * `parent` is the main window, kept so a conversation can open the station
 * window for whoever is on the other end of it.
 */
GtkWidget *xa_gtk4_messages_pane(GtkWindow *parent);

// Show or hide the sidebar.  The map takes back the space when it is hidden.
void xa_gtk4_messages_set_visible(int visible);
int  xa_gtk4_messages_get_visible(void);

/*
 * Open this callsign's conversation, revealing the sidebar if it is collapsed.
 *
 * For the station window's "Messages" button, and for anything else that has a
 * callsign and wants the traffic to and from it.
 */
void xa_gtk4_messages_show_call(const char *call);

/*
 * How many arrivals have not been looked at.
 *
 * The header-bar toggle shows this, which is the whole reason an unread count
 * is tracked at all: a collapsed sidebar must still be able to say that
 * something came in.  Cheap -- it returns a running total rather than counting.
 */
int xa_gtk4_messages_unread(void);

/*
 * Be told when that total moves, rather than polling it.
 *
 * The count changes on two quite different occasions -- a message arrives, or
 * one is read -- and only one of them is something the window would otherwise
 * hear about.  Whoever owns the header bar registers here and relabels its
 * toggle; nothing else in this file knows a toggle exists.
 */
void xa_gtk4_messages_set_unread_notify(void (*fn)(void));

/*
 * A message was sent or received.  Wired to the xa_ui message_logged callback.
 *
 * Rebuilds the correspondent list -- which is a scan of the message store, not
 * a widget rebuild, and happens once per message rather than on a timer.
 */
void xa_gtk4_messages_logged(char from, const char *call_sign,
                             const char *from_call, const char *message);

/*
 * The core has decided a conversation with `to_call` has started.  '*' prefix
 * for a group.
 *
 * Deliberately does not reveal the sidebar.  The core says a conversation
 * exists; whether that is worth moving the map aside for is this front end's
 * decision, and it is not -- the toggle's unread count says so instead.
 */
void xa_gtk4_messages_open_window(const char *to_call);

/* ---- the core's message-window contract, implemented over the sidebar ------
 *
 * Windows are addressed by index because that is how the core tracks them.
 * Here an index is a conversation slot: it holds a callsign and a transcript,
 * and at most one slot's transcript is on screen at a time.  The rest stay
 * current in the background, which is what makes switching between them free.
 */
int  xa_gtk4_msg_window_is_open(int i);
int  xa_gtk4_msg_window_is_group(int i);
int  xa_gtk4_msg_window_callsign(int i, char *out, int n);
void xa_gtk4_msg_window_raise(int i);
void xa_gtk4_msg_window_close_all(void);
void xa_gtk4_msg_window_clear(int i);
int  xa_gtk4_msg_window_append(int i, long pos, const char *text,
                               long hl_from, long hl_to, int hl_selected);
void xa_gtk4_msg_window_show(int i, long pos);

#endif /* ASTIR_GTK4_MESSAGES_H */
