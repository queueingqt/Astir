/*
 * Check the PMTiles reader against an archive it did not write.
 *
 * tools/make_test_pmtiles.py builds the file from the specification in Python,
 * independently of the C.  A reader tested only against its own writer agrees
 * with itself about anything, including being wrong about the format.
 *
 * Built and run by tools/pmtiles_test.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/map/pmtiles.h"

static int failures, checks;

static void ok(int cond, const char *what)
{
  checks++;
  if (!cond) { failures++; printf("  FAIL  %s\n", what); }
}

static void expect_tile(pmtiles *p, int z, unsigned x, unsigned y,
                        const char *want)
{
  unsigned char *d = NULL;
  size_t n = 0;
  char label[128];

  snprintf(label, sizeof(label), "tile %d/%u/%u is \"%s\"", z, x, y, want);
  if (!pmtiles_get(p, z, x, y, &d, &n))
  {
    checks++; failures++;
    printf("  FAIL  %s (not found)\n", label);
    return;
  }
  ok(n == strlen(want) && memcmp(d, want, n) == 0, label);
  free(d);
}

int main(int argc, char **argv)
{
  const char *path = argc > 1 ? argv[1] : "/tmp/test.pmtiles";
  pmtiles *p = pmtiles_open(path);
  unsigned char *d = NULL;
  size_t n = 0;

  ok(p != NULL, "archive opens");
  if (p == NULL) { printf("\n%d checks, %d failures\n", checks, failures); return 1; }

  ok(p->tile_type == PMTILES_TYPE_MVT, "tile type is MVT");
  ok(p->tile_compression == PMTILES_COMPRESS_GZIP, "tiles are gzipped");
  ok(p->min_zoom == 0 && p->max_zoom == 2, "zoom range 0..2");

  // Every tile in the archive, including one that exercises the Hilbert index
  // at a deeper level.
  expect_tile(p, 0, 0, 0, "tile-zero");
  expect_tile(p, 1, 0, 0, "tile-one-zero-zero");
  expect_tile(p, 1, 1, 0, "tile-one-one-zero");
  expect_tile(p, 2, 3, 3, "tile-two-three-three");

  // Absent tiles are a normal answer, not an error: a tile set is sparse.
  ok(!pmtiles_get(p, 2, 0, 0, &d, &n), "absent tile reports not found");
  ok(!pmtiles_get(p, 5, 0, 0, &d, &n), "zoom outside the archive is refused");
  pmtiles_close(p);

  ok(pmtiles_open("/nonexistent/nope.pmtiles") == NULL, "missing file rejected");
  {
    // A file that is not PMTiles at all.
    FILE *f = fopen("/tmp/notpm.bin", "wb");
    if (f) { fwrite("this is not a tile archive at all, not even close", 1, 48, f); fclose(f); }
    ok(pmtiles_open("/tmp/notpm.bin") == NULL, "non-PMTiles file rejected");
  }
  {
    // A truncated archive: right magic, nothing behind it.
    FILE *src = fopen(argc > 1 ? argv[1] : "/tmp/test.pmtiles", "rb");
    FILE *dst = fopen("/tmp/trunc.pmtiles", "wb");
    unsigned char buf[200];
    size_t got;

    if (src && dst)
    {
      got = fread(buf, 1, sizeof(buf), src);
      fwrite(buf, 1, got > 130 ? 130 : got, dst);   // header plus a stub
      fclose(src); fclose(dst);
      p = pmtiles_open("/tmp/trunc.pmtiles");
      if (p != NULL)
      {
        (void)pmtiles_get(p, 2, 3, 3, &d, &n);      // must not crash
        pmtiles_close(p);
      }
      ok(1, "truncated archive handled without crashing");
    }
  }

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
