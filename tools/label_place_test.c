/*
 * Check the label placer: does it reject overlaps, and does priority win?
 *
 * The placer draws through draw_rotated_label_text() and measures through
 * xa_text_width(), both of which need a backend.  Stubs here provide a
 * predictable font -- every glyph 8 wide, every line 14 tall -- so the expected
 * geometry is arithmetic rather than a property of whatever font happens to be
 * installed.  A test whose expected values depend on the host's fonts tells you
 * about the host.
 *
 * Built and run by tools/label_place_test.sh.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core/render/label_place.h"

static int failures, checks;

static void ok(int cond, const char *what)
{
  checks++;
  if (!cond) { failures++; printf("  FAIL  %s\n", what); }
}

/* ---- the stubs the placer draws and measures through ------------------- */

#define GLYPH_W 8
#define LINE_H  14

long screen_width = 800, screen_height = 600;
xa_color colors[256];

int xa_text_width(const char *text, const char *fontspec)
{
  (void)fontspec;
  return (int)strlen(text) * GLYPH_W;
}

int xa_text_height(const char *fontspec)
{
  (void)fontspec;
  return LINE_H;
}

// What actually got drawn, and where, so the test can check the outcome and
// the invariant rather than only the count.
#define DRAWN_MAX 512
static char drawn[DRAWN_MAX][96];
static int dx0[DRAWN_MAX], dy0[DRAWN_MAX], dx1[DRAWN_MAX], dy1[DRAWN_MAX];
static int ndrawn;

void draw_rotated_label_text(int rotation, int x, int y, int len,
                             int color, char *text, int fontsize)
{
  (void)rotation; (void)len; (void)color; (void)fontsize;
  if (ndrawn < DRAWN_MAX)
  {
    snprintf(drawn[ndrawn], sizeof(drawn[0]), "%s", text);
    // The same box the placer computes, so the invariant is checked against
    // what was actually reserved rather than against a guess.
    dx0[ndrawn] = x;
    dy0[ndrawn] = y - LINE_H;
    dx1[ndrawn] = x + (int)strlen(text) * GLYPH_W;
    dy1[ndrawn] = y;
    ndrawn++;
  }
}


/*
 * The invariant: nothing drawn overlaps anything else drawn.
 *
 * This is the property the whole module exists for, and counting placements
 * does not check it -- a placer that reserved the wrong rectangle would still
 * return a plausible count while drawing text on top of text.
 */
static int any_overlap(void)
{
  int i, j;

  for (i = 0; i < ndrawn; i++)
  {
    for (j = i + 1; j < ndrawn; j++)
    {
      if (dx0[i] < dx1[j] && dx1[i] > dx0[j]
          && dy0[i] < dy1[j] && dy1[i] > dy0[j])
      {
        printf("  overlap: \"%s\" (%d,%d)-(%d,%d) vs \"%s\" (%d,%d)-(%d,%d)\n",
               drawn[i], dx0[i], dy0[i], dx1[i], dy1[i],
               drawn[j], dx0[j], dy0[j], dx1[j], dy1[j]);
        return 1;
      }
    }
  }
  return 0;
}

/*
 * The outlined path's renderer.  Recorded the same way, because a label drawn
 * through either path has to obey the same non-overlap invariant.
 */
void draw_nice_string(xa_surface_id where, int style, long x, long y,
                      char *text, int bgcolor, int fgcolor, int len)
{
  (void)where; (void)style; (void)bgcolor; (void)fgcolor; (void)len;
  if (ndrawn < DRAWN_MAX)
  {
    snprintf(drawn[ndrawn], sizeof(drawn[0]), "%s", text);
    dx0[ndrawn] = (int)x;
    dy0[ndrawn] = (int)y - LINE_H;
    dx1[ndrawn] = (int)x + (int)strlen(text) * GLYPH_W;
    dy1[ndrawn] = (int)y;
    ndrawn++;
  }
}


static int was_drawn(const char *s)
{
  int i;

  for (i = 0; i < ndrawn; i++)
  {
    if (strcmp(drawn[i], s) == 0) { return 1; }
  }
  return 0;
}

int main(void)
{
  int sub, n;

  /* 1. Two labels far apart: both fit. */
  ndrawn = 0;
  label_frame_begin();
  label_submit(10, 100, -90, "Alpha", "f", 8, 1, LABEL_PRIO_MAP);
  label_submit(400, 400, -90, "Bravo", "f", 8, 1, LABEL_PRIO_MAP);
  n = label_flush(0, &sub);
  ok(sub == 2, "two submitted");
  ok(n == 2, "two placed when far apart");

  /* 2. Two labels on the same spot: only one survives. */
  ndrawn = 0;
  label_frame_begin();
  label_submit(100, 100, -90, "Charlie", "f", 8, 1, LABEL_PRIO_MAP);
  label_submit(100, 100, -90, "Delta", "f", 8, 1, LABEL_PRIO_MAP);
  n = label_flush(0, &sub);
  ok(sub == 2, "two submitted, overlapping");
  ok(n == 1, "only one placed when they overlap");

  /* 3. Priority decides which one, regardless of submission order. */
  ndrawn = 0;
  label_frame_begin();
  label_submit(100, 100, -90, "lowly", "f", 8, 1, LABEL_PRIO_MAP);
  label_submit(100, 100, -90, "vital", "f", 8, 1, LABEL_PRIO_STATION);
  n = label_flush(0, &sub);
  ok(n == 1, "one placed");
  ok(was_drawn("vital"), "the higher priority label won");
  ok(!was_drawn("lowly"), "the lower priority label was dropped");

  /* ...and the same when submitted the other way round. */
  ndrawn = 0;
  label_frame_begin();
  label_submit(100, 100, -90, "vital", "f", 8, 1, LABEL_PRIO_STATION);
  label_submit(100, 100, -90, "lowly", "f", 8, 1, LABEL_PRIO_MAP);
  n = label_flush(0, &sub);
  ok(was_drawn("vital") && !was_drawn("lowly"),
     "priority wins regardless of submission order");

  /* 4. Adjacent but not touching: both fit.  "Alpha" is 5*8 = 40 wide. */
  ndrawn = 0;
  label_frame_begin();
  label_submit(100, 100, -90, "Alpha", "f", 8, 1, LABEL_PRIO_MAP);
  label_submit(100 + 40 + 8, 100, -90, "Alpha", "f", 8, 1, LABEL_PRIO_MAP);
  n = label_flush(0, &sub);
  ok(n == 2, "labels that clear each other both fit");

  /* ...and one pixel closer than the padding allows: only one. */
  ndrawn = 0;
  label_frame_begin();
  label_submit(100, 100, -90, "Alpha", "f", 8, 1, LABEL_PRIO_MAP);
  label_submit(100 + 40, 100, -90, "Alpha", "f", 8, 1, LABEL_PRIO_MAP);
  n = label_flush(0, &sub);
  ok(n == 1, "labels closer than the padding collide");

  /* 5. Vertically separated by more than a line: both fit. */
  ndrawn = 0;
  label_frame_begin();
  label_submit(100, 100, -90, "Alpha", "f", 8, 1, LABEL_PRIO_MAP);
  label_submit(100, 100 + LINE_H + 8, -90, "Alpha", "f", 8, 1, LABEL_PRIO_MAP);
  n = label_flush(0, &sub);
  ok(n == 2, "labels a line apart both fit");

  /* 6. Off screen is skipped, not placed. */
  ndrawn = 0;
  label_frame_begin();
  label_submit(-500, 100, -90, "Offleft", "f", 8, 1, LABEL_PRIO_MAP);
  label_submit(100, 5000, -90, "Offbottom", "f", 8, 1, LABEL_PRIO_MAP);
  n = label_flush(0, &sub);
  ok(sub == 2 && n == 0, "off-screen labels are not drawn");

  /* 7. A dense field: everything drawn must be non-overlapping. */
  ndrawn = 0;
  label_frame_begin();
  {
    int i;

    for (i = 0; i < 300; i++)
    {
      char t[16];

      snprintf(t, sizeof(t), "L%d", i);
      label_submit(20 + (i * 37) % 700, 20 + (i * 53) % 500, -90, t, "f",
                   8, 1, LABEL_PRIO_MAP);
    }
  }
  n = label_flush(0, &sub);
  ok(sub == 300, "300 submitted");
  ok(n > 0 && n < 300, "a dense field places some and rejects some");
  ok(!any_overlap(), "nothing drawn overlaps anything else drawn");
  printf("  (dense field: %d of %d placed)\n", n, sub);

  /* 8. Outlined labels obey the same rules as rotated ones. */
  ndrawn = 0;
  label_frame_begin();
  label_submit_styled(100, 100, 0, "callsign", "f", 8, 15, 1,
                      LABEL_PRIO_STATION, LABEL_STYLE_OUTLINED, 0);
  label_submit_styled(100, 100, 0, "another", "f", 8, 15, 1,
                      LABEL_PRIO_STATION, LABEL_STYLE_OUTLINED, 0);
  n = label_flush(0, &sub);
  ok(n == 1, "outlined labels collide with each other too");
  ok(!any_overlap(), "outlined labels obey the non-overlap invariant");

  /* ...and a station outranks a map name in the same place. */
  ndrawn = 0;
  label_frame_begin();
  label_submit(100, 100, -90, "Placename", "f", 8, 1, LABEL_PRIO_MAP);
  label_submit_styled(100, 100, 0, "N0CALL", "f", 8, 15, 1,
                      LABEL_PRIO_STATION, LABEL_STYLE_OUTLINED, 0);
  n = label_flush(0, &sub);
  ok(was_drawn("N0CALL") && !was_drawn("Placename"),
     "a station callsign outranks a map name");

  /* 9. Empty and repeated flushes are harmless. */
  label_frame_begin();
  ok(label_flush(0, &sub) == 0 && sub == 0, "empty flush places nothing");
  ok(label_flush(0, &sub) == 0, "a second flush is a no-op");
  ok(label_submit(0, 0, 0, NULL, "f", 8, 1, 0) == 0, "NULL text refused");
  ok(label_submit(0, 0, 0, "", "f", 8, 1, 0) == 0, "empty text refused");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
