// display_station, draw_trail, track_station and set_map_position moved to
// station_draw.c; declared in station_draw.h.
#ifndef XASTIR_DB_GUI_H
#define XASTIR_DB_GUI_H

#include <Xm/XmAll.h>

#define MY_TRAIL_COLOR      0x16    /* trail color index reserved for my station */


// ------------------------------------------------------------------------
// INITIALIZATION FUNCTIONS
// ------------------------------------------------------------------------
void db_gui_init(void);


// ------------------------------------------------------------------------
// STATION TRACKING FUNCTIONS
// ------------------------------------------------------------------------


// ------------------------------------------------------------------------
// DRAWING AND RENDERING FUNCTIONS
// ------------------------------------------------------------------------
// draw_ruler_text/draw_ruler/display_file moved to station_draw.h: none of
// them ever used the Widget they carried.


// ------------------------------------------------------------------------
// STATION DATA DIALOG FUNCTIONS
// ------------------------------------------------------------------------
extern void Station_data_destroy_track(Widget widget, XtPointer clientData, XtPointer callData);

// ------------------------------------------------------------------------
// STATION INFO DIALOG FUNCTIONS
// ------------------------------------------------------------------------
extern void Station_info(Widget w, XtPointer clientData, XtPointer calldata);


// ------------------------------------------------------------------------
// STATION QUERY FUNCTIONS
// ------------------------------------------------------------------------
extern void General_query(Widget w, XtPointer clientData, XtPointer calldata);
extern void IGate_query(Widget w, XtPointer clientData, XtPointer calldata);
extern void WX_query(Widget w, XtPointer clientData, XtPointer calldata);
void Show_Aloha_Stats(Widget w, XtPointer clientData, XtPointer callData);


// ------------------------------------------------------------------------
// MAIN STATION DATA DIALOG
// ------------------------------------------------------------------------
void Station_data(Widget w, XtPointer clientData, XtPointer calldata);


// ------------------------------------------------------------------------
// STATION INFO DIALOG FUNCTIONS
// ------------------------------------------------------------------------
extern void update_station_info(Widget w);


// ------------------------------------------------------------------------
// MAP POSITION AND LOCATION UTILITIES
// ------------------------------------------------------------------------
int locate_station(Widget w, char *call, int follow_case, int get_match, int center_map);


#endif // XASTIR_DB_GUI_H