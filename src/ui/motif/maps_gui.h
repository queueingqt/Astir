/*
 * maps_gui.h -- the map dialogs and the two place-locators.
 *
 * Moved out of maps.h, which every map driver includes.
 */

#ifndef ASTIR_MAPS_GUI_H
#define ASTIR_MAPS_GUI_H

#include <X11/Intrinsic.h>
#include "core/map/maps.h"

extern void  Monochrome( Widget widget, XtPointer clientData, XtPointer callData);

extern int  gnis_locate_place(Widget w, char *name, char *state,
                              char *county, char *quad, char* type, char *filename, int
                              follow_case, int get_match, char match_array_name[50][200], long
                              match_array_lat[50], long match_array_long[50]);

extern int  pop_locate_place(Widget w, char *name, char *state,
                             char *county, char *quad, char* type, char *filename, int
                             follow_case, int get_match, char match_array_name[50][200], long
                             match_array_lat[50], long match_array_long[50]);

extern void Print_Postscript(Widget widget, XtPointer clientData, XtPointer callData);

#endif  /* ASTIR_MAPS_GUI_H */
