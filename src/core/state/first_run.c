/*
 * First-run setup for ~/.astir.  See first_run.h for why this is in the core
 * rather than in a front end.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "core/astir.h"
#include "core/state/first_run.h"
#include "core/state/xa_config.h"
#include "core/util/snprintf.h"

/*
 * The per-user directories.
 *
 * "config" first because everything else reports through a config file or a
 * language file, and a run that cannot write its config is not worth
 * continuing into.
 */
static const char *user_dirs[] =
{
  "config",                  /* settings, map index, chosen maps, language */
  "data",                    /* station databases and downloaded lookups */
  "logs",                    /* packet and message logs */
  "tracklogs",               /* recorded GPS tracks */
  "gps",                     /* GPS device scratch */
  "map_cache",               /* downloaded map tiles and rendered pieces */
  "tmp",                     /* working files; may be cleared between runs */
};

/*
 * The map selection a new user starts with.
 *
 * Offline vector layers only.  A first launch should draw a map on a laptop
 * with no network -- which is the situation this program is actually carried
 * into -- so the default cannot be a tile server.  Online tiles are a checkbox
 * away for anyone who wants them, whereas a blank window on first launch reads
 * as a broken install.
 *
 * These four ship with Astir.  Nothing else can be listed here: a default that
 * names a file the user has not downloaded is a default that does not work.
 * Natural Earth at 1:50m is a world basemap and no more than that; at street
 * zoom it draws the coast and nothing else, and filling that in means adding
 * local data, which is what the map chooser is for.
 */
static const char *default_maps[] =
{
  "NaturalEarth/ne_50m_admin_0_countries_lakes.shp",   /* filled land */
  "NaturalEarth/ne_50m_lakes.shp",
  "NaturalEarth/ne_50m_rivers_lake_centerlines.shp",
  "NaturalEarth/ne_50m_coastline.shp",                 /* drawn over the fill */
};


/*
 * mkdir, treating "it is already there" as success.
 *
 * A directory that exists is the normal case on every run after the first, and
 * a plain FILE in the way is a real failure -- that is the difference this
 * checks, rather than trusting EEXIST alone.
 */
static int ensure_dir(const char *path)
{
  struct stat st;

  if (mkdir(path, S_IRWXU) == 0)
  {
    return 1;
  }
  if (errno != EEXIST)
  {
    fprintf(stderr, "astir: cannot create %s: %s\n", path, strerror(errno));
    return 0;
  }
  if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
  {
    fprintf(stderr, "astir: %s exists but is not a directory\n", path);
    return 0;
  }
  return 1;
}


/*
 * The map background that goes with the default map set.
 *
 * A basemap draws land and leaves the sea to the background, so with these maps
 * selected the background IS the water and has to be water-coloured.  Written
 * into the config rather than changed in the code, because it is a preference:
 * anyone who switches to raster tiles, where the background is only what shows
 * through the gaps, should be able to set it back and have that stick.
 *
 * Only ever written next to a freshly seeded map selection, so an existing
 * installation never has its background changed underneath it.
 */
#define DEFAULT_MAP_BGCOLOR 12         /* water; see map_background_names[] */

static void seed_background_for_default_maps(void)
{
  char path[MAX_VALUE];
  FILE *f;

  get_user_base_dir("config/astir.cnf", path, sizeof(path));

  // Append: load_data_or_default() has not run yet, so anything already in the
  // file is somebody else's and must survive.
  f = fopen(path, "a");
  if (f == NULL)
  {
    return;                    // the map choice still stands; only the sea is grey
  }
  fprintf(f, "MAP_BGCOLOR:%d\n", DEFAULT_MAP_BGCOLOR);
  fclose(f);
}


// Write the default map selection, but only if the user has no selection yet.
// An empty file counts as a choice -- somebody deselected everything -- so
// only a missing file is seeded.
static void seed_default_maps(void)
{
  char path[MAX_VALUE];
  FILE *f;
  size_t i;

  get_user_base_dir("config/selected_maps.sys", path, sizeof(path));
  if (access(path, F_OK) == 0)
  {
    return;
  }

  f = fopen(path, "w");
  if (f == NULL)
  {
    fprintf(stderr, "astir: cannot write %s: %s\n", path, strerror(errno));
    return;                    // not fatal; the user starts with no maps
  }
  for (i = 0; i < sizeof(default_maps) / sizeof(default_maps[0]); i++)
  {
    fprintf(f, "%s\n", default_maps[i]);
  }
  fclose(f);

  seed_background_for_default_maps();

  fprintf(stderr, "astir: starting with the default offline map set\n");
}


int xa_user_dirs_create(void)
{
  char base[MAX_VALUE];
  char path[MAX_VALUE];
  size_t i;

  /*
   * ~/.astir itself.  get_user_base_dir("") returns it with a trailing slash,
   * which mkdir does not mind, and asking for it here also fixes the base into
   * xa_config_dir before anything else needs it.
   */
  get_user_base_dir("", base, sizeof(base));
  if (!ensure_dir(base))
  {
    return 0;
  }

  for (i = 0; i < sizeof(user_dirs) / sizeof(user_dirs[0]); i++)
  {
    get_user_base_dir((char *)user_dirs[i], path, sizeof(path));
    if (!ensure_dir(path))
    {
      // Only config is worth refusing to start over.  Losing tracklogs means
      // tracks are not recorded; losing config means nothing can be saved.
      if (strcmp(user_dirs[i], "config") == 0)
      {
        return 0;
      }
    }
  }

  seed_default_maps();
  return 1;
}


int xa_data_base_is_ours(void)
{
  const char *base = get_data_base_dir("");

  if (base == NULL)
  {
    return 1;                    // nothing to judge; let the caller carry on
  }
  if (strstr(base, "xastir") == NULL)
  {
    return 1;
  }

  fprintf(stderr,
          "astir: refusing to run from another application's data directory:\n"
          "         %s\n"
          "       Astir shares no files with Xastir.  Unset ASTIR_DATA_BASE to\n"
          "       use the installed copy, or point it at Astir's own tree --\n"
          "       tools/devdata.sh builds one for a working checkout.\n",
          base);
  return 0;
}


int xa_resolve_config(const char *name, char *out, size_t out_size)
{
  char rel[MAX_VALUE];

  if (name == NULL || out == NULL || out_size == 0)
  {
    return 0;
  }

  /*
   * The user's own copy wins, so an edited file is never overridden by an
   * upgrade of the installed one -- unless it is not really theirs.
   *
   * Astir's predecessor put symlinks here pointing into its own install, and a
   * ~/.astir seeded by copying a ~/.xastir inherits them.  They resolve, they
   * are readable, and they win, so every string in the program comes out of
   * another application's file while Astir's own copy sits unread.  A link that
   * leads out of Astir's world is not the user's preference; it is a leftover,
   * and it is skipped.
   */
  astir_snprintf(rel, sizeof(rel), "config/%s", name);
  get_user_base_dir(rel, out, out_size);
  if (access(out, R_OK) == 0)
  {
    // NULL rather than a buffer of our own: realpath() wants at least PATH_MAX
    // and will allocate exactly what it needs when asked to.
    char *real = realpath(out, NULL);
    int foreign = (real != NULL && strstr(real, "xastir") != NULL);

    if (!foreign)
    {
      free(real);
      return 1;
    }
    fprintf(stderr,
            "astir: ignoring %s: it points into another application\n"
            "         %s -> %s\n"
            "       Using Astir's own copy.  Delete that link to silence this.\n",
            name, out, real);
    free(real);
  }

  astir_snprintf(out, out_size, "%s", get_data_base_dir(rel));
  if (access(out, R_OK) == 0)
  {
    return 1;
  }

  out[0] = '\0';
  return 0;
}
