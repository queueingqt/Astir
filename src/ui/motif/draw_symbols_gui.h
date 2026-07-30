/*
 * draw_symbols_gui.h -- the symbol-selection dialog.
 *
 * These three were in draw_symbols.h, which core files include for the drawing
 * entry points.  The drawing ones now take xa_surface_id rather than Pixmap --
 * the same type, without the toolkit's name on it -- so that header is
 * X-free and these had to go somewhere.
 */

#ifndef XASTIR_DRAW_SYMBOLS_GUI_H
#define XASTIR_DRAW_SYMBOLS_GUI_H

#include <X11/Intrinsic.h>

extern void Select_symbol( Widget w, XtPointer clientData, XtPointer callData);
extern Widget select_symbol_dialog;
extern void Select_symbol_destroy_shell( Widget widget, XtPointer clientData, XtPointer callData);

#endif  /* XASTIR_DRAW_SYMBOLS_GUI_H */
