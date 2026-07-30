/*
 * xa_gtk4_main.c -- a GTK4 front end for Xastir.
 *
 * WHAT THIS IS
 *
 * A real application: it initialises the core, loads the map index, renders
 * maps through xa_draw_gtk4.c, and shows them in a window you can pan and zoom.
 * It is the first thing in this tree to draw an Xastir map without Motif.
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "xastir.h"
#include "xa_draw.h"
#include "xa_state.h"
#include "xa_settings.h"
#include "xa_config.h"
#include "xa_ui.h"
#include "maps.h"
#include "lang.h"
#include "util.h"
#include "db_funcs.h"
#include "station_draw.h"
#include "draw_symbols.h"
#include "snprintf.h"
#include "xa_perf.h"

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

#include <pwd.h>
#include <unistd.h>

// The backend's private entry points.  Declared here rather than in a header
// because exactly one front end ever calls them, and which backend is in use
// is a build-time choice.
void             xa_gtk4_set_canvas(GtkWidget *canvas, int width, int height);
cairo_surface_t *xa_gtk4_canvas_surface(void);
int              xa_gtk4_set_device_scale(int scale);
int              xa_gtk4_device_scale(void);

// From xa_gtk4_palette.c, generated from main.c's colour table.
void xa_gtk4_load_palette(void);

static GtkWidget *xa_area = NULL;      // the map canvas
static GtkWidget *xa_status = NULL;    // the status line in the header bar
static int xa_w = 1024, xa_h = 700;
static int xa_ready = 0;               // core initialised, safe to render


/* ---- rendering --------------------------------------------------------- */

/*
 * Compose a frame.
 *
 * The same sequence main.c's create_image() runs, minus the parts that need a
 * front end this does not have yet (weather alerts, station symbols -- both
 * live behind db_gui.c).  Every call in it is an xa_draw one, which is why it
 * fits in thirty lines here and took a thousand there.
 */
static void xa_render(void)
{
  if (!xa_ready || pixmap == XA_SURFACE_NONE)
  {
    return;
  }

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
  convert_from_xastir_coordinates(&f_NW_corner_longitude, &f_NW_corner_latitude,
                                  NW_corner_longitude, NW_corner_latitude);
  convert_from_xastir_coordinates(&f_SE_corner_longitude, &f_SE_corner_latitude,
                                  SE_corner_longitude, SE_corner_latitude);

  xa_pen_color(gc, colors[0xfd]);      // map background
  xa_pen_bg(gc, colors[0xfd]);
  xa_fill_rect(pixmap, gc, 0, 0, (int)screen_width, (int)screen_height);

  load_maps();

  xa_copy_area(pixmap, pixmap_alerts, gc, 0, 0,
               (int)screen_width, (int)screen_height, 0, 0);
  xa_copy_area(pixmap_alerts, pixmap_final, gc, 0, 0,
               (int)screen_width, (int)screen_height, 0, 0);

  if (long_lat_grid)
  {
    draw_grid();
  }

  // Stations, symbols, trails and the range rings, onto pixmap_final over the
  // map.  display_file() used to live in db_gui.c and take a Widget it never
  // touched, which is the only reason a second front end could not call it.
  display_file();

  xa_present_full(pixmap_final);
  xa_perf_frame_end("gtk4_render");
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
static guint render_timer;

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

static int rendering;
static void render_soon(void);   // defined below; render_now() re-arms itself

static gboolean render_now(gpointer u)
{
  double dx0, dy0, s0;

  (void)u;

  // Cleared FIRST, and unconditionally.  Returning early with it still set
  // leaves render_soon() believing a render is pending forever, so nothing
  // ever renders again -- one skipped frame stops the map permanently.
  render_timer = 0;

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

  rendering = 1;
  xa_render();
  rendering = 0;

  view_dx -= dx0;
  view_dy -= dy0;
  if (s0 != 0.0)
  {
    view_scale /= s0;
  }
  if (getenv("XASTIR_GTK4_TRACE_ZOOM"))
  {
    g_print("[render] scale_y=%ld view_scale=%.3f\n", scale_y, view_scale);
  }
  gtk_widget_queue_draw(xa_area);
  return G_SOURCE_REMOVE;
}

static void render_soon(void)
{
  if (render_timer == 0)
  {
    // Long enough to swallow a scroll burst, short enough not to feel laggy.
    render_timer = g_timeout_add(150, render_now, NULL);
  }
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
}


/*
 * How many device pixels the toolkit gives us per logical pixel.
 *
 * The widget knows, once it is on a monitor; before that, and in the headless
 * render, there is no widget to ask.  XASTIR_GTK4_DEVICE_SCALE overrides both,
 * which is the only way to render a HiDPI frame without a HiDPI monitor and so
 * the only way to gate this from a script.
 */
static int wanted_device_scale(GtkWidget *w)
{
  const char *env = getenv("XASTIR_GTK4_DEVICE_SCALE");
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
  xa_gtk4_set_canvas(area, width, height);
  pixmap        = xa_surface_create(width, height, XA_DEPTH_CANVAS);
  pixmap_alerts = xa_surface_create(width, height, XA_DEPTH_CANVAS);
  pixmap_final  = xa_surface_create(width, height, XA_DEPTH_CANVAS);
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
 * Xastir's map position is a centre and a scale in 1/100 second per pixel, and
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
 * Xastir unit, and the ratio wanted is sc_y/sc_x.  Below 50000 that produced a
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




static void xa_zoom(double factor)
{
  long s = (long)(scale_y * factor);
  long max_out;

  /*
   * Stop zooming out once the whole world already fits.
   *
   * Xastir's stored limit is 500000, which is not a view limit -- it is just
   * the largest value the config will hold.  Past the point where 180 degrees
   * of latitude spans the window there is nothing further to reveal, only a
   * shrinking earth in a growing field of background, which is what "too far
   * out to see anything" is.
   *
   * 32400000 Xastir units is 180 degrees, so the whole world fits vertically
   * at exactly this scale.  Derived from the window height rather than fixed,
   * because a taller window can show the same world at a smaller scale.
   */
  max_out = (screen_height > 0) ? (32400000L / screen_height) : 500000L;
  if (max_out > 500000L)
  {
    max_out = 500000L;      // never exceed what the config can store
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
    return;                        // already at the limit; do not queue work
  }
  // The frame on screen was drawn at the old scale, so showing it that much
  // larger or smaller is exactly right until the new one is ready.
  view_scale *= (double)scale_y / (double)s;
  if (getenv("XASTIR_GTK4_TRACE_ZOOM"))
  {
    g_print("[zoom]   scale_y %ld -> %ld  view_scale=%.3f\n",
            scale_y, s, view_scale);
  }
  scale_y = s;
  xa_rescale();
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

static void act_about(GSimpleAction *a, GVariant *p, gpointer u)
{
  GtkWidget *dlg;

  (void)a; (void)p;
  dlg = gtk_about_dialog_new();
  gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), "Xastir");
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
static void ui_status(const char *text)
{
  if (xa_status != NULL && text != NULL)
  {
    gtk_label_set_text(GTK_LABEL(xa_status), text);
  }
}

static void ui_pump_events(void)
{
  // The core calls this every 64 shapes so a pan can interrupt a slow redraw.
  while (g_main_context_pending(NULL))
  {
    g_main_context_iteration(NULL, FALSE);
  }
}

static void ui_busy(void)
{
  if (xa_area != NULL)
  {
    gtk_widget_set_cursor_from_name(xa_area, "wait");
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
  xa_ui_set_callbacks(&cb);
}


/* ---- startup ----------------------------------------------------------- */

static int init_core(void)
{
  char base[400];
  struct passwd *pw;

  // Where ~/.xastir lives.  get_user_base_dir() reads user_dir, which is core
  // state that nothing in the core fills in -- main.c did it, so a second front
  // end has to as well.  Without it every path comes out as "/.xastir/...".
  pw = getpwuid(getuid());
  if (pw != NULL)
  {
    xastir_snprintf(user_dir, sizeof(user_dir), "%s", pw->pw_dir);
  }

  // Language first: almost everything else reports through langcode().
  if (!load_language_file(get_user_base_dir("config/language.sys", base,
                                            sizeof(base))))
  {
    g_printerr("could not load the language file\n");
    return 0;
  }

  // Xastir's own map tracing, which is the fastest way to see why a map is not
  // drawn: bit 16 reports every name it reads and every one it skips.
  if (getenv("XASTIR_DEBUG") != NULL)
  {
    debug_level = atoi(getenv("XASTIR_DEBUG"));
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

  pixmap        = xa_surface_create(xa_w, xa_h, XA_DEPTH_CANVAS);
  pixmap_alerts = xa_surface_create(xa_w, xa_h, XA_DEPTH_CANVAS);
  pixmap_final  = xa_surface_create(xa_w, xa_h, XA_DEPTH_CANVAS);
  if (pixmap == XA_SURFACE_NONE || pixmap_final == XA_SURFACE_NONE)
  {
    g_printerr("could not create the drawing surfaces\n");
    return 0;
  }

  // Let a scripted render pick the scale, so "are the maps culled by zoom?"
  // is one run rather than a guess.  scale_y is 1/100 second per pixel.
  if (getenv("XASTIR_GTK4_SCALE") != NULL)
  {
    scale_y = atol(getenv("XASTIR_GTK4_SCALE"));
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
  if (getenv("XASTIR_REPLAY") != NULL)
  {
    // A local, not main.c's read_file_ptr global: that pointer is the Motif
    // front end's own state, and only the `read_file` flag is core.
    FILE *replay = fopen(getenv("XASTIR_REPLAY"), "r");

    if (replay != NULL)
    {
      read_file = 1;
      while (read_file)
      {
        read_file_line(replay);
      }
      g_print("replayed %s\n", getenv("XASTIR_REPLAY"));
    }
  }
  index_restore_from_file();     // the map index

  return 1;
}


static gboolean popup_menu_once(gpointer button)
{
  gtk_menu_button_popup(GTK_MENU_BUTTON(button));
  return G_SOURCE_REMOVE;
}


static void on_activate(GtkApplication *app, gpointer user_data)
{
  GtkWidget *win, *header, *box;
  GtkGesture *drag;
  GtkEventController *scroll;
  GMenu *menu, *view, *maps, *help;
  GtkWidget *hamburger;
  static const GActionEntry acts[] =
  {
    { "zoom-in",     act_zoom_in,  NULL, NULL,    NULL, {0} },
    { "zoom-out",    act_zoom_out, NULL, NULL,    NULL, {0} },
    { "redraw",      act_redraw,   NULL, NULL,    NULL, {0} },
    { "about",       act_about,    NULL, NULL,    NULL, {0} },
    { "grid",        NULL, NULL, "false", act_toggle, {0} },
    { "map-labels",  NULL, NULL, "false", act_toggle, {0} },
    { "filled-maps", NULL, NULL, "false", act_toggle, {0} },
  };

  (void)user_data;

  win = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "Xastir");
  gtk_window_set_default_size(GTK_WINDOW(win), xa_w, xa_h);

  header = gtk_header_bar_new();
  xa_status = gtk_label_new("");
  gtk_label_set_ellipsize(GTK_LABEL(xa_status), PANGO_ELLIPSIZE_END);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header), xa_status);
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
  g_menu_append_section(menu, "Maps", G_MENU_MODEL(maps));
  g_object_unref(maps);

  help = g_menu_new();
  g_menu_append(help, "About Xastir", "win.about");
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

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(box), xa_area);
  gtk_window_set_child(GTK_WINDOW(win), box);

  rebuild_surfaces(xa_area, xa_w, xa_h);
  gtk_window_present(GTK_WINDOW(win));

  // Open the menu shortly after startup, for screenshots and UI tests.  There
  // is no input automation on this Wayland session, so a popover cannot
  // otherwise be captured at all.
  if (getenv("XASTIR_GTK4_SHOW_MENU") != NULL)
  {
    g_timeout_add_seconds(2, (GSourceFunc)popup_menu_once, hamburger);
  }
}


int main(int argc, char **argv)
{
  GtkApplication *app;
  int status;

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
  // XASTIR_REPLAY exists on the Motif side.
  if (getenv("XASTIR_GTK4_RENDER_TO") != NULL)
  {
    const char *path = getenv("XASTIR_GTK4_RENDER_TO");
    cairo_surface_t *s;

    xa_gtk4_set_canvas(NULL, xa_w, xa_h);
    g_print("centre %ld,%ld  scale %ld/%ld  maps selected from %s\n",
            center_longitude, center_latitude, scale_x, scale_y,
            SELECTED_MAP_DATA);
    xa_render();
    s = xa_gtk4_canvas_surface();
    if (s == NULL
        || cairo_surface_write_to_png(s, path) != CAIRO_STATUS_SUCCESS)
    {
      g_printerr("could not write %s\n", path);
      return 1;
    }
    g_print("wrote %s\n", path);
    xa_perf_report_totals();     // XASTIR_PERF=1 to see what was actually drawn
    return 0;
  }

  app = gtk_application_new("org.xastir.Gtk4", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
