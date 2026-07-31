/*
 * wx_gui.h -- the weather dialogs.
 *
 * Moved out of wx.h, which wx.c and the alert code include.
 */

#ifndef ASTIR_WX_GUI_H
#define ASTIR_WX_GUI_H

#include <X11/Intrinsic.h>

extern Widget GetTopShell(Widget w);
extern void pos_dialog(Widget w);
extern void WX_station(Widget w, XtPointer clientData, XtPointer callData);
extern void wx_alert_finger_output( Widget widget, char *handle);

#endif  /* ASTIR_WX_GUI_H */
