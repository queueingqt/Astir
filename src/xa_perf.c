/*
 * xa_perf.c -- lightweight phase timing for Xastir rendering work.
 * See xa_perf.h.  Inert unless XASTIR_PERF is set in the environment.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "xa_perf.h"

static int   perf_on = -1;          // -1 = not yet checked
static long  zone_ns[XA_ZONE_COUNT];
static long  zone_start[XA_ZONE_COUNT];
static long  zone_calls[XA_ZONE_COUNT];
static long  counters[XA_CNT_COUNT];

static long  total_zone_ns[XA_ZONE_COUNT];
static long  total_counters[XA_CNT_COUNT];
static long  frame_ns_sum = 0;
static long  frame_ns_max = 0;
static long  frame_count  = 0;
static long  frame_start  = 0;
static long  frames_over_100ms = 0;

static const char *zone_name[XA_ZONE_COUNT] =
{
  "create_image", "refresh_image", "shp_open", "shp_index",
  "shp_read", "shp_transform", "shp_draw",
  "load_maps", "map_one", "dbfawk", "dbfawk_setup", "rtree_build",
  "rtree_read", "rtree_insert",
  "alert_maps", "display_file", "draw_grid"
};

static const char *counter_name[XA_CNT_COUNT] =
{
  "maps", "shapes_read", "shapes_skipped", "vertices", "draw_calls"
};


static long now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec * 1000000000L + ts.tv_nsec;
}


int xa_perf_enabled(void)
{
  if (perf_on < 0)
  {
    const char *e = getenv("XASTIR_PERF");
    perf_on = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
  }
  return perf_on;
}


void xa_perf_frame_begin(void)
{
  if (!xa_perf_enabled())
  {
    return;
  }
  memset(zone_ns, 0, sizeof(zone_ns));
  memset(zone_calls, 0, sizeof(zone_calls));
  memset(counters, 0, sizeof(counters));
  frame_start = now_ns();
}


void xa_perf_begin(xa_zone_t zone)
{
  if (!xa_perf_enabled() || zone >= XA_ZONE_COUNT)
  {
    return;
  }
  zone_start[zone] = now_ns();
}


void xa_perf_end(xa_zone_t zone)
{
  if (!xa_perf_enabled() || zone >= XA_ZONE_COUNT || zone_start[zone] == 0)
  {
    return;
  }
  long d = now_ns() - zone_start[zone];
  zone_ns[zone] += d;
  total_zone_ns[zone] += d;
  zone_calls[zone]++;
  zone_start[zone] = 0;
}


void xa_perf_count(xa_counter_t counter, long n)
{
  if (!xa_perf_enabled() || counter >= XA_CNT_COUNT)
  {
    return;
  }
  counters[counter] += n;
  total_counters[counter] += n;
}


void xa_perf_frame_end(const char *label)
{
  int i;
  long frame_ns;

  if (!xa_perf_enabled() || frame_start == 0)
  {
    return;
  }

  frame_ns = now_ns() - frame_start;
  frame_start = 0;
  frame_ns_sum += frame_ns;
  frame_count++;
  if (frame_ns > frame_ns_max)
  {
    frame_ns_max = frame_ns;
  }
  // 100 ms is the stutter threshold from the project's acceptance criteria.
  if (frame_ns > 100000000L)
  {
    frames_over_100ms++;
  }

  fprintf(stderr, "[perf] %-14s %7.1f ms |", label, frame_ns / 1e6);
  for (i = 0; i < XA_ZONE_COUNT; i++)
  {
    if (zone_ns[i] > 0)
    {
      fprintf(stderr, " %s %.1f", zone_name[i], zone_ns[i] / 1e6);
    }
  }
  fprintf(stderr, " |");
  for (i = 0; i < XA_CNT_COUNT; i++)
  {
    if (counters[i] > 0)
    {
      fprintf(stderr, " %s %ld", counter_name[i], counters[i]);
    }
  }
  fprintf(stderr, "\n");
  fflush(stderr);
}


void xa_perf_report_totals(void)
{
  int i;

  if (!xa_perf_enabled() || frame_count == 0)
  {
    return;
  }

  fprintf(stderr, "\n=== xastir perf summary ===\n");
  fprintf(stderr, "frames            : %ld\n", frame_count);
  fprintf(stderr, "mean frame        : %.1f ms\n",
          frame_ns_sum / 1e6 / (double)frame_count);
  fprintf(stderr, "worst frame       : %.1f ms\n", frame_ns_max / 1e6);
  fprintf(stderr, "frames over 100ms : %ld  (stutter threshold)\n",
          frames_over_100ms);
  fprintf(stderr, "implied fps       : %.1f\n",
          frame_ns_sum > 0
          ? (double)frame_count / (frame_ns_sum / 1e9) : 0.0);

  fprintf(stderr, "\ncumulative time by phase:\n");
  for (i = 0; i < XA_ZONE_COUNT; i++)
  {
    if (total_zone_ns[i] > 0)
    {
      fprintf(stderr, "  %-14s %8.1f ms\n", zone_name[i],
              total_zone_ns[i] / 1e6);
    }
  }

  fprintf(stderr, "\ncumulative counts:\n");
  for (i = 0; i < XA_CNT_COUNT; i++)
  {
    if (total_counters[i] > 0)
    {
      fprintf(stderr, "  %-14s %8ld\n", counter_name[i], total_counters[i]);
    }
  }
  fprintf(stderr, "===========================\n");
  fflush(stderr);
}
