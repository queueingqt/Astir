/*
 * cad_objects_gui.h -- the CAD object dialogs.
 *
 * Moved out of cad_objects.h, which cad_objects.c (a core file) includes.
 */

#ifndef ASTIR_CAD_OBJECTS_GUI_H
#define ASTIR_CAD_OBJECTS_GUI_H

#include <X11/Intrinsic.h>

extern void Draw_CAD_Objects_erase_dialog(Widget w, XtPointer clientData, XtPointer callData);
extern void Draw_CAD_Objects_list_dialog(Widget w, XtPointer clientData, XtPointer callData);
extern void Draw_CAD_Objects_mode( Widget widget, XtPointer clientData, XtPointer callData);
extern void Draw_CAD_Objects_close_polygon(Widget w, XtPointer clientData, XtPointer calldata);
extern void Draw_CAD_Objects_erase(Widget w, XtPointer clientData, XtPointer calldata);

#endif  /* ASTIR_CAD_OBJECTS_GUI_H */
