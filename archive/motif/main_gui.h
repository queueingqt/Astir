/*
 * main_gui.h -- the parts of main.h that are made of widgets.
 *
 * main.h is the front end's own header, but core files include it for the
 * settings, counters and helpers that also live there -- and it opened with
 * #include <X11/Intrinsic.h>, so it was the second root (after astir.h) that
 * put Xt in front of every core file in the tree.
 *
 * Everything moved here is a toplevel widget, a Widget-taking callback, or the
 * coordinate-calculator dialog's own struct.  None of it has a core caller.
 *
 * Only the front end includes this header.  If a core file needs to, something
 * has been put on the wrong side.
 */

#ifndef ASTIR_MAIN_GUI_H
#define ASTIR_MAIN_GUI_H

#include <X11/Intrinsic.h>

typedef struct
{
  Widget calling_dialog;  // NULL if the calling dialog has been closed.
  Widget input_lat_deg;   // Pointers to calling dialog's widgets
  Widget input_lat_min;   // (Where to get/put the data)
  Widget input_lat_dir;
  Widget input_lon_deg;
  Widget input_lon_min;
  Widget input_lon_dir;
} coordinate_calc_array_type;
extern coordinate_calc_array_type coordinate_calc_array;
extern void Coordinate_calc(Widget w, XtPointer clientData, XtPointer callData);


extern void HandlePendingEvents(XtAppContext app);
extern void create_gc(Widget w);
extern void Station_List(Widget w, XtPointer clientData, XtPointer calldata);
extern void Tracks_All_Clear(Widget w, XtPointer clientData, XtPointer callData);
extern void Locate_station(Widget w, XtPointer clientData, XtPointer callData);
extern void Locate_place(Widget w, XtPointer clientData, XtPointer callData);
extern void Geocoder_place(Widget w, XtPointer clientData, XtPointer callData);
extern void Configure_geocoder_settings(Widget w, XtPointer clientData, XtPointer callData);
extern void Display_Wx_Alert(Widget w, XtPointer clientData, XtPointer callData);
extern void Auto_msg_option(Widget w, XtPointer clientData, XtPointer calldata);
extern void Auto_msg_set(Widget w, XtPointer clientData, XtPointer calldata);
extern void Bulletins(Widget w, XtPointer clientData, XtPointer callData);
extern void on_off_switch(int switchpos, Widget first, Widget second);
extern void busy_cursor(Widget w);
extern void pos_dialog(Widget w);
extern int create_image(Widget w);

extern Widget iface_da;

extern Widget trackme_button;

extern Widget CAD_close_polygon_menu_item;

extern Widget Display_data_dialog;

extern Widget Display_data_text;

extern Widget text3;

extern Widget text4;

extern Widget log_indicator;

extern void Center_Zoom(Widget w, XtPointer clientData, XtPointer calldata);

extern Widget auto_msg_toggle;

extern Widget configure_station_dialog;

extern Widget station_config_group_data;

extern Widget station_config_symbol_data;

extern void updateSymbolPictureCallback(Widget w,XtPointer clientData,XtPointer callData);

extern Widget object_dialog;

extern Widget object_group_data;

extern Widget object_symbol_data;

extern void updateObjectPictureCallback(Widget w,XtPointer clientData,XtPointer callData);

#endif  /* ASTIR_MAIN_GUI_H */
