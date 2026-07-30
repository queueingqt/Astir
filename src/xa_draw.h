/*
 * xa_draw.h -- drawing abstraction layer (Stage 2 of the modernization plan).
 *
 * Purpose: funnel every drawing operation through one interface so the backend
 * can be swapped without touching call sites.  Today the only backend is
 * xa_draw_x11.c, a thin mapping onto the existing Xlib calls and GCs, and the
 * output must stay pixel-identical.
 *
 * The replacement backend is deliberately NOT named here.  GTK4 does not draw
 * through Cairo -- that was GTK3; GTK4 renders GSK node trees through Vulkan or
 * GL, with Cairo only as a fallback path -- so "which renderer" is an open
 * decision, not a settled one.  (Measured on this machine GTK4 does in fact
 * land on GskCairoRenderer, because the GPU offers only GL 2.1 and no Vulkan.)
 * This layer exists so that decision can be made, and remade, against
 * measurements instead of by editing 452 call sites.
 *
 * This header deliberately includes no X11 headers.  Code that draws should be
 * able to compile without knowing which backend is in use.
 *
 * Measured scope of the conversion (the C sources, with comments and string
 * literals excluded): 452 call sites, 17 files, 28 Xlib primitives.  Half of
 * those sites are GC state changes rather than drawing, which is why the
 * interface is pen-based -- most replacement renderers are state machines too.
 */

#ifndef XA_DRAW_H
#define XA_DRAW_H

/*
 * An opaque handle to something that can be drawn on or copied from.
 *
 * It is an X11 Drawable (XID) today, which is why the underlying type is an
 * unsigned long: it lets callers pass the existing `pixmap`, `pixmap_final`
 * and `pixmap_alerts` globals unchanged during the transition.  Callers must
 * not interpret the value or do arithmetic on it, so that a later backend can
 * redefine it as an index or a pointer to its own surface type.
 */
typedef unsigned long xa_surface_id;

// A drawing state: colour, line attributes, fill, clip.  An X11 GC today.
// void * so that a GC converts implicitly and no cast is needed at call sites.
typedef void *xa_pen;

// A colour.  An X11 Pixel (index or packed value) today, not an RGB triple;
// converting the ~250 colours[] lookups to RGB is a separate step.
typedef unsigned long xa_color;

// A point.  Deliberately laid out to match XPoint (two shorts) so that arrays
// can be passed straight through without copying -- the drawing loops build
// tens of thousands of these per frame and a conversion pass would show up in
// the frame time.  xa_draw_x11.c asserts the layout at compile time.
typedef struct
{
  short x;
  short y;
} xa_point;

// A surface handle that refers to nothing.
#define XA_SURFACE_NONE ((xa_surface_id)0)

/*
 * Style constants.
 *
 * These deliberately carry the same numeric values as the X11 constants they
 * currently map to, so the backend needs no translation table and the
 * conversion cannot change behaviour.  xa_draw_x11.c static-asserts every one
 * of them against the real X value, so if that assumption ever breaks the
 * build fails rather than the rendering quietly changing.
 */
#define XA_LINE_SOLID        0
#define XA_LINE_ON_OFF_DASH  1
#define XA_LINE_DOUBLE_DASH  2

#define XA_CAP_NOT_LAST      0
#define XA_CAP_BUTT          1
#define XA_CAP_ROUND         2
#define XA_CAP_PROJECTING    3

#define XA_JOIN_MITER        0
#define XA_JOIN_ROUND        1
#define XA_JOIN_BEVEL        2

#define XA_FILL_SOLID        0
#define XA_FILL_TILED        1
#define XA_FILL_STIPPLED     2
#define XA_FILL_OPAQUE_STIPPLED 3

#define XA_COORD_ORIGIN      0
#define XA_COORD_PREVIOUS    1

#define XA_SHAPE_COMPLEX     0
#define XA_SHAPE_NONCONVEX   1
#define XA_SHAPE_CONVEX      2

// Raster ops.  Only the two Xastir actually uses are exposed.
#define XA_FUNC_COPY         3   /* GXcopy */
#define XA_FUNC_XOR          6   /* GXxor  */


/* ---- the canvas ------------------------------------------------------- */

/*
 * The on-screen surface that finished frames are presented to, and the current
 * size of that canvas.  Queried rather than assumed so that no call site has to
 * name the drawing area, and so a backend that presents somewhere else only has
 * to change here.  xa_screen_target() returns XA_SURFACE_NONE before the canvas
 * exists.
 */
xa_surface_id xa_screen_target(void);
void          xa_canvas_size(int *width, int *height);


/* ---- text and fonts --------------------------------------------------- */

/*
 * Fonts were the last thing keeping the core on Xlib, and the reason two files
 * could not be split.  The grid subsystem in maps.c -- draw_grid, both UTM grid
 * functions, the lat/lon grid, the largest core functions in the file -- reached
 * Motif through nothing but text measurement, as did draw_symbol.  No dialog was
 * ever involved.
 *
 * An opaque font handle.  An XFontStruct * today, which is why it is a pointer:
 * the existing rotated_label_font[] array can hold these unchanged.  Callers
 * must not dereference it.
 */
typedef void *xa_font;

#define XA_FONT_NONE ((xa_font)0)

// Load a font by backend-specific name (an XLFD today).  XA_FONT_NONE if it is
// not available -- callers already check for that and fall back.
xa_font xa_font_load(const char *name);
void    xa_font_free(xa_font f);

/*
 * Font metrics, in pixels.
 *
 * max_width and min_width are the widest and narrowest glyph, not the average:
 * the call sites compute (3*max + min)/4 as a stand-in for average advance, and
 * reproducing that arithmetic exactly matters more than improving it, since the
 * output must stay pixel-identical.
 */
typedef struct
{
  int max_width;
  int min_width;
  int ascent;
  int descent;
} xa_font_metrics;

void xa_font_metrics_get(xa_font f, xa_font_metrics *m);

/*
 * Ink extents of one string in a given font, in pixels.  Distinct from
 * xa_font_metrics: those are the font's widest and tallest glyph, these are what
 * this particular string actually occupies.  Both callers exist -- the label
 * width uses the string, the symbol layout uses the font.
 *
 * Any output pointer may be NULL.
 */
void xa_font_text_extents(xa_font f, const char *text, int length,
                          int *width, int *ascent, int *descent);

// Width alone, for the common case.
int xa_font_text_width(xa_font f, const char *text, int length);

/*
 * The same two questions about whatever font a pen is currently drawing with.
 *
 * These exist because draw_symbols.c never names a font -- it asks the GC what
 * it is set to.  Under X that is XGContextFromGC() plus XQueryFont(), a server
 * round trip and an allocation that has to be freed; three call sites did it and
 * two of them leaked on one path.  A backend that tracks its own pen state
 * answers both without asking anyone.
 */
void xa_pen_font_metrics(xa_pen pen, xa_font_metrics *m);
int  xa_pen_text_width(xa_pen pen, const char *text, int length);

// Set the font a pen draws text with.
void xa_pen_font(xa_pen pen, xa_font f);

/*
 * Alignment for rotated text.  Same numeric values as rotated.c's constants,
 * static-asserted in the backend like the style constants above.
 */
#define XA_ALIGN_NONE     0
#define XA_ALIGN_TLEFT    1
#define XA_ALIGN_TCENTRE  2
#define XA_ALIGN_TRIGHT   3
#define XA_ALIGN_MLEFT    4
#define XA_ALIGN_MCENTRE  5
#define XA_ALIGN_MRIGHT   6
#define XA_ALIGN_BLEFT    7
#define XA_ALIGN_BCENTRE  8
#define XA_ALIGN_BRIGHT   9

/*
 * Draw text rotated by `degrees`, anchored per `align`.
 *
 * Today this lands on rotated.c, a self-contained rotated-text renderer that
 * predates Xft.  A GTK4 backend does not convert it -- Pango rotates text
 * natively -- so keeping it behind one call is what makes that substitution a
 * backend change rather than a call-site sweep.
 */
void xa_draw_text_rotated(xa_surface_id dst, xa_pen pen, xa_font f,
                          int x, int y, float degrees, int align,
                          const char *text);

/*
 * Text named by font spec rather than by loaded handle, with rotation,
 * alignment and an optional outline.
 *
 * This is the interface the tree actually needs, and the reason it is separate
 * from the handle-based calls above is worth recording.  Xastir has TWO text
 * implementations selected by HAVE_CAIRO: Cairo, which takes a font spec string
 * and needs no font handle at all, and the older xvertext path, which needs a
 * loaded XFontStruct.  Both were open-coded at the call sites, so every caller
 * had an #ifdef in it and the Cairo half needed a Display to pass down.
 *
 * Behind this call the backend picks the implementation, caches whatever it
 * needs to, and supplies its own display connection.  Callers name a font and a
 * colour and stop knowing which renderer is compiled in.
 *
 * fontspec is an XLFD or "Family:size=N".  outline non-zero draws a one-pixel
 * outline in outline_color first.
 */
void xa_draw_text_styled(xa_surface_id dst, int x, int y, float degrees,
                         const char *text, const char *fontspec,
                         xa_color fg, int outline, xa_color outline_color,
                         int align);

// Measurement by font spec, for the same reason.
int xa_text_width(const char *text, const char *fontspec);
int xa_text_height(const char *fontspec);


/* ---- colours ---------------------------------------------------------- */

/*
 * Resolve a colour to its components, 0-65535 per channel as X reports them.
 *
 * Needed because the trail drawing picks a contrasting dot colour by computing
 * the brightness of the trail colour, and xa_color is an opaque index rather
 * than an RGB triple.  The one caller is why this exists; a backend that
 * already holds RGB answers it without a round trip.  Leaves the outputs at 0
 * if the colour cannot be resolved.
 */
void xa_color_rgb(xa_color c, unsigned short *r, unsigned short *g,
                  unsigned short *b);

/*
 * Look up a colour by name ("black", "MistyRose").
 *
 * The last thing keeping a Widget in the station-drawing signatures.  After the
 * font work, display_station -> draw_symbol -> draw_nice_string threaded a
 * Widget down three levels for one GetPixelByName(w, name), which wanted it only
 * for XtDisplay(w).  With this the chain drops the parameter and db.c stops
 * referencing the drawing area -- the last symbol the core needed from main.o.
 */
xa_color xa_color_by_name(const char *name);

/*
 * Resolve an RGB triple to a drawable colour.  The inverse of xa_color_rgb(),
 * and the call every raster map driver needs.
 *
 * Channels are 0-65535, as X reports them.  All four arguments are in/out and
 * none may be NULL, because this reproduces XAllocColor's contract exactly: on
 * a palette display the backend hands back the nearest colour it could actually
 * obtain, which is not necessarily the one asked for.  Callers depend on that.
 * map_geo.c reads the resolved channels straight back and multiplies them by
 * raster_map_intensity, so a version that only returned a pixel would quietly
 * change what that driver draws on such a display.
 *
 * Returns non-zero on success.  On failure nothing is written -- again as
 * XAllocColor behaves -- so *pixel keeps whatever the caller had in it.  The
 * eight call sites all ignored the old return value and still do; the value is
 * exposed because throwing it away is what left them unable to tell a colour
 * they got from a colour they did not.
 *
 * Which of the two mechanisms runs -- allocation against the colormap, or
 * packing the channels into the visual's bit fields -- is the backend's
 * business.  Every call site used to make that choice itself by testing
 * visual_type, an X visual classification, in the middle of a map driver.
 */
int xa_color_resolve(unsigned short *r, unsigned short *g, unsigned short *b,
                     xa_color *pixel);


/* ---- surfaces --------------------------------------------------------- */

// Create an offscreen surface.  XA_DEPTH_CANVAS means "same as the canvas";
// XA_DEPTH_BITMAP is the 1-bit depth used for stipples and clip masks.
#define XA_DEPTH_CANVAS 0
#define XA_DEPTH_BITMAP 1
xa_surface_id xa_surface_create(int width, int height, int depth);
void          xa_surface_destroy(xa_surface_id s);

// Create a bitmap from packed 1-bit-per-pixel data (X bitmap / .xbm format).
xa_surface_id xa_bitmap_from_data(const char *bits, int width, int height);

/*
 * Load a 1-bit bitmap from a file, for use as a stipple or a clip mask.
 *
 * The weather-alert shading in map_shp.c reads its pattern from an .xbm on
 * disk.  Width and height may be NULL; both are left alone on failure, which is
 * reported as XA_SURFACE_NONE rather than by a status code, so that a caller
 * cannot use the surface without having looked.  The X hotspot the underlying
 * call also returns is not exposed -- no caller ever read it.
 */
xa_surface_id xa_bitmap_load(const char *path, int *width, int *height);


/* ---- pixel buffers ---------------------------------------------------- */

/*
 * A client-side buffer of pixels that can be read and written one at a time,
 * then handed to a surface in one go.  An XImage today.
 *
 * This exists for speed, not convenience, and the reason is worth keeping: the
 * OSM drivers used to draw each map pixel with its own fill-a-1x1-rectangle
 * call, which is a round trip to the X server per pixel and was the dominant
 * cost on slow or remote displays.  They now capture the frame once, write into
 * the buffer, and put it back once.  So this is the one part of the drawing
 * interface where per-pixel cost matters, and a backend should treat
 * xa_image_put_pixel() as something that must not become a round trip.
 *
 * Opaque, and a pointer today because XImage is one.
 */
typedef void *xa_image;

#define XA_IMAGE_NONE ((xa_image)0)

/*
 * Read a rectangle of a surface back into a buffer.
 *
 * The OSM drivers seed their buffer this way rather than starting from blank, so
 * that areas no tile covers keep whatever was already drawn there -- the
 * lower-zoom fallback tiles underneath.  Starting blank would erase them.
 */
xa_image xa_image_capture(xa_surface_id src, int x, int y,
                          int width, int height);

// A zero-filled buffer at canvas depth, for when a capture is not possible.
xa_image xa_image_create(int width, int height);

/*
 * Load an image file into a buffer, reporting its size.  XPM today.
 *
 * Only reachable in a build without ImageMagick, which is not the build here, so
 * this path compiles but is untested -- said plainly because the alternative is
 * to imply otherwise.  Returns XA_IMAGE_NONE on failure; width and height may
 * be NULL and are untouched unless the load succeeds.
 */
xa_image xa_image_load(const char *path, int *width, int *height);

void xa_image_destroy(xa_image img);

void     xa_image_put_pixel(xa_image img, int x, int y, xa_color c);
xa_color xa_image_get_pixel(xa_image img, int x, int y);

// Hand the buffer to a surface.
void xa_image_to_surface(xa_surface_id dst, xa_pen pen, xa_image img,
                         int src_x, int src_y, int dst_x, int dst_y,
                         int width, int height);


/* ---- regions ---------------------------------------------------------- */

/*
 * A region: an arbitrary set of pixels, used here only to build a clip mask.
 *
 * map_shp.c draws polygons with holes in them by starting from a rectangle
 * covering the shape and subtracting one region per hole, then clipping the
 * fill to what is left -- a "swiss-cheese rectangle", in the comment that has
 * been there since the method was chosen.  That is the whole use of regions in
 * the tree, and the reason this is a narrow interface rather than a general one:
 * six operations, one consumer.
 *
 * Opaque, and a pointer today because X's Region is one.
 */
typedef void *xa_region;

#define XA_REGION_NONE ((xa_region)0)

// Winding rules for turning a polygon into a region.  Same numeric values as
// X11's, static-asserted in the backend like the style constants above.
#define XA_EVEN_ODD 0
#define XA_WINDING  1

xa_region xa_region_create(void);
void      xa_region_destroy(xa_region r);

// A region covering one rectangle, built from a polygon, and the two combining
// operations.  xa_region_add_rect() grows r in place; xa_region_subtract()
// writes a - b into dst, which must be a third region distinct from both.
xa_region xa_region_from_polygon(xa_point *points, int npoints, int rule);
void      xa_region_add_rect(xa_region r, int x, int y, int width, int height);
void      xa_region_subtract(xa_region a, xa_region b, xa_region dst);


/* ---- pens ------------------------------------------------------------- */

// Create a drawing state usable with the given surface, or with the canvas if
// XA_SURFACE_NONE.  Pens are a scarce server resource under X, so they are
// created once and reused rather than per draw.
xa_pen xa_pen_create(xa_surface_id for_surface);
void   xa_pen_destroy(xa_pen pen);

/*
 * Copy a rectangle between surfaces.  The general primitive; everything else
 * that moves pixels is expressed in terms of it.  A copy wholly or partly
 * outside either surface is the caller's responsibility, exactly as with the
 * Xlib call this replaces.
 */
void xa_copy_area(xa_surface_id src,
                  xa_surface_id dst,
                  xa_pen pen,
                  int src_x, int src_y,
                  int width, int height,
                  int dst_x, int dst_y);

/*
 * Present a full-screen offscreen surface to the visible canvas.
 *
 * This replaced 37 hand-written copies of the same ten-line XCopyArea block --
 * two thirds of every XCopyArea in the tree.  Under a compositing toolkit this
 * becomes buffer presentation, so keeping it as one named operation is what
 * makes that substitution possible.  Does nothing if the canvas does not exist
 * yet, rather than issuing a request against a window that has not been created.
 */
void xa_present_full(xa_surface_id src);


/* ---- pen state -------------------------------------------------------- */

void xa_pen_color(xa_pen pen, xa_color c);
void xa_pen_bg(xa_pen pen, xa_color c);
void xa_pen_line(xa_pen pen, int width, int line_style, int cap, int join);
void xa_pen_dashes(xa_pen pen, int dash_offset, const char *dash_list, int n);
void xa_pen_fill_style(xa_pen pen, int fill_style);
void xa_pen_stipple(xa_pen pen, xa_surface_id bitmap);
void xa_pen_ts_origin(xa_pen pen, int x, int y);
void xa_pen_function(xa_pen pen, int func);
void xa_pen_clip_mask(xa_pen pen, xa_surface_id mask);
void xa_pen_clip_origin(xa_pen pen, int x, int y);

// Clip a pen to a region.  There is no way to undo this, so the one caller
// makes a throwaway pen, clips it, draws one shape and destroys it.
void xa_pen_clip_region(xa_pen pen, xa_region r);

/*
 * Copy the fill style and stipple from one pen to another.
 *
 * Deliberately not a general "copy pen state": the single caller wants exactly
 * these two attributes, and an interface that took a mask of which attributes to
 * copy would be handing every backend a set of X11 GC component names to
 * implement.
 */
void xa_pen_copy_fill(xa_pen dst, xa_pen src);


/* ---- drawing ---------------------------------------------------------- */

void xa_draw_line(xa_surface_id dst, xa_pen pen,
                  int x1, int y1, int x2, int y2);
void xa_draw_lines(xa_surface_id dst, xa_pen pen,
                   xa_point *points, int npoints, int coord_mode);
void xa_draw_point(xa_surface_id dst, xa_pen pen, int x, int y);
void xa_draw_rect(xa_surface_id dst, xa_pen pen,
                  int x, int y, int width, int height);
void xa_fill_rect(xa_surface_id dst, xa_pen pen,
                  int x, int y, int width, int height);
void xa_fill_polygon(xa_surface_id dst, xa_pen pen,
                     xa_point *points, int npoints, int shape, int coord_mode);
void xa_draw_arc(xa_surface_id dst, xa_pen pen,
                 int x, int y, int width, int height, int angle1, int angle2);
void xa_fill_arc(xa_surface_id dst, xa_pen pen,
                 int x, int y, int width, int height, int angle1, int angle2);
void xa_draw_string(xa_surface_id dst, xa_pen pen,
                    int x, int y, const char *text, int length);


/* ---- the surfaces and pens the application draws with ------------------ */

/*
 * These are the long-lived drawing objects the whole application shares.  They
 * are defined by the backend (xa_draw_x11.c) and every core drawing site passes
 * them straight to the calls above.
 *
 * They were declared in `xastir.h` and `main.h` as `Pixmap`, `GC` and `Pixel`,
 * which is why `xastir.h` needed <X11/Intrinsic.h> and therefore why every core
 * file in the tree got X11 whether it wanted it or not.  Declared here in the
 * neutral types instead, which is what the call sites already treat them as --
 * no core file ever passed one of these to an Xlib function.
 *
 * The types are not merely compatible, they are identical: X11's `Pixmap` and
 * `Pixel` are both `unsigned long`, matching `xa_surface_id` and `xa_color`, so
 * the backend's own definitions agree with these declarations exactly.  `GC` is
 * a pointer, and `xa_pen` is `void *`, which a GC converts to implicitly -- so
 * the backend keeps using them with Xlib and needs no cast.
 */

extern xa_pen gc;               // maps
extern xa_pen gc2;              // symbols
extern xa_pen gc_tint;          // tinting maps and symbols
extern xa_pen gc_stipple;       // stippled symbols
extern xa_pen gc_bigfont;

extern xa_surface_id pixmap;            // the map being composed
extern xa_surface_id pixmap_final;      // the finished frame, what is presented
extern xa_surface_id pixmap_alerts;     // weather alert overlay

// Stipple patterns: position ambiguity at three sizes, and weather alerts.
extern xa_surface_id pixmap_50pct_stipple;
extern xa_surface_id pixmap_25pct_stipple;
extern xa_surface_id pixmap_13pct_stipple;
extern xa_surface_id pixmap_wx_stipple;

extern xa_color colors[256];    // the allocated screen colours

#endif // XA_DRAW_H
