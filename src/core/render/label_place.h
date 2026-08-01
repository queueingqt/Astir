/*
 * Deciding which labels to draw when they would overlap.
 *
 * A map with more names than room is worse than a map with fewer: overlapping
 * text is unreadable everywhere it overlaps, so drawing every label costs you
 * the ones that mattered as well as the ones that did not.  Every renderer that
 * draws names has to solve this, and it is a large part of what a full vector
 * tile renderer spends its time on.
 *
 * WHY PLACEMENT IS DEFERRED
 *
 * The obvious approach -- test each label against the ones already drawn and
 * skip it if it collides -- gives the map to whichever label happened to be
 * drawn first, which is an artefact of file order and tile order.  A village
 * beats a city because its tile came first.
 *
 * So labels are SUBMITTED during drawing and placed at the end: sorted by
 * priority, then greedily placed, so the important names claim their space and
 * the rest fit around them.  The cost is holding a few hundred strings for the
 * length of a pass, which is nothing against the time to draw the map itself.
 *
 * WHAT IT DOES NOT DO
 *
 * No alternate placement.  A real labeller will try a name above, below, left
 * and right of its anchor, and will curve a name along a river.  This tries one
 * position and gives up, which is the difference between "readable" and "well
 * typeset" and is worth knowing before blaming the data.
 */
#ifndef ASTIR_LABEL_PLACE_H
#define ASTIR_LABEL_PLACE_H

#include "draw/xa_draw.h"

/*
 * Priorities.  Higher wins the space.
 *
 * Station callsigns outrank every map name deliberately: this is an APRS
 * client, the stations are the reason it is running, and a place name is
 * context.  A map that hides a callsign behind a suburb name has its job
 * backwards.
 */
#define LABEL_PRIO_STATION   1000
/*
 * The lines under a callsign: speed, course, weather, distance, age.
 *
 * Below the callsign and above everything on the map.  Which matters when a net
 * or an event puts a dozen stations in one block: the callsigns still win their
 * space, and the detail beside them is dropped rather than drawn through them.
 * Zoom in and there is room, so it comes back.
 *
 * Before this they did not compete at all -- only the callsign went through the
 * placer and every other line was drawn straight to the canvas, so a crowd of
 * weather stations produced a solid block of overlapping text.
 */
#define LABEL_PRIO_STATION_DETAIL 950
#define LABEL_PRIO_PLACE_MAX  900   /* a continent or country */
#define LABEL_PRIO_MAP        500   /* shapefile feature names */
#define LABEL_PRIO_MIN          0

// Start a pass.  Clears whatever the previous one placed.
void label_frame_begin(void);

/*
 * Offer a label for placement.
 *
 * Copies the text, so the caller's buffer may be reused immediately -- which
 * matters because most callers are working out of one scratch buffer per
 * feature.  Returns 0 if the label could not be queued at all.
 *
 * `angle` is passed through to the renderer unchanged.  Rotated labels are
 * tested by their upright bounding box, which is generous for a diagonal
 * street name; making it tight would mean rotating the rectangle and is not
 * worth it until something looks wrong.
 */
/*
 * How a label is rendered once it has won its space.
 *
 * ROTATED is the map-label path.  OUTLINED is draw_nice_string's, which paints
 * the text several times in a contrasting colour before painting it -- the only
 * thing that keeps a callsign readable over an arbitrary map underneath it.
 * Placement does not care which; drawing does.
 */
#define LABEL_STYLE_ROTATED  0
#define LABEL_STYLE_OUTLINED 1

/*
 * For OUTLINED, draw_nice_string's own style number is carried alongside.
 * It is a user setting -- outline, grey box, or plain -- and hardcoding it
 * here silently overrode the preference for every station on the map.
 */

int label_submit(long x, long y, int angle, const char *text,
                 const char *fontspec, int color, int font_size, int priority);

// As label_submit, choosing how it is drawn and, for outlined text, the
// colour it is outlined in.
int label_submit_styled(long x, long y, int angle, const char *text,
                        const char *fontspec, int color, int bgcolor,
                        int font_size, int priority, int style,
                        int text_style);

/*
 * Place everything submitted, and draw what fits.
 *
 * Returns the number drawn; `submitted` receives the number offered, so a
 * caller can report how many names the view could not hold.
 */
int label_flush(xa_surface_id where, int *submitted);

#endif /* ASTIR_LABEL_PLACE_H */
