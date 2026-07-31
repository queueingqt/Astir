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
 * cad_objects_gui.c -- the Motif half of the CAD object feature.
 *
 * Split out of cad_objects.c, which was 74% dialog code by volume and needed
 * six symbols from main.o purely to build those dialogs.  What is left there
 * is the data model -- allocate, delete, compute area, save, restore, draw --
 * and it now links without the toolkit.
 *
 * The boundary is one-way by construction: this file calls into the model, the
 * model does not call back here.  The three places it used to (redraw after a
 * load, after an erase, after closing a polygon) go through xa_ui_redraw().
 *
 * Everything below was moved verbatim.  Only the file-scope declarations were
 * rewritten, and only to say which half owns each one.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif  // HAVE_CONFIG_H

#include "core/util/snprintf.h"

#include "core/astir.h"
#include "ui/motif/astir_gui.h"
#include "ui/motif/wx_gui.h"
#include "ui/motif/main_gui.h"
#include "ui/motif/draw_symbols_gui.h"
#include "ui/motif/cad_objects_gui.h"
#include "core/globals.h"
#include "core/main.h"
#include "core/state/xa_config.h"
#include "core/aprs/db_funcs.h"
#include "core/render/draw_symbols.h"
#include "core/map/maps.h"  // for draw_vector prototype
#include "core/aprs/cad_objects.h"

#include <Xm/XmAll.h>
#include <X11/cursorfont.h>

#include "draw/xa_draw.h"

#include "core/xa_ui.h"

// Must be last include file
#include "core/util/leak_detection.h"

// lesstif (at least as of version 0.94 in 2008), doesn't
// have full implementation of combo boxes.
#ifndef USE_COMBO_BOX
  #if (XmVERSION >= 2 && !defined(LESSTIF_VERSION))
    #define USE_COMBO_BOX 1
  #endif
#endif  // USE_COMBO_BOX

extern XmFontList fontlist1;    // Menu/System fontlist
extern void pos_dialog(Widget w);

// The dialogs themselves.  All of these were file-scope in cad_objects.c and
// referenced nowhere outside it, except cad_line_style_data, which
// interface_gui.c fills in.
Widget draw_CAD_objects_dialog = (Widget)NULL;
Widget cad_dialog = (Widget)NULL;
Widget cad_label_data,
       cad_comment_data,
       cad_probability_data,
       cad_line_style_data;
// Values entered in the cad_dialog
Widget cad_erase_dialog;
Widget list_of_existing_CAD_objects = (Widget)NULL;
Widget cad_list_dialog = (Widget)NULL;
Widget list_of_existing_CAD_objects_edit = (Widget)NULL;

#ifndef USE_COMBO_BOX
  int clsd_value;  // replacement value for cad line type combo box
#endif // !USE_COMBO_BOX

// The busy cursor for CAD drawing mode.
static Cursor cs_CAD = (Cursor)NULL;

// Forward declarations for the dialogs that reference each other.
void Draw_CAD_Objects_erase( Widget w, XtPointer clientData, XtPointer callData);
void Update_CAD_objects_list_dialog(void);
void Draw_CAD_Objects_erase_dialog( Widget w, XtPointer clientData, XtPointer callData );
void Draw_CAD_Objects_list_dialog( Widget w, XtPointer clientData, XtPointer callData );
void Draw_CAD_Objects_erase_dialog_close(Widget w, XtPointer clientData, XtPointer callData);
void Draw_CAD_Objects_list_dialog_close(Widget w, XtPointer clientData, XtPointer callData);

// Provided by cad_objects.c -- the model half.  Declared here rather than in
// cad_objects.h because nothing outside these two files calls them.
extern void CAD_object_set_raw_probability(CADRow *object_ptr, float probability, int as_percent);
extern float CAD_object_get_raw_probability(CADRow *object_ptr, int as_percent);
extern double CAD_object_compute_area(CADRow *CAD_list_head);
extern void CAD_object_delete(CADRow *object);
extern void CAD_object_delete_all(void);
extern void CAD_object_set_label_at_centroid(CADRow *CAD_object);
extern void Save_CAD_Objects_to_file(void);
extern void Format_area_for_output(double *area_km2, char *area_description, int sizeof_area_description);







// This is the callback for the CAD objects parameters dialog.  It
// takes the values entered in the dialog and stores them in the
// most recently created object.
//
void Set_CAD_object_parameters (Widget widget,
                                XtPointer clientData,
                                XtPointer calldata)
{

  float probability = 0.0;
  CADRow *target_object = NULL;
  int cb_selected;
  // need to find out object to edit from clientData rather than
  // using the first object in list as the one to edit.
  //target_object = CAD_list_head;
  target_object = (CADRow *)clientData;

  // set label, comment, and probability for area
  astir_snprintf(target_object->label,
                  sizeof(target_object->label),
                  "%s", XmTextGetString(cad_label_data)
                 );
  astir_snprintf(target_object->comment,
                  sizeof(target_object->comment),
                  "%s", XmTextGetString(cad_comment_data)
                 );
  // Is more error checking needed?  atof appears to correctly handle
  // empty input, reasonable probability values, and text (0.00).
  // User side probabilities are expressed as percent.
  probability = atof(XmTextGetString(cad_probability_data));
  CAD_object_set_raw_probability(target_object, probability, TRUE);

  // Use the selected line type, default is dashed
  cb_selected = FALSE;

#ifdef USE_COMBO_BOX
  XtVaGetValues(cad_line_style_data,
                XmNselectedPosition,
                &cb_selected,
                NULL);
#else
  cb_selected = clsd_value;
#endif // USE_COMBO_BOX        

  if (cb_selected)
  {
    target_object->line_type = cb_selected;
  }
  else
  {
    target_object->line_type = 2; // LineOnOffDash
  }

  if (cad_list_dialog)
  {
    Update_CAD_objects_list_dialog();
  }

  // close object_parameters dialog
  XtPopdown(cad_dialog);
  XtDestroyWidget(cad_dialog);
  cad_dialog = (Widget)NULL;

  Save_CAD_Objects_to_file();
  // Reload symbols/tracks/CAD objects so that object name will show on map.
  redraw_symbols(da);

  // Here we update the erase cad objects dialog if it is up on
  // the screen.  We get rid of it and re-establish it, which will
  // usually make the dialog move, but this is better than having
  // it be out-of-date.
  //
  if (cad_erase_dialog != NULL)
  {
    Draw_CAD_Objects_erase_dialog_close(widget,clientData,calldata);
    Draw_CAD_Objects_erase_dialog(widget,clientData,calldata);
  }

  // Here we update the edit cad objects dialog by getting rid of
  // it and then re-establishing it if it is active when we start.
  // This will usually make the dialog move, but it's better than
  // having it be out-of-date.
  //
  if (cad_list_dialog!=NULL)
  {
    // Update the Edit CAD Objects list
    Draw_CAD_Objects_list_dialog_close(widget, clientData, calldata);
    Draw_CAD_Objects_list_dialog(widget, clientData, calldata);
  }
}





// Update the list of existing CAD objects on the cad list dialog to
// reflect the current list of objects.
//
void Update_CAD_objects_list_dialog(void)
{
  CADRow *object_ptr = CAD_list_head;
  int counter = 1;
  XmString cb_item;

  if (list_of_existing_CAD_objects_edit!=NULL && cad_list_dialog)
  {
    XmListDeleteAllItems(list_of_existing_CAD_objects_edit);

    // iterate through list of objects to populate scrolled list
    while (object_ptr != NULL)
    {

      //  If no label, use the string "<Empty Label>" instead
      if (object_ptr->label[0] == '\0')
      {
        cb_item = XmStringCreateLtoR("<Empty Label>", XmFONTLIST_DEFAULT_TAG);
      }
      else
      {
        cb_item = XmStringCreateLtoR(object_ptr->label, XmFONTLIST_DEFAULT_TAG);
      }

      XmListAddItem(list_of_existing_CAD_objects_edit,
                    cb_item,
                    counter);
      counter++;
      XmStringFree(cb_item);
      object_ptr = object_ptr->next;
    }
  }
}





void close_object_params_dialog(Widget widget, XtPointer clientData, XtPointer calldata)
{
  XtPopdown(cad_dialog);
  XtDestroyWidget(cad_dialog);
  cad_dialog = (Widget)NULL;

  // Here we update the erase cad objects dialog if it is up on
  // the screen.  We get rid of it and re-establish it, which will
  // usually make the dialog move, but this is better than having
  // it be out-of-date.
  //
  if (cad_erase_dialog != NULL)
  {
    Draw_CAD_Objects_erase_dialog_close(widget,clientData,calldata);
    Draw_CAD_Objects_erase_dialog(widget,clientData,calldata);
  }

  // Here we update the edit cad objects dialog by getting rid of
  // it and then re-establishing it if it is active when we start.
  // This will usually make the dialog move, but it's better than
  // having it be out-of-date.
  //
  if (cad_list_dialog!=NULL)
  {
    // Update the Edit CAD Objects list
    Draw_CAD_Objects_list_dialog_close(widget, clientData, calldata);
    Draw_CAD_Objects_list_dialog(widget, clientData, calldata);
  }
}


#ifndef USE_COMBO_BOX
void clsd_menuCallback(Widget widget, XtPointer ptr, XtPointer callData)
{
  //XmPushButtonCallbackStruct *data = (XmPushButtonCallbackStruct *)callData;
  XtPointer userData;

  XtVaGetValues(widget, XmNuserData, &userData, NULL);

  //clsd_menu is zero based, cad_line_style_data constants are one based.
  clsd_value = (int)userData + 1;
  if (debug_level & 1)
  {
    fprintf(stderr,"Selected value on cad line type pulldown: %d\n",clsd_value);
  }
}
#endif  // !USE_COMBO_BOX



// Create a dialog to obtain information about a newly created CAD
// object from the user.  Values of probability, name, and comment
// are initially blank.  Takes as a parameter a string describing
// the area of the object.  There is a single button with a callback
// to Set_CAD_object_parameters, which stores values from the dialog
// in the object's struct.  Should be generalized to allow editing
// of a pre-existing CAD object (except for the name).  Parameter
// should be a pointer to the object.
//
void Set_CAD_object_parameters_dialog(char *area_description, CADRow *CAD_object)
{
  Widget cad_pane, cad_form,
         cad_label,
         cad_comment,
         cad_probability,
         cad_line_style,
         button_done,
         button_cancel;
  char   probability_string[5];
  int i;  // loop counters
  //XmString cb_item;  // used to create picklist of line styles
  XmString cb_items[3];
#ifndef USE_COMBO_BOX
  Widget clsd_menuPane;
  Widget clsd_button;
  Widget clsd_buttons[3];
  Widget clsd_menu;
  char buf[18];
  int x;
  Arg args[12]; // available for XtSetArguments
#endif // !USE_COMBO_BOX
  Widget clsd_widget;


  if (cad_dialog)
  {
    (void)XRaiseWindow(XtDisplay(cad_dialog), XtWindow(cad_dialog));
  }
  else
  {

    // Area Object"
    cad_dialog = XtVaCreatePopupShell(langcode("CADPUD001"),
                                      xmDialogShellWidgetClass,
                                      appshell,
                                      XmNdeleteResponse,          XmDESTROY,
                                      XmNdefaultPosition,         FALSE,
                                      XmNfontList, fontlist1,
                                      NULL);

    cad_pane = XtVaCreateWidget("Set_Del_Object pane",
                                xmPanedWindowWidgetClass,
                                cad_dialog,
                                MY_FOREGROUND_COLOR,
                                MY_BACKGROUND_COLOR,
                                NULL);

    cad_form =  XtVaCreateWidget("Set_Del_Object ob_form",
                                 xmFormWidgetClass,
                                 cad_pane,
                                 XmNfractionBase,            2,
                                 XmNautoUnmanage,            FALSE,
                                 XmNshadowThickness,         1,
                                 MY_FOREGROUND_COLOR,
                                 MY_BACKGROUND_COLOR,
                                 NULL);
    // Area of polygon, already scaled and internationalized.
    cad_label = XtVaCreateManagedWidget(area_description,
                                        xmLabelWidgetClass,
                                        cad_form,
                                        XmNtopAttachment,           XmATTACH_FORM,
                                        XmNtopOffset,               10,
                                        XmNbottomAttachment,        XmATTACH_NONE,
                                        XmNleftAttachment,          XmATTACH_FORM,
                                        XmNleftOffset,              10,
                                        XmNrightAttachment,         XmATTACH_NONE,
                                        MY_FOREGROUND_COLOR,
                                        MY_BACKGROUND_COLOR,
                                        XmNfontList, fontlist1,
                                        NULL);
    // "Area Label:"
    cad_label = XtVaCreateManagedWidget(langcode("CADPUD002"),
                                        xmLabelWidgetClass,
                                        cad_form,
                                        XmNtopAttachment,           XmATTACH_FORM,
                                        XmNtopOffset,               50,
                                        XmNbottomAttachment,        XmATTACH_NONE,
                                        XmNleftAttachment,          XmATTACH_FORM,
                                        XmNleftOffset,              10,
                                        XmNrightAttachment,         XmATTACH_NONE,
                                        MY_FOREGROUND_COLOR,
                                        MY_BACKGROUND_COLOR,
                                        XmNfontList, fontlist1,
                                        NULL);
    // label text field
    cad_label_data = XtVaCreateManagedWidget("Set_Del_Object name_data",
                     xmTextFieldWidgetClass,
                     cad_form,
                     XmNeditable,                TRUE,
                     XmNcursorPositionVisible,   TRUE,
                     XmNsensitive,               TRUE,
                     XmNshadowThickness,         1,
                     XmNcolumns,                 20,
                     XmNmaxLength,               CAD_LABEL_MAX_SIZE - 1,
                     XmNtopAttachment,           XmATTACH_FORM,
                     XmNtopOffset,               50,
                     XmNbottomAttachment,        XmATTACH_NONE,
                     XmNleftAttachment,          XmATTACH_WIDGET,
                     XmNleftWidget,              cad_label,
                     XmNrightAttachment,         XmATTACH_NONE,
                     XmNbackground,              colors[0x0f],
                     XmNfontList, fontlist1,
                     NULL);
    // "Comment"
    cad_comment = XtVaCreateManagedWidget(langcode("CADPUD003"),
                                          xmLabelWidgetClass,
                                          cad_form,
                                          XmNtopAttachment,           XmATTACH_FORM,
                                          XmNtopOffset,               90,
                                          XmNbottomAttachment,        XmATTACH_NONE,
                                          XmNleftAttachment,          XmATTACH_FORM,
                                          XmNleftOffset,              10,
                                          XmNrightAttachment,         XmATTACH_NONE,
                                          MY_FOREGROUND_COLOR,
                                          MY_BACKGROUND_COLOR,
                                          XmNfontList, fontlist1,
                                          NULL);
    // comment text field
    cad_comment_data = XtVaCreateManagedWidget("Set_Del_Object name_data",
                       xmTextFieldWidgetClass,
                       cad_form,
                       XmNeditable,                TRUE,
                       XmNcursorPositionVisible,   TRUE,
                       XmNsensitive,               TRUE,
                       XmNshadowThickness,         1,
                       XmNcolumns,                 40,
                       XmNmaxLength,               CAD_COMMENT_MAX_SIZE - 1,
                       XmNtopAttachment,           XmATTACH_FORM,
                       XmNtopOffset,               90,
                       XmNbottomAttachment,        XmATTACH_NONE,
                       XmNleftAttachment,          XmATTACH_WIDGET,
                       XmNleftWidget,              cad_comment,
                       XmNrightAttachment,         XmATTACH_NONE,
                       XmNbackground,              colors[0x0f],
                       XmNfontList, fontlist1,
                       NULL);
    // "Probability (as %)"
    cad_probability = XtVaCreateManagedWidget(langcode("CADPUD004"),
                      xmLabelWidgetClass,
                      cad_form,
                      XmNtopAttachment,           XmATTACH_FORM,
                      XmNtopOffset,               130,
                      XmNbottomAttachment,        XmATTACH_NONE,
                      XmNleftAttachment,          XmATTACH_FORM,
                      XmNleftOffset,              10,
                      XmNrightAttachment,         XmATTACH_NONE,
                      MY_FOREGROUND_COLOR,
                      MY_BACKGROUND_COLOR,
                      XmNfontList, fontlist1,
                      NULL);
    // probability field
    cad_probability_data = XtVaCreateManagedWidget("Set_Del_Object name_data",
                           xmTextFieldWidgetClass,
                           cad_form,
                           XmNeditable,                TRUE,
                           XmNcursorPositionVisible,   TRUE,
                           XmNsensitive,               TRUE,
                           XmNshadowThickness,         1,
                           XmNcolumns,                 5,
                           XmNmaxLength,               5,
                           XmNtopAttachment,           XmATTACH_FORM,
                           XmNtopOffset,               130,
                           XmNbottomAttachment,        XmATTACH_NONE,
                           XmNleftAttachment,          XmATTACH_WIDGET,
                           XmNleftWidget,              cad_probability,
                           XmNrightAttachment,         XmATTACH_NONE,
                           XmNbackground,              colors[0x0f],
                           XmNfontList, fontlist1,
                           NULL);
    // Boundary Line Type
    cad_line_style = XtVaCreateManagedWidget("Line Type:",
                     xmLabelWidgetClass,
                     cad_form,
                     XmNtopAttachment,           XmATTACH_WIDGET,
                     XmNtopWidget,               cad_probability_data,
                     XmNtopOffset,               5,
                     XmNbottomAttachment,        XmATTACH_NONE,
                     XmNleftAttachment,          XmATTACH_FORM,
                     XmNleftOffset,              10,
                     XmNrightAttachment,         XmATTACH_NONE,
                     MY_FOREGROUND_COLOR,
                     MY_BACKGROUND_COLOR,
                     XmNfontList, fontlist1,
                     NULL);

    // lesstif as of 0.95 in 2008 doesn't fully support combo boxes
    //
    // Need to replace combo boxes with a pull down menu when lesstif is used.
    // See xpdf's  XPDFViewer.cc/XPDFViewer.h for an example.
    //cb_items = (XmString *) XtMalloc ( sizeof (XmString) * 4 );
    // Solid
    cb_items[0] = XmStringCreateLtoR( langcode("CADPUD012"), XmFONTLIST_DEFAULT_TAG);
    // Dashed
    cb_items[1] = XmStringCreateLtoR( langcode("CADPUD013"), XmFONTLIST_DEFAULT_TAG);
    // Double Dash
    cb_items[2] = XmStringCreateLtoR( langcode("CADPUD014"), XmFONTLIST_DEFAULT_TAG);

    clsd_widget = cad_line_style_data;

#ifdef USE_COMBO_BOX
    // Combo box to pick line style
    cad_line_style_data = XtVaCreateManagedWidget("select line style",
                          xmComboBoxWidgetClass,
                          cad_form,
                          XmNtopAttachment,           XmATTACH_WIDGET,
                          XmNtopWidget,               cad_probability_data,
                          XmNtopOffset,               5,
                          XmNbottomAttachment,        XmATTACH_NONE,
                          XmNleftAttachment,          XmATTACH_WIDGET,
                          XmNleftWidget,              cad_line_style,
                          XmNleftOffset,              10,
                          XmNrightAttachment,         XmATTACH_NONE,
                          XmNnavigationType,          XmTAB_GROUP,
                          XmNcomboBoxType,            XmDROP_DOWN_LIST,
                          XmNpositionMode,            XmONE_BASED,
                          XmNvisibleItemCount,        3,
                          MY_FOREGROUND_COLOR,
                          MY_BACKGROUND_COLOR,
                          XmNfontList, fontlist1,
                          NULL);
    XmComboBoxAddItem(cad_line_style_data,cb_items[0],1,1);
    XmComboBoxAddItem(cad_line_style_data,cb_items[1],2,1);
    XmComboBoxAddItem(cad_line_style_data,cb_items[2],3,1);

    clsd_widget = cad_line_style_data;
#else
    // menu replacement for combo box when using lesstif
    x = 0;
    XtSetArg(args[x], XmNmarginWidth, 0);
    ++x;
    XtSetArg(args[x], XmNmarginHeight, 0);
    ++x;
    XtSetArg(args[x], XmNfontList, fontlist1);
    ++x;
    clsd_menuPane = XmCreatePulldownMenu(cad_form,"sddd_menuPane", args, x);
    //sddd_menu is zero based, constants for database types are one based.
    //sddd_value is set to match constants in callback.
    for (i=0; i<3; i++)
    {
      x = 0;
      XtSetArg(args[x], XmNlabelString, cb_items[i]);
      x++;
      XtSetArg(args[x], XmNuserData, (XtPointer)i);
      x++;
      XtSetArg(args[x], XmNfontList, fontlist1);
      ++x;
      sprintf(buf,"button%d",i);
      clsd_button = XmCreatePushButton(clsd_menuPane, buf, args, x);
      XtManageChild(clsd_button);
      XtAddCallback(clsd_button, XmNactivateCallback, clsd_menuCallback, Set_CAD_object_parameters_dialog);
      clsd_buttons[i] = clsd_button;
    }
    x = 0;
    XtSetArg(args[x], XmNleftAttachment, XmATTACH_WIDGET);
    ++x;
    XtSetArg(args[x], XmNleftWidget, cad_line_style);
    ++x;
    XtSetArg(args[x], XmNtopAttachment, XmATTACH_WIDGET);
    ++x;
    XtSetArg(args[x], XmNtopWidget, cad_probability_data);
    ++x;
    XtSetArg(args[x], XmNmarginWidth, 0);
    ++x;
    XtSetArg(args[x], XmNmarginHeight, 0);
    ++x;
    XtSetArg(args[x], XmNtopOffset, 5);
    ++x;
    XtSetArg(args[x], XmNleftOffset, 10);
    ++x;
    XtSetArg(args[x], XmNsubMenuId, clsd_menuPane);
    ++x;
    XtSetArg(args[x], XmNfontList, fontlist1);
    ++x;
    clsd_menu = XmCreateOptionMenu(cad_form, "sddd_Menu", args, x);
    XtManageChild(clsd_menu);
    clsd_value = 2;   // set a default value (line on off dash)
    clsd_widget = clsd_menu;
#endif  // USE_COMBO_BOX
    // free up space from combo box strings
    for (i=0; i<3; i++)
    {
      XmStringFree(cb_items[i]);
    }


    // "OK"
    button_done = XtVaCreateManagedWidget(langcode("CADPUD005"),
                                          xmPushButtonGadgetClass,
                                          cad_form,
                                          XmNtopAttachment,     XmATTACH_WIDGET,
                                          XmNtopWidget,         clsd_widget,
                                          XmNtopOffset,         5,
                                          XmNbottomAttachment,  XmATTACH_FORM,
                                          XmNbottomOffset,      5,
                                          XmNleftAttachment,    XmATTACH_POSITION,
                                          XmNleftPosition,      0,
                                          XmNrightAttachment,   XmATTACH_POSITION,
                                          XmNrightPosition,     1,
                                          XmNnavigationType,    XmTAB_GROUP,
                                          MY_FOREGROUND_COLOR,
                                          MY_BACKGROUND_COLOR,
                                          XmNfontList, fontlist1,
                                          NULL);

    // "Cancel"
    button_cancel = XtVaCreateManagedWidget(langcode("UNIOP00002"),
                                            xmPushButtonGadgetClass,
                                            cad_form,
                                            XmNtopAttachment,     XmATTACH_WIDGET,
                                            XmNtopWidget,         clsd_widget,
                                            XmNtopOffset,         5,
                                            XmNbottomAttachment,  XmATTACH_FORM,
                                            XmNbottomOffset,      5,
                                            XmNleftAttachment,    XmATTACH_POSITION,
                                            XmNleftPosition,      1,
                                            XmNrightAttachment,   XmATTACH_POSITION,
                                            XmNrightPosition,     2,
                                            XmNnavigationType,    XmTAB_GROUP,
                                            MY_FOREGROUND_COLOR,
                                            MY_BACKGROUND_COLOR,
                                            XmNfontList, fontlist1,
                                            NULL);


    // callback depends on whether this is a new or old object
    //XtAddCallback(button_done, XmNactivateCallback, Set_CAD_object_parameters, Set_CAD_object_parameters_dialog);

    if (CAD_object!=NULL)
    {
      XtAddCallback(button_done, XmNactivateCallback, Set_CAD_object_parameters, (XtPointer *)CAD_object);
    }
    else
    {
      // called to get information for a newly created cad object
      // pass pointer to the head of the list, which contains
      // the most recently created cad object.
      XtAddCallback(button_done, XmNactivateCallback, Set_CAD_object_parameters, CAD_list_head);
    }

    XtAddCallback(button_cancel, XmNactivateCallback, close_object_params_dialog, NULL);

    pos_dialog(cad_dialog);
    XmInternAtom(XtDisplay(cad_dialog),"WM_DELETE_WINDOW", FALSE);

    XtManageChild(cad_form);
    XtManageChild(cad_pane);

    XtPopup(cad_dialog,XtGrabNone);
  } // end if ! caddialog

  if (CAD_object!=NULL)
  {
    XmString tempSelection;

    // given an existing object, fill form with its information
    XmTextFieldSetString(cad_label_data,CAD_object->label);
    XmTextFieldSetString(cad_comment_data,CAD_object->comment);
    astir_snprintf(probability_string,
                    sizeof(probability_string),
                    "%01.2f",
                    CAD_object_get_raw_probability(CAD_object,1));
    XmTextFieldSetString(cad_probability_data,probability_string);

    switch(CAD_object->line_type)
    {

      case 1: // Solid
#ifndef USE_COMBO_BOX
        i = 0;
#endif // !USE_COMBO_BOX
        tempSelection = XmStringCreateLtoR( langcode("CADPUD012"),
                                            XmFONTLIST_DEFAULT_TAG);
        break;

      case 2: // Dashed
#ifndef USE_COMBO_BOX
        i = 1;
#endif // !USE_COMBO_BOX
        tempSelection = XmStringCreateLtoR( langcode("CADPUD013"),
                                            XmFONTLIST_DEFAULT_TAG);
        break;

      case 3: // Double Dash
#ifndef USE_COMBO_BOX
        i = 2;
#endif // !USE_COMBO_BOX
        tempSelection = XmStringCreateLtoR( langcode("CADPUD014"),
                                            XmFONTLIST_DEFAULT_TAG);
        break;

      default:
#ifndef USE_COMBO_BOX
        i = 1;
#endif // !USE_COMBO_BOX
        tempSelection = XmStringCreateLtoR( langcode("CADPUD013"),
                                            XmFONTLIST_DEFAULT_TAG);
        break;
    }
#ifdef USE_COMBO_BOX
    XmComboBoxSelectItem(cad_line_style_data, tempSelection);
#else
    clsd_value = i+1;
    //clsd_menu is zero based, line types are one based.
    //clsd_value matches line types (1-3).
    XtVaSetValues(clsd_menu, XmNmenuHistory, clsd_buttons[i], NULL);
#endif // USE_COMBO_BOX
    XmStringFree(tempSelection);
  }
}

void free_cs_CAD(void)
{
  XFreeCursor(XtDisplay(da), cs_CAD);
  cs_CAD = (Cursor)NULL;
}

// This is the callback for the Draw togglebutton
//
void Draw_CAD_Objects_mode( Widget UNUSED(widget),
                            XtPointer UNUSED(clientData),
                            XtPointer callData)
{

  XmToggleButtonCallbackStruct *state = (XmToggleButtonCallbackStruct *)callData;


  if(state->set)
  {
    draw_CAD_objects_flag = 1;

    // Create the "pencil" cursor so we know what mode we're in.
    //
    if(!cs_CAD)
    {
      cs_CAD=XCreateFontCursor(XtDisplay(da),XC_pencil);
      atexit(free_cs_CAD);
    }

    // enable the close polygon button on an open CAD menu
    if (CAD_close_polygon_menu_item)
    {
      XtSetSensitive(CAD_close_polygon_menu_item,TRUE);
    }

    (void)XDefineCursor(XtDisplay(da),XtWindow(da),cs_CAD);
    (void)XFlush(XtDisplay(da));

    draw_CAD_objects_flag = 1;
    polygon_last_x = -1;    // Invalid position
    polygon_last_y = -1;    // Invalid position
  }
  else
  {
    draw_CAD_objects_flag = 0;
    polygon_last_x = -1;    // Invalid position
    polygon_last_y = -1;    // Invalid position

    Save_CAD_Objects_to_file();

    // Remove the special "pencil" cursor.
    (void)XUndefineCursor(XtDisplay(da),XtWindow(da));
    (void)XFlush(XtDisplay(da));

    // disable the close polygon button on an open CAD menu.
    if (CAD_close_polygon_menu_item)
    {
      XtSetSensitive(CAD_close_polygon_menu_item,FALSE);
    }
  }
}





// popdown and destroy the cad_erase_dialog.
//
void Draw_CAD_Objects_erase_dialog_close ( Widget UNUSED(w),
    XtPointer UNUSED(clientData),
    XtPointer UNUSED(callData) )
{

  if (cad_erase_dialog!=NULL)
  {
    // close cad_erase_dialog
    XtPopdown(cad_erase_dialog);
    XtDestroyWidget(cad_erase_dialog);
    cad_erase_dialog = (Widget)NULL;
  }

}





// Call back for delete selected button on
// Draw_CAD_Objects_erase_dialog.  Iterates through the list of
// selected CAD objects and deletes them.
//
void Draw_CAD_Objects_erase_selected ( Widget w,
                                       XtPointer clientData,
                                       XtPointer callData)
{
  int itemCount;       // number of items in list of CAD objects.
  XmString *listItems; // names of CAD objects on list
  char *cadName;       // the text name of a CAD object
  Position x;          // position on list
  char *selectedName;  // the text name of a selected CAD object
  CADRow *object_ptr = CAD_list_head;  // pointer to the linked list of CAD objects
  int done = 0;        // has a cad object with a name matching the current selection been found

  // For more than a few objects this loop/save/redraw will need to move
  // off to a separate thread.

  XtVaGetValues(list_of_existing_CAD_objects,
                XmNitemCount,&itemCount,
                XmNitems,&listItems,
                NULL);
  // iterate through list and delete each first object with a name matching
  // those that are selected on the list.
  //
  // *** Note: If names are not unique the results may not be what the user expects.
  // The first match to a selection will be deleted, not necessarily the selection.
  //
  for (x=1; x<=itemCount; x++)
  {

    if (done)
    {
      break;
    }

    if (XmListPosSelected(list_of_existing_CAD_objects,x))
    {
      int no_label = 0;

      XmStringGetLtoR(listItems[(x-1)],XmFONTLIST_DEFAULT_TAG,&selectedName);

      // Check for our own definition of no label for the CAD
      // objects, which is "<Empty Label>"
      if (strcmp(selectedName,"<Empty Label>") == 0)
      {
        no_label++;
      }

      object_ptr = CAD_list_head;
      done = 0;

      while (object_ptr != NULL && done == 0)
      {

        cadName = object_ptr->label;

        if (strcmp(cadName,selectedName)==0
            || ( (cadName == NULL || cadName[0] == '\0') && no_label) )
        {
          // delete CAD object matching the selected name
          CAD_object_delete(object_ptr);
          done = 1;
        }
        else
        {
          object_ptr = object_ptr->next;
        }
      }
    }
  }

  Draw_CAD_Objects_erase_dialog_close(w,clientData,callData);

  // Save the altered list to file.
  Save_CAD_Objects_to_file();
  // Reload symbols/tracks/CAD objects
  redraw_symbols(da);

  // Here we update the edit cad objects dialog by getting rid of it and
  // then re-establishing it if it is active when we start.  This will
  // usually make the dialog move, but it's better than having it be
  // out-of-date.
  //
  if (cad_list_dialog!=NULL)
  {
    // Update the Edit CAD Objects list
    Draw_CAD_Objects_list_dialog_close(w, clientData, callData);
    Draw_CAD_Objects_list_dialog(w, clientData, callData);
  }
}





// Callback for delete CAD objects menu option.  Dialog to allow
// users to delete all CAD objects or select individual CAD objects
// to delete.
//
void Draw_CAD_Objects_erase_dialog( Widget UNUSED(w),
                                    XtPointer UNUSED(clientData),
                                    XtPointer UNUSED(callData) )
{

  Widget cad_erase_pane, cad_erase_form, cad_erase_label,
         button_delete_all, button_delete_selected, button_cancel;
  Arg al[100];       /* Arg List */
  unsigned int ac;   /* Arg Count */
  CADRow *object_ptr = CAD_list_head;
  int counter = 1;
  XmString cb_item;

  if (cad_erase_dialog)
  {
    (void)XRaiseWindow(XtDisplay(cad_erase_dialog), XtWindow(cad_erase_dialog));
  }
  else
  {

    // Delete CAD Objects
    cad_erase_dialog = XtVaCreatePopupShell("Delete CAD Objects",
                                            xmDialogShellWidgetClass,
                                            appshell,
                                            XmNdeleteResponse,          XmDESTROY,
                                            XmNdefaultPosition,         FALSE,
                                            XmNfontList, fontlist1,
                                            NULL);

    cad_erase_pane = XtVaCreateWidget("CAD erase Object pane",
                                      xmPanedWindowWidgetClass,
                                      cad_erase_dialog,
                                      MY_FOREGROUND_COLOR,
                                      MY_BACKGROUND_COLOR,
                                      NULL);

    cad_erase_form =  XtVaCreateWidget("Cad erase Object form",
                                       xmFormWidgetClass,
                                       cad_erase_pane,
                                       XmNfractionBase,            3,
                                       XmNautoUnmanage,            FALSE,
                                       XmNshadowThickness,         1,
                                       MY_FOREGROUND_COLOR,
                                       MY_BACKGROUND_COLOR,
                                       NULL);

    // heading: Delete CAD Objects
    cad_erase_label = XtVaCreateManagedWidget(langcode("CADPUD009"),
                      xmLabelWidgetClass,
                      cad_erase_form,
                      XmNtopAttachment,           XmATTACH_FORM,
                      XmNtopOffset,               10,
                      XmNbottomAttachment,        XmATTACH_NONE,
                      XmNleftAttachment,          XmATTACH_FORM,
                      XmNleftOffset,              10,
                      XmNrightAttachment,         XmATTACH_NONE,
                      MY_FOREGROUND_COLOR,
                      MY_BACKGROUND_COLOR,
                      XmNfontList, fontlist1,
                      NULL);

    // *** need to handle the special case of no CAD objects ? ***

    // scrolled pick list to allow selection of current objects
    /*set args for list */
    ac=0;
    XtSetArg(al[ac], XmNvisibleItemCount, 11);
    ac++;
    XtSetArg(al[ac], XmNtraversalOn, TRUE);
    ac++;
    XtSetArg(al[ac], XmNshadowThickness, 3);
    ac++;
    XtSetArg(al[ac], XmNselectionPolicy, XmEXTENDED_SELECT);
    ac++;
    XtSetArg(al[ac], XmNscrollBarPlacement, XmBOTTOM_RIGHT);
    ac++;
    XtSetArg(al[ac], XmNtopAttachment, XmATTACH_WIDGET);
    ac++;
    XtSetArg(al[ac], XmNtopWidget, cad_erase_label);
    ac++;
    XtSetArg(al[ac], XmNtopOffset, 5);
    ac++;
    XtSetArg(al[ac], XmNbottomAttachment, XmATTACH_NONE);
    ac++;
    XtSetArg(al[ac], XmNrightAttachment, XmATTACH_FORM);
    ac++;
    XtSetArg(al[ac], XmNrightOffset, 5);
    ac++;
    XtSetArg(al[ac], XmNleftAttachment, XmATTACH_FORM);
    ac++;
    XtSetArg(al[ac], XmNleftOffset, 5);
    ac++;
    XtSetArg(al[ac], XmNforeground, MY_FG_COLOR);
    ac++;
    //XtSetArg(al[ac], XmNbackground, MY_BG_COLOR); ac++;
    XtSetArg(al[ac], XmNbackground, colors[0x0f]);
    ac++;
    XtSetArg(al[ac], XmNfontList, fontlist1);
    ac++;

    list_of_existing_CAD_objects = XmCreateScrolledList(cad_erase_form,
                                   "CAD objects for deletion scrolled list",
                                   al,
                                   ac);
    // make sure list is empty
    XmListDeleteAllItems(list_of_existing_CAD_objects);

    // iterate through list of objects to populate scrolled list
    while (object_ptr != NULL)
    {

      //  If no label, use the string "<Empty Label>" instead
      if (object_ptr->label[0] == '\0')
      {
        cb_item = XmStringCreateLtoR("<Empty Label>", XmFONTLIST_DEFAULT_TAG);
      }
      else
      {
        cb_item = XmStringCreateLtoR(object_ptr->label, XmFONTLIST_DEFAULT_TAG);
      }


      XmListAddItem(list_of_existing_CAD_objects,
                    cb_item,
                    counter);
      counter++;
      XmStringFree(cb_item);
      object_ptr = object_ptr->next;
    }

    // "Delete All"
    button_delete_all = XtVaCreateManagedWidget(langcode("CADPUD010"),
                        xmPushButtonGadgetClass,
                        cad_erase_form,
                        XmNtopAttachment,     XmATTACH_WIDGET,
                        XmNtopWidget,         list_of_existing_CAD_objects,
                        XmNtopOffset,         5,
                        XmNbottomAttachment,  XmATTACH_FORM,
                        XmNbottomOffset,      5,
                        XmNleftAttachment,    XmATTACH_FORM,
                        XmNleftOffset,        5,
                        XmNnavigationType,    XmTAB_GROUP,
                        MY_FOREGROUND_COLOR,
                        MY_BACKGROUND_COLOR,
                        XmNfontList, fontlist1,
                        NULL);
    XtAddCallback(button_delete_all, XmNactivateCallback, Draw_CAD_Objects_erase, Draw_CAD_Objects_erase_dialog);

    // "Delete Selected"
    button_delete_selected = XtVaCreateManagedWidget(langcode("CADPUD011"),
                             xmPushButtonGadgetClass,
                             cad_erase_form,
                             XmNtopAttachment,     XmATTACH_WIDGET,
                             XmNtopWidget,         list_of_existing_CAD_objects,
                             XmNtopOffset,         5,
                             XmNbottomAttachment,  XmATTACH_FORM,
                             XmNbottomOffset,      5,
                             XmNleftAttachment,    XmATTACH_WIDGET,
                             XmNleftWidget,        button_delete_all,
                             XmNleftOffset,        10,
                             XmNnavigationType,    XmTAB_GROUP,
                             MY_FOREGROUND_COLOR,
                             MY_BACKGROUND_COLOR,
                             XmNfontList, fontlist1,
                             NULL);
    XtAddCallback(button_delete_selected, XmNactivateCallback, Draw_CAD_Objects_erase_selected, Draw_CAD_Objects_erase_dialog);

    // "Cancel"
    button_cancel = XtVaCreateManagedWidget(langcode("CADPUD008"),
                                            xmPushButtonGadgetClass,
                                            cad_erase_form,
                                            XmNtopAttachment,     XmATTACH_WIDGET,
                                            XmNtopWidget,         list_of_existing_CAD_objects,
                                            XmNtopOffset,         5,
                                            XmNbottomAttachment,  XmATTACH_FORM,
                                            XmNbottomOffset,      5,
                                            XmNleftAttachment,    XmATTACH_WIDGET,
                                            XmNleftWidget,        button_delete_selected,
                                            XmNleftOffset,        10,
                                            XmNnavigationType,    XmTAB_GROUP,
                                            MY_FOREGROUND_COLOR,
                                            MY_BACKGROUND_COLOR,
                                            XmNfontList, fontlist1,
                                            NULL);
    XtAddCallback(button_cancel, XmNactivateCallback, Draw_CAD_Objects_erase_dialog_close, Draw_CAD_Objects_erase_dialog);
    pos_dialog(cad_erase_dialog);
    XmInternAtom(XtDisplay(cad_erase_dialog),"WM_DELETE_WINDOW", FALSE);

    XtManageChild(cad_erase_form);
    XtManageChild(list_of_existing_CAD_objects);
    XtManageChild(cad_erase_pane);

    XtPopup(cad_erase_dialog,XtGrabNone);
  }
}





// popdown and destroy the cad_list_dialog
//
void Draw_CAD_Objects_list_dialog_close ( Widget UNUSED(w),
    XtPointer UNUSED(clientData),
    XtPointer UNUSED(callData) )
{

  if (cad_list_dialog!=NULL)
  {
    // close cad_list_dialog
    XtPopdown(cad_list_dialog);
    XtDestroyWidget(cad_list_dialog);
    cad_list_dialog = (Widget)NULL;
  }

}





// Show details for selected CAD object.  Callback for the show/edit
// details button on the Draw_CAD_Objects_list dialog.
//
void Show_selected_CAD_object_details ( Widget UNUSED(w),
                                        XtPointer UNUSED(clientData),
                                        XtPointer UNUSED(callData) )
{

  static int sizeof_area_description = 200;
  int itemCount;       // number of items in list of CAD objects.
  XmString *listItems; // names of CAD objects on list
  char *cadName;       // the text name of a CAD object
  Position x;          // position on list
  char *selectedName;  // the text name of a selected CAD object
  CADRow *object_ptr = CAD_list_head;  // pointer to the linked list of CAD objects
  int done = 0;        // has a cad object with a name matching the current selection been found
  double area;
  char area_description[sizeof_area_description];
  astir_snprintf(area_description, sizeof_area_description, "Area");

  if (cad_list_dialog!=NULL)
  {
    // get the selected object
    XtVaGetValues(list_of_existing_CAD_objects_edit,
                  XmNitemCount,&itemCount,
                  XmNitems,&listItems,
                  NULL);
    // iterate through list and find each object with a name
    // matching one selected on the list.
    //
    // *** Note: If names are not unique the results may not be what the user expects.
    // The first match to a selection will be used, not necessarily the selection.
    //
    for (x=1; x<=itemCount; x++)
    {
      if (XmListPosSelected(list_of_existing_CAD_objects_edit,x))
      {
        int no_label = 0;

        XmStringGetLtoR(listItems[(x-1)],XmFONTLIST_DEFAULT_TAG,&selectedName);

        // Check for our own definition of no label for the CAD
        // objects, which is "<Empty Label>"
        if (strcmp(selectedName,"<Empty Label>") == 0)
        {
          no_label++;
        }

        object_ptr = CAD_list_head;
        done = 0;

        while (object_ptr != NULL && done == 0)
        {

          cadName = object_ptr->label;

          if (strcmp(cadName,selectedName)==0
              || ( (cadName == NULL || cadName[0] == '\0') && no_label) )
          {

            // get the area for the CAD object matching the selected name
            // and format it as a localized string.
            area = object_ptr->computed_area;
            Format_area_for_output(&area, area_description,sizeof_area_description);
            // open the CAD object details dialog for the matching CAD object
            Set_CAD_object_parameters_dialog(area_description,object_ptr);
            done = 1;
          }
          else
          {
            object_ptr = object_ptr->next;
          }
        }
      }
    }

    // leave the list dialog open
  }
}





// Callback for edit CAD objects menu option.  Dialog to allow users
// to select individual CAD objects in order to edit their metadata.
//
void Draw_CAD_Objects_list_dialog( Widget UNUSED(w),
                                   XtPointer UNUSED(clientData),
                                   XtPointer UNUSED(callData) )
{

  Widget cad_list_pane, cad_list_form, cad_list_label,
         button_list_selected, button_close;
  Arg al[100];       /* Arg List */
  unsigned int ac;   /* Arg Count */
  CADRow *object_ptr = CAD_list_head;
  int counter = 1;
  XmString cb_item;

  if (cad_list_dialog)
  {
    (void)XRaiseWindow(XtDisplay(cad_list_dialog), XtWindow(cad_list_dialog));
  }
  else
  {

    // List CAD Objects
    cad_list_dialog = XtVaCreatePopupShell("List CAD Objects",
                                           xmDialogShellWidgetClass,
                                           appshell,
                                           XmNdeleteResponse,          XmDESTROY,
                                           XmNdefaultPosition,         FALSE,
                                           XmNfontList, fontlist1,
                                           NULL);

    cad_list_pane = XtVaCreateWidget("CAD list Object pane",
                                     xmPanedWindowWidgetClass,
                                     cad_list_dialog,
                                     MY_FOREGROUND_COLOR,
                                     MY_BACKGROUND_COLOR,
                                     XmNfontList, fontlist1,
                                     NULL);

    cad_list_form =  XtVaCreateWidget("Cad list Object form",
                                      xmFormWidgetClass,
                                      cad_list_pane,
                                      XmNfractionBase,            3,
                                      XmNautoUnmanage,            FALSE,
                                      XmNshadowThickness,         1,
                                      MY_FOREGROUND_COLOR,
                                      MY_BACKGROUND_COLOR,
                                      XmNfontList, fontlist1,
                                      NULL);

    // heading: CAD Objects
    cad_list_label = XtVaCreateManagedWidget(langcode("CADPUD006"),
                     xmLabelWidgetClass,
                     cad_list_form,
                     XmNtopAttachment,           XmATTACH_FORM,
                     XmNtopOffset,               10,
                     XmNbottomAttachment,        XmATTACH_NONE,
                     XmNleftAttachment,          XmATTACH_FORM,
                     XmNleftOffset,              10,
                     XmNrightAttachment,         XmATTACH_NONE,
                     MY_FOREGROUND_COLOR,
                     MY_BACKGROUND_COLOR,
                     XmNfontList, fontlist1,
                     NULL);

    // *** need to handle the special case of no CAD objects ? ***

    // scrolled pick list to allow selection of current objects
    /*set args for list */
    ac=0;
    XtSetArg(al[ac], XmNvisibleItemCount, 11);
    ac++;
    XtSetArg(al[ac], XmNtraversalOn, TRUE);
    ac++;
    XtSetArg(al[ac], XmNshadowThickness, 3);
    ac++;
    XtSetArg(al[ac], XmNselectionPolicy, XmSINGLE_SELECT);
    ac++;
    XtSetArg(al[ac], XmNscrollBarPlacement, XmBOTTOM_RIGHT);
    ac++;
    XtSetArg(al[ac], XmNtopAttachment, XmATTACH_WIDGET);
    ac++;
    XtSetArg(al[ac], XmNtopWidget, cad_list_label);
    ac++;
    XtSetArg(al[ac], XmNtopOffset, 5);
    ac++;
    XtSetArg(al[ac], XmNbottomAttachment, XmATTACH_NONE);
    ac++;
    XtSetArg(al[ac], XmNrightAttachment, XmATTACH_FORM);
    ac++;
    XtSetArg(al[ac], XmNrightOffset, 5);
    ac++;
    XtSetArg(al[ac], XmNleftAttachment, XmATTACH_FORM);
    ac++;
    XtSetArg(al[ac], XmNleftOffset, 5);
    ac++;
    XtSetArg(al[ac], XmNforeground, MY_FG_COLOR);
    ac++;
    //XtSetArg(al[ac], XmNbackground, MY_BG_COLOR); ac++;
    XtSetArg(al[ac], XmNbackground, colors[0x0f]);
    ac++;
    XtSetArg(al[ac], XmNfontList, fontlist1);
    ac++;

    list_of_existing_CAD_objects_edit = XmCreateScrolledList(cad_list_form,
                                        "CAD objects for deletion scrolled list",
                                        al,
                                        ac);
    // make sure list is empty
    XmListDeleteAllItems(list_of_existing_CAD_objects_edit);

    // iterate through list of objects to populate scrolled list
    while (object_ptr != NULL)
    {

      //  If no label, use the string "<Empty Label>" instead
      if (object_ptr->label[0] == '\0')
      {
        cb_item = XmStringCreateLtoR("<Empty Label>", XmFONTLIST_DEFAULT_TAG);
      }
      else
      {
        cb_item = XmStringCreateLtoR(object_ptr->label, XmFONTLIST_DEFAULT_TAG);
      }

      XmListAddItem(list_of_existing_CAD_objects_edit,
                    cb_item,
                    counter);
      counter++;
      XmStringFree(cb_item);
      object_ptr = object_ptr->next;
    }

    // "Show/edit details"
    button_list_selected = XtVaCreateManagedWidget(langcode("CADPUD007"),
                           xmPushButtonGadgetClass,
                           cad_list_form,
                           XmNtopAttachment,     XmATTACH_WIDGET,
                           XmNtopWidget,         list_of_existing_CAD_objects_edit,
                           XmNtopOffset,         5,
                           XmNbottomAttachment,  XmATTACH_FORM,
                           XmNbottomOffset,      5,
                           XmNleftAttachment,    XmATTACH_FORM,
                           XmNleftOffset,        10,
                           XmNnavigationType,    XmTAB_GROUP,
                           MY_FOREGROUND_COLOR,
                           MY_BACKGROUND_COLOR,
                           XmNfontList, fontlist1,
                           NULL);
    XtAddCallback(button_list_selected, XmNactivateCallback, Show_selected_CAD_object_details, Draw_CAD_Objects_list_dialog);

    // "Close"
    button_close = XtVaCreateManagedWidget(langcode("UNIOP00003"),
                                           xmPushButtonGadgetClass,
                                           cad_list_form,
                                           XmNtopAttachment,     XmATTACH_WIDGET,
                                           XmNtopWidget,         list_of_existing_CAD_objects_edit,
                                           XmNtopOffset,         5,
                                           XmNbottomAttachment,  XmATTACH_FORM,
                                           XmNbottomOffset,      5,
                                           XmNleftAttachment,    XmATTACH_WIDGET,
                                           XmNleftWidget,        button_list_selected,
                                           XmNleftOffset,        10,
                                           XmNnavigationType,    XmTAB_GROUP,
                                           MY_FOREGROUND_COLOR,
                                           MY_BACKGROUND_COLOR,
                                           XmNfontList, fontlist1,
                                           NULL);
    XtAddCallback(button_close, XmNactivateCallback, Draw_CAD_Objects_list_dialog_close, Draw_CAD_Objects_erase_dialog);
    pos_dialog(cad_list_dialog);
    XmInternAtom(XtDisplay(cad_list_dialog),"WM_DELETE_WINDOW", FALSE);

    XtManageChild(cad_list_form);
    XtManageChild(list_of_existing_CAD_objects_edit);
    XtManageChild(cad_list_pane);

    XtPopup(cad_list_dialog,XtGrabNone);
  }
}





// Free the object and vertice lists then do a screen update.
// callback from delete all button on cad_erase_dialog.
//
void Draw_CAD_Objects_erase( Widget w,
                             XtPointer clientData,
                             XtPointer callData)
{

  // if we were called from the cad_erase_dialog, make sure it is closed properly
  if (cad_erase_dialog)
  {
    Draw_CAD_Objects_erase_dialog_close(w,clientData,callData);
  }

  CAD_object_delete_all();
  polygon_last_x = -1;    // Invalid position
  polygon_last_y = -1;    // Invalid position

  // Save the empty list out to file
  Save_CAD_Objects_to_file();

  // Reload symbols/tracks/CAD objects
  xa_ui_redraw();
}



// Add an ending vertice that is the same as the starting vertice.
// Best not to use the screen coordinates we captured first, as the
// user may have zoomed or panned since then.  Better to copy the
// first vertice that we recorded in our linked list.
//
// Compute the area of the closed polygon.  Write it out to STDERR,
// the computed_area field in the Object, and to a dialog that pops
// up on the screen.
//
void Draw_CAD_Objects_close_polygon( Widget UNUSED(widget),
                                     XtPointer UNUSED(clientData),
                                     XtPointer UNUSED(callData) )
{
  static int sizeof_area_description = 200;
  VerticeRow *tmp;
  double area;
  int n;
  //char temp_course[20];
  char area_description[sizeof_area_description];
  astir_snprintf(area_description, sizeof_area_description, "Area");

  // Check whether we're currently working on a polygon.  If not,
  // get out of here.
  if (polygon_last_x == -1 || polygon_last_y == -1)
  {

    // Tell the code that we're starting a new polygon by wiping
    // out the first position.
    polygon_last_x = -1;    // Invalid position
    polygon_last_y = -1;    // Invalid position

    return;
  }

  // Find the last vertice in the linked list.  That will be the
  // first vertice we recorded for the object.

  // Check for at least three vertices.  We don't need to check
  // that the first/last point are equal:  We force it below by
  // copying the first vertice to the last.
  //
  n = 0;
  if (CAD_list_head != NULL)
  {

    // Walk the linked list.  Stop at the last record.
    tmp = CAD_list_head->start;
    if (tmp != NULL)
    {
      n++;
      while (tmp->next != NULL)
      {
        tmp = tmp->next;
        n++;
      }
      if (n > 2)
      {
        // We have more than a point or a line, therefore
        // can copy the first point to the last, closing the
        // polygon.
        CAD_vertice_allocate(tmp->latitude, tmp->longitude);
      }
    }
  }
  // Reload symbols/tracks/CAD objects and redraw the polygon
  xa_ui_redraw();

#ifdef CAD_DEBUG
  fprintf(stderr,"Points in closed polygon: n = %d\n",n);
#endif

  if (n < 3)
  {
    // Not enough points to compute an area.

    // Tell the code that we're starting a new polygon by wiping
    // out the first position.
    polygon_last_x = -1;    // Invalid position
    polygon_last_y = -1;    // Invalid position

    return;
  }
  area =  CAD_object_compute_area(CAD_list_head);

  // Save it in the object.  Convert nautical square miles to
  // square kilometers because that's what "Format_area_for_output"
  // requires.
  area = area * 3.429903999977917; // Now in km squared
//fprintf(stderr,"SQUARE KM: %f\n", area);
  CAD_list_head->computed_area = area;

  Format_area_for_output(&area, area_description, sizeof_area_description);

#ifdef CAD_DEBUG
  // Also write the area to stderr
  fprintf(stderr,"New CAD object %s\n",area_description);
#endif

  // Tell the code that we're starting a new polygon by wiping out
  // the first position.
  polygon_last_x = -1;    // Invalid position
  polygon_last_y = -1;    // Invalid position

  CAD_object_set_label_at_centroid(CAD_list_head);
  // CAD object vertices are ready, needs associated data
  // obtain label, comment, and probability for this polygon
  // from user through a dialog.
  Set_CAD_object_parameters_dialog(area_description,NULL);
}