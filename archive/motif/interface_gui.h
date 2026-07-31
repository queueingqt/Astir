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
 * The interface dialogs.
 *
 * These four were declared in interface.h, which core files include --
 * interface.c is itself a core file.  Naming a Widget there worked only as long
 * as something else had already pulled in Xt, which stopped being true when
 * astir.h gave up <X11/Intrinsic.h>.
 *
 * Only the front end includes this header.
 */

#ifndef ASTIR_INTERFACE_GUI_H
#define ASTIR_INTERFACE_GUI_H

#include <X11/Intrinsic.h>

extern void Configure_interface_destroy_shell(Widget widget, XtPointer clientData, XtPointer callData);
extern void Configure_interface(Widget w, XtPointer clientData, XtPointer callData);
extern void control_interface(Widget w, XtPointer clientData, XtPointer callData);
extern void interface_status(Widget w);

#endif  /* ASTIR_INTERFACE_GUI_H */
