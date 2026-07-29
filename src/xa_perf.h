/*
 * xa_perf.h -- lightweight phase timing for Xastir rendering work.
 *
 * Purpose: answer "where do the milliseconds go during pan/zoom" with measured
 * numbers instead of guesses.  Entirely inert unless the environment variable
 * XASTIR_PERF is set, so it can stay in the tree without affecting normal runs.
 *
 * Usage:
 *     xa_perf_frame_begin();
 *     xa_perf_begin(XA_ZONE_SHP_READ);
 *     ...
 *     xa_perf_end(XA_ZONE_SHP_READ);
 *     xa_perf_count(XA_CNT_VERTICES, n);
 *     xa_perf_frame_end("create_image");
 *
 * Zones must not be nested with themselves.  Different zones may nest freely.
 */

#ifndef XA_PERF_H
#define XA_PERF_H

typedef enum
{
  XA_ZONE_CREATE_IMAGE = 0,   // whole base-map render
  XA_ZONE_REFRESH_IMAGE,      // overlay/composite pass
  XA_ZONE_SHP_OPEN,           // SHPOpen + DBFOpen per map
  XA_ZONE_SHP_INDEX,          // RTree build/search
  XA_ZONE_SHP_READ,           // SHPReadObject: disk + parse
  XA_ZONE_SHP_TRANSFORM,      // coordinate conversion
  XA_ZONE_SHP_DRAW,           // X11 drawing calls
  XA_ZONE_LOAD_MAPS,          // load_maps()/load_auto_maps(): whole map pass
  XA_ZONE_MAP_ONE,            // one map driver call, end to end
  XA_ZONE_MAP_ONSCREEN,       // map_onscreen_index(): per-file visibility test
  XA_ZONE_MAP_XMUPDATE,       // XmUpdateDisplay() issued once per map file
  XA_ZONE_EVENTS,             // HandlePendingEvents(): X event pump
  XA_ZONE_STATUSLINE,         // statusline(): Motif text field update
  XA_ZONE_DBFAWK,             // dbfawk signature match + rule evaluation
  XA_ZONE_DBFAWK_SETUP,       // per-file: sig lookup, symtab, awk compile
  XA_ZONE_RTREE_BUILD,        // build_rtree(): one-time index construction
  XA_ZONE_RTREE_READ,         //   ...of which: reading shape extents
  XA_ZONE_RTREE_INSERT,       //   ...of which: RTree insertion
  XA_ZONE_ALERT_MAPS,         // load_alert_maps()
  XA_ZONE_DISPLAY_FILE,       // display_file(): stations, symbols, trails
  XA_ZONE_DRAW_GRID,          // draw_grid()
  XA_ZONE_COUNT
} xa_zone_t;

typedef enum
{
  XA_CNT_MAPS = 0,            // maps drawn this frame
  XA_CNT_SHAPES_READ,         // shapes actually read from disk
  XA_CNT_SHAPES_SKIPPED,      // shapes culled before draw
  XA_CNT_VERTICES,            // vertices submitted to the renderer
  XA_CNT_DRAW_CALLS,          // individual draw primitives issued
  XA_CNT_COUNT
} xa_counter_t;

// Returns nonzero when profiling is active.  Cheap; safe to call anywhere.
int  xa_perf_enabled(void);

void xa_perf_frame_begin(void);
void xa_perf_frame_end(const char *label);

void xa_perf_begin(xa_zone_t zone);
void xa_perf_end(xa_zone_t zone);
void xa_perf_count(xa_counter_t counter, long n);

// Cumulative totals across the whole session, printed on exit.
void xa_perf_report_totals(void);

#endif // XA_PERF_H
