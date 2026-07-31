/*
 * xa_state.h -- shared view state, owned by the core rather than by main.c.
 *
 * Core extraction (Stage 4).  The goal is a core that a new GTK4 front end can
 * link against without dragging in Motif.  Today the blocker is that core
 * objects reference state defined in main.c, which is 31k lines of mixed Motif
 * and core logic, so linking the core means linking the Motif GUI.
 *
 * Measured before starting (core_boundary.py against the built objects):
 * 40 core objects need 224 symbols from main.o, 194 of them data.  But the
 * widely-shared ones are not GUI at all -- they are the current view:
 *
 *     debug_level        used by 30 core objects
 *     scale_y / scale_x  used by 15 / 13
 *     NW/SE corners      used by 12 / 12 / 10 / 9
 *     screen_width/height used by 12 each
 *
 * Only about ten of the shared symbols are real X11/Motif handles (gc, pixmap,
 * da, appshell, app_context, colors, cmap, visual_type), and those are what the
 * xa_draw layer exists to hide.  So the split is clean: this file holds the
 * plain-data view state, xa_draw holds the toolkit handles.
 *
 * This header must never include an X11, Xt or Motif header.  That is the
 * property that makes it useful -- anything that includes only this can be
 * built into a core library and linked by any front end.
 *
 * The names and types are unchanged from main.c on purpose: relocating the
 * definitions moves the symbols out of main.o without touching a single one of
 * the hundreds of call sites, so the step cannot alter behaviour.  Introducing
 * accessors, if wanted, is a later and separable change.
 */

#ifndef XA_STATE_H
#define XA_STATE_H

// Verbosity, consulted throughout the core.  The single most widely shared
// symbol in the tree.
extern int debug_level;

// The current view, in Astir coordinates (1/100 second units).  Updated by
// create_image() and refresh_image() as the map is panned and zoomed.
extern long center_longitude;       // Longitude at center of map
extern long center_latitude;        // Latitude  at center of map
extern long NW_corner_longitude;    // Longitude at NW corner
extern long NW_corner_latitude;     // Latitude  at NW corner
extern long SE_corner_longitude;    // Longitude at SE corner
extern long SE_corner_latitude;     // Latitude  at SE corner

// The same corners in floating-point degrees, kept in step with the above.
extern float f_NW_corner_longitude;
extern float f_NW_corner_latitude;
extern float f_SE_corner_longitude;
extern float f_SE_corner_latitude;

// Scaling, in 1/100 sec per pixel.  scale_x is derived from scale_y.
extern long scale_x;
extern long scale_y;

// Size of the map area, excluding any border, in pixels.
extern long screen_width;
extern long screen_height;

#endif // XA_STATE_H
