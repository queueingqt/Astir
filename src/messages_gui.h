/*
 *
 * XASTIR, Amateur Station Tracking and Information Reporting
 * Copyright (C) 1999,2000  Frank Giannandrea
 * Copyright (C) 2000-2026 The Xastir Group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Look at the README for more information on the program.
 */

/*
 * The Send Message windows, and everything about them that is made of
 * widgets.
 *
 * All of this used to live in messages.h and xastir.h, which the core includes,
 * so every core file that wanted a message-store declaration got an array of
 * Motif widgets with it.  The core no longer names any of this: it goes through
 * the msg_window_* callbacks in xa_ui.h instead.
 *
 * Only the front end includes this header.  If a core file needs to, something
 * has been put on the wrong side.
 */

#ifndef XASTIR_MESSAGES_GUI_H
#define XASTIR_MESSAGES_GUI_H

// MAX_MESSAGE_WINDOWS stays in messages.h: the core still loops over window
// indices, it just cannot see what a window is made of.
#include "messages.h"
#include "xa_ui.h"          // xa_ui_callbacks, for the registration hook

typedef struct
{
  char win[10];
  char to_call_sign[MAX_CALLSIGN+1];
  int message_group;
  Widget send_message_dialog;
  Widget send_message_call_data;
  Widget D700_mode;
  Widget D7_mode;
  Widget HamHUD_mode;
  Widget message_data_line1;
  Widget message_data_line2;
  Widget message_data_line3;
  Widget message_data_line4;
  Widget send_message_text;
  Widget send_message_path;
  Widget send_message_reverse_path;
  Widget send_message_change_path;
  Widget pane, form, button_ok, button_cancel;
  Widget button_clear_old_msgs, button_submit_call;
  Widget button_clear_pending_msgs;
  Widget button_kick_timer;
  Widget call, message, path, reverse_path_label;
} Message_Window;

// Defined in messages_gui.c.  It was in messages.c -- a core file -- which is
// what kept messages.o from being toolkit-free.
extern Message_Window mw[MAX_MESSAGE_WINDOWS+1];

extern Widget auto_msg_on, auto_msg_off;

extern void Send_message(Widget w, XtPointer clientData, XtPointer callData);
extern void Show_pending_messages(Widget w, XtPointer clientData, XtPointer callData);
extern void Clear_messages(Widget w, XtPointer clientData, XtPointer callData);
extern void Send_message_call(Widget w, XtPointer clientData, XtPointer callData);

// Fill in the msg_window_* half of the callback table.  Called from the front
// end's callback registration, before it hands the table to xa_ui.
void messages_gui_register_ui(xa_ui_callbacks *cb);

#endif  /*  XASTIR_MESSAGES_GUI_H */
