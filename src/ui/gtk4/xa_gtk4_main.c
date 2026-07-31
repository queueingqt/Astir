/*
 * xa_gtk4_main.c -- a GTK4 front end for Astir.
 *
 * WHAT THIS IS
 *
 * A real application: it initialises the core, loads the map index, renders
 * maps through xa_draw_gtk4.c, and shows them in a window you can pan and zoom.
 * It is the first thing in this tree to draw an Astir map without Motif.
 *
 * WHAT IT IS NOT
 *
 * It is not a replacement for the Motif front end, which is ~69,000 lines
 * across 16 files plus main.c's 30,800.  There are no dialogs, no menus beyond
 * a handful of actions, no interfaces, no message windows, no station list, no
 * configuration UI.  What is here is the spine -- window, canvas, render loop,
 * the xa_ui callbacks -- and the point of it is that the spine is the part
 * nobody could write until the core stopped requiring Motif.
 *
 * Deliberately not a port of main.c's style.  Motif's idioms are not GTK4's:
 * this uses GtkApplication, a header bar, GAction, and gesture controllers for
 * pan and zoom rather than an event-mask switch.  The core does not care --
 * that is the whole result of the last several sessions.
 *
 * WHAT IT SHARES WITH THE MOTIF BUILD
 *
 * Everything below xa_draw.h and xa_ui.h, which is to say the entire
 * application: the config file, the map index, the map drivers, the shapefile
 * reader, the APRS parser, the station database.  Not one line of it is
 * compiled differently for this binary.
 */

#include <gtk/gtk.h>
#include <glib-unix.h>          // g_unix_signal_add, for saving on Ctrl-C
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/astir.h"
#include "draw/xa_draw.h"
#include "core/state/xa_state.h"
#include "core/state/xa_settings.h"
#include "core/state/xa_config.h"
#include "core/state/first_run.h"
#include "core/io/incoming.h"
#include "core/io/interface.h"
#include "ui/gtk4/xa_gtk4_interfaces.h"
#include "ui/gtk4/xa_gtk4_station.h"
#include "core/xa_ui.h"
#include "core/map/maps.h"
#include "core/util/lang.h"
#include "core/util/util.h"
#include "core/aprs/db_funcs.h"
#include "core/aprs/station_draw.h"
#include "core/render/draw_symbols.h"
#include "core/render/label_place.h"
#include "core/util/snprintf.h"
#include "core/util/xa_perf.h"

#ifdef HAVE_LIBCURL
  #include <curl/curl.h>
#endif
#ifdef HAVE_GRAPHICSMAGICK
  #include <magick/api.h>
#else
  #ifdef HAVE_IMAGEMAGICK
    #include <MagickCore/MagickCore.h>
  #endif
#endif

#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>

// The backend's private entry points.  Declared here rather than in a header
// because exactly one front end ever calls them, and which backend is in use
// is a build-time choice.
void             xa_gtk4_set_canvas(GtkWidget *canvas, int width, int height);
cairo_surface_t *xa_gtk4_canvas_surface(void);
cairo_surface_t *xa_gtk4_surface_of(xa_surface_id s);
int              xa_gtk4_set_device_scale(int scale);
int              xa_gtk4_device_scale(void);

// From xa_gtk4_palette.c, generated from main.c's colour table.
void xa_gtk4_load_palette(void);

static GtkWidget *xa_area = NULL;      // the map canvas
static GtkWidget *xa_status = NULL;    // the status line in the header bar
static int xa_w = 1024, xa_h = 700;
static int xa_ready = 0;               // core initialised, safe to render

/*
 * The marker layer.
 *
 * Stations, symbols, trails and range rings, on their own transparent surface
 * rather than painted into the map.  They are not part of the picture of the
 * world; they are things AT places in it, and the difference shows the moment
 * the view moves: a map scales when you zoom, a station does not.  It moves.
 *
 * Sharing one buffer forced them to share one transform, which is why an icon
 * grew during a zoom and snapped back when the real frame landed -- the whole
 * reason for tracing the symbols into outlines was undone by compositing them
 * into something that gets stretched.
 *
 * Measured as separate passes: the map is 1393 ms and the markers 35 ms, so
 * markers can be redrawn on every frame of a gesture and the map cannot.  That
 * ratio is the argument for the split, not tidiness.
 */
static xa_surface_id pixmap_markers = XA_SURFACE_NONE;
static void xa_render_markers(void);

/*
 * How far the marker layer has to slide to match the current view.
 *
 * Separate from view_dx/view_dy, and it has to be: the two layers go stale at
 * different moments.  view_dx is how far the MAP is behind, and the map is
 * redrawn on a timer; the markers are redrawn immediately, so the instant they
 * are they owe nothing, even while the map still owes the whole gesture.
 *
 * Sharing view_dx meant that at the end of a drag the markers were re-projected
 * to the new centre AND then translated by the same amount again -- so they
 * flew past the pointer and snapped back when the map caught up.
 */
static double marker_dx = 0.0, marker_dy = 0.0;

/*
 * The map layer as it was last drawn, and the view it was drawn for.
 *
 * A pan moves the view without changing the scale, so most of the new frame is
 * the old one shifted: only an L-shaped strip along two edges is genuinely
 * new.  Keeping the previous layer lets that strip be the only thing redrawn.
 *
 * Only for pans.  A zoom changes what every pixel means, and there is nothing
 * to reuse.
 */
static xa_surface_id pixmap_prev = XA_SURFACE_NONE;
static long prev_center_lon, prev_center_lat;
static long prev_scale_x, prev_scale_y;
static int  prev_valid = 0;


/* ---- rendering --------------------------------------------------------- */

/*
 * Compose a frame.
 *
 * The same sequence main.c's create_image() runs, minus the parts that need a
 * front end this does not have yet (weather alerts, station symbols -- both
 * live behind db_gui.c).  Every call in it is an xa_draw one, which is why it
 * fits in thirty lines here and took a thousand there.
 */
/*
 * Set for as long as a frame is being composed.
 *
 * Read by ui_pump_events(), which must not re-enter the main loop while this is
 * set.  Declared here rather than beside the render scheduler because it has to
 * wrap EVERY entry to xa_render(), not just the scheduled one -- a resize, a
 * scale change, a reindex and the core's own redraw request all call it too, and
 * a flag that covered one of five was a flag that reported the wrong thing four
 * times out of five.
 */
static int rendering;

static void xa_render(void)
{
  if (!xa_ready || pixmap == XA_SURFACE_NONE)
  {
    return;
  }
  rendering++;

  xa_perf_frame_begin();

  /*
   * The view rectangle, from the centre and the scale.
   *
   * This is not bookkeeping: map_onscreen_index() culls every map against these
   * corners, so with them left at zero every map in the index reports
   * MAP_NOT_VIS and load_maps() draws nothing at all.  That is what it did --
   * three maps found, three drivers selected, nothing drawn, and no error
   * anywhere, because "not visible" is a normal answer.
   *
   * create_image() computes them in main.c, which is exactly the kind of thing
   * a second front end has no way to discover except by finding it missing.
   */
  NW_corner_longitude = center_longitude - (screen_width  * scale_x / 2);
  NW_corner_latitude  = center_latitude  - (screen_height * scale_y / 2);
  SE_corner_longitude = center_longitude + (screen_width  * scale_x / 2);
  SE_corner_latitude  = center_latitude  + (screen_height * scale_y / 2);
  convert_from_astir_coordinates(&f_NW_corner_longitude, &f_NW_corner_latitude,
                                  NW_corner_longitude, NW_corner_latitude);
  convert_from_astir_coordinates(&f_SE_corner_longitude, &f_SE_corner_latitude,
                                  SE_corner_longitude, SE_corner_latitude);

  /*
   * Reuse the previous map layer when this is a pan.
   *
   * Same scale and a shift smaller than the window means the old pixels are
   * still correct, just in a different place.  Blit them across and mark only
   * the newly exposed strip dirty; everything else is skipped by the culling
   * that every map driver already runs.
   *
   * The exposed area is an L, and this uses its bounding box, so a diagonal
   * drag saves little and an axis-aligned one -- which is most dragging --
   * saves nearly everything.  Two rectangles would cover the L exactly and is
   * the obvious refinement.
   */
  {
    int reused = 0;

    // ASTIR_GTK4_REUSE=0 forces a full redraw, which is how the reused frame
    // is checked against the frame it is supposed to be identical to.
    static int reuse_enabled = -1;

    // The configured background into colors[0xfd], which both clears below use.
    map_background_apply();

    if (reuse_enabled < 0)
    {
      const char *e = getenv("ASTIR_GTK4_REUSE");

      reuse_enabled = (e != NULL && atoi(e) == 0) ? 0 : 1;
    }

    if (reuse_enabled && prev_valid && pixmap_prev != XA_SURFACE_NONE
        && scale_x == prev_scale_x && scale_y == prev_scale_y)
    {
      long dpx = (prev_center_lon - center_longitude) / scale_x;
      long dpy = (prev_center_lat - center_latitude) / scale_y;

      if ((dpx != 0 || dpy != 0)
          && labs(dpx) < screen_width && labs(dpy) < screen_height)
      {
        long l, r, tp, bt;

        xa_pen_color(gc, colors[0xfd]);
        xa_pen_bg(gc, colors[0xfd]);
        xa_fill_rect(pixmap, gc, 0, 0, (int)screen_width, (int)screen_height);
        xa_copy_area(pixmap_prev, pixmap, gc, 0, 0,
                     (int)screen_width, (int)screen_height,
                     (int)dpx, (int)dpy);

        // The strip uncovered by the shift, as a bounding box, in Astir units.
        l  = (dpx > 0) ? NW_corner_longitude
                       : NW_corner_longitude + (screen_width + dpx) * scale_x;
        r  = (dpx > 0) ? NW_corner_longitude + dpx * scale_x
                       : SE_corner_longitude;
        tp = (dpy > 0) ? NW_corner_latitude
                       : NW_corner_latitude + (screen_height + dpy) * scale_y;
        bt = (dpy > 0) ? NW_corner_latitude + dpy * scale_y
                       : SE_corner_latitude;

        if (dpx == 0) { l = NW_corner_longitude; r = SE_corner_longitude; }
        if (dpy == 0) { tp = NW_corner_latitude; bt = SE_corner_latitude; }
        if (dpx != 0 && dpy != 0)
        {
          l = NW_corner_longitude;      // both axes moved: the L spans
          r = SE_corner_longitude;      // the window, so redraw it all
          tp = NW_corner_latitude;
          bt = SE_corner_latitude;
        }
        xa_dirty_set(l, r, tp, bt);
        reused = 1;
      }
    }

    if (!reused)
    {
      xa_dirty_clear();
      xa_pen_color(gc, colors[0xfd]);      // map background
      xa_pen_bg(gc, colors[0xfd]);
      xa_fill_rect(pixmap, gc, 0, 0, (int)screen_width, (int)screen_height);
    }
  }

  /*
   * Labels are collected during the map pass and placed at the end of it.
   * Opening the frame here and flushing below is what lets an important name
   * beat one that merely drew first.
   */
  label_frame_begin();
  load_maps();
  xa_dirty_clear();

  // Remember this layer and the view it belongs to, for the next pan.
  if (pixmap_prev != XA_SURFACE_NONE)
  {
    xa_copy_area(pixmap, pixmap_prev, gc, 0, 0,
                 (int)screen_width, (int)screen_height, 0, 0);
    prev_center_lon = center_longitude;
    prev_center_lat = center_latitude;
    prev_scale_x = scale_x;
    prev_scale_y = scale_y;
    prev_valid = 1;
  }

  xa_copy_area(pixmap, pixmap_alerts, gc, 0, 0,
               (int)screen_width, (int)screen_height, 0, 0);
  xa_copy_area(pixmap_alerts, pixmap_final, gc, 0, 0,
               (int)screen_width, (int)screen_height, 0, 0);

  {
    int offered = 0;
    int drawn = label_flush(pixmap_final, &offered);

    if (debug_level & 16)
    {
      fprintf(stderr, "labels: %d of %d placed\n", drawn, offered);
    }
  }

  if (long_lat_grid)
  {
    xa_perf_begin(XA_ZONE_DRAW_GRID);
    draw_grid();
    xa_perf_end(XA_ZONE_DRAW_GRID);
  }

  xa_present_full(pixmap_final);
  xa_perf_frame_end("gtk4_render");

  // The markers are drawn for the view the map was just drawn for.
  xa_render_markers();

  rendering--;
}


/*
 * Draw the marker layer for the CURRENT view.
 *
 * Cheap enough to run on every frame of a drag or a zoom -- 35 ms against the
 * map's 1393 -- which is the point: markers are re-projected rather than
 * stretched, so they stay the size and shape they were drawn at while the map
 * underneath is still catching up.
 *
 * The corners have to be recomputed here as well as in xa_render(), because
 * this runs without it: display_file() projects every station against them, and
 * with stale corners the markers would be drawn for the previous view.
 */
static void xa_render_markers(void)
{
  if (!xa_ready || pixmap_markers == XA_SURFACE_NONE)
  {
    return;
  }
  NW_corner_longitude = center_longitude - (screen_width  * scale_x / 2);
  NW_corner_latitude  = center_latitude  - (screen_height * scale_y / 2);
  SE_corner_longitude = center_longitude + (screen_width  * scale_x / 2);
  SE_corner_latitude  = center_latitude  + (screen_height * scale_y / 2);
  convert_from_astir_coordinates(&f_NW_corner_longitude, &f_NW_corner_latitude,
                                  NW_corner_longitude, NW_corner_latitude);
  convert_from_astir_coordinates(&f_SE_corner_longitude, &f_SE_corner_latitude,
                                  SE_corner_longitude, SE_corner_latitude);

  // Its own frame, because it is its own pass now.  Folded into the map frame
  // it was 23 ms lost in 633; run after that frame ended it was not counted at
  // all.  It is the pass that has to stay fast, so it is the pass to watch.
  xa_perf_frame_begin();
  xa_surface_clear(pixmap_markers);

  // display_file() draws onto whatever pixmap_final names, so point it at the
  // overlay for the duration.  A parameter would be better and is a separate
  // change: fifteen call sites reach for these globals.
  {
    xa_surface_id was = pixmap_final;

    pixmap_final = pixmap_markers;
    xa_perf_begin(XA_ZONE_DISPLAY_FILE);

    /*
     * The marker layer gets its own label frame.
     *
     * Callsigns compete with each other, not with the map's names: the two
     * layers are drawn at different times into different surfaces, which is
     * the whole point of the split, so one registry cannot span both.  In
     * practice the ordering is right anyway -- the marker layer is composited
     * over the map, so a callsign covers a place name rather than the reverse.
     * What is lost is the ink spent on the covered name, not the callsign.
     */
    label_frame_begin();
    display_file();
    {
      int offered = 0;
      int drawn = label_flush(pixmap_markers, &offered);

      if (debug_level & 16)
      {
        fprintf(stderr, "station labels: %d of %d placed\n", drawn, offered);
      }
    }
    xa_perf_end(XA_ZONE_DISPLAY_FILE);
    pixmap_final = was;
  }
  // Drawn for the current view, so nothing is owed.
  marker_dx = 0.0;
  marker_dy = 0.0;
  xa_perf_frame_end("gtk4_markers");

  if (xa_area != NULL)
  {
    gtk_widget_queue_draw(xa_area);
  }
}


/*
 * Rendering is deferred and coalesced.
 *
 * xa_render() re-reads every map: half a second to a second.  Calling it
 * straight from an event handler blocks the main loop for that long, which has
 * two visible consequences.  A burst of scroll events each queue their own
 * render, so the map runs away and every intermediate frame is wasted work.
 * And gtk_widget_queue_draw() cannot be serviced while the handler is still
 * inside a render, so the canvas keeps showing the previous frame until some
 * later event lets the loop run -- which is why a stale grey area would appear
 * to "render if you click after".
 *
 * So handlers change the position and ask for a render; one actually happens,
 * shortly after the input stops.
 */
static guint render_tick;

/*
 * How far the view has moved since the frame on screen was composed.
 *
 * A full render is half a second to a second, so waiting for one before
 * anything moves is what makes a drag or a scroll feel dead.  Instead the
 * gesture updates these, the drawing area transforms the frame it already has
 * -- instant, and blurry or edge-clipped in the way every map application is
 * mid-gesture -- and the real render replaces it when it arrives.
 */
static double view_dx, view_dy;
static double view_scale = 1.0;

static void render_soon(void);   // defined below; render_now() re-arms itself
static void housekeep_now(void);
static gboolean render_now(gpointer u);
static gboolean render_on_frame(GtkWidget *w, GdkFrameClock *c, gpointer u);

/*
 * The frame clock has come round.  Draw, and stand down.
 *
 * Removing itself is what keeps an idle Astir idle: with nothing moving there
 * is no callback installed at all, so the process sleeps rather than being
 * woken sixty times a second to decide it has nothing to do.
 */
static gboolean render_on_frame(GtkWidget *w, GdkFrameClock *clock, gpointer u)
{
  (void)w;
  (void)clock;
  render_tick = 0;
  housekeep_now();               // expiry, on an event rather than a clock
  (void)render_now(u);
  return G_SOURCE_REMOVE;
}


static gboolean render_now(gpointer u)
{
  double dx0, dy0, s0;

  (void)u;

  // Cleared FIRST, and unconditionally.  Returning early with it still set
  // leaves render_soon() believing a render is pending forever, so nothing
  // ever renders again -- one skipped frame stops the map permanently.
  render_tick = 0;

  if (rendering)
  {
    render_soon();                 // re-entered; try again once this one is out
    return G_SOURCE_REMOVE;
  }

  /*
   * Take the preview transform as it stands *now*, because the render is not
   * atomic: the core calls xa_ui_pump_events() every 64 shapes so a slow
   * redraw can be interrupted, and this front end services the main loop
   * there.  Scroll and drag events therefore run *inside* xa_render(), and
   * they move view_scale/view_dx while it works.
   *
   * Resetting them to 1 and 0 afterwards, which is what this did first, threw
   * away every one of those -- so zooming out several notches during a render
   * committed only the part that arrived before it started, and the view
   * snapped back to roughly one step out.  Divide out what this frame
   * accounts for instead, and whatever accumulated meanwhile survives.
   */
  dx0 = view_dx;
  dy0 = view_dy;
  s0  = view_scale;

  xa_render();

  view_dx -= dx0;
  view_dy -= dy0;
  if (s0 != 0.0)
  {
    view_scale /= s0;
  }
  if (getenv("ASTIR_GTK4_TRACE_ZOOM"))
  {
    g_print("[render] scale_y=%ld view_scale=%.3f\n", scale_y, view_scale);
  }
  gtk_widget_queue_draw(xa_area);
  return G_SOURCE_REMOVE;
}

/*
 * Ask for a frame.
 *
 * On the widget's frame clock, not a timeout.  The frame clock is the
 * compositor asking whether anything wants to be drawn, so a render is
 * scheduled against the display's own rhythm instead of against a hundred and
 * fifty milliseconds somebody picked.  Nothing is armed while the map is still:
 * the callback removes itself after one frame, and installing it again is what
 * the next gesture does.
 *
 * Repeated calls before that frame arrives collapse into one, which is the
 * coalescing the timeout was there for.
 */
static void render_soon(void)
{
  if (render_tick == 0 && xa_area != NULL)
  {
    render_tick = gtk_widget_add_tick_callback(xa_area, render_on_frame,
                                               NULL, NULL);
  }
}


/*
 * The credit the drawn maps require, painted over the finished frame.
 *
 * After cairo_restore, deliberately.  Everything before it is inside the
 * scheduler's preview transform -- view_scale and view_dx/view_dy show the
 * previous frame stretched and slid while the next one is composed -- so a
 * credit drawn with the map zoomed with the map and slid with a drag.  It is
 * chrome: fixed corner, fixed size, whatever the map underneath is doing.
 *
 * Pango rather than the core's text helper, because the core's helper draws
 * into a surface and this has to land on the widget.
 */
static void draw_attribution(cairo_t *cr, int width, int height)
{
  PangoLayout *layout;
  int tw, th;

  (void)width;
  if (map_attribution[0] == '\0')
  {
    return;                      // no map on screen asks for one
  }

  layout = pango_cairo_create_layout(cr);
  pango_layout_set_text(layout, map_attribution, -1);
  {
    PangoFontDescription *d = pango_font_description_from_string("Sans 9");
    pango_layout_set_font_description(layout, d);
    pango_font_description_free(d);
  }
  pango_layout_get_pixel_size(layout, &tw, &th);

  // Bottom left; the range scale sits bottom right.  A translucent plate
  // rather than an outline, so the text stays legible over a light coastline
  // and a dark forest without eight redraws of every glyph.
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.72);
  cairo_rectangle(cr, 0, height - th - 6, tw + 12, th + 6);
  cairo_fill(cr);

  cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
  cairo_move_to(cr, 6, height - th - 3);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
}


// The drawing area paints whatever the backend has composed.  GTK4 widgets
// render from a snapshot, so the canvas surface the backend owns is the thing
// that persists between frames and this just blits it.
static void xa_draw_cb(GtkDrawingArea *area, cairo_t *cr,
                       int width, int height, gpointer user_data)
{
  cairo_surface_t *s = xa_gtk4_canvas_surface();

  (void)area; (void)user_data;
  if (s == NULL)
  {
    return;
  }
  /*
   * Three layers, three transform policies.  That is the whole design.
   *
   *   the map      IS a picture of the world, so while a new one is composed
   *                the old one is stretched and slid to preview the gesture.
   *                Scaling a picture of a map is a reasonable lie for 150 ms.
   *
   *   the markers  are NOT a picture.  A station is a thing at a place: when
   *                the view changes it moves, and it never changes size.  So
   *                this layer is redrawn for the new view instead of being
   *                transformed, and drawn one to one.
   *
   *   the chrome   does not belong to the world at all and never moves.
   *
   * Before this the three shared one buffer and therefore one policy, and the
   * markers got the map's.  That is why a station icon grew during a zoom and
   * snapped back afterwards -- and why tracing the symbols into outlines had
   * not fixed the pixelation people actually see, which happens during the
   * gesture and not after it.
   */
  cairo_save(cr);
  if (view_scale != 1.0)
  {
    // About the centre of the window, which is what the zoom keeps fixed.
    cairo_translate(cr, width / 2.0, height / 2.0);
    cairo_scale(cr, view_scale, view_scale);
    cairo_translate(cr, -width / 2.0, -height / 2.0);
  }
  cairo_translate(cr, view_dx, view_dy);
  cairo_set_source_surface(cr, s, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);

  if (pixmap_markers != XA_SURFACE_NONE)
  {
    cairo_surface_t *m = xa_gtk4_surface_of(pixmap_markers);

    if (m != NULL)
    {
      /*
       * Translated with the map, but never scaled.
       *
       * The two halves of the gesture are not the same kind of change.  A pan
       * moves every marker by exactly the same number of pixels, so sliding
       * the whole layer is not an approximation -- it is the right answer, and
       * it is why the markers can keep up with the pointer without being
       * redrawn.  A zoom moves them by different amounts each and changes none
       * of their sizes, so the layer is redrawn instead and never stretched.
       *
       * Painting this layer at a fixed origin got the zoom rule right and the
       * pan rule wrong: the map slid under the pointer while the stations
       * stayed nailed to the window and jumped into place on release.
       */
      cairo_save(cr);
      cairo_translate(cr, marker_dx, marker_dy);
      cairo_set_source_surface(cr, m, 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
    }
  }

  draw_attribution(cr, width, height);
}


/*
 * How many device pixels the toolkit gives us per logical pixel.
 *
 * The widget knows, once it is on a monitor; before that, and in the headless
 * render, there is no widget to ask.  ASTIR_GTK4_DEVICE_SCALE overrides both,
 * which is the only way to render a HiDPI frame without a HiDPI monitor and so
 * the only way to gate this from a script.
 */
static int wanted_device_scale(GtkWidget *w)
{
  const char *env = getenv("ASTIR_GTK4_DEVICE_SCALE");
  int s;

  if (env != NULL && (s = atoi(env)) >= 1)
  {
    return s;
  }
  if (w != NULL && gtk_widget_get_realized(w))
  {
    return gtk_widget_get_scale_factor(w);
  }
  return 1;
}


/*
 * Build the canvas and the layer pixmaps at the current size and device scale.
 * Both change independently -- a resize keeps the scale, dragging the window to
 * a different monitor keeps the size -- and either one invalidates every
 * surface, because they are all sized from the canvas.
 */
static void rebuild_surfaces(GtkWidget *area, int width, int height)
{
  xa_gtk4_set_device_scale(wanted_device_scale(area));

  // Surfaces are sized to the canvas, so they are rebuilt rather than scaled.
  xa_surface_destroy(pixmap);
  xa_surface_destroy(pixmap_alerts);
  xa_surface_destroy(pixmap_final);
  xa_surface_destroy(pixmap_markers);
  xa_surface_destroy(pixmap_prev);
  xa_gtk4_set_canvas(area, width, height);
  pixmap         = xa_surface_create(width, height, XA_DEPTH_CANVAS);
  pixmap_alerts  = xa_surface_create(width, height, XA_DEPTH_CANVAS);
  pixmap_final   = xa_surface_create(width, height, XA_DEPTH_CANVAS);
  pixmap_markers = xa_surface_create(width, height, XA_DEPTH_ALPHA);
  pixmap_prev    = xa_surface_create(width, height, XA_DEPTH_CANVAS);
  prev_valid = 0;
}


static void xa_resized(GtkDrawingArea *area, int width, int height,
                       gpointer user_data)
{
  int rescaled = wanted_device_scale(GTK_WIDGET(area)) != xa_gtk4_device_scale();

  (void)user_data;
  // The size test alone is not enough.  This is the first callback that runs
  // with a realized widget, so it is where the real monitor scale first becomes
  // knowable -- and if the allocation happens to match the default size, the
  // early return would leave the frame at scale 1 for good.
  if (width <= 0 || height <= 0
      || (width == xa_w && height == xa_h && !rescaled))
  {
    return;
  }
  xa_w = width;
  xa_h = height;
  screen_width = width;
  screen_height = height;

  rebuild_surfaces(GTK_WIDGET(area), width, height);
  xa_render();
}


// The window moved to a monitor with a different scale.  The logical size has
// not changed, so "resize" does not fire and nothing else would notice.
static void xa_scale_changed(GObject *obj, GParamSpec *pspec,
                             gpointer user_data)
{
  GtkWidget *area = GTK_WIDGET(obj);

  (void)pspec; (void)user_data;
  if (!xa_ready || xa_w <= 0 || xa_h <= 0
      || wanted_device_scale(area) == xa_gtk4_device_scale())
  {
    return;
  }
  rebuild_surfaces(area, xa_w, xa_h);
  xa_render();
}


/* ---- navigation -------------------------------------------------------- */

/*
 * Astir's map position is a centre and a scale in 1/100 second per pixel, and
 * that is core state (xa_state.h), not front-end state.  So panning is
 * arithmetic on center_longitude/center_latitude and zooming is arithmetic on
 * scale_y -- exactly what the Motif front end does, because it is the only
 * thing either front end *can* do.
 */
/*
 * scale_x is not a free variable: it is derived from scale_y and the position,
 * by get_x_scale(), so that a mile is the same number of pixels in both
 * directions.  It also carries two guards worth not reinventing -- it gives up
 * and returns scale_y near the poles (where sc_x collapses) and above
 * scale_y 50000 (where big parts of the world are on screen).
 *
 * The first version here multiplied scale_y by calc_dscale_x(), which is not
 * that formula and not even the right shape -- calc_dscale_x is metres per
 * Astir unit, and the ratio wanted is sc_y/sc_x.  Below 50000 that produced a
 * wildly wrong x scale; at or above it the guard hid the error entirely, which
 * is why the config's own zoom looked fine and zooming in did not.
 */
static void xa_rescale(void)
{
  scale_x = get_x_scale(center_longitude, center_latitude, scale_y);
  if (scale_x < 1)
  {
    scale_x = 1;
  }
}




/*
 * The scale at which the whole world just fills the window.
 *
 * Astir's stored limit is 500000, which is not a view limit -- it is just the
 * largest value the config will hold.  Past the point where 180 degrees of
 * latitude spans the window there is nothing further to reveal, only a
 * shrinking earth in a growing field of background.
 *
 * 32400000 Astir units is 90 degrees of latitude -- a degree is 360000 units --
 * so this fills the window with a hemisphere's worth of height rather than the
 * full 180.  That is deliberate: Web Mercator cannot draw beyond about 85
 * degrees, so the last slice of a full-height view is empty background either
 * way, and stopping at 90 keeps the map filling the window instead of
 * shrinking inside it.
 *
 * Derived from the window height rather than fixed, because a taller window
 * shows the same span at a smaller scale.
 */
static long xa_max_zoom_out(void)
{
  long m = (screen_height > 0) ? (32400000L / screen_height) : 500000L;

  return (m > 500000L) ? 500000L : m;
}


static void xa_zoom(double factor)
{
  long s = (long)(scale_y * factor + 0.5);
  long max_out = xa_max_zoom_out();

  /*
   * Guarantee the step actually moves.
   *
   * scale_y is an integer, and a scroll notch is a 1.15x factor, so once
   * zoomed in far enough the multiplication truncates straight back to where
   * it started: at scale_y 6, 6 * 1.15 is 6.9, which is 6.  The function then
   * returned "already at the limit" and the scroll wheel stopped zooming out
   * entirely, while the menu button kept working because it uses a factor of
   * two and two times an integer is always a different integer.
   *
   * Rounding alone is not enough -- 6 * 1.15 rounds to 7 but 3 * 1.15 rounds
   * back to 3 -- so a step that lands on its own starting value is pushed one
   * unit in the direction it was going.
   */
  if (s == scale_y)
  {
    if (factor > 1.0)
    {
      s = scale_y + 1;
    }
    else if (factor < 1.0)
    {
      s = scale_y - 1;
    }
  }

  if (s < 1)
  {
    s = 1;
  }
  if (s > max_out)
  {
    s = max_out;
  }
  if (s == scale_y)
  {
    return;                        // genuinely at the limit; do not queue work
  }
  // The frame on screen was drawn at the old scale, so showing it that much
  // larger or smaller is exactly right until the new one is ready.
  view_scale *= (double)scale_y / (double)s;
  if (getenv("ASTIR_GTK4_TRACE_ZOOM"))
  {
    g_print("[zoom]   scale_y %ld -> %ld  view_scale=%.3f\n",
            scale_y, s, view_scale);
  }
  scale_y = s;
  xa_rescale();
  // Markers now, map later.  Re-projecting the stations for the new scale is
  // 35 ms, so they land in the right places at the right size immediately,
  // while the map pass waits for the timer and the stretched old frame stands
  // in for it.
  xa_render_markers();
  gtk_widget_queue_draw(xa_area);
  render_soon();
}


/*
 * Panning.
 *
 * Two things here that the first version got wrong.
 *
 * "drag-begin" reports the start *point*; "drag-update" reports the *offset*
 * from that point.  Seeding the tracker with the start coordinate and then
 * subtracting an offset from it made the first update a jump of however many
 * pixels from the window edge the drag happened to start -- which threw the
 * map somewhere else entirely on the first move.  The tracker is an offset now
 * and starts at zero, like the values it is compared against.
 *
 * And a full xa_render() re-reads and re-draws every map, which is half a
 * second to a second here.  Doing that per drag-update means the gesture
 * queues behind renders and the map appears frozen.  So a drag accumulates
 * position only and the render happens once, on release.
 */
static double drag_prev_ox, drag_prev_oy;

static void on_drag_begin(GtkGestureDrag *g, double x, double y, gpointer u)
{
  (void)g; (void)x; (void)y; (void)u;
  drag_prev_ox = 0.0;
  drag_prev_oy = 0.0;
}

static void on_drag_update(GtkGestureDrag *g, double ox, double oy, gpointer u)
{
  (void)g; (void)u;
  // Just slide the frame that is already composed.  No map work at all, so
  // this keeps up with the pointer.
  view_dx = ox;
  view_dy = oy;
  // The markers are not redrawn during the drag, so they slide with the map.
  // A pan moves every one of them by the same amount, which is why sliding is
  // exact rather than an approximation.
  marker_dx = ox;
  marker_dy = oy;
  gtk_widget_queue_draw(xa_area);
}

static void on_drag_end(GtkGestureDrag *g, double ox, double oy, gpointer u)
{
  (void)g; (void)u;
  // Commit the whole gesture to the map position in one step.  view_dx/dy stay
  // where they are so the picture does not snap back while the render runs.
  view_dx = ox;
  view_dy = oy;
  center_longitude -= (long)(ox * scale_x);
  center_latitude  -= (long)(oy * scale_y);
  xa_rescale();
  xa_render_markers();             // see the zoom handler
  gtk_widget_queue_draw(xa_area);
  render_soon();
}

/*
 * Scroll to zoom.
 *
 * Proportional to dy, not a fixed factor per event.  A wheel notch reports
 * dy = +/-1 and a touchpad reports fractions of one, but a single flick of
 * either delivers several events -- so doubling the scale per event, which is
 * what this did first, ran from a city to the whole globe before the hand had
 * stopped moving.  Past that point no map is in range and the screen goes
 * grey, which reads as "zooming does not redraw" rather than as overshoot.
 *
 * 1.2 per notch takes about four notches to double, which is controllable.
 * The buttons and keys still step by 2, because those are deliberate.
 */
static gboolean on_scroll(GtkEventControllerScroll *c, double dx, double dy,
                          gpointer u)
{
  (void)c; (void)dx; (void)u;
  if (dy != 0.0)
  {
    // Clamped, because dy is not comparable across devices: a wheel notch may
    // report 1, a high-resolution wheel or touchpad tens.  Without this a
    // single notch could be a several-fold jump, which is what "still too
    // fast" was.  Capped this way one event is at most 1.15x whatever sent it.
    double step = (dy > 1.0) ? 1.0 : (dy < -1.0) ? -1.0 : dy;
    xa_zoom(pow(1.15, step));
  }
  return TRUE;
}


static void act_zoom_in(GSimpleAction *a, GVariant *p, gpointer u)
{
  (void)a; (void)p; (void)u;
  xa_zoom(0.5);
}

static void act_zoom_out(GSimpleAction *a, GVariant *p, gpointer u)
{
  (void)a; (void)p; (void)u;
  xa_zoom(2.0);
}

static void act_redraw(GSimpleAction *a, GVariant *p, gpointer u)
{
  (void)a; (void)p; (void)u;
  render_soon();
}

// Stateful actions, so the menu shows a check mark and GTK keeps it in step
// with the setting rather than the two drifting apart.
static void act_toggle(GSimpleAction *a, GVariant *state, gpointer u)
{
  const char *name = g_action_get_name(G_ACTION(a));
  gboolean on = g_variant_get_boolean(state);

  (void)u;
  if (!g_strcmp0(name, "grid"))
  {
    long_lat_grid = on ? 1 : 0;
  }
  else if (!g_strcmp0(name, "map-labels"))
  {
    map_labels = on ? 1 : 0;
  }
  else if (!g_strcmp0(name, "filled-maps"))
  {
    map_color_levels = on ? 1 : 0;
  }
  g_simple_action_set_state(a, state);
  render_soon();
}

/*
 * Rebuild the map index.
 *
 * Needed after any map is added -- notably after tools/osm_import.sh writes new
 * shapefiles, which are invisible until the index knows their extents.
 */
static void act_reindex(GSimpleAction *a, GVariant *p, gpointer u)
{
  (void)a; (void)p; (void)u;
  map_indexer(0);
  xa_render();
}


// What Astir is connected to.  Its own window rather than a submenu of
// toggles: an interface has state worth watching, not just a setting.
static void act_interfaces(GSimpleAction *a, GVariant *p, gpointer u)
{
  (void)a; (void)p;
  xa_gtk4_interfaces_show(GTK_WINDOW(u));
}


/*
 * A click on the map: open whichever station is under it.
 *
 * The press position is remembered so the release can tell a click from a pan.
 * The same button does both, and a drag that happens to finish near a symbol
 * must not open it.  Judged here from the two positions rather than from a flag
 * the drag gesture sets, because both gestures see the same button release and
 * nothing promises which of them is told first.
 */
static double click_press_x, click_press_y;


/*
 * The toast's history, as a popover of recent messages.
 *
 * Entries naming a station are buttons onto that station; the rest are plain
 * text.  Newest at the top, because the reason for opening this is something
 * seen a moment ago and missed.
 */
static void on_history_row(GtkButton *b, gpointer win)
{
  const char *call = g_object_get_data(G_OBJECT(b), "callsign");
  GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(b), GTK_TYPE_POPOVER);

  if (pop != NULL)
  {
    gtk_popover_popdown(GTK_POPOVER(pop));
  }
  if (call != NULL)
  {
    xa_gtk4_station_show(GTK_WINDOW(win), call);
  }
}


static void on_toast_clicked(GtkButton *b, gpointer win)
{
  const char *text[24];
  const char *call[24];
  int n = xa_gtk4_station_history(text, call, 24);
  GtkWidget *pop, *box, *scroll;
  int i;

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  if (n == 0)
  {
    GtkWidget *l = gtk_label_new("Nothing recent.");

    gtk_widget_add_css_class(l, "dim-label");
    gtk_widget_set_margin_top(l, 12);
    gtk_widget_set_margin_bottom(l, 12);
    gtk_widget_set_margin_start(l, 12);
    gtk_widget_set_margin_end(l, 12);
    gtk_box_append(GTK_BOX(box), l);
  }

  for (i = 0; i < n; i++)
  {
    GtkWidget *row;

    if (call[i] != NULL)
    {
      GtkWidget *child;

      row = gtk_button_new_with_label(text[i]);
      gtk_button_set_has_frame(GTK_BUTTON(row), FALSE);
      // Copied: the history ring can be overwritten while this is open.
      g_object_set_data_full(G_OBJECT(row), "callsign", g_strdup(call[i]),
                             g_free);
      g_signal_connect(row, "clicked", G_CALLBACK(on_history_row), win);
      gtk_widget_set_tooltip_text(row, "Open this station");

      child = gtk_button_get_child(GTK_BUTTON(row));
      if (GTK_IS_LABEL(child))
      {
        gtk_label_set_xalign(GTK_LABEL(child), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(child), PANGO_ELLIPSIZE_END);
      }
    }
    else
    {
      row = gtk_label_new(text[i]);
      gtk_label_set_xalign(GTK_LABEL(row), 0.0);
      gtk_label_set_ellipsize(GTK_LABEL(row), PANGO_ELLIPSIZE_END);
      gtk_widget_add_css_class(row, "dim-label");
      gtk_widget_set_margin_start(row, 8);
      gtk_widget_set_margin_end(row, 8);
    }
    gtk_box_append(GTK_BOX(box), row);
  }

  scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
  gtk_widget_set_size_request(scroll, 340, (n > 8) ? 320 : -1);

  pop = gtk_popover_new();
  gtk_popover_set_child(GTK_POPOVER(pop), scroll);
  gtk_widget_set_parent(pop, GTK_WIDGET(b));
  gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_TOP);
  g_signal_connect(pop, "closed", G_CALLBACK(gtk_widget_unparent), NULL);
  gtk_popover_popup(GTK_POPOVER(pop));
}


static void on_map_press(GtkGestureClick *g, int n_press, double x, double y,
                         gpointer u)
{
  (void)g; (void)n_press; (void)u;
  click_press_x = x;
  click_press_y = y;
}

static void on_map_click(GtkGestureClick *g, int n_press, double x, double y,
                         gpointer win)
{
  DataRow *st;
  double dx = x - click_press_x;
  double dy = y - click_press_y;

  (void)g;
  (void)n_press;

  // A few pixels of slop: a hand resting on a trackpad moves a little between
  // press and release, and that is still a click.
  if ((dx * dx + dy * dy) > 25.0)
  {
    return;                      // that was a pan
  }

  // 18 px: a symbol is about 20 across and people aim at the middle of the
  // picture rather than at the point it is anchored to.
  st = station_at_screen_pos((long)x, (long)y, 18);
  if (st != NULL)
  {
    xa_gtk4_station_show(GTK_WINDOW(win), st->call_sign);
  }
}


static void act_about(GSimpleAction *a, GVariant *p, gpointer u)
{
  GtkWidget *dlg;

  (void)a; (void)p;
  dlg = gtk_about_dialog_new();
  gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), "Astir");
  gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dlg),
                                "GTK4 front end.\n"
                                "The same core as the Motif build, drawing "
                                "through xa_draw.h on Cairo and Pango.");
  gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dlg), GTK_LICENSE_GPL_2_0);
  gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(u));
  gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
  gtk_window_present(GTK_WINDOW(dlg));
}


/* ---- the xa_ui callbacks ----------------------------------------------- */

/*
 * What the core asks of a front end.  The Motif build answers these with
 * dialogs and a Motif text field; this answers the ones that mean something
 * without dialogs and leaves the rest NULL, which xa_ui.c turns into no-ops.
 * That the table can be filled in this partially, and the core neither knows
 * nor cares, is the property the whole extraction was for.
 */
/*
 * Progress and status, as a toast over the bottom right of the map.
 *
 * It used to be a label in the header bar, beside the menu button.  The text is
 * whatever the core last had to say -- "Indexing maps", a station callsign, a
 * tile count -- so its width changed constantly while maps loaded and stations
 * arrived, and everything packed beside it moved.  Including the menu button,
 * which slid out from under the pointer on its way to being clicked.
 *
 * A menu bar holds things whose position you learn.  Anything that changes size
 * on its own does not belong in one.  So this floats over the map instead, where
 * moving costs nothing.
 *
 * It does NOT time out.  It used to clear itself four seconds after the last
 * message, which meant a timer whose only job was to make information go away --
 * and nothing to wait for except the clock.  Leaving the last thing that
 * happened on screen until something else happens is both simpler and more
 * useful: the toast becomes a standing "most recent activity" rather than a
 * notification you had to be looking at.  Clicking it still opens the history.
 */
static void ui_status(const char *text)
{
  if (xa_status == NULL || text == NULL)
  {
    return;
  }

  if (text[0] == '\0')
  {
    gtk_widget_set_visible(xa_status, FALSE);
    return;
  }

  /*
   * Keep it, and note which station it was about.
   *
   * The core's status strings are not structured, so the callsign is recovered
   * by looking the first word up in the station list.  That is deliberately
   * conservative: a word that is not a known station yields no callsign and the
   * entry is simply not clickable, which is the right answer for "Indexing
   * maps".  Guessing harder would make history entries that open the wrong
   * station, and a wrong answer here is worse than none.
   */
  {
    char first[MAX_CALLSIGN + 1];
    const char *end = strpbrk(text, " \t");
    size_t len = (end != NULL) ? (size_t)(end - text) : strlen(text);
    DataRow *p = NULL;

    if (len > MAX_CALLSIGN) { len = MAX_CALLSIGN; }
    memcpy(first, text, len);
    first[len] = '\0';

    xa_gtk4_station_note(text,
                         (len > 0 && search_station_name(&p, first, 1) && p)
                           ? first : NULL);
  }

  gtk_label_set_text(GTK_LABEL(xa_status), text);
  gtk_widget_set_visible(xa_status, TRUE);

}

/*
 * Let the front end breathe during a long operation.
 *
 * NOT DURING A RENDER, which is what this used to do and is where it came from:
 * the core calls it every 64 shapes so that a pan can interrupt a slow redraw,
 * and the Motif build answered with HandlePendingEvents(app_context).  Xt
 * tolerated being re-entered like that.  GTK4 does not.
 *
 * Dispatching input from inside a render runs handlers while the frame they
 * would affect is half composed, and hands GTK's gesture recognisers and event
 * controllers a press whose release arrives in a different nesting level.  What
 * that looks like from the outside is a program that ignores clicks: a button or
 * a menu has to be hit several times in a row before one lands in a gap between
 * renders.  It is not GTK being slow, and it is not a busy main loop -- the
 * marker redraw measures 15 ms and fires about twice a second, nowhere near
 * enough to swallow a click.  It is this function.
 *
 * The interruption it bought is worth much less than it was.  A warm frame was
 * 941 ms when this was written and is 113 ms now, so a render that runs to
 * completion is not something anybody notices, while a lost click is.
 *
 * Long work that is NOT a render -- rebuilding the map index, a tile download --
 * still gets pumped, which is the other half of what this is for.
 */
static void ui_pump_events(void)
{
  if (rendering)
  {
    return;
  }
  while (g_main_context_pending(NULL))
  {
    g_main_context_iteration(NULL, FALSE);
  }
}

// Put the pointer back when the event loop next goes idle.
static gboolean ui_unbusy(gpointer unused)
{
  (void)unused;
  if (xa_area != NULL)
  {
    gtk_widget_set_cursor(xa_area, NULL);   // back to the inherited cursor
  }
  return G_SOURCE_REMOVE;
}


/*
 * Something slow started.
 *
 * xa_ui.h is explicit that there is no matching "not busy", because the Motif
 * shell cleared its cursor from an idle work procedure.  This front end set the
 * wait cursor and had nothing that ever cleared it, so the first slow thing in a
 * session -- resolving a hostname, starting an interface -- left the pointer
 * spinning for as long as Astir stayed open.
 *
 * So it does what Motif did.  The cursor is set now and cleared the next time
 * the main loop reaches idle, which is precisely "when the slow thing is no
 * longer holding us up" and needs nothing added to the core's side of the
 * contract.
 */
static void ui_busy(void)
{
  if (xa_area != NULL)
  {
    gtk_widget_set_cursor_from_name(xa_area, "wait");
    g_idle_add(ui_unbusy, NULL);
  }
}

static void ui_flush(void)
{
  if (xa_area != NULL)
  {
    gtk_widget_queue_draw(xa_area);
  }
}

static void ui_warn(const char *text)
{
  g_warning("%s", text ? text : "(null)");
}

static void ui_popup(const char *banner, const char *message)
{
  // No dialogs yet.  Said on stderr rather than swallowed, which is what a
  // build without popups has always done.
  g_message("%s: %s", banner ? banner : "", message ? message : "");
}

/*
 * Retry a dropped interface, once, some time after it drops.
 *
 * The only scheduled wake-up left in the program, and it is armed BY a failure
 * rather than running on a cadence: nothing is pending while every interface is
 * healthy.  It has to exist, because a dead network connection is the one thing
 * that cannot announce its own recovery -- with every port down no packets
 * arrive and no frames are drawn, so an event-driven program would otherwise
 * never look again.
 *
 * Five minutes, as check_ports() always used.  Retrying a refused server every
 * second is a denial of service aimed at whoever runs it.
 */
static guint reconnect_source;

static gboolean reconnect_once(gpointer unused)
{
  (void)unused;
  reconnect_source = 0;
  if (xa_ready)
  {
    check_ports();               // brings back anything that has dropped
  }
  return G_SOURCE_REMOVE;
}


// The core announces this from inside interface.c's locks, so the actual work
// is deferred rather than run here.
static void ui_interfaces_changed(void)
{
  int i;

  xa_gtk4_interfaces_changed();

  // Arm a retry if anything is down and wants to come back, and nothing is
  // already waiting to try.
  if (reconnect_source != 0)
  {
    return;
  }
  for (i = 0; i < MAX_IFACE_DEVICES; i++)
  {
    if (devices[i].device_type != DEVICE_NONE
        && devices[i].reconnect
        && port_data[i].status != DEVICE_UP)
    {
      reconnect_source = g_timeout_add_seconds(300, reconnect_once, NULL);
      return;
    }
  }
}


static void ui_redraw(void)
{
  xa_render();
  if (xa_area != NULL)
  {
    gtk_widget_queue_draw(xa_area);
  }
}

static void ui_free_label(void *label)
{
  g_free(label);
}

static void install_ui_callbacks(void)
{
  xa_ui_callbacks cb = { 0 };

  cb.status = ui_status;
  cb.pump_events = ui_pump_events;
  cb.busy = ui_busy;
  cb.flush = ui_flush;
  cb.warn = ui_warn;
  cb.popup = ui_popup;
  cb.popup_always = ui_popup;
  cb.redraw = ui_redraw;
  cb.free_label = ui_free_label;

  /*
   * Two notifications this front end was ignoring.
   *
   * Both have been in the contract all along, announced from the core, with
   * nothing registered to hear them -- so the windows that needed the
   * information polled for it instead, on timers, rebuilding themselves whether
   * anything had changed or not.  Registering them is what lets those timers go.
   */
  cb.interfaces_changed = ui_interfaces_changed;
  cb.station_changed = xa_gtk4_station_changed;
  xa_ui_set_callbacks(&cb);
}


/* ---- startup ----------------------------------------------------------- */

static int init_core(void)
{
  char base[400];
  struct passwd *pw;

  // Where ~/.astir lives.  get_user_base_dir() reads user_dir, which is core
  // state that nothing in the core fills in -- main.c did it, so a second front
  // end has to as well.  Without it every path comes out as "/.astir/...".
  pw = getpwuid(getuid());
  if (pw != NULL)
  {
    astir_snprintf(user_dir, sizeof(user_dir), "%s", pw->pw_dir);
  }

  // Before anything is read: whose data directory is this?  Getting this wrong
  // renders a convincing map out of another program's files.
  if (!xa_data_base_is_ours())
  {
    return 0;
  }

  // Build ~/.astir before anything reads out of it.  A first run has no
  // directory at all, and every failure after this point would otherwise be
  // reported as a missing file rather than as a missing install.
  if (!xa_user_dirs_create())
  {
    g_printerr("astir: cannot set up the user directory; giving up\n");
    return 0;
  }

  // Language first: almost everything else reports through langcode().  The
  // user's own language.sys wins if they have one; otherwise Astir's shipped
  // copy for the configured language is read straight out of the data
  // directory, with no symlink in between to go stale.
  if (!xa_resolve_config("language.sys", base, sizeof(base)))
  {
    char want[64];

    /*
     * The configured language is not known yet: langcode() has to work before
     * the config file can be reported on, so the language load comes first and
     * the setting arrives with load_data_or_default() below.  English is the
     * language every install ships, so it is what a first run reads; a user who
     * has chosen another gets it from the next launch onward.
     */
    astir_snprintf(want, sizeof(want), "language-%s.sys",
                   lang_to_use[0] != '\0' ? lang_to_use : "English");
    if (!xa_resolve_config(want, base, sizeof(base)))
    {
      g_printerr("astir: no language file found; is Astir installed?\n");
      return 0;
    }
  }
  if (!load_language_file(base))
  {
    g_printerr("could not load the language file\n");
    return 0;
  }

  // Astir's own map tracing, which is the fastest way to see why a map is not
  // drawn: bit 16 reports every name it reads and every one it skips.
  if (getenv("ASTIR_DEBUG") != NULL)
  {
    debug_level = atoi(getenv("ASTIR_DEBUG"));
  }

  xa_gtk4_load_palette();

  // Before any surface is created, because a surface is built at whatever the
  // scale was when it was made.  There is no widget yet, so this can only pick
  // up the environment override; the real monitor scale arrives with the first
  // resize, which rebuilds everything anyway.
  xa_gtk4_set_device_scale(wanted_device_scale(NULL));

  load_data_or_default();        // the config file, into the core's settings

  screen_width  = xa_w;
  screen_height = xa_h;

  // Pens.  Under X these were scarce server resources created once; here they
  // are structs, and they are still created once because the interface says so.
  gc         = xa_pen_create(XA_SURFACE_NONE);
  gc2        = xa_pen_create(XA_SURFACE_NONE);
  gc_tint    = xa_pen_create(XA_SURFACE_NONE);
  gc_stipple = xa_pen_create(XA_SURFACE_NONE);
  gc_bigfont = xa_pen_create(XA_SURFACE_NONE);

  pixmap         = xa_surface_create(xa_w, xa_h, XA_DEPTH_CANVAS);
  pixmap_alerts  = xa_surface_create(xa_w, xa_h, XA_DEPTH_CANVAS);
  pixmap_final   = xa_surface_create(xa_w, xa_h, XA_DEPTH_CANVAS);
  pixmap_markers = xa_surface_create(xa_w, xa_h, XA_DEPTH_ALPHA);
  pixmap_prev    = xa_surface_create(xa_w, xa_h, XA_DEPTH_CANVAS);
  if (pixmap == XA_SURFACE_NONE || pixmap_final == XA_SURFACE_NONE)
  {
    g_printerr("could not create the drawing surfaces\n");
    return 0;
  }

  /*
   * Build the map index if there is not one.
   *
   * map_indexer() has always been in the core; nothing in this front end ever
   * called it, so a GTK4-only install had no way to produce an index at all --
   * it silently inherited whatever the Motif build had left in ~/.astir.
   *
   * Without an index map_onscreen_index() culls every map as not visible and
   * load_maps() draws nothing, with no error anywhere, because "not visible"
   * is a normal answer.  A newly imported map is exactly the case that hits
   * this: the files are there, they are selected, and the map is blank.
   */
  {
    char idx[MAX_VALUE];
    struct stat st;

    get_user_base_dir(MAP_INDEX_DATA, idx, sizeof(idx));
    if (stat(idx, &st) != 0)
    {
      fprintf(stderr, "no map index at %s; building one\n", idx);
      map_indexer(0);
    }
  }

  /*
   * Clamp whatever the config asked for to the same limit.
   *
   * The limit was only applied when zooming, so a stored SCREEN_ZOOM past it
   * started the program showing a world too small to read -- 152470 puts 180
   * degrees of latitude into 212 pixels of a 700 pixel window.  The value came
   * from a config seeded from another program and there was nothing to catch
   * it, because zoom limits were enforced on the way out and not on the way
   * in.
   */
  {
    long max_out = xa_max_zoom_out();

    if (scale_y > max_out)
    {
      fprintf(stderr, "startup zoom %ld is further out than the world; "
              "using %ld\n", scale_y, max_out);
      scale_y = max_out;
      xa_rescale();
    }
  }

  // Let a scripted render pick the scale, so "are the maps culled by zoom?"
  // is one run rather than a guess.  scale_y is 1/100 second per pixel.
  if (getenv("ASTIR_GTK4_SCALE") != NULL)
  {
    scale_y = atol(getenv("ASTIR_GTK4_SCALE"));
    xa_rescale();
  }

  // The symbol icons.  Without this every station draws as a bare callsign,
  // because draw_symbol() has no pixmaps to blit -- which is exactly what it
  // did until this line existed.
  load_pixmap_symbol_file("symbols.dat", 0);

  init_station_data();

  // Packets, without an interface.  main.c does this from its event loop for
  // File > Open Log File; here it runs to completion before the first render,
  // which is what a one-shot render needs and is good enough for an
  // interactive run to start with something on screen.
  if (getenv("ASTIR_REPLAY") != NULL)
  {
    // A local, not main.c's read_file_ptr global: that pointer is the Motif
    // front end's own state, and only the `read_file` flag is core.
    FILE *replay = fopen(getenv("ASTIR_REPLAY"), "r");

    if (replay != NULL)
    {
      read_file = 1;
      while (read_file)
      {
        read_file_line(replay);
      }
      g_print("replayed %s\n", getenv("ASTIR_REPLAY"));
    }
  }
  index_restore_from_file();     // the map index

  /*
   * Every port starts with no socket.
   *
   * clear_all_port_data() sets each port's channel to -1, meaning "no
   * descriptor".  main.c called it; nothing did afterwards, so the whole table
   * kept the zero it got from being a global -- and zero is a perfectly good
   * file descriptor.
   *
   * net_init() reads "channel != -1" as "there is a socket here left over from
   * a previous attempt; shut it down before making a new one".  With every
   * channel at 0 that was true of every port before any of them had a socket at
   * all, so bringing up an interface closed file descriptor 0.  Once freed, 0
   * goes to the next socket created -- so a later interface would hand its own
   * live connection the number that an earlier one still believed was its own,
   * and closing "its" socket dropped somebody else's.
   *
   * It showed up as starting a GPS killing an APRS-IS connection three slots
   * away, in either order, at the exact instant the new port cleaned up.
   */
  clear_all_port_data();

  /*
   * Bring up whatever interfaces the config defines.
   *
   * startup_all_or_defined_port() has been in the core the whole time and was
   * called only from the Motif main(), so a GTK4 build with a perfectly good
   * interface in its config connected to nothing: the devices were loaded, the
   * list was right, and no thread was ever started.
   *
   * Devices marked "start on run" are the only ones this brings up; the rest
   * wait to be started from the interface window.
   */
  startup_all_or_defined_port(-1);

  return 1;
}


/*
 * A packet arrived.  Decode it.
 *
 * Called by GLib when the core's wakeup descriptor becomes readable, which
 * happens the moment an interface thread queues a packet -- so this runs once
 * per burst of traffic and at no other time.
 *
 * It used to be a fifty-millisecond timer asking whether anything had turned
 * up, which on a quiet channel is twenty questions a second with the same
 * answer.  None of that was necessary: the producer knows when it produces, and
 * now it says so.
 */
static gboolean on_packet_ready(gint fd, GIOCondition cond, gpointer user_data)
{
  (void)fd;
  (void)cond;
  (void)user_data;

  if (!xa_ready)
  {
    return G_SOURCE_CONTINUE;
  }

  // Cleared BEFORE draining, so a packet queued while we decode leaves the
  // descriptor readable and we are called again -- rather than going back to
  // sleep on a queue that is not empty.
  xa_incoming_drain_wakeup();

  if (xa_incoming_pump(0) > 0)
  {
    redraw_on_new_data = 2;      // 2 = "and reposition", as the core reads it
  }

  housekeep_now();               // expiry, on an event rather than a clock

  if (redraw_on_new_data > 0)
  {
    redraw_on_new_data = 0;
    // Markers only: a new station changes where the symbols are, not what the
    // map underneath them looks like, and the marker pass is 15 ms against the
    // map's hundred and more.
    xa_render_markers();
    gtk_widget_queue_draw(xa_area);
  }

  return G_SOURCE_CONTINUE;
}


/*
 * Expiry, driven by the things that were going to happen anyway.
 *
 * Removing old stations, old messages and expired alerts used to run from a
 * one-second timer.  Nothing announces the passage of time, so that looked
 * unavoidable -- but it is only needed when the answer could have CHANGED WHAT
 * IS ON SCREEN, and the two moments that can do that are a packet arriving and
 * a frame being drawn.  Both are already events, and both already call through
 * here.
 *
 * xa_housekeeping() rate-limits itself to once a second internally, so calling
 * it from a busy path costs a comparison.  On a channel with nothing on it
 * expiry stalls -- and correctly so: with no packets and no redraws, nothing is
 * being displayed that could be stale to anybody.  The moment either resumes,
 * the first thing that happens is the sweep.
 */
static void housekeep_now(void)
{
  if (!xa_ready)
  {
    return;
  }
  if (xa_housekeeping(sec_now()) && redraw_on_new_data > 0)
  {
    redraw_on_new_data = 0;
    xa_render_markers();
    gtk_widget_queue_draw(xa_area);
  }
}


static gboolean show_station_once(gpointer win)
{
  const char *call = getenv("ASTIR_GTK4_SHOW_STATION");

  // "*" means whichever station is first in the list, so a test does not have
  // to know a callsign that happens to be on the air right now.
  if (call != NULL && strcmp(call, "*") == 0)
  {
    call = (n_first != NULL) ? n_first->call_sign : NULL;
  }
  // Dump what the history holds, so the popover's contents can be checked
  // without a pointer.
  {
    const char *t[24];
    const char *c[24];
    int n = xa_gtk4_station_history(t, c, 24);
    int i;

    g_print("history holds %d entries:\n", n);
    for (i = 0; i < n; i++)
    {
      g_print("   [%s] %s\n", c[i] ? c[i] : "(no station)", t[i]);
    }
  }

  if (call != NULL && call[0] != '\0')
  {
    g_print("opening station window for %s\n", call);
    xa_gtk4_station_show(GTK_WINDOW(win), call);
  }
  else
  {
    g_print("no stations heard yet\n");
  }
  return G_SOURCE_REMOVE;
}


static gboolean show_interfaces_once(gpointer win)
{
  const char *what = getenv("ASTIR_GTK4_SHOW_INTERFACES");

  // "add" opens the dialog ALONE, so a screenshot of the active window gets
  // the dialog rather than the list sitting in front of it.
  if (what != NULL && strcmp(what, "add") == 0)
  {
    xa_gtk4_interfaces_show_add(GTK_WINDOW(win));
  }
  else
  {
    xa_gtk4_interfaces_show(GTK_WINDOW(win));
  }
  return G_SOURCE_REMOVE;
}


static gboolean popup_menu_once(gpointer button)
{
  gtk_menu_button_popup(GTK_MENU_BUTTON(button));
  return G_SOURCE_REMOVE;
}


/*
 * Write the config out on the way down.
 *
 * save_data() has been sitting in the core unused: the Motif main() called it
 * from its shutdown path, its signal handler and a menu item, and none of that
 * came across.  So Astir has been reading a config it never wrote -- a new user
 * ended up with an empty astir.cnf and lost their position, their zoom and
 * every preference on every exit, which looked like the settings not working
 * rather than like the file not being saved.
 *
 * Only when the core actually came up.  Saving from a failed startup would
 * write defaults over a config that was probably fine.
 */
static void on_shutdown(GApplication *app, gpointer user_data)
{
  (void)app;
  (void)user_data;

  if (!xa_ready)
  {
    return;
  }
  save_data();
  xa_perf_report_totals();       // ASTIR_PERF=1; nothing otherwise
}


// SIGINT/SIGTERM: ask the application to quit rather than dying here, so the
// shutdown handler above still runs.  Returning G_SOURCE_REMOVE means a second
// interrupt is not caught, which is the behaviour you want if the first one
// wedges.
static gboolean quit_on_signal(gpointer app)
{
  g_application_quit(G_APPLICATION(app));
  return G_SOURCE_REMOVE;
}


static void on_activate(GtkApplication *app, gpointer user_data)
{
  GtkWidget *win, *header, *box;
  GtkGesture *drag;
  GtkEventController *scroll;
  GMenu *menu, *view, *maps, *iface, *help;
  GtkWidget *hamburger;
  static const GActionEntry acts[] =
  {
    { "zoom-in",     act_zoom_in,  NULL, NULL,    NULL, {0} },
    { "zoom-out",    act_zoom_out, NULL, NULL,    NULL, {0} },
    { "redraw",      act_redraw,   NULL, NULL,    NULL, {0} },
    { "about",       act_about,    NULL, NULL,    NULL, {0} },
    { "reindex",     act_reindex,  NULL, NULL,    NULL, {0} },
    { "interfaces",  act_interfaces, NULL, NULL,  NULL, {0} },
    { "grid",        NULL, NULL, "false", act_toggle, {0} },
    { "map-labels",  NULL, NULL, "false", act_toggle, {0} },
    { "filled-maps", NULL, NULL, "false", act_toggle, {0} },
  };

  (void)user_data;

  win = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "Astir");
  gtk_window_set_default_size(GTK_WINDOW(win), xa_w, xa_h);

  header = gtk_header_bar_new();
  {
    GtkWidget *zi = gtk_button_new_from_icon_name("zoom-in-symbolic");
    GtkWidget *zo = gtk_button_new_from_icon_name("zoom-out-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(zi), "win.zoom-in");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(zo), "win.zoom-out");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), zi);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), zo);
  }
  // A GMenu rather than a menu bar: one model, described once, and GTK builds
  // the popover from it.  The Motif build spends several hundred lines on
  // XmCreatePulldownMenu calls to say less than this.
  menu = g_menu_new();

  view = g_menu_new();
  g_menu_append(view, "Zoom In", "win.zoom-in");
  g_menu_append(view, "Zoom Out", "win.zoom-out");
  g_menu_append(view, "Redraw", "win.redraw");
  g_menu_append_section(menu, "View", G_MENU_MODEL(view));
  g_object_unref(view);

  maps = g_menu_new();
  g_menu_append(maps, "Lat/Long Grid", "win.grid");
  g_menu_append(maps, "Map Labels", "win.map-labels");
  g_menu_append(maps, "Filled Maps", "win.filled-maps");
  // Was appended after this section had been added and unreffed, from inside
  // the help block below.  It survived on the section holding a reference,
  // which is luck rather than intent.
  g_menu_append(maps, "Rebuild Map Index", "win.reindex");
  g_menu_append_section(menu, "Maps", G_MENU_MODEL(maps));
  g_object_unref(maps);

  // What Astir is connected to: radios, software TNCs, APRS-IS.
  iface = g_menu_new();
  g_menu_append(iface, "Interfaces\xe2\x80\xa6", "win.interfaces");
  g_menu_append_section(menu, "Connections", G_MENU_MODEL(iface));
  g_object_unref(iface);

  help = g_menu_new();
  g_menu_append(help, "About Astir", "win.about");
  g_menu_append_section(menu, NULL, G_MENU_MODEL(help));
  g_object_unref(help);

  hamburger = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(hamburger), "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(hamburger), G_MENU_MODEL(menu));
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header), hamburger);
  g_object_unref(menu);

  gtk_window_set_titlebar(GTK_WINDOW(win), header);

  xa_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(xa_area, TRUE);
  gtk_widget_set_vexpand(xa_area, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(xa_area), xa_draw_cb,
                                 NULL, NULL);
  g_signal_connect(xa_area, "resize", G_CALLBACK(xa_resized), NULL);
  g_signal_connect(xa_area, "notify::scale-factor",
                   G_CALLBACK(xa_scale_changed), NULL);

  /*
   * A click on the map opens the station under it.
   *
   * Attached before the drag gesture so a plain click is seen as a click.  The
   * radius is generous: a symbol is about 20 px across and people aim at the
   * middle of a picture, not at its anchor point.
   */
  {
    GtkGesture *tap = gtk_gesture_click_new();

    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tap), GDK_BUTTON_PRIMARY);
    g_signal_connect(tap, "pressed", G_CALLBACK(on_map_press), NULL);
    g_signal_connect(tap, "released", G_CALLBACK(on_map_click), win);
    gtk_widget_add_controller(xa_area, GTK_EVENT_CONTROLLER(tap));
  }

  drag = gtk_gesture_drag_new();
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
  g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
  g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), NULL);
  gtk_widget_add_controller(xa_area, GTK_EVENT_CONTROLLER(drag));

  scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), NULL);
  gtk_widget_add_controller(xa_area, scroll);

  g_action_map_add_action_entries(G_ACTION_MAP(win), acts,
                                  G_N_ELEMENTS(acts), win);

  // The toggles start wherever the config left them, so the menu reflects the
  // real setting on the first open rather than after the first click.
  g_action_change_state(g_action_map_lookup_action(G_ACTION_MAP(win), "grid"),
                        g_variant_new_boolean(long_lat_grid != 0));
  g_action_change_state(g_action_map_lookup_action(G_ACTION_MAP(win), "map-labels"),
                        g_variant_new_boolean(map_labels != 0));
  g_action_change_state(g_action_map_lookup_action(G_ACTION_MAP(win), "filled-maps"),
                        g_variant_new_boolean(map_color_levels != 0));
  gtk_application_set_accels_for_action(app, "win.zoom-in",
                                        (const char *[]){ "plus", "equal", NULL });
  gtk_application_set_accels_for_action(app, "win.zoom-out",
                                        (const char *[]){ "minus", NULL });
  gtk_application_set_accels_for_action(app, "win.redraw",
                                        (const char *[]){ "F5", NULL });
  gtk_application_set_accels_for_action(app, "win.interfaces",
                                        (const char *[]){ "<Control>i", NULL });

  // Same reason as the menu hook below: this is a Wayland session with no input
  // automation, so a window reached through a menu cannot be got on screen for
  // a screenshot unless the application opens it itself.
  if (getenv("ASTIR_GTK4_SHOW_INTERFACES") != NULL)
  {
    g_timeout_add_seconds(2, (GSourceFunc)show_interfaces_once, win);
  }

  // ASTIR_GTK4_SHOW_STATION=CALL opens that station's window, so what a click
  // does can be checked without a pointer.  Late, so traffic has arrived first.
  if (getenv("ASTIR_GTK4_SHOW_STATION") != NULL)
  {
    g_timeout_add_seconds(20, (GSourceFunc)show_station_once, win);
  }

  /*
   * The map, with the status toast floating over its bottom right corner.
   *
   * An overlay rather than another row in the box: a row would take height from
   * the map and shift it every time the toast appeared, which is the same fault
   * as the header bar had, moved to a different edge.  Over the map it costs the
   * map nothing and moves nothing.
   */
  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  {
    GtkWidget *overlay = gtk_overlay_new();
    GtkCssProvider *css = gtk_css_provider_new();

    gtk_overlay_set_child(GTK_OVERLAY(overlay), xa_area);

    xa_status = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(xa_status), PANGO_ELLIPSIZE_END);
    /*
     * A FIXED width, not one that follows the text.
     *
     * Same fault as the header bar had, one step removed: the toast is
     * right-aligned, so a message of a different length moves its left edge --
     * and the history popover, which hangs off the middle of it, slid back and
     * forth as the text changed underneath.  A thing other things attach to
     * must not resize itself.
     *
     * Both bounds set to the same number, so short text pads rather than
     * shrinks and long text ellipsizes rather than grows.
     */
    gtk_label_set_width_chars(GTK_LABEL(xa_status), 42);
    gtk_label_set_max_width_chars(GTK_LABEL(xa_status), 42);
    gtk_label_set_xalign(GTK_LABEL(xa_status), 0.0);
    gtk_widget_set_visible(xa_status, FALSE);
    gtk_widget_add_css_class(xa_status, "astir-toast");

    // Readable over any map underneath it, which is why it is not simply the
    // theme's own background colour.
    gtk_css_provider_load_from_string(css,
      ".astir-toast {"
      "  background-color: rgba(0,0,0,0.72);"
      "  color: #ffffff;"
      "  border-radius: 8px;"
      "  padding: 6px 12px;"
      "  font-size: 0.9em;"
      "}");
    gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /*
     * The toast is a button onto its own history.
     *
     * A message shows for four seconds and goes.  That is right for a progress
     * line and wrong for "a station was heard" -- by the time you look up, the
     * thing worth clicking has gone.  Clicking the toast reopens the last few,
     * and any that names a station opens it.
     */
    {
      GtkWidget *toast_btn = gtk_button_new();
      GtkGesture *unused_g = NULL;

      (void)unused_g;
      gtk_button_set_child(GTK_BUTTON(toast_btn), xa_status);
      gtk_widget_set_halign(toast_btn, GTK_ALIGN_END);
      gtk_widget_set_valign(toast_btn, GTK_ALIGN_END);
      gtk_widget_set_margin_end(toast_btn, 12);
      gtk_widget_set_margin_bottom(toast_btn, 40);
      gtk_widget_add_css_class(toast_btn, "flat");
      gtk_widget_set_tooltip_text(toast_btn, "Recent activity");
      g_signal_connect(toast_btn, "clicked", G_CALLBACK(on_toast_clicked), win);

      // The label's own visibility is what ui_status() toggles; the button
      // follows it, so an empty status leaves nothing on screen to press.
      g_object_bind_property(xa_status, "visible", toast_btn, "visible",
                             G_BINDING_SYNC_CREATE);

      gtk_overlay_add_overlay(GTK_OVERLAY(overlay), toast_btn);
    }
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_box_append(GTK_BOX(box), overlay);
  }
  gtk_window_set_child(GTK_WINDOW(win), box);

  rebuild_surfaces(xa_area, xa_w, xa_h);
  gtk_window_present(GTK_WINDOW(win));

  // Open the menu shortly after startup, for screenshots and UI tests.  There
  // is no input automation on this Wayland session, so a popover cannot
  // otherwise be captured at all.
  if (getenv("ASTIR_GTK4_SHOW_MENU") != NULL)
  {
    g_timeout_add_seconds(2, (GSourceFunc)popup_menu_once, hamburger);
  }
}


int main(int argc, char **argv, char **envp)
{
  GtkApplication *app;
  int status;

  /*
   * The core keeps its own copy of the command line, and something has to give
   * it one.
   *
   * Only one thing reads it: forked_getaddrinfo() forks a child to resolve a
   * hostname, and the child renames itself by overwriting argv in place so a
   * process listing shows "hostname lookup" rather than a second copy of
   * Astir.  With these left as the zero they were initialised to, that child
   * dereferences a null argv and dies -- and a resolver child that dies is
   * indistinguishable from a name that would not resolve.
   *
   * The visible symptom was that no network interface could ever connect: the
   * address lookup for a host as ordinary as "localhost" failed, and the
   * interface reported a hard failure with nothing to say why.  main.c set
   * these; nothing did after the front end changed.
   */
  my_argc = argc;
  my_argv = argv;
  my_envp = envp;

  // The third-party libraries the map drivers use need their own startup, and
  // it is main.c that has always done it.  Missing InitializeMagick() shows up
  // as an assertion inside GraphicsMagick the first time a raster map loads,
  // which is a long way from the cause.
#ifdef HAVE_LIBCURL
  curl_global_init(CURL_GLOBAL_ALL);
#endif
#ifdef HAVE_GRAPHICSMAGICK
  InitializeMagick(*argv);
#else
  #ifdef HAVE_IMAGEMAGICK
  MagickCoreGenesis(*argv, MagickTrue);
  #endif
#endif

  install_ui_callbacks();
  if (!init_core())
  {
    return 1;
  }
  xa_ready = 1;

  // A one-shot render mode, so the front end can be tested without a human
  // looking at it: draw one frame, write it out, exit.  The same reason
  // ASTIR_REPLAY exists on the Motif side.
  if (getenv("ASTIR_GTK4_RENDER_TO") != NULL)
  {
    const char *path = getenv("ASTIR_GTK4_RENDER_TO");
    cairo_surface_t *s;

    xa_gtk4_set_canvas(NULL, xa_w, xa_h);
    g_print("centre %ld,%ld  scale %ld/%ld  maps selected from %s\n",
            center_longitude, center_latitude, scale_x, scale_y,
            SELECTED_MAP_DATA);

    /*
     * Optionally listen before rendering, so a received station can be checked
     * without a human watching the window.
     *
     * The interfaces are already up -- init_core() started them -- but the tick
     * that drains their queue belongs to the main loop, which this path never
     * reaches.  Without this, a headless run connects to a radio, fills the
     * queue and renders an empty map, which would look exactly like a receive
     * path that does not work.
     */
    {
      const char *rx = getenv("ASTIR_GTK4_RX_SECONDS");
      int secs = (rx != NULL) ? atoi(rx) : 0;
      int got = 0;
      int i;

      /*
       * Optionally press "Start" on one interface partway through, and report
       * every interface's status once a second.
       *
       * ASTIR_GTK4_START_PORT="2@10" starts port 2 after ten seconds.  This
       * exists because the bug it was written for -- starting one interface
       * knocking another one over -- only happens when a SECOND interface is
       * brought up while a first is running, and that is a sequence no
       * one-shot render could produce.  Reasoning about it instead of
       * reproducing it got the wrong answer twice.
       */
      const char *sp = getenv("ASTIR_GTK4_START_PORT");
      int start_port = -1, start_at = 0;
      int last_report = -1;

      if (sp != NULL)
      {
        const char *at = strchr(sp, '@');

        start_port = atoi(sp);
        start_at = (at != NULL) ? atoi(at + 1) : 0;
      }

      for (i = 0; i < secs * 20; i++)       // 50 ms, as the real tick uses
      {
        int now_s = i / 20;

        if (start_port >= 0 && now_s == start_at && (i % 20) == 0)
        {
          g_print("--- starting port %d now ---\n", start_port);
          startup_all_or_defined_port(start_port);
        }

        if (getenv("ASTIR_IFACE_TRACE") != NULL && now_s != last_report
            && (i % 20) == 0)
        {
          int p;

          last_report = now_s;
          g_print("t=%2ds ", now_s);
          for (p = 0; p < MAX_IFACE_DEVICES; p++)
          {
            if (devices[p].device_type != DEVICE_NONE)
            {
              int st = get_device_status(p);

              // The file descriptor matters as much as the status: two ports
              // showing the same one is the whole bug.
              g_print(" [%d:%s fd=%d]", p,
                      st == DEVICE_UP ? "up"
                      : st == DEVICE_ERROR ? "ERR" : "down",
                      port_data[p].channel);
            }
          }
          g_print("  packets=%d\n", got);
        }

        got += xa_incoming_pump(0);
        xa_housekeeping(sec_now());
        g_usleep(50000);
      }
      if (secs > 0)
      {
        g_print("listened %d s, %d packets decoded\n", secs, got);
      }
    }

    /*
     * Render more than once when asked.
     *
     * The first frame pays every one-time cost -- building the shapefile
     * R-tree indexes, compiling the dbfawk rules, filling the tile cache --
     * and this harness rendered exactly one frame and exited, so every number
     * it ever produced was a COLD frame.  Optimising against those numbers
     * means optimising startup while the thing people complain about is
     * panning, which is always warm.
     */
    {
      const char *nf = getenv("ASTIR_GTK4_RENDER_FRAMES");
      int frames = (nf != NULL) ? atoi(nf) : 1;
      int f;

      /*
       * Optionally pan between frames, so the surface-reuse path is exercised
       * headlessly.  Without this the harness renders the same view twice and
       * never takes the branch that a real drag takes every time.
       */
      const char *pan = getenv("ASTIR_GTK4_PAN_BETWEEN");
      long pan_px = (pan != NULL) ? atol(pan) : 0;

      if (frames < 1) { frames = 1; }
      for (f = 0; f < frames; f++)
      {
        if (f > 0 && pan_px != 0)
        {
          center_longitude += pan_px * scale_x;
          xa_rescale();
        }
        if (frames > 1)
        {
          g_print("--- frame %d of %d (%s) ---\n", f + 1, frames,
                  f == 0 ? "cold" : "warm");
        }
        xa_render();
      }
    }
    s = xa_gtk4_canvas_surface();

    /*
     * Composite the layers the widget would have composited.
     *
     * Everything above the map -- the markers, the chrome -- lives on its own
     * surface and is put together by the draw callback, and headless there is
     * no widget and no draw callback.  Without this the scripted render, which
     * is the only thing that can check any of it, shows the map alone.
     *
     * That has now happened twice in this session: once when the attribution
     * moved off the canvas and once when the markers did.  Both times the gate
     * went blind in the same commit that added the feature.
     */
    if (s != NULL)
    {
      cairo_t *cr = cairo_create(s);

      if (pixmap_markers != XA_SURFACE_NONE)
      {
        cairo_surface_t *m = xa_gtk4_surface_of(pixmap_markers);

        if (m != NULL)
        {
          cairo_set_source_surface(cr, m, 0, 0);
          cairo_paint(cr);
        }
      }
      draw_attribution(cr, xa_w, xa_h);
      cairo_destroy(cr);
    }

    /*
     * Optionally write each layer on its own.
     *
     * The composite cannot answer the question the layer split exists for.
     * "Did the markers scale, or move?" and "is that symbol still the size it
     * was drawn at?" are both invisible once the layers are flattened onto a
     * busy map -- a marker that is wrong just looks like map detail.  The
     * marker layer alone, on transparency, makes the POLICY testable rather
     * than only the result.
     */
    if (getenv("ASTIR_GTK4_RENDER_LAYERS") != NULL
        && pixmap_markers != XA_SURFACE_NONE)
    {
      cairo_surface_t *m = xa_gtk4_surface_of(pixmap_markers);
      char buf[512];

      g_snprintf(buf, sizeof(buf), "%s-markers.png",
                 getenv("ASTIR_GTK4_RENDER_LAYERS"));
      if (m != NULL
          && cairo_surface_write_to_png(m, buf) == CAIRO_STATUS_SUCCESS)
      {
        g_print("wrote %s\n", buf);
      }
    }

    if (s == NULL
        || cairo_surface_write_to_png(s, path) != CAIRO_STATUS_SUCCESS)
    {
      g_printerr("could not write %s\n", path);
      return 1;
    }
    g_print("wrote %s\n", path);
    xa_perf_report_totals();     // ASTIR_PERF=1 to see what was actually drawn
    return 0;
  }

  app = gtk_application_new("org.astir.Gtk4", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);

  /*
   * Wake on packets, not on a clock.
   *
   * Registered here rather than in on_activate() so it covers the whole run,
   * and outside the headless render path above, which returns before reaching
   * this.  There is deliberately no fallback timer if the pipe cannot be made:
   * silently degrading to polling is how a program ends up polling with nobody
   * aware of it, so it says so instead.
   */
  {
    int fd = xa_incoming_wakeup_fd();

    if (fd >= 0)
    {
      g_unix_fd_add(fd, G_IO_IN, on_packet_ready, NULL);
    }
    else
    {
      g_printerr("astir: no wakeup pipe; incoming packets will not be seen\n");
    }
  }


  /*
   * Quit cleanly on an interrupt so the config is still written.
   *
   * Not a nicety: a program run from a terminal during development is stopped
   * with Ctrl-C far more often than it is closed with the window button, and
   * the default disposition kills the process outright.  Routing both signals
   * through g_application_quit() means every way of stopping Astir ends at the
   * same shutdown handler.
   */
  g_unix_signal_add(SIGINT, quit_on_signal, app);
  g_unix_signal_add(SIGTERM, quit_on_signal, app);

  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
