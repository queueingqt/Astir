/*
 *
 * ASTIR, Amateur Station Tracking and Information Reporting
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
 * The parts of the old astir.h that are made of widgets.
 *
 * astir.h is included by every file in the tree, and it opened with
 * `#include <X11/Intrinsic.h>` -- so a map driver, the shapefile reader and the
 * APRS parser all got Xt whether they wanted it or not.  That single line was
 * the difference between "no core object calls Motif", which was already true,
 * and "the core can be compiled on a machine with no X headers", which is what
 * a second front end actually needs.
 *
 * What was in there fell into two groups.  The drawing objects -- the pixmaps,
 * the GCs, the colour table -- moved to xa_draw.h in the neutral types, because
 * core drawing code genuinely uses them and only ever passes them to xa_draw
 * calls.  Everything left is this: the toplevel widgets, the Xt application
 * context, and callbacks that take a Widget.  None of it has a core caller.
 *
 * Only the front end includes this header.  If a core file needs to, something
 * has been put on the wrong side.
 */

#ifndef ASTIR_GUI_H
#define ASTIR_GUI_H

#include <X11/Intrinsic.h>

// Macros that help us avoid warnings on 64-bit CPU's.
// Borrowed from the freeciv project (also a GPL project).
#define INT_TO_XTPOINTER(m_i)  ((XtPointer)((long)(m_i)))
#define XTPOINTER_TO_INT(m_p)  ((int)((long)(m_p)))

// black
#define MY_FG_COLOR             colors[0x08]
#define MY_FOREGROUND_COLOR     XmNforeground,colors[0x08]
// gray73
#define MY_BG_COLOR             colors[0xff]
#define MY_BACKGROUND_COLOR     XmNbackground,colors[0xff]

extern Widget appshell;         // the toplevel shell
extern Widget da;               // the drawing area the map is presented into
extern Widget text;             // the status line
extern XtAppContext app_context;

// Where the main window sits.  Read back from the shell and stored in the
// config; nothing outside main.c touches them.
extern Position screen_x_offset;
extern Position screen_y_offset;

extern void resize_dialog( Widget form, Widget dialog);
extern void sort_list(char *filename,int size, Widget list, int *item);
extern void redraw_symbols(Widget w);

/* from location_gui.c */
extern void Last_location(Widget w, XtPointer clientData, XtPointer callData);
extern void Jump_location(Widget w, XtPointer clientData, XtPointer callData);

/* from view_message_gui.c */
extern void view_all_messages(Widget w, XtPointer clientData, XtPointer callData);

#endif  /* ASTIR_GUI_H */
