/*
 * Making a new user's ~/.astir usable before anything tries to read it.
 *
 * Astir keeps per-user state in ~/.astir: the config file, the map index, the
 * chosen maps, logs, tracklogs and a scratch area.  None of it can be shipped,
 * because it is per-user and writable, so the first run has to build it.
 *
 * This lived in the Motif main() and was lost when the entry point moved to
 * GTK4 -- the sort of thing that goes missing when a program changes front
 * ends, because it is not GUI code but it was sitting in a GUI file.  It is in
 * the core now, where a third front end would get it for free.
 *
 * WHAT IT DELIBERATELY DOES NOT DO
 *
 * It does not symlink language.sys and help.dat into the data directory, which
 * is what main() used to do.  A link into /usr/share/astir is a link that dangles
 * the moment the data directory moves -- running a development build with
 * ASTIR_DATA_BASE set is enough -- and a dangling link reads as "the file is
 * missing", not as "the link is stale".  Those two files are resolved when they
 * are opened instead: see xa_resolve_config().
 */
#ifndef ASTIR_FIRST_RUN_H
#define ASTIR_FIRST_RUN_H

#include <stddef.h>

/*
 * Create ~/.astir and its subdirectories if they are not already there, and
 * seed the files a first run needs.  Safe to call on every startup; an
 * existing directory or file is left exactly as it is.
 *
 * Returns 1 when the tree is usable, 0 when it is not -- a read-only home
 * directory, or a plain file sitting where a directory belongs.  The caller
 * decides what to do about it; a library has no business calling exit().
 */
int xa_user_dirs_create(void);

/*
 * Find a config file that may be the user's copy or the one Astir ships.
 *
 * Looks in ~/.astir/config first so an edited copy always wins, then in the
 * installed data directory.  `name` is a bare filename such as
 * "language-English.sys".  Returns 1 and fills `out` when a readable file was
 * found; 0 when neither location has one.
 *
 * The search is why nothing needs a symlink: the fallback is evaluated at open
 * time, so moving the data directory cannot leave a stale pointer behind.
 */
int xa_resolve_config(const char *name, char *out, size_t out_size);

/*
 * Refuse to run out of another application's data directory.
 *
 * Astir is a separate program from Xastir, not a newer version of it, and the
 * two share no files.  Pointing ASTIR_DATA_BASE at /usr/share/xastir makes it
 * look like everything works -- the shapefiles are there, the symbols are there
 * and a map appears -- while every render is being styled by another program's
 * copy of the rules, so a change made here does nothing and a change made there
 * silently alters this.
 *
 * That happened, and a warning was not enough: it was ignored precisely because
 * the map still drew.  This refuses.
 *
 * Returns 1 when the data directory is Astir's own, 0 when it is not.
 */
int xa_data_base_is_ours(void);

#endif /* ASTIR_FIRST_RUN_H */
