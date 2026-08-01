/*
 * Priority-ordered label placement.  See label_place.h for why it is deferred.
 *
 * The placement itself is greedy: sort by priority, and take each label if its
 * box is clear.  Greedy is not optimal -- choosing the largest set of
 * non-overlapping rectangles is NP-hard -- but optimal is not what a map wants
 * anyway.  A map wants the important names, and greedy-by-priority gives
 * exactly that.
 *
 * Collision is tested against a uniform grid rather than a list.  A linear scan
 * is fine for a hundred labels and quadratic for a thousand, and a dense city
 * tile submits thousands; the grid keeps each test proportional to the labels
 * actually near the one being placed.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "core/astir.h"
#include "core/map/maps.h"   // draw_rotated_label_text
#include "core/render/draw_symbols.h"   // draw_nice_string
#include "core/render/label_place.h"
#include "core/util/snprintf.h"

#define LABEL_MAX      4096        /* per pass; beyond this the view is hopeless */
#define LABEL_TEXT_MAX 96

/*
 * Labels are allowed to touch but not to overlap, and a little clear space is
 * what makes two names read as two names.
 *
 * HORIZONTAL ONLY, and that is not a detail.
 *
 * A station's annotation is several lines stacked one font-height apart:
 * callsign, then speed, then weather, and so on.  Padding the box vertically
 * made each line four pixels taller than the gap between them, so every line
 * after the first overlapped the one above -- its OWN station's -- and was
 * rejected.  What reached the screen was a callsign and nothing else, for every
 * station, however much empty map surrounded it.
 *
 * That is why the weather and heading lines vanished even at close zoom with
 * room to spare, and it is a different fault from the two that were also hiding
 * them: text that was not valid UTF-8, and text that never went through the
 * placer at all.  Three causes, one symptom.
 *
 * Stacked lines are meant to be adjacent, so vertically they are allowed to be.
 * Side by side is where a gap is needed, and that is where it is applied.
 */
#define LABEL_PAD 2

typedef struct
{
  long x, y;
  int angle;
  int color;
  int font_size;
  int priority;
  int style;
  int bgcolor;                     /* the outline colour, for OUTLINED */
  int text_style;                  /* draw_nice_string style, a user setting */
  int w, h;                        /* measured at submit time */
  char text[LABEL_TEXT_MAX];
  const char *font;
} label;

static label labels[LABEL_MAX];
static int nlabels;

/*
 * The occupancy grid.
 *
 * Cell size is a compromise: too small and a long name touches many cells, too
 * large and every test degenerates to a linear scan of the neighbourhood.  A
 * cell about the height of a line of text means a typical label spans a few
 * cells horizontally and one vertically.
 */
#define GRID_CELL 24
#define GRID_W    128
#define GRID_H    128
#define GRID_DEPTH 8               /* placed labels remembered per cell */

static struct
{
  short n;
  short idx[GRID_DEPTH];
} grid[GRID_H][GRID_W];

static int placed_x0[LABEL_MAX], placed_y0[LABEL_MAX];
static int placed_x1[LABEL_MAX], placed_y1[LABEL_MAX];
static int nplaced;


void label_frame_begin(void)
{
  nlabels = 0;
  nplaced = 0;
  memset(grid, 0, sizeof(grid));
}


int label_submit(long x, long y, int angle, const char *text,
                 const char *fontspec, int color, int font_size, int priority)
{
  return label_submit_styled(x, y, angle, text, fontspec, color, 0,
                             font_size, priority, LABEL_STYLE_ROTATED, 0);
}


int label_submit_styled(long x, long y, int angle, const char *text,
                        const char *fontspec, int color, int bgcolor,
                        int font_size, int priority, int style,
                        int text_style)
{
  label *l;

  if (text == NULL || text[0] == '\0' || nlabels >= LABEL_MAX)
  {
    return 0;
  }

  l = &labels[nlabels];
  l->x = x;
  l->y = y;
  l->angle = angle;
  l->color = color;
  l->font_size = font_size;
  l->priority = priority;
  l->style = style;
  l->bgcolor = bgcolor;
  l->text_style = text_style;
  l->font = fontspec;
  astir_snprintf(l->text, sizeof(l->text), "%s", text);

  /*
   * Measure now, not at flush.  The font a label is drawn in is a property of
   * the label, and asking later means either storing enough state to ask the
   * right question or asking the wrong one.
   */
  l->w = xa_text_width(l->text, fontspec);
  l->h = xa_text_height(fontspec);
  if (l->w <= 0)
  {
    // A backend with no text measurement: fall back to a rough guess rather
    // than treating every label as zero-width and placing all of them.
    l->w = (int)strlen(l->text) * 7;
  }
  if (l->h <= 0)
  {
    l->h = 12;
  }

  nlabels++;
  return 1;
}


static int by_priority(const void *a, const void *b)
{
  const label *la = (const label *)a;
  const label *lb = (const label *)b;

  if (la->priority != lb->priority)
  {
    return lb->priority - la->priority;      /* descending */
  }
  /*
   * Ties break on length, shortest first.
   *
   * Not arbitrary: a short name occupies less space, so taking it first fits
   * more names overall.  It also makes the result stable rather than dependent
   * on the order tiles happened to be read, which matters because an unstable
   * labeller makes the map flicker as you pan.
   */
  return la->w - lb->w;
}


// Does this box overlap anything already placed?
static int collides(int x0, int y0, int x1, int y1)
{
  int cx0 = x0 / GRID_CELL, cx1 = x1 / GRID_CELL;
  int cy0 = y0 / GRID_CELL, cy1 = y1 / GRID_CELL;
  int cx, cy, k;

  if (cx0 < 0) { cx0 = 0; }
  if (cy0 < 0) { cy0 = 0; }
  if (cx1 >= GRID_W) { cx1 = GRID_W - 1; }
  if (cy1 >= GRID_H) { cy1 = GRID_H - 1; }

  for (cy = cy0; cy <= cy1; cy++)
  {
    for (cx = cx0; cx <= cx1; cx++)
    {
      for (k = 0; k < grid[cy][cx].n; k++)
      {
        int i = grid[cy][cx].idx[k];

        /*
         * Strictly less-than, so boxes that merely TOUCH do not collide.
         *
         * The header says labels are allowed to touch but not to overlap, and
         * this said otherwise: with <= and >=, two boxes sharing an edge were
         * a collision.  Stacked lines of one station share an edge exactly --
         * each is a font-height below the last and exactly a font-height tall
         * -- so the second line of every station was rejected by the first.
         *
         * That is the last of three separate reasons the weather and heading
         * lines were invisible.  The others were text that was not valid UTF-8,
         * which Pango silently refused to lay out, and text that never went
         * through the placer at all.
         */
        if (x0 < placed_x1[i] && x1 > placed_x0[i]
            && y0 < placed_y1[i] && y1 > placed_y0[i])
        {
          return 1;
        }
      }
    }
  }
  return 0;
}


static void occupy(int x0, int y0, int x1, int y1)
{
  int cx0 = x0 / GRID_CELL, cx1 = x1 / GRID_CELL;
  int cy0 = y0 / GRID_CELL, cy1 = y1 / GRID_CELL;
  int cx, cy;
  int i = nplaced;

  placed_x0[i] = x0;
  placed_y0[i] = y0;
  placed_x1[i] = x1;
  placed_y1[i] = y1;
  nplaced++;

  if (cx0 < 0) { cx0 = 0; }
  if (cy0 < 0) { cy0 = 0; }
  if (cx1 >= GRID_W) { cx1 = GRID_W - 1; }
  if (cy1 >= GRID_H) { cy1 = GRID_H - 1; }

  for (cy = cy0; cy <= cy1; cy++)
  {
    for (cx = cx0; cx <= cx1; cx++)
    {
      // A full cell drops the reference, not the label: the label is still
      // placed and still drawn, it is just no longer a collision candidate for
      // later ones in that cell.  Losing a few of those is much cheaper than
      // making the grid unbounded.
      if (grid[cy][cx].n < GRID_DEPTH)
      {
        grid[cy][cx].idx[grid[cy][cx].n++] = (short)i;
      }
    }
  }
}


int label_flush(xa_surface_id where, int *submitted)
{
  int i, drawn = 0;

  if (submitted != NULL)
  {
    *submitted = nlabels;
  }
  if (nlabels == 0)
  {
    return 0;
  }

  qsort(labels, (size_t)nlabels, sizeof(labels[0]), by_priority);

  for (i = 0; i < nlabels && nplaced < LABEL_MAX; i++)
  {
    label *l = &labels[i];
    int x0, y0, x1, y1;

    /*
     * The box.  Text is drawn from a baseline, so the box sits mostly above
     * the anchor; the exact offsets matter less than being consistent, since
     * every label is measured the same way and they are only compared with
     * each other.
     */
    x0 = (int)l->x - LABEL_PAD;
    y0 = (int)l->y - l->h;
    x1 = (int)l->x + l->w + LABEL_PAD;
    y1 = (int)l->y;

    // Wholly off screen: not a collision, just nothing to draw.
    if (x1 < 0 || y1 < 0 || x0 > (int)screen_width || y0 > (int)screen_height)
    {
      continue;
    }
    if (collides(x0, y0, x1, y1))
    {
      // ASTIR_DEBUG bit 16: which labels lost, and to what.  A label that is
      // simply absent from the screen gives no clue whether it was rejected,
      // never submitted, or refused by the renderer.
      if (debug_level & 16)
      {
        fprintf(stderr, "  label rejected: \"%s\" box %d,%d..%d,%d\n",
                l->text, x0, y0, x1, y1);
      }
      continue;
    }
    occupy(x0, y0, x1, y1);
    /*
     * ASTIR_DEBUG bit 16 reports every placement and every rejection, with
     * boxes.  A label that is not on screen gives no clue whether it was
     * rejected, never submitted, or refused by the renderer, and all three have
     * happened here.  Printing the boxes is what finally showed that a
     * station's own lines were landing on the same pixel row.
     */
    if (debug_level & 16)
    {
      fprintf(stderr, "  label PLACED:   \"%s\" box %d,%d..%d,%d\n",
              l->text, x0, y0, x1, y1);
    }

    if (l->style == LABEL_STYLE_OUTLINED)
    {
      draw_nice_string(where, l->text_style, l->x, l->y, l->text,
                       l->bgcolor, l->color, (int)strlen(l->text));
    }
    else
    {
      (void)draw_rotated_label_text(l->angle, (int)l->x, (int)l->y,
                                    (int)strlen(l->text), colors[l->color],
                                    l->text, l->font_size);
    }
    drawn++;
  }

  nlabels = 0;
  return drawn;
}
