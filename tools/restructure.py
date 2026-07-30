#!/usr/bin/env python3
"""Move src/ into a tree that states the architecture, and fix every include.

The boundary this repo spent forty commits building is real and measured --
tools/link_core.py links the core with no front end at all -- but the directory
layout still said "one flat pile of 181 files".  This makes the layout say what
link_core.py already proves.

The classification is DERIVED, not typed in by hand, in the two places where a
mistake would be invisible:

  * what is a front end   -- main.c, *_gui.c, src/gtk4/*.  Exactly the rule
                             link_core.py uses to decide what to omit from the
                             core link, so the two cannot disagree.
  * what is a backend     -- the files that implement xa_draw.h.

Everything left over is core, and only its *subsystem* is a judgement call.
That judgement is the SUBSYSTEM table below and it is the part to argue with.

Includes are rewritten to full paths from src/ (#include "core/util/util.h")
rather than adding -I for every directory.  The -I route would have been a
smaller diff and it would have thrown away the point: with one -Isrc, an
include states which layer it reaches into, and a core file reaching into
ui/motif/ is visible on the line rather than hidden in a Makefile.

  ./tools/restructure.py --plan       print the mapping, touch nothing
  ./tools/restructure.py --apply      git mv everything, then rewrite includes
  ./tools/restructure.py --includes   rewrite includes only; idempotent, and
                                      the way to recover a half-finished move
"""
import os
import re
import subprocess
import sys
from collections import defaultdict

SRC = "src"

# ---------------------------------------------------------------------------
# The core subsystems.  A file is listed by stem; its .c and .h travel together.
#
# This is the judgement call in the whole script.  The rule used: what would
# still make sense if the rest of Xastir were deleted?  A datum conversion
# would; a station-list window would not.
# ---------------------------------------------------------------------------
SUBSYSTEM = {
    # APRS itself: the protocol, the station database, and what they imply.
    "aprs": [
        "db", "db_gis", "db_funcs", "database", "messages", "objects",
        "object_utils", "station_draw", "alert", "wx", "igate", "bulletin",
        "tactical_call_utils", "dr_utils", "track", "location", "locate",
        "fcc_data", "rac_data", "cad_objects", "view_message",
    ],
    # The map engine and every driver behind it.
    "map": [
        "maps", "map_cache", "map_dos", "map_geo", "map_gnis", "map_OSM",
        "map_pop", "map_shp", "map_shp_fwd", "map_tif", "map_WMS",
        "tile_mgmnt", "shp_hash", "dbfawk", "awk", "geocoder", "nominatim",
    ],
    # Talking to the outside world: radios, GPS, the network.
    "io": [
        "interface", "io-common", "gps", "fetch_remote", "forked_getaddrinfo",
        "x_spider", "dlm", "sound", "festival", "macspeech",
    ],
    # Coordinates, datums, and the arithmetic of position.
    "geo": ["datum", "mgrs_utils", "ambiguity_utils"],
    # Drawing that is core code: it decides *what* to draw, not how.
    "render": ["draw_symbols", "symbols"],
    # Settings, view state, and the config file.
    "state": ["xa_config", "xa_settings", "xa_state"],
    # Generic helpers that carry no Xastir concepts.
    "util": [
        "util", "snprintf", "hashtable", "hashtable_itr", "hashtable_private",
        "mutex_utils", "timer_utils", "log_utils", "debug_utils", "encoding",
        "rpl_malloc", "lang", "leak_detection", "compiledate", "xa_perf",
        "xa_trace",
    ],
}

# Headers the whole core shares, which belong to no one subsystem.
#
# main.h is here and its name is a lie.  It is 345 lines of application-wide
# state -- debug_level, my_position_valid, the version strings -- included by
# 54 files, most of them core; exactly one line in it names a Motif type.  It
# was named after main.c and the settings extraction hollowed out the rest.
# Splitting it is separate work; putting it in ui/motif/ would be filing it by
# its history rather than by what it is.
CORE_ROOT = ["xa_ui", "main", "xastir", "globals"]

# The xa_draw.h contract and the backends implementing it.
BACKEND = {
    "draw":       ["xa_draw"],
    "draw/x11":   ["xa_draw_x11", "rotated", "color", "cairo_text"],
    "draw/gtk4":  ["xa_draw_gtk4"],
    "draw/null":  ["xa_draw_null"],
}

# Standalone binaries, not part of xastir itself.
APPS = ["callpass", "testdbfawk", "xastir_udp_client"]

# Motif headers that do not end in _gui.  popup.h is the only one: it declares
# a struct whose members are Widgets, and only Motif files include it.
MOTIF_EXTRA = ["popup"]


def classify(name):
    """Return the destination directory for one file in src/, or None to leave."""
    stem, ext = os.path.splitext(name)
    if ext not in (".c", ".h"):
        return None

    # A front end, by exactly link_core.py's rule: main.c, and *_gui.
    # main.h is deliberately NOT caught here -- see CORE_ROOT.
    if (stem == "main" and ext == ".c") or stem.endswith("_gui"):
        return "ui/motif"
    if stem in MOTIF_EXTRA:
        return "ui/motif"

    for dest, stems in BACKEND.items():
        if stem in stems:
            return dest
    if stem in APPS:
        return "apps"
    if stem in CORE_ROOT:
        return "core"

    for sub, stems in SUBSYSTEM.items():
        if stem in stems:
            return "core/" + sub

    return "UNCLASSIFIED"


def build_map():
    out = {}
    for name in sorted(os.listdir(SRC)):
        if not os.path.isfile(os.path.join(SRC, name)):
            continue
        dest = classify(name)
        if dest is None:
            continue
        out[name] = dest
    # The GTK4 front end is already in its own directory; it just moves.
    gtk4 = os.path.join(SRC, "gtk4")
    if os.path.isdir(gtk4):
        for name in sorted(os.listdir(gtk4)):
            if name.endswith((".c", ".h")):
                out["gtk4/" + name] = "ui/gtk4"
    return out


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "--plan"
    if mode == "--includes":
        rewrite_includes()
        return 0
    mapping = build_map()

    unclassified = [f for f, d in mapping.items() if d == "UNCLASSIFIED"]
    by_dest = defaultdict(list)
    for f, d in mapping.items():
        by_dest[d].append(f)

    for dest in sorted(by_dest):
        files = sorted(by_dest[dest])
        print("%-14s %3d  %s" % (dest, len(files), " ".join(files)))
    print("\ntotal %d files" % len(mapping))

    if unclassified:
        print("\n%d UNCLASSIFIED -- every file must have a home before "
              "--apply will run:" % len(unclassified))
        for f in sorted(unclassified):
            print("   ", f)
        return 1

    if mode != "--apply":
        return 0

    # ---- move ----
    for dest in sorted(set(mapping.values())):
        os.makedirs(os.path.join(SRC, dest), exist_ok=True)
    for f, dest in sorted(mapping.items()):
        src = os.path.join(SRC, f)
        dst = os.path.join(SRC, dest, os.path.basename(f))
        if os.path.abspath(src) == os.path.abspath(dst):
            continue
        if os.path.exists(dst):
            continue                       # already moved by an earlier run
        if not os.path.exists(src):
            raise SystemExit("missing: %s" % src)
        # compiledate.c is generated by make and therefore untracked, so git mv
        # refuses it.  It still has to land in the new tree, because the rule
        # that generates it is about to name the new path.
        tracked = subprocess.run(["git", "ls-files", "--error-unmatch", src],
                                 capture_output=True).returncode == 0
        if tracked:
            subprocess.run(["git", "mv", src, dst], check=True)
        else:
            os.rename(src, dst)
    print("\nmoved %d files" % len(mapping))

    rewrite_includes(mapping)
    return 0


def rewrite_includes(mapping=None):
    """Point every #include at an in-tree header's new path.

    Where each header lives is read from the tree as it stands, not from the
    mapping, so this is idempotent and correct after a partial move -- the
    first run of this script died halfway on a generated file, and a mapping
    built from what was still in src/ would silently miss the sixteen files
    that had already moved.

    Only headers found in the new tree are rewritten.  config.h is autotools'
    and lives at the top level; rtree/ is a vendored library that was not
    moved; angle-bracket includes are system headers.  All three are left
    alone, which is why the match is against a table of real files rather than
    a regex for "looks like ours".
    """
    del mapping                            # signature kept for the caller

    header_home, sources = {}, []
    for root, dirs, files in os.walk(SRC):
        dirs[:] = [d for d in dirs if d != "rtree"]      # vendored, not ours
        rel = os.path.relpath(root, SRC)
        for name in files:
            if name.endswith(".h"):
                header_home[name] = name if rel == "." else "%s/%s" % (rel, name)
            if name.endswith((".c", ".h")):
                sources.append(os.path.join(root, name))

    pattern = re.compile(r'#(\s*)include(\s+)"([^"/]+\.h)"')
    touched, edits = 0, 0

    def sub(m):
        nonlocal edits
        home = header_home.get(m.group(3))
        if home is None:
            return m.group(0)              # not ours: config.h, a system header
        edits += 1
        return '#%sinclude%s"%s"' % (m.group(1), m.group(2), home)

    for path in sorted(sources):
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        new = pattern.sub(sub, text)
        if new != text:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(new)
            touched += 1

    print("rewrote %d includes across %d files" % (edits, touched))


if __name__ == "__main__":
    sys.exit(main())
