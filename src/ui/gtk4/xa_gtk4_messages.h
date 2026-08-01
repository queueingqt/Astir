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
 * The core still says which conversations exist and which messages are in one.
 * It no longer says what they look like: the transcript is built here, out of
 * the message store, because a chat layout -- own messages to the right, the
 * heading on its own line above the words -- needs the pieces of a message and
 * the core hands over a line it has already laid out.
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
 * Open a conversation, type `text` into its compose box, and press send.
 *
 * The test hook behind ASTIR_GTK4_SEND_MESSAGE, and the counterpart of
 * ASTIR_GTK4_MYSTATION_SAVE: this is a Wayland session with no input
 * automation, so the one thing that cannot otherwise be checked is what
 * happens when somebody actually presses the button.
 *
 * Deliberately goes through the same call the button does, rather than calling
 * the core directly -- a hook that takes a shortcut past the widget proves the
 * shortcut works and nothing about the button.
 */
void xa_gtk4_messages_compose(const char *call, const char *text);

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
 * Here an index is a conversation slot, which holds a callsign and nothing
 * else -- the messages are read from the store when one is drawn.
 */
int  xa_gtk4_msg_window_is_open(int i);
int  xa_gtk4_msg_window_is_group(int i);
int  xa_gtk4_msg_window_callsign(int i, char *out, int n);
void xa_gtk4_msg_window_raise(int i);
void xa_gtk4_msg_window_close_all(void);

/*
 * The core's msg_window_clear, msg_window_append and msg_window_show are NOT
 * implemented, and are deliberately left unregistered.
 *
 * They exist for a front end that lets update_messages() render a conversation
 * into a text widget one preformatted line at a time.  This one builds the
 * transcript from the message store instead -- own messages to the right, the
 * heading on its own line above the words -- and none of that can be recovered
 * from a line the core has already laid out.  Unregistered, each call is a
 * no-op at the xa_ui boundary, so the core may go on making them.
 */

#endif /* ASTIR_GTK4_MESSAGES_H */
