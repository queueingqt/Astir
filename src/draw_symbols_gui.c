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
 * draw_symbols_gui.c -- the symbol chooser dialog.
 *
 * Split out of draw_symbols.c.  Three functions and about 11k of Motif, against
 * 30 functions of symbol rendering that stay behind and now link without the
 * toolkit.
 *
 * This split waited on the font abstraction.  Before it, draw_symbol() and
 * draw_nice_string() reached Xlib through XQueryFont and XSetClipOrigin, so
 * cutting here would have dragged the whole rendering half across with the
 * dialog -- which is the opposite of the point.  Text goes through xa_draw.h
 * now, and the seam is where it looks like it should be.
 *
 * Moved verbatim.  Only the file-scope declarations changed, and only to say
 * which half owns each one.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif  // HAVE_CONFIG_H

#include "snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <X11/Intrinsic.h>
#include <Xm/XmAll.h>

#ifdef HAVE_X11_XPM_H
  #include <X11/xpm.h>
  #ifdef HAVE_LIBXPM // if we have both, prefer the extra library
    #undef HAVE_XM_XPMI_H
  #endif // HAVE_LIBXPM
#endif // HAVE_X11_XPM_H

#ifdef HAVE_XM_XPMI_H
  #include <Xm/XpmI.h>
#endif // HAVE_XM_XPMI_H

#include "xastir.h"
#include "xastir_gui.h"
#include "wx_gui.h"
#include "main_gui.h"
#include "draw_symbols_gui.h"
#include "cad_objects_gui.h"
#include "globals.h"
#include "main.h"
#include "draw_symbols.h"
#include "maps.h"
#include "util.h"
#include "color.h"
#include "xa_config.h"
#include "xa_settings.h"

#include "xa_draw.h"

#include "xa_ui.h"

// Must be last include file
#include "leak_detection.h"

extern XmFontList fontlist1;    // Menu/System fontlist
extern void pos_dialog(Widget w);
extern void resize_dialog(Widget form, Widget dialog);

// The dialog and its state.  select_symbol_dialog and
// symbol_change_requested_from are read by main.c and objects_gui.c, which are
// the two places that open the chooser; the rest is private to it.
Widget select_symbol_dialog = (Widget)NULL;
static xastir_mutex select_symbol_dialog_lock;
Pixmap select_icons[(126-32)*2];    //33 to 126 with both '/' and '\' symbols (94 * 2) or 188
int symbol_change_requested_from = 0;


// draw_symbols_init() moved here with the lock it initialises -- it did nothing
// else, so it belongs on this side.  main.c calls it by the same name.







void draw_symbols_init(void)
{
  init_critical_section( &select_symbol_dialog_lock );
}





void Select_symbol_destroy_shell( Widget UNUSED(widget), XtPointer clientData, XtPointer UNUSED(callData) )
{
  Widget shell = (Widget) clientData;
  int i;

  XtPopdown(shell);

  // Free all 188 symbol pixmaps
  for ( i = 0; i < (126-32)*2; i++ )
  {
    xa_surface_destroy(select_icons[i]);
  }

  begin_critical_section(&select_symbol_dialog_lock, "draw_symbols.c:Select_symbol_destroy_shell" );

  XtDestroyWidget(shell);
  select_symbol_dialog = (Widget)NULL;

  end_critical_section(&select_symbol_dialog_lock, "draw_symbols.c:Select_symbol_destroy_shell" );

}





void Select_symbol_change_data(Widget widget, XtPointer clientData, XtPointer callData)
{
  char table[2];
  char symbol[2];
  int i = XTPOINTER_TO_INT(clientData);

  //fprintf(stderr,"Selected a symbol: %d\n", clientData);

  if ( i > 0)
  {
    //fprintf(stderr,"Symbol is from primary symbol table: /%c\n",(char)i);
    table[0] = '/';
    symbol[0] = (char)i;
  }
  else
  {
    //fprintf(stderr,"Symbol is from secondary symbol table: \\%c\n",(char)(-i));
    table[0] = '\\';
    symbol[0] = (char)(-i);
  }
  table[1] = '\0';
  symbol[1] = '\0';


  if (symbol_change_requested_from == 1)          // Configure->Station Dialog
  {
    symbol_change_requested_from = 0;
    //fprintf(stderr,"Updating Configure->Station Dialog\n");

    XmTextFieldSetString(station_config_group_data,table);
    XmTextFieldSetString(station_config_symbol_data,symbol);
    updateSymbolPictureCallback(widget,clientData,callData);
  }
  else if (symbol_change_requested_from == 2)     // Create->Object/Item Dialog
  {
    symbol_change_requested_from = 0;
    //fprintf(stderr,"Updating Create->Object/Item Dialog\n");

    XmTextFieldSetString(object_group_data,table);
    XmTextFieldSetString(object_symbol_data,symbol);
    updateObjectPictureCallback(widget,clientData,callData);
  }
  else    // Do nothing.  We shouldn't be here.
  {
    symbol_change_requested_from = 0;
  }

  Select_symbol_destroy_shell(widget,select_symbol_dialog,callData);
}





void Select_symbol( Widget UNUSED(w), XtPointer UNUSED(clientData), XtPointer UNUSED(callData) )
{
  static Widget  pane, my_form, my_form2, my_form3, button_cancel,
         frame, frame2, b1, scrollwindow;
  int i;
  Atom delw;


  if (!select_symbol_dialog)
  {


    begin_critical_section(&select_symbol_dialog_lock, "draw_symbols.c:Select_symbol" );


    select_symbol_dialog = XtVaCreatePopupShell(langcode("SYMSEL0001"),
                           xmDialogShellWidgetClass, appshell,
                           XmNdeleteResponse,XmDESTROY,
                           XmNdefaultPosition, FALSE,
                           XmNfontList, fontlist1,
                           NULL);

    pane = XtVaCreateWidget("Select_symbol pane",
                            xmPanedWindowWidgetClass,
                            select_symbol_dialog,
                            MY_FOREGROUND_COLOR,
                            MY_BACKGROUND_COLOR,
                            NULL);

    scrollwindow = XtVaCreateManagedWidget("Select_symbol scrollwindow",
                                           xmScrolledWindowWidgetClass,
                                           pane,
                                           XmNscrollingPolicy, XmAUTOMATIC,
                                           NULL);

    my_form =  XtVaCreateWidget("Select_symbol my_form",
                                xmFormWidgetClass,
                                scrollwindow,
                                XmNfractionBase, 5,
                                XmNautoUnmanage, FALSE,
                                XmNshadowThickness, 1,
                                MY_FOREGROUND_COLOR,
                                MY_BACKGROUND_COLOR,
                                NULL);

    frame = XtVaCreateManagedWidget("Select_symbol frame",
                                    xmFrameWidgetClass,
                                    my_form,
                                    XmNtopAttachment,XmATTACH_FORM,
                                    XmNtopOffset,10,
                                    XmNbottomAttachment,XmATTACH_NONE,
                                    XmNleftAttachment, XmATTACH_FORM,
                                    XmNleftOffset, 10,
                                    XmNrightAttachment,XmATTACH_NONE,
                                    MY_FOREGROUND_COLOR,
                                    MY_BACKGROUND_COLOR,
                                    NULL);

    // Discard the return value of this function, we're not using it.
    // GCC 6.x *hates* when we assign to variables we never use.
    XtVaCreateManagedWidget(langcode("SYMSEL0002"),
                            xmLabelWidgetClass,
                            frame,
                            XmNchildType, XmFRAME_TITLE_CHILD,
                            MY_FOREGROUND_COLOR,
                            MY_BACKGROUND_COLOR,
                            XmNfontList, fontlist1,
                            NULL);

    frame2 = XtVaCreateManagedWidget("Select_symbol frame",
                                     xmFrameWidgetClass,
                                     my_form,
                                     XmNtopAttachment,XmATTACH_FORM,
                                     XmNtopOffset,10,
                                     XmNbottomAttachment,XmATTACH_NONE,
                                     XmNleftAttachment, XmATTACH_WIDGET,
                                     XmNleftWidget, frame,
                                     XmNleftOffset, 10,
                                     XmNrightAttachment,XmATTACH_FORM,
                                     XmNrightOffset, 10,
                                     MY_FOREGROUND_COLOR,
                                     MY_BACKGROUND_COLOR,
                                     NULL);

    // Discard the return value of this function call, we're not using it.
    // GCC 6.x *hates* when we assign to variables we never use.
    XtVaCreateManagedWidget(langcode("SYMSEL0003"),
                            xmLabelWidgetClass,
                            frame2,
                            XmNchildType, XmFRAME_TITLE_CHILD,
                            MY_FOREGROUND_COLOR,
                            MY_BACKGROUND_COLOR,
                            XmNfontList, fontlist1,
                            NULL);

    my_form2 =  XtVaCreateWidget("Select_symbol my_form2",
                                 xmRowColumnWidgetClass,
                                 frame,
                                 XmNorientation, XmHORIZONTAL,
                                 XmNpacking, XmPACK_COLUMN,
                                 XmNnumColumns, 10,
                                 XmNautoUnmanage, FALSE,
                                 XmNshadowThickness, 1,
                                 MY_FOREGROUND_COLOR,
                                 MY_BACKGROUND_COLOR,
                                 NULL);

    my_form3 =  XtVaCreateWidget("Select_symbol my_form3",
                                 xmRowColumnWidgetClass,
                                 frame2,
                                 XmNorientation, XmHORIZONTAL,
                                 XmNpacking, XmPACK_COLUMN,
                                 XmNnumColumns, 10,
                                 XmNautoUnmanage, FALSE,
                                 XmNshadowThickness, 1,
                                 MY_FOREGROUND_COLOR,
                                 MY_BACKGROUND_COLOR,
                                 NULL);

    // Symbols:  33 to 126, for both '/' and '\' tables (94 * 2)
    // 33 = start of icons in ASCII table, 126 = end

    // Draw the primary symbol set
    for ( i = 33; i < 127; i++ )
    {

      select_icons[i-33] = xa_surface_create(20, 20, XA_DEPTH_CANVAS);

      b1 = XtVaCreateManagedWidget("symbol button",
                                   xmPushButtonWidgetClass,
                                   my_form2,
                                   XmNlabelType,               XmPIXMAP,
                                   XmNlabelPixmap,             select_icons[i-33],
                                   XmNnavigationType, XmTAB_GROUP,
                                   MY_FOREGROUND_COLOR,
                                   MY_BACKGROUND_COLOR,
                                   XmNfontList, fontlist1,
                                   NULL);

      symbol(0,'/',(char)i,' ',select_icons[i-33],0,0,0,' ');  // create icon

      // Here we send back the ascii number of the symbol.  We need to keep it within
      // the range of short int's.
      XtAddCallback(b1,
                    XmNactivateCallback,
                    Select_symbol_change_data,
                    INT_TO_XTPOINTER(i) );
    }

    // Draw the alternate symbol set
    for ( i = 33+94; i < 127+94; i++ )
    {

      select_icons[i-33] = xa_surface_create(20, 20, XA_DEPTH_CANVAS);

      b1 = XtVaCreateManagedWidget("symbol button",
                                   xmPushButtonWidgetClass,
                                   my_form3,
                                   XmNlabelType,               XmPIXMAP,
                                   XmNlabelPixmap,             select_icons[i-33],
                                   XmNnavigationType, XmTAB_GROUP,
                                   MY_FOREGROUND_COLOR,
                                   MY_BACKGROUND_COLOR,
                                   XmNfontList, fontlist1,
                                   NULL);

      symbol(0,'\\',(char)i-94,' ',select_icons[i-33],0,0,0,' ');  // create icon

      // Here we send back the ascii number of the symbol negated.  We need to keep it
      // within the range of short int's.
      XtAddCallback(b1,
                    XmNactivateCallback,
                    Select_symbol_change_data,
                    INT_TO_XTPOINTER(-(i-94)) );
    }

    button_cancel = XtVaCreateManagedWidget(langcode("UNIOP00002"),
                                            xmPushButtonGadgetClass,
                                            my_form,
                                            XmNtopAttachment, XmATTACH_WIDGET,
                                            XmNtopWidget, frame,
                                            XmNtopOffset, 5,
                                            XmNbottomAttachment, XmATTACH_FORM,
                                            XmNbottomOffset, 5,
                                            XmNleftAttachment, XmATTACH_FORM,
                                            XmNleftOffset, 5,
                                            XmNrightAttachment, XmATTACH_FORM,
                                            XmNrightOffset, 5,
                                            XmNnavigationType, XmTAB_GROUP,
                                            MY_FOREGROUND_COLOR,
                                            MY_BACKGROUND_COLOR,
                                            XmNfontList, fontlist1,
                                            NULL);

    XtAddCallback(button_cancel, XmNactivateCallback, Select_symbol_destroy_shell, select_symbol_dialog);

    pos_dialog(select_symbol_dialog);

    delw = XmInternAtom(XtDisplay(select_symbol_dialog),"WM_DELETE_WINDOW", FALSE);
    XmAddWMProtocolCallback(select_symbol_dialog, delw, Select_symbol_destroy_shell, (XtPointer)select_symbol_dialog);
    XtManageChild(my_form3);
    XtManageChild(my_form2);
    XtManageChild(my_form);
    XtManageChild(pane);

    resize_dialog(my_form, select_symbol_dialog);

    XtPopup(select_symbol_dialog,XtGrabNone);

    // Move focus to the Close button.  This appears to highlight the
    // button fine, but we're not able to hit the <Enter> key to
    // have that default function happen.  Note:  We _can_ hit the
    // <SPACE> key, and that activates the option.
//        XmUpdateDisplay(select_symbol_dialog);
    XmProcessTraversal(button_cancel, XmTRAVERSE_CURRENT);


    end_critical_section(&select_symbol_dialog_lock, "draw_symbols.c:Select_symbol" );


  }
  else
  {
    (void)XRaiseWindow(XtDisplay(select_symbol_dialog), XtWindow(select_symbol_dialog));
  }
}