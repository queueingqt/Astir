/*
 * xa_state.c -- definitions of the shared view state.  See xa_state.h.
 *
 * These were defined in main.c.  Relocating them here moves the symbols out of
 * main.o, so a core object that needs the current view no longer forces the
 * Motif GUI to be linked.  Names, types and initial values are unchanged, so no
 * call site needed editing and behaviour cannot have changed.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "core/state/xa_state.h"

int debug_level;

long center_longitude;       // Longitude at center of map, updated by display_zoom_image()
long center_latitude;        // Latitude  at center of map, updated by display_zoom_image()
long NW_corner_longitude;    // Longitude at NW corner, updated by create_image(), refresh_image()
long NW_corner_latitude;     // Latitude  at NW corner, updated by create_image(), refresh_image()
long SE_corner_longitude;    // Longitude at SE corner, updated by create_image(), refresh_image()
long SE_corner_latitude;     // Latitude  at SE corner, updated by create_image(), refresh_image()

float f_NW_corner_longitude; // longitude of NW corner, updated by create_image(), refresh_image()
float f_NW_corner_latitude;  // latitude  of NW corner, updated by create_image(), refresh_image()
float f_SE_corner_longitude; // longitude of SE corner, updated by create_image(), refresh_image()
float f_SE_corner_latitude;  // latitude  of SE corner, updated by create_image(), refresh_image()

long scale_x;                // x scaling in 1/100 sec per pixel, calculated from scale_y
long scale_y;                // y scaling in 1/100 sec per pixel

long screen_width;           // Screen width,  map area without border (in pixels)
long screen_height;          // Screen height, map area without border (in pixels)
